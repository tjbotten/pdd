"""Johannesburg and Pretoria conditions from Open-Meteo. No API key."""

from __future__ import annotations

from typing import Callable

from .http_util import UpstreamError, get_json

PLACES = (
    {"id": "johannesburg", "name": "Johannesburg", "latitude": -26.2041, "longitude": 28.0473},
    {"id": "pretoria", "name": "Pretoria", "latitude": -25.7479, "longitude": 28.2293},
)

# WMO weather interpretation codes used by Open-Meteo.
WMO_LABELS = {
    0: "Clear",
    1: "Mainly clear",
    2: "Partly cloudy",
    3: "Overcast",
    45: "Fog",
    48: "Rime fog",
    51: "Light drizzle",
    53: "Drizzle",
    55: "Heavy drizzle",
    56: "Freezing drizzle",
    57: "Freezing drizzle",
    61: "Light rain",
    63: "Rain",
    65: "Heavy rain",
    66: "Freezing rain",
    67: "Freezing rain",
    71: "Light snow",
    73: "Snow",
    75: "Heavy snow",
    77: "Snow grains",
    80: "Light showers",
    81: "Showers",
    82: "Heavy showers",
    85: "Snow showers",
    86: "Heavy snow showers",
    95: "Thunderstorm",
    96: "Thunderstorm, hail",
    99: "Thunderstorm, hail",
}

FetchJson = Callable[[str], dict | list]


def forecast_url() -> str:
    latitudes = ",".join(str(place["latitude"]) for place in PLACES)
    longitudes = ",".join(str(place["longitude"]) for place in PLACES)
    return (
        "https://api.open-meteo.com/v1/forecast"
        f"?latitude={latitudes}&longitude={longitudes}"
        "&current=temperature_2m,weather_code,wind_speed_10m"
        "&hourly=temperature_2m,weather_code,precipitation_probability"
        "&daily=weather_code,temperature_2m_max,temperature_2m_min"
        "&timezone=Africa%2FJohannesburg"
        "&forecast_days=2"
        "&wind_speed_unit=kmh"
    )


def condition_label(code) -> str | None:
    if isinstance(code, bool) or not isinstance(code, (int, float)):
        return None
    return WMO_LABELS.get(int(code))


def _round_number(value) -> int | None:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    return int(round(value))


def _nearest_place(record: dict) -> dict | None:
    try:
        latitude = float(record["latitude"])
        longitude = float(record["longitude"])
    except (KeyError, TypeError, ValueError):
        return None
    return min(
        PLACES,
        key=lambda place: (place["latitude"] - latitude) ** 2 + (place["longitude"] - longitude) ** 2,
    )


def _hours_ahead(record: dict) -> list[dict]:
    hourly = record.get("hourly") if isinstance(record, dict) else None
    current = record.get("current") if isinstance(record, dict) else None
    if not isinstance(hourly, dict) or not isinstance(current, dict):
        return []
    times = hourly.get("time") or []
    temperatures = hourly.get("temperature_2m") or []
    codes = hourly.get("weather_code") or []
    chances = hourly.get("precipitation_probability") or []
    current_time = str(current.get("time") or "")
    # "2026-11-24T17:15" -> start at the next clock hour.
    current_hour = current_time[:13] if len(current_time) >= 13 else ""
    upcoming = []
    for index, stamp in enumerate(times):
        if not isinstance(stamp, str) or stamp[:13] <= current_hour:
            continue
        temperature = _round_number(temperatures[index] if index < len(temperatures) else None)
        if temperature is None:
            continue
        chance = chances[index] if index < len(chances) else None
        upcoming.append(
            {
                "time": stamp[11:16],
                "temperature": temperature,
                "condition": condition_label(codes[index] if index < len(codes) else None),
                "rainPercent": _round_number(chance) if not isinstance(chance, bool) else None,
            }
        )
        if len(upcoming) == 4:
            break
    return upcoming


def _day(record: dict, index: int, label: str) -> dict | None:
    daily = record.get("daily") if isinstance(record, dict) else None
    if not isinstance(daily, dict):
        return None
    highs = daily.get("temperature_2m_max") or []
    lows = daily.get("temperature_2m_min") or []
    codes = daily.get("weather_code") or []
    if index >= len(highs) or index >= len(lows):
        return None
    high = _round_number(highs[index])
    low = _round_number(lows[index])
    if high is None or low is None:
        return None
    return {
        "label": label,
        "high": high,
        "low": low,
        "condition": condition_label(codes[index] if index < len(codes) else None),
    }


def parse_place(record: dict, place: dict) -> dict:
    current = record.get("current") if isinstance(record, dict) else None
    if not isinstance(current, dict):
        raise UpstreamError("no current conditions")
    temperature = _round_number(current.get("temperature_2m"))
    if temperature is None:
        raise UpstreamError("no temperature")
    days = [day for day in (_day(record, 0, "Today"), _day(record, 1, "Tomorrow")) if day]
    return {
        "id": place["id"],
        "name": place["name"],
        "available": True,
        "temperature": temperature,
        "condition": condition_label(current.get("weather_code")),
        "windKmh": _round_number(current.get("wind_speed_10m")),
        "days": days,
        "hours": _hours_ahead(record),
    }


def parse_forecast(payload) -> dict:
    records = payload if isinstance(payload, list) else [payload]
    by_id = {}
    for record in records:
        if not isinstance(record, dict):
            continue
        place = _nearest_place(record)
        if place is None or place["id"] in by_id:
            continue
        try:
            by_id[place["id"]] = parse_place(record, place)
        except UpstreamError:
            continue
    places = []
    for place in PLACES:
        if place["id"] in by_id:
            places.append(by_id[place["id"]])
        else:
            places.append({"id": place["id"], "name": place["name"], "available": False})
    return {
        "available": any(item.get("available") for item in places),
        "message": None if any(item.get("available") for item in places) else "Weather unavailable",
        "places": places,
    }


def build_report(fetch: FetchJson | None = None) -> dict:
    fetcher = fetch or get_json
    try:
        payload = fetcher(forecast_url())
    except (UpstreamError, TimeoutError, ValueError):
        return {
            "available": False,
            "message": "Weather unavailable",
            "places": [{"id": place["id"], "name": place["name"], "available": False} for place in PLACES],
        }
    return parse_forecast(payload)
