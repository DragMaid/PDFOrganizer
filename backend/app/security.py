"""Password hashing and JWT issue/verify helpers."""

from __future__ import annotations

import uuid
from datetime import datetime, timedelta, timezone
from typing import Any, Literal

import logging
import bcrypt
import jwt

from .config import get_settings

TokenType = Literal["access", "refresh"]

# bcrypt hashes at most 72 bytes and errors on longer input in 4.x, so the
# password is pre-hashed to a fixed-width digest first. This also means a very
# long passphrase keeps all of its entropy instead of being silently truncated.
logger = logging.getLogger(__name__)
_BCRYPT_ROUNDS = 12


def _prepare(password: str) -> bytes:
    import hashlib

    return hashlib.sha256(password.encode("utf-8")).digest()


def hash_password(password: str) -> str:
    return bcrypt.hashpw(_prepare(password), bcrypt.gensalt(_BCRYPT_ROUNDS)).decode()


def verify_password(password: str, password_hash: str) -> bool:
    try:
        return bcrypt.checkpw(_prepare(password), password_hash.encode())
    except ValueError:
        return False


def _create_token(subject: int, token_type: TokenType, ttl: timedelta) -> str:
    settings = get_settings()
    now = datetime.now(timezone.utc)
    payload: dict[str, Any] = {
        "sub": str(subject),
        "typ": token_type,
        "iat": int(now.timestamp()),
        "exp": int((now + ttl).timestamp()),
        "jti": uuid.uuid4().hex,
    }
    return jwt.encode(payload, settings.jwt_secret, algorithm=settings.jwt_algorithm)


def create_access_token(user_id: int) -> str:
    settings = get_settings()
    return _create_token(
        user_id, "access", timedelta(minutes=settings.access_token_ttl_minutes)
    )


def create_refresh_token(user_id: int) -> str:
    settings = get_settings()
    return _create_token(
        user_id, "refresh", timedelta(days=settings.refresh_token_ttl_days)
    )


def decode_token(token: str, expected_type: TokenType) -> int | None:
    """Return the user id encoded in ``token``, or ``None`` if it is unusable."""
    settings = get_settings()
    try:
        payload = jwt.decode(
            token, settings.jwt_secret, algorithms=[settings.jwt_algorithm]
        )
    except jwt.PyJWTError as e:
        logger.error(f"Exception decoding JWT: {e}")
        return None

    if payload.get("typ") != expected_type:
        return None

    try:
        return int(payload["sub"])
    except (KeyError, TypeError, ValueError) as e:
        logger.info(f"Conversion error: {e}")
        return None
