#!/usr/bin/env python
"""Create the database schema.

    python init_db.py

Idempotent — safe to re-run. This project deliberately does not carry Alembic;
if you start changing the schema after going live, add it then.
"""

from __future__ import annotations

import sys

from sqlalchemy import inspect

from app.db import Base, engine
from app import models  # noqa: F401 - imported so Base knows every table


def main() -> int:
    Base.metadata.create_all(bind=engine)
    tables = sorted(inspect(engine).get_table_names())
    print(f"Schema ready on {engine.url.render_as_string(hide_password=True)}")
    print("Tables: " + ", ".join(tables))
    return 0


if __name__ == "__main__":
    sys.exit(main())
