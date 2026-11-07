#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "slideshow_policy.h"

int main(void)
{
    slideshow_policy_t p;
    assert(!slideshow_interval_valid(0) && !slideshow_interval_valid(UINT32_MAX));
    assert(slideshow_interval_valid(1) && slideshow_interval_valid(10080));
    assert(!slideshow_interval_valid(10081));
    slideshow_policy_init(&p, false, 60, 0, false);
    assert(slideshow_policy_step(&p, 100000, false, 3, false) == SLIDE_OFF);
    slideshow_policy_init(&p, true, 2, 0, false);
    assert(slideshow_policy_step(&p, 59999, false, 3, false) == SLIDE_WINDOW);
    assert(slideshow_policy_step(&p, 60000, false, 3, false) == SLIDE_SLEEP);
    assert(p.deadline_ms == 120000);
    assert(slideshow_policy_step(&p, 120000, false, 3, true) == SLIDE_BUSY);
    assert(slideshow_policy_step(&p, 180000, false, 3, false) == SLIDE_NEXT);
    slideshow_policy_rendered(&p, 195000, true);
    assert(p.deadline_ms == 315000);  /* no catch-up flashes */
    assert(slideshow_policy_step(&p, 195000, false, 3, false) == SLIDE_SLEEP);
    assert(slideshow_policy_step(&p, 200000, true, 3, false) == SLIDE_USB);
    assert(slideshow_policy_step(&p, 999000, true, 3, false) == SLIDE_USB);
    assert(slideshow_policy_step(&p, 1000000, false, 3, false) == SLIDE_WINDOW);
    assert(p.deadline_ms == 1120000 && p.awake_until_ms == 1060000);
    slideshow_policy_activity(&p, 1050000);
    assert(p.awake_until_ms == 1110000 && p.deadline_ms == 1170000);
    slideshow_policy_rendered(&p, 1070000, true);
    assert(p.deadline_ms == 1190000 && p.awake_until_ms == 1110000);
    slideshow_policy_rendered(&p, 1090000, false);
    assert(slideshow_policy_step(&p, 1200000, false, 3, false) == SLIDE_FAULT);
    slideshow_policy_init(&p, true, 1, 0, true);
    assert(slideshow_policy_step(&p, 0, false, 3, false) == SLIDE_SLEEP);
    assert(slideshow_policy_step(&p, 60000, false, 1, false) == SLIDE_SLEEP);
    assert(p.deadline_ms == 120000); /* one image, no unnecessary refresh */
    assert(slideshow_policy_step(&p, 120000, false, 0, false) == SLIDE_EMPTY);
    slideshow_policy_init(&p, true, 10080, INT64_C(5000000000), true);
    assert(p.deadline_ms == INT64_C(5604800000));
    assert(!strcmp(slideshow_action_name(SLIDE_USB), "usb_paused"));
    return 0;
}
