"""ORM models.

Identity and scoping rules that the rest of the backend depends on:

* A **file** (``files``) is content-addressed: keyed by the SHA-256 of its
  bytes, and shared — blob and all — by every group that happens to hold the
  same content. It exists purely for storage dedup and never appears in an
  API response as an identity a client should remember.
* A **group's listing of a file** (``group_files``) is the identity clients
  actually track, anchored by ``client_uuid`` — a value a client assigns once,
  the first time it sees a PDF, and keeps for as long as it tracks that file
  locally. Registering the same uuid again repoints ``file_id`` at whatever
  content came with the call, which is what lets a PDF be annotated (new
  bytes, new content hash) without losing the tags and notes attached to it:
  those hang off ``group_files.id``, which does not change just because the
  bytes did. A uuid the group has never seen falls back to matching by content
  hash instead, so two members registering an unmodified PDF for the first
  time still resolve to one listing.
* A file becomes visible through ``group_files``. Nothing outside a group is
  readable, so every permission check reduces to "is the caller a member of
  this group".
* **Tags and notes are group-scoped**, and — since the move to per-listing
  identity — group-file-scoped: both carry a foreign key to the ``group_files``
  row they belong to rather than to the shared content, so the same PDF
  registered in two groups keeps two independent sets of tags and notes.
* A group's ``share_code`` is a **bearer credential**: whoever holds it can join
  the group. It is therefore random and opaque, never the sequential ``id``,
  which anyone could count up through.
"""

from __future__ import annotations

import re
import secrets
from datetime import datetime, timezone

from sqlalchemy import (
    BigInteger,
    Boolean,
    CheckConstraint,
    DateTime,
    ForeignKey,
    Index,
    Integer,
    String,
    Text,
    UniqueConstraint,
    func,
)
from sqlalchemy.orm import Mapped, mapped_column, relationship

from .db import Base

ROLE_OWNER = "owner"
ROLE_MEMBER = "member"

# Crockford-style alphabet: no I, L, O, U, 0 or 1, so a code read aloud or
# copied by hand cannot turn into a different valid code.
SHARE_ALPHABET = "23456789ABCDEFGHJKMNPQRSTVWXYZ"
SHARE_BODY_LEN = 12
SHARE_PREFIX = "PDFORG"


def utcnow() -> datetime:
    return datetime.now(timezone.utc)


def new_share_code() -> str:
    """A fresh join code, e.g. ``PDFORG-7K2M-9QX4-H3TB``.

    12 symbols out of a 30-symbol alphabet is ~59 bits — enough that guessing a
    live code is not a realistic attack, which matters because holding the code
    is by itself sufficient to join.
    """
    body = "".join(secrets.choice(SHARE_ALPHABET) for _ in range(SHARE_BODY_LEN))
    return format_share_code(body)


def format_share_code(body: str) -> str:
    groups = [body[i : i + 4] for i in range(0, len(body), 4)]
    return "-".join([SHARE_PREFIX, *groups])


def normalize_share_code(raw: str) -> str | None:
    """Canonicalise whatever the user pasted, or return ``None`` if it can't be.

    Accepts the code with or without the prefix, in any case, and with any
    spacing or hyphenation — people retype these from chat messages.
    """
    body = re.sub(r"[^A-Za-z0-9]", "", raw).upper()
    if body.startswith(SHARE_PREFIX):
        body = body[len(SHARE_PREFIX) :]
    if len(body) != SHARE_BODY_LEN:
        return None
    if any(char not in SHARE_ALPHABET for char in body):
        return None
    return format_share_code(body)


class User(Base):
    __tablename__ = "users"

    id: Mapped[int] = mapped_column(primary_key=True)
    email: Mapped[str] = mapped_column(String(320), unique=True, nullable=False)
    password_hash: Mapped[str] = mapped_column(String(255), nullable=False)
    display_name: Mapped[str] = mapped_column(String(120), nullable=False)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), nullable=False
    )

    memberships: Mapped[list["GroupMember"]] = relationship(
        back_populates="user", cascade="all, delete-orphan"
    )


class Group(Base):
    __tablename__ = "groups"

    id: Mapped[int] = mapped_column(primary_key=True)
    name: Mapped[str] = mapped_column(String(200), nullable=False)
    owner_id: Mapped[int] = mapped_column(
        ForeignKey("users.id", ondelete="CASCADE"), nullable=False
    )
    # Auto-created at signup. Guarantees every registered file has a permission
    # context even before the user shares anything.
    is_personal: Mapped[bool] = mapped_column(Boolean, default=False, nullable=False)
    # Handing this string to someone is what lets them join. Personal groups
    # carry one only because the column is not nullable — joining one is refused,
    # and the API never hands it out.
    share_code: Mapped[str] = mapped_column(
        String(32), unique=True, nullable=False, default=new_share_code
    )
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), nullable=False
    )

    members: Mapped[list["GroupMember"]] = relationship(
        back_populates="group", cascade="all, delete-orphan"
    )

    __table_args__ = (Index("ix_groups_owner", "owner_id"),)


class GroupMember(Base):
    __tablename__ = "group_members"

    group_id: Mapped[int] = mapped_column(
        ForeignKey("groups.id", ondelete="CASCADE"), primary_key=True
    )
    user_id: Mapped[int] = mapped_column(
        ForeignKey("users.id", ondelete="CASCADE"), primary_key=True
    )
    role: Mapped[str] = mapped_column(String(16), default=ROLE_MEMBER, nullable=False)
    joined_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), nullable=False
    )

    group: Mapped[Group] = relationship(back_populates="members")
    user: Mapped[User] = relationship(back_populates="memberships")

    __table_args__ = (
        CheckConstraint(f"role IN ('{ROLE_OWNER}', '{ROLE_MEMBER}')", name="ck_role"),
        Index("ix_group_members_user", "user_id"),
    )


class File(Base):
    """A PDF's content, keyed by hash and shared across every group that holds it.

    Purely a storage-dedup row: see ``GroupFile`` for the identity a client
    actually tracks.
    """

    __tablename__ = "files"

    id: Mapped[int] = mapped_column(primary_key=True)
    content_hash: Mapped[str] = mapped_column(String(64), unique=True, nullable=False)
    # Name recorded by whoever registered the content first; per-group overrides
    # live on GroupFile.display_name.
    file_name: Mapped[str] = mapped_column(String(500), nullable=False)
    file_size_bytes: Mapped[int] = mapped_column(BigInteger, default=0, nullable=False)
    page_count: Mapped[int] = mapped_column(Integer, default=0, nullable=False)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), nullable=False
    )

    # ── Backblaze blob (one per content hash, deduped across all groups) ──────
    b2_file_id: Mapped[str | None] = mapped_column(String(200), nullable=True)
    b2_file_name: Mapped[str | None] = mapped_column(String(1024), nullable=True)
    uploaded_at: Mapped[datetime | None] = mapped_column(
        DateTime(timezone=True), nullable=True
    )
    uploaded_by: Mapped[int | None] = mapped_column(
        ForeignKey("users.id", ondelete="SET NULL"), nullable=True
    )


class GroupFile(Base):
    """One group's listing of a file — the identity a client tracks.

    ``id`` and ``client_uuid`` outlive the content: re-registering the same
    ``client_uuid`` with a different ``file_id`` (an annotated PDF, re-scanned)
    repoints this row rather than creating a second listing, so the tags and
    notes hanging off ``id`` survive the edit.
    """

    __tablename__ = "group_files"

    id: Mapped[int] = mapped_column(primary_key=True)
    group_id: Mapped[int] = mapped_column(
        ForeignKey("groups.id", ondelete="CASCADE"), nullable=False
    )
    file_id: Mapped[int] = mapped_column(
        ForeignKey("files.id", ondelete="CASCADE"), nullable=False
    )
    # Client-assigned and stable for as long as that client tracks the file
    # locally. Unique per group, not globally — two different groups may each
    # have been handed the same uuid by the same client for the same folder.
    client_uuid: Mapped[str] = mapped_column(String(64), nullable=False)
    display_name: Mapped[str] = mapped_column(String(500), nullable=False)
    added_by: Mapped[int | None] = mapped_column(
        ForeignKey("users.id", ondelete="SET NULL"), nullable=True
    )
    added_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), nullable=False
    )

    __table_args__ = (
        UniqueConstraint("group_id", "client_uuid", name="uq_group_files_uuid"),
        Index("ix_group_files_file", "file_id"),
        Index("ix_group_files_group", "group_id"),
    )


class Tag(Base):
    """Tag vocabulary, scoped to one group."""

    __tablename__ = "tags"

    id: Mapped[int] = mapped_column(primary_key=True)
    group_id: Mapped[int] = mapped_column(
        ForeignKey("groups.id", ondelete="CASCADE"), nullable=False
    )
    name: Mapped[str] = mapped_column(String(120), nullable=False)
    # Lower-cased copy so "Taxes" and "taxes" collide on a plain unique index —
    # this is what makes concurrent creation of the same tag a no-op instead of
    # a duplicate.
    name_lower: Mapped[str] = mapped_column(String(120), nullable=False)
    created_by: Mapped[int | None] = mapped_column(
        ForeignKey("users.id", ondelete="SET NULL"), nullable=True
    )
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), nullable=False
    )

    __table_args__ = (
        UniqueConstraint("group_id", "name_lower", name="uq_tag_group_name"),
    )


class FileTag(Base):
    __tablename__ = "file_tags"

    # References the group's listing, not the shared content — see GroupFile.
    group_file_id: Mapped[int] = mapped_column(
        ForeignKey("group_files.id", ondelete="CASCADE"), primary_key=True
    )
    tag_id: Mapped[int] = mapped_column(
        ForeignKey("tags.id", ondelete="CASCADE"), primary_key=True
    )
    added_by: Mapped[int | None] = mapped_column(
        ForeignKey("users.id", ondelete="SET NULL"), nullable=True
    )
    added_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), nullable=False
    )

    __table_args__ = (Index("ix_file_tags_tag", "tag_id"),)


class Note(Base):
    __tablename__ = "notes"

    id: Mapped[int] = mapped_column(primary_key=True)
    group_id: Mapped[int] = mapped_column(
        ForeignKey("groups.id", ondelete="CASCADE"), nullable=False
    )
    # References the group's listing, not the shared content — see GroupFile.
    group_file_id: Mapped[int] = mapped_column(
        ForeignKey("group_files.id", ondelete="CASCADE"), nullable=False
    )
    author_id: Mapped[int] = mapped_column(
        ForeignKey("users.id", ondelete="CASCADE"), nullable=False
    )
    body: Mapped[str] = mapped_column(Text, nullable=False)
    # Bumped on every edit; clients send the version they read so a concurrent
    # edit from the author's other machine is rejected instead of silently lost.
    version: Mapped[int] = mapped_column(Integer, default=1, nullable=False)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), nullable=False
    )
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), nullable=False
    )
    deleted_at: Mapped[datetime | None] = mapped_column(
        DateTime(timezone=True), nullable=True
    )

    author: Mapped[User] = relationship()

    __table_args__ = (Index("ix_notes_group_file", "group_id", "group_file_id"),)
