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
    Base.metadata.create_all(bind=engine)
    filled = backfill_share_codes()

    tables = sorted(inspect(engine).get_table_names())
    print(f"Schema ready on {engine.url.render_as_string(hide_password=True)}")
    print("Tables: " + ", ".join(tables))
    if filled:
        print(f"Generated share codes for {filled} existing group(s).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
