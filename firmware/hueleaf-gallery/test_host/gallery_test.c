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
esp_err_t nvs_open(const char *n, int m, nvs_handle_t *h) { (void)n;(void)m;*h=1; return saved_name[0] ? ESP_OK : ESP_ERR_NOT_FOUND; }
esp_err_t nvs_get_i32(nvs_handle_t h,const char *k,int32_t *v) { (void)h;(void)k;*v=saved_index; return ESP_OK; }
esp_err_t nvs_get_u8(nvs_handle_t h,const char *k,uint8_t *v) { (void)h;(void)k;(void)v; return ESP_ERR_NOT_FOUND; }
esp_err_t nvs_get_str(nvs_handle_t h,const char *k,char *v,size_t *n) { (void)h;(void)k;host_strlcpy(v,saved_name,*n); return ESP_OK; }
esp_err_t nvs_set_i32(nvs_handle_t h,const char *k,int32_t v) { (void)h;(void)k;(void)v; return ESP_OK; }
esp_err_t nvs_set_u8(nvs_handle_t h,const char *k,uint8_t v) { (void)h;(void)k;(void)v; return ESP_OK; }
esp_err_t nvs_set_str(nvs_handle_t h,const char *k,const char *v) { (void)h;(void)k;(void)v; return ESP_OK; }
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

#ifndef GALLERY_REFRESH_TEST
static void test_button_navigation(void)
{
    /* Left/BOOT goes back; right/PEK goes forward, wrapping at both ends. */
    assert(s_n==3 && s_cur==0);
    int before=shows;
    assert(gallery_btn_boot_short()); run_gallery_once(); assert(s_cur==2);
    assert(gallery_btn_pek_short()); run_gallery_once(); assert(s_cur==0);
    assert(gallery_btn_pek_short()); run_gallery_once(); assert(s_cur==1);
    assert(gallery_btn_boot_short()); run_gallery_once(); assert(s_cur==0);
    assert(shows==before+4);
    assert(gallery_tap_next()); run_gallery_once(); assert(s_cur==1);

    /* Refreshing and queued input must not accumulate extra navigation. */
    s_rendering=true;
    int old=notifications;
    before=shows;
    assert(!gallery_btn_boot_short() && !gallery_btn_pek_short() && !gallery_tap_next());
    assert(s_pending==0 && notifications==old && shows==before);
    s_rendering=false;
    assert(gallery_btn_boot_short());
    old=notifications;
    assert(!gallery_btn_boot_short() && !gallery_btn_pek_short() && !gallery_tap_next());
    assert(notifications==old && shows==before);
    run_gallery_once(); assert(s_cur==0 && shows==before+1);
}

static void test_picker_controls(void)
{
    /* Exercise both picker modes across a page boundary without drawing it. */
    int count=s_n, before=shows;
    s_n=PK_ROWS+2;
    assert(gallery_btn_boot_long() && pk_on && pk_cur==s_cur);
    assert(gallery_btn_boot_short() && pk_cur==1);
    assert(gallery_btn_pek_short() && pk_cur==PK_ROWS);
    assert(gallery_btn_pek_short() && pk_cur==0);
    tap_on=true;
    assert(gallery_btn_pek_short() && pk_cur==s_n-1);
    assert(gallery_btn_boot_short() && pk_cur==0);
    assert(gallery_tap_next() && pk_cur==PK_ROWS);
    tap_on=false;
    s_n=count; pk_cur=s_cur;
    assert(gallery_btn_boot_long() && pk_confirm);
    assert(picker_step()==portMAX_DELAY && !pk_on && !s_force && shows==before);

    /* Long-press selection still displays the highlighted image. */
    assert(gallery_btn_boot_long() && pk_on);
    assert(gallery_btn_boot_short() && pk_cur==1);
    assert(gallery_btn_boot_long() && pk_confirm);
    assert(picker_step()==portMAX_DELAY && !pk_on);
    run_gallery_once(); assert(s_cur==1 && shows==before+1);
}

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
    assert(gallery_render_initial()==ESP_OK && shows==3); assert(shown_byte()==0x55);
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
    assert(gallery_render_initial()==ESP_OK);
    assert(shown_byte()==0x55);
    /* An overwrite stays invisible until explicitly drawn/restarted. */
    frame_byte=0x66; old=notifications; before=shows;
    gallery_rescan(false); assert(notifications==old && shows==before);
    assert(gallery_init()==ESP_OK && s_cur==1);
    assert(gallery_render_initial()==ESP_OK);
    assert(shown_byte()==0x66);
    /* Starting with an empty card: uploads leave the empty message intact,
     * and startup later selects the first image. */
    card_count=0; saved_name[0]=0; assert(gallery_init()==ESP_OK && s_n==0);
    assert(gallery_render_initial()==ESP_OK); old=notifications; before=shows;
    card_count=3; gallery_rescan(false);
    assert(notifications==old && shows==before && !s_force);
    assert(gallery_init()==ESP_OK && s_cur==0 && !strcmp(s_names[0],"a-first.bin"));
    assert(gallery_render_initial()==ESP_OK);
    gallery_set_ready(true);
    test_button_navigation();
    test_picker_controls();
    /* Session admission and physical input share the same state lock. */
    old=notifications; before=shows;
    assert(gallery_upload_session_begin());
    assert(!gallery_btn_boot_short() && !gallery_btn_pek_short());
    assert(!gallery_btn_boot_long() && !gallery_tap_next());
    gallery_nav(1); gallery_set_strip(true);
    assert(gallery_show_name("one.bin")==ESP_ERR_INVALID_STATE);
    assert(s_pending==0 && !s_request[0] && !s_force && !pk_on);
    assert(notifications==old && shows==before);
    assert(runtime_session_end());
    assert(notifications==old && shows==before);
    assert(gallery_btn_boot_short() && s_pending==-1);
    assert(!gallery_upload_session_begin());
    s_pending=0;
    assert(gallery_upload_file_begin());
    old=notifications;
    assert(!gallery_btn_boot_long() && !gallery_btn_boot_short() && !gallery_btn_pek_short() && !gallery_tap_next());
    assert(s_pending==0 && notifications==old);
    runtime_file_end();
    assert(gallery_btn_boot_long() && pk_on);
    assert(!gallery_upload_session_begin());
    pk_on=false; pk_drawn=false; pk_dirty=false; s_force=false;
    s_cur=-1; before=shows;
    assert(gallery_btn_pek_short()); run_gallery_once();
    assert(s_cur==0 && shows==before+1);  /* First next after a formerly empty album. */
    before=shows;
    assert(gallery_render_scheduled(s_names[0]) == ESP_OK && s_cur == 1 && shows == before + 1);
    assert(gallery_render_scheduled(s_names[2]) == ESP_OK && s_cur == 0 && shows == before + 2);
    before=shows;
    int ok_before=frame_ok;
    char persisted_before[64]; strcpy(persisted_before, saved_name);
    assert(gallery_restore_retained(s_names[2]) == ESP_OK && s_cur == 2 && shows == before);
    assert(frame_ok == ok_before + 1 && !s_force && !strcmp(saved_name, persisted_before));
    assert(gallery_restore_retained("missing.bin") == ESP_OK && shows == before + 1);
    int old_cur = s_cur;
    display_result=ESP_FAIL;
    assert(gallery_render_scheduled(s_names[0]) == ESP_FAIL && s_cur == old_cur);
    display_result=ESP_OK;
    s_cur=0;
    int activity_before=activity_count;
    slide_due=false; assert(!gallery_auto_next()); slide_due=true;
    assert(gallery_auto_next() && !gallery_sleep_begin() && activity_count == activity_before);
    run_gallery_once(); assert(s_cur == 1);
    assert(gallery_sleep_begin());
    assert(!gallery_auto_next() && !gallery_btn_pek_short() && !gallery_upload_session_begin());
    runtime_sleep_end();
    s_n=1; s_cur=0; before=shows;
    assert(gallery_render_scheduled(s_names[0]) == ESP_OK && shows == before);
    assert(gallery_restore_retained(s_names[0]) == ESP_OK && shows == before);
    assert(!gallery_auto_next());
    s_n=0; s_cur=-1;
#ifdef CONFIG_HL_REMOTE
    assert(gallery_sleep_begin()); runtime_sleep_end();
#else
    assert(!gallery_sleep_begin());
#endif
    assert(frame_ok > 0 && frame_failed > 0);
    /* BLE admission reserves queued navigation before render task starts. */
    s_n=3; s_cur=0; s_force=false; s_pending=0; s_request[0]=0; s_view.magic=0;
    before=shows;
    assert(gallery_remote_request(+1, NULL) == ESP_OK);
    assert(runtime_remote_operation_pending());
    assert(gallery_remote_request(+1, NULL) == ESP_ERR_INVALID_STATE);
    assert(!gallery_btn_pek_short() && !gallery_sleep_begin() && !gallery_upload_session_begin());
    run_gallery_once();
    assert(s_cur == 1 && shows == before+1 && !runtime_remote_operation_pending());
    before=shows;
    gallery_restore_remote("removed.bin");
    assert(s_cur == -1 && !s_force && shows == before);
    assert(gallery_remote_request(0, "removed.bin") == ESP_ERR_NOT_FOUND);
    assert(!runtime_remote_operation_pending());
    assert(gallery_remote_request(0, s_names[0]) == ESP_OK);
    display_result=ESP_FAIL;
    run_gallery_once();
    assert(!runtime_remote_operation_pending() && s_cur == -1);
    display_result=ESP_OK;


    /* Timed navigation and timer boot use the same name-based filter.
     * Manual navigation still uses the complete gallery. */
    card_names[0]="a.bin"; card_names[1]="b.bin"; card_names[2]="c.bin"; card_names[3]="d.bin";
    card_count=4; selection_text="HLSEL1\na.bin\nc.bin\n";
    gallery_rescan(false); s_cur=0; s_force=false; before=shows;
    assert(gallery_auto_next()); run_gallery_once(); assert(s_cur==2 && shows==before+1);
    assert(gallery_auto_next()); run_gallery_once(); assert(s_cur==0);
    assert(gallery_btn_pek_short()); run_gallery_once(); assert(s_cur==1); /* B remains manual */
    assert(gallery_auto_next()); run_gallery_once(); assert(s_cur==2);
    before=shows;
    assert(gallery_render_scheduled("b.bin")==ESP_OK && s_cur==2 && shows==before);
    assert(gallery_render_scheduled("c.bin")==ESP_OK && s_cur==0 && shows==before+1);
    assert(gallery_render_scheduled("missing.bin")==ESP_OK && s_cur==0);
    selection_text="HLSEL1\nc.bin\n"; gallery_rescan(false);
    assert(gallery_auto_next()); run_gallery_once(); assert(s_cur==2);
    before=shows;
    assert(!gallery_auto_next());
    assert(gallery_render_scheduled("c.bin")==ESP_OK && shows==before);
    assert(gallery_restore_retained("b.bin")==ESP_OK && s_cur==1 && shows==before);
    assert(gallery_auto_next()); run_gallery_once(); assert(s_cur==2);
    selection_text="HLSEL1\n"; gallery_rescan(false); before=shows;
    assert(!gallery_auto_next());
    assert(gallery_render_scheduled("b.bin")==ESP_OK && s_cur==1 && shows==before);
    assert(gallery_sleep_begin()); runtime_sleep_end();
    assert(gallery_btn_pek_short()); run_gallery_once(); assert(s_cur==2);
    selection_text="broken\n"; gallery_rescan(false); before=shows;
    runtime_snapshot_t snapshot; runtime_snapshot(&snapshot);
    assert(snapshot.slideshow_fault && !gallery_auto_next());
    assert(gallery_render_scheduled("b.bin")==ESP_OK && shows==before);
    selection_text=NULL; gallery_rescan(false); s_cur=0;
    assert(gallery_auto_next()); run_gallery_once(); assert(s_cur==1); /* missing file = legacy all */
#ifdef CONFIG_HL_REMOTE
    /* All compact UI entrances must use MonoFast, including external frames. */
    static uint8_t external[FRAME_UI_BYTES];
    int mono_before=mono_calls;
    assert(gallery_show_ui_frame(external)==ESP_OK && last_mono_bitmap==external);
    assert(runtime_display_begin());
    render_picker(0,1,s_names);
    assert(last_mono_bitmap==s_frame && mono_calls==mono_before+2);
    display_result=ESP_FAIL;
    assert(gallery_show_ui_frame(external)==ESP_FAIL);
    runtime_snapshot(&snapshot); assert(!snapshot.display);
    display_result=ESP_OK;
#endif
    return 0;
}

#endif
