"""Local and international headlines.

Sky News publishes an RSS feed. EWN's old RSS address
(https://ewn.co.za/RSS%20Feeds/Latest%20News) now returns 404, and the current
site has no RSS autodiscovery link. The local stories come from the public
article list the EWN homepage itself renders:

    https://www.ewn.co.za/_payload.json

Headlines are whatever those two sources return. Nothing in here is a sample story.
"""

from __future__ import annotations

import json
import xml.etree.ElementTree as ET
from email.utils import parsedate_to_datetime
from html.parser import HTMLParser
from typing import Callable

from .config import STORY_SECONDS
from .http_util import UpstreamError, get_bytes

EWN_PAYLOAD_URL = "https://www.ewn.co.za/_payload.json"
SKY_RSS_URL = "https://feeds.skynews.com/feeds/rss/world.xml"
EWN_SITE = "https://www.ewn.co.za"
STORY_LIMIT = 8

FetchBytes = Callable[[str], bytes]


class _TextExtractor(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.parts: list[str] = []

    def handle_data(self, data: str) -> None:
        self.parts.append(data)


def plain_text(value: str | None) -> str:
    if not value:
        return ""
    parser = _TextExtractor()
    parser.feed(value)
    parser.close()
    return " ".join("".join(parser.parts).split())


def clip(text: str, limit: int = 220) -> str:
    text = plain_text(text)
    if len(text) <= limit:
        return text
    cut = text[:limit].rsplit(" ", 1)[0].rstrip(".,;:")
    return (cut or text[:limit].rstrip()) + "…"


def _resolve(data: list, value, depth: int = 0, seen: set[int] | None = None):
    """Follow Nuxt's devalue index references, including ShallowReactive wrappers."""
    if depth > 12:
        return value
    if isinstance(value, int) and 0 <= value < len(data):
        trail = seen or set()
        if value in trail:
            return None
        trail = set(trail)
        trail.add(value)
        return _resolve(data, data[value], depth + 1, trail)
    if isinstance(value, dict):
        return {key: _resolve(data, item, depth + 1, set(seen or ())) for key, item in value.items()}
    if (
        isinstance(value, list)
        and len(value) == 2
        and isinstance(value[0], str)
        and isinstance(value[1], int)
    ):
        return _resolve(data, value[1], depth + 1, seen)
    return value


def parse_ewn_payload(payload) -> list[dict]:
    if not isinstance(payload, list):
        raise UpstreamError("EWN payload was not a list")
    stories = []
    seen_paths: set[str] = set()
    for node in payload:
        if not isinstance(node, dict) or "headline" not in node or "path" not in node:
            continue
        headline = _resolve(payload, node.get("headline"))
        path = _resolve(payload, node.get("path"))
        if not isinstance(headline, str) or not isinstance(path, str):
            continue
        headline = plain_text(headline)
        if not headline or not path.startswith("/"):
            continue
        if path in seen_paths:
            continue
        seen_paths.add(path)
        summary = _resolve(payload, node.get("subline")) if "subline" in node else ""
        published = _resolve(payload, node.get("publishedAt")) if "publishedAt" in node else None
        published_at = ""
        if isinstance(published, dict) and isinstance(published.get("iso8601"), str):
            published_at = published["iso8601"]
        stories.append(
            {
                "source": "EWN",
                "kicker": "Local",
                "title": headline,
                "summary": clip(summary if isinstance(summary, str) else ""),
                "link": EWN_SITE + path,
                "publishedAt": published_at,
            }
        )
    stories.sort(key=lambda story: story["publishedAt"], reverse=True)
    if not stories:
        raise UpstreamError("EWN payload had no stories")
    return stories[:STORY_LIMIT]


def parse_rss(xml_bytes: bytes, source: str, kicker: str) -> list[dict]:
    try:
        root = ET.fromstring(xml_bytes)
    except ET.ParseError as exc:
        raise UpstreamError("RSS did not parse") from exc
    channel = root.find("channel")
    if channel is None:
        raise UpstreamError("RSS had no channel")
    stories = []
    for item in channel.findall("item"):
        title = plain_text(item.findtext("title"))
        if not title:
            continue
        link = (item.findtext("link") or "").strip()
        published_at = ""
        raw_date = item.findtext("pubDate")
        if raw_date:
            try:
                published_at = parsedate_to_datetime(raw_date).isoformat()
            except (TypeError, ValueError, IndexError):
                published_at = ""
        stories.append(
            {
                "source": source,
                "kicker": kicker,
                "title": title,
                "summary": clip(item.findtext("description") or ""),
                "link": link,
                "publishedAt": published_at,
            }
        )
        if len(stories) >= STORY_LIMIT:
            break
    if not stories:
        raise UpstreamError("RSS had no stories")
    return stories


def interleave(first: list[dict], second: list[dict]) -> list[dict]:
    mixed = []
    index = 0
    while index < len(first) or index < len(second):
        if index < len(first):
            mixed.append(first[index])
        if index < len(second):
            mixed.append(second[index])
        index += 1
    return mixed


def build_report(fetch: FetchBytes | None = None) -> dict:
    """Return both wires. One failed source does not drop the other."""
    fetcher = fetch or (lambda url: get_bytes(url, timeout=12))
    errors = {}
    ewn: list[dict] = []
    sky: list[dict] = []
    try:
        ewn = parse_ewn_payload(json.loads(fetcher(EWN_PAYLOAD_URL).decode("utf-8")))
    except (UpstreamError, UnicodeDecodeError, ValueError, json.JSONDecodeError):
        errors["ewn"] = "EWN unavailable"
    try:
        sky = parse_rss(fetcher(SKY_RSS_URL), "Sky News", "International")
    except UpstreamError:
        errors["sky"] = "Sky News unavailable"
    stories = interleave(ewn, sky)
    return {
        "available": bool(stories),
        "partial": bool(stories) and bool(errors),
        "message": None if stories else "News unavailable",
        "errors": errors,
        "cycleSeconds": STORY_SECONDS,
        "stories": stories,
    }
