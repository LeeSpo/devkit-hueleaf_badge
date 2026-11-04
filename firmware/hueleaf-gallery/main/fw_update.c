#include "fw_update.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_app_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "gallery.h"
#include "fw_retire.h"
#include "mbedtls/sha256.h"
#include "nvs.h"

static const char *TAG = "fwupd";
typedef struct { uint32_t size; uint8_t sha[32]; uint8_t ok; } fw_seen_t;
enum { FW_SEEN_FAILED = 0, FW_SEEN_APPLIED = 1, FW_SEEN_PREPARED = 2 };
#define FW_NVS_NS "hlgal"
#define FW_NVS_KEY "fw_seen"
static uint8_t s_buf[4096];

static esp_err_t file_sha(FILE *f, uint8_t out[32], size_t expected)
{
    mbedtls_sha256_context c;
    mbedtls_sha256_init(&c);
    int rc = mbedtls_sha256_starts(&c, 0);
    size_t n, total = 0;
    while (rc == 0 && (n = fread(s_buf, 1, sizeof s_buf, f)) != 0) {
        if (n > expected - total) { rc = -1; break; }
        total += n;
        rc = mbedtls_sha256_update(&c, s_buf, n);
    }
    if (ferror(f) || total != expected) rc = -1;
    if (rc == 0) rc = mbedtls_sha256_finish(&c, out);
    mbedtls_sha256_free(&c);
    if (rc != 0 || fseek(f, 0, SEEK_SET) != 0) return ESP_FAIL;
    return ESP_OK;
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

static bool seen_put(const fw_seen_t *s)
{
    nvs_handle_t h;
    if (nvs_open(FW_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e = nvs_set_blob(h, FW_NVS_KEY, s, sizeof *s);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e == ESP_OK;
}

static bool retire(const char *suffix)
{
    int r = fw_retire(FW_UPDATE_PATH, suffix);
    if (r == 0) ESP_LOGI(TAG, "renamed to firmware.bin%s", suffix);
    else if (r == 1) ESP_LOGW(TAG, "firmware.bin removed");
    else ESP_LOGE(TAG, "cannot retire firmware.bin");
    return r >= 0;
}

esp_err_t fw_update_from_tf(void)
{
    struct stat st;
    FILE *f = NULL;
    esp_err_t err = ESP_OK;
    fw_seen_t cur = {0}, prev;
    bool identity = false, active = false, retired = false;
    esp_ota_handle_t h = 0;
    size_t total = 0, n;
    const esp_partition_t *dst;

    sd_lock();
    if (stat(FW_UPDATE_PATH, &st) != 0) {
        err = errno == ENOENT ? ESP_OK : ESP_FAIL;
        goto out;
    }
    dst = esp_ota_get_next_update_partition(NULL);
    if (!dst || dst->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
        err = ESP_ERR_NOT_FOUND;
        goto out;
    }
    if (st.st_size <= 0 || (uint64_t)st.st_size > dst->size || (uint64_t)st.st_size > UINT32_MAX) {
        err = ESP_ERR_INVALID_SIZE;
        goto out;
    }
    f = fopen(FW_UPDATE_PATH, "rb");
    if (!f) { err = ESP_FAIL; goto out; }
    cur.size = (uint32_t)st.st_size;
    err = file_sha(f, cur.sha, cur.size);
    if (err != ESP_OK) goto out;
    identity = true;
    if (seen_get(&prev) && prev.size == cur.size && !memcmp(prev.sha, cur.sha, sizeof cur.sha)) {
        if (fclose(f) != 0) { f = NULL; err = ESP_FAIL; goto out; }
        f = NULL;
        if (prev.ok == FW_SEEN_PREPARED) {
            /* Selection may or may not have completed before power was lost.
             * Do not reflash or discard the only copy without user recovery. */
            ESP_LOGE(TAG, "firmware.bin update interrupted; retained for manual recovery");
            err = ESP_ERR_INVALID_STATE;
            goto out;
        }
        ESP_LOGW(TAG, "firmware.bin already handled (%s)", prev.ok == FW_SEEN_APPLIED ? "applied" : "failed");
        retire(prev.ok == FW_SEEN_APPLIED ? ".done" : ".failed");
        goto out;
    }
    /* Read the complete image header before creating an OTA transaction. */
    esp_image_header_t header;
    if (fread(&header, 1, sizeof header, f) != sizeof header || ferror(f)) {
        err = ESP_ERR_INVALID_SIZE;
        goto failed;
    }
    if (header.magic != ESP_IMAGE_HEADER_MAGIC || header.chip_id != ESP_CHIP_ID_ESP32C5) {
        err = ESP_ERR_INVALID_VERSION;
        goto failed;
    }
    if (fseek(f, 0, SEEK_SET) != 0) { err = ESP_FAIL; goto failed; }
    err = esp_ota_begin(dst, cur.size, &h);
    if (err != ESP_OK) goto failed;
    active = true;
    while ((n = fread(s_buf, 1, sizeof s_buf, f)) != 0) {
        if (n > cur.size - total) { err = ESP_FAIL; break; }
        err = esp_ota_write(h, s_buf, n);
        if (err != ESP_OK) break;
        total += n;
    }
    if (ferror(f) || total != cur.size) err = ESP_FAIL;
    if (fclose(f) != 0 && err == ESP_OK) err = ESP_FAIL;
    f = NULL;
    if (err != ESP_OK) goto failed;
    err = esp_ota_end(h);
    active = false; /* end consumes the handle even on verification failure */
    if (err != ESP_OK) goto failed;

    /* Record the uncertain selection window separately from known failure.
     * If NVS is unavailable, removal of the source must succeed instead. */
    cur.ok = FW_SEEN_PREPARED;
    if (!seen_put(&cur)) {
        retired = retire(".done");
        if (!retired) { err = ESP_FAIL; goto out; }
    }
    err = esp_ota_set_boot_partition(dst);
    if (err != ESP_OK) {
        if (retired) {
            /* Preserve the diagnostic when the early rename succeeded. */
            (void)rename(FW_UPDATE_PATH ".done", FW_UPDATE_PATH ".failed");
        }
        goto failed;
    }
    cur.ok = FW_SEEN_APPLIED;
    if (!seen_put(&cur)) ESP_LOGW(TAG, "could not update seen status to applied");
    if (!retired) retire(".done");
    sd_unlock();
    ESP_LOGW(TAG, "update written (%u bytes), rebooting into %s", (unsigned)total, dst->label);
    esp_restart();
    return ESP_FAIL; /* esp_restart normally does not return */

failed:
    if (active) esp_ota_abort(h);
    if (f) {
        if (fclose(f) != 0 && err == ESP_OK) err = ESP_FAIL;
        f = NULL;
    }
    if (identity && !retired) {
        cur.ok = FW_SEEN_FAILED;
        (void)seen_put(&cur);
        retire(".failed");
    }
out:
    if (f && fclose(f) != 0 && err == ESP_OK) err = ESP_FAIL;
    sd_unlock();
    if (err != ESP_OK) ESP_LOGE(TAG, "update failed: %s", esp_err_to_name(err));
    return err;
}
