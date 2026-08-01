"""A single error shape for every failure the client can encounter.

Every non-2xx response body is ``{"code": ..., "message": ..., "detail": ...}``.
The Qt client maps ``message`` straight into an error modal, so messages are
written to be read by a human, not a log parser.
"""

from __future__ import annotations

from typing import Any

from fastapi import HTTPException, Request, status
from fastapi.encoders import jsonable_encoder
from fastapi.exceptions import RequestValidationError
from fastapi.responses import JSONResponse


class ApiError(HTTPException):
    def __init__(
        self,
        status_code: int,
        code: str,
        message: str,
        detail: Any = None,
    ) -> None:
        super().__init__(status_code=status_code, detail=message)
        self.code = code
        self.message = message
        self.extra = detail


def not_found(what: str) -> ApiError:
    return ApiError(status.HTTP_404_NOT_FOUND, "not_found", f"{what} was not found.")


def forbidden(message: str) -> ApiError:
    return ApiError(status.HTTP_403_FORBIDDEN, "forbidden", message)


def conflict(code: str, message: str, detail: Any = None) -> ApiError:
    return ApiError(status.HTTP_409_CONFLICT, code, message, detail)


def bad_request(message: str) -> ApiError:
    return ApiError(status.HTTP_400_BAD_REQUEST, "bad_request", message)


def unauthorized(message: str = "Your session has expired. Sign in again.") -> ApiError:
    return ApiError(status.HTTP_401_UNAUTHORIZED, "unauthorized", message)


async def api_error_handler(_: Request, exc: ApiError) -> JSONResponse:
    return JSONResponse(
        status_code=exc.status_code,
        content={
            "code": exc.code,
            "message": exc.message,
            "detail": jsonable_encoder(exc.extra),
        },
        headers=exc.headers,
    )


async def http_exception_handler(_: Request, exc: HTTPException) -> JSONResponse:
    """Give plain ``HTTPException``s (including FastAPI's own) the same shape."""
    return JSONResponse(
        status_code=exc.status_code,
        content={
            "code": "http_error",
            "message": str(exc.detail),
            "detail": None,
        },
        headers=exc.headers,
    )


async def validation_exception_handler(
    _: Request, exc: RequestValidationError
) -> JSONResponse:
    first = exc.errors()[0] if exc.errors() else {}
    field = ".".join(str(p) for p in first.get("loc", ())[1:]) or "request"
    return JSONResponse(
        status_code=status.HTTP_422_UNPROCESSABLE_ENTITY,
        content={
            "code": "validation_error",
            "message": f"{field}: {first.get('msg', 'is invalid')}",
            "detail": jsonable_encoder(exc.errors()),
        },
    )


async def unhandled_exception_handler(_: Request, exc: Exception) -> JSONResponse:
    return JSONResponse(
        status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
        content={
            "code": "internal_error",
            "message": "The server hit an unexpected error. Try again.",
            "detail": None,
        },
    )
