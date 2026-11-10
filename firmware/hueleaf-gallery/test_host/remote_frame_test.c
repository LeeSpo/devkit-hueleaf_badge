#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "frame.h"
int main(void)
{
    uint8_t guarded[FRAME_UI_BYTES + 2];
    memset(guarded, 0x5a, sizeof(guarded));
    uint8_t *frame = guarded + 1;
    frame_fill(frame, SCREEN_COLOR_WHITE);
    frame_set(frame, 479, 719, SCREEN_COLOR_BLACK);
    assert(frame[FRAME_UI_BYTES-1] == 0xfe);
    frame_set(frame, 0, 0, SCREEN_COLOR_BLACK);
    assert(frame[0] == 0x7f);
    frame_set(frame, -1, 0, SCREEN_COLOR_BLACK);
    frame_set(frame, 480, 0, SCREEN_COLOR_BLACK);
    frame_set(frame, 0, 720, SCREEN_COLOR_BLACK);
    frame_rect(frame, 2, 3, 20, 10, SCREEN_COLOR_BLACK);
    for (int x = 2; x < 22; x++) for (int y = 3; y < 13; y++) {
        unsigned pixel = x * 720 + y;
        assert(!(frame[pixel/8] & (0x80 >> (pixel%8))));
    }
    assert(guarded[0] == 0x5a && guarded[sizeof(guarded)-1] == 0x5a);
}
