#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

#include "modes.h"
#include "display.h"
#include "build_time.h"
#include "github.h"

#define POMODORO_ALERT_MS 4000     // display flashes this long when a phase ends

static const char *TAG = "modes";

// Status mode: scrolls the status line. Other tasks (Wi-Fi, setup) update it with
// text_set_status(); the new text is picked up by the display task when the
// current pass starts over, or right away on a long press.

#define STATUS_LEN 200

static portMUX_TYPE status_mux = portMUX_INITIALIZER_UNLOCKED;
static char status_pending[STATUS_LEN];
static bool status_dirty;
static char status_shown[STATUS_LEN];   // only touched by the display task

static scroller_t text_scroller;
static uint32_t text_last_ms;

void text_set_status(const char *fmt, ...)
{
    char buf[STATUS_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    ESP_LOGI(TAG, "Status: %s", buf);

    taskENTER_CRITICAL(&status_mux);
    memcpy(status_pending, buf, sizeof(buf));
    status_dirty = true;
    taskEXIT_CRITICAL(&status_mux);
}

static void take_status(void)
{
    taskENTER_CRITICAL(&status_mux);
    if (status_dirty) {
        memcpy(status_shown, status_pending, sizeof(status_shown));
        status_dirty = false;
    }
    taskEXIT_CRITICAL(&status_mux);
}

static void text_enter(uint32_t now_ms)
{
    take_status();
    memset(fb, 0, sizeof(fb));
    scroller_start(&text_scroller, status_shown);
    text_last_ms = now_ms;
}

static bool text_render(uint32_t now_ms)
{
    if (now_ms - text_last_ms < CONFIG_EXAMPLE_SCROLL_DELAY)
        return false;
    text_last_ms = now_ms;

    // Switch to a new status between passes: at the start of the text, after the
    // trailing blank columns have pushed the old text out.
    if (status_dirty && text_scroller.p == text_scroller.text && !text_scroller.glyph &&
        text_scroller.blank == 0) {
        take_status();
        scroller_start(&text_scroller, status_shown);
    }

    memmove(fb, fb + 1, WIDTH - 1);
    fb[WIDTH - 1] = scroller_next(&text_scroller);
    return true;
}

static void text_on_button(button_event_t ev)
{
    if (ev == EV_LONG)
        text_enter(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

// Clock mode: HH:MM with a blinking colon, long press toggles the date (DD.MM).
// The time starts from the build time (see clock_set_initial_time()) until SNTP
// syncs it; until then the bottom-right pixel is lit as a "time is a guess" hint.

static volatile bool clock_synced;      // set from the SNTP task
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
    key = key * 2 + clock_synced;
    if (key == clock_last_key)
        return false;
    clock_last_key = key;

    char buf[16];
    if (clock_show_date)
        snprintf(buf, sizeof(buf), "%02d.%02d", tm.tm_mday, tm.tm_mon + 1);
    else
        snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
    draw_narrow(buf, colon);
    if (!clock_synced)
        fb[WIDTH - 1] |= 0x80;
    return true;
}

void clock_set_synced(void)
{
    clock_synced = true;
}

static void clock_on_button(button_event_t ev)
{
    if (ev != EV_LONG)
        return;
    clock_show_date = !clock_show_date;
    clock_last_key = -1;
}

// No network time yet: start the system clock from the build time. BUILD_TIME is
// the build machine's local time; mktime() reads it in the TZ set by main.c (UTC
// if none), so this is right as long as the board and the build share a zone.
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

// GitHub mode: contribution heatmap, one column per week (oldest left), one row
// per weekday (Sunday on top). Today's pixel blinks while it has no contributions;
// the bottom row marks the week containing the 1st of each month. Every GH_STATS_EVERY_MS the
// stats scroll by once. Long press: stats only. Without a token (no calendar)
// only the stats scroll.

#define GH_STATS_EVERY_MS 60000
#define GH_SYNC_MS 1000            // how often to copy fresh data from github.c

static github_data_t gh;
static bool gh_month[GH_WEEKS];    // bottom-row markers, computed on load
static uint32_t gh_synced_ms;
static bool gh_stats_only;
static bool gh_scrolling;          // stats text is moving across the display
static uint32_t gh_next_stats_ms;
static uint32_t gh_last_step_ms;
static scroller_t gh_scroller;
static char gh_text[96];

// Copies fresh data and marks the week that contains the 1st of a month, i.e.
// whose Saturday is among the first 7 days of a month. Done here, not per frame:
// mktime() with a TZ is not cheap.
static void gh_load(void)
{
    time_t old_updated = gh.updated;
    github_get(&gh);
    if (gh.updated == old_updated && gh.updated)
        return;
    for (int w = 0; w < GH_WEEKS; w++) {
        struct tm tm;
        localtime_r(&gh.start, &tm);
        tm.tm_mday += w * 7 + 6;   // Saturday of week w
        tm.tm_hour = 12;           // stay clear of DST edges
        tm.tm_isdst = -1;
        mktime(&tm);
        gh_month[w] = tm.tm_mday <= 7;
    }
}

static void gh_make_text(void)
{
    if (!gh.valid)
        snprintf(gh_text, sizeof(gh_text), "GitHub: loading...");
    else if (gh.has_calendar)
        snprintf(gh_text, sizeof(gh_text), "streak %d - today %d - %d followers",
                 gh.streak, gh.today, gh.followers);
    else
        snprintf(gh_text, sizeof(gh_text), "%d followers - %d repos", gh.followers, gh.repos);
}

static bool gh_heatmap_shown(void)
{
    return gh.valid && gh.has_calendar && !gh_stats_only;
}

static void gh_start_scroll(uint32_t now_ms)
{
    gh_make_text();
    scroller_start(&gh_scroller, gh_text);
    gh_scrolling = true;
    gh_last_step_ms = now_ms;
}

static void github_enter(uint32_t now_ms)
{
    gh_load();
    gh_synced_ms = now_ms;
    gh_scrolling = false;
    gh_next_stats_ms = now_ms + GH_STATS_EVERY_MS;
    memset(fb, 0, sizeof(fb));
    if (!gh_heatmap_shown())
        gh_start_scroll(now_ms);
}

static void draw_heatmap(uint32_t now_ms)
{
    memset(fb, 0, sizeof(fb));
    for (int w = 0; w < GH_WEEKS; w++) {
        for (int d = 0; d < 7; d++) {
            uint16_t c = gh.days[w][d];
            if (c != 0 && c != GH_NO_DAY)
                fb[w] |= 1 << d;
        }
        if (gh_month[w])
            fb[w] |= 0x80;
    }

    if (gh.today == 0 && gh.today_week >= 0 && now_ms % 2000 < 1000)
        fb[gh.today_week] |= 1 << gh.today_day;
}

static bool github_render(uint32_t now_ms)
{
    if (now_ms - gh_synced_ms >= GH_SYNC_MS) {
        gh_synced_ms = now_ms;
        bool had_heatmap = gh_heatmap_shown();
        gh_load();
        if (gh_heatmap_shown() != had_heatmap)
            github_enter(now_ms);
    }

    if (gh_scrolling) {
        if (now_ms - gh_last_step_ms < CONFIG_EXAMPLE_SCROLL_DELAY)
            return false;
        gh_last_step_ms = now_ms;
        memmove(fb, fb + 1, WIDTH - 1);
        fb[WIDTH - 1] = scroller_next(&gh_scroller);

        // One pass done: the text has wrapped and its trailing blanks pushed it out.
        if (gh_scroller.p == gh_scroller.text && !gh_scroller.glyph && gh_scroller.blank == 0) {
            if (gh_heatmap_shown()) {
                gh_scrolling = false;
                gh_next_stats_ms = now_ms + GH_STATS_EVERY_MS;
            } else {
                gh_start_scroll(now_ms);   // refresh the numbers for the next pass
            }
        }
        return true;
    }

    if ((int32_t)(now_ms - gh_next_stats_ms) >= 0) {
        gh_start_scroll(now_ms);
        return false;
    }

    uint8_t old[WIDTH];
    memcpy(old, fb, sizeof(fb));
    draw_heatmap(now_ms);
    return memcmp(old, fb, sizeof(fb)) != 0;
}

static void github_on_button(button_event_t ev)
{
    if (ev != EV_LONG)
        return;
    gh_stats_only = !gh_stats_only;
    ESP_LOGI(TAG, "GitHub: %s", gh_stats_only ? "stats only" : "heatmap");
    github_enter(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

const display_mode_t modes[] = {
    { "GitHub", github_enter, github_render, github_on_button },
    { "Clock", clock_enter, clock_render, clock_on_button },
    { "Pomodoro", pomodoro_enter, pomodoro_render, pomodoro_on_button },
    { "Status", text_enter, text_render, text_on_button },
};
const size_t mode_count = sizeof(modes) / sizeof(modes[0]);
const size_t mode_github = 0;
const size_t mode_pomodoro = 2;
const size_t mode_status = 3;
