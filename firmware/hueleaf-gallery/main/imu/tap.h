#pragma once
#include <stdbool.h>
#include "esp_err.h"
/* Double-tap = next image (MSA321 on I2C0 @0x62). Off unless enabled via
 * hueleaf.cfg `tap_next=1` (same rules as mono_fast: file > NVS > default off). */
void tap_set_enabled(bool on);
bool tap_enabled(void);
void tap_start(void);   /* no-op when disabled or IMU missing */
/* Caller has frozen runtime I/O. Independent of tap_next; lazy IMU init.
 * ESP_ERR_NOT_FINISHED means a tap is pending; defer sleep for a new window. */
esp_err_t tap_wake_prepare(void);
/* Undo wake pin routing after cancelled sleep; never touches SPI pins. */
void tap_wake_release(void);
