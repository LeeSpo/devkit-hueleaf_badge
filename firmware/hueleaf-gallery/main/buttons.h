#pragma once
#include <stdbool.h>
#include "esp_err.h"
typedef enum {
    BUTTON_NONE, BUTTON_BOOT_SHORT, BUTTON_BOOT_LONG, BUTTON_PEK_SHORT
} button_action_t;
/* Viewing images: BOOT (GPIO28) short = previous; AXP2101 PEK short = next.
 * BOOT long opens/confirms the picker; PEK long stays the PMIC's hardware
 * power-off. Each accepted press pulses the AXP2101 CHGLED (see axp2101.c). */
esp_err_t buttons_start(void);
/* Lightweight startup only: caller owns runtime I/O admission and polls at 10ms.
 * No worker tasks, TF access or display initialization are needed. */
esp_err_t buttons_poll_wake(button_action_t *action);
/* Dispatch one completed gesture once the gallery is ready. */
bool buttons_dispatch(button_action_t action);
