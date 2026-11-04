#pragma once
#include "esp_err.h"
/* v1 button map: BOOT (GPIO28) short = next; AXP2101 PEK short = previous.
 * Long presses are reserved (unassigned); PEK long stays the PMIC's hardware
 * power-off. Each accepted press pulses the AXP2101 CHGLED (see axp2101.c). */
esp_err_t buttons_start(void);
