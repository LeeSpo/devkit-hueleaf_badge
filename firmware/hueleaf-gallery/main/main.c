/* Startup keeps USB diagnostics available while storage and the first frame
 * are validated. Pending OTA images are accepted only after synchronous render
 * and worker creation; startup failures roll back or fall back to factory. */
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
#include "file_store.h"
#include "gallery.h"
#include "logbuf.h"
#include "nvs_flash.h"
#include "pcf8563.h"
#include "proto.h"
#include "screen_driver.h"
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

static void startup_failed(bool pending, const char *step)
{
    ESP_LOGE(TAG, "startup failed: %s", step);
    if (pending) {
        esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();
        ESP_LOGE(TAG, "rollback did not reboot: %s; USB diagnostics remain available",
                 esp_err_to_name(err));
        return;
    }
    if (factory_app_valid()) boot_select_restart_to_stock();
}

void app_main(void)
{
    logbuf_init();
    ESP_LOGI(TAG, "HueLeaf gallery %s", HL_FW_VERSION);
    esp_err_t nvs = nvs_flash_init();
    if (nvs != ESP_OK) ESP_LOGE(TAG, "NVS init %s: running without persistence", esp_err_to_name(nvs));
    const bool pending = pending_verify();
    esp_err_t err = board_init();
    if (err != ESP_OK) { vTaskDelay(pdMS_TO_TICKS(200)); err = board_init(); }
    if (err != ESP_OK) { startup_failed(pending, "board"); return; }
    err = pcf8563_init();
    if (err != ESP_OK) ESP_LOGW(TAG, "RTC unavailable: %s", esp_err_to_name(err));
    if (gallery_prepare() != ESP_OK) { startup_failed(pending, "gallery locks"); return; }
    gallery_set_ready(false);
    if (proto_start() != ESP_OK) { startup_failed(pending, "USB protocol"); return; }
    esp_err_t tf = mount_tf(3);
    if (tf != ESP_OK && !pending && factory_app_valid()) {
        boot_select_restart_to_stock();
        return;
    }
    if (screen_driver_init() != ESP_OK) { startup_failed(pending, "display"); return; }
    if (tf != ESP_OK) {
        if (gallery_show_message_no_tf() != ESP_OK) { startup_failed(pending, "no-TF display"); return; }
        while (tf_card_mount() != ESP_OK) vTaskDelay(pdMS_TO_TICKS(2000));
    }
    sd_lock();
    bool recovered = file_store_recover_tree(GALLERY_DIR);
    recovered = file_store_recover_tree("/sdcard/update") && recovered;
    sd_unlock();
    if (!recovered) { startup_failed(pending, "storage recovery"); return; }
    if (gallery_init() != ESP_OK) { startup_failed(pending, "gallery init"); return; }
    if (gallery_render_initial() != ESP_OK) { startup_failed(pending, "initial frame"); return; }
    if (gallery_start_task() != ESP_OK) { startup_failed(pending, "gallery task"); return; }
    if (buttons_start() != ESP_OK) { startup_failed(pending, "buttons"); return; }
    if (pending && esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) {
        startup_failed(true, "OTA validation"); return;
    }
    /* The updater manages its own SD lock; USB and buttons remain gated. */
    (void)fw_update_from_tf();
    gallery_set_ready(true);
}
