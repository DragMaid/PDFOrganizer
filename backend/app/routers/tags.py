"""Tags.

Concurrency policy, per the product decision: tag work is *never* an error
just because someone else got there first.

* Creating a tag that already exists returns the existing tag, 200.
* Assigning a tag already on the file returns 200 and leaves it be.
* Removing a tag that is already gone returns 204.

So two members tagging the same file at the same moment both succeed and end
up with the same result. Only genuine problems — no such group, not a member,
renaming onto a name that is taken — produce an error the client will show.
"""

from __future__ import annotations

from fastapi import APIRouter, BackgroundTasks, Response, status
from sqlalchemy import delete, select
from sqlalchemy.dialects.postgresql import insert as pg_insert
from sqlalchemy.orm import Session

from ..deps import CurrentUser, DbSession, group_file_or_404, group_or_404
from ..errors import bad_request, conflict, not_found
from ..models import FileTag, GroupMember, Tag
from ..schemas import TagAssign, TagCreate, TagOut, TagSet
from .ws import manager

router = APIRouter(tags=["tags"])


def _get_or_create_tag(db: Session, group_id: int, name: str, user_id: int) -> Tag:
    """Return the group's tag for ``name``, creating it if nobody has yet."""
    clean = name.strip()
    if not clean:
        raise bad_request("A tag needs a name.")

    lower = clean.lower()
    db.execute(
        pg_insert(Tag)
        .values(
            group_id=group_id, name=clean, name_lower=lower, created_by=user_id
        )
        .on_conflict_do_nothing(index_elements=["group_id", "name_lower"])
    )
    return db.execute(
        select(Tag).where(Tag.group_id == group_id, Tag.name_lower == lower)
    ).scalar_one()


# ── Vocabulary ────────────────────────────────────────────────────────────────


@router.get("/tags", response_model=list[TagOut])
def list_all_tags(user: CurrentUser, db: DbSession) -> list[Tag]:
    """Every tag across every group the caller belongs to.

    The client's tag sidebar and search filter use this union so the user sees
    one vocabulary, while storage stays partitioned per group.
    """
    return list(
        db.execute(
            select(Tag)
            .join(GroupMember, GroupMember.group_id == Tag.group_id)
            .where(GroupMember.user_id == user.id)
            .order_by(Tag.name)
        )
        .scalars()
        .all()
    )


@router.get("/groups/{group_id}/tags", response_model=list[TagOut])
def list_group_tags(group_id: int, user: CurrentUser, db: DbSession) -> list[Tag]:
    group_or_404(db, group_id, user)
    return list(
        db.execute(select(Tag).where(Tag.group_id == group_id).order_by(Tag.name))
        .scalars()
        .all()
    )


@router.post(
    "/groups/{group_id}/tags", response_model=TagOut, status_code=status.HTTP_200_OK
)
def create_tag(
    group_id: int, payload: TagCreate, user: CurrentUser, db: DbSession
) -> Tag:
    """Create a tag, or hand back the one that already exists. Never 409s."""
    group_or_404(db, group_id, user)
    tag = _get_or_create_tag(db, group_id, payload.name, user.id)
    db.commit()
    db.refresh(tag)
    return tag


@router.patch("/groups/{group_id}/tags/{tag_id}", response_model=TagOut)
def rename_tag(
    group_id: int,
    tag_id: int,
    payload: TagCreate,
    user: CurrentUser,
    db: DbSession,
    background_tasks: BackgroundTasks,
) -> Tag:
    group_or_404(db, group_id, user)
    tag = db.get(Tag, tag_id)
    if tag is None or tag.group_id != group_id:
        raise not_found("That tag")

    clean = payload.name.strip()
    lower = clean.lower()
    if lower != tag.name_lower:
        taken = db.execute(
            select(Tag).where(Tag.group_id == group_id, Tag.name_lower == lower)
        ).scalar_one_or_none()
        # Renaming is the one tag operation that *can* conflict: silently
        # merging two tags would lose assignments, so the user gets told.
        if taken is not None:
            raise conflict(
                "tag_exists", f"This group already has a tag called '{taken.name}'."
            )
    tag.name = clean
    tag.name_lower = lower
    db.commit()
    db.refresh(tag)
    background_tasks.add_task(manager.broadcast_to_group, group_id, "sync_needed", db)
    return tag


@router.delete(
    "/groups/{group_id}/tags/{tag_id}", status_code=status.HTTP_204_NO_CONTENT
)
def delete_tag(group_id: int, tag_id: int, user: CurrentUser, db: DbSession, background_tasks: BackgroundTasks) -> None:
    group_or_404(db, group_id, user)
    tag = db.get(Tag, tag_id)
    if tag is None or tag.group_id != group_id:
        # Already gone is the outcome the caller wanted.
        return
    db.delete(tag)
    db.commit()
    background_tasks.add_task(manager.broadcast_to_group, group_id, "sync_needed", db)


# ── Assignment ────────────────────────────────────────────────────────────────


@router.get(
    "/groups/{group_id}/files/{file_id}/tags", response_model=list[TagOut]
)
def list_file_tags(
    group_id: int, file_id: int, user: CurrentUser, db: DbSession
) -> list[Tag]:
    group_or_404(db, group_id, user)
    group_file_or_404(db, group_id, file_id)
    return list(
        db.execute(
            select(Tag)
            .join(FileTag, FileTag.tag_id == Tag.id)
            .where(FileTag.file_id == file_id, Tag.group_id == group_id)
            .order_by(Tag.name)
        )
        .scalars()
        .all()
    )


@router.post(
    "/groups/{group_id}/files/{file_id}/tags",
    response_model=list[TagOut],
    status_code=status.HTTP_200_OK,
)
def add_file_tag(
    group_id: int,
    file_id: int,
    payload: TagAssign,
    user: CurrentUser,
    db: DbSession,
    background_tasks: BackgroundTasks,
) -> list[Tag]:
    """Add one tag to a file. Idempotent — a tag another member already added
    is left exactly as it is and the call still succeeds."""
    group_or_404(db, group_id, user)
    group_file_or_404(db, group_id, file_id)

    tag = _get_or_create_tag(db, group_id, payload.name, user.id)
    db.execute(
        pg_insert(FileTag)
        .values(file_id=file_id, tag_id=tag.id, added_by=user.id)
        .on_conflict_do_nothing(index_elements=["file_id", "tag_id"])
    )
    db.commit()
    background_tasks.add_task(manager.broadcast_to_group, group_id, "sync_needed", db)
    return list_file_tags(group_id, file_id, user, db)


@router.put(
    "/groups/{group_id}/files/{file_id}/tags", response_model=list[TagOut]
)
def set_file_tags(
    group_id: int,
    file_id: int,
    payload: TagSet,
    user: CurrentUser,
    db: DbSession,
    background_tasks: BackgroundTasks,
) -> list[Tag]:
    """Replace this file's tags within the group.

    Used by the client's "Edit Tags" dialog. Additions are idempotent; removals
    only touch tags in *this* group, so another group's tags on the same file
    are untouched.
    """
    group_or_404(db, group_id, user)
    group_file_or_404(db, group_id, file_id)

    wanted: list[Tag] = []
    seen: set[str] = set()
    for raw in payload.names:
        clean = raw.strip()
        if not clean or clean.lower() in seen:
            continue
        seen.add(clean.lower())
        wanted.append(_get_or_create_tag(db, group_id, clean, user.id))

    keep_ids = [tag.id for tag in wanted]
    stale = delete(FileTag).where(
        FileTag.file_id == file_id,
        FileTag.tag_id.in_(select(Tag.id).where(Tag.group_id == group_id)),
    )
    if keep_ids:
        stale = stale.where(FileTag.tag_id.notin_(keep_ids))
    db.execute(stale)

    for tag in wanted:
        db.execute(
            pg_insert(FileTag)
            .values(file_id=file_id, tag_id=tag.id, added_by=user.id)
            .on_conflict_do_nothing(index_elements=["file_id", "tag_id"])
        )
    db.commit()
    background_tasks.add_task(manager.broadcast_to_group, group_id, "sync_needed", db)
    return list_file_tags(group_id, file_id, user, db)


@router.delete(
    "/groups/{group_id}/files/{file_id}/tags/{tag_id}",
    status_code=status.HTTP_204_NO_CONTENT,
)
def remove_file_tag(
    group_id: int,
    file_id: int,
    tag_id: int,
    user: CurrentUser,
    db: DbSession,
    background_tasks: BackgroundTasks,
) -> Response:
    """Remove a tag from a file. Also idempotent — if another member removed it
    first, the caller still gets a success."""
    group_or_404(db, group_id, user)
    group_file_or_404(db, group_id, file_id)

    tag = db.get(Tag, tag_id)
    if tag is None or tag.group_id != group_id:
        return
    db.execute(
        delete(FileTag).where(FileTag.file_id == file_id, FileTag.tag_id == tag_id)
    )
    db.commit()
