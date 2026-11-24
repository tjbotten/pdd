"""Small HTTP helper. Upstream errors stay here so callers can degrade one band."""

from __future__ import annotations

import json
import urllib.error
import urllib.request

USER_AGENT = "RosebankKiosk/1.0 (lobby display)"


class UpstreamError(Exception):
    """An upstream call failed. The message is safe to log; it has no secrets."""


def get_bytes(url: str, timeout: float = 8, headers: dict | None = None) -> bytes:
    request_headers = {"User-Agent": USER_AGENT, "Accept": "*/*"}
    if headers:
        request_headers.update(headers)
    request = urllib.request.Request(url, headers=request_headers)
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return response.read()
    except urllib.error.HTTPError as exc:
        raise UpstreamError(f"HTTP {exc.code}") from exc
    except urllib.error.URLError as exc:
        raise UpstreamError("connection failed") from exc
    except TimeoutError as exc:
        raise UpstreamError("timed out") from exc


def get_json(url: str, timeout: float = 8, headers: dict | None = None):
    raw = get_bytes(url, timeout=timeout, headers=headers)
    try:
        return json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise UpstreamError("response was not JSON") from exc


def post_json(url: str, payload: dict, timeout: float = 8, headers: dict | None = None):
    body = json.dumps(payload).encode("utf-8")
    request_headers = {
        "User-Agent": USER_AGENT,
        "Accept": "application/json",
        "Content-Type": "application/json",
    }
    if headers:
        request_headers.update(headers)
    request = urllib.request.Request(url, data=body, headers=request_headers)
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            raw = response.read()
    except urllib.error.HTTPError as exc:
        raise UpstreamError(f"HTTP {exc.code}") from exc
    except urllib.error.URLError as exc:
        raise UpstreamError("connection failed") from exc
    except TimeoutError as exc:
        raise UpstreamError("timed out") from exc
    try:
        return json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise UpstreamError("response was not JSON") from exc
