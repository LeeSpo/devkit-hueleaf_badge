#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "fw_update.h"
#undef FW_UPDATE_PATH
#define FW_UPDATE_PATH "firmware.bin"
#define fw_retire real_retire
#include "../main/fw_retire.c"
#undef fw_retire
int fw_retire(const char *, const char *);
static size_t test_fread(void *, size_t, size_t, FILE *);
static int test_ferror(FILE *);
#define fread test_fread
#define ferror test_ferror
/* The host ESP-IDF stub only defines the errors previously used here. */
#define ESP_ERR_INVALID_STATE 4
#include "../main/fw_update.c"
#undef fread
#undef ferror

static int begins, writes, ends, aborts, boots, restarts, locks, nvs_fail, retire_fail, end_fail, boot_fail, read_failure;
static size_t test_fread(void *p, size_t size, size_t count, FILE *f) { if (begins && read_failure) return 0; return fread(p, size, count, f); }
static int test_ferror(FILE *f) { return (begins && read_failure == 2) || ferror(f); }
static int saved, cut_at_boot;
static fw_seen_t record;
static jmp_buf restart_jump;
static esp_partition_t partition = { 1, 8192, "ota" };
int fw_retire(const char *p, const char *s) { return retire_fail ? -1 : real_retire(p, s); }
void sd_lock(void) { assert(!locks); locks++; }
void sd_unlock(void) { assert(locks == 1); locks--; }
const char *esp_err_to_name(int e) { (void)e; return "error"; }
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *p) { (void)p; return &partition; }
int esp_ota_begin(const esp_partition_t *p, size_t n, esp_ota_handle_t *h) { assert(locks && p == &partition && n); begins++; *h = 1; return 0; }
int esp_ota_write(esp_ota_handle_t h, const void *p, size_t n) { (void)h; (void)p; (void)n; writes++; return 0; }
int esp_ota_end(esp_ota_handle_t h) { (void)h; ends++; return end_fail ? -1 : 0; }
int esp_ota_abort(esp_ota_handle_t h) { (void)h; aborts++; return 0; }
int esp_ota_set_boot_partition(const esp_partition_t *p) { assert(p == &partition); boots++; if (cut_at_boot) longjmp(restart_jump, 1); return boot_fail ? -1 : 0; }
void esp_restart(void) { restarts++; longjmp(restart_jump, 1); }
int nvs_open(const char *ns, int mode, nvs_handle_t *h) { (void)ns; *h = mode; return mode && nvs_fail ? -1 : 0; }
int nvs_get_blob(nvs_handle_t h, const char *k, void *p, size_t *n) { (void)h; (void)k; if (!saved) return -1; assert(*n == sizeof record); memcpy(p, &record, *n); return 0; }
int nvs_set_blob(nvs_handle_t h, const char *k, const void *p, size_t n) { (void)h; (void)k; assert(n == sizeof record); memcpy(&record, p, n); return 0; }
int nvs_commit(nvs_handle_t h) { (void)h; saved = 1; return 0; }
void nvs_close(nvs_handle_t h) { (void)h; }
void mbedtls_sha256_init(mbedtls_sha256_context *c) { c->hash = 0; }
int mbedtls_sha256_starts(mbedtls_sha256_context *c, int x) { (void)c; (void)x; return 0; }
int mbedtls_sha256_update(mbedtls_sha256_context *c, const unsigned char *p, size_t n) { while (n--) c->hash = c->hash * 33 + *p++; return 0; }
int mbedtls_sha256_finish(mbedtls_sha256_context *c, unsigned char *p) { memset(p, 0, 32); memcpy(p, &c->hash, sizeof c->hash); return 0; }
void mbedtls_sha256_free(mbedtls_sha256_context *c) { (void)c; }
static void image(int chip, size_t size) { FILE *f = fopen(FW_UPDATE_PATH, "wb"); assert(f); esp_image_header_t h = { ESP_IMAGE_HEADER_MAGIC, chip }; assert(size >= sizeof h); assert(fwrite(&h, 1, sizeof h, f) == sizeof h); for (size_t i = sizeof h; i < size; i++) fputc(42, f); assert(!fclose(f)); }
int main(int argc, char **argv) {
    assert(argc == 2);
    if (!strcmp(argv[1], "missing")) { assert(fw_update_from_tf() == 0); assert(!begins && !boots); }
    else if (!strcmp(argv[1], "success")) { image(ESP_CHIP_ID_ESP32C5, 100); if (!setjmp(restart_jump)) { fw_update_from_tf(); assert(0); } assert(begins == 1 && ends == 1 && boots == 1 && restarts == 1 && !locks && saved && record.ok); }
    else if (!strcmp(argv[1], "oversize")) { image(ESP_CHIP_ID_ESP32C5, 9000); assert(fw_update_from_tf() != 0); assert(!begins && !boots); }
    else if (!strcmp(argv[1], "wrong")) { image(0, 100); assert(fw_update_from_tf() != 0); assert(!begins && !boots); }
    else if (!strcmp(argv[1], "short")) { FILE *f = fopen(FW_UPDATE_PATH, "wb"); assert(f); fputc(1, f); fclose(f); assert(fw_update_from_tf() != 0); assert(!begins && !boots); }
    else if (!strcmp(argv[1], "truncated") || !strcmp(argv[1], "readerror")) { image(ESP_CHIP_ID_ESP32C5, 100); read_failure = !strcmp(argv[1], "readerror") ? 2 : 1; assert(fw_update_from_tf() != 0); assert(begins == 1 && aborts == 1 && !ends && !boots); }
    else if (!strcmp(argv[1], "badimage")) { image(ESP_CHIP_ID_ESP32C5, 100); end_fail = 1; assert(fw_update_from_tf() != 0); assert(ends == 1 && !boots); }
    else if (!strcmp(argv[1], "stuck")) { image(ESP_CHIP_ID_ESP32C5, 100); retire_fail = 1; if (!setjmp(restart_jump)) fw_update_from_tf(); assert(boots == 1); assert(fw_update_from_tf() == 0); assert(boots == 1 && begins == 1); }
    else if (!strcmp(argv[1], "prepared")) { image(ESP_CHIP_ID_ESP32C5, 100); cut_at_boot = 1; if (!setjmp(restart_jump)) { fw_update_from_tf(); assert(0); } assert(saved && record.ok == FW_SEEN_PREPARED && begins == 1 && boots == 1); locks = 0; cut_at_boot = 0; assert(fw_update_from_tf() == ESP_ERR_INVALID_STATE); assert(begins == 1 && boots == 1 && !restarts); assert(access(FW_UPDATE_PATH, F_OK) == 0 && access(FW_UPDATE_PATH ".failed", F_OK) != 0); assert(fw_update_from_tf() == ESP_ERR_INVALID_STATE); assert(begins == 1 && boots == 1); }
    else if (!strcmp(argv[1], "unsecured")) { image(ESP_CHIP_ID_ESP32C5, 100); retire_fail = nvs_fail = 1; assert(fw_update_from_tf() != 0); assert(ends == 1 && !boots && !restarts); }
    else if (!strcmp(argv[1], "bootfail")) { image(ESP_CHIP_ID_ESP32C5, 100); boot_fail = 1; assert(fw_update_from_tf() != 0); assert(saved && !record.ok && !restarts); }
    else assert(0);
    assert(!locks); return 0;
}
