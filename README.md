# ESP32-S3 8x32 LED matrix

HubitHab (working title): a desk gadget on four cascaded MAX7219 8x8 LED modules (8x32 pixels)
driven by an ESP32-S3-Zero. It shows your GitHub contribution heatmap and stats, a clock synced over
the internet and a Pomodoro timer. Wi-Fi and GitHub are set up from a phone through a captive portal.
See [ROADMAP.md](ROADMAP.md) for what is done and what comes next.

## Hardware

| Signal | ESP32-S3-Zero |
|---|---|
| MAX7219 `DIN` (MOSI) | GPIO4 |
| MAX7219 `CS` | GPIO5 |
| MAX7219 `CLK` | GPIO6 |
| Button | GPIO0 (BOOT) |
| `VCC` / `GND` | 5V / GND |

Connect to the input header of the panel (marked `IN` / `DIN`), and power the panel from 5V, not
3V3. The ESP32's 3.3 V logic drives the MAX7219 fine in practice.

The modules on this board are rotated by 90 degrees relative to what the stock `max7219` driver
expects. `fb_flush()` in `main/display.c` handles the remapping, so drawing code works with normal
x/y coordinates. Several modules at high brightness need a solid power supply.

## Build and flash

Requires ESP-IDF v5.2.

```sh
. path/to/esp-idf/export.sh
idf.py set-target esp32s3   # first time only
idf.py build flash monitor
```

The partition table (`partitions.csv`) has NVS plus two 1.9 MB app slots for future OTA updates.
Pomodoro lengths are in `idf.py menuconfig` → HubitHab.

## Code layout

| File | Purpose |
|---|---|
| `main/main.c` | Startup, display loop, button routing |
| `main/display.c` | Framebuffer, rotated-module mapping, text scroller, narrow digits |
| `main/modes.c` | GitHub, Clock, Pomodoro and Status modes |
| `main/buttons.c` | BOOT button (click / long press) |
| `main/settings.c` | Settings in NVS |
| `main/portal.c` | Setup access point, captive portal, web form |
| `main/wifi.c` | Station connection, reconnects, SNTP |
| `main/github.c` | GitHub API fetcher (GraphQL / REST) |

## Setup

On first boot (or after holding BOOT for 10 s) the board starts an open Wi-Fi network
`HubitHab-XXXX`, and the display shows its name. Join it from a phone: the setup page opens
automatically (otherwise go to http://192.168.4.1). Enter Wi-Fi, GitHub username, optional
read-only GitHub token and time zone, then save; the board restarts with the new settings.

With saved settings it joins your Wi-Fi and shows its IP address. If the first three attempts fail
(wrong password, network not found) it opens the setup network again with the form pre-filled, and
restarts after 5 minutes to retry if nobody uses it. Later drops are retried in the background.

GitHub data refreshes every 15 minutes. The contribution calendar needs a token: create a
fine-grained token (GitHub → Settings → Developer settings) with public repositories read-only and
no extra permissions. Without a token only followers and public repos are shown.

GitHub mode shows the last 32 weeks as a heatmap: one column per week (oldest on the left), one row
per weekday (Sunday on top), a dot on the bottom row under the week containing the 1st of each month. Today's
pixel blinks until you have contributed. Once a minute the streak, today's count and followers
scroll by. Contributions to organisation repositories are not included with a fine-grained token.

## Usage

- Click BOOT: next mode (GitHub, Clock, Pomodoro, Status).
- Hold BOOT ~1 s: mode action — GitHub: heatmap / stats only, Clock: toggle time / date,
  Pomodoro: start / pause, Status: restart the scroll.
- Hold BOOT 3 s in Pomodoro: reset.
- Hold BOOT 10 s: erase settings and restart into setup.

Pomodoro alternates 25 min work / 5 min break (`idf.py menuconfig` → HubitHab). The bottom row is a
progress bar: it fills during work and empties during a break. When a phase ends the display jumps
to Pomodoro from any mode and flashes.

The clock syncs over the internet (SNTP) and uses the time zone from the setup page. Without Wi-Fi it
starts from the build time; a lit bottom-right pixel means the time has not been synced yet.

Scroll speed is `CONFIG_EXAMPLE_SCROLL_DELAY` (`idf.py menuconfig` → Example configuration).

## Credits

- Drivers from [esp-idf-lib](https://github.com/UncleRus/esp-idf-lib) (`components/`).
- Font: [font8x8](https://github.com/dhepper/font8x8) by Daniel Hepper (public domain).
- DNS server for the captive portal: ESP-IDF `captive_portal` example (Unlicense / CC0).
