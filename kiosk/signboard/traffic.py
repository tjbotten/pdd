"""Live driving times from Rosebank via the TomTom Routing API.

TomTom is called only when TOMTOM_API_KEY is set. With no key the band stays
empty: this module never invents a travel time.

Each route asks for every travel-time basis TomTom can return
(computeTravelTimeFor=all):

- travelTimeInSeconds: best estimate for leaving now, including live traffic
- historicTrafficTravelTimeInSeconds: typical conditions for this time
- noTrafficTravelTimeInSeconds: free-flow, as if the roads were empty
"""

from __future__ import annotations

import urllib.parse
from concurrent.futures import ThreadPoolExecutor
from typing import Callable

from .config import tomtom_api_key
from .http_util import UpstreamError, get_json

# Landmarks, not suburb centroids, so the route has a real start and end.
# Checked against OpenStreetMap on 24 Nov 2026.
ROUTES = (
    {
        "id": "sandton",
        "origin_name": "Rosebank",
        "destination_name": "Sandton",
        "origin": (-26.1447122, 28.0434135),  # Oxford Road, Rosebank
        "destination": (-26.1090427, 28.0524052),  # Sandton City
    },
    {
        "id": "centurion",
        "origin_name": "Rosebank",
        "destination_name": "Centurion",
        "origin": (-26.1447122, 28.0434135),
        "destination": (-25.8564676, 28.1869355),  # Centurion Mall
    },
    {
        "id": "roodepoort",
        "origin_name": "Rosebank",
        "destination_name": "Roodepoort",
        "origin": (-26.1447122, 28.0434135),
        "destination": (-26.1386201, 27.8620396),  # Westgate, Roodepoort
    },
)

FetchJson = Callable[[str], dict]


def route_url(key: str, origin: tuple[float, float], destination: tuple[float, float]) -> str:
    locations = f"{origin[0]},{origin[1]}:{destination[0]},{destination[1]}"
    query = urllib.parse.urlencode(
        {
            "key": key,
            "computeTravelTimeFor": "all",
            "traffic": "true",
            "routeType": "fastest",
            "travelMode": "car",
            "routeRepresentation": "summaryOnly",
        }
    )
    return f"https://api.tomtom.com/routing/1/calculateRoute/{locations}/json?{query}"


def _optional_int(value) -> int | None:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    return int(value)


def parse_route_body(body: dict) -> dict:
    """Pull live, typical, and free-flow seconds out of a Calculate Route body."""
    routes = body.get("routes") if isinstance(body, dict) else None
    if not routes:
        raise UpstreamError("no route")
    summary = routes[0].get("summary") if isinstance(routes[0], dict) else None
    if not isinstance(summary, dict):
        raise UpstreamError("no summary")
    live = _optional_int(summary.get("travelTimeInSeconds"))
    if live is None:
        raise UpstreamError("no travel time")
    return {
        "liveSeconds": live,
        "typicalSeconds": _optional_int(summary.get("historicTrafficTravelTimeInSeconds")),
        "freeFlowSeconds": _optional_int(summary.get("noTrafficTravelTimeInSeconds")),
        "distanceMeters": _optional_int(summary.get("lengthInMeters")),
    }


def _empty_route(route: dict) -> dict:
    return {
        "id": route["id"],
        "from": route["origin_name"],
        "to": route["destination_name"],
        "available": False,
    }


def _missing_key_report() -> dict:
    return {
        "available": False,
        "reason": "missing_key",
        "message": "Live times need a TomTom key on the server.",
        "routes": [_empty_route(route) for route in ROUTES],
    }


def build_report(fetch: FetchJson | None = None, key: str | None = None) -> dict:
    if key is None:
        key = tomtom_api_key()
    if not key:
        return _missing_key_report()
    fetcher = fetch or get_json

    def one(route: dict) -> dict:
        url = route_url(key, route["origin"], route["destination"])
        try:
            parsed = parse_route_body(fetcher(url))
        except Exception:
            # Do not log the exception: the request URL contains the API key.
            return _empty_route(route)
        parsed.update(
            {
                "id": route["id"],
                "from": route["origin_name"],
                "to": route["destination_name"],
                "available": True,
            }
        )
        return parsed

    with ThreadPoolExecutor(max_workers=len(ROUTES)) as pool:
        routes = list(pool.map(one, ROUTES))
    any_live = any(route["available"] for route in routes)
    return {
        "available": any_live,
        "reason": None if any_live else "upstream",
        "message": None if any_live else "Travel times unavailable",
        "routes": routes,
    }
