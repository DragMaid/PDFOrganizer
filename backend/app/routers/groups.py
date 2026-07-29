"""Groups and membership.

Owner-only: rename, delete, add member, remove member.
Any member: read the group, and everything under files/tags/notes.
"""

from __future__ import annotations

from fastapi import APIRouter, status
from sqlalchemy import delete, func, select
from sqlalchemy.dialects.postgresql import insert as pg_insert
from sqlalchemy.orm import Session

from ..deps import CurrentUser, DbSession, group_or_404, ownership_or_403
from ..errors import ApiError, bad_request, conflict, forbidden, not_found
from ..models import (
    ROLE_MEMBER,
    ROLE_OWNER,
    Group,
    GroupFile,
    GroupMember,
    User,
)
from ..schemas import GroupCreate, GroupOut, GroupUpdate, MemberAdd, MemberOut

router = APIRouter(prefix="/groups", tags=["groups"])


def _to_out(db: Session, group: Group, role: str) -> GroupOut:
    member_count = db.execute(
        select(func.count())
        .select_from(GroupMember)
        .where(GroupMember.group_id == group.id)
    ).scalar_one()
    file_count = db.execute(
        select(func.count())
        .select_from(GroupFile)
        .where(GroupFile.group_id == group.id)
    ).scalar_one()
    return GroupOut(
        id=group.id,
        name=group.name,
        owner_id=group.owner_id,
        is_personal=group.is_personal,
        created_at=group.created_at,
        my_role=role,
        member_count=member_count,
        file_count=file_count,
    )


@router.get("", response_model=list[GroupOut])
def list_groups(user: CurrentUser, db: DbSession) -> list[GroupOut]:
    rows = db.execute(
        select(Group, GroupMember.role)
        .join(GroupMember, GroupMember.group_id == Group.id)
        .where(GroupMember.user_id == user.id)
        .order_by(Group.is_personal.desc(), Group.name)
    ).all()
    return [_to_out(db, group, role) for group, role in rows]


@router.post("", response_model=GroupOut, status_code=status.HTTP_201_CREATED)
def create_group(payload: GroupCreate, user: CurrentUser, db: DbSession) -> GroupOut:
    group = Group(name=payload.name.strip(), owner_id=user.id, is_personal=False)
    db.add(group)
    db.flush()
    db.add(GroupMember(group_id=group.id, user_id=user.id, role=ROLE_OWNER))
    db.commit()
    db.refresh(group)
    return _to_out(db, group, ROLE_OWNER)


@router.get("/{group_id}", response_model=GroupOut)
def get_group(group_id: int, user: CurrentUser, db: DbSession) -> GroupOut:
    group, member = group_or_404(db, group_id, user)
    return _to_out(db, group, member.role)


@router.patch("/{group_id}", response_model=GroupOut)
def rename_group(
    group_id: int, payload: GroupUpdate, user: CurrentUser, db: DbSession
) -> GroupOut:
    ownership_or_403(db, group_id, user)
    group = db.get(Group, group_id)
    if group is None:
        raise not_found("That group")
    if group.is_personal:
        raise bad_request("Your personal group cannot be renamed.")

    group.name = payload.name.strip()
    db.commit()
    db.refresh(group)
    return _to_out(db, group, ROLE_OWNER)


@router.delete("/{group_id}", status_code=status.HTTP_204_NO_CONTENT)
def delete_group(group_id: int, user: CurrentUser, db: DbSession) -> None:
    ownership_or_403(db, group_id, user)
    group = db.get(Group, group_id)
    if group is None:
        raise not_found("That group")
    if group.is_personal:
        raise bad_request("Your personal group cannot be deleted.")

    # Cascades remove memberships, group_files, tags and notes for this group.
    # The underlying files rows survive because other groups may reference them.
    db.delete(group)
    db.commit()


# ── Membership ────────────────────────────────────────────────────────────────


@router.get("/{group_id}/members", response_model=list[MemberOut])
def list_members(group_id: int, user: CurrentUser, db: DbSession) -> list[MemberOut]:
    group_or_404(db, group_id, user)
    rows = db.execute(
        select(GroupMember, User)
        .join(User, User.id == GroupMember.user_id)
        .where(GroupMember.group_id == group_id)
        .order_by(GroupMember.role, User.display_name)
    ).all()
    return [
        MemberOut(
            user_id=member.user_id,
            email=account.email,
            display_name=account.display_name,
            role=member.role,
            joined_at=member.joined_at,
        )
        for member, account in rows
    ]


@router.post(
    "/{group_id}/members", response_model=MemberOut, status_code=status.HTTP_200_OK
)
def add_member(
    group_id: int, payload: MemberAdd, user: CurrentUser, db: DbSession
) -> MemberOut:
    ownership_or_403(db, group_id, user)
    group = db.get(Group, group_id)
    if group is None:
        raise not_found("That group")
    if group.is_personal:
        raise bad_request("Your personal group cannot have other members.")

    email = payload.email.strip().lower()
    invitee = db.execute(
        select(User).where(func.lower(User.email) == email)
    ).scalar_one_or_none()
    if invitee is None:
        raise ApiError(
            status.HTTP_404_NOT_FOUND,
            "user_not_found",
            f"No account exists for {email}. They need to sign up first.",
        )

    # Idempotent: two owners inviting the same person concurrently both succeed.
    db.execute(
        pg_insert(GroupMember)
        .values(group_id=group_id, user_id=invitee.id, role=ROLE_MEMBER)
        .on_conflict_do_nothing(index_elements=["group_id", "user_id"])
    )
    db.commit()

    member = db.get(GroupMember, {"group_id": group_id, "user_id": invitee.id})
    assert member is not None
    return MemberOut(
        user_id=invitee.id,
        email=invitee.email,
        display_name=invitee.display_name,
        role=member.role,
        joined_at=member.joined_at,
    )


@router.delete(
    "/{group_id}/members/{user_id}", status_code=status.HTTP_204_NO_CONTENT
)
def remove_member(
    group_id: int, user_id: int, user: CurrentUser, db: DbSession
) -> None:
    group, caller = group_or_404(db, group_id, user)

    # The owner may remove anyone; anyone else may only remove themselves.
    if caller.role != ROLE_OWNER and user_id != user.id:
        raise forbidden("Only the group owner can remove other members.")
    if user_id == group.owner_id:
        raise conflict(
            "owner_locked",
            "The group owner cannot be removed. Delete the group instead.",
        )

    result = db.execute(
        delete(GroupMember).where(
            GroupMember.group_id == group_id, GroupMember.user_id == user_id
        )
    )
    if result.rowcount == 0:
        raise not_found("That member")
    db.commit()
