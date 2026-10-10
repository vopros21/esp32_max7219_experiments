# ESP32-S3 8x32 LED matrix

Scrolling text on four cascaded MAX7219 8x8 LED modules driven by an ESP32-S3-Zero, on the way to a
desk gadget (working title HubitHab: GitHub stats, clock, Pomodoro). See [ROADMAP.md](ROADMAP.md) for plans.

## Hardware

| Signal | ESP32-S3-Zero |
|---|---|
| MAX7219 `DIN` (MOSI) | GPIO4 |
| MAX7219 `CS` | GPIO5 |
| MAX7219 `CLK` | GPIO6 |
| Button | GPIO0 (BOOT) |
| `VCC` / `GND` | 5V / GND |

The modules on this board are rotated by 90 degrees relative to what the stock `max7219` driver
expects. `fb_flush()` in `main/main.c` handles the remapping, so drawing code works with normal
x/y coordinates. Several modules at high brightness need a solid power supply.

## Build and flash

Requires ESP-IDF v5.2.

```sh
. path/to/esp-idf/export.sh
idf.py set-target esp32s3   # first time only
idf.py build flash monitor
```

## Usage

- Click BOOT: next mode (Text, Clock, Pomodoro).
- Hold BOOT ~1 s: mode action — Text: next message, Clock: toggle time / date,
  Pomodoro: start / pause.
- Hold BOOT 3 s in Pomodoro: reset.

Pomodoro alternates 25 min work / 5 min break (`idf.py menuconfig` → HubitHab). The bottom row is a
progress bar: it fills during work and empties during a break. When a phase ends the display jumps
to Pomodoro from any mode and flashes.

Until Wi-Fi time sync exists, the clock starts from the build time, so it resets on every reboot.

Messages are in `MESSAGES` at the top of `main/main.c`; scroll speed is
`CONFIG_EXAMPLE_SCROLL_DELAY` (`idf.py menuconfig` → Example configuration).

## Credits

- Drivers from [esp-idf-lib](https://github.com/UncleRus/esp-idf-lib) (`components/`).
- Font: [font8x8](https://github.com/dhepper/font8x8) by Daniel Hepper (public domain).
