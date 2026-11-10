#pragma once
#include <stdbool.h>
#include "esp_err.h"

/* Before board/I2C init: stop LP accesses and consume the retained event. */
void usb_wake_boot_init(void);
bool usb_wake_boot(void);
void usb_wake_ready(void);
void usb_wake_activity(void);
/* Only a USB-only boot may bypass the usual battery operation window. */
bool usb_wake_probe_only(void);
/* Sleep owner polls while awake; blocking I2C never runs in a critical section. */
void usb_wake_poll(void);
bool usb_wake_blocked(void);
/* Caller owns runtime sleep admission. Success holds I2C until sleep/cancel.
 * NOT_FINISHED means insertion/host activity: restore peripherals and retry. */
esp_err_t usb_wake_prepare(void);
void usb_wake_cancel(void);
