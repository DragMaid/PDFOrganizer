"""Application configuration, loaded from the environment / .env file."""

from __future__ import annotations

from functools import lru_cache

from pydantic import Field
from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(
        env_file=".env",
        env_prefix="PDFORG_",
        extra="ignore",
    )

    # ── Database ──────────────────────────────────────────────────────────────
    database_url: str = "postgresql+psycopg://pdforg:pdforg@localhost:5432/pdforg"
    sql_echo: bool = False

    # ── Auth ──────────────────────────────────────────────────────────────────
    jwt_secret: str = "change-me-to-a-long-random-string"
    jwt_algorithm: str = "HS256"
    access_token_ttl_minutes: int = 30
    refresh_token_ttl_days: int = 30
    allow_registration: bool = True

    # ── Backblaze B2 ──────────────────────────────────────────────────────────
    b2_key_id: str = ""
    b2_application_key: str = ""
    b2_bucket_id: str = ""
    b2_bucket_name: str = ""
    b2_api_url: str = "https://api.backblazeb2.com"
    max_upload_bytes: int = 200 * 1024 * 1024

    # ── Misc ──────────────────────────────────────────────────────────────────
    cors_origins: list[str] = Field(default_factory=list)

    @property
    def b2_configured(self) -> bool:
        return bool(
            self.b2_key_id
            and self.b2_application_key
            and self.b2_bucket_id
            and self.b2_bucket_name
        )


@lru_cache
def get_settings() -> Settings:
    return Settings()
