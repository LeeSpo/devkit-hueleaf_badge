#include "../usb_wake_shared.h"
#include "ulp_lp_core_i2c.h"
#include "ulp_lp_core_utils.h"

volatile usb_watch_t watch;
volatile uint32_t watch_magic;

int main(void)
{
    if (watch_magic != USB_WATCH_MAGIC) return 0;
    uint8_t reg = USB_WATCH_STATUS_REG, status = 0;
    /* Bounded transactions; the LP core halts between 500 ms samples. */
    esp_err_t err = lp_core_i2c_master_write_read_device(LP_I2C_NUM_0,
        USB_WATCH_ADDRESS, &reg, 1, &status, 1, 5000);
    usb_watch_sample(&watch, err == ESP_OK, status);
    if (watch.event) ulp_lp_core_wakeup_main_processor();
    return 0;
}
