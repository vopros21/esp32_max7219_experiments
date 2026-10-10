#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_idf_version.h>
#include <max7219.h>
#include <esp_idf_lib_helpers.h>
#include <button.h>

#include "font8x8.h"
#include "font_digits.h"
#include "build_time.h"


#define HOST HELPER_SPI_HOST_DEFAULT

#ifndef APP_CPU_NUM
#define APP_CPU_NUM PRO_CPU_NUM
#endif

#define CASCADE_SIZE 4
#define PIN_MOSI 4
#define PIN_CS 5
#define PIN_CLK 6
#define BUTTON_GPIO GPIO_NUM_0

#define WIDTH (CASCADE_SIZE * 8)  // display width in pixels
#define LETTER_SPACING 1           // empty columns between letters
#define SPACE_WIDTH 3              // width of ' ' in columns

#define TICK_MS 10                 // display loop period

static const char *TAG = "8x8";

// Messages for the text mode; a long press switches to the next one.
static const char *MESSAGES[] = {
    "Hello, ESP32-S3! 0123456789",
    "8x32 LED matrix",
    "Click BOOT: next mode. Hold BOOT: next text.",
};
#define MESSAGE_COUNT (sizeof(MESSAGES) / sizeof(MESSAGES[0]))


// Logical framebuffer: one byte per column, x = 0 is the leftmost column.
// Bit y of a column is the pixel in row y (y = 0 is the top row).
static uint8_t fb[WIDTH];

// Each 8x8 module is rotated by 90 degrees, so a MAX7219 digit register holds a
// horizontal row of one module, not a column of the whole display. For pixel (x, y):
//   digit = (x / 8) * 8 + y,  bit = x % 8
// (digit index as passed to max7219_set_digit with dev.mirrored = true).
static void fb_flush(max7219_t *dev)
{
    for (int m = 0; m < CASCADE_SIZE; m++) {
        for (int y = 0; y < 8; y++) {
            uint8_t row = 0;
            for (int b = 0; b < 8; b++) {
                if (fb[m * 8 + b] & (1 << y))
                    row |= 1 << b;
            }
            max7219_set_digit(dev, m * 8 + y, row);
        }
    }
}

static const uint8_t *get_glyph(char c)
{
    if (c < FONT8X8_FIRST || c > FONT8X8_LAST)
        c = '?';
    return font8x8[c - FONT8X8_FIRST];
}

// Take column x of a glyph (stored as rows) and turn it into a column byte.
static uint8_t glyph_column(const uint8_t *glyph, int x)
{
    uint8_t col = 0;
    for (int y = 0; y < 8; y++) {
        if (glyph[y] & (1 << x))
            col |= 1 << y;
    }
    return col;
}

// ---------------------------------------------------------------------------
// Scroller: produces the columns of a string one at a time, so the display loop
// never blocks and can react to buttons in the middle of a message.
// Letters are proportional: empty columns on both sides of a glyph are trimmed.
// After the last letter it emits WIDTH blank columns and starts over.

typedef struct {
    const char *text;
    const char *p;          // next character to load
    const uint8_t *glyph;   // current glyph, NULL between glyphs
    int x, last;            // remaining glyph columns: x..last
    int blank;              // blank columns to emit before anything else
} scroller_t;

static void scroller_start(scroller_t *s, const char *text)
{
    s->text = text;
    s->p = text;
    s->glyph = NULL;
    s->x = s->last = 0;
    s->blank = 0;
}

static uint8_t scroller_next(scroller_t *s)
{
    for (;;) {
        if (s->blank > 0) {
            s->blank--;
            return 0;
        }

        if (s->glyph) {
            uint8_t col = glyph_column(s->glyph, s->x++);
            if (s->x > s->last) {
                s->glyph = NULL;
                s->blank = LETTER_SPACING;
            }
            return col;
        }

        if (!*s->p) {
            s->p = s->text;
            s->blank = WIDTH;
            continue;
        }

        const uint8_t *glyph = get_glyph(*s->p++);
        uint8_t used = 0;
        for (int y = 0; y < 8; y++)
            used |= glyph[y];

        if (!used) {
            s->blank = SPACE_WIDTH;
            continue;
        }

        s->glyph = glyph;
        s->x = __builtin_ctz(used);
        s->last = 31 - __builtin_clz(used);
    }
}

// ---------------------------------------------------------------------------
// Modes. Each one draws into fb; the display loop calls render() every TICK_MS
// and flushes fb only when render() reports a change.

typedef enum {
    EV_CLICK,
    EV_LONG,
} button_event_t;

typedef struct {
    const char *name;
    void (*enter)(uint32_t now_ms);
    bool (*render)(uint32_t now_ms);           // returns true if fb changed
    void (*on_button)(button_event_t ev);      // long press action, may be NULL
} display_mode_t;

// Text mode: scrolls one of MESSAGES, long press picks the next one.

static scroller_t text_scroller;
static size_t text_index;
static uint32_t text_last_ms;

static void text_enter(uint32_t now_ms)
{
    memset(fb, 0, sizeof(fb));
    scroller_start(&text_scroller, MESSAGES[text_index]);
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

static void text_on_button(button_event_t ev)
{
    text_index = (text_index + 1) % MESSAGE_COUNT;
    ESP_LOGI(TAG, "Message %u: %s", (unsigned)text_index, MESSAGES[text_index]);
    text_enter(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

// Draw a string of digits, ':' and '.' with the narrow font, centred.
// A ':' with `colon` false is left blank but keeps its width, so nothing jumps.
static void draw_narrow(const char *str, bool colon)
{
    int width = -1;
    for (const char *p = str; *p; p++)
        width += (*p >= '0' && *p <= '9' ? DIGIT_WIDTH : 1) + 1;

    memset(fb, 0, sizeof(fb));
    int x = (WIDTH - width) / 2;
    for (const char *p = str; *p; p++) {
        if (*p >= '0' && *p <= '9') {
            memcpy(fb + x, font_digits[*p - '0'], DIGIT_WIDTH);
            x += DIGIT_WIDTH;
        } else {
            if (*p == '.')
                fb[x] = DOT_COLUMN;
            else if (*p == ':' && colon)
                fb[x] = COLON_COLUMN;
            x++;
        }
        x++;
    }
}

// Clock mode: HH:MM with a blinking colon, long press toggles the date (DD.MM).
// Without Wi-Fi the time starts from the build time (see set_initial_time()).

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
    clock_show_date = !clock_show_date;
    clock_last_key = -1;
}

// No network time yet: start the system clock from the build time. It is local
// time stored as if it were UTC (no time zone is set), which is fine for display.
static void set_initial_time(void)
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

static const display_mode_t modes[] = {
    { "Text", text_enter, text_render, text_on_button },
    { "Clock", clock_enter, clock_render, clock_on_button },
};
#define MODE_COUNT (sizeof(modes) / sizeof(modes[0]))

// ---------------------------------------------------------------------------
// Buttons. The driver calls back from its esp_timer task; events go through a
// queue so all mode logic runs in the display task.

static QueueHandle_t button_queue;

static void on_button(button_t *btn, button_state_t state)
{
    button_event_t ev;
    if (state == BUTTON_CLICKED)
        ev = EV_CLICK;
    else if (state == BUTTON_PRESSED_LONG)
        ev = EV_LONG;
    else
        return;
    xQueueSend(button_queue, &ev, 0);
}

static button_t boot_button = {
    .gpio = BUTTON_GPIO,
    .internal_pull = true,
    .pressed_level = 0,
    .autorepeat = false,
    .callback = on_button,
};

void display_task(void *pvParameter)
{
    // Configure SPI bus
    spi_bus_config_t cfg = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 0,
        .flags = 0
    };
    ESP_ERROR_CHECK(spi_bus_initialize(HOST, &cfg, 0));

    // Configure device
    max7219_t dev = {
        .cascade_size = CASCADE_SIZE,
        .digits = 0,
        .mirrored = true
    };
    ESP_ERROR_CHECK(max7219_init_desc(&dev, HOST, MAX7219_MAX_CLOCK_SPEED_HZ, PIN_CS));
    ESP_ERROR_CHECK(max7219_init(&dev));
    max7219_set_brightness(&dev, 0);

    size_t mode = 0;
    modes[mode].enter(xTaskGetTickCount() * portTICK_PERIOD_MS);

    while (1) {
        button_event_t ev;
        if (xQueueReceive(button_queue, &ev, pdMS_TO_TICKS(TICK_MS))) {
            if (ev == EV_CLICK) {
                mode = (mode + 1) % MODE_COUNT;
                ESP_LOGI(TAG, "Click: mode %s", modes[mode].name);
                modes[mode].enter(xTaskGetTickCount() * portTICK_PERIOD_MS);
            } else {
                ESP_LOGI(TAG, "Long press in mode %s", modes[mode].name);
                if (modes[mode].on_button)
                    modes[mode].on_button(ev);
            }
        }

        if (modes[mode].render(xTaskGetTickCount() * portTICK_PERIOD_MS))
            fb_flush(&dev);
    }
}

void app_main()
{
    set_initial_time();
    button_queue = xQueueCreate(8, sizeof(button_event_t));
    ESP_ERROR_CHECK(button_init(&boot_button));
    xTaskCreatePinnedToCore(display_task, "display", 4096, NULL, 5, NULL, APP_CPU_NUM);
}
