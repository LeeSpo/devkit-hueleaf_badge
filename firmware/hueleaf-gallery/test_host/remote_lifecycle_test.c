/* Execute the real remote coordinator, including SDK timer/PM interactions. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "platform.h"
#include "../main/remote.c"

static int64_t time_us = 1000000, slide_remaining = -1;
static uint64_t rtc_us = 1000000000, timer_us, wake_mask;
static bool timer_armed, pm_locked, usb, mounted, ready, tx_pending;
static bool usb_boot, usb_probe, usb_block;
static int usb_prepare_error;
static bool slide_overdue;
static int reset_reason = ESP_RST_POWERON, fail_stage, sleeps, replies, operations;
static esp_sleep_wakeup_cause_t cause;
static jmp_buf escape;
static command_t queued;
static bool queue_full;
static unsigned last_op, last_id;
static bool do_gallery_work;

/* An idle interval rewrites the timer unless NO_LIGHT_SLEEP is held, as IDF
 * pm_impl.c does. Every blocking sleep-preparation stub models that interval. */
static void idle(void) { if (!pm_locked) { timer_armed = true; timer_us = 200000; } }
int64_t esp_timer_get_time(void) { return time_us; }
uint64_t esp_rtc_get_time_us(void) { return rtc_us; }
int esp_reset_reason(void) { return reset_reason; }
esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause(void) { return cause; }
uint64_t esp_sleep_get_ext1_wakeup_status(void) { return wake_mask; }
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t us) { assert(pm_locked); timer_armed=true; timer_us=us; return fail_stage==4 ? ESP_FAIL : ESP_OK; }
esp_err_t esp_sleep_disable_wakeup_source(esp_sleep_wakeup_cause_t c)
{ assert(c==ESP_SLEEP_WAKEUP_TIMER && pm_locked); bool armed=timer_armed; timer_armed=false; timer_us=0; return armed ? ESP_OK : ESP_ERR_INVALID_STATE; }
void esp_deep_sleep_start(void)
{ assert(!timer_armed || timer_us!=200000); assert(pm_locked); sleeps++; longjmp(escape,1); }
esp_err_t esp_pm_lock_create(int t,int n,const char *name,esp_pm_lock_handle_t *out)
{ assert(t==ESP_PM_NO_LIGHT_SLEEP); (void)n;(void)name; *out=&pm_locked; return ESP_OK; }
esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t l) { assert(l && !pm_locked); pm_locked=true; return ESP_OK; }
esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t l) { assert(l && pm_locked); pm_locked=false; return ESP_OK; }
esp_err_t esp_pm_configure(const esp_pm_config_t *c) { assert(c->light_sleep_enable); idle(); return ESP_OK; }
esp_err_t tap_wake_prepare(void) { idle(); return fail_stage==1 ? ESP_ERR_NOT_FINISHED : ESP_OK; }
void tap_wake_release(void) {}
esp_err_t board_sleep_prepare(void) { idle(); return fail_stage==3 ? ESP_FAIL : ESP_OK; }
void board_sleep_release(void) {}
bool usb_serial_jtag_is_connected(void) { return usb; }
bool usb_wake_boot(void) { return usb_boot; }
bool usb_wake_probe_only(void) { if (usb) usb_probe=false; return usb_probe; }
void usb_wake_activity(void) { usb_probe=false; }
void usb_wake_poll(void) {}
bool usb_wake_blocked(void) { return usb_block; }
esp_err_t usb_wake_prepare(void) { idle(); return usb_prepare_error; }
void usb_wake_cancel(void) {}
void slideshow_resume_deadline(int64_t due) { (void)due; }

bool tf_card_is_mounted(void) { return mounted; }
bool tf_card_has_fault(void) { return false; }
esp_err_t tf_card_mount(void) { mounted=true; return ESP_OK; }
esp_err_t tf_card_unmount(void) { idle(); mounted=false; return ESP_OK; }
bool screen_driver_initialized(void) { return ready; }
esp_err_t screen_driver_deep_sleep(void) { idle(); return fail_stage==2 ? ESP_FAIL : ESP_OK; }
esp_err_t screen_driver_resume(void) { return ESP_OK; }
bool gallery_is_ready(void) { return ready; }
bool gallery_sleep_begin(void) { return runtime_sleep_begin(); }
bool gallery_picker_open(void) { return false; }
void sd_lock(void) {} void sd_unlock(void) {}
void slideshow_snapshot(slideshow_snapshot_t *v) { *v=(slideshow_snapshot_t){.interval_min=10}; }
int64_t slideshow_remaining_ms(void) { return slide_remaining; }
bool slideshow_due(void) { return slide_overdue; }
void slideshow_retain_remote(const char *n,uint32_t m,bool timer) { (void)n;(void)m;(void)timer; }
esp_err_t gallery_selection_read(uint16_t a,uint64_t b,uint8_t *c,size_t d,size_t *n)
{ (void)a;(void)b;(void)c;(void)d; *n=0; return ESP_OK; }
static int labeled_reads;
esp_err_t gallery_selection_labels_read(uint16_t a,uint64_t b,uint8_t *c,size_t d,size_t *n)
{
    runtime_snapshot_t snap; runtime_snapshot(&snap); assert(snap.storage);
    assert(d <= REMOTE_MESSAGE_MAX - 6); labeled_reads++;
    return gallery_selection_read(a,b,c,d,n);
}
esp_err_t gallery_remote_request(int delta,const char *name)
{ (void)delta;(void)name; assert(runtime_remote_operation_begin()); operations++; do_gallery_work=true; return ESP_OK; }
static void gallery_complete(void)
{ assert(do_gallery_work); do_gallery_work=false; runtime_remote_operation_end(); remote_render_done(true); }
esp_err_t ble_remote_start(const uint8_t *k,bool p) { (void)k;(void)p; return ESP_OK; }
esp_err_t ble_remote_stop(void) { idle(); return ESP_OK; }
void ble_remote_slow(void) {}
bool ble_remote_pending(void) { return tx_pending; }
void ble_remote_disconnect(void) { remote_disconnected(); }
void ble_remote_reply(uint16_t id,uint8_t op,int status,const void *p,size_t n)
{ (void)p;(void)n; assert(status==0); last_id=id; last_op=op; replies++; tx_pending=true; }
int xTaskCreate(void (*f)(void *),const char *n,unsigned st,void *a,unsigned p,TaskHandle_t *out)
{ (void)f;(void)n;(void)st;(void)a;(void)p; *out=(void *)1; return pdPASS; }
void xTaskNotifyGive(TaskHandle_t t) { (void)t; }
uint32_t ulTaskNotifyTake(int c,TickType_t t) { (void)c;(void)t; longjmp(escape,2); }
void vTaskDelay(TickType_t t) { (void)t; idle(); }
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t t) { (void)t; return 2000; }
QueueHandle_t xQueueCreate(unsigned n,unsigned size) { assert(n==1 && size==sizeof(command_t)); return &queued; }
int xQueueSend(QueueHandle_t q,const void *cmd,unsigned t)
{ (void)q;(void)t; assert(!queue_full); queued=*(const command_t *)cmd; queue_full=true; return pdTRUE; }
int xQueueReceive(QueueHandle_t q,void *cmd,unsigned t)
{ (void)q;(void)t; if (!queue_full) return 0; *(command_t *)cmd=queued; queue_full=false; return pdTRUE; }
unsigned esp_get_free_heap_size(void) { return 50000; }
unsigned heap_caps_get_largest_free_block(int c) { (void)c; return 30000; }
const char *esp_err_to_name(esp_err_t e) { (void)e; return "test"; }
esp_err_t nvs_flash_init_partition(const char *p) { (void)p; return ESP_OK; }
esp_err_t nvs_open_from_partition(const char *p,const char *n,int mode,nvs_handle_t *h)
{ (void)p;(void)n;(void)mode; *h=1; return ESP_OK; }
esp_err_t nvs_get_blob(nvs_handle_t h,const char *k,void *p,size_t *n)
{ (void)h;(void)k;(void)p;(void)n; return ESP_ERR_NOT_FOUND; }
esp_err_t nvs_get_u8(nvs_handle_t h,const char *k,uint8_t *v) { (void)h;(void)k;(void)v; return ESP_ERR_NOT_FOUND; }
esp_err_t nvs_set_blob(nvs_handle_t h,const char *k,const void *p,size_t n) { (void)h;(void)k;(void)p;(void)n; return ESP_OK; }
esp_err_t nvs_set_u8(nvs_handle_t h,const char *k,uint8_t v) { (void)h;(void)k;(void)v; return ESP_OK; }
esp_err_t nvs_erase_key(nvs_handle_t h,const char *k) { (void)h;(void)k; return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; }
/* Deterministic digest test double: exercises marker lifecycle/corruption. */
int mbedtls_sha256(const uint8_t *p,size_t n,uint8_t *out,int mode)
{ (void)mode; memset(out,0,32); for(size_t i=0;i<n;i++) out[i%32]=(out[i%32]*33)^p[i]; return 0; }

static void run_task_once(void) { if (!setjmp(escape)) task(NULL); }
static void sleep_now(void)
{
    int before=sleeps;
    if (!setjmp(escape)) { tick(); assert(!"sleep expected"); }
    assert(sleeps==before+1 && pm_locked);
    pm_locked=false; runtime_sleep_end(); mounted=true; /* a reboot clears DRAM/locks */
}
static void boot_clear(void)
{
    s_enabled=s_ready=s_initializing=s_success=s_fault=s_tx_wait=false;
    s_light=s_cooldown_boot=false; s_slide_due_us=0; s_image[0]=0;
    s_task=NULL; ready=false; runtime_set_ready(false); s_policy=(remote_policy_t){0};
}

int main(void)
{
    assert(!remote_boot_init(false)); assert(remote_init()==ESP_OK);
    ready=true; runtime_set_ready(true); runtime_publish_gallery(0,1,"one.bin",false); remote_ready();
    assert(remote_connected() && remote_authenticate());
    remote_disconnected(); time_us+=10000000;
    timer_armed=true; timer_us=200000; /* leaked automatic light-sleep timer */
    sleep_now(); assert(!timer_armed && timer_us==0);
    /* Tap wake, authenticate, status/list, disconnect: no render request. */
    for (int cycle=0;cycle<10;cycle++) {
        boot_clear(); reset_reason=ESP_RST_DEEPSLEEP; cause=ESP_SLEEP_WAKEUP_EXT1; wake_mask=1ULL<<imu_irq;
        assert(remote_boot_init(false) && s_light); assert(remote_init()==ESP_OK);
        assert(remote_connected() && remote_authenticate()); assert(remote_wait()==ESP_OK);
        ready=true; runtime_set_ready(true); remote_ready();
        command_t status={.epoch=s_epoch,.len=4,.data={1,REMOTE_STATUS,1,0}};
        tx_pending=false; execute(&status); tx_pending=false;
        command_t list={.epoch=s_epoch,.len=14,.data={1,REMOTE_LIST,2,0}};
        execute(&list); tx_pending=false;
        command_t labels={.epoch=s_epoch,.len=14,.data={1,REMOTE_LIST_LABELS,3,0}};
        int before_labels=labeled_reads;
        execute(&labels); tx_pending=false;
        assert(labeled_reads==before_labels+1 && last_op==REMOTE_LIST_LABELS);
        assert(operations==0);
        remote_disconnected(); time_us+=10000000; idle(); sleep_now(); assert(!timer_armed);
    }
    /* Stray/early timers cannot force a full startup or an old-image redraw. */
    boot_clear(); cause=ESP_SLEEP_WAKEUP_TIMER;
    assert(remote_boot_init(false)); assert(s_light);
    assert(remote_init()==ESP_OK); ready=true; runtime_set_ready(true); remote_ready();
    assert(remote_connected() && remote_authenticate());
    /* ACK and completion are ordered even for an unchanged image. */
    command_t show={.epoch=s_epoch,.len=11,.data={1,REMOTE_SHOW,3,0,'o','n','e','.','b','i','n'}};
    execute(&show); assert(s_tx_wait && last_op==REMOTE_SHOW && last_id==3);
    gallery_complete(); int ack_count=replies;
    run_task_once(); assert(replies==ack_count && s_tx_wait); /* ACK not yet confirmed */
    tx_pending=false; run_task_once();
    assert(!s_tx_wait && !runtime_remote_operation_pending() && last_op==0x40 && last_id==3);
    tx_pending=false; execute(&show); assert(operations==1 && last_op==REMOTE_SHOW); /* retry is cached */
    tx_pending=false;
    show.data[2]=4; execute(&show); gallery_complete(); tx_pending=false;
    remote_disconnected(); ack_count=replies; run_task_once();
    assert(!s_tx_wait && replies==ack_count && !runtime_remote_operation_pending());

    /* Slideshow timers survive all blocking preparation unchanged. */
    slide_remaining=60000; remote_disconnected(); time_us+=10000000;
    sleep_now(); assert(timer_armed && timer_us==60000000);
    boot_clear(); rtc_us+=60000000; cause=ESP_SLEEP_WAKEUP_TIMER;
    assert(!remote_boot_init(false) && !s_light); /* real due timer: full scheduled startup */
    s_retained.magic=RETAIN_MAGIC; s_retained.inverse=~RETAIN_MAGIC; retain_hash(s_retained.digest);
    assert(!remote_boot_init(true)); /* OTA pending cannot trust retained state */
    s_retained.magic=RETAIN_MAGIC; s_retained.inverse=~RETAIN_MAGIC; retain_hash(s_retained.digest);
    s_retained.digest[0]^=1; assert(!remote_boot_init(false));

    /* Early exits and failures release the PM and sleep guards. */
    boot_clear(); s_light=false; slide_remaining=-1; assert(remote_init()==ESP_OK);
    ready=true; runtime_set_ready(true); remote_ready();
    time_us+=30000000;
    for (fail_stage=1;fail_stage<=4;fail_stage++) {
        s_fault=false; s_policy.phase=REMOTE_SLEEP;
        if (fail_stage==4) slide_remaining=60000;
        assert(!tick() && !pm_locked);
        runtime_snapshot_t v; runtime_snapshot(&v); assert(!v.sleeping);
    }
    fail_stage=0; s_fault=false; s_initializing=false; s_success=false;
    s_policy.phase=REMOTE_SLEEP; s_policy.options.cooldown_ms=60000; s_policy.options.cooldown_after=1;
    slide_remaining=-1; s_missed=0;
    sleep_now(); assert(timer_armed && timer_us==60000000 && s_retained.cooldown);
    /* A charging cable during cooldown preserves its original absolute end. */
    int64_t cooldown_end=s_retained.cooldown_due_us;
    boot_clear(); rtc_us+=5000000; cause=ESP_SLEEP_WAKEUP_ULP;
    usb_boot=usb_probe=usb_block=true;
    assert(remote_boot_init(false) && s_light && s_cooldown_due_us==cooldown_end);
    assert(remote_init()==ESP_OK && remote_wait()==ESP_OK);
    ready=true; runtime_set_ready(true); remote_ready();
    assert(!tick());
    rtc_us+=5000000; usb_block=false;
    sleep_now();
    assert(timer_us==50000000 && s_retained.cooldown && s_retained.cooldown_due_us==cooldown_end);
    usb_boot=usb_probe=false;
    boot_clear(); rtc_us+=50000000; cause=ESP_SLEEP_WAKEUP_TIMER;
    assert(remote_boot_init(false) && s_cooldown_boot);
    assert(remote_init()==ESP_OK); sleep_now();
    assert(!timer_armed); /* cooldown ends in tap-wait sleep, never a timer loop */
    /* A USB wake starts diagnostics immediately without BLE authentication,
     * keeps the image, and does not consume a missed discovery attempt. */
    boot_clear(); cause=ESP_SLEEP_WAKEUP_ULP; usb_boot=usb_probe=usb_block=true;
    assert(remote_boot_init(false) && s_light);
    assert(remote_init()==ESP_OK && s_initializing);
    assert(remote_wait()==ESP_OK); /* no host yet: enumeration needs proto_start */
    ready=true; runtime_set_ready(true); remote_ready();
    assert(!tick() && !s_fault);
    usb_block=false; usb=true;
    assert(!tick() && !usb_wake_probe_only());
    usb=false; usb_probe=true;
    s_policy.phase=REMOTE_WAIT; s_missed=0;
    s_policy.options.cooldown_after=3;
    sleep_now(); assert(s_missed==0 && !s_retained.cooldown);
    /* A slideshow deadline crossed during probing still gets its timer. */
    slide_overdue=true; slide_remaining=-1000; s_policy.phase=REMOTE_SLEEP;
    sleep_now(); assert(timer_armed && timer_us==1000 && s_retained.slide_due_us);
    slide_overdue=false; slide_remaining=-1;
    /* An insertion racing the final sleep step is a recoverable retry. */
    usb_boot=usb_probe=false; s_policy.phase=REMOTE_SLEEP;
    usb_prepare_error=ESP_ERR_NOT_FINISHED;
    assert(!tick() && !s_fault && !pm_locked);
    runtime_snapshot_t snapshot; runtime_snapshot(&snapshot); assert(!snapshot.sleeping);
    usb_prepare_error=ESP_OK;
    puts("OK actual remote lifecycle: stale timer, PM exclusion, 10 reconnects, no-op ACK/completion, scheduled wake, corruption/OTA and failure guards");
    return 0;
}
