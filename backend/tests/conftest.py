"""Test fixtures.

These tests need a real Postgres because the idempotent-write behaviour relies
on ``ON CONFLICT DO NOTHING``. Point PDFORG_TEST_DATABASE_URL at a throwaway
database; every test run drops and recreates the schema.

    docker run -d --name pdforg-test-pg \
        -e POSTGRES_USER=pdforg -e POSTGRES_PASSWORD=pdforg \
        -e POSTGRES_DB=pdforg -p 55432:5432 postgres:16-alpine

    PDFORG_TEST_DATABASE_URL=postgresql+psycopg://pdforg:pdforg@localhost:55432/pdforg \
        pytest
"""

from __future__ import annotations

import os

import pytest

DEFAULT_URL = "postgresql+psycopg://pdforg:pdforg@localhost:55432/pdforg"

os.environ.setdefault(
    "PDFORG_DATABASE_URL", os.environ.get("PDFORG_TEST_DATABASE_URL", DEFAULT_URL)
)
os.environ.setdefault("PDFORG_JWT_SECRET", "test-secret-not-for-production")

from fastapi.testclient import TestClient  # noqa: E402

from app.db import Base, engine  # noqa: E402
from app.main import app  # noqa: E402
from app import models  # noqa: E402,F401


@pytest.fixture(autouse=True)
def fresh_schema():
    Base.metadata.drop_all(bind=engine)
    Base.metadata.create_all(bind=engine)
    yield
    Base.metadata.drop_all(bind=engine)


@pytest.fixture
def client() -> TestClient:
    return TestClient(app)


class Account:
    """A signed-in user, with helpers that carry its bearer token."""

    def __init__(self, client: TestClient, email: str, name: str) -> None:
        self._client = client
        response = client.post(
            "/auth/register",
            json={"email": email, "password": "hunter2hunter2", "display_name": name},
        )
        assert response.status_code == 201, response.text
        body = response.json()
        self.token = body["access_token"]
        self.refresh_token = body["refresh_token"]
        self.id = body["user"]["id"]
        self.email = email
        self.name = name

    @property
    def headers(self) -> dict[str, str]:
        return {"Authorization": f"Bearer {self.token}"}

    def get(self, url: str, **kw):
        return self._client.get(url, headers=self.headers, **kw)

    def post(self, url: str, **kw):
        return self._client.post(url, headers=self.headers, **kw)

    def put(self, url: str, **kw):
        return self._client.put(url, headers=self.headers, **kw)

    def patch(self, url: str, **kw):
        return self._client.patch(url, headers=self.headers, **kw)

    def delete(self, url: str, **kw):
        return self._client.delete(url, headers=self.headers, **kw)


@pytest.fixture
def alice(client: TestClient) -> Account:
    return Account(client, "alice@example.com", "Alice")


@pytest.fixture
def bob(client: TestClient) -> Account:
    return Account(client, "bob@example.com", "Bob")


@pytest.fixture
def carol(client: TestClient) -> Account:
    return Account(client, "carol@example.com", "Carol")
