"""Backblaze B2 uploads.

Credentials come from the environment and never leave this process — clients
POST bytes to us and we do the B2 conversation on their behalf.

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
        if not api_url:
            raise B2Error("Backblaze returned an unexpected authorization response.")

        return _Auth(
            api_url=api_url,
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


class _Unauthorized(Exception):
    """Internal signal to re-authorize and retry."""


def _percent_encode(name: str) -> str:
    from urllib.parse import quote

    return quote(name, safe="/")


storage = B2Storage()


def object_name_for(content_hash: str) -> str:
    """Blobs are keyed by content, so identical PDFs are stored exactly once."""
    return f"files/{content_hash[:2]}/{content_hash}.pdf"


def sha256_of(content: bytes) -> str:
    return hashlib.sha256(content).hexdigest()
