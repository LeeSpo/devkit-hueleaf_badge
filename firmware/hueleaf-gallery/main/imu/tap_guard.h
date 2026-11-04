#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Anti-false-trigger guard for "double-tap = next image" (pure C, host-tested).
 * Feed one accelerometer sample every TAP_GUARD_TICK_MS. A double tap reported
 * by the IMU is accepted only if the frame was still for TAP_GUARD_STILL_MS
 * right before the tap window (so walking / a swinging bag never fires), and
 * not within TAP_GUARD_COOLDOWN_MS of the previous accepted tap. */
#define TAP_GUARD_TICK_MS      50
#define TAP_GUARD_TAPWIN_MS    700    /* the taps themselves move the frame */
#define TAP_GUARD_STILL_MS     1500
#define TAP_GUARD_STILL_SHORT_MS 500   /* picker: frame is usually hand-held */
#define TAP_GUARD_COOLDOWN_MS  1500
#define TAP_GUARD_MOTION_MG    80     /* per-axis change between samples */
#define TAP_GUARD_SLOTS ((TAP_GUARD_TAPWIN_MS + TAP_GUARD_STILL_MS) / TAP_GUARD_TICK_MS)
typedef struct {
    int16_t px, py, pz;
    bool have_prev;
    uint8_t moved[TAP_GUARD_SLOTS];   /* ring, newest at head-1 */
    int head, filled;
    int cooldown_ticks;
} tap_guard_t;
void tap_guard_init(tap_guard_t *g);
/* One tick: sample in mg; dtap = IMU flagged a double tap this tick.
 * Returns true when the tap should change the image. */
/* still_ms <= TAP_GUARD_STILL_MS: how long the frame must have been still. */
bool tap_guard_tick(tap_guard_t *g, int16_t x_mg, int16_t y_mg, int16_t z_mg, bool dtap, int still_ms);
