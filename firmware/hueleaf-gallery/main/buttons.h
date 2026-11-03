#pragma once
/* v1 button map: BOOT (GPIO28) short = next; AXP2101 PEK short = previous.
 * Long presses are reserved (unassigned); PEK long stays the PMIC's hardware
 * power-off. Each accepted press pulses the AXP2101 CHGLED (see axp2101.c). */
void buttons_start(void);
