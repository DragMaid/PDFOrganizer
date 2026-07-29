"""Request and response bodies."""

from __future__ import annotations

import re
from datetime import datetime

from pydantic import BaseModel, ConfigDict, EmailStr, Field, field_validator

HASH_RE = re.compile(r"^[0-9a-f]{64}$")


class ORMModel(BaseModel):
    model_config = ConfigDict(from_attributes=True)


# ── Auth ──────────────────────────────────────────────────────────────────────


class RegisterRequest(BaseModel):
    email: EmailStr
    password: str = Field(min_length=8, max_length=200)
    display_name: str = Field(min_length=1, max_length=120)


class LoginRequest(BaseModel):
    email: EmailStr
    password: str


class RefreshRequest(BaseModel):
    refresh_token: str


class UserOut(ORMModel):
    id: int
    email: str
    display_name: str
    created_at: datetime


class TokenPair(BaseModel):
    access_token: str
    refresh_token: str
    token_type: str = "bearer"
    expires_in: int
    user: UserOut


# ── Groups ────────────────────────────────────────────────────────────────────


class GroupCreate(BaseModel):
    name: str = Field(min_length=1, max_length=200)


class GroupUpdate(BaseModel):
    name: str = Field(min_length=1, max_length=200)


class GroupOut(BaseModel):
    id: int
    name: str
    owner_id: int
    is_personal: bool
    created_at: datetime
    my_role: str
    member_count: int
    file_count: int


class MemberOut(BaseModel):
    user_id: int
    email: str
    display_name: str
    role: str
    joined_at: datetime


class MemberAdd(BaseModel):
    email: EmailStr


# ── Files ─────────────────────────────────────────────────────────────────────


class FileRegister(BaseModel):
    """Register a locally scanned PDF into a group.

    Repeating this call with the same ``content_hash`` is a no-op that returns
    the existing record, so two members adding the same PDF never conflict.
    """

    content_hash: str
    file_name: str = Field(min_length=1, max_length=500)
    file_size_bytes: int = Field(ge=0)
    page_count: int = Field(default=0, ge=0)

    @field_validator("content_hash")
    @classmethod
    def _check_hash(cls, value: str) -> str:
        value = value.strip().lower()
        if not HASH_RE.match(value):
            raise ValueError("must be a lower-case hex SHA-256 digest")
        return value


class FileOut(BaseModel):
    id: int
    content_hash: str
    file_name: str
    file_size_bytes: int
    page_count: int
    added_at: datetime
    added_by: int | None
    uploaded: bool
    uploaded_at: datetime | None
    tags: list[str] = []


# ── Tags ──────────────────────────────────────────────────────────────────────


class TagCreate(BaseModel):
    name: str = Field(min_length=1, max_length=120)


class TagOut(ORMModel):
    id: int
    group_id: int
    name: str
    created_by: int | None
    created_at: datetime


class TagAssign(BaseModel):
    name: str = Field(min_length=1, max_length=120)


class TagSet(BaseModel):
    names: list[str]


# ── Notes ─────────────────────────────────────────────────────────────────────


class NoteCreate(BaseModel):
    body: str = Field(min_length=1, max_length=20000)


class NoteUpdate(BaseModel):
    body: str = Field(min_length=1, max_length=20000)
    # Version the client last read. Omit to force the write through.
    version: int | None = None


class NoteOut(BaseModel):
    id: int
    group_id: int
    file_id: int
    author_id: int
    author_name: str
    body: str
    version: int
    created_at: datetime
    updated_at: datetime
    # True when the caller wrote this note, i.e. the client may offer
    # edit/delete. The server enforces the same rule regardless.
    editable: bool


# ── Sync ──────────────────────────────────────────────────────────────────────


class SyncStatusOut(BaseModel):
    group_id: int
    total_files: int
    uploaded_files: int
    pending: list[FileOut]


class UploadResult(BaseModel):
    file_id: int
    uploaded: bool
    already_present: bool
    b2_file_id: str | None
    message: str
