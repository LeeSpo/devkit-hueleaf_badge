#include "tap_guard.h"
#include <string.h>
#include <stdlib.h>
#define TAPWIN_SLOTS (TAP_GUARD_TAPWIN_MS / TAP_GUARD_TICK_MS)

void tap_guard_init(tap_guard_t *g) { memset(g, 0, sizeof(*g)); }

static bool was_still(const tap_guard_t *g)
{
    if (g->filled < TAP_GUARD_SLOTS) return false;   /* not enough history yet */
    /* slots older than the tap window, i.e. ages TAPWIN_SLOTS .. SLOTS-1 */
    for (int age = TAPWIN_SLOTS; age < TAP_GUARD_SLOTS; age++) {
        int i = (g->head - 1 - age + 2 * TAP_GUARD_SLOTS) % TAP_GUARD_SLOTS;
        if (g->moved[i]) return false;
    }
    return true;
}

bool tap_guard_tick(tap_guard_t *g, int16_t x, int16_t y, int16_t z, bool dtap)
{
    bool m = false;
    if (g->have_prev)
        m = abs(x - g->px) > TAP_GUARD_MOTION_MG || abs(y - g->py) > TAP_GUARD_MOTION_MG ||
            abs(z - g->pz) > TAP_GUARD_MOTION_MG;
    g->px = x; g->py = y; g->pz = z; g->have_prev = true;
    g->moved[g->head] = m;
    g->head = (g->head + 1) % TAP_GUARD_SLOTS;
    if (g->filled < TAP_GUARD_SLOTS) g->filled++;
    if (g->cooldown_ticks > 0) { g->cooldown_ticks--; return false; }
    if (!dtap || !was_still(g)) return false;
    g->cooldown_ticks = TAP_GUARD_COOLDOWN_MS / TAP_GUARD_TICK_MS;
    return true;
}
