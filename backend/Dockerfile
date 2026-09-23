# The API image. Also runs the schema step: `docker compose run migrate` is this image with
# `python init_db.py` as its command, so a release always migrates with its own models.

# ── Build: resolve the locked dependencies into a venv ───────────────────────
FROM python:3.13-slim AS build

# psycopg2 ships no Linux wheel, so it compiles against libpq here and only the result
# is copied into the runtime stage.
RUN apt-get update \
    && apt-get install -y --no-install-recommends gcc libc6-dev libpq-dev \
    && rm -rf /var/lib/apt/lists/*

COPY --from=ghcr.io/astral-sh/uv:0.8 /uv /usr/local/bin/uv

ENV UV_COMPILE_BYTECODE=1 \
    UV_LINK_MODE=copy \
    UV_PYTHON_DOWNLOADS=never

WORKDIR /app
COPY pyproject.toml uv.lock ./
RUN uv sync --locked --no-install-project --no-dev

# ── Runtime ──────────────────────────────────────────────────────────────────
FROM python:3.13-slim

# libpq5 is what both psycopg (v3, pure Python) and psycopg2 load at runtime.
RUN apt-get update \
    && apt-get install -y --no-install-recommends libpq5 curl \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --uid 10001 --home /app app

WORKDIR /app
COPY --from=build /app/.venv /app/.venv
COPY app ./app
COPY init_db.py ./

ENV PATH="/app/.venv/bin:$PATH" \
    PYTHONUNBUFFERED=1

USER app
EXPOSE 8000

HEALTHCHECK --interval=10s --timeout=5s --retries=5 \
    CMD curl -fs http://127.0.0.1:8000/health | grep -q '"status":"ok"' || exit 1

# --proxy-headers: Traefik is the only thing that can reach this container, so its
# X-Forwarded-* headers are trusted.
CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8000", \
     "--proxy-headers", "--forwarded-allow-ips", "*"]
