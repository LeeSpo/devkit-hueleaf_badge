#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "slideshow.h"
#include "runtime_state.h"
#include "esp_sleep.h"
#include "nvs.h"

static char directory[] = "/tmp/hl-slideshow-XXXXXX", cfg[256], tmp[256];
static bool host, nv_exists, io_fault, allow_sleep, held, panel_down, mounted = true;
static bool host_during_prepare, input_before_admission;
static bool usb_boot, usb_probe, usb_block;
static int usb_prepare_error, usb_cancels;
static uint32_t nv;
static int fail_stage, sleeps, auto_nexts, resumes, remounts, boot_releases;
static int64_t now_us;
static int usb_checks, tf_checks, picker_checks, clock_checks, notify_count;
static int last_wait;
static uint32_t pending_notifications;
static bool enable_while_waiting;
static size_t largest_allocation = 8192;
static int allocations, allocation_failure;
static void *bounded_malloc(size_t n)
{
    allocations++;
    return n > largest_allocation || allocations == allocation_failure ? NULL : malloc(n);
}
static uint64_t timer_us, wake_mask = 1ULL << 4;
static int wake_prepares, wake_error;
static bool wake_armed;
static esp_sleep_wakeup_cause_t cause;
static jmp_buf escape;
static const char *map_path(const char *p)
{ return strstr(p, ".hueleaf.cfg.tmp") ? tmp : cfg; }
static FILE *mock_open(const char *p, const char *mode) { return fopen(map_path(p), mode); }
static int mock_unlink(const char *p) { return unlink(map_path(p)); }
static int mock_sync(int fd) { return io_fault ? -1 : fsync(fd); }
static size_t host_strlcpy(char *d, const char *s, size_t cap)
{ size_t n = strlen(s); if (cap) { size_t k = n < cap - 1 ? n : cap - 1; memcpy(d, s, k); d[k] = 0; } return n; }
#undef strlcpy
#define strlcpy host_strlcpy
#define fopen mock_open
#define unlink mock_unlink
#define fsync mock_sync
#define malloc bounded_malloc
#include "../main/slideshow.c"
#undef malloc
#undef fopen
#undef unlink
#undef fsync

int64_t esp_timer_get_time(void) { clock_checks++; return now_us; }
uint64_t esp_rtc_get_time_us(void) { return 1000000000ULL + now_us; }
bool usb_wake_boot(void) { return usb_boot; }
bool usb_wake_probe_only(void) { if (host) usb_probe = false; return usb_probe; }
void usb_wake_activity(void) { usb_probe = false; }
void usb_wake_ready(void) {}
void usb_wake_poll(void) {}
bool usb_wake_blocked(void) { return usb_block; }
esp_err_t usb_wake_prepare(void) { return usb_prepare_error; }
void usb_wake_cancel(void) { usb_cancels++; }

bool usb_serial_jtag_is_connected(void) { usb_checks++; return host; }
esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause(void) { return cause; }
uint64_t esp_sleep_get_ext1_wakeup_status(void) { return wake_mask; }
esp_err_t tap_wake_prepare(void) { wake_prepares++; wake_armed = !wake_error; return wake_error; }
void tap_wake_release(void) { wake_armed = false; }
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t us) { timer_us = us; return fail_stage == 1 ? ESP_FAIL : ESP_OK; }
esp_err_t esp_sleep_disable_wakeup_source(esp_sleep_wakeup_cause_t source)
{ assert(source == ESP_SLEEP_WAKEUP_TIMER); timer_us = 0; return ESP_OK; }
void esp_deep_sleep_start(void)
{
    assert(allow_sleep && held && panel_down && !mounted && timer_us > 0);
    sleeps++; longjmp(escape, 1);
}
esp_err_t board_sleep_prepare(void)
{ held = true; if (host_during_prepare) host = true; return fail_stage == 4 ? ESP_FAIL : ESP_OK; }
void board_sleep_release(void) { held = false; }
/* Re-routes pads to plain GPIO: legal only before the SPI devices exist. */
void board_sleep_boot_release(void) { boot_releases++; held = false; }
const char *esp_err_to_name(esp_err_t e) { (void)e; return "injected"; }
esp_err_t screen_driver_deep_sleep(void) { panel_down = true; return fail_stage == 2 ? ESP_FAIL : ESP_OK; }
esp_err_t screen_driver_resume(void) { panel_down = false; resumes++; return ESP_OK; }
esp_err_t tf_card_unmount(void)
{ if (fail_stage == 3) return ESP_FAIL; mounted = false; return ESP_OK; }
esp_err_t tf_card_mount(void) { mounted = true; remounts++; return ESP_OK; }
bool tf_card_has_fault(void) { tf_checks++; return false; }
bool gallery_sleep_begin(void)
{ if (input_before_admission) slideshow_activity(); return runtime_sleep_begin(); }
bool gallery_picker_open(void) { picker_checks++; return false; }
bool gallery_auto_next(void) { auto_nexts++; return true; }
void sd_lock(void) {}
void sd_unlock(void) {}
uint32_t ulTaskNotifyTake(int clear, TickType_t timeout)
{
    assert(clear == pdTRUE && (timeout == 250 || timeout == portMAX_DELAY));
    last_wait = timeout;
    if (timeout == portMAX_DELAY && enable_while_waiting) {
        enable_while_waiting = false;
        assert(slideshow_set(true, 2) == ESP_OK);
    }
    if (pending_notifications) {
        uint32_t count = pending_notifications; pending_notifications = 0; return count;
    }
    longjmp(escape, 2);
}
void xTaskNotifyGive(TaskHandle_t task)
{ assert(task == s_task && task); notify_count++; pending_notifications++; }
int xTaskCreate(void (*f)(void *), const char *name, int stack, void *arg, int pri, void *h)
{ (void)f; (void)name; (void)stack; (void)arg; (void)pri; assert(h); *(TaskHandle_t *)h = (void *)1; return pdPASS; }
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *h)
{ (void)mode; assert(!strcmp(name, "hlgal")); *h = 1; return ESP_OK; }
esp_err_t nvs_get_u32(nvs_handle_t h, const char *key, uint32_t *v)
{ (void)h; assert(!strcmp(key, "slide_cfg")); if (!nv_exists) return ESP_ERR_NOT_FOUND; *v = nv; return ESP_OK; }
esp_err_t nvs_set_u32(nvs_handle_t h, const char *key, uint32_t v)
{ (void)h; (void)key; nv = v; nv_exists = true; return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; }
bool file_store_replace(const char *from, const char *to)
{ assert(!strcmp(from, CFG_TMP) && !strcmp(to, HL_CFG_PATH)); return !io_fault && rename(tmp, cfg) == 0; }

static void write_cfg(const char *text)
{ FILE *f = fopen(cfg, "wb"); assert(f); assert(fputs(text, f) >= 0); assert(!fclose(f)); }
static void read_cfg(char *text, size_t cap)
{ FILE *f = fopen(cfg, "rb"); assert(f); size_t n = fread(text, 1, cap - 1, f); text[n] = 0; fclose(f); }
static void tick(void)
{ if (!setjmp(escape)) slideshow_task(NULL); }

static void assert_disabled_is_idle(void)
{
    int usb_before = usb_checks, tf_before = tf_checks, picker_before = picker_checks, clock_before = clock_checks;
    tick();
    slideshow_activity(); slideshow_frame_done(true); assert(!slideshow_due());
    assert(last_wait == portMAX_DELAY);
    assert(usb_checks == usb_before && tf_checks == tf_before && picker_checks == picker_before);
    assert(clock_checks == clock_before);
}

int main(void)
{
    assert(mkdtemp(directory));
    snprintf(cfg, sizeof(cfg), "%s/config", directory); snprintf(tmp, sizeof(tmp), "%s/tmp", directory);
    runtime_set_ready(true); runtime_publish_gallery(0, 3, "a.bin", true);
    runtime_publish_slideshow(3, true, false, false);
    slideshow_boot_init();
    assert(slideshow_load_config() == ESP_OK);
    slideshow_snapshot_t snapshot; slideshow_snapshot(&snapshot);
    assert(!snapshot.enabled && snapshot.interval_min == 60 && !snapshot.tap_wake);
    assert(slideshow_start() == ESP_OK);
    assert_disabled_is_idle();
    assert_disabled_is_idle(); /* no work after any length of disabled time */
    enable_while_waiting = true; tick();
    assert(last_wait == 250 && s_policy.enabled); /* setting arrives just before indefinite wait */
    assert(slideshow_set(false, 60) == ESP_OK); assert_disabled_is_idle();
    /* Real device: largest free heap block was 8192, but the old save needed
     * 8194 contiguous bytes. Verify actual file persistence under that bound. */
    allocations = 0;
    assert(slideshow_set(true, 20) == ESP_OK && allocations == 2);
    assert(slideshow_load_config() == ESP_OK);
    slideshow_snapshot(&snapshot); assert(snapshot.enabled && snapshot.interval_min == 20);
    for (int failure = 1; failure <= 2; failure++) {
        allocations = 0; allocation_failure = failure;
        assert(slideshow_set(false, 60) == ESP_ERR_NO_MEM);
        slideshow_snapshot(&snapshot); assert(snapshot.enabled && snapshot.interval_min == 20);
    }
    allocation_failure = 0;
    assert(slideshow_load_config() == ESP_OK);
    slideshow_snapshot(&snapshot); assert(snapshot.enabled && snapshot.interval_min == 20);
    assert(slideshow_set(false, 60) == ESP_OK);
    char previous[64];
    int checks_before = usb_checks;
    assert(!slideshow_boot_name(previous, sizeof(previous)) && !slideshow_due());
    assert(usb_checks == checks_before);
    write_cfg("# preserve\r\nmono_fast=0\r\nslideshow_enabled=1 # on\r\nslideshow_interval_min=30\r\n");
    assert(slideshow_load_config() == ESP_OK);
    slideshow_snapshot(&snapshot); assert(snapshot.enabled && snapshot.interval_min == 30);
    assert(slideshow_set(true, 0) == ESP_ERR_INVALID_ARG);
    io_fault = true;
    assert(slideshow_set(false, 10) != ESP_OK);
    slideshow_snapshot(&snapshot); assert(snapshot.enabled && snapshot.interval_min == 30);
    char text[512]; read_cfg(text, sizeof(text));
    assert(strstr(text, "slideshow_enabled=1 # on") && strstr(text, "slideshow_interval_min=30"));
    io_fault = false;
    int notifications_before = notify_count;
    checks_before = usb_checks;
    assert(slideshow_set(false, 10) == ESP_OK);
    assert(notify_count == notifications_before + 1 && usb_checks == checks_before);
    assert_disabled_is_idle();
    read_cfg(text, sizeof(text)); assert(strstr(text, "# preserve\r\nmono_fast=0\r\n"));
    assert(strstr(text, "slideshow_enabled=0 # on") && strstr(text, "slideshow_interval_min=10"));
    assert(slideshow_load_config() == ESP_OK); slideshow_snapshot(&snapshot);
    assert(!snapshot.enabled && snapshot.interval_min == 10); /* GUI cannot be undone by an old file */
    unlink(cfg); assert(slideshow_load_config() == ESP_OK); slideshow_snapshot(&snapshot);
    assert(!snapshot.enabled && snapshot.interval_min == 10); /* NVS fallback */
    assert(slideshow_set(true, 2) == ESP_OK);
    assert(slideshow_start() == ESP_OK);
    tick(); assert(last_wait == 250 && !sleeps && !auto_nexts);
    /* Disable an active task, leave it off past its old deadline, and resume
     * it without rebooting. A failed write must not wake/change it. */
    checks_before = usb_checks;
    assert(slideshow_set(false, 2) == ESP_OK && usb_checks == checks_before);
    now_us = 999000000;
    assert_disabled_is_idle();
    io_fault = true; notifications_before = notify_count;
    assert(slideshow_set(true, 2) != ESP_OK && notify_count == notifications_before);
    assert_disabled_is_idle();
    io_fault = false;
    assert(slideshow_set(true, 2) == ESP_OK);
    tick(); assert(last_wait == 250 && !sleeps && !auto_nexts);
    now_us = 0; assert(slideshow_set(true, 2) == ESP_OK);
    now_us = 60000000;
    assert(runtime_io_begin()); tick(); assert(!sleeps); runtime_io_end();
    assert(runtime_command_begin()); tick(); assert(!sleeps); runtime_command_end();
    assert(runtime_session_begin(now_us / 1000)); tick(); assert(!sleeps); runtime_session_end();
    const int boot_releases_before = boot_releases;
    for (fail_stage = 1; fail_stage <= 4; fail_stage++) {
        assert(slideshow_set(true, 2) == ESP_OK);
        now_us += 60000000;
        tick();
        runtime_snapshot_t v; runtime_snapshot(&v);
        assert(!v.sleeping && !held && !panel_down && mounted && !sleeps && !timer_us);
        slideshow_snapshot(&snapshot); assert(!strcmp(snapshot.state, "fault"));
    }
    assert(resumes == 3 && remounts == 1);
    fail_stage = 0;
    assert(slideshow_set(true, 2) == ESP_OK);
    host = true; now_us += 999000000;
    tick(); assert(!sleeps && !auto_nexts);
    slideshow_snapshot(&snapshot); assert(!strcmp(snapshot.state, "usb_paused"));
    host = false; tick(); assert(!sleeps);
    now_us += 60000000;
    input_before_admission = true; tick(); assert(!sleeps && !held && !panel_down && mounted);
    input_before_admission = false; now_us += 60000000;
    host_during_prepare = true; tick(); assert(!sleeps && !held && !panel_down && mounted);
    host_during_prepare = false; tick(); /* observe the newly connected host */
    /* Cancelled sleeps must keep the registered SPI CS routing intact. */
    assert(boot_releases == boot_releases_before);
    host = false; tick(); /* fresh full interval on disconnect */
    now_us += 60000000; allow_sleep = true;
    tick(); assert(sleeps == 1 && timer_us == 60000000);
    assert(!runtime_command_begin()); /* admission remains closed until hardware wake */
    cause = ESP_SLEEP_WAKEUP_TIMER;
    runtime_sleep_end(); panel_down = false; mounted = true;
    slideshow_boot_init();
    assert(!held && boot_releases == boot_releases_before + 1);
    assert(!s_retained.magic && !s_retained.inverse);
    assert(slideshow_load_config() == ESP_OK);
    assert(slideshow_boot_name(previous, sizeof(previous)) && !strcmp(previous, "a.bin"));
    assert(slideshow_start() == ESP_OK);
    tick(); assert(sleeps == 2 && timer_us == 120000000); /* no 60-second timer-wake window */
    runtime_sleep_end(); panel_down = false; mounted = true;
    cause = ESP_SLEEP_WAKEUP_UNDEFINED; slideshow_boot_init();
    assert(slideshow_load_config() == ESP_OK && !slideshow_boot_name(previous, sizeof(previous)));
    s_retained.magic = SLIDE_MAGIC; s_retained.inverse = ~SLIDE_MAGIC; s_retained.minutes = 2;
    strcpy(s_retained.name, "a.bin"); cause = ESP_SLEEP_WAKEUP_TIMER; slideshow_boot_init(); host = true;
    assert(!slideshow_boot_name(previous, sizeof(previous))); host = false;
    write_cfg("slideshow_enabled=1\nslideshow_interval_min=3\n");
    assert(slideshow_load_config() == ESP_OK && !slideshow_boot_name(previous, sizeof(previous)));
    assert(wake_prepares == 0); /* off by default: never touch the IMU wake source */
    /* Broken selection pauses automatic changes without latching a permanent
     * slideshow fault; a repaired snapshot resumes without rebooting. */
    runtime_publish_slideshow(0, false, true, true);
    tick(); slideshow_snapshot(&snapshot); assert(!strcmp(snapshot.state,"fault") && !s_policy.fault);
    runtime_publish_slideshow(3, true, true, false);
    tick(); slideshow_snapshot(&snapshot); assert(strcmp(snapshot.state,"fault"));
    /* Wake option persists atomically, and legacy 5-byte writes preserve it. */
    assert(slideshow_set_options(true, 2, true) == ESP_OK);
    read_cfg(text, sizeof(text)); assert(strstr(text, "tap_wake=1"));
    io_fault = true;
    assert(slideshow_set_options(true, 2, false) != ESP_OK);
    slideshow_snapshot(&snapshot); assert(snapshot.tap_wake);
    io_fault = false;
    assert(slideshow_set(true, 2) == ESP_OK);
    unlink(cfg); assert(slideshow_load_config() == ESP_OK);
    slideshow_snapshot(&snapshot); assert(snapshot.tap_wake && snapshot.interval_min == 2);
    assert(slideshow_set_options(true, 2, true) == ESP_OK);
    now_us += 60000000;
    wake_error = ESP_ERR_NOT_FINISHED; tick();
    assert(sleeps == 2);
    assert(!wake_armed && !s_policy.fault && s_policy.awake_until_ms > now_us / 1000);
    now_us += 60000000;
    wake_error = ESP_FAIL; tick();
    assert(!wake_armed && s_policy.fault);
    wake_error = 0;
    assert(slideshow_set_options(true, 2, true) == ESP_OK);
    now_us += 60000000;
    fail_stage = 3; tick(); assert(!wake_armed && s_policy.fault);
    fail_stage = 0;
    assert(slideshow_set_options(true, 2, true) == ESP_OK);
    now_us += 60000000;
    tick(); assert(wake_armed && sleeps == 3 && s_retained.tap_wake == 1);
    runtime_sleep_end(); panel_down = false; mounted = true; wake_armed = false;
    cause = ESP_SLEEP_WAKEUP_EXT1;
    slideshow_boot_init(); assert(slideshow_load_config() == ESP_OK);
    assert(slideshow_tap_boot_name(previous, sizeof(previous)) && !strcmp(previous, "a.bin"));
    assert(!slideshow_boot_name(previous, sizeof(previous)));
    slideshow_frame_done(true);
    assert(slideshow_start() == ESP_OK);
    tick(); assert(sleeps == 3 && s_policy.awake_until_ms == now_us / 1000 + 60000);
    assert(s_policy.deadline_ms == now_us / 1000 + 120000);
    now_us += 60000000;
    tick(); assert(sleeps == 4 && timer_us == 60000000);
    runtime_sleep_end(); panel_down = false; mounted = true; wake_armed = false;
    /* Wrong GPIO and stale/reset markers must never skip checked startup. */
    wake_mask = 1ULL << 3;
    slideshow_boot_init(); assert(!slideshow_tap_boot_name(previous, sizeof(previous)));
    wake_mask = 1ULL << 4;
    slideshow_boot_init(); assert(!slideshow_tap_boot_name(previous, sizeof(previous)));
    assert(wake_prepares >= 4);
    s_retained.magic = SLIDE_MAGIC; s_retained.inverse = ~SLIDE_MAGIC;
    s_retained.tap_wake = 0;
    slideshow_boot_init(); assert(!slideshow_tap_boot_name(previous, sizeof(previous)));
    s_retained.magic = SLIDE_MAGIC; s_retained.inverse = ~SLIDE_MAGIC;
    s_retained.tap_wake = 1;
    slideshow_boot_init();
    assert(slideshow_set_options(true, 2, false) == ESP_OK);
    assert(!slideshow_tap_boot_name(previous, sizeof(previous))); /* config explicitly disables it */
    FILE *f = fopen(cfg, "wb"); assert(f); for (int i = 0; i < 5000; i++) fputc('#', f); fclose(f);
    assert(slideshow_load_config() != ESP_OK); tick(); assert(sleeps == 4);
    /* USB-only wake restores the absolute deadline even with tap wake off.
     * The five-second probe cannot reset the slideshow or redraw a frame. */
    write_cfg("slideshow_enabled=1\nslideshow_interval_min=2\ntap_wake=0\n");
    s_retained.magic=SLIDE_MAGIC; s_retained.inverse=~SLIDE_MAGIC;
    s_retained.minutes=2; s_retained.tap_wake=0;
    s_retained.due_us=esp_rtc_get_time_us()+30000000;
    strcpy(s_retained.name,"a.bin");
    usb_boot=usb_probe=usb_block=true; cause=ESP_SLEEP_WAKEUP_ULP;
    slideshow_boot_init(); assert(slideshow_load_config()==ESP_OK);
    assert(slideshow_usb_boot_name(previous,sizeof(previous)) && !strcmp(previous,"a.bin"));
    slideshow_frame_done(true); /* normal retained-image restore callback */
    assert(slideshow_start()==ESP_OK);
    assert(s_policy.deadline_ms==now_us/1000+30000);
    tick(); assert(sleeps==4 && !held && !auto_nexts);
    now_us+=5000000; usb_block=false;
    usb_prepare_error=ESP_ERR_NOT_FINISHED;
    int cancelled=usb_cancels; tick();
    assert(sleeps==4 && !s_policy.fault && usb_cancels>cancelled);
    assert(!held && !panel_down && mounted);
    usb_prepare_error=ESP_OK;
    tick(); assert(sleeps==5 && timer_us==25000000);
    runtime_sleep_end(); panel_down=false; mounted=true;
    usb_boot=usb_probe=false;
    unlink(cfg); unlink(tmp); assert(!rmdir(directory));
    return 0;
}
