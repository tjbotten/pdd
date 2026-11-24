"""In-memory admin sessions. Restarting the process signs everyone out."""

from __future__ import annotations

import hmac
import hashlib
import secrets
import threading
import time

SESSION_SECONDS = 12 * 60 * 60
LOCKOUT_SECONDS = 10 * 60
LOCKOUT_AFTER = 8


def password_matches(given: str, expected: str) -> bool:
    if not expected:
        return False
    return hmac.compare_digest(
        hashlib.sha256(given.encode("utf-8")).digest(),
        hashlib.sha256(expected.encode("utf-8")).digest(),
    )


class Sessions:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._sessions: dict[str, dict] = {}
        self._failures: dict[str, list[float]] = {}

    def create(self) -> tuple[str, str]:
        token = secrets.token_urlsafe(32)
        csrf = secrets.token_urlsafe(32)
        with self._lock:
            self._sessions[token] = {"csrf": csrf, "created": time.time()}
        return token, csrf

    def read(self, token: str | None) -> dict | None:
        if not token:
            return None
        with self._lock:
            session = self._sessions.get(token)
            if session is None:
                return None
            if time.time() - session["created"] > SESSION_SECONDS:
                self._sessions.pop(token, None)
                return None
            return dict(session)

    def destroy(self, token: str | None) -> None:
        if not token:
            return
        with self._lock:
            self._sessions.pop(token, None)

    def csrf_ok(self, token: str | None, provided: str) -> bool:
        session = self.read(token)
        if session is None or not provided:
            return False
        return hmac.compare_digest(session["csrf"], provided)

    def allow_attempt(self, ip: str) -> bool:
        now = time.time()
        with self._lock:
            recent = [stamp for stamp in self._failures.get(ip, []) if now - stamp < LOCKOUT_SECONDS]
            self._failures[ip] = recent
            return len(recent) < LOCKOUT_AFTER

    def note_failure(self, ip: str) -> None:
        with self._lock:
            self._failures.setdefault(ip, []).append(time.time())

    def clear_failures(self, ip: str) -> None:
        with self._lock:
            self._failures.pop(ip, None)
