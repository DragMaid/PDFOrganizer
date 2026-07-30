"""Files within a group, plus the Backblaze sync endpoints.

Registering a file is deliberately idempotent at two levels: the content hash
may already exist globally (someone else has the same PDF) and the group link
may already exist (a second member added it first). Both are treated as
success, which is what keeps two members scanning the same shared drive from
fighting each other.

Sync runs in both directions. ``upload`` pushes bytes a member holds locally;
``content`` pulls them back down, which is how someone who joins a group with
its share code ends up with the actual PDFs and not just their names — and how
a member who deleted a PDF from their disk gets it back.

Removal is deliberately two-tiered. Detaching a file from a group is an
everyday act any member may perform and leaves the stored bytes alone.
Destroying the stored copy (``?purge=true``) is owner-only, irreversible, and
refuses to strip a blob another group still shares.
"""

from __future__ import annotations

from datetime import datetime, timezone
from pathlib import PurePosixPath
from urllib.parse import quote

from fastapi import APIRouter, File as UploadField, Response, UploadFile, status, BackgroundTasks
from sqlalchemy import delete, func, select
from sqlalchemy.dialects.postgresql import insert as pg_insert
from sqlalchemy.orm import Session

from ..deps import CurrentUser, DbSession, group_file_or_404, group_or_404
from ..errors import ApiError, bad_request, conflict, forbidden
from ..models import ROLE_OWNER, File, FileTag, GroupFile, Note, Tag
from ..schemas import (
    FileOut,
    FileRegister,
    FileRemoveResult,
    SyncStatusOut,
    UploadResult,
)
from ..storage import object_name_for, sha256_of, storage
from .ws import manager

router = APIRouter(prefix="/groups/{group_id}/files", tags=["files"])


def _tags_for(db: Session, group_id: int, file_ids: list[int]) -> dict[int, list[str]]:
    if not file_ids:
        return {}
    rows = db.execute(
        select(FileTag.file_id, Tag.name)
        .join(Tag, Tag.id == FileTag.tag_id)
        .where(FileTag.file_id.in_(file_ids), Tag.group_id == group_id)
        .order_by(Tag.name)
    ).all()
    out: dict[int, list[str]] = {}
    for file_id, name in rows:
        out.setdefault(file_id, []).append(name)
    return out


def _to_out(file: File, link: GroupFile, tags: list[str]) -> FileOut:
    return FileOut(
        id=file.id,
        content_hash=file.content_hash,
        file_name=link.display_name,
        file_size_bytes=file.file_size_bytes,
        page_count=file.page_count,
        added_at=link.added_at,
        added_by=link.added_by,
        uploaded=file.b2_file_id is not None,
        uploaded_at=file.uploaded_at,
        tags=tags,
    )


@router.get("", response_model=list[FileOut])
def list_files(group_id: int, user: CurrentUser, db: DbSession) -> list[FileOut]:
    group_or_404(db, group_id, user)
    rows = db.execute(
        select(File, GroupFile)
        .join(GroupFile, GroupFile.file_id == File.id)
        .where(GroupFile.group_id == group_id)
        .order_by(GroupFile.display_name)
    ).all()
    tags = _tags_for(db, group_id, [file.id for file, _ in rows])
    return [_to_out(file, link, tags.get(file.id, [])) for file, link in rows]


@router.post("", response_model=FileOut, status_code=status.HTTP_200_OK)
def register_file(
    group_id: int, payload: FileRegister, user: CurrentUser, db: DbSession
) -> FileOut:
    group_or_404(db, group_id, user)

    # Claim the content hash. ON CONFLICT DO NOTHING plus a follow-up SELECT is
    # race-free: whichever transaction loses simply reads the winner's row.
    db.execute(
        pg_insert(File)
        .values(
            content_hash=payload.content_hash,
            file_name=payload.file_name,
            file_size_bytes=payload.file_size_bytes,
            page_count=payload.page_count,
        )
        .on_conflict_do_nothing(index_elements=["content_hash"])
    )
    file = db.execute(
        select(File).where(File.content_hash == payload.content_hash)
    ).scalar_one()

    # Fill in page_count if this caller knows it and the original registrant
    # did not. Never overwrite a known value with zero.
    if payload.page_count and not file.page_count:
        file.page_count = payload.page_count

    db.execute(
        pg_insert(GroupFile)
        .values(
            group_id=group_id,
            file_id=file.id,
            display_name=payload.file_name,
            added_by=user.id,
        )
        .on_conflict_do_nothing(index_elements=["group_id", "file_id"])
    )
    db.commit()

    link = db.get(GroupFile, {"group_id": group_id, "file_id": file.id})
    assert link is not None
    db.refresh(file)
    tags = _tags_for(db, group_id, [file.id])
    return _to_out(file, link, tags.get(file.id, []))


@router.get("/{file_id}", response_model=FileOut)
def get_file(
    group_id: int, file_id: int, user: CurrentUser, db: DbSession
) -> FileOut:
    group_or_404(db, group_id, user)
    file, link = group_file_or_404(db, group_id, file_id)
    tags = _tags_for(db, group_id, [file.id])
    return _to_out(file, link, tags.get(file.id, []))


@router.delete("/{file_id}", response_model=FileRemoveResult)
def remove_file(
    group_id: int,
    file_id: int,
    user: CurrentUser,
    db: DbSession,
    background_tasks: BackgroundTasks,
    purge: bool = False,
) -> FileRemoveResult:
    """Detach a file from this group, and optionally destroy the stored copy.

    Any member may detach. This group's notes and tag assignments go away; by
    default the file row and its blob survive, because other groups may still
    reference the same content.

    ``purge=true`` additionally deletes the bytes from Backblaze and drops the
    file record. That is irreversible and affects everyone, so it is restricted
    to the group's owner — and it is skipped when another group still links the
    same content hash, since blobs are deduplicated and the delete would strip
    the file out from under that group too. The response says which of the two
    actually happened.

    Deleting a PDF from a member's disk never reaches this endpoint; only an
    explicit removal in the app does.
    """
    _, member = group_or_404(db, group_id, user)
    file, _link = group_file_or_404(db, group_id, file_id)

    if purge and member.role != ROLE_OWNER:
        raise forbidden(
            "Only the group's owner can delete a file's stored copy. You can "
            "remove it from the group without deleting the stored copy."
        )

    # Asked before anything is detached, so a failure to reach B2 below leaves
    # the database exactly as it was.
    still_referenced = (
        db.execute(
            select(GroupFile.file_id)
            .where(GroupFile.file_id == file_id, GroupFile.group_id != group_id)
            .limit(1)
        ).first()
        is not None
    )

    purged = False
    if purge and not still_referenced and file.b2_file_id is not None:
        # B2 first: if it refuses, nothing has been detached yet and the owner
        # can retry against an unchanged group.
        storage.delete(
            file.b2_file_id, file.b2_file_name or object_name_for(file.content_hash)
        )
        purged = True

    db.execute(delete(Note).where(Note.group_id == group_id, Note.file_id == file_id))
    db.execute(
        delete(FileTag).where(
            FileTag.file_id == file_id,
            FileTag.tag_id.in_(select(Tag.id).where(Tag.group_id == group_id)),
        )
    )
    # Core DELETE rather than db.delete(link): these run in the order written,
    # so the link is gone before the file row below takes its cascade with it.
    db.execute(
        delete(GroupFile).where(
            GroupFile.group_id == group_id, GroupFile.file_id == file_id
        )
    )

    if purge and not still_referenced:
        # Nothing points at the content any more, so the row would only be a
        # tombstone claiming an upload that no longer exists. Registering the
        # same PDF again later starts clean.
        db.delete(file)

    db.commit()
    background_tasks.add_task(manager.broadcast_to_group, group_id, "sync_needed", db)

    if not purge:
        message = "Removed from the group. The stored copy was kept."
    elif still_referenced:
        message = (
            "Removed from the group, but the stored copy was kept: another "
            "group holds the same file and shares that copy."
        )
    elif purged:
        message = "Removed from the group and deleted from storage."
    else:
        message = "Removed from the group. It had never been uploaded."

    return FileRemoveResult(
        file_id=file_id,
        detached=True,
        purged=purged,
        still_referenced=purge and still_referenced,
        message=message,
    )


# ── Backblaze sync ────────────────────────────────────────────────────────────


sync_router = APIRouter(prefix="/groups/{group_id}", tags=["sync"])


@sync_router.get("/sync-status", response_model=SyncStatusOut)
def sync_status(group_id: int, user: CurrentUser, db: DbSession) -> SyncStatusOut:
    """What this group's files look like from the server's side.

    ``pending`` is the upload side: files registered here whose bytes never
    arrived. The download side cannot be answered here — only the client knows
    what is on its own disk — so ``files`` carries the whole list, hashes
    included, and the client subtracts what it already holds.
    """
    group_or_404(db, group_id, user)

    rows = db.execute(
        select(File, GroupFile)
        .join(GroupFile, GroupFile.file_id == File.id)
        .where(GroupFile.group_id == group_id)
        .order_by(GroupFile.display_name)
    ).all()
    tags = _tags_for(db, group_id, [file.id for file, _ in rows])
    files = [_to_out(file, link, tags.get(file.id, [])) for file, link in rows]
    pending = [out for out in files if not out.uploaded]
    return SyncStatusOut(
        group_id=group_id,
        total_files=len(rows),
        uploaded_files=len(rows) - len(pending),
        pending=pending,
        files=files,
    )


@sync_router.post("/files/{file_id}/upload", response_model=UploadResult)
def upload_file(
    group_id: int,
    file_id: int,
    user: CurrentUser,
    db: DbSession,
    background_tasks: BackgroundTasks,
    content: UploadFile = UploadField(...),
) -> UploadResult:
    """Push one file's bytes to Backblaze.

    The client sends the bytes; B2 credentials stay on the server. Because
    blobs are keyed by content hash, a file already stored by another member —
    or by the same content in a different group — is reported back as
    ``already_present`` without a second upload.
    """
    group_or_404(db, group_id, user)
    file, _ = group_file_or_404(db, group_id, file_id)

    if file.b2_file_id is not None:
        return UploadResult(
            file_id=file.id,
            uploaded=False,
            already_present=True,
            b2_file_id=file.b2_file_id,
            message="Already stored.",
        )

    payload = content.file.read()
    if not payload:
        raise bad_request("The uploaded file was empty.")

    digest = sha256_of(payload)
    if digest != file.content_hash:
        raise bad_request(
            "The uploaded bytes do not match the registered file. The file "
            "changed on disk — rescan the folder and try again."
        )

    object_name = object_name_for(digest)
    b2_file_id = storage.upload(payload, object_name)

    # Another member may have uploaded the same content while we were talking
    # to B2. Last writer wins on the pointer; the duplicate blob is harmless
    # since both have identical content under the same object name.
    file.b2_file_id = b2_file_id
    file.b2_file_name = object_name
    file.uploaded_at = datetime.now(timezone.utc)
    file.uploaded_by = user.id
    db.commit()
    background_tasks.add_task(manager.broadcast_to_group, group_id, "sync_needed", db)

    return UploadResult(
        file_id=file.id,
        uploaded=True,
        already_present=False,
        b2_file_id=b2_file_id,
        message="Uploaded.",
    )


@sync_router.get(
    "/files/{file_id}/content",
    response_class=Response,
    responses={200: {"content": {"application/pdf": {}}}},
)
def download_file(
    group_id: int, file_id: int, user: CurrentUser, db: DbSession
) -> Response:
    """Stream one file's bytes back to a member.

    The mirror of :func:`upload_file`, and the reason joining a group can
    reproduce its folder on a machine that never held the PDFs. The bytes come
    through this process rather than a B2 URL, so membership is checked on every
    single fetch and no storage credential reaches a client.
    """
    group_or_404(db, group_id, user)
    file, link = group_file_or_404(db, group_id, file_id)

    if file.b2_file_id is None:
        raise conflict(
            "not_uploaded",
            f"'{link.display_name}' is registered in this group but its "
            "contents were never uploaded. A member who holds the file needs to "
            "sync it first.",
        )

    payload = storage.download(file.b2_file_id)

    # A mismatch means the stored blob is not what this row claims to be.
    # Handing it over under the wrong name would be worse than failing.
    if sha256_of(payload) != file.content_hash:
        raise ApiError(
            status.HTTP_502_BAD_GATEWAY,
            "storage_corrupt",
            f"The stored copy of '{link.display_name}' does not match its "
            "recorded checksum, so it was not served.",
        )

    return Response(
        content=payload,
        media_type="application/pdf",
        headers={
            "Content-Disposition": _content_disposition(link.display_name),
            "X-Content-Sha256": file.content_hash,
        },
    )


def _content_disposition(display_name: str) -> str:
    """``attachment`` with both an ASCII fallback and the real UTF-8 name."""
    base = PurePosixPath(display_name.replace("\\", "/")).name or "document.pdf"
    ascii_fallback = base.encode("ascii", "replace").decode("ascii").replace('"', "_")
    return (
        f'attachment; filename="{ascii_fallback}"; '
        f"filename*=UTF-8''{quote(base, safe='')}"
    )


@sync_router.get("/stats")
def group_stats(group_id: int, user: CurrentUser, db: DbSession) -> dict[str, int]:
    group_or_404(db, group_id, user)
    total_bytes = db.execute(
        select(func.coalesce(func.sum(File.file_size_bytes), 0))
        .select_from(GroupFile)
        .join(File, File.id == GroupFile.file_id)
        .where(GroupFile.group_id == group_id)
    ).scalar_one()
    note_count = db.execute(
        select(func.count()).select_from(Note).where(Note.group_id == group_id)
    ).scalar_one()
    return {"total_bytes": int(total_bytes), "note_count": int(note_count)}
