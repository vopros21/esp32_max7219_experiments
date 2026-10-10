# Project context

Desk display built from an ESP32-S3-Zero and four cascaded MAX7219 8x8 LED modules (8x32 pixels).
Working title: HubitHab. Goal: a desk gadget with modes (GitHub stats, clock, Pomodoro, ...),
see `ROADMAP.md`.

## Hardware

- Board: Waveshare ESP32-S3-Zero (ESP32-S3FH4R2: 4 MB flash, 2 MB PSRAM — PSRAM not enabled in `sdkconfig`).
- Display: 4 x MAX7219 modules, SPI. Pins: MOSI = GPIO4, CS = GPIO5, CLK = GPIO6.
- Button: BOOT button on GPIO0 (input, internal pull-up), handled by `components/button`
  (click = next mode, ~1 s = mode action, 3 s = very long, 10 s = erase settings).
- Onboard WS2812 RGB LED on GPIO21 (unused).
- Pins to avoid for new inputs: 0/3/45/46 (strapping), 19/20 (USB), 21 (RGB LED), 4/5/6 (display).
  Good candidates for extra buttons: GPIO7, GPIO8.

## Display geometry (the non-obvious part)

Each 8x8 module is physically rotated by 90 degrees, so the stock `max7219_draw_image_8x8()` draws
sideways. A MAX7219 digit register holds one horizontal row of one module. For logical pixel
(x = 0..31 left to right, y = 0..7 top to bottom):

```
digit = (x / 8) * 8 + y      // index passed to max7219_set_digit(), with dev.mirrored = true
bit   = x % 8
```

This mapping lives only in `fb_flush()` in `main/display.c`. Everything else draws into the logical
framebuffer `fb[32]` (one byte per column, bit y = row y). Keep it that way: new modes should
render into `fb`, never call `max7219_set_digit()` directly.

## Code layout

- `main/main.c` — startup and the display loop: runs every `TICK_MS`, routes button events,
  flushes `fb` only when the mode's `render()` reports a change. Click = next mode; `EV_LONG`
  (~1 s) and `EV_VERY_LONG` (3 s, timed by the loop from raw press / release) go to the mode's
  `on_button`; a 10 s hold erases settings and reboots into setup.
- `main/display.c/.h` — framebuffer `fb`, `fb_flush()` (rotation mapping), SPI / MAX7219 init,
  non-blocking scroller (`scroller_next()` yields one column per call), `draw_narrow()`.
- `main/buttons.c/.h` — BOOT button via `components/button`, callback → queue.
- `main/modes.c/.h` — `display_mode_t` (`enter` / `render` / `on_button`) and the `modes[]` table:
  GitHub (heatmap: column = week, row = weekday, bottom row = week containing the 1st of a month, today blinks while 0;
  stats scroll once a minute, long press = stats only), Clock, Pomodoro, Status (scrolls the
  status line; `text_set_status()` is safe from any task, the new text is taken between passes).
  `main.c` starts on Status and switches to GitHub after the first successful fetch. The Pomodoro timer runs in the background
  (`pomodoro_tick()`) and takes over the display when a phase ends.
- `main/settings.c/.h` — `settings_t` (Wi-Fi, GitHub user / token, POSIX TZ) in NVS namespace
  `hubithab`. Configured = SSID set.
- `main/wifi.c/.h` — `net_init()` (netifs, event loop, driver; idempotent), `wifi_connect()` blocks
  until the first IP or 3 failed attempts; after one success it reconnects forever. Updates the
  status line. Hostname `hubithab`. After the first IP it starts SNTP (`pool.ntp.org`, hourly);
  the first sync calls `clock_set_synced()`. `main.c` applies the saved POSIX TZ before
  `clock_set_initial_time()`, so the build-time guess is read in the right zone.
- `main/github.c/.h` — background task (started after Wi-Fi), refreshes every 15 min, 1 min retry
  on error. Token set: GraphQL `contributionsCollection` from the Sunday 31 weeks ago → 32x7
  `days[week][weekday]` (Sunday = 0, `GH_NO_DAY` after today), today, streak, followers. No token:
  REST `/users/<name>` (followers, public repos). `github_get()` copies under a mutex. Uses
  `esp_http_client` + `esp_crt_bundle`, cJSON. Never log the token. Fine-grained tokens only see
  the user's own repositories: contributions to organisation repos are missing (accepted).
- `main/portal.c/.h` — setup mode: open SoftAP `HubitHab-XXXX` (from MAC), DNS answers everything
  with 192.168.4.1, `esp_http_server` form at `/`, `/rescan`, `POST /save` → NVS → reboot; 404s
  redirect to the form (captive portal). Responses are chunked: an empty chunk ends the response,
  so `send()` skips empty strings. `portal_start(true)` = fallback after a failed connect: form is
  pre-filled, and the board restarts after 5 min if no client is on the AP.
- `main/font8x8.h` — ASCII 0x20..0x7E 8x8 font (public domain font8x8_basic). Byte 0 = top row,
  bit 0 = leftmost pixel. The scroller trims empty glyph columns for proportional spacing.
- `main/font_digits.h` — hand-drawn 5x7 digits plus `:` / `.` for the clock, stored by column
  (ready for `fb`), rows 0..6; the bottom row stays free.
- `main/build_time.cmake` — generates `build/esp-idf/main/build_time.h` (`BUILD_TIME`) on every
  build; the clock starts from it until SNTP syncs (bottom-right pixel lit until then). Hooked up in `main/CMakeLists.txt`.
- `main/alphabet.h` — older hand-made `uint64_t` symbols (arrows, heart, sun, ...). Same orientation
  as the font (LSB byte = top row). Currently unused.
- `components/` — vendored [esp-idf-lib](https://github.com/UncleRus/esp-idf-lib) drivers. Only
  `max7219`, `esp_idf_lib_helpers` and `button` are used. `components/dns_server` is copied from
  ESP-IDF's `captive_portal` example (Unlicense / CC0).
  Don't edit vendored drivers unless necessary.

## Building

ESP-IDF v5.2.4 lives in the parent folder (`../esp-idf`). `~/.zshrc` has an alias:

```sh
get_idf                  # = . ~/esp32_repos/esp-idf/export.sh
idf.py build
idf.py flash monitor
```

Target is `esp32s3`. The working Python env is `~/.espressif/python_env/idf5.2_py3.14_env`
(the `idf5.2_py3.9_env` one is broken). If `idf.py` complains about a Python env mismatch,
run `idf.py fullclean` — `build/` is generated and git-ignored.

Partition table is `partitions.csv`: NVS + two 1.9 MB app slots (`ota_0` / `ota_1`) for future OTA.

## Verifying without hardware

Display logic can be checked on the host: compile `main/display.c` / `main/modes.c` with stub headers
(`freertos/*.h`, `driver/gpio.h`, `max7219.h`, ...), implement `max7219_set_digit()` to record
digits and `vTaskDelay()` to snapshot frames, then print `fb` as ASCII art. This is how the
string scroller was checked against the original single-letter `run_text`.

## Workflow preferences

- Small, focused commits. Merging straight into `main` is fine; delete feature branches afterwards.
- Match the existing C style: 4-space indent, `snake_case`, short comments explaining the "why".

## Verifying on hardware

The board shows up as `/dev/cu.usbmodem1101`: `idf.py -p /dev/cu.usbmodem1101 build flash`.
To read the boot log without an interactive monitor, open the port with pyserial from the IDF
Python env, pulse RTS to reset, and read for ~20 s (status lines are logged by `modes` as
`Status: ...`, the GitHub calendar as ASCII art by `github`). For timers, flash a short test
value first (e.g. Pomodoro 2 / 1 min in `sdkconfig`) and restore it before committing.
