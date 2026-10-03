#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <esp_idf_version.h>
#include <max7219.h>
#include <esp_idf_lib_helpers.h>

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

static const char *TEXT = "Hello, ESP32-S3! 0123456789";


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

// Shift the whole display one column left and put `col` into the rightmost column.
static void push_column(max7219_t *dev, uint8_t col, uint32_t delay_ms)
{
    memmove(fb, fb + 1, WIDTH - 1);
    fb[WIDTH - 1] = col;
    fb_flush(dev);
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
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

// Scroll a string from right to left until it fully leaves the display.
// Letters are proportional: empty columns on both sides of a glyph are trimmed.
void scroll_text(max7219_t *dev, const char *text, uint32_t delay_ms)
{
    for (const char *p = text; *p; p++) {
        const uint8_t *glyph = get_glyph(*p);

        uint8_t used = 0;
        for (int y = 0; y < 8; y++)
            used |= glyph[y];

        if (!used) {
            for (int i = 0; i < SPACE_WIDTH; i++)
                push_column(dev, 0, delay_ms);
            continue;
        }

        int first = __builtin_ctz(used);
        int last = 31 - __builtin_clz(used);
        for (int x = first; x <= last; x++)
            push_column(dev, glyph_column(glyph, x), delay_ms);
        for (int i = 0; i < LETTER_SPACING; i++)
            push_column(dev, 0, delay_ms);
    }

    for (int i = 0; i < WIDTH; i++)
        push_column(dev, 0, delay_ms);
}

void task(void *pvParameter)
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

    while (1)
    {
        scroll_text(&dev, TEXT, CONFIG_EXAMPLE_SCROLL_DELAY);
    }
}

void button_task(void *pvParameter)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    while (1) {
        int level = gpio_get_level(BUTTON_GPIO);
        // Process button state here or send it to another task
        printf("Button state: %d\n", level);
        vTaskDelay(pdMS_TO_TICKS(1000));  // Poll every 1000 ms
    }
}

void app_main()
{
    xTaskCreatePinnedToCore(task, "task", configMINIMAL_STACK_SIZE * 3, NULL, 5, NULL, APP_CPU_NUM);
    xTaskCreatePinnedToCore(button_task, "button_task", configMINIMAL_STACK_SIZE * 3, NULL, 5, NULL, APP_CPU_NUM);
}
