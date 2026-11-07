#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "slideshow_policy.h"

typedef struct {
    bool enabled;
    uint32_t interval_min;
    const char *state;
    bool tap_wake;
} slideshow_snapshot_t;

/* Release GPIO holds and consume the retained marker before board startup. */
void slideshow_boot_init(void);
esp_err_t slideshow_load_config(void);
/* Only a validated timer wake may advance the retained physical image. */
bool slideshow_boot_name(char *name, size_t cap);
/* A validated IMU wake restores the physical image without advancing/redrawing. */
bool slideshow_tap_boot_name(char *name, size_t cap);
esp_err_t slideshow_start(void);
void slideshow_activity(void);
bool slideshow_due(void);
void slideshow_frame_done(bool success);
void slideshow_inhibit(void);
void slideshow_snapshot(slideshow_snapshot_t *out);
/* Caller owns runtime storage admission; TF commit is the durable source. */
esp_err_t slideshow_set(bool enabled, uint32_t interval_min);
esp_err_t slideshow_set_options(bool enabled, uint32_t interval_min, bool tap_wake);
