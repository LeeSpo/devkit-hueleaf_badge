#include "slideshow_policy.h"

bool slideshow_interval_valid(uint32_t minutes)
{
    return minutes >= 1 && minutes <= SLIDESHOW_MAX_MIN;
}

void slideshow_policy_init(slideshow_policy_t *p, bool enabled, uint32_t minutes,
                           int64_t now_ms, bool timer_boot)
{
    *p = (slideshow_policy_t) {
        .enabled = enabled, .interval_min = minutes,
        .deadline_ms = now_ms + (int64_t)minutes * 60000,
        .awake_until_ms = now_ms + (timer_boot ? 0 : SLIDESHOW_WINDOW_MS),
    };
}

void slideshow_policy_activity(slideshow_policy_t *p, int64_t now_ms)
{
    p->awake_until_ms = now_ms + SLIDESHOW_WINDOW_MS;
    p->deadline_ms = now_ms + (int64_t)p->interval_min * 60000;
}

void slideshow_policy_rendered(slideshow_policy_t *p, int64_t now_ms, bool success)
{
    if (!success) p->fault = true;
    else p->deadline_ms = now_ms + (int64_t)p->interval_min * 60000;
}

slideshow_action_t slideshow_policy_step(slideshow_policy_t *p, int64_t now_ms,
                                         bool usb, int images, bool busy)
{
    return slideshow_policy_step_selection(p, now_ms, usb, images, images, true, busy);
}

slideshow_action_t slideshow_policy_step_selection(slideshow_policy_t *p, int64_t now_ms,
    bool usb, int images, int selected, bool current_selected, bool busy)
{
    if (p->usb && !usb) slideshow_policy_activity(p, now_ms);
    p->usb = usb;
    if (!p->enabled) return SLIDE_OFF;
    if (usb) return SLIDE_USB;
    if (p->fault) return SLIDE_FAULT;
    if (images < 1) return SLIDE_EMPTY;
    if (busy) return SLIDE_BUSY;
    if (now_ms < p->awake_until_ms) return SLIDE_WINDOW;
    if (now_ms >= p->deadline_ms) {
        if (selected > 1 || (selected == 1 && !current_selected)) return SLIDE_NEXT;
        /* Empty selection or its sole image already displayed: keep the
         * physical frame, but still use bounded sleeps and tap wake. */
        p->deadline_ms = now_ms + (int64_t)p->interval_min * 60000;
    }
    return SLIDE_SLEEP;
}

const char *slideshow_action_name(slideshow_action_t action)
{
    static const char *const names[] = {
        "off", "usb_paused", "fault", "empty", "busy", "awake_window", "next", "sleeping"
    };
    return names[action];
}
