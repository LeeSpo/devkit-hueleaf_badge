#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>

static size_t host_strlcpy(char *d, const char *s, size_t n) {
    size_t len = strlen(s); if (n) { size_t k = len < n-1 ? len : n-1; memcpy(d,s,k); d[k]=0; } return len;
}
static FILE *fake_open(const char *path, const char *mode) {
    (void)mode;
    if (!strstr(path, "/one.bin")) return NULL;
    FILE *f = tmpfile(); assert(f);
    for (int i = 0; i < 172800; ++i) fputc(0x55, f);
    rewind(f); return f;
}
#undef strlcpy
#define strlcpy host_strlcpy
#define fopen fake_open
#include GALLERY_SOURCE
#undef fopen

static int alloc_calls, fail_alloc, deletes, notifications, waits, shows, display_result, task_result = pdPASS;
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
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t timeout) { assert(clear == pdTRUE); assert(timeout == portMAX_DELAY); ++waits; longjmp(escape,1); }
TickType_t xTaskGetTickCount(void) { return 0; }
void vTaskDelay(TickType_t t) { (void)t; }
esp_err_t nvs_open(const char *n, int m, nvs_handle_t *h) { (void)n;(void)m;(void)h; return ESP_ERR_NOT_FOUND; }
esp_err_t nvs_get_i32(nvs_handle_t h,const char *k,int32_t *v) { (void)h;(void)k;(void)v; return ESP_ERR_NOT_FOUND; }
esp_err_t nvs_get_u8(nvs_handle_t h,const char *k,uint8_t *v) { (void)h;(void)k;(void)v; return ESP_ERR_NOT_FOUND; }
esp_err_t nvs_get_str(nvs_handle_t h,const char *k,char *v,size_t *n) { (void)h;(void)k;(void)v;(void)n; return ESP_ERR_NOT_FOUND; }
esp_err_t nvs_set_i32(nvs_handle_t h,const char *k,int32_t v) { (void)h;(void)k;(void)v; return ESP_OK; }
esp_err_t nvs_set_u8(nvs_handle_t h,const char *k,uint8_t v) { (void)h;(void)k;(void)v; return ESP_OK; }
esp_err_t nvs_set_str(nvs_handle_t h,const char *k,const char *v) { (void)h;(void)k;(void)v; return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; }
esp_err_t screen_driver_show_frame(const uint8_t *f,size_t len) { assert(f && len == SCREEN_FRAME_BYTES); ++shows; return display_result; }
esp_err_t screen_driver_show_mono(uint8_t *f, size_t len) { return screen_driver_show_frame(f, len); }
bool tap_enabled(void) { return false; }
void screen_driver_mono_stash_lost(void) {}
void frame_fill(uint8_t *f,uint8_t ink) { memset(f,ink,SCREEN_FRAME_BYTES); }
void frame_rect(uint8_t *f,int x,int y,int w,int h,uint8_t ink) { (void)f;(void)x;(void)y;(void)w;(void)h;(void)ink; }
int frame_text(uint8_t *f,int x,int y,const char *s,int scale,uint8_t ink) { (void)f;(void)y;(void)s;(void)scale;(void)ink; return x; }
int frame_text_width(const char *s,int scale) { (void)s;(void)scale; return 0; }
void frame_bitmap_centered(uint8_t *f,const msg_bitmap_t *b,int y,uint8_t ink) { (void)f;(void)b;(void)y;(void)ink; }
void frame_bits(uint8_t *f,int x,int y,int w,int h,const uint8_t *b,uint8_t ink) { (void)f;(void)x;(void)y;(void)w;(void)h;(void)b;(void)ink; }
esp_err_t axp2101_gauge_status(bool *e,bool *p,int *s) { (void)e;(void)p;(void)s; return ESP_FAIL; }
esp_err_t pcf8563_get_time(pcf8563_time_t *t) { (void)t; return ESP_FAIL; }

int main(void) {
    fail_alloc=1; assert(gallery_prepare()==ESP_ERR_NO_MEM); assert(!s_sd && !s_state);
    fail_alloc=alloc_calls+2; int old_deletes=deletes;
    assert(gallery_prepare()==ESP_ERR_NO_MEM); assert(deletes==old_deletes+1); assert(!s_sd && !s_state);
    fail_alloc=0; assert(gallery_prepare()==ESP_OK); void *sd=s_sd, *state=s_state; int calls=alloc_calls;
    assert(gallery_prepare()==ESP_OK && s_sd==sd && s_state==state && alloc_calls==calls);
    assert(!gallery_is_ready()); gallery_set_ready(true); assert(gallery_is_ready()); gallery_set_ready(false); assert(!gallery_is_ready());
    s_n=0; s_cur=-1; assert(gallery_render_initial()==ESP_OK && shows==1);
    display_result=ESP_FAIL; assert(gallery_render_initial()==ESP_FAIL && shows==2); display_result=ESP_OK;
    s_n=1; s_cur=0; strcpy(s_names[0],"one.bin"); s_strip=false;
    assert(gallery_render_initial()==ESP_OK && shows==3); assert(s_frame[0]==0x55);
    task_result=0; assert(gallery_start_task()==ESP_ERR_NO_MEM); task_result=pdPASS; assert(gallery_start_task()==ESP_OK);
    s_n=0; s_cur=-1; int before=shows; if (!setjmp(escape)) gallery_task(NULL); assert(waits==1 && shows==before);
    int old=notifications; gallery_rescan(); assert(notifications==old+1);
    return 0;
}
