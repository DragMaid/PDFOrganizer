"""Shared FastAPI dependencies: current user, group membership, ownership."""

from __future__ import annotations

from typing import Annotated

from fastapi import Depends
from fastapi.security import HTTPAuthorizationCredentials, HTTPBearer
from sqlalchemy import select
from sqlalchemy.orm import Session

from .db import get_db
from .errors import forbidden, not_found, unauthorized
from .models import ROLE_OWNER, File, Group, GroupFile, GroupMember, User
from .security import decode_token

_bearer = HTTPBearer(auto_error=False)

DbSession = Annotated[Session, Depends(get_db)]


def current_user(
    db: DbSession,
    creds: Annotated[HTTPAuthorizationCredentials | None, Depends(_bearer)] = None,
) -> User:
    if creds is None:
        raise unauthorized("Sign in to continue.")

    user_id = decode_token(creds.credentials, "access")
    if user_id is None:
        raise unauthorized()

    user = db.get(User, user_id)
    if user is None:
        raise unauthorized("This account no longer exists.")
    return user


CurrentUser = Annotated[User, Depends(current_user)]


def membership_or_403(db: Session, group_id: int, user: User) -> GroupMember:
    """Every content operation funnels through here."""
    member = db.get(GroupMember, {"group_id": group_id, "user_id": user.id})
    if member is None:
        # Deliberately identical to the missing-group case: a non-member should
        # not be able to probe which group ids exist.
        raise not_found("That group")
    return member


def ownership_or_403(db: Session, group_id: int, user: User) -> GroupMember:
    member = membership_or_403(db, group_id, user)
    if member.role != ROLE_OWNER:
        raise forbidden("Only the group owner can do that.")
    return member


def group_or_404(db: Session, group_id: int, user: User) -> tuple[Group, GroupMember]:
    member = membership_or_403(db, group_id, user)
    group = db.get(Group, group_id)
    if group is None:
        raise not_found("That group")
    return group, member


def group_file_or_404(
    db: Session, group_id: int, file_id: int
) -> tuple[File, GroupFile]:
    link = db.get(GroupFile, {"group_id": group_id, "file_id": file_id})
    # TODO: add proper file meta here later
    if link is None:
        raise not_found("That file")
    file = db.get(File, file_id)
    if file is None:
        raise not_found("That file")
    return file, link


def shares_a_group(db: Session, user: User, file_id: int) -> bool:
    stmt = (
        select(GroupFile.file_id)
        .join(GroupMember, GroupMember.group_id == GroupFile.group_id)
        .where(GroupFile.file_id == file_id, GroupMember.user_id == user.id)
        .limit(1)
    )
    return db.execute(stmt).first() is not None
