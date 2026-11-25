"""Rosebank traffic map.

The base map is a plain street map and does not need a key. Traffic colours
and the incident lines come from TomTom, and only when TOMTOM_API_KEY is set.
Incident text is copied from the provider's descriptions. Nothing here invents
a delay, a closure, or an accident.
"""

from __future__ import annotations

import math
import urllib.parse
from typing import Callable

from .config import tomtom_api_key
from .http_util import UpstreamError, get_bytes, get_json

# The lobby is in Rosebank. Zoom 12 shows the surrounding suburbs and highways.
CENTER = (-26.146, 28.041)
ZOOM = 12
MAP_WIDTH_PX = 500
MAP_HEIGHT_PX = 640
# The proxy only serves tiles that can appear on this fixed Rosebank view.
TILE_WINDOW_PX = (960, 1200)
INCIDENT_LIMIT = 4

FetchJson = Callable[[str], dict]


def tile_bounds(
    latitude: float = CENTER[0],
    longitude: float = CENTER[1],
    zoom: int = ZOOM,
) -> tuple[int, int, int, int]:
    width, height = TILE_WINDOW_PX
    center_x, center_y = _world_pixel(latitude, longitude, zoom)
    x0 = math.floor((center_x - width / 2) / 256)
    y0 = math.floor((center_y - height / 2) / 256)
    x1 = math.floor((center_x + width / 2 - 1) / 256)
    y1 = math.floor((center_y + height / 2 - 1) / 256)
    limit = 2**zoom
    return max(0, x0), max(0, y0), min(limit - 1, x1), min(limit - 1, y1)


def valid_tile(zoom: int, x: int, y: int) -> bool:
    if zoom != ZOOM:
        return False
    x0, y0, x1, y1 = tile_bounds()
    return x0 <= x <= x1 and y0 <= y <= y1


def base_tile_url(zoom: int, x: int, y: int) -> str:
    return f"https://tile.openstreetmap.org/{zoom}/{x}/{y}.png"


def flow_tile_url(zoom: int, x: int, y: int, key: str) -> str:
    query = urllib.parse.urlencode({"key": key, "thickness": "8"})
    return f"https://api.tomtom.com/traffic/map/4/tile/flow/relative/{zoom}/{x}/{y}.png?{query}"


def _world_pixel(latitude: float, longitude: float, zoom: int) -> tuple[float, float]:
    scale = 256 * (2**zoom)
    sine = math.sin(math.radians(latitude))
    sine = min(0.9999, max(-0.9999, sine))
    x = (longitude + 180.0) / 360.0 * scale
    y = (0.5 - math.log((1 + sine) / (1 - sine)) / (4 * math.pi)) * scale
    return x, y


def _lat_lon(pixel_x: float, pixel_y: float, zoom: int) -> tuple[float, float]:
    scale = 256 * (2**zoom)
    longitude = pixel_x / scale * 360.0 - 180.0
    mercator = math.pi - 2.0 * math.pi * pixel_y / scale
    latitude = math.degrees(math.atan(math.sinh(mercator)))
    return latitude, longitude


def view_bbox(
    latitude: float = CENTER[0],
    longitude: float = CENTER[1],
    zoom: int = ZOOM,
    width: int = MAP_WIDTH_PX,
    height: int = MAP_HEIGHT_PX,
) -> tuple[float, float, float, float]:
    """Return minLon, minLat, maxLon, maxLat for the on-screen map."""
    center_x, center_y = _world_pixel(latitude, longitude, zoom)
    south, west = _lat_lon(center_x - width / 2, center_y + height / 2, zoom)
    north, east = _lat_lon(center_x + width / 2, center_y - height / 2, zoom)
    return west, south, east, north


def incident_url(key: str, bbox: tuple[float, float, float, float]) -> str:
    west, south, east, north = bbox
    query = urllib.parse.urlencode(
        {
            "key": key,
            "bbox": f"{west:.5f},{south:.5f},{east:.5f},{north:.5f}",
            "fields": "{incidents{properties{magnitudeOfDelay,events{description},from,to}}}",
            "language": "en-GB",
            "timeValidityFilter": "present",
        }
    )
    return "https://api.tomtom.com/traffic/services/5/incidentDetails?" + query


def _clean(value) -> str:
    if not isinstance(value, str):
        return ""
    return " ".join(value.split())


def parse_incidents(body) -> list[dict]:
    """Keep only incident sentences the provider actually sent."""
    if not isinstance(body, dict):
        return []
    raw = body.get("incidents")
    if not isinstance(raw, list):
        return []
    found = []
    for feature in raw:
        if not isinstance(feature, dict):
            continue
        props = feature.get("properties")
        if not isinstance(props, dict):
            props = feature
        events = props.get("events")
        descriptions = []
        if isinstance(events, list):
            for event in events:
                if not isinstance(event, dict):
                    continue
                text = _clean(event.get("description"))
                if text and text not in descriptions:
                    descriptions.append(text)
        if not descriptions:
            continue
        sentence = "; ".join(descriptions)
        origin = _clean(props.get("from"))
        if origin:
            sentence = f"{sentence} · {origin}"
        magnitude = props.get("magnitudeOfDelay")
        if isinstance(magnitude, bool) or not isinstance(magnitude, (int, float)):
            magnitude = 0
        found.append((int(magnitude), sentence))
    found.sort(key=lambda item: item[0], reverse=True)
    return [{"text": sentence} for _magnitude, sentence in found[:INCIDENT_LIMIT]]


def _report(colours: bool, reason: str | None, message: str | None, incidents: list[dict]) -> dict:
    return {
        "available": colours,
        "colours": colours,
        "reason": reason,
        "message": message,
        "center": {"latitude": CENTER[0], "longitude": CENTER[1]},
        "zoom": ZOOM,
        "incidents": incidents,
    }


def build_report(fetch: FetchJson | None = None, key: str | None = None) -> dict:
    if key is None:
        key = tomtom_api_key()
    if not key:
        return _report(False, "missing_key", "Traffic colours unavailable", [])
    fetcher = fetch or get_json
    try:
        # A failed call means we will not paint flow tiles either.
        body = fetcher(incident_url(key, view_bbox()))
    except Exception:
        return _report(False, "upstream", "Traffic colours unavailable", [])
    return _report(True, None, None, parse_incidents(body))


def fetch_tile(kind: str, zoom: int, x: int, y: int) -> bytes:
    if kind == "base":
        url = base_tile_url(zoom, x, y)
    elif kind == "flow":
        key = tomtom_api_key()
        if not key:
            raise UpstreamError("no key")
        url = flow_tile_url(zoom, x, y, key)
    else:
        raise UpstreamError("unknown tile")
    data = get_bytes(url, timeout=8, headers={"Accept": "image/png"})
    if not data.startswith(b"\x89PNG"):
        raise UpstreamError("not a png")
    return data
