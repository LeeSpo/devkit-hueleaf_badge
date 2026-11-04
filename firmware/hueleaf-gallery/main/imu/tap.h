#pragma once
#include <stdbool.h>
/* Double-tap = next image (MSA321 on I2C0 @0x62). Off unless enabled via
 * hueleaf.cfg `tap_next=1` (same rules as mono_fast: file > NVS > default off). */
void tap_set_enabled(bool on);
bool tap_enabled(void);
void tap_start(void);   /* no-op when disabled or IMU missing */
