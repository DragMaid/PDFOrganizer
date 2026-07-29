"""Registration, login and token refresh."""

from __future__ import annotations

from fastapi import APIRouter, status
from sqlalchemy import func, select
from sqlalchemy.exc import IntegrityError

from ..config import get_settings
from ..deps import CurrentUser, DbSession
from ..errors import bad_request, conflict, unauthorized
from ..models import ROLE_OWNER, Group, GroupMember, User
from ..schemas import (
    LoginRequest,
    RefreshRequest,
    RegisterRequest,
    TokenPair,
    UserOut,
)
from ..security import (
    create_access_token,
    create_refresh_token,
    decode_token,
    hash_password,
    verify_password,
)

router = APIRouter(prefix="/auth", tags=["auth"])


def _token_pair(user: User) -> TokenPair:
    settings = get_settings()
    return TokenPair(
        access_token=create_access_token(user.id),
        refresh_token=create_refresh_token(user.id),
        expires_in=settings.access_token_ttl_minutes * 60,
        user=UserOut.model_validate(user),
    )


@router.post("/register", response_model=TokenPair, status_code=status.HTTP_201_CREATED)
def register(payload: RegisterRequest, db: DbSession) -> TokenPair:
    settings = get_settings()
    if not settings.allow_registration:
        raise bad_request("Registration is disabled on this server.")

    email = payload.email.strip().lower()
    user = User(
        email=email,
        password_hash=hash_password(payload.password),
        display_name=payload.display_name.strip(),
    )
    db.add(user)
    try:
        db.flush()
    except IntegrityError:
        db.rollback()
        raise conflict(
            "email_taken", "An account already exists for that email address."
        ) from None

    # Every user gets a private group so that files, tags and notes always have
    # a group to live in, even before anything is shared.
    personal = Group(name="Personal", owner_id=user.id, is_personal=True)
    db.add(personal)
    db.flush()
    db.add(GroupMember(group_id=personal.id, user_id=user.id, role=ROLE_OWNER))
    db.commit()
    db.refresh(user)
    return _token_pair(user)


@router.post("/login", response_model=TokenPair)
def login(payload: LoginRequest, db: DbSession) -> TokenPair:
    email = payload.email.strip().lower()
    user = db.execute(
        select(User).where(func.lower(User.email) == email)
    ).scalar_one_or_none()

    # Same message either way so the endpoint cannot be used to enumerate
    # registered addresses.
    if user is None or not verify_password(payload.password, user.password_hash):
        raise unauthorized("Incorrect email address or password.")
    return _token_pair(user)


@router.post("/refresh", response_model=TokenPair)
def refresh(payload: RefreshRequest, db: DbSession) -> TokenPair:
    user_id = decode_token(payload.refresh_token, "refresh")
    if user_id is None:
        raise unauthorized("Your session has expired. Sign in again.")

    user = db.get(User, user_id)
    if user is None:
        raise unauthorized("This account no longer exists.")
    return _token_pair(user)


@router.get("/me", response_model=UserOut)
def me(user: CurrentUser) -> User:
    return user


