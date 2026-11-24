"""One HTTP server for both floor stands. The display is `/`. Admin is `/admin`."""

from __future__ import annotations

import json
import sqlite3
import threading
import time
import urllib.parse
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from . import news, store, traffic, weather
from .config import (
    ANNOUNCEMENT_MAX_CHARS,
    ANNOUNCEMENT_MAX_POSTS,
    ANNOUNCEMENT_POLL_SECONDS,
    JOHANNESBURG,
    NEWS_GRACE_SECONDS,
    NEWS_TTL_SECONDS,
    STATIC,
    TEMPLATES,
    TRAFFIC_GRACE_SECONDS,
    TRAFFIC_TTL_SECONDS,
    WEATHER_GRACE_SECONDS,
    WEATHER_TTL_SECONDS,
    admin_password,
    cookie_secure,
    database_path,
)
from .sessions import Sessions, password_matches

STATIC_TYPES = {
    ".css": "text/css; charset=utf-8",
    ".js": "text/javascript; charset=utf-8",
    ".svg": "image/svg+xml",
    ".woff2": "font/woff2",
}

NOTICES = {
    "posted": "Posted. Both screens will show it within 30 seconds.",
    "removed": "Removed.",
}
ERRORS = {
    "auth": "That password is not right.",
    "unconfigured": "Set KIOSK_ADMIN_PASSWORD on the server, then try again.",
    "empty": "Write a notice before posting.",
    "long": f"Keep the notice to {ANNOUNCEMENT_MAX_CHARS} characters.",
    "limit": f"Remove an older notice before adding another. The board holds {ANNOUNCEMENT_MAX_POSTS}.",
    "csrf": "The form expired. Try again.",
    "locked": "Too many attempts. Wait a few minutes.",
}


class FeedCache:
    """Serve a cached payload, and keep the previous one if a refresh fails."""

    def __init__(self, ttl: float, grace: float, produce) -> None:
        self.ttl = ttl
        self.grace = grace
        self.produce = produce
        self._lock = threading.Lock()
        self._payload = None
        self._stored_at = 0.0

    def get(self) -> dict:
        with self._lock:
            now = time.monotonic()
            fresh_enough = self._payload is not None and now - self._stored_at < self.ttl
            if fresh_enough:
                return self._payload
            try:
                payload = self.produce()
            except Exception:
                payload = None
            if isinstance(payload, dict) and payload.get("available"):
                payload = dict(payload)
                payload["stale"] = False
                payload["fetchedAt"] = _now()
                self._payload = payload
                self._stored_at = now
                return payload
            if self._payload is not None and now - self._stored_at < self.grace:
                stale = dict(self._payload)
                stale["stale"] = True
                return stale
            if isinstance(payload, dict):
                failed = dict(payload)
                failed.setdefault("fetchedAt", _now())
                failed["stale"] = False
                return failed
            return {"available": False, "message": "Unavailable", "fetchedAt": _now(), "stale": False}


def _now() -> str:
    return datetime.now(JOHANNESBURG).isoformat(timespec="seconds")


class App:
    def __init__(self, db_path: Path | None = None) -> None:
        self.db_path = db_path or database_path()
        store.connect(self.db_path).close()
        self.sessions = Sessions()
        self.traffic = FeedCache(TRAFFIC_TTL_SECONDS, TRAFFIC_GRACE_SECONDS, traffic.build_report)
        self.weather = FeedCache(WEATHER_TTL_SECONDS, WEATHER_GRACE_SECONDS, weather.build_report)
        self.news = FeedCache(NEWS_TTL_SECONDS, NEWS_GRACE_SECONDS, news.build_report)


def create_handler(app: App):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.0"

        def log_message(self, fmt: str, *args) -> None:
            path = self.path.split("?", 1)[0]
            print(f"{self.command} {path}", flush=True)

        def do_GET(self) -> None:
            path = urllib.parse.urlparse(self.path).path
            if path == "/":
                self._file(TEMPLATES / "display.html", "text/html; charset=utf-8")
                return
            if path == "/admin":
                self._admin_page()
                return
            if path == "/api/traffic":
                self._json(200, app.traffic.get())
                return
            if path == "/api/weather":
                self._json(200, app.weather.get())
                return
            if path == "/api/news":
                self._json(200, app.news.get())
                return
            if path == "/api/announcements":
                self._json(200, self._announcement_payload())
                return
            if path == "/favicon.ico":
                self._empty(204)
                return
            if path.startswith("/static/"):
                self._static(path[len("/static/") :])
                return
            self._empty(404)

        def do_POST(self) -> None:
            path = urllib.parse.urlparse(self.path).path
            form = self._form()
            if path == "/admin/login":
                self._login(form)
                return
            if path == "/admin/logout":
                self._logout(form)
                return
            if path == "/admin/announcements":
                self._add_announcement(form)
                return
            prefix = "/admin/announcements/"
            if path.startswith(prefix) and path.endswith("/delete"):
                raw_id = path[len(prefix) : -len("/delete")]
                if raw_id.isdigit():
                    self._delete_announcement(form, int(raw_id))
                    return
            self._empty(404)

        def _session_token(self) -> str | None:
            header = self.headers.get("Cookie", "")
            for part in header.split(";"):
                name, _, value = part.strip().partition("=")
                if name == "kiosk_session" and value:
                    return value
            return None

        def _form(self) -> dict[str, str]:
            length = int(self.headers.get("Content-Length", "0") or "0")
            if length < 0 or length > 8192:
                return {}
            raw = self.rfile.read(length) if length else b""
            parsed = urllib.parse.parse_qs(raw.decode("utf-8", errors="replace"), keep_blank_values=True)
            return {key: values[0] if values else "" for key, values in parsed.items()}

        def _client_ip(self) -> str:
            return self.client_address[0] if self.client_address else ""

        def _redirect(self, location: str, cookie: str | None = None, clear_cookie: bool = False) -> None:
            self.send_response(303)
            self.send_header("Location", location)
            self.send_header("Cache-Control", "no-store")
            if cookie is not None:
                self.send_header("Set-Cookie", _session_cookie(cookie, clear=False))
            if clear_cookie:
                self.send_header("Set-Cookie", _session_cookie("", clear=True))
            self.send_header("Content-Length", "0")
            self.end_headers()

        def _login(self, form: dict[str, str]) -> None:
            ip = self._client_ip()
            if not app.sessions.allow_attempt(ip):
                self._redirect("/admin?error=locked")
                return
            expected = admin_password()
            if not expected:
                self._redirect("/admin?error=unconfigured")
                return
            if not password_matches(form.get("password", ""), expected):
                app.sessions.note_failure(ip)
                self._redirect("/admin?error=auth")
                return
            app.sessions.clear_failures(ip)
            token, _csrf = app.sessions.create()
            self._redirect("/admin", cookie=token)

        def _require_session(self, form: dict[str, str]) -> str | None:
            token = self._session_token()
            if app.sessions.read(token) is None:
                self._redirect("/admin?error=auth")
                return None
            if not app.sessions.csrf_ok(token, form.get("csrf", "")):
                self._redirect("/admin?error=csrf")
                return None
            return token

        def _logout(self, form: dict[str, str]) -> None:
            token = self._require_session(form)
            if token is None:
                return
            app.sessions.destroy(token)
            self._redirect("/admin", clear_cookie=True)

        def _add_announcement(self, form: dict[str, str]) -> None:
            if self._require_session(form) is None:
                return
            try:
                store.add_post(app.db_path, form.get("body", ""))
            except ValueError as exc:
                code = str(exc)
                if code not in {"empty", "long", "limit"}:
                    code = "empty"
                self._redirect(f"/admin?error={code}")
                return
            self._redirect("/admin?notice=posted")

        def _delete_announcement(self, form: dict[str, str], post_id: int) -> None:
            if self._require_session(form) is None:
                return
            store.delete_post(app.db_path, post_id)
            self._redirect("/admin?notice=removed")

        def _announcement_payload(self) -> dict:
            try:
                posts = store.list_posts(app.db_path)
            except sqlite3.Error:
                return {
                    "available": False,
                    "message": "Announcements unavailable",
                    "posts": [],
                    "pollSeconds": ANNOUNCEMENT_POLL_SECONDS,
                    "fetchedAt": _now(),
                }
            return {
                "available": True,
                "message": None,
                "posts": posts,
                "pollSeconds": ANNOUNCEMENT_POLL_SECONDS,
                "fetchedAt": _now(),
            }

        def _admin_page(self) -> None:
            query = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
            notice = NOTICES.get(_first(query, "notice"), "")
            error = ERRORS.get(_first(query, "error"), "")
            token = self._session_token()
            session = app.sessions.read(token)
            if session is None:
                body = _login_html(error or notice, configured=bool(admin_password()))
            else:
                posts = store.list_posts(app.db_path)
                body = _manager_html(posts, session["csrf"], notice, error)
            page = (TEMPLATES / "admin.html").read_text(encoding="utf-8").replace("{{BODY}}", body)
            self._bytes(200, page.encode("utf-8"), "text/html; charset=utf-8")

        def _static(self, relative: str) -> None:
            root = STATIC.resolve()
            candidate = (STATIC / relative).resolve()
            try:
                candidate.relative_to(root)
            except ValueError:
                self._empty(404)
                return
            if not candidate.is_file():
                self._empty(404)
                return
            content_type = STATIC_TYPES.get(candidate.suffix.lower())
            if content_type is None:
                self._empty(404)
                return
            self._file(candidate, content_type)

        def _file(self, path: Path, content_type: str) -> None:
            if not path.is_file():
                self._empty(404)
                return
            self._bytes(200, path.read_bytes(), content_type)

        def _json(self, status: int, payload: dict) -> None:
            self._bytes(
                status,
                json.dumps(payload, ensure_ascii=False).encode("utf-8"),
                "application/json; charset=utf-8",
            )

        def _bytes(self, status: int, body: bytes, content_type: str) -> None:
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("Referrer-Policy", "no-referrer")
            if content_type.startswith("text/html"):
                self.send_header("Content-Security-Policy", "default-src 'self'; style-src 'self'; script-src 'self'")
            self.end_headers()
            self.wfile.write(body)

        def _empty(self, status: int) -> None:
            self.send_response(status)
            self.send_header("Content-Length", "0")
            self.send_header("Cache-Control", "no-store")
            self.end_headers()

    return Handler


def _first(query: dict, key: str) -> str:
    values = query.get(key) or []
    return values[0] if values else ""


def _session_cookie(token: str, clear: bool) -> str:
    parts = [
        f"kiosk_session={token}",
        "HttpOnly",
        "SameSite=Lax",
        "Path=/",
        "Max-Age=0" if clear else "Max-Age=43200",
    ]
    if cookie_secure():
        parts.append("Secure")
    return "; ".join(parts)


def _when(iso: str) -> str:
    try:
        stamp = datetime.fromisoformat(iso)
    except ValueError:
        return iso
    return stamp.strftime("%d %b %Y, %H:%M")


def _escape(text: str) -> str:
    return (
        text.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
    )


def _login_html(message: str, configured: bool) -> str:
    banner = f'<p class="banner" role="status">{_escape(message)}</p>' if message else ""
    if not configured:
        return (
            banner
            + '<p class="banner">Set KIOSK_ADMIN_PASSWORD on the server before signing in. '
            "The password is not stored in the repo.</p>"
        )
    return (
        banner
        + """
        <form method="post" action="/admin/login">
          <label for="password">Password</label>
          <input id="password" name="password" type="password" autocomplete="current-password" required>
          <button type="submit">Sign in</button>
        </form>
        """
    )


def _manager_html(posts: list[dict], csrf: str, notice: str, error: str) -> str:
    message = notice or error
    banner = f'<p class="banner" role="status">{_escape(message)}</p>' if message else ""
    items = []
    for post in posts:
        items.append(
            "<li>"
            f'<p>{_escape(post["body"])}</p>'
            f'<p class="meta">{_escape(_when(post["createdAt"]))}</p>'
            f'<form method="post" action="/admin/announcements/{post["id"]}/delete">'
            f'<input type="hidden" name="csrf" value="{_escape(csrf)}">'
            '<button type="submit">Remove</button>'
            "</form>"
            "</li>"
        )
    listing = "<ul class=\"posts\">" + "".join(items) + "</ul>" if items else '<p class="empty">No announcements yet.</p>'
    return f"""
        {banner}
        <form method="post" action="/admin/announcements">
          <input type="hidden" name="csrf" value="{_escape(csrf)}">
          <label for="body">New notice</label>
          <textarea id="body" name="body" maxlength="{ANNOUNCEMENT_MAX_CHARS}" rows="4" required></textarea>
          <p class="meta">Up to {ANNOUNCEMENT_MAX_CHARS} characters. Shown on both screens.</p>
          <button type="submit">Post</button>
        </form>
        {listing}
        <form method="post" action="/admin/logout" class="logout">
          <input type="hidden" name="csrf" value="{_escape(csrf)}">
          <button type="submit">Sign out</button>
        </form>
    """


def serve(host: str, port: int, db_path: Path | None = None) -> ThreadingHTTPServer:
    app = App(db_path)
    server = ThreadingHTTPServer((host, port), create_handler(app))
    server.daemon_threads = True
    return server


def main() -> None:
    import os

    from .config import load_dotenv

    load_dotenv()
    host = os.environ.get("KIOSK_HOST", "0.0.0.0")
    port = int(os.environ.get("PORT", "8080"))
    httpd = serve(host, port)
    print(f"Kiosk display http://{host}:{port}/", flush=True)
    print(f"Announcements http://{host}:{port}/admin", flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("Stopping", flush=True)
    finally:
        httpd.server_close()
