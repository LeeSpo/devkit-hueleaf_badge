#pragma once
#ifndef GALLERY_SOURCE
#error "GALLERY_SOURCE must point to gallery.c"
#endif

#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <stdbool.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>

static const char *card_names[8], *index_text, *selection_text;
static int card_count, dir_pos, saved_index;
static char saved_name[64];
static bool saved_display_valid;
static uint64_t saved_display_hash;
static int32_t saved_display_cur, saved_display_count;
static uint8_t saved_display_strip;
static char saved_display_name[64];
static uint8_t frame_byte = 0x55;
#ifdef CONFIG_HL_REMOTE
static uint8_t last_card_byte;
static const uint8_t *last_mono_bitmap;
static int mono_calls;
#endif
static bool tap_on;
#ifdef CONFIG_HL_REMOTE
static int remote_completions;
static bool remote_success;
#endif
static int activity_count, frame_ok, frame_failed;
static bool slide_due = true;
void slideshow_activity(void) { activity_count++; }
bool slideshow_due(void) { return slide_due; }
void slideshow_frame_done(bool success) { if (success) frame_ok++; else frame_failed++; }

static size_t host_strlcpy(char *d, const char *s, size_t n) {
    size_t len = strlen(s); if (n) { size_t k = len < n-1 ? len : n-1; memcpy(d,s,k); d[k]=0; } return len;
}
static FILE *fake_open(const char *path, const char *mode) {
    (void)mode;
#ifdef GALLERY_LABEL_IO
    if (strstr(path, "/.labels/")) return label_open(path);
#endif
#ifdef GALLERY_SELECTION_IO
    if (strstr(path,"/slideshow.txt") || strstr(path,"/.slideshow.tmp")) return selection_open(path,mode);
#endif
    if (strstr(path, "/slideshow.txt")) {
        if (!selection_text) { errno=ENOENT; return NULL; }
        FILE *f=tmpfile(); assert(f); fputs(selection_text,f); rewind(f); return f;
    }
    if (strstr(path, "/index.txt")) {
        if (!index_text) return NULL;
        FILE *f = tmpfile(); assert(f); fputs(index_text,f); rewind(f); return f;
    }
    bool found = strstr(path, "/one.bin") != NULL;
    for (int i=0; i<card_count; i++) if (strstr(path,card_names[i])) found=true;
    if (!found) return NULL;
    FILE *f = tmpfile(); assert(f);
    for (int i = 0; i < 172800; ++i) fputc(frame_byte, f);
    rewind(f); return f;
}
static DIR *fake_opendir(const char *path) { (void)path; dir_pos=0; return (DIR *)1; }
static struct dirent *fake_readdir(DIR *d) {
    (void)d; static struct dirent entry;
    if (dir_pos==card_count) return NULL;
    strcpy(entry.d_name,card_names[dir_pos++]); return &entry;
}
static int fake_closedir(DIR *d) { (void)d; return 0; }
static int fake_stat(const char *path,struct stat *st) {
    const char *name=strrchr(path,'/'); if (!name) return -1;
    for (int i=0; i<card_count; i++) if (!strcmp(name+1,card_names[i])) { st->st_size=172800; return 0; }
    return -1;
}
static int fake_mkdir(const char *path,mode_t mode) { (void)path;(void)mode; return 0; }
#undef strlcpy
#define strlcpy host_strlcpy
#define fopen fake_open
#define opendir fake_opendir
#define readdir fake_readdir
#define closedir fake_closedir
#define stat(path, st) fake_stat(path, st)
#define mkdir fake_mkdir
#include GALLERY_SOURCE
#undef fopen
#undef opendir
#undef readdir
#undef closedir
#undef stat
#undef mkdir

#ifndef GALLERY_SELECTION_IO
bool file_store_recover(const char *path) { (void)path; return true; }
bool file_store_replace(const char *tmp, const char *path) { (void)tmp; (void)path; return false; }
#endif
static int alloc_calls, fail_alloc, deletes, notifications, waits, shows, display_result, task_result = pdPASS;
static int permit_wakes, nvs_commits;
static jmp_buf escape;
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void) { ++alloc_calls; return fail_alloc == alloc_calls ? NULL : (void *)(uintptr_t)alloc_calls; }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { ++alloc_calls; return fail_alloc == alloc_calls ? NULL : (void *)(uintptr_t)alloc_calls; }
void vSemaphoreDelete(SemaphoreHandle_t h) { assert(h); ++deletes; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t h, TickType_t t) { assert(h); (void)t; return pdTRUE; }
BaseType_t xSemaphoreGive(SemaphoreHandle_t h) { assert(h); return pdTRUE; }
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t h, TickType_t t) { return xSemaphoreTake(h,t); }
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t h) { return xSemaphoreGive(h); }
BaseType_t xTaskCreate(void (*fn)(void *), const char *n, unsigned stack, void *arg, unsigned pri, TaskHandle_t *out) { (void)fn;(void)n;(void)stack;(void)arg;(void)pri; if (task_result == pdPASS) *out=(void *)1; return task_result; }
void xTaskNotifyGive(TaskHandle_t t) { assert(t); ++notifications; }
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t timeout) { assert(clear == pdTRUE); assert(timeout == portMAX_DELAY); ++waits; if (permit_wakes>0) { --permit_wakes; return 1; } longjmp(escape,1); }
TickType_t xTaskGetTickCount(void) { return 0; }
int64_t esp_timer_get_time(void) { return 0; }
void vTaskDelay(TickType_t t) { (void)t; }
esp_err_t nvs_open(const char *n, int m, nvs_handle_t *h) { (void)n;(void)m;*h=1; return saved_name[0] || saved_display_valid ? ESP_OK : ESP_ERR_NOT_FOUND; }
esp_err_t nvs_get_i32(nvs_handle_t h,const char *k,int32_t *v) {
    (void)h;
    if (!strcmp(k, "dcur") && saved_display_valid) { *v=saved_display_cur; return ESP_OK; }
    if (!strcmp(k, "dcount") && saved_display_valid) { *v=saved_display_count; return ESP_OK; }
    if (!strcmp(k, "curi")) { *v=saved_index; return ESP_OK; }
    return ESP_ERR_NOT_FOUND;
}
esp_err_t nvs_get_u64(nvs_handle_t h,const char *k,uint64_t *v) {
    (void)h;
    if (!strcmp(k, "dhash") && saved_display_valid) { *v=saved_display_hash; return ESP_OK; }
    return ESP_ERR_NOT_FOUND;
}
esp_err_t nvs_get_u8(nvs_handle_t h,const char *k,uint8_t *v) {
    (void)h;
    if (!strcmp(k, "dvalid")) { *v=saved_display_valid; return ESP_OK; }
    if (!strcmp(k, "dstrip") && saved_display_valid) { *v=saved_display_strip; return ESP_OK; }
    return ESP_ERR_NOT_FOUND;
}
esp_err_t nvs_get_str(nvs_handle_t h,const char *k,char *v,size_t *n) {
    (void)h;
    if (!strcmp(k, "dname") && saved_display_valid) { host_strlcpy(v,saved_display_name,*n); return ESP_OK; }
    if (!strcmp(k, "cur") && saved_name[0]) { host_strlcpy(v,saved_name,*n); return ESP_OK; }
    return ESP_ERR_NOT_FOUND;
}
esp_err_t nvs_set_i32(nvs_handle_t h,const char *k,int32_t v) {
    (void)h;
    if (!strcmp(k, "dcur")) saved_display_cur=v;
    else if (!strcmp(k, "dcount")) saved_display_count=v;
    return ESP_OK;
}
esp_err_t nvs_set_u64(nvs_handle_t h,const char *k,uint64_t v) {
    (void)h;
    if (!strcmp(k, "dhash")) saved_display_hash=v;
    return ESP_OK;
}
esp_err_t nvs_set_u8(nvs_handle_t h,const char *k,uint8_t v) {
    (void)h;
    if (!strcmp(k, "dvalid")) saved_display_valid=v != 0;
    else if (!strcmp(k, "dstrip")) saved_display_strip=v;
    return ESP_OK;
}
esp_err_t nvs_set_str(nvs_handle_t h,const char *k,const char *v) {
    (void)h;
    if (!strcmp(k, "dname")) host_strlcpy(saved_display_name,v,sizeof(saved_display_name));
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; nvs_commits++; return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; }
esp_err_t screen_driver_show_frame(const uint8_t *f,size_t len) { assert(f && len == SCREEN_FRAME_BYTES); ++shows; return display_result; }
esp_err_t screen_driver_show_mono(uint8_t *f, size_t len) { return screen_driver_show_frame(f, len); }
#ifdef CONFIG_HL_REMOTE
esp_err_t screen_driver_init(void) { return ESP_OK; }
void remote_render_done(bool success) { remote_completions++; remote_success = success; }
esp_err_t screen_driver_show_mono_bitmap(const uint8_t *bitmap, size_t len)
{
    assert(bitmap && len == SCREEN_MONO_BYTES);
    last_mono_bitmap=bitmap; mono_calls++; shows++; return display_result;
}
esp_err_t screen_driver_show_card_stream(screen_frame_read_cb_t read_cb, void *context)
{
    uint8_t bytes[2048]; size_t offset=0;
    while (offset < SCREEN_FRAME_BYTES) {
        size_t cap=SCREEN_FRAME_BYTES-offset, received=0;
        if (cap>sizeof(bytes)) cap=sizeof(bytes);
        assert(read_cb(context,bytes,cap,&received)==ESP_OK && received==cap);
        if (!offset) last_card_byte=bytes[0];
        offset+=received;
    }
    shows++; return display_result;
}
#endif
bool tap_enabled(void) { return tap_on; }
void screen_driver_mono_stash_lost(void) {}
void frame_fill(uint8_t *f,uint8_t ink) { memset(f,ink,FRAME_UI_BYTES); }
void frame_rect(uint8_t *f,int x,int y,int w,int h,uint8_t ink) { (void)f;(void)x;(void)y;(void)w;(void)h;(void)ink; }
int frame_text(uint8_t *f,int x,int y,const char *s,int scale,uint8_t ink) { (void)f;(void)y;(void)s;(void)scale;(void)ink; return x; }
int frame_text_width(const char *s,int scale) { (void)s;(void)scale; return 0; }
void frame_bitmap_centered(uint8_t *f,const msg_bitmap_t *b,int y,uint8_t ink) { (void)f;(void)b;(void)y;(void)ink; }
void frame_bits(uint8_t *f,int x,int y,int w,int h,const uint8_t *b,uint8_t ink) { (void)f;(void)x;(void)y;(void)w;(void)h;(void)b;(void)ink; }
esp_err_t axp2101_gauge_status(bool *e,bool *p,int *s) { (void)e;(void)p;(void)s; return ESP_FAIL; }
esp_err_t pcf8563_get_time(pcf8563_time_t *t) { (void)t; return ESP_FAIL; }

#if !defined(GALLERY_STARTUP_TEST) && !defined(GALLERY_NO_WORKER_HELPERS)
static uint8_t shown_byte(void)
{
#ifdef CONFIG_HL_REMOTE
    return last_card_byte;
#else
    return s_frame[0];
#endif
}

static void run_gallery_once(void)
{
    permit_wakes=1;
    if (!setjmp(escape)) gallery_task(NULL);
}
#endif

