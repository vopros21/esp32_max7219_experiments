#include <stdio.h>
#include <string.h>
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
#define DEMO_STEP_MS 40            // demo animation speed

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

// Demo mode: a dot bouncing around the 32x8 display. Placeholder until Clock.

static int demo_x, demo_y, demo_dx, demo_dy;
static uint32_t demo_last_ms;

static void demo_enter(uint32_t now_ms)
{
    demo_x = 0;
    demo_y = 0;
    demo_dx = 1;
    demo_dy = 1;
    demo_last_ms = now_ms - DEMO_STEP_MS;  // draw the first frame right away
}

static bool demo_render(uint32_t now_ms)
{
    if (now_ms - demo_last_ms < DEMO_STEP_MS)
        return false;
    demo_last_ms = now_ms;

    memset(fb, 0, sizeof(fb));
    fb[demo_x] = 1 << demo_y;

    if (demo_x + demo_dx < 0 || demo_x + demo_dx >= WIDTH)
        demo_dx = -demo_dx;
    if (demo_y + demo_dy < 0 || demo_y + demo_dy >= 8)
        demo_dy = -demo_dy;
    demo_x += demo_dx;
    demo_y += demo_dy;
    return true;
}

static const display_mode_t modes[] = {
    { "Text", text_enter, text_render, text_on_button },
    { "Demo", demo_enter, demo_render, NULL },
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
    button_queue = xQueueCreate(8, sizeof(button_event_t));
    ESP_ERROR_CHECK(button_init(&boot_button));
    xTaskCreatePinnedToCore(display_task, "display", 4096, NULL, 5, NULL, APP_CPU_NUM);
}
