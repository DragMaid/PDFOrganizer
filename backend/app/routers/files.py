"""Files within a group, plus the Backblaze sync endpoints.

Registering a file is deliberately idempotent at two levels: the content hash
may already exist globally (someone else has the same PDF) and the group link
may already exist (a second member added it first). Both are treated as
success, which is what keeps two members scanning the same shared drive from
fighting each other.
"""

from __future__ import annotations

from datetime import datetime, timezone

from fastapi import APIRouter, File as UploadField, UploadFile, status
from sqlalchemy import delete, func, select
from sqlalchemy.dialects.postgresql import insert as pg_insert
from sqlalchemy.orm import Session

from ..deps import CurrentUser, DbSession, group_file_or_404, group_or_404
from ..errors import bad_request
from ..models import File, FileTag, GroupFile, Note, Tag
from ..schemas import FileOut, FileRegister, SyncStatusOut, UploadResult
from ..storage import object_name_for, sha256_of, storage

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


@router.delete("/{file_id}", status_code=status.HTTP_204_NO_CONTENT)
def remove_file(
    group_id: int, file_id: int, user: CurrentUser, db: DbSession
) -> None:
    """Detach a file from this group.

    Any member may do this. The file row and its blob survive because other
    groups may still reference the same content; only this group's notes and
    tag assignments go away.
    """
    group_or_404(db, group_id, user)
    group_file_or_404(db, group_id, file_id)

    db.execute(delete(Note).where(Note.group_id == group_id, Note.file_id == file_id))
    db.execute(
        delete(FileTag).where(
            FileTag.file_id == file_id,
            FileTag.tag_id.in_(select(Tag.id).where(Tag.group_id == group_id)),
        )
    )
    db.execute(
        delete(GroupFile).where(
            GroupFile.group_id == group_id, GroupFile.file_id == file_id
        )
    )
    db.commit()


# ── Backblaze sync ────────────────────────────────────────────────────────────


sync_router = APIRouter(prefix="/groups/{group_id}", tags=["sync"])


@sync_router.get("/sync-status", response_model=SyncStatusOut)
def sync_status(group_id: int, user: CurrentUser, db: DbSession) -> SyncStatusOut:
    """Which of this group's files still need their bytes uploaded."""
    group_or_404(db, group_id, user)

    rows = db.execute(
        select(File, GroupFile)
        .join(GroupFile, GroupFile.file_id == File.id)
        .where(GroupFile.group_id == group_id)
        .order_by(GroupFile.display_name)
    ).all()
    tags = _tags_for(db, group_id, [file.id for file, _ in rows])
    pending = [
        _to_out(file, link, tags.get(file.id, []))
        for file, link in rows
        if file.b2_file_id is None
    ]
    return SyncStatusOut(
        group_id=group_id,
        total_files=len(rows),
        uploaded_files=len(rows) - len(pending),
        pending=pending,
    )


@sync_router.post("/files/{file_id}/upload", response_model=UploadResult)
def upload_file(
    group_id: int,
    file_id: int,
    user: CurrentUser,
    db: DbSession,
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

    return UploadResult(
        file_id=file.id,
        uploaded=True,
        already_present=False,
        b2_file_id=b2_file_id,
        message="Uploaded.",
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
