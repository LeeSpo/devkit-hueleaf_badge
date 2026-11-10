#pragma once
#include <stdbool.h>
#include <stdint.h>

#define USB_WATCH_MAGIC 0x55534231u
#define USB_WATCH_PERIOD_US 500000u
#define USB_WATCH_ADDRESS 0x34
#define USB_WATCH_STATUS_REG 0x00
#define USB_WATCH_VBUS_GOOD (1u << 5)
#define USB_WATCH_INSERTED 1u
#define USB_WATCH_FAULT 2u

typedef struct {
    uint32_t present, errors, event;
} usb_watch_t;

/* Shared by the LP program and host regression tests. An event stays latched
 * until the HP CPU stops the watcher, including insertion just before sleep. */
static inline void usb_watch_sample(volatile usb_watch_t *s, bool ok, uint8_t status)
{
    if (s->event) return;
    if (!ok) {
        if (++s->errors >= 3) s->event = USB_WATCH_FAULT;
        return;
    }
    s->errors = 0;
    bool present = (status & USB_WATCH_VBUS_GOOD) != 0;
    if (present && !s->present) s->event = USB_WATCH_INSERTED;
    s->present = present;
}
