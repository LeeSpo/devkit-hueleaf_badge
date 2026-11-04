#include "mock.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *config;
static bool read_error, enabled;
static int calls, stored, writes;
static FILE *cfg_open(const char *path, const char *mode)
{
    assert(strcmp(path, "/sdcard/hueleaf.cfg") == 0 && strcmp(mode, "rb") == 0);
    if (!config) return NULL;
    FILE *f = tmpfile();
    assert(f);
    assert(fwrite(config, 1, strlen(config), f) == strlen(config));
    rewind(f);
    return f;
}
static size_t cfg_read(void *ptr, size_t size, size_t count, FILE *f)
{
    return fread(ptr, size, count, f);
}
static int cfg_error(FILE *f) { return read_error || ferror(f); }
#define fopen cfg_open
#define fread cfg_read
#define ferror cfg_error
#include "../main/main.c"
#undef fopen
#undef fread
#undef ferror

void screen_driver_set_mono_fast(bool on) { enabled = on; calls++; }
int nvs_open(const char *ns, int mode, nvs_handle_t *h)
{
    assert(strcmp(ns, "hlgal") == 0);
    assert(mode == NVS_READONLY || mode == NVS_READWRITE);
    *h = mode;
    return ESP_OK;
}
int nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *value)
{
    (void)h; assert(strcmp(key, "mono_fast") == 0);
    *value = (uint8_t)stored; return ESP_OK;
}
int nvs_set_u8(nvs_handle_t h, const char *key, uint8_t value)
{
    (void)h; assert(strcmp(key, "mono_fast") == 0);
    stored = value; writes++; return ESP_OK;
}
int nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; }

static void check(const char *text, bool error, bool expected)
{
    config = text; read_error = error; stored = 1; writes = 0;
    enabled = true; calls = 0;
    apply_cfg_early();
    assert(calls == 1 && !enabled);
    /* Simulate an already-enabled driver to verify apply_cfg explicitly sets it. */
    enabled = true; calls = 0;
    apply_cfg();
    assert(calls == 1 && enabled == expected);
}
int main(void)
{
    check(NULL, false, false); assert(stored == 1 && writes == 0);
    check("mono_fast = 0\n", false, false); assert(stored == 0 && writes == 1);
    check("mono_fast = 1\n", false, true); assert(stored == 1 && writes == 0);
    check("mono_fast = corrupt\n", false, false); assert(stored == 1 && writes == 0);
    check("other = 1\n", false, false); assert(stored == 1 && writes == 0);
    /* A partial read may contain a valid opt-in before the I/O error. */
    check("mono_fast = 1\n", true, false); assert(stored == 1 && writes == 0);
    return 0;
}
