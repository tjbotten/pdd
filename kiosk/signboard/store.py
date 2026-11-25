"""SQLite store for announcements shared by both kiosks.

The database starts empty. Nothing here inserts sample or company posts.
"""

from __future__ import annotations

import sqlite3
from datetime import datetime
from pathlib import Path

from .config import ANNOUNCEMENT_MAX_CHARS, ANNOUNCEMENT_MAX_POSTS, JOHANNESBURG


def connect(path: Path) -> sqlite3.Connection:
    path.parent.mkdir(parents=True, exist_ok=True)
    connection = sqlite3.connect(path)
    connection.row_factory = sqlite3.Row
    connection.execute(
        """
        CREATE TABLE IF NOT EXISTS announcements (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            body TEXT NOT NULL,
            created_at TEXT NOT NULL
        )
        """
    )
    connection.commit()
    return connection


def clean_body(text: str) -> str:
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    text = "".join(ch for ch in text if ch == "\n" or (ch >= " " and ch != "\x7f"))
    lines = [line.strip() for line in text.split("\n")]
    text = "\n".join(lines).strip()
    while "\n\n\n" in text:
        text = text.replace("\n\n\n", "\n\n")
    return text


def list_posts(path: Path) -> list[dict]:
    with connect(path) as connection:
        rows = connection.execute(
            "SELECT id, body, created_at FROM announcements ORDER BY id DESC"
        ).fetchall()
    return [{"id": row["id"], "body": row["body"], "createdAt": row["created_at"]} for row in rows]


def add_post(path: Path, body: str) -> dict:
    cleaned = clean_body(body)
    if not cleaned:
        raise ValueError("empty")
    if len(cleaned) > ANNOUNCEMENT_MAX_CHARS:
        raise ValueError("long")
    with connect(path) as connection:
        count = connection.execute("SELECT COUNT(*) AS n FROM announcements").fetchone()["n"]
        if count >= ANNOUNCEMENT_MAX_POSTS:
            raise ValueError("limit")
        created_at = datetime.now(JOHANNESBURG).isoformat(timespec="seconds")
        cursor = connection.execute(
            "INSERT INTO announcements (body, created_at) VALUES (?, ?)",
            (cleaned, created_at),
        )
        connection.commit()
        post_id = cursor.lastrowid
    return {"id": post_id, "body": cleaned, "createdAt": created_at}


def delete_post(path: Path, post_id: int) -> bool:
    with connect(path) as connection:
        cursor = connection.execute("DELETE FROM announcements WHERE id = ?", (post_id,))
        connection.commit()
        return cursor.rowcount > 0
