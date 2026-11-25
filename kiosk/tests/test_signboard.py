"""Offline checks for the lobby board. Live feeds are opt-in via KIOSK_LIVE=1."""

from __future__ import annotations

import http.client
import http.cookiejar
import json
import os
import re
import tempfile
import threading
import unittest
import urllib.parse
import urllib.request
from pathlib import Path

import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from signboard import news, store, traffic, weather
from signboard.config import TRAFFIC_GRACE_SECONDS, TRAFFIC_TTL_SECONDS
from signboard.http_util import UpstreamError
from signboard.server import serve

ROOT = Path(__file__).resolve().parents[1]


class TrafficTests(unittest.TestCase):
    def test_lookup_interval_is_thirty_minutes(self):
        self.assertEqual(TRAFFIC_TTL_SECONDS, 30 * 60)
        self.assertGreater(TRAFFIC_GRACE_SECONDS, TRAFFIC_TTL_SECONDS)
        js = (ROOT / "static" / "display.js").read_text(encoding="utf-8")
        self.assertIn("var TRAFFIC_MS = 30 * 60 * 1000;", js)
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        self.assertIn("Refreshed every 30 minutes.", readme)
        self.assertNotIn("60 seconds", readme)

    def test_missing_key_does_not_invent_times_or_call_upstream(self):
        def fetch(_url):
            raise AssertionError("upstream was called without a key")

        report = traffic.build_report(fetch=fetch, key="")
        self.assertFalse(report["available"])
        self.assertFalse(report["colours"])
        self.assertEqual(report["reason"], "missing_key")
        self.assertEqual(report["incidents"], [])
        self.assertEqual(report["center"]["latitude"], -26.146)
        self.assertEqual(report["center"]["longitude"], 28.041)
        self.assertEqual(report["zoom"], 12)
        self.assertNotIn("routes", report)
        self.assertNotIn("liveSeconds", json.dumps(report))

    def test_incidents_keep_only_provider_descriptions(self):
        body = {
            "incidents": [
                {
                    "properties": {
                        "magnitudeOfDelay": 3,
                        "from": "M1",
                        "events": [{"description": "stationary traffic"}],
                    }
                },
                {"properties": {"iconCategory": 1, "events": [{}]}},
                {"properties": {"events": [{"description": "  road closed  "}]}},
            ]
        }
        parsed = traffic.parse_incidents(body)
        self.assertEqual(
            [item["text"] for item in parsed],
            ["stationary traffic · M1", "road closed"],
        )
        self.assertNotIn("Accident", json.dumps(parsed))

    def test_failed_lookup_does_not_invent_incidents(self):
        def fetch(_url):
            raise UpstreamError("down")

        report = traffic.build_report(fetch=fetch, key="test-key")
        self.assertFalse(report["colours"])
        self.assertEqual(report["incidents"], [])
        self.assertNotIn("test-key", json.dumps(report))

    def test_working_key_returns_colours_and_real_incidents_only(self):
        seen = []

        def fetch(url):
            seen.append(url)
            return {
                "incidents": [
                    {"properties": {"events": [{"description": "slow traffic"}], "from": "Oxford Rd"}}
                ]
            }

        report = traffic.build_report(fetch=fetch, key="test-key")
        self.assertTrue(report["colours"])
        self.assertEqual(report["incidents"], [{"text": "slow traffic · Oxford Rd"}])
        self.assertEqual(len(seen), 1)
        self.assertIn("incidentDetails", seen[0])
        self.assertNotIn("test-key", json.dumps(report))
        west, south, east, north = traffic.view_bbox()
        self.assertLess(west, 28.041)
        self.assertGreater(east, 28.041)
        self.assertLess(south, -26.146)
        self.assertGreater(north, -26.146)
        self.assertLess(east - west, 0.5)
        self.assertLess(north - south, 0.5)

    def test_tile_proxy_is_limited_to_the_rosebank_view(self):
        x0, y0, x1, y1 = traffic.tile_bounds()
        self.assertTrue(traffic.valid_tile(12, x0, y0))
        self.assertTrue(traffic.valid_tile(12, x1, y1))
        self.assertFalse(traffic.valid_tile(11, x0, y0))
        self.assertFalse(traffic.valid_tile(12, 0, 0))
        self.assertLess(x1 - x0, 8)
        self.assertLess(y1 - y0, 8)


class WeatherTests(unittest.TestCase):
    def test_parses_two_cities_and_the_next_hours(self):
        def place(lat, lon, temp, code):
            return {
                "latitude": lat,
                "longitude": lon,
                "current": {"time": "2026-11-24T17:15", "temperature_2m": temp, "weather_code": code, "wind_speed_10m": 9.2},
                "hourly": {
                    "time": ["2026-11-24T17:00", "2026-11-24T18:00", "2026-11-24T19:00"],
                    "temperature_2m": [23.8, 22.4, 21.2],
                    "weather_code": [1, 81, 61],
                    "precipitation_probability": [10, 40, 20],
                },
                "daily": {
                    "time": ["2026-11-24", "2026-11-25"],
                    "weather_code": [81, 3],
                    "temperature_2m_max": [25.2, 24.4],
                    "temperature_2m_min": [14.6, 15.1],
                },
            }

        report = weather.parse_forecast(
            [
                place(-25.75, 28.23, 24.3, 51),
                place(-26.20, 28.04, 23.6, 1),
            ]
        )
        self.assertTrue(report["available"])
        self.assertEqual([item["name"] for item in report["places"]], ["Johannesburg", "Pretoria"])
        johannesburg = report["places"][0]
        self.assertEqual(johannesburg["temperature"], 24)
        self.assertEqual(johannesburg["condition"], "Mainly clear")
        self.assertEqual(johannesburg["hours"][0]["time"], "18:00")
        self.assertEqual(johannesburg["hours"][0]["rainPercent"], 40)
        self.assertEqual(johannesburg["days"][0]["high"], 25)
        self.assertEqual(johannesburg["days"][1]["label"], "Tomorrow")

    def test_upstream_failure_marks_both_cities(self):
        def fetch(_url):
            raise UpstreamError("down")

        report = weather.build_report(fetch=fetch)
        self.assertFalse(report["available"])
        self.assertEqual(len(report["places"]), 2)
        self.assertTrue(all(not place["available"] for place in report["places"]))


class NewsTests(unittest.TestCase):
    def test_ewn_payload_and_sky_rss_interleave(self):
        payload = json.dumps(
            [
                "Older local story",
                "Newer local story",
                "A short local summary",
                "/2026/11/24/newer",
                "/2026/11/23/older",
                {"iso8601": 6},
                "2026-11-24T12:00:00Z",
                {"iso8601": 8},
                "2026-11-23T08:00:00Z",
                {"headline": 1, "subline": 2, "path": 3, "publishedAt": 5},
                {"headline": 0, "subline": 2, "path": 4, "publishedAt": 7},
            ]
        ).encode()
        rss = b"""<?xml version="1.0"?>
        <rss version="2.0"><channel>
          <item>
            <title>World desk &amp; overnight</title>
            <link>https://news.sky.com/story/example</link>
            <description>&lt;p&gt;A short international summary.&lt;/p&gt;</description>
            <pubDate>Tue, 24 Nov 2026 12:12:00 +0000</pubDate>
          </item>
        </channel></rss>"""

        def fetch(url):
            if url == news.EWN_PAYLOAD_URL:
                return payload
            if url == news.SKY_RSS_URL:
                return rss
            raise AssertionError(url)

        report = news.build_report(fetch=fetch)
        self.assertTrue(report["available"])
        self.assertFalse(report["partial"])
        self.assertEqual(
            [story["title"] for story in report["stories"]],
            ["Newer local story", "World desk & overnight", "Older local story"],
        )
        self.assertEqual(report["stories"][1]["summary"], "A short international summary.")
        self.assertEqual(report["cycleSeconds"], 18)

    def test_sky_still_shows_when_ewn_fails(self):
        rss = b"""<?xml version="1.0"?><rss><channel>
          <item><title>International headline</title><link>https://news.sky.com/story/1</link></item>
        </channel></rss>"""

        def fetch(url):
            if url == news.SKY_RSS_URL:
                return rss
            raise UpstreamError("ewn down")

        report = news.build_report(fetch=fetch)
        self.assertTrue(report["available"])
        self.assertTrue(report["partial"])
        self.assertEqual([story["source"] for story in report["stories"]], ["Sky News"])
        self.assertIn("ewn", report["errors"])

    def test_both_sources_down_is_unavailable(self):
        def fetch(_url):
            raise UpstreamError("down")

        report = news.build_report(fetch=fetch)
        self.assertFalse(report["available"])
        self.assertEqual(report["stories"], [])


class StoreTests(unittest.TestCase):
    def test_new_database_has_no_posts(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "announcements.db"
            self.assertEqual(store.list_posts(path), [])
            post = store.add_post(path, "  Lifts out of service on the north bank.  ")
            self.assertEqual(store.list_posts(path), [post])
            self.assertTrue(store.delete_post(path, post["id"]))
            self.assertEqual(store.list_posts(path), [])

    def test_rejects_blank_and_overlong_notices(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "announcements.db"
            with self.assertRaises(ValueError):
                store.add_post(path, "   ")
            with self.assertRaises(ValueError):
                store.add_post(path, "x" * 181)


class ServerTests(unittest.TestCase):
    def setUp(self):
        self._env = {
            "KIOSK_ADMIN_PASSWORD": os.environ.get("KIOSK_ADMIN_PASSWORD"),
            "TOMTOM_API_KEY": os.environ.get("TOMTOM_API_KEY"),
        }
        os.environ["KIOSK_ADMIN_PASSWORD"] = "correct-horse"
        os.environ.pop("TOMTOM_API_KEY", None)
        self.tmp = tempfile.TemporaryDirectory()
        self.httpd = serve("127.0.0.1", 0, Path(self.tmp.name) / "announcements.db")
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        self.thread.start()
        self.port = self.httpd.server_address[1]
        self.base = f"http://127.0.0.1:{self.port}"
        jar = http.cookiejar.CookieJar()
        self.opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(jar))

    def tearDown(self):
        self.httpd.shutdown()
        self.httpd.server_close()
        self.tmp.cleanup()
        for key, value in self._env.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value

    def _open(self, path, data=None):
        request = urllib.request.Request(self.base + path, data=data)
        with self.opener.open(request, timeout=5) as response:
            return response.status, response.read().decode()

    def test_display_has_four_bands_and_no_pointer(self):
        status, html = self._open("/")
        self.assertEqual(status, 200)
        for band in ("band-traffic", "band-weather", "band-news", "band-announcements"):
            self.assertIn(f'id="{band}"', html)
        self.assertIn("Johannesburg Traffic", html)
        self.assertIn("Current Weather", html)
        self.assertIn("Company Events", html)
        self.assertIn('src="/static/standard-bank-logo.svg"', html)
        self.assertIn('alt="Standard Bank"', html)
        self.assertNotIn("calendar-day", html)
        logo = ROOT / "static" / "standard-bank-logo.svg"
        self.assertTrue(logo.is_file())
        self.assertIn("<svg", logo.read_text(encoding="utf-8"))
        self.assertIn(">News<", html)
        css = (ROOT / "static" / "display.css").read_text(encoding="utf-8")
        js = (ROOT / "static" / "display.js").read_text(encoding="utf-8")
        self.assertIn("cursor: none", css)
        self.assertIn("#0033A1", css)
        self.assertNotIn("#c62828", css)
        self.assertIn('grid-template-areas:', css)
        self.assertIn('"traffic weather"', css)
        self.assertIn('"events news"', css)
        self.assertNotIn("innerHTML", js)

    def test_traffic_endpoint_stays_empty_without_a_key(self):
        status, body = self._open("/api/traffic")
        self.assertEqual(status, 200)
        payload = json.loads(body)
        self.assertFalse(payload["available"])
        self.assertFalse(payload["colours"])
        self.assertEqual(payload["reason"], "missing_key")
        self.assertEqual(payload["incidents"], [])
        self.assertNotIn("liveSeconds", body)
        self.assertNotIn("routes", payload)

    def test_announcements_round_trip_is_shared_and_escaped(self):
        status, body = self._open("/api/announcements")
        self.assertEqual(json.loads(body)["posts"], [])
        _status, denied = self._open(
            "/admin/login", urllib.parse.urlencode({"password": "wrong"}).encode()
        )
        self.assertIn("That password is not right.", denied)
        self.assertNotIn("New notice", denied)

        self._open("/admin/login", urllib.parse.urlencode({"password": "correct-horse"}).encode())
        _status, page = self._open("/admin")
        csrf = re.search(r'name="csrf" value="([^"]+)"', page).group(1)
        notice = "North stair <script>alert(1)</script> closed"
        self._open(
            "/admin/announcements",
            urllib.parse.urlencode({"csrf": csrf, "body": notice}).encode(),
        )
        _status, listed = self._open("/api/announcements")
        posts = json.loads(listed)["posts"]
        self.assertEqual(len(posts), 1)
        self.assertEqual(posts[0]["body"], notice)
        _status, manager = self._open("/admin")
        self.assertIn("North stair &lt;script&gt;alert(1)&lt;/script&gt; closed", manager)
        self.assertNotIn("<script>alert(1)</script>", manager)
        csrf = re.search(r'name="csrf" value="([^"]+)"', manager).group(1)
        self._open(
            f"/admin/announcements/{posts[0]['id']}/delete",
            urllib.parse.urlencode({"csrf": csrf}).encode(),
        )
        _status, cleared = self._open("/api/announcements")
        self.assertEqual(json.loads(cleared)["posts"], [])

    def test_post_without_a_session_is_rejected(self):
        opener = urllib.request.build_opener()
        request = urllib.request.Request(
            self.base + "/admin/announcements",
            data=urllib.parse.urlencode({"body": "should not stick", "csrf": "nope"}).encode(),
        )
        with opener.open(request, timeout=5) as response:
            response.read()
        _status, body = self._open("/api/announcements")
        self.assertEqual(json.loads(body)["posts"], [])

    def test_flow_tiles_stay_off_without_a_key_and_other_zooms_are_rejected(self):
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=5)
        x0, y0, _x1, _y1 = traffic.tile_bounds()
        connection.request("GET", f"/tiles/flow/12/{x0}/{y0}.png")
        response = connection.getresponse()
        self.assertEqual(response.status, 404)
        response.read()
        connection.request("GET", "/tiles/base/3/0/0.png")
        response = connection.getresponse()
        self.assertEqual(response.status, 404)
        response.read()
        connection.request("GET", "/tiles/base/12/0/0.png")
        response = connection.getresponse()
        self.assertEqual(response.status, 404)
        connection.close()

    def test_static_path_cannot_escape(self):
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=5)
        connection.request("GET", "/static/../app.py")
        response = connection.getresponse()
        self.assertEqual(response.status, 404)
        connection.close()
        status, _body = self._open("/static/display.js")
        self.assertEqual(status, 200)


class LiveFeedTests(unittest.TestCase):
    @unittest.skipUnless(os.environ.get("KIOSK_LIVE") == "1", "set KIOSK_LIVE=1 to hit live feeds")
    def test_live_weather_and_news(self):
        forecast = weather.build_report()
        self.assertTrue(forecast["available"])
        available = [place["name"] for place in forecast["places"] if place.get("available")]
        self.assertEqual(available, ["Johannesburg", "Pretoria"])
        for place in forecast["places"]:
            self.assertIsInstance(place["temperature"], int)
        bulletin = news.build_report()
        self.assertTrue(bulletin["available"])
        sources = {story["source"] for story in bulletin["stories"]}
        self.assertIn("EWN", sources)
        self.assertIn("Sky News", sources)
        for story in bulletin["stories"]:
            self.assertGreater(len(story["title"].strip()), 8)
            self.assertTrue(story["link"].startswith("https://"))


if __name__ == "__main__":
    unittest.main()
