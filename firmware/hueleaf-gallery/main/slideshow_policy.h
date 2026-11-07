#pragma once
#include <stdbool.h>
#include <stdint.h>

#define SLIDESHOW_DEFAULT_MIN 60u
#define SLIDESHOW_MAX_MIN 10080u
#define SLIDESHOW_WINDOW_MS 60000

typedef struct {
    bool enabled, usb, fault;
    uint32_t interval_min;
    int64_t deadline_ms, awake_until_ms;
} slideshow_policy_t;

typedef enum {
    SLIDE_OFF, SLIDE_USB, SLIDE_FAULT, SLIDE_EMPTY, SLIDE_BUSY,
    SLIDE_WINDOW, SLIDE_NEXT, SLIDE_SLEEP
} slideshow_action_t;

bool slideshow_interval_valid(uint32_t minutes);
void slideshow_policy_init(slideshow_policy_t *p, bool enabled, uint32_t minutes,
                           int64_t now_ms, bool timer_boot);
void slideshow_policy_activity(slideshow_policy_t *p, int64_t now_ms);
void slideshow_policy_rendered(slideshow_policy_t *p, int64_t now_ms, bool success);
slideshow_action_t slideshow_policy_step(slideshow_policy_t *p, int64_t now_ms,
                                         bool usb, int images, bool busy);
const char *slideshow_action_name(slideshow_action_t action);
