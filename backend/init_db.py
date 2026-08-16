#!/usr/bin/env python
"""Create the database schema.

    python init_db.py

Idempotent — safe to re-run. This project deliberately does not carry Alembic;
if you start changing the schema after going live, add it then.

``create_all`` only ever creates missing *tables*, so a column added to an
existing table needs a nudge. :func:`backfill_share_codes` is that nudge for
``groups.share_code`` — it is a no-op on a database that already has it, and on
a fresh one.
"""

from __future__ import annotations

import sys

from sqlalchemy import inspect, text

from app.db import Base, engine
from app import models  # noqa: F401 - imported so Base knows every table
from app.models import new_share_code


def reset_file_identity_schema() -> bool:
    """Drop and recreate ``group_files``/``file_tags``/``notes`` if they predate
    the per-file uuid identity.

    File identity moved from content hash to a client-assigned uuid on
    ``group_files``, which needs its own primary key — not a new column, which
    is all ``create_all`` and the ``ALTER TABLE`` approach used elsewhere in
    this file can add to an existing table. This project carries no migration
    framework by design (see the module docstring), so the honest fix is the
    same one used everywhere else here: recreate the affected tables and let
    clients re-sync. Groups, memberships and users are untouched — only files,
    tags and notes are dropped, and only on a database still on the old shape.
    A fresh or already-migrated database is unaffected.
    """
    inspector = inspect(engine)
    if "group_files" not in inspector.get_table_names():
        return False
    columns = {column["name"] for column in inspector.get_columns("group_files")}
    if "id" in columns:
        return False

    with engine.begin() as connection:
        for table in ("notes", "file_tags", "group_files"):
            connection.execute(text(f"DROP TABLE IF EXISTS {table} CASCADE"))
    return True


def backfill_share_codes() -> int:
    """Add ``groups.share_code`` where missing and fill in a code per row.

    Codes are generated one row at a time rather than in SQL because they have
    to come from a CSPRNG — a database-side ``random()`` would be guessable.
    """
    inspector = inspect(engine)
    if "groups" not in inspector.get_table_names():
        return 0

    columns = {column["name"] for column in inspector.get_columns("groups")}
    filled = 0

    with engine.begin() as connection:
        if "share_code" not in columns:
            # Nullable and unconstrained to begin with, so existing rows survive
            # the ALTER; both are tightened once every row has a code.
            connection.execute(
                text("ALTER TABLE groups ADD COLUMN share_code VARCHAR(32)")
            )

        pending = connection.execute(
            text("SELECT id FROM groups WHERE share_code IS NULL")
        ).scalars().all()

        for group_id in pending:
            connection.execute(
                text("UPDATE groups SET share_code = :code WHERE id = :id"),
                {"code": new_share_code(), "id": group_id},
            )
            filled += 1

        connection.execute(
            text(
                "CREATE UNIQUE INDEX IF NOT EXISTS uq_groups_share_code "
                "ON groups (share_code)"
            )
        )
        connection.execute(
            text("ALTER TABLE groups ALTER COLUMN share_code SET NOT NULL")
        )

    return filled


def main() -> int:
    reset = reset_file_identity_schema()
    Base.metadata.create_all(bind=engine)
    filled = backfill_share_codes()

    tables = sorted(inspect(engine).get_table_names())
    print(f"Schema ready on {engine.url.render_as_string(hide_password=True)}")
    print("Tables: " + ", ".join(tables))
    if reset:
        print(
            "Recreated group_files/file_tags/notes for the new per-file uuid "
            "identity — existing files, tags and notes were dropped. Clients "
            "pick everything back up on their next sync."
        )
    if filled:
        print(f"Generated share codes for {filled} existing group(s).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
