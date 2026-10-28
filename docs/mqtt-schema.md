# MQTT starter schema (PGS Displays)

Prototype topic map for parking bay availability kiosks. Production ACLs/TLS are deferred; this documents the contract the Chromium kiosk expects.

## Conventions

| Item | Value |
|------|--------|
| Site id | Short slug, e.g. `pgs-demo` |
| Level id | Matches physical level, e.g. `B1` |
| Display id | Unique per physical screen, e.g. `pillar-b1-east` |
| QoS | Prefer QoS 1 |
| Retain | Availability + screen commands should be **retained** so a kiosk reboot shows last known state immediately |
| Transport (kiosk) | MQTT over **WebSockets** (`ws://` or `wss://`) so Chromium can subscribe in-browser |

## Topics

### Level availability (primary)

```
pgs/{siteId}/levels/{levelId}/availability
```

**Example:** `pgs/pgs-demo/levels/B1/availability`

**Payload (JSON object):**

```json
{
  "levelId": "B1",
  "ts": "2026-10-28T16:00:00.000Z",
  "general": 124,
  "disabled": 8,
  "ev": 2,
  "wealth_and_investment": 12
}
```

| Field | Type | Required | Notes |
|-------|------|----------|--------|
| `general` | number | yes* | Free GENERAL bays |
| `disabled` | number | yes* | Free DISABLED bays |
| `ev` | number | yes* | Free EV bays |
| `wealth_and_investment` | number | yes* | Free WEALTH AND INVESTMENT bays |
| `levelId` | string | no | Echo for debugging |
| `ts` | ISO-8601 string | no | Publisher timestamp |
| `counts` | object | no | Optional nested map; same keys as above (forward-compatible) |

\* Keys present in the display’s `bayTypes` config are applied; unknown keys are ignored. Missing keys leave last-known values unchanged.

Bay type ids are **snake_case** and must match `config.bayTypes[].id`.

### Per-display screen switch (stub)

```
pgs/{siteId}/displays/{displayId}/screen
```

**Example:** `pgs/pgs-demo/displays/pillar-b1-east/screen`

**Payload:**

```json
{ "screen": "bay-counts", "ts": "2026-10-28T16:00:00.000Z" }
```

Or a bare string: `"welcome"`.

| `screen` id | Behaviour in prototype |
|-------------|-------------------------|
| `bay-counts` | Primary portrait bay availability UI |
| `welcome` | Stub welcome layout |
| `info` | Stub info layout |

Alternate welcome/info designs are deferred; this topic only proves switching without rewriting the kiosk shell.

## Per-display config

Each physical screen loads a JSON config (see `config/display-b1.json`):

- `displayId`, `siteId`, `levelId`, `levelLabel`
- `broker.url` — WebSocket URL, e.g. `ws://192.168.1.10:9001`
- `topics.availability`, `topics.screen`
- `bayTypes[]` — static labels, colours, icons, zero-padding
- `branding` — logo path + name
- `defaults` — seed counts before first MQTT message / when cache empty
- `screen` — initial screen id (overridable by `?screen=` or MQTT)

Pass a custom file with `?config=../config/display-b1.json`.

## Offline behaviour

- On disconnect/reconnect failure the kiosk keeps **last-known counts** (in-memory + `localStorage`).
- A subtle **Offline** indicator appears in the corner until the broker is reachable again.
- Retained availability messages restore counts quickly after reconnect.

## Local demo broker

`docker compose up -d` runs Mosquitto with:

- MQTT TCP: `1883` (publisher scripts)
- MQTT WebSockets: `9001` (Chromium / kiosk)

Anonymous auth is enabled for local demo only.
