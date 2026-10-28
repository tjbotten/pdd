# PGS Displays

Borderless Chromium-on-Pi parking bay availability kiosk driven by MQTT.

The Pi is a **subscriber/renderer only** — it never talks to the main database. Counts arrive over MQTT (WebSockets in the browser); labels, icons, level, and branding come from a per-display JSON config.

## Quick local demo

Requirements: Node 18+, a modern browser, and either **Docker** or a local **Mosquitto** install.

```bash
# 1. Install publisher dependency
npm install

# 2. Start Mosquitto (MQTT :1883 + WebSockets :9001)
docker compose up -d
# If Docker is unavailable:
#   mosquitto -c mosquitto/mosquitto.conf
# Or a tiny Node fallback: npm run broker:node

# 3. Serve the static kiosk + config (repo root)
python3 -m http.server 8080
# → http://localhost:8080/kiosk/?cursor=1

# 4. In another terminal, publish sample counts
npm run publish
```

Open the kiosk URL in a tall/narrow window (or DevTools device mode) to approximate the 55" portrait panel. Add `?cursor=1` while developing so the mouse stays visible.

You should see Level B1 rows update as the publisher wanders the counts. Stop the broker to verify the subtle offline indicator and last-known values.

### Screen switch stub

```bash
node scripts/publish-demo.mjs --once --screen welcome
node scripts/publish-demo.mjs --once --screen bay-counts
```

Or open `http://localhost:8080/kiosk/?screen=welcome`.

## Layout

Portrait composition matches the client mockup: black background, level header, GENERAL / DISABLED / EV / WEALTH AND INVESTMENT rows with large digital-style counts, Standard Bank-style footer. Only the counts change live.

## Docs

- [MQTT topic & payload schema](docs/mqtt-schema.md)
- [Pi Chromium kiosk runbook](docs/pi-kiosk-runbook.md)
- Per-display config example: [`config/display-b1.json`](config/display-b1.json)

## Repo map

| Path | Role |
|------|------|
| `kiosk/` | Static web app for Chromium kiosk mode |
| `config/` | Per-display JSON (level, broker, topics, branding) |
| `mosquitto/` | Local demo broker config (WS enabled) |
| `scripts/publish-demo.mjs` | Sample count publisher |
| `docker-compose.yml` | Local Mosquitto |

## Production notes

- Point `broker.url` at the shared LAN Mosquitto WebSocket listener (`wss://` when TLS is enabled).
- Auth, ACLs, TLS, and the DB→MQTT bridge are out of scope for this prototype.
- Alternate welcome/info screens are stubbed via config / MQTT `screen` topic; designs come later case by case.
