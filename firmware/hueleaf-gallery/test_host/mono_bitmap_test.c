#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "screen_driver_priv.h"
#include "screen_driver.h"

TickType_t g_ticks;
uint32_t g_us;
static uint8_t last_cmd, expected[3][SCREEN_FRAME_BYTES], reference_prev[SCREEN_MONO_BYTES];
static size_t plane_bytes;
static int planes, expected_planes, busyflip, failure, data_calls;
static bool checking = true;

esp_err_t sd_write_cmd(uint8_t cmd)
{
    last_cmd = cmd;
    if (cmd == 0x10) {
        if (checking && planes) assert(plane_bytes == SCREEN_FRAME_BYTES);
        planes++; plane_bytes = 0;
    }
    return ESP_OK;
}
esp_err_t sd_write_data(const uint8_t *data, size_t len)
{
    if (last_cmd != 0x10) return ESP_OK;
    if (failure == 1 && ++data_calls == 5) return ESP_ERR_INVALID_RESPONSE;
    if (checking) {
        assert(planes <= expected_planes && plane_bytes + len <= SCREEN_FRAME_BYTES);
        assert(!memcmp(data, expected[planes - 1] + plane_bytes, len));
    }
    plane_bytes += len;
    return ESP_OK;
}
esp_err_t sd_write_data_byte(uint8_t data) { return sd_write_data(&data, 1); }
esp_err_t sd_wait_busy(void) { return ESP_OK; }
int sd_busy_level(void)
{
    if (failure == 2 && last_cmd == 0x12) return 1; /* REF never asserts */
    if (failure == 3 && last_cmd == 0x02) return 0; /* POF never releases */
    return (busyflip++) & 1;
}

/* FrameFilm oracle uses a separate controller-order 1bpp black bitmap.
 * Compare EVERY DTM plane, including full_clear and periodic mono_clear. */
static uint8_t reverse(uint8_t v)
{
    uint8_t out = 0;
    for (int bit = 0; bit < 8; bit++) out |= ((v >> bit) & 1) << (7 - bit);
    return out;
}
static void reference_write(const uint8_t *bitmap)
{
    uint8_t *out = expected[expected_planes++];
    for (int row = 0; row < SCREEN_WIDTH; row++) {
        for (int x = 0; x < SCREEN_HEIGHT / 8; x++) {
            uint8_t black = bitmap ? (uint8_t)~bitmap[(SCREEN_WIDTH - 1 - row) * (SCREEN_HEIGHT / 8) + x] : 0;
            size_t at = row * (SCREEN_HEIGHT / 8) + x;
            uint8_t old = reference_prev[at], next = (uint8_t)~reverse(black);
            reference_prev[at] = next;
            for (int j = 0; j < 4; j++) {
                *out++ = ((old << 5) & 0x20) | ((next << 4) & 0x10) | (old & 2) | ((next >> 1) & 1);
                old >>= 2; next >>= 2;
            }
        }
    }
}
static void prepare(const uint8_t *bitmap, bool first, bool clean)
{
    checking = true; planes = expected_planes = 0; plane_bytes = 0;
    if (first) {
        memset(reference_prev, 0, sizeof(reference_prev));
        memset(expected[expected_planes++], 0, SCREEN_FRAME_BYTES);
    } else if (clean) reference_write(NULL);
    reference_write(bitmap);
}
static void show(const uint8_t *bitmap, uint8_t *previous, bool first, bool clean)
{
    prepare(bitmap, first, clean);
    assert(mono_fast_show_bitmap_locked(bitmap, previous) == ESP_OK);
    assert(planes == expected_planes && plane_bytes == SCREEN_FRAME_BYTES);
    assert(!memcmp(previous, reference_prev, SCREEN_MONO_BYTES));
}
int main(void)
{
    static uint8_t guarded_bitmap[SCREEN_MONO_BYTES + 2], guarded_previous[SCREEN_MONO_BYTES + 2];
    uint8_t *bitmap = guarded_bitmap + 1, *previous = guarded_previous + 1;
    memset(guarded_bitmap, 0xa5, sizeof(guarded_bitmap));
    memset(guarded_previous, 0x5a, sizeof(guarded_previous));
    srand(17);
    for (int frame = 0; frame < 45; frame++) {
        if (frame == 0) memset(bitmap, 0, SCREEN_MONO_BYTES);
        else if (frame == 1) memset(bitmap, 255, SCREEN_MONO_BYTES);
        else if (frame == 2) {
            /* Asymmetric edge pixels detect mirrored columns/bit order. */
            bitmap[0] &= 0x7f; bitmap[SCREEN_MONO_BYTES-1] &= 0xfe;
        } else for (size_t i = 0; i < SCREEN_MONO_BYTES; i++) bitmap[i] = rand();
        uint8_t before[SCREEN_MONO_BYTES]; memcpy(before, bitmap, sizeof(before));
        show(bitmap, previous, frame == 0, frame > 0 && frame % 20 == 0);
        assert(!memcmp(before, bitmap, sizeof(before)));
        assert(guarded_bitmap[0] == 0xa5 && guarded_bitmap[sizeof(guarded_bitmap)-1] == 0xa5);
        assert(guarded_previous[0] == 0x5a && guarded_previous[sizeof(guarded_previous)-1] == 0x5a);
    }
    for (failure = 1; failure <= 3; failure++) {
        checking = false; data_calls = 0;
        assert(mono_fast_show_bitmap_locked(bitmap, previous) != ESP_OK);
        assert(!mono_fast_active());
        int saved = failure; failure = 0;
        show(bitmap, previous, true, false); /* corrupted baseline never reused */
        failure = saved;
    }
    failure = 0;
    mono_fast_drop_stash(); show(bitmap, previous, true, false);
    mono_fast_forget(); show(bitmap, previous, true, false);
    static uint8_t other[SCREEN_MONO_BYTES];
    show(bitmap, other, true, false); /* changed backing store forces clear */
    show(bitmap, previous, true, false);
    static uint8_t legacy[SCREEN_FRAME_BYTES];
    memset(legacy, 0x11, sizeof(legacy));
    checking = false;
    assert(mono_fast_show_locked(legacy) == ESP_OK);
    show(bitmap, previous, true, false); /* switching 4bpp -> 1bpp drops stash */
    puts("OK compact MonoFast: all 45 frames/cleanup planes match FrameFilm; bounds, SPI/REF/POF recovery, baseline ownership");
    return 0;
}
