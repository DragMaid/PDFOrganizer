"""Backblaze B2 uploads and downloads.

Credentials come from the environment and never leave this process — clients
POST bytes to us and we do the B2 conversation on their behalf. Downloads work
the same way round: the server fetches the blob and streams it back, so a client
never holds a B2 token or a public object URL.

Only the single-shot upload path is implemented; anything above
``max_upload_bytes`` is rejected with a clear message rather than silently
truncated. B2's large-file (multipart) API would be the next step if you start
storing PDFs bigger than a couple hundred megabytes.
"""

from __future__ import annotations

import hashlib
import logging
import threading
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone

import httpx

from .config import get_settings
from .errors import ApiError, bad_request

log = logging.getLogger(__name__)

# B2 auth tokens last 24h; refresh well before that.
_AUTH_TTL = timedelta(hours=12)


@dataclass
class _Auth:
    api_url: str
    download_url: str
    authorization_token: str
    obtained_at: datetime

    @property
    def stale(self) -> bool:
        return datetime.now(timezone.utc) - self.obtained_at > _AUTH_TTL


class B2Error(ApiError):
    def __init__(self, message: str) -> None:
        super().__init__(502, "storage_error", message)


class B2Storage:
    def __init__(self) -> None:
        self._auth: _Auth | None = None
        self._lock = threading.Lock()

    # ── Auth ──────────────────────────────────────────────────────────────────

    def _authorize(self) -> _Auth:
        settings = get_settings()
        try:
            response = httpx.get(
                f"{settings.b2_api_url}/b2api/v3/b2_authorize_account",
                auth=(settings.b2_key_id, settings.b2_application_key),
                timeout=30.0,
            )
        except httpx.HTTPError as exc:
            raise B2Error(f"Could not reach Backblaze B2: {exc}") from exc

        if response.status_code != 200:
            raise B2Error(
                "Backblaze rejected the server's credentials. Check "
                "PDFORG_B2_KEY_ID and PDFORG_B2_APPLICATION_KEY."
            )

        body = response.json()
        storage = body.get("apiInfo", {}).get("storageApi", {})
        api_url = storage.get("apiUrl") or body.get("apiUrl")
        download_url = storage.get("downloadUrl") or body.get("downloadUrl")
        if not api_url or not download_url:
            raise B2Error("Backblaze returned an unexpected authorization response.")

        return _Auth(
            api_url=api_url,
            download_url=download_url,
            authorization_token=body["authorizationToken"],
            obtained_at=datetime.now(timezone.utc),
        )

    def _current_auth(self, force: bool = False) -> _Auth:
        with self._lock:
            if force or self._auth is None or self._auth.stale:
                self._auth = self._authorize()
            return self._auth

    # ── Upload ────────────────────────────────────────────────────────────────

    def _upload_url(self, auth: _Auth) -> tuple[str, str]:
        settings = get_settings()
        try:
            response = httpx.post(
                f"{auth.api_url}/b2api/v3/b2_get_upload_url",
                headers={"Authorization": auth.authorization_token},
                json={"bucketId": settings.b2_bucket_id},
                timeout=30.0,
            )
        except httpx.HTTPError as exc:
            raise B2Error(f"Could not reach Backblaze B2: {exc}") from exc

        if response.status_code == 401:
            raise _Unauthorized()
        if response.status_code != 200:
            raise B2Error(
                "Backblaze would not issue an upload URL. Check that "
                "PDFORG_B2_BUCKET_ID is correct and the key can write to it."
            )
        body = response.json()
        return body["uploadUrl"], body["authorizationToken"]

    def upload(self, content: bytes, object_name: str) -> str:
        """Upload ``content`` and return the B2 file id."""
        settings = get_settings()
        if not settings.b2_configured:
            raise ApiError(
                503,
                "storage_unconfigured",
                "File storage is not configured on the server. Ask an "
                "administrator to set the PDFORG_B2_* environment variables.",
            )
        if len(content) > settings.max_upload_bytes:
            raise bad_request(
                f"That file is larger than the {settings.max_upload_bytes // (1024 * 1024)} MB "
                "upload limit."
            )

        sha1 = hashlib.sha1(content).hexdigest()
        headers_name = _percent_encode(object_name)

        # B2 documents 401/503 on an upload URL as "get a new URL and retry".
        for attempt in range(2):
            auth = self._current_auth(force=attempt > 0)
            try:
                upload_url, upload_token = self._upload_url(auth)
            except _Unauthorized:
                continue

            try:
                response = httpx.post(
                    upload_url,
                    content=content,
                    headers={
                        "Authorization": upload_token,
                        "X-Bz-File-Name": headers_name,
                        "Content-Type": "application/pdf",
                        "Content-Length": str(len(content)),
                        "X-Bz-Content-Sha1": sha1,
                    },
                    timeout=300.0,
                )
            except httpx.HTTPError as exc:
                raise B2Error(f"Upload to Backblaze failed: {exc}") from exc

            if response.status_code in (401, 503) and attempt == 0:
                log.warning("B2 upload URL expired (%s); retrying", response.status_code)
                continue
            if response.status_code != 200:
                raise B2Error(
                    f"Backblaze rejected the upload ({response.status_code}). "
                    "The file was not stored."
                )
            return str(response.json()["fileId"])

        raise B2Error("Backblaze kept rejecting the upload session. Try again.")

    # ── Download ──────────────────────────────────────────────────────────────

    def download(self, b2_file_id: str) -> bytes:
        """Fetch a stored blob by its B2 file id.

        By id rather than by name so a blob stays reachable even if the object
        naming scheme changes later; the id is what the ``files`` row records.
        """
        settings = get_settings()
        if not settings.b2_configured:
            raise ApiError(
                503,
                "storage_unconfigured",
                "File storage is not configured on the server. Ask an "
                "administrator to set the PDFORG_B2_* environment variables.",
            )

        # As with uploads, a 401 here means the account token aged out rather
        # than that anything is wrong with the request.
        for attempt in range(2):
            auth = self._current_auth(force=attempt > 0)
            try:
                response = httpx.get(
                    f"{auth.download_url}/b2api/v3/b2_download_file_by_id",
                    params={"fileId": b2_file_id},
                    headers={"Authorization": auth.authorization_token},
                    timeout=300.0,
                )
            except httpx.HTTPError as exc:
                raise B2Error(f"Download from Backblaze failed: {exc}") from exc

            if response.status_code == 401 and attempt == 0:
                log.warning("B2 download token expired; re-authorizing")
                continue
            if response.status_code == 404:
                raise ApiError(
                    502,
                    "storage_missing",
                    "The stored copy of that file is gone from Backblaze. "
                    "Someone who still holds it locally needs to sync it again.",
                )
            if response.status_code != 200:
                raise B2Error(
                    f"Backblaze refused the download ({response.status_code})."
                )
            return response.content

        raise B2Error("Backblaze kept rejecting the download. Try again.")


    # ── Delete ────────────────────────────────────────────────────────────────

    def delete(self, b2_file_id: str, b2_file_name: str) -> None:
        """Permanently remove one stored blob.

        Both the id and the name are required by B2, which versions objects by
        name. Callers must be sure nothing else still points at the blob: it is
        deduplicated by content hash, so one delete can strip the bytes from
        every group holding that PDF.

        Idempotent — a blob B2 no longer has is treated as already deleted, so
        retrying a half-finished removal converges instead of failing.
        """
        settings = get_settings()
        if not settings.b2_configured:
            raise ApiError(
                503,
                "storage_unconfigured",
                "File storage is not configured on the server. Ask an "
                "administrator to set the PDFORG_B2_* environment variables.",
            )

        for attempt in range(2):
            auth = self._current_auth(force=attempt > 0)
            try:
                response = httpx.post(
                    f"{auth.api_url}/b2api/v3/b2_delete_file_version",
                    headers={"Authorization": auth.authorization_token},
                    json={"fileId": b2_file_id, "fileName": b2_file_name},
                    timeout=60.0,
                )
            except httpx.HTTPError as exc:
                raise B2Error(f"Deleting from Backblaze failed: {exc}") from exc

            if response.status_code == 401 and attempt == 0:
                log.warning("B2 delete token expired; re-authorizing")
                continue
            if response.status_code == 200:
                return
            if _is_already_gone(response):
                log.info("B2 blob %s was already gone", b2_file_id)
                return
            raise B2Error(
                f"Backblaze refused to delete the file ({response.status_code}). "
                "Nothing was removed from storage."
            )

        raise B2Error("Backblaze kept rejecting the delete. Try again.")


class _Unauthorized(Exception):
    """Internal signal to re-authorize and retry."""


def _is_already_gone(response: httpx.Response) -> bool:
    """True when B2 is saying the object is not there, which is what we wanted."""
    if response.status_code not in (400, 404):
        return False
    try:
        code = response.json().get("code", "")
    except ValueError:
        return False
    return code in ("file_not_present", "no_such_file", "not_found")


def _percent_encode(name: str) -> str:
    from urllib.parse import quote

    return quote(name, safe="/")


storage = B2Storage()


def object_name_for(content_hash: str) -> str:
    """Blobs are keyed by content, so identical PDFs are stored exactly once."""
    return f"files/{content_hash[:2]}/{content_hash}.pdf"


def sha256_of(content: bytes) -> str:
    return hashlib.sha256(content).hexdigest()
