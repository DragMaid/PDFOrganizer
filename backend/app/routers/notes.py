"""Notes (comments) on a file within a group.

Concurrency policy, per the product decision, and the mirror image of the tag
policy:

* Anyone in the group may read notes and write new ones.
* **Only the author may edit or delete their own note.** The group owner is
  not exempt — ownership governs the group, not other people's words.
* Edits carry the version the client last read. If the note moved on in the
  meantime (the same author editing from a second machine), the write is
  rejected with 409 and the current note comes back in the error payload so
  the client can show what it lost rather than silently overwriting.
"""

from __future__ import annotations

from datetime import datetime, timezone

from fastapi import APIRouter, status
from sqlalchemy import select
from sqlalchemy.orm import Session

from ..deps import CurrentUser, DbSession, group_file_or_404, group_or_404
from ..errors import bad_request, conflict, forbidden, not_found
from ..models import Note, User
from ..schemas import NoteCreate, NoteOut, NoteUpdate

router = APIRouter(tags=["notes"])


def _to_out(note: Note, author: User, viewer_id: int) -> NoteOut:
    return NoteOut(
        id=note.id,
        group_id=note.group_id,
        file_id=note.file_id,
        author_id=note.author_id,
        author_name=author.display_name,
        body=note.body,
        version=note.version,
        created_at=note.created_at,
        updated_at=note.updated_at,
        editable=note.author_id == viewer_id,
    )


def _load(db: Session, note_id: int) -> tuple[Note, User]:
    row = db.execute(
        select(Note, User)
        .join(User, User.id == Note.author_id)
        .where(Note.id == note_id, Note.deleted_at.is_(None))
    ).first()
    if row is None:
        raise not_found("That note")
    return row[0], row[1]


@router.get("/groups/{group_id}/files/{file_id}/notes", response_model=list[NoteOut])
def list_notes(
    group_id: int, file_id: int, user: CurrentUser, db: DbSession
) -> list[NoteOut]:
    group_or_404(db, group_id, user)
    group_file_or_404(db, group_id, file_id)

    rows = db.execute(
        select(Note, User)
        .join(User, User.id == Note.author_id)
        .where(
            Note.group_id == group_id,
            Note.file_id == file_id,
            Note.deleted_at.is_(None),
        )
        .order_by(Note.created_at)
    ).all()
    return [_to_out(note, author, user.id) for note, author in rows]


@router.post(
    "/groups/{group_id}/files/{file_id}/notes",
    response_model=NoteOut,
    status_code=status.HTTP_201_CREATED,
)
def create_note(
    group_id: int,
    file_id: int,
    payload: NoteCreate,
    user: CurrentUser,
    db: DbSession,
) -> NoteOut:
    group_or_404(db, group_id, user)
    group_file_or_404(db, group_id, file_id)

    body = payload.body.strip()
    if not body:
        raise bad_request("A note cannot be empty.")

    note = Note(
        group_id=group_id,
        file_id=file_id,
        author_id=user.id,
        body=body,
        version=1,
    )
    db.add(note)
    db.commit()
    db.refresh(note)
    return _to_out(note, user, user.id)


@router.patch("/notes/{note_id}", response_model=NoteOut)
def update_note(
    note_id: int, payload: NoteUpdate, user: CurrentUser, db: DbSession
) -> NoteOut:
    note, author = _load(db, note_id)
    # Membership is still required to even see the note...
    group_or_404(db, note.group_id, user)
    # ...but authorship is what allows changing it.
    if note.author_id != user.id:
        raise forbidden(
            f"This note was written by {author.display_name}. Only its author "
            "can edit it."
        )

    if payload.version is not None and payload.version != note.version:
        raise conflict(
            "stale_note",
            "This note was changed somewhere else since you opened it. "
            "Reload it and reapply your edit.",
            {
                "current_version": note.version,
                "current_body": note.body,
                "updated_at": note.updated_at.isoformat(),
            },
        )

    note.body = payload.body.strip()
    note.version += 1
    note.updated_at = datetime.now(timezone.utc)
    db.commit()
    db.refresh(note)
    return _to_out(note, author, user.id)


@router.delete("/notes/{note_id}", status_code=status.HTTP_204_NO_CONTENT)
def delete_note(note_id: int, user: CurrentUser, db: DbSession) -> None:
    note, author = _load(db, note_id)
    group_or_404(db, note.group_id, user)
    if note.author_id != user.id:
        raise forbidden(
            f"This note was written by {author.display_name}. Only its author "
            "can delete it."
        )

    # Soft delete keeps the row for auditing while hiding it everywhere.
    note.deleted_at = datetime.now(timezone.utc)
    db.commit()
