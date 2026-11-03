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
#include "fw_retire.h"
#include "mbedtls/sha256.h"
#include "nvs.h"

static const char *TAG = "fwupd";

/* Last image handled from TF (applied or failed): size + SHA-256 of the file.
 * If the file can't be renamed/deleted we still never apply it twice, so a
 * stuck firmware.bin can't cause a reflash/reboot loop. */
typedef struct { uint32_t size; uint8_t sha[32]; uint8_t ok; } fw_seen_t;
#define FW_NVS_NS  "hlgal"
#define FW_NVS_KEY "fw_seen"

static uint8_t s_buf[4096];

static esp_err_t file_sha(FILE *f, uint8_t out[32])
{
    mbedtls_sha256_context c;
    mbedtls_sha256_init(&c);
    mbedtls_sha256_starts(&c, 0);
    size_t n;
    while ((n = fread(s_buf, 1, sizeof s_buf, f)) > 0) mbedtls_sha256_update(&c, s_buf, n);
    esp_err_t err = ferror(f) ? ESP_FAIL : ESP_OK;
    mbedtls_sha256_finish(&c, out);
    mbedtls_sha256_free(&c);
    rewind(f);
    return err;
}

static bool seen_get(fw_seen_t *s)
{
    nvs_handle_t h;
    size_t len = sizeof *s;
    if (nvs_open(FW_NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    esp_err_t e = nvs_get_blob(h, FW_NVS_KEY, s, &len);
    nvs_close(h);
    return e == ESP_OK && len == sizeof *s;
}

static void seen_put(const fw_seen_t *s)
{
    nvs_handle_t h;
    if (nvs_open(FW_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_blob(h, FW_NVS_KEY, s, sizeof *s) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

static void retire(const char *suffix)
{
    int r = fw_retire(FW_UPDATE_PATH, suffix);
    if (r == 0) ESP_LOGI(TAG, "renamed to firmware.bin%s", suffix);
    else if (r == 1) ESP_LOGW(TAG, "rename failed, deleted firmware.bin");
    else ESP_LOGE(TAG, "cannot rename or delete firmware.bin; NVS record prevents re-apply");
}

esp_err_t fw_update_from_tf(void)
{
    struct stat st;
    if (stat(FW_UPDATE_PATH, &st) != 0) return ESP_OK;
    const esp_partition_t *dst = esp_ota_get_next_update_partition(NULL);
    if (!dst || dst->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) return ESP_ERR_NOT_FOUND;

    sd_lock();
    FILE *f = fopen(FW_UPDATE_PATH, "rb");
    fw_seen_t cur = { .size = (uint32_t)st.st_size };
    esp_err_t err = f ? file_sha(f, cur.sha) : ESP_FAIL;
    fw_seen_t prev;
    if (err == ESP_OK && seen_get(&prev) && prev.size == cur.size && !memcmp(prev.sha, cur.sha, 32)) {
        fclose(f);
        ESP_LOGW(TAG, "firmware.bin already handled (%s), not re-applying", prev.ok ? "applied" : "failed");
        retire(prev.ok ? ".done" : ".failed");
        sd_unlock();
        return ESP_OK;
    }
    if (err == ESP_OK && (st.st_size <= 0 || (size_t)st.st_size > dst->size)) {
        ESP_LOGE(TAG, "bad size %ld > slot %lu", (long)st.st_size, (unsigned long)dst->size);
        err = ESP_ERR_INVALID_SIZE;
    }
    esp_ota_handle_t h = 0;
    size_t n, total = 0;
    if (err == ESP_OK) {
        ESP_LOGW(TAG, "updating %s from TF (%ld bytes)", dst->label, (long)st.st_size);
        err = esp_ota_begin(dst, OTA_SIZE_UNKNOWN, &h);
    }
    while (err == ESP_OK && (n = fread(s_buf, 1, sizeof(s_buf), f)) > 0) {
        if (total == 0 && n >= sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
            const esp_image_header_t *ih = (const esp_image_header_t *)s_buf;
            if (ih->magic != ESP_IMAGE_HEADER_MAGIC || ih->chip_id != ESP_CHIP_ID_ESP32C5) err = ESP_ERR_INVALID_VERSION;
        }
        if (err == ESP_OK) err = esp_ota_write(h, s_buf, n);
        total += n;
    }
    if (f) fclose(f);
    if (h) { esp_err_t e2 = err == ESP_OK ? esp_ota_end(h) : (esp_ota_abort(h), err); if (err == ESP_OK) err = e2; } /* end verifies image + SHA */
    if (err == ESP_OK) err = esp_ota_set_boot_partition(dst);
    cur.ok = err == ESP_OK;
    seen_put(&cur);                        /* before retire: survives a stuck file */
    retire(err == ESP_OK ? ".done" : ".failed");
    sd_unlock();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "update failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGW(TAG, "update written (%u bytes), rebooting into %s (pending verify)", (unsigned)total, dst->label);
    esp_restart();
}
