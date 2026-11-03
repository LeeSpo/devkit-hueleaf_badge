#include "frame.h"
#include <string.h>
#include <ctype.h>
void frame_fill(uint8_t *f, uint8_t ink) { memset(f, (ink << 4) | ink, SCREEN_FRAME_BYTES); }
void frame_rect(uint8_t *f, int x, int y, int w, int h, uint8_t ink)
{
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++) frame_set(f, xx, yy, ink);
}
/* 5x7 glyphs, 7 rows of 5 bits (bit4 = left). */
static const char k_chars[] = "0123456789%/:-. ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const uint8_t k_font[][7] = {
    {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{31,2,4,2,1,17,14},
    {2,6,10,18,31,2,2},{31,16,30,1,1,17,14},{6,8,16,30,17,17,14},{31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14},{14,17,17,15,1,2,12},{24,25,2,4,8,19,3},{1,1,2,4,8,16,16},
    {0,12,12,0,12,12,0},{0,0,0,31,0,0,0},{0,0,0,0,0,12,12},{0,0,0,0,0,0,0},
    {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},{28,18,17,17,17,18,28},
    {31,16,16,30,16,16,31},{31,16,16,30,16,16,16},{14,17,16,23,17,17,15},{17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14},{7,2,2,2,2,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17},{17,17,25,21,19,17,17},{14,17,17,17,17,17,14},{30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13},{30,17,17,30,20,18,17},{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
};
int frame_text_width(const char *s, int scale) { return (int)strlen(s) * 6 * scale; }
int frame_text(uint8_t *f, int x, int y, const char *s, int scale, uint8_t ink)
{
    for (; *s; s++, x += 6 * scale) {
        const char *p = strchr(k_chars, toupper((unsigned char)*s));
        if (!p) p = k_chars + 15; /* space */
        const uint8_t *g = k_font[p - k_chars];
        for (int r = 0; r < 7; r++)
            for (int c = 0; c < 5; c++)
                if (g[r] & (16 >> c)) frame_rect(f, x + c * scale, y + r * scale, scale, scale, ink);
    }
    return x;
}
void frame_bitmap_centered(uint8_t *f, const msg_bitmap_t *bm, int cy, uint8_t ink)
{
    const int stride = (bm->w + 7) / 8, x0 = (SCREEN_WIDTH - bm->w) / 2, y0 = cy - bm->h / 2;
    for (int y = 0; y < bm->h; y++)
        for (int x = 0; x < bm->w; x++)
            if (bm->bits[y * stride + x / 8] & (0x80 >> (x % 8))) frame_set(f, x0 + x, y0 + y, ink);
}
void frame_bits(uint8_t *f, int x0, int y0, int w, int h, const uint8_t *bits, uint8_t ink)
{
    const int stride = (w + 7) / 8;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (bits[y * stride + x / 8] & (0x80 >> (x % 8))) frame_set(f, x0 + x, y0 + y, ink);
}
