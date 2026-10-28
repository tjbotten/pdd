# Raspberry Pi Chromium kiosk runbook (sketch)

Brief setup notes for a borderless portrait parking display. Adjust paths/user for your image.

## Goals

- Full-screen Chromium, no browser chrome, no desktop clutter
- Cursor hidden; screen blanking / DPMS disabled
- Autostart on boot to the local (or LAN) kiosk URL
- Pi is an MQTT subscriber/renderer only — never talks to the main DB

## Serve the kiosk files

On the Pi (or a LAN static host the Pi can reach):

```bash
# From the repo root — simple static server
python3 -m http.server 8080
# Open: http://localhost:8080/kiosk/?config=../config/display-b1.json
```

Point `config/display-b1.json` → `broker.url` at the shared LAN Mosquitto WebSocket listener, e.g. `ws://192.168.10.20:9001`.

For production, prefer nginx/caddy or copy `kiosk/` + `config/` onto the Pi and serve locally so the panel still boots if the file server is down (MQTT can still be remote).

## Disable blanking / screensaver (labwc / Wayland or X11)

**X11 example** (in `~/.xinitrc` or a session autostart):

```bash
xset s off
xset -dpms
xset s noblank
```

**wayland / labwc:** disable idle blanking in compositor config, or install `wlhope`/`swayidle` policies that never blank the output used by the kiosk.

Also disable any desktop “screen lock” for the kiosk user.

## Chromium kiosk flags

```bash
chromium-browser \
  --kiosk \
  --noerrdialogs \
  --disable-infobars \
  --disable-session-crashed-bubble \
  --disable-restore-session-state \
  --autoplay-policy=no-user-gesture-required \
  --check-for-update-interval=31536000 \
  --disable-features=TranslateUI \
  --no-first-run \
  "http://127.0.0.1:8080/kiosk/?config=../config/display-b1.json"
```

Notes:

- `--kiosk` removes chrome and goes fullscreen.
- Hide the mouse: CSS already sets `cursor: none`; for a hardware cursor blanker you can also run `unclutter -idle 0.1 -root` (X11).
- Force portrait via the panel / OS display rotation (e.g. `display_rotate` / Wayland output transform), not via CSS alone.

Package name may be `chromium` or `chromium-browser` depending on the OS image.

## systemd user service sketch

`~/.config/systemd/user/pgs-kiosk.service`:

```ini
[Unit]
Description=PGS parking display Chromium kiosk
After=graphical-session.target network-online.target
Wants=network-online.target

[Service]
Environment=DISPLAY=:0
ExecStart=/usr/bin/chromium-browser --kiosk --noerrdialogs --disable-infobars --disable-session-crashed-bubble --no-first-run http://127.0.0.1:8080/kiosk/?config=../config/display-b1.json
Restart=on-failure
RestartSec=5

[Install]
WantedBy=graphical-session.target
```

Enable:

```bash
systemctl --user daemon-reload
systemctl --user enable --now pgs-kiosk.service
loginctl enable-linger "$USER"   # optional: start without interactive login
```

Pair with a small `pgs-static.service` that runs `python3 -m http.server 8080` (or nginx) from the deploy directory.

## Checklist

1. Panel orientation = portrait, 55" edge-to-edge
2. Autologin kiosk user to graphical session
3. Blanking / DPMS off
4. Chromium kiosk URL loads display config
5. `broker.url` points at shared Mosquitto WS port
6. Confirm counts update; unplug broker → offline cue + last-known numbers
