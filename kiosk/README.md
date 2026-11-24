# Portrait lobby board

One web page for the two Hikvision DS-D6055UN-D/S 55-inch floor stands. Both open the same URL. The page is laid out for a portrait panel at **1080×1920**.

The screen is a 2×2 board: traffic map, weather, company events, news. A quadrant that cannot load shows a short unavailable line and leaves the others up. The display hides the pointer.

The firmware in the rest of this repo is unrelated. Nothing here is flashed to the ESP32 boards.

## Run it

Python 3.11 or newer. No extra packages.

```bash
cd kiosk
cp .env.example .env
# edit .env — set KIOSK_ADMIN_PASSWORD, and TOMTOM_API_KEY if you want live traffic
python3 app.py
```

Then open:

| | |
| --- | --- |
| Both screens | http://localhost:8080/ |
| Announcements | http://localhost:8080/admin |

Environment variables already exported in the shell override `.env`. `.env` is not committed.

On each floor stand, set the panel to portrait and open the display URL in the browser's kiosk mode (no toolbar). For Chrome:

```bash
google-chrome --kiosk --no-first-run --disable-infobars http://<server>:8080/
```

Turn the panel's screensaver off. Both stands use that same address. Announcements are stored in one SQLite file and show on both.

## Environment

| Variable | Required | What it does |
| --- | --- | --- |
| `KIOSK_ADMIN_PASSWORD` | To post notices | Password for `/admin`. If it is unset, the admin page says so and refuses login. |
| `TOMTOM_API_KEY` | No | Traffic colours and incident lines on the Rosebank map. Without it the map is plain and the colour legend says it is unavailable. No delays or accidents are made up. |
| `KIOSK_DB_PATH` | No | SQLite file. Default `kiosk/data/announcements.db`. |
| `PORT` | No | Default `8080`. |
| `KIOSK_HOST` | No | Default `0.0.0.0`, so both stands on the LAN can connect. |
| `KIOSK_COOKIE_SECURE` | No | Set to `1` when the admin page is served over HTTPS. |

## Quadrants

**Traffic.** A street map centred on Rosebank (about -26.146, 28.041), zoomed to the surrounding suburbs. The base map is [OpenStreetMap](https://www.openstreetmap.org/copyright) and it always shows. Traffic colours are TomTom relative-flow tiles (green light, amber moderate, red heavy). Incident lines under the map are the descriptions TomTom returns for that view, and only those. Refreshed every 30 minutes. One incident lookup every 30 minutes, plus the flow tiles for that view. If a refresh fails, the previous colours stay up until the next lookup. Without `TOMTOM_API_KEY`, or if TomTom rejects the key, the map stays plain and the legend says the colours are unavailable.

**Weather.** Johannesburg and Pretoria from [Open-Meteo](https://open-meteo.com/), which needs no key. Current temperature and conditions, today's high and low, and the next three days. Refreshed every 10 minutes.

**News.** A short list of headlines, mixing EWN and Sky News. The feeds are refetched every 3 minutes.

| | |
| --- | --- |
| Sky News, international | https://feeds.skynews.com/feeds/rss/world.xml |
| EWN, local | https://www.ewn.co.za/_payload.json |

Sky's address is the World feed linked from https://news.sky.com/info/rss. EWN no longer publishes RSS: `https://ewn.co.za/RSS%20Feeds/Latest%20News` returns 404, and the current homepage has no RSS autodiscovery link. `_payload.json` is the public article list the EWN homepage itself renders. Headlines are taken from those responses only.

If one wire fails, the list keeps the other. If both fail, the news quadrant says it is unavailable.

**Company events.** Shared announcements. Add and remove them at `/admin`. The screens poll every 30 seconds and list the current notices. Posts live in SQLite. A new database has no notices. Each notice is at most 180 characters. The calendar graphic shows today's date in Johannesburg. It is not an event.

## Checks

```bash
cd kiosk
python3 -m unittest tests/test_signboard.py
KIOSK_LIVE=1 python3 -m unittest tests.test_signboard.LiveFeedTests
```

The first command stays offline. The second calls Open-Meteo, EWN, and Sky News.
