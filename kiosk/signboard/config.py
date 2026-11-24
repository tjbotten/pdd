"""Paths and environment. Secrets are read from the environment, never from the repo."""

from __future__ import annotations

import os
from pathlib import Path
from zoneinfo import ZoneInfo

ROOT = Path(__file__).resolve().parent.parent
TEMPLATES = ROOT / "templates"
STATIC = ROOT / "static"

JOHANNESBURG = ZoneInfo("Africa/Johannesburg")

# How often the server is willing to hit upstreams. The display polls on the
# same cadence, so two kiosks share one fetch.
TRAFFIC_TTL_SECONDS = 30 * 60
WEATHER_TTL_SECONDS = 10 * 60
NEWS_TTL_SECONDS = 3 * 60
# Keep the last good payload if a refresh fails. Traffic grace covers the
# next 30-minute lookup, so one miss does not blank the band.
TRAFFIC_GRACE_SECONDS = 60 * 60
WEATHER_GRACE_SECONDS = 30 * 60
NEWS_GRACE_SECONDS = 15 * 60

STORY_SECONDS = 18
ANNOUNCEMENT_POLL_SECONDS = 30
ANNOUNCEMENT_MAX_CHARS = 180
ANNOUNCEMENT_MAX_POSTS = 40


def admin_password() -> str:
    return os.environ.get("KIOSK_ADMIN_PASSWORD", "")


def tomtom_api_key() -> str:
    return os.environ.get("TOMTOM_API_KEY", "").strip()


def database_path() -> Path:
    raw = os.environ.get("KIOSK_DB_PATH", "").strip()
    if raw:
        return Path(raw)
    return ROOT / "data" / "announcements.db"


def cookie_secure() -> bool:
    return os.environ.get("KIOSK_COOKIE_SECURE", "").strip() in {"1", "true", "yes"}


def load_dotenv() -> None:
    """Load kiosk/.env without overriding variables already set in the process."""
    path = ROOT / ".env"
    if not path.is_file():
        return
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#") or "=" not in stripped:
            continue
        key, value = stripped.split("=", 1)
        key = key.strip()
        value = value.strip().strip('"').strip("'")
        if key:
            os.environ.setdefault(key, value)
