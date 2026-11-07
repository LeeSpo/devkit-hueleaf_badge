#include "tf_card.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "board.h"
#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "pin_map.h"
#include "sdmmc_cmd.h"

static const char *TAG = "tf";

#define TF_SPI_KHZ  10000

static bool s_mounted;
static bool s_fault;
static sdmmc_card_t *s_card;
static FILE *s_file;
static bool s_file_locked;

/* Do not unmount: another caller may still hold a file handle. */
static void latch_fault(void)
{
    __atomic_store_n(&s_fault, true, __ATOMIC_SEQ_CST);
    __atomic_store_n(&s_mounted, false, __ATOMIC_SEQ_CST);
}

static esp_err_t write_locked(const char *path, const char *text, bool append)
{
    FILE *f = fopen(path, append ? "a" : "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "open %s failed", path);
        return ESP_FAIL;
    }

    size_t len = strlen(text);
    size_t n = fwrite(text, 1, len, f);
    int flush_err = fflush(f);
    int sync_err = fsync(fileno(f));
    int close_err = fclose(f);

    if (n != len || flush_err || sync_err || close_err) {
        ESP_LOGE(TAG, "write %s short %u/%u", path, (unsigned)n, (unsigned)len);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t tf_card_mount(void)
{
    if (__atomic_load_n(&s_fault, __ATOMIC_SEQ_CST)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (__atomic_load_n(&s_mounted, __ATOMIC_SEQ_CST)) {
        return ESP_OK;
    }

    gpio_reset_pin(spi2_tf_cs);
    gpio_set_pull_mode(spi2_tf_cs, GPIO_PULLUP_ONLY);

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = BOARD_SPI_HOST;
    host.max_freq_khz = TF_SPI_KHZ;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = BOARD_SPI_HOST;
    slot.gpio_cs = spi2_tf_cs;
    slot.wait_for_miso = -1;

    esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    board_spi_lock();
    esp_err_t err = esp_vfs_fat_sdspi_mount(TF_CARD_MOUNT_POINT, &host, &slot, &mount, &s_card);
    if (err == ESP_OK) {
        (void)mkdir(TF_CARD_NFC_DIR, 0777);
    }
    board_spi_unlock();

    if (err != ESP_OK) {
        board_tf_cs_idle();
        ESP_LOGW(TAG, "mount failed: %s  (FAT32 TF on CS=%d)",
                 esp_err_to_name(err), (int)spi2_tf_cs);
        return err;
    }

    __atomic_store_n(&s_mounted, true, __ATOMIC_SEQ_CST);
    sdmmc_card_print_info(stdout, s_card);
    ESP_LOGI(TAG, "mounted %s  MOSI=%d MISO=%d CLK=%d CS=%d",
             TF_CARD_MOUNT_POINT,
             (int)spi2_sdi, (int)spi2_sdo, (int)spi2_sck, (int)spi2_tf_cs);

    return tf_card_write_text(TF_CARD_NFC_LOG, "# NFC dump log\n\n", true);
}

bool tf_card_is_mounted(void)
{
    return __atomic_load_n(&s_mounted, __ATOMIC_SEQ_CST) &&
           !__atomic_load_n(&s_fault, __ATOMIC_SEQ_CST);
}

esp_err_t tf_card_unmount(void)
{
    if (!tf_card_is_mounted() || s_file || s_file_locked) return ESP_ERR_INVALID_STATE;
    board_spi_lock();
    esp_err_t err = esp_vfs_fat_sdcard_unmount(TF_CARD_MOUNT_POINT, s_card);
    /* IDF may already have freed the card before reporting a VFS cleanup
     * error. Never expose a stale mounted card or try to reuse its pointer. */
    s_card = NULL;
    __atomic_store_n(&s_mounted, false, __ATOMIC_SEQ_CST);
    if (err != ESP_OK) latch_fault();
    board_tf_cs_idle();
    board_spi_unlock();
    return err;
}

bool tf_card_has_fault(void)
{
    return __atomic_load_n(&s_fault, __ATOMIC_SEQ_CST);
}

esp_err_t tf_card_check(void)
{
    if (!tf_card_is_mounted()) return ESP_ERR_INVALID_STATE;
    board_spi_lock();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (tf_card_is_mounted()) {
        err = sdmmc_get_status(s_card);
        if (err != ESP_OK) latch_fault();
    }
    board_spi_unlock();
    return err;
}

esp_err_t tf_card_write_text(const char *path, const char *text, bool append)
{
    if (!tf_card_is_mounted() || path == NULL || text == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    board_spi_lock();
    esp_err_t err = tf_card_is_mounted() ? write_locked(path, text, append) : ESP_ERR_INVALID_STATE;
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) latch_fault();
    board_spi_unlock();

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "saved %s (%u bytes)", path, (unsigned)strlen(text));
    }
    return err;
}

esp_err_t tf_card_begin_write(const char *path, bool append)
{
    if (!tf_card_is_mounted() || path == NULL || s_file != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    board_spi_lock();
    if (!tf_card_is_mounted()) {
        board_spi_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    s_file_locked = true;
    s_file = fopen(path, append ? "a" : "w");
    if (s_file == NULL) {
        latch_fault();
        s_file_locked = false;
        board_spi_unlock();
        ESP_LOGE(TAG, "open %s failed", path);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t tf_card_write_raw(const void *data, size_t len)
{
    if (!tf_card_is_mounted() || s_file == NULL || data == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (len == 0) {
        return ESP_OK;
    }
    if (fwrite(data, 1, len, s_file) != len) {
        latch_fault();
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t tf_card_print(const char *text)
{
    if (text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return tf_card_write_raw(text, strlen(text));
}

esp_err_t tf_card_end_write(void)
{
    esp_err_t err = (s_file && tf_card_has_fault()) ? ESP_FAIL : ESP_OK;
    if (s_file) {
        if (fflush(s_file) != 0) err = ESP_FAIL;
        if (fsync(fileno(s_file)) != 0) err = ESP_FAIL;
        if (fclose(s_file) != 0) {
            err = ESP_FAIL;
        }
        s_file = NULL;
    }
    if (err != ESP_OK) latch_fault();
    if (s_file_locked) {
        s_file_locked = false;
        board_spi_unlock();
    }
    return err;
}
