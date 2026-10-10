#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

#include "modes.h"
#include "display.h"
#include "build_time.h"

#define POMODORO_ALERT_MS 4000     // display flashes this long when a phase ends

static const char *TAG = "modes";

// Text mode: scrolls one of the messages, long press picks the next one.

static const char *const *text_messages;
static size_t text_count;
static scroller_t text_scroller;
static size_t text_index;
static uint32_t text_last_ms;

static void text_enter(uint32_t now_ms)
{
    memset(fb, 0, sizeof(fb));
    scroller_start(&text_scroller, text_count ? text_messages[text_index] : "");
    text_last_ms = now_ms;
}

static bool text_render(uint32_t now_ms)
{
    if (now_ms - text_last_ms < CONFIG_EXAMPLE_SCROLL_DELAY)
        return false;
    text_last_ms = now_ms;

    memmove(fb, fb + 1, WIDTH - 1);
    fb[WIDTH - 1] = scroller_next(&text_scroller);
    return true;
}

void text_set_messages(const char *const *messages, size_t count)
{
    text_messages = messages;
    text_count = count;
    text_index = 0;
}

static void text_on_button(button_event_t ev)
{
    if (ev != EV_LONG)
        return;
    if (!text_count)
        return;
    text_index = (text_index + 1) % text_count;
    ESP_LOGI(TAG, "Message %u: %s", (unsigned)text_index, text_messages[text_index]);
    text_enter(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

// Clock mode: HH:MM with a blinking colon, long press toggles the date (DD.MM).
// Without Wi-Fi the time starts from the build time (see clock_set_initial_time()).

static bool clock_show_date;
static int clock_last_key;   // what is on the display now, -1 = redraw

static void clock_enter(uint32_t now_ms)
{
    clock_last_key = -1;
}

static bool clock_render(uint32_t now_ms)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tm;
    localtime_r(&tv.tv_sec, &tm);

    bool colon = tv.tv_usec < 500000;
    int key = clock_show_date ? 10000 + tm.tm_mday * 100 + tm.tm_mon
                              : (tm.tm_hour * 60 + tm.tm_min) * 2 + colon;
    if (key == clock_last_key)
        return false;
    clock_last_key = key;

    char buf[16];
    if (clock_show_date)
        snprintf(buf, sizeof(buf), "%02d.%02d", tm.tm_mday, tm.tm_mon + 1);
    else
        snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
    draw_narrow(buf, colon);
    return true;
}

static void clock_on_button(button_event_t ev)
{
    if (ev != EV_LONG)
        return;
    clock_show_date = !clock_show_date;
    clock_last_key = -1;
}

// No network time yet: start the system clock from the build time. It is local
// time stored as if it were UTC (no time zone is set), which is fine for display.
void clock_set_initial_time(void)
{
    struct tm tm = { 0 };
    sscanf(BUILD_TIME, "%d-%d-%d %d:%d:%d",
           &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    struct timeval tv = { .tv_sec = mktime(&tm) };
    settimeofday(&tv, NULL);
    ESP_LOGI(TAG, "Time set from build: %s", BUILD_TIME);
}

// Pomodoro mode: MM:SS countdown with a progress bar on the bottom row. Work and
// break phases alternate automatically. The timer runs in the background in any
// mode; pomodoro_tick() reports a phase change so the display loop can jump here.
// Hold = start / pause, keep holding for 3 s (EV_VERY_LONG) = reset.

#define POMODORO_WORK_MS  (CONFIG_POMODORO_WORK_MIN * 60 * 1000)
#define POMODORO_BREAK_MS (CONFIG_POMODORO_BREAK_MIN * 60 * 1000)

typedef enum {
    POMO_IDLE,
    POMO_RUNNING,
    POMO_PAUSED,
} pomo_state_t;

static pomo_state_t pomo_state = POMO_IDLE;
static bool pomo_break;                       // false = work phase
static uint32_t pomo_left_ms = POMODORO_WORK_MS;  // remaining time unless running
static uint32_t pomo_end_ms;                  // end of the phase while running
static uint32_t pomo_alert_end_ms;
static bool pomo_alert;

static uint32_t pomo_phase_ms(void)
{
    return pomo_break ? POMODORO_BREAK_MS : POMODORO_WORK_MS;
}

static uint32_t pomo_remaining(uint32_t now_ms)
{
    if (pomo_state != POMO_RUNNING)
        return pomo_left_ms;
    int32_t left = (int32_t)(pomo_end_ms - now_ms);
    return left > 0 ? left : 0;
}

bool pomodoro_tick(uint32_t now_ms)
{
    if (pomo_alert && (int32_t)(now_ms - pomo_alert_end_ms) >= 0)
        pomo_alert = false;

    if (pomo_state != POMO_RUNNING || (int32_t)(now_ms - pomo_end_ms) < 0)
        return false;

    pomo_break = !pomo_break;
    pomo_end_ms = now_ms + pomo_phase_ms();
    pomo_alert = true;
    pomo_alert_end_ms = now_ms + POMODORO_ALERT_MS;
    ESP_LOGI(TAG, "Pomodoro: %s", pomo_break ? "break" : "work");
    return true;
}

static void pomodoro_enter(uint32_t now_ms)
{
}

static bool pomodoro_render(uint32_t now_ms)
{
    uint8_t old[WIDTH];
    memcpy(old, fb, sizeof(fb));

    uint32_t left = pomo_remaining(now_ms);
    uint32_t secs = (left + 999) / 1000;      // 25:00 at the start, 00:01 at the end
    char buf[16];
    snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)(secs / 60), (unsigned)(secs % 60));
    draw_narrow(buf, true);

    // Paused: digits blink, the bar stays.
    if (pomo_state == POMO_PAUSED && now_ms % 1000 >= 500)
        memset(fb, 0, sizeof(fb));

    // Work: the bar fills up. Break: it empties.
    uint32_t phase = pomo_phase_ms();
    uint32_t done = phase - left;
    int bar = pomo_break ? (int)(((uint64_t)left * WIDTH + phase - 1) / phase)
                         : (int)((uint64_t)done * WIDTH / phase);
    for (int x = 0; x < bar; x++)
        fb[x] |= 0x80;

    // Phase change: flash the whole display.
    if (pomo_alert && now_ms % 500 < 250) {
        for (int x = 0; x < WIDTH; x++)
            fb[x] = ~fb[x];
    }

    return memcmp(old, fb, sizeof(fb)) != 0;
}

static void pomodoro_on_button(button_event_t ev)
{
    uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

    if (ev == EV_VERY_LONG) {
        pomo_state = POMO_IDLE;
        pomo_break = false;
        pomo_left_ms = POMODORO_WORK_MS;
        pomo_alert = false;
        ESP_LOGI(TAG, "Pomodoro: reset");
        return;
    }

    if (pomo_state == POMO_RUNNING) {
        pomo_left_ms = pomo_remaining(now_ms);
        pomo_state = POMO_PAUSED;
        ESP_LOGI(TAG, "Pomodoro: paused");
    } else {
        pomo_end_ms = now_ms + pomo_left_ms;
        pomo_state = POMO_RUNNING;
        ESP_LOGI(TAG, "Pomodoro: running");
    }
}

const display_mode_t modes[] = {
    { "Text", text_enter, text_render, text_on_button },
    { "Clock", clock_enter, clock_render, clock_on_button },
    { "Pomodoro", pomodoro_enter, pomodoro_render, pomodoro_on_button },
};
const size_t mode_count = sizeof(modes) / sizeof(modes[0]);
const size_t mode_pomodoro = 2;
