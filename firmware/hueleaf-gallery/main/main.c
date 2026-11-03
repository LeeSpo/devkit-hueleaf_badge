/* HueLeaf gallery v1 — TF-card photo frame for OSPTEK HueLeaf Base.
 * Boot flow (see README "自定义固件"):
 *  1. TF card present  -> gallery (this app, ota_0/ota_1).
 *  2. No TF card       -> one-shot reboot into the untouched stock app in
 *                         `factory` (bootloader hook hl_boot_select), unless
 *                         this image is still pending rollback verification.
 *  App is marked valid only after EPD + TF init succeed.
 * Wi-Fi/BLE are not started in v1. BLE upload (v2) should feed the same
 * gallery_nav()/upload paths used by proto.c. */
#include <string.h>
#include "axp2101.h"
#include "board.h"
#include "boot_select.h"
#include "bootloader_common.h"
#include "buttons.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "fw_update.h"
#include "gallery.h"
#include "logbuf.h"
#include "nvs_flash.h"
#include "pcf8563.h"
#include "proto.h"
#include "screen_driver.h"
#include "hl_cfg.h"
#include <stdio.h>
#include "sdkconfig.h"

/* Runtime switches from TF root hueleaf.cfg; missing file/key = Kconfig default. */
static void apply_cfg(void)
{
#if CONFIG_HL_MONO_FAST
    static char buf[1024];
    size_t n = 0;
    FILE *f = fopen(HL_CFG_PATH, "rb");
    if (f) { n = fread(buf, 1, sizeof(buf), f); fclose(f); }
    const bool on = hl_cfg_get_bool(buf, n, "mono_fast", 1) != 0;
    screen_driver_set_mono_fast(on);
    ESP_LOGI("main", "MonoFast: %s (%s)", on ? "ON" : "OFF",
             !f ? "no hueleaf.cfg, Kconfig default" : on ? "hueleaf.cfg/default" : "hueleaf.cfg mono_fast=0");
#else
    ESP_LOGI("main", "MonoFast: OFF (compiled out, CONFIG_HL_MONO_FAST=n)");
#endif
}
#include "tf_card.h"
#include "version.h"

static const char *TAG = "main";

void boot_select_restart_to_stock(void)
{
    rtc_retain_mem_t *m = bootloader_common_get_rtc_retain_mem();
    volatile uint32_t *c = (volatile uint32_t *)m->custom;
    c[0] = HL_STOCK_MAGIC;
    c[1] = ~HL_STOCK_MAGIC;
    esp_restart();
}

static bool factory_app_valid(void)
{
    const esp_partition_t *f = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    esp_app_desc_t d;
    return f && esp_ota_get_partition_description(f, &d) == ESP_OK;
}

static bool pending_verify(void)
{
    esp_ota_img_states_t st;
    return esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) == ESP_OK &&
           st == ESP_OTA_IMG_PENDING_VERIFY;
}

static esp_err_t mount_tf(int tries)
{
    esp_err_t e = ESP_FAIL;
    for (int i = 0; i < tries && (e = tf_card_mount()) != ESP_OK; i++) vTaskDelay(pdMS_TO_TICKS(150));
    return e;
}

void app_main(void)
{
    logbuf_init();
    ESP_LOGI(TAG, "HueLeaf gallery %s", HL_FW_VERSION);
    /* Shared with the stock app: never erase it automatically. */
    esp_err_t nvs = nvs_flash_init();
    if (nvs != ESP_OK) ESP_LOGE(TAG, "NVS init %s: running without persistence", esp_err_to_name(nvs));

    esp_err_t err = board_init();
    if (err != ESP_OK) { vTaskDelay(pdMS_TO_TICKS(200)); err = board_init(); }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "board init failed");
        if (pending_verify()) esp_ota_mark_app_invalid_rollback_and_reboot();
        if (factory_app_valid()) boot_select_restart_to_stock();
        return;
    }
    (void)pcf8563_init();
    const bool pending = pending_verify();

    /* Cheapest check first (no EPD refresh) so the no-card path to stock is quick. */
    esp_err_t tf = mount_tf(3);
    if (tf != ESP_OK && !pending && factory_app_valid()) {
        ESP_LOGW(TAG, "no TF card -> stock firmware");
        boot_select_restart_to_stock();
    }

    if (screen_driver_init() != ESP_OK) {
        ESP_LOGE(TAG, "EPD init failed");
        if (pending) esp_ota_mark_app_invalid_rollback_and_reboot();
        if (factory_app_valid()) boot_select_restart_to_stock();
        return;
    }
    proto_start();   /* USB link is useful even without a card (info/log/stock) */

    if (tf != ESP_OK) {
        /* Pending verification (fresh install/update) or no stock app: tell
         * the user and wait for a card WITHOUT resetting (a reset while
         * pending would roll back). */
        gallery_show_message_no_tf();
        while ((tf = tf_card_mount()) != ESP_OK) vTaskDelay(pdMS_TO_TICKS(2000));
    }
    if (pending) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGW(TAG, "EPD + TF OK: app marked valid");
    }
    apply_cfg();
    (void)fw_update_from_tf();   /* reboots on success */
    gallery_init();
    gallery_start_task();
    buttons_start();
}
