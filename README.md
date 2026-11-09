# Zone-buffer serial sniffers

Listen-only firmware for the two serial taps on a zone buffer. Flash the **same image** to both ESP32-C3 Super Mini boards. The boards differ only by a role, **TX** or **RX**, chosen in the web UI and stored on the board.

Each board listens on UART RX. It never drives a TX pin, so it cannot feed data back onto the RS232 line.

## Hardware

Both sides are identical:

- BDD ESP32-C3 Super Mini (ESP32-C3, 4 MB flash, native USB-C)
- RS232-to-TTL adapter, powered so its TTL output is **3.3 V**
- USB-C cable for power and flashing

The Super Mini is not 5 V tolerant. If the adapter is powered from 5 V and its TTL TX idles or swings at 5 V, it can damage GPIO20. Power the adapter from the board's **3V3** pin when the module allows it (most MAX3232 modules do). Share one ground across the ESP32, the adapter, and the cable you are tapping.

GPIO8 is the onboard blue LED and GPIO9 is the BOOT button. Leave both free. GPIO18 and GPIO19 are the USB data pins.

## Wiring

The sniffer only receives. One board taps the conductor that carries the TX side of the link. The other taps the conductor that carries the RX side. Set each board's role to match the conductor it is actually hearing.

```
  Device A TX  ---- RS232 wire ----+---- Device B RX
                                   |
                                   +---- adapter RS232 input (RIN / RS232 RX)

  adapter TTL TX  --------------->  ESP32 GPIO20 (UART RX)
  adapter TTL RX                  leave open
  adapter RS232 output (TOUT)     leave open — do not tie it onto the bus
  adapter GND      --------------  ESP32 GND
  adapter VCC      --------------  ESP32 3V3 (only if the module is happy at 3.3 V)

  ESP32 GPIO21                    leave open
```

| Super Mini pin | Connect to | Direction |
| --- | --- | --- |
| GPIO20 (silk RX / 20) | RS232 adapter TTL TX | Adapter drives, ESP listens |
| GPIO21 (silk TX / 21) | nothing | Must stay unconnected |
| GND | adapter ground and the tapped link's ground | common |
| 3V3 | adapter VCC, if the adapter runs at 3.3 V | power |
| 5V | do not use for the TTL side | USB 5 V only |
| USB-C | computer or a 5 V supply | power and flashing |

GPIO21 is the chip's UART0 TX pad. The ROM can still wiggle it during reset, before this firmware runs, which is why it must not touch the adapter. After boot the firmware forces GPIO21 back to a floating input and never assigns a UART TX, RTS, or CTS pin.

Adapter silkscreens disagree about which pin is "TX". Use the pin that **outputs TTL** from the RS232 receiver. If connecting the sniffer makes the real link start framing badly, the adapter's RS232 driver is on the bus and has to come off.

The UART is not inverted. A normal RS232-to-TTL module already undoes the RS232 levels.

## Build and flash

[PlatformIO](https://platformio.org/) is the supported path. From this directory:

```bash
pip install platformio
pio run -e esp32-c3-supermini
pio run -e esp32-c3-supermini -t upload
pio device monitor
```

The environment `esp32-c3-supermini` uses the ESP32-C3 DevKitM-1 definition (the Super Mini is the same chip and 4 MB flash) with native USB CDC turned on and flash mode DIO. `Serial` logs come out the USB-C port, not GPIO21.

If upload cannot find a port, use a data-capable USB-C cable. If the port appears and upload still fails, hold **BOOT**, tap **RST**, release **BOOT**, and upload again.

Flash this image onto both boards. Do not build a second target.

A host-side check of the capture line format and the live-view ring (no hardware required):

```bash
g++ -std=c++17 -Wall -Wextra -Iinclude host/host_test.cpp -o /tmp/zone-sniffer-test
/tmp/zone-sniffer-test
```

## First boot

No home-network name or password is compiled in.

On first boot, or whenever the saved network cannot be joined, the board starts a setup access point:

| | |
| --- | --- |
| SSID | `zone-sniff-` plus the last two bytes of the MAC, for example `zone-sniff-A1B2` |
| Password | `sniffsetup` |
| Address | http://192.168.4.1/ |

The four hex digits are there so the two boards can be told apart while both are in setup mode. The USB serial log prints the exact SSID. A phone usually opens the captive portal on its own; if it does not, browse to http://192.168.4.1/ yourself.

In the page:

1. Set the role to **TX** or **RX**. The hostname defaults to `zone-tx` or `zone-rx` unless you type another one.
2. Enter the home Wi-Fi name and password and save. An open network is allowed; leave the password blank only to keep a password that is already stored.
3. Set the UART if the link is not 9600 8N1.

The board then joins that network. The web UI stays on the station address and on the setup AP (http://192.168.4.1/). mDNS is `http://zone-tx.local/` or `http://zone-rx.local/` once the role is saved. If the join fails, or the station drops and does not return, the setup AP is the fallback. The password above is only for that setup AP.

The UI has no login. Anyone who can join the setup AP, or who can reach the board on the home LAN, can view and download the capture.

## Web UI

The page shows link, role, UART settings, how many bytes have been captured, whether recording is on, NTP, and UART overflow counts.

- **Live view** is the latest traffic, hex and ASCII, whether or not you are recording.
- **Start / Stop** include or exclude new bytes from the downloadable recording. Stop does not delete what you already kept.
- **Clear** wipes the live view, the recording, and the counters.
- **Download recording** saves a text file (`zone-tx-capture.txt` or whatever the hostname is) for someone else to read.

Lines look like this:

```
15320	2026-11-09T08:32:01.234Z	48 45 4C 4C 4F 0D 0A	HELLO..
15880	-	55 AA	U.
```

Columns are milliseconds since boot, UTC time, hex, and ASCII. UTC is `-` until NTP syncs (it tries after the home network connects). A new line starts after 20 ms of idle, a `0x0A` byte, or 16 bytes. Non-printable bytes are `.`.

## How much it keeps

The UI states the limits it is actually using. The build targets are:

| Buffer | Size | What happens when it fills |
| --- | --- | --- |
| UART driver | 16 KiB | Hardware FIFO is copied by an IRAM interrupt so a flash write does not drop a short burst. An overflow increments the counter on the page and drops the bytes already lost. |
| Live view | latest 24 KiB of text | Oldest text scrolls off the page only. |
| Recording RAM | 32 KiB | Spills to flash. |
| Recording flash | up to 2 MiB ring on the LittleFS partition (about 2.4 MiB is reserved; the page shows the usable cap) | Oldest recorded text is discarded. |

Recording starts out stopped after a reboot. A recording already on flash is still there to download until you clear it. The RAM tail is flushed to flash about every 5 seconds, and again when you stop or download, so a reboot loses at most a few seconds.

## LED

The blue LED on GPIO8 is active-low on the common Super Mini:

- Fast blink: recording
- Slow blink: setup AP, not joined to home Wi-Fi
- Short blip every two seconds: joined to home Wi-Fi

## Limits worth knowing

- One image, two roles. There is no TX-only or RX-only build.
- Baud from 300 to 1000000, data bits 5–8, parity N/E/O, stop bits 1, 1.5, or 2. Default **9600 8N1**.
- The passive tap shares the RS232 wire with the real receiver. Keep the adapter's RS232 driver disconnected so the sniffer cannot contend with either end.
- Download is a snapshot. Stop recording first if you want the file to stop growing while it transfers. Bytes that show up during the transfer stay on the board for the next download.
