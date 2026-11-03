#include "fw_update.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "gallery.h"

static const char *TAG = "fwupd";

esp_err_t fw_update_from_tf(void)
{
    struct stat st;
    if (stat(FW_UPDATE_PATH, &st) != 0) return ESP_OK;
    const esp_partition_t *dst = esp_ota_get_next_update_partition(NULL);
    if (!dst || dst->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) return ESP_ERR_NOT_FOUND;
    if (st.st_size <= 0 || (size_t)st.st_size > dst->size) {
        ESP_LOGE(TAG, "bad size %ld > slot %lu", (long)st.st_size, (unsigned long)dst->size);
        rename(FW_UPDATE_PATH, FW_UPDATE_PATH ".failed");
        return ESP_ERR_INVALID_SIZE;
    }
    ESP_LOGW(TAG, "updating %s from TF (%ld bytes)", dst->label, (long)st.st_size);
    sd_lock();
    FILE *f = fopen(FW_UPDATE_PATH, "rb");
    esp_ota_handle_t h = 0;
    esp_err_t err = f ? esp_ota_begin(dst, OTA_SIZE_UNKNOWN, &h) : ESP_FAIL;
    static uint8_t buf[4096];
    size_t n, total = 0;
    while (err == ESP_OK && f && (n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (total == 0 && n >= sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
            const esp_image_header_t *ih = (const esp_image_header_t *)buf;
            if (ih->magic != ESP_IMAGE_HEADER_MAGIC || ih->chip_id != ESP_CHIP_ID_ESP32C5) err = ESP_ERR_INVALID_VERSION;
        }
        if (err == ESP_OK) err = esp_ota_write(h, buf, n);
        total += n;
    }
    if (f) fclose(f);
    if (h) { esp_err_t e2 = esp_ota_end(h); if (err == ESP_OK) err = e2; } /* end verifies image + SHA */
    if (err == ESP_OK) err = esp_ota_set_boot_partition(dst);
    rename(FW_UPDATE_PATH, err == ESP_OK ? FW_UPDATE_PATH ".done" : FW_UPDATE_PATH ".failed");
    sd_unlock();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "update failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGW(TAG, "update written (%u bytes), rebooting into %s (pending verify)", (unsigned)total, dst->label);
    esp_restart();
}
