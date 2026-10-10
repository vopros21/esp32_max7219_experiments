# Roadmap

Goal: a desk gadget, working title HubitHab (inspiration: https://x.com/_MaxBlade). Out of the box it starts
its own Wi-Fi with a setup page, the user enters their Wi-Fi and GitHub details, and then it shows
GitHub stats. Unlike the original, buttons switch between several modes.

## Target behaviour

```
Power on
  │
  ├─ Wi-Fi credentials in NVS? ──no──► Setup mode
  │                                     • SoftAP "HubitHab-XXXX"
  │                                     • DNS server answers every name with 192.168.4.1
  │                                       → phone opens the captive portal automatically
  │                                     • Form: Wi-Fi SSID, password, GitHub username,
  │                                       GitHub token, time zone
  │                                     • Save to NVS → reboot
  │
  └─ yes ─► Connect as station
              ├─ 3 failures ─► back to setup mode
              └─ OK ─► SNTP time sync → normal mode
                        (settings page at http://hubithab.local)
```

Holding a button for 10 s erases the credentials and returns to setup mode.

## Modes

| Mode | Display (8x32) | Action button |
|---|---|---|
| GitHub | Contribution heatmap: 7 rows x 32 columns = 7 days x 32 weeks (fits exactly). Periodically scrolls "streak N • today N" | Toggle heatmap / stats |
| Clock | `14:37` with blinking colon | Toggle time / date |
| Pomodoro | `24:59` countdown + progress bar on the bottom row | Start / pause, long press = reset |
| Later | Weather (open-meteo.com, no key), messages typed on the web page, other stats | — |

## Buttons

- One button: short press = next mode, long press = action, 10 s hold = Wi-Fi reset.
- Two buttons (preferred): Mode + Action.
- BOOT (GPIO0) works today without wiring. Extra buttons: GPIO7 / GPIO8 to GND, internal pull-up.
- Use `components/button` (already vendored) for click / long-press detection.
- Optional: onboard RGB LED (GPIO21) as status — blue = setup mode, red = no Wi-Fi.

## Architecture

- Each mode is a module with `enter()`, `render(fb)`, `on_button(event)`.
- Display task renders the active mode into `fb` and calls `fb_flush()` (keeps the rotation logic
  in one place).
- Button task → event queue → active mode.
- Network task fetches data periodically into shared state (mutex-protected).
- `esp_http_server` serves the setup / settings page.
- Libraries: `esp_wifi`, `nvs_flash`, `esp_http_server`, `esp_http_client` + `esp_crt_bundle`
  (HTTPS), `esp_sntp`, `mdns`, cJSON (`json` component). Starting point for the portal:
  `../esp-idf/examples/protocols/http_server/captive_portal`.

## Constraints

1. **GitHub contributions need a token.** The calendar is only available via the GraphQL API
   (`contributionsCollection`), which requires a personal access token (read-only is enough).
   Followers / repos work without one, but anonymous requests are limited to 60/hour → poll every
   10–15 min.
2. **MAX7219 has no per-pixel brightness** (only per module). The heatmap is on/off with a threshold
   (e.g. ≥ 1 contribution) instead of GitHub's shades of green.
3. **Clock digits:** the 8x8 font makes `14:37` ~31 px wide. Add a narrow 4x7 digit font.
4. **Flash:** the default 1 MB app partition will be tight with Wi-Fi + TLS + HTTP server.
   Switch to a larger app partition; later use two OTA slots for updates over Wi-Fi.
5. **Power:** four modules at high brightness can draw close to 1 A. USB is fine at low brightness.

## Phases

- [x] **0. Text scrolling** — rotated-module mapping, framebuffer, ASCII font, `scroll_text()`.
- [x] **1. Modes without Wi-Fi**
  - [x] Mode framework, buttons (click / long press), non-blocking scroller, Demo mode.
  - [x] Narrow digit font, Clock (starts from build time for now).
  - [x] Pomodoro.
- [x] **2. Provisioning**
  - [x] Split `main.c` into modules, OTA-ready partition table.
  - [x] SoftAP + captive portal + NVS settings, 10 s hold = reset to setup.
  - [x] Station mode with fallback to setup, show IP.
  - [x] SNTP + time zone.
- [ ] **3. GitHub**
  - [x] HTTPS fetch (GraphQL with token, REST without), JSON parsing, polling, status line.
  - [ ] GitHub mode: heatmap + stats rendering.
- [ ] **4. Extras** — settings page in station mode (brightness, username, modes on/off),
      mDNS `hubithab.local`, more modes (weather, messages), OTA updates, status LED.

## Open questions

- One or two buttons? Which GPIOs?
- Enclosure / diffuser (the original has a fabric-like front).
- Should the GitHub token be required, or offer a token-free "basic stats" mode?
