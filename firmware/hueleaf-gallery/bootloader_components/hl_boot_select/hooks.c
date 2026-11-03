/* One-shot jump to the stock factory app.
 * The gallery app (ota_0/ota_1) writes HL_STOCK_MAGIC into the custom RTC
 * retain area and calls esp_restart() when no TF card is present. The flag is
 * consumed here (cleared before jumping), so the NEXT reset/power-on boots the
 * normal otadata selection again (= gallery app). Power loss clears RTC RAM. */
#include <stdint.h>
#include "bootloader_common.h"
#include "bootloader_utility.h"
#include "bootloader_config.h"
#include "esp_log.h"
#include "../../main/boot_select.h"

void bootloader_hooks_include(void) {}

void bootloader_after_init(void)
{
    rtc_retain_mem_t *m = bootloader_common_get_rtc_retain_mem();
    volatile uint32_t *c = (volatile uint32_t *)m->custom;
    if (c[0] != HL_STOCK_MAGIC || c[1] != (uint32_t)~HL_STOCK_MAGIC) return;
    c[0] = 0; c[1] = 0;
    bootloader_state_t bs = {0};
    if (!bootloader_utility_load_partition_table(&bs) || bs.factory.offset == 0) return;
    ESP_LOGW("hl_boot", "one-shot: booting stock factory app");
    bootloader_utility_load_boot_image(&bs, FACTORY_INDEX); /* no return */
}
