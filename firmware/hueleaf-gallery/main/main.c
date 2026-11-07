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
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "fw_update.h"
#include "file_store.h"
#include "gallery.h"
#include "logbuf.h"
#include "nvs_flash.h"
#include "pcf8563.h"
#include "proto.h"
#include "runtime_state.h"
#include "slideshow.h"
#include "screen_driver.h"
#include "hl_cfg.h"
#include <stdio.h>
#include "sdkconfig.h"

#include "nvs.h"
#include "tap.h"
/* hueleaf.cfg switches: explicit value in the file > NVS (last explicit value)
 * > built-in default. NVS is applied before any screen, the file once TF is up. */
#define CFG_NVS_NS "hlgal"
static int nvs_cfg_get(const char *key)
{
    nvs_handle_t h; uint8_t v;
    if (nvs_open(CFG_NVS_NS, NVS_READONLY, &h) != ESP_OK) return -1;
    esp_err_t e = nvs_get_u8(h, key, &v);
    nvs_close(h);
    return e == ESP_OK ? (v != 0) : -1;
}
static void nvs_cfg_put(const char *key, int on)
{
    if (nvs_cfg_get(key) == on) return;           /* only write if changed */
    nvs_handle_t h;
    if (nvs_open(CFG_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_u8(h, key, (uint8_t)on) == ESP_OK) nvs_commit(h);
    nvs_close(h);
    ESP_LOGI("main", "cfg: stored %s=%d in NVS", key, on);
}
/* Resolve one switch; file text may be NULL (no hueleaf.cfg). */
static int cfg_resolve(const char *buf, size_t n, bool have_file, const char *key,
                       int dflt, const char **src)
{
    const int file = have_file ? hl_cfg_get_bool(buf, n, key, -1) : -1;
    if (file >= 0) { nvs_cfg_put(key, file); *src = "hueleaf.cfg"; return file; }
    const int nv = nvs_cfg_get(key);
    if (nv >= 0) { *src = have_file ? "NVS, last hueleaf.cfg; no key in file" : "NVS, last hueleaf.cfg"; return nv; }
    *src = have_file ? "default; no key in hueleaf.cfg" : "default; no hueleaf.cfg";
    return dflt;
}
static void apply_cfg_early(void)
{
#if CONFIG_HL_MONO_FAST
    const int nv = nvs_cfg_get("mono_fast");
    screen_driver_set_mono_fast(nv != 0);
    ESP_LOGI("main", "MonoFast: %s (%s)", nv != 0 ? "ON" : "OFF",
             nv < 0 ? "Kconfig default" : "NVS, last hueleaf.cfg");
#endif
}
static void apply_cfg(void)
{
    static char buf[1024];
    size_t n = 0;
    FILE *f = fopen(HL_CFG_PATH, "rb");
    if (f) { n = fread(buf, 1, sizeof(buf), f); fclose(f); }
    const char *src;
#if CONFIG_HL_MONO_FAST
    const int mono = cfg_resolve(buf, n, f != NULL, "mono_fast", 1, &src);
    screen_driver_set_mono_fast(mono);
    ESP_LOGI("main", "MonoFast: %s (%s)", mono ? "ON" : "OFF", src);
#else
    ESP_LOGI("main", "MonoFast: OFF (compiled out, CONFIG_HL_MONO_FAST=n)");
#endif
    const int tap = cfg_resolve(buf, n, f != NULL, "tap_next", 0, &src);
    tap_set_enabled(tap);
    ESP_LOGI("main", "TapNext: %s (%s)", tap ? "ON" : "OFF", src);
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
    slideshow_boot_init();
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
    ESP_LOGI(TAG, "heap before TF: free=%u largest=%u", (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    esp_err_t tf = mount_tf(3);
    if (tf != ESP_OK && !pending && factory_app_valid()) {
        boot_select_restart_to_stock();
        return;
    }
    apply_cfg_early();
    if (screen_driver_init() != ESP_OK) { startup_failed(pending, "display"); return; }
    if (tf != ESP_OK) {
        if (gallery_show_message_no_tf() != ESP_OK) { startup_failed(pending, "no-TF display"); return; }
        while (tf_card_mount() != ESP_OK) vTaskDelay(pdMS_TO_TICKS(2000));
    }
    sd_lock();
    bool recovered = file_store_recover_tree(GALLERY_DIR);
    recovered = file_store_recover_tree(GALLERY_LABEL_DIR) && recovered;
    recovered = file_store_recover_tree("/sdcard/update") && recovered;
    recovered = file_store_recover(HL_CFG_PATH) && recovered;
    sd_unlock();
    if (!recovered) { startup_failed(pending, "storage recovery"); return; }
    apply_cfg();
    (void)slideshow_load_config(); /* Config faults keep the device awake. */
    if (gallery_init() != ESP_OK) { startup_failed(pending, "gallery init"); return; }
    char previous[GALLERY_NAME_MAX];
    if (!pending && slideshow_tap_boot_name(previous, sizeof(previous)))
        err = gallery_restore_retained(previous);
    else if (!pending && slideshow_boot_name(previous, sizeof(previous)))
        err = gallery_render_scheduled(previous);
    else err = gallery_render_initial();
    if (err != ESP_OK) { startup_failed(pending, "initial frame"); return; }
    if (gallery_start_task() != ESP_OK) { startup_failed(pending, "gallery task"); return; }
    if (buttons_start() != ESP_OK) { startup_failed(pending, "buttons"); return; }
    if (status_cache_start() != ESP_OK) { startup_failed(pending, "status cache"); return; }
    ESP_LOGI(TAG, "heap after workers: free=%u largest=%u", (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    tap_start();   /* optional: IMU failure only disables double-tap */
    if (pending && esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) {
        startup_failed(true, "OTA validation"); return;
    }
    /* The updater manages its own SD lock; USB and buttons remain gated. */
    if (fw_update_from_tf() != ESP_OK) slideshow_inhibit();
    gallery_set_ready(true);
    if (slideshow_start() != ESP_OK) ESP_LOGE(TAG, "slideshow task unavailable; staying awake");
}
