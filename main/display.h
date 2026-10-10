#pragma once

#include <stdint.h>
#include <stdbool.h>

#define WIDTH 32  // display width in pixels (4 modules x 8)

// Logical framebuffer: one byte per column, x = 0 is the leftmost column.
// Bit y of a column is the pixel in row y (y = 0 is the top row).
// Everything draws here; only fb_flush() talks to the MAX7219s.
extern uint8_t fb[WIDTH];

void display_init(void);
void fb_flush(void);

// Produces the columns of a string one at a time, so callers never block.
typedef struct {
    const char *text;
    const char *p;          // next character to load
    const uint8_t *glyph;   // current glyph, NULL between glyphs
    int x, last;            // remaining glyph columns: x..last
    int blank;              // blank columns to emit before anything else
} scroller_t;

void scroller_start(scroller_t *s, const char *text);
uint8_t scroller_next(scroller_t *s);

// Draw digits, ':' and '.' with the narrow 5x7 font, centred. A ':' with
// `colon` false is left blank but keeps its width, so nothing jumps.
void draw_narrow(const char *str, bool colon);
