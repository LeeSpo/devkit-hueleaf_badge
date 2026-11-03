#pragma once
/* Panel framebuffer helpers. Layout = official driver/web pipeline (otp6-v1):
 * 172800 bytes, column-major from logical x=479 down to x=0; each column is
 * 360 bytes top->bottom; byte = (ink(y even) << 4) | ink(y odd).
 * Ink codes: 0 black, 1 white, 2 yellow, 3 red, 5 blue, 6 green. */
#include <stdint.h>
#include "screen_driver.h"
#include "msg_bitmap.h"
static inline void frame_set(uint8_t *f, int x, int y, uint8_t ink)
{
    if ((unsigned)x >= SCREEN_WIDTH || (unsigned)y >= SCREEN_HEIGHT) return;
    uint8_t *b = &f[(size_t)(SCREEN_WIDTH - 1 - x) * (SCREEN_HEIGHT / 2) + (size_t)(y >> 1)];
    *b = (y & 1) ? (uint8_t)((*b & 0xF0) | ink) : (uint8_t)((*b & 0x0F) | (ink << 4));
}
void frame_fill(uint8_t *f, uint8_t ink);
void frame_rect(uint8_t *f, int x, int y, int w, int h, uint8_t ink);
/* ASCII subset 0-9 A-Z % / : - . space; 5x7 font, scaled. Returns end x. */
int frame_text(uint8_t *f, int x, int y, const char *s, int scale, uint8_t ink);
int frame_text_width(const char *s, int scale);
void frame_bitmap_centered(uint8_t *f, const msg_bitmap_t *bm, int cy, uint8_t ink);
void frame_bits(uint8_t *f, int x0, int y0, int w, int h, const uint8_t *bits, uint8_t ink);
#define FRAME_STRIP_H 28
