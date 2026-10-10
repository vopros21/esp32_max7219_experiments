#include <string.h>
#include <esp_err.h>
#include <max7219.h>
#include <esp_idf_lib_helpers.h>

#include "display.h"
#include "font8x8.h"
#include "font_digits.h"

#define HOST HELPER_SPI_HOST_DEFAULT

#define CASCADE_SIZE (WIDTH / 8)
#define PIN_MOSI 4
#define PIN_CS 5
#define PIN_CLK 6

#define LETTER_SPACING 1           // empty columns between letters
#define SPACE_WIDTH 3              // width of ' ' in columns

uint8_t fb[WIDTH];

static max7219_t dev = {
    .cascade_size = CASCADE_SIZE,
    .digits = 0,
    .mirrored = true
};

void display_init(void)
{
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

    ESP_ERROR_CHECK(max7219_init_desc(&dev, HOST, MAX7219_MAX_CLOCK_SPEED_HZ, PIN_CS));
    ESP_ERROR_CHECK(max7219_init(&dev));
    max7219_set_brightness(&dev, 0);
}

// Each 8x8 module is rotated by 90 degrees, so a MAX7219 digit register holds a
// horizontal row of one module, not a column of the whole display. For pixel (x, y):
//   digit = (x / 8) * 8 + y,  bit = x % 8
// (digit index as passed to max7219_set_digit with dev.mirrored = true).
void fb_flush(void)
{
    for (int m = 0; m < CASCADE_SIZE; m++) {
        for (int y = 0; y < 8; y++) {
            uint8_t row = 0;
            for (int b = 0; b < 8; b++) {
                if (fb[m * 8 + b] & (1 << y))
                    row |= 1 << b;
            }
            max7219_set_digit(&dev, m * 8 + y, row);
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

// Letters are proportional: empty columns on both sides of a glyph are trimmed.
// After the last letter the scroller emits WIDTH blank columns and starts over.
void scroller_start(scroller_t *s, const char *text)
{
    s->text = text;
    s->p = text;
    s->glyph = NULL;
    s->x = s->last = 0;
    s->blank = 0;
}

uint8_t scroller_next(scroller_t *s)
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

void draw_narrow(const char *str, bool colon)
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
