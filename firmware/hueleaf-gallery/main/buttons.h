#pragma once
#include "esp_err.h"
/* Viewing images: BOOT (GPIO28) short = previous; AXP2101 PEK short = next.
 * BOOT long opens/confirms the picker; PEK long stays the PMIC's hardware
 * power-off. Each accepted press pulses the AXP2101 CHGLED (see axp2101.c). */
esp_err_t buttons_start(void);
