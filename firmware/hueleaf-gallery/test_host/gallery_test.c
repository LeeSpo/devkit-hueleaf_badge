#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/stat.h>

static const char *card_names[8], *index_text;
static int card_count, dir_pos, saved_index;
static char saved_name[64];
static uint8_t frame_byte = 0x55;

static size_t host_strlcpy(char *d, const char *s, size_t n) {
    size_t len = strlen(s); if (n) { size_t k = len < n-1 ? len : n-1; memcpy(d,s,k); d[k]=0; } return len;
}
static FILE *fake_open(const char *path, const char *mode) {
    (void)mode;
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
esp_err_t nvs_open(const char *n, int m, nvs_handle_t *h) { (void)n;(void)m;*h=1; return saved_name[0] ? ESP_OK : ESP_ERR_NOT_FOUND; }
esp_err_t nvs_get_i32(nvs_handle_t h,const char *k,int32_t *v) { (void)h;(void)k;*v=saved_index; return ESP_OK; }
esp_err_t nvs_get_u8(nvs_handle_t h,const char *k,uint8_t *v) { (void)h;(void)k;(void)v; return ESP_ERR_NOT_FOUND; }
esp_err_t nvs_get_str(nvs_handle_t h,const char *k,char *v,size_t *n) { (void)h;(void)k;host_strlcpy(v,saved_name,*n); return ESP_OK; }
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
    int old=notifications;
    s_force=false; gallery_rescan(false);
    assert(notifications==old && !s_force && shows==before);
    gallery_rescan(true); assert(notifications==old+1 && s_force);
    /* Quiet scanning preserves the current file even when new names sort
     * before it, without drawing or changing the persisted selection. */
    card_names[0]="one.bin"; card_names[1]="z-last.bin"; card_count=2;
    s_n=1; s_cur=0; strcpy(s_names[0],"one.bin"); strcpy(saved_name,"one.bin");
    s_force=false; old=notifications; before=shows;
    assert(gallery_rescan(false)==2);
    card_names[2]="a-first.bin"; card_count=3;
    assert(gallery_rescan(false)==3 && s_cur==1 && !strcmp(s_names[s_cur],"one.bin"));
    assert(notifications==old && shows==before && !s_force && !strcmp(saved_name,"one.bin"));
    assert(gallery_init()==ESP_OK && s_cur==1 && !strcmp(s_names[s_cur],"one.bin"));
    assert(gallery_render_initial()==ESP_OK && s_frame[0]==0x55);
    /* An overwrite stays invisible until explicitly drawn/restarted. */
    frame_byte=0x66; old=notifications; before=shows;
    gallery_rescan(false); assert(notifications==old && shows==before);
    assert(gallery_init()==ESP_OK && s_cur==1);
    assert(gallery_render_initial()==ESP_OK && s_frame[0]==0x66);
    /* Starting with an empty card: uploads leave the empty message intact,
     * and startup later selects the first image. */
    card_count=0; saved_name[0]=0; assert(gallery_init()==ESP_OK && s_n==0);
    assert(gallery_render_initial()==ESP_OK); old=notifications; before=shows;
    card_count=3; gallery_rescan(false);
    assert(notifications==old && shows==before && !s_force);
    assert(gallery_init()==ESP_OK && s_cur==0 && !strcmp(s_names[0],"a-first.bin"));
    assert(gallery_render_initial()==ESP_OK);
    return 0;
}
