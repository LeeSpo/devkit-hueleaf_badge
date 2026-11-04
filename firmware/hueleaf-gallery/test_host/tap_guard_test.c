#include <stdio.h>
#include "tap_guard.h"
static int fails;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); fails++; } } while (0)
static tap_guard_t g;
/* run n ticks still (z=1000mg) or shaking; return count of accepted taps */
static int run(int n, int shake, int tap_at)
{
    int acc = 0;
    for (int i = 0; i < n; i++) {
        int16_t x = shake ? ((i & 1) ? 300 : -300) : 0;
        acc += tap_guard_tick(&g, x, 0, 1000, i == tap_at, TAP_GUARD_STILL_MS);
    }
    return acc;
}
int main(void)
{
    tap_guard_init(&g);
    CHECK(run(5, 0, 2) == 0, "tap before enough history rejected");
    tap_guard_init(&g);
    CHECK(run(60, 0, 59) == 1, "still then tap accepted");
    tap_guard_init(&g);
    CHECK(run(60, 1, 59) == 0, "walking tap rejected");
    /* shaking only inside the tap window (the taps themselves) is fine */
    tap_guard_init(&g);
    run(50, 0, -1);
    int a = 0;
    for (int i = 0; i < 10; i++) a += tap_guard_tick(&g, (i & 1) ? 400 : 0, 0, 1000, i == 9, TAP_GUARD_STILL_MS);
    CHECK(a == 1, "motion within tap window tolerated");
    /* cooldown */
    tap_guard_init(&g);
    run(60, 0, 59);
    CHECK(run(10, 0, 5) == 0, "second tap within cooldown rejected");
    CHECK(run(60, 0, 59) == 1, "tap after cooldown accepted");
    /* moved 1s ago -> rejected */
    tap_guard_init(&g);
    run(60, 0, -1);
    tap_guard_tick(&g, 500, 0, 1000, false, TAP_GUARD_STILL_MS);
    CHECK(run(20, 0, 19) == 0, "recent motion rejects tap");
    /* picker: 0.5 s still is enough */
    tap_guard_init(&g);
    run(60, 0, -1);
    tap_guard_tick(&g, 500, 0, 1000, false, TAP_GUARD_STILL_MS);
    a = 0;
    for (int i = 0; i < 25; i++) a += tap_guard_tick(&g, 0, 0, 1000, i == 24, TAP_GUARD_STILL_SHORT_MS);
    CHECK(a == 1, "picker: tap 1.2s after motion accepted with short still");
    tap_guard_init(&g);
    run(60, 1, -1);
    a = 0;
    for (int i = 0; i < 5; i++) a += tap_guard_tick(&g, 0, 0, 1000, i == 4, TAP_GUARD_STILL_SHORT_MS);
    CHECK(a == 0, "picker: tap right after shaking still rejected");
    printf("tap_guard: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
