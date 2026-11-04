#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_vfs_fat.h"

static int locked, mounts, mount_result, status_result, failure, formatted;
static sdmmc_card_t card;
void board_spi_lock(void) { assert(!locked); locked = 1; }
void board_spi_unlock(void) { assert(locked); locked = 0; }
void board_tf_cs_idle(void) {}
void gpio_reset_pin(int n) { (void)n; }
void gpio_set_pull_mode(int n, int m) { (void)n; (void)m; }
const char *esp_err_to_name(int n) { (void)n; return "error"; }
esp_err_t sdmmc_get_status(sdmmc_card_t *c) { assert(locked && c == &card); return status_result; }
void sdmmc_card_print_info(FILE *f, sdmmc_card_t *c) { (void)f; (void)c; }
esp_err_t esp_vfs_fat_sdspi_mount(const char *p, const sdmmc_host_t *h, const sdspi_device_config_t *s, const esp_vfs_fat_sdmmc_mount_config_t *m, sdmmc_card_t **c)
{ (void)p; (void)h; (void)s; assert(locked); mounts++; formatted += m->format_if_mount_failed; *c = &card; return mount_result; }
static FILE *mock_open(const char *p, const char *m) { (void)p; (void)m; return failure == 1 ? NULL : (FILE *)&card; }
static size_t mock_write(const void *p, size_t s, size_t n, FILE *f) { (void)p; (void)s; (void)f; assert(locked); return failure == 2 ? 0 : n; }
static int mock_flush(FILE *f) { (void)f; assert(locked); return failure == 3 ? -1 : 0; }
static int mock_sync(int fd) { (void)fd; assert(locked); return failure == 4 ? -1 : 0; }
static int mock_close(FILE *f) { (void)f; assert(locked); return failure == 5 ? -1 : 0; }
static int mock_fileno(FILE *f) { (void)f; return 1; }
static int mock_mkdir(const char *p, mode_t m) { (void)p; (void)m; return 0; }
#define fopen mock_open
#define fwrite mock_write
#define fflush mock_flush
#define fsync mock_sync
#define fclose mock_close
#define fileno mock_fileno
#define mkdir mock_mkdir
#include "../main/tf_card/tf_card.c"

static void reset_state(void)
{ s_mounted = false; s_fault = false; s_card = NULL; s_file = NULL; s_file_locked = false; locked = mounts = mount_result = status_result = failure = formatted = 0; }
int main(void)
{
    reset_state();
    assert(tf_card_check() == ESP_ERR_INVALID_STATE);
    mount_result = ESP_FAIL;
    assert(tf_card_mount() == ESP_FAIL && !tf_card_has_fault());
    mount_result = ESP_OK;
    assert(tf_card_mount() == ESP_OK && mounts == 2 && !formatted);
    assert(tf_card_check() == ESP_OK && tf_card_is_mounted() && !locked);
    status_result = ESP_FAIL;
    assert(tf_card_check() == ESP_FAIL && tf_card_has_fault() && !tf_card_is_mounted() && !locked);
    assert(tf_card_mount() == ESP_ERR_INVALID_STATE && mounts == 2);
    for (int i = 1; i <= 5; i++) {
        reset_state(); assert(tf_card_mount() == ESP_OK); failure = i;
        if (i == 1) assert(tf_card_begin_write("x", false) == ESP_FAIL);
        else {
            assert(tf_card_begin_write("x", false) == ESP_OK);
            if (i == 2) {
                assert(tf_card_write_raw("a", 1) == ESP_FAIL);
                assert(tf_card_write_raw("a", 1) == ESP_ERR_INVALID_STATE);
            }
            assert(tf_card_end_write() == ESP_FAIL);
        }
        assert(tf_card_has_fault() && !tf_card_is_mounted() && !locked);
        assert(tf_card_mount() == ESP_ERR_INVALID_STATE && mounts == 1 && !formatted);
    }
    for (int i = 1; i <= 5; i++) {
        reset_state(); assert(tf_card_mount() == ESP_OK); failure = i;
        assert(tf_card_write_text("x", "data", false) == ESP_FAIL);
        assert(tf_card_has_fault() && !locked);
    }
    return 0;
}
