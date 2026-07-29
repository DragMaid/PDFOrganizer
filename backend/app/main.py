"""FastAPI application entry point."""

from __future__ import annotations

import logging

from fastapi import FastAPI, HTTPException
from fastapi.exceptions import RequestValidationError
from fastapi.middleware.cors import CORSMiddleware
from sqlalchemy import select

from . import __version__
from .config import Settings, get_settings
from .db import get_db
from .errors import (
    ApiError,
    api_error_handler,
    http_exception_handler,
    unhandled_exception_handler,
    validation_exception_handler,
)
from .routers import auth, files, groups, notes, tags

logging.basicConfig(level=logging.INFO)

settings = get_settings()

app = FastAPI(
    title="PDF Organizer API",
    version=__version__,
    description=(
        "Backend for the PDF Organizer desktop client. Owns Postgres and "
        "Backblaze B2; the client never talks to either directly."
    ),
)

if settings.cors_origins:
    app.add_middleware(
        CORSMiddleware,
        allow_origins=settings.cors_origins,
        allow_credentials=True,
        allow_methods=["*"],
        allow_headers=["*"],
    )

app.add_exception_handler(ApiError, api_error_handler)
app.add_exception_handler(HTTPException, http_exception_handler)
app.add_exception_handler(RequestValidationError, validation_exception_handler)
app.add_exception_handler(Exception, unhandled_exception_handler)

log = logging.getLogger(__name__)

if settings.jwt_secret == Settings.model_fields["jwt_secret"].default:
    log.warning(
        "PDFORG_JWT_SECRET is still the default value. Every token this server "
        "issues can be forged. Set it before exposing this to anyone."
    )
elif len(settings.jwt_secret) < 32:
    log.warning(
        "PDFORG_JWT_SECRET is only %d characters. Use at least 32.",
        len(settings.jwt_secret),
    )

if not settings.b2_configured:
    log.warning(
        "Backblaze B2 is not configured; file uploads will be refused with a "
        "clear error. Set the PDFORG_B2_* variables to enable them."
    )

app.include_router(auth.router)
app.include_router(groups.router)
app.include_router(files.router)
app.include_router(files.sync_router)
app.include_router(tags.router)
app.include_router(notes.router)


@app.get("/health", tags=["meta"])
def health() -> dict[str, object]:
    """Cheap probe the Qt client calls before showing the login dialog."""
    db_ok = True
    db = next(get_db())
    try:
        db.execute(select(1))
    except Exception:  # noqa: BLE001 - a probe must never raise
        db_ok = False
    finally:
        db.close()

    return {
        "status": "ok" if db_ok else "degraded",
        "version": __version__,
        "database": db_ok,
        "storage_configured": settings.b2_configured,
        "registration_open": settings.allow_registration,
    }
