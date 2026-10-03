# Project context

Desk display built from an ESP32-S3-Zero and four cascaded MAX7219 8x8 LED modules (8x32 pixels).
Today it scrolls text. The goal is a DeskHub-style gadget (GitHub stats, clock, Pomodoro, ...),
see `ROADMAP.md`.

## Hardware

- Board: Waveshare ESP32-S3-Zero (ESP32-S3FH4R2: 4 MB flash, 2 MB PSRAM — PSRAM not enabled in `sdkconfig`).
- Display: 4 x MAX7219 modules, SPI. Pins: MOSI = GPIO4, CS = GPIO5, CLK = GPIO6.
- Button: BOOT button on GPIO0 (input, internal pull-up), currently only polled and printed.
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

This mapping lives only in `fb_flush()` in `main/main.c`. Everything else draws into the logical
framebuffer `fb[32]` (one byte per column, bit y = row y). Keep it that way: new modes should
render into `fb`, never call `max7219_set_digit()` directly.

## Code layout

- `main/main.c` — app: framebuffer, `fb_flush()`, `push_column()`, `scroll_text()`, display task,
  button task.
- `main/font8x8.h` — ASCII 0x20..0x7E 8x8 font (public domain font8x8_basic). Byte 0 = top row,
  bit 0 = leftmost pixel. `scroll_text()` trims empty glyph columns for proportional spacing.
- `main/alphabet.h` — older hand-made `uint64_t` symbols (arrows, heart, sun, ...). Same orientation
  as the font (LSB byte = top row). Currently unused.
- `components/` — vendored [esp-idf-lib](https://github.com/UncleRus/esp-idf-lib) drivers. Only
  `max7219` and `esp_idf_lib_helpers` are used; `button` (click / long press) is a likely next one.
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

Partition table is the default single-app (1 MB app). Wi-Fi + HTTPS will need a larger app partition.

## Verifying without hardware

Display logic can be checked on the host: compile `main/main.c` with stub headers
(`freertos/*.h`, `driver/gpio.h`, `max7219.h`, ...), implement `max7219_set_digit()` to record
digits and `vTaskDelay()` to snapshot frames, then print `fb` as ASCII art. This is how the
string scroller was checked against the original single-letter `run_text`.

## Workflow preferences

- Small, focused commits. Merging straight into `main` is fine; delete feature branches afterwards.
- Match the existing C style: 4-space indent, `snake_case`, short comments explaining the "why".
