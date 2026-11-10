/* Execute the real HP watcher lifecycle and inject every handoff failure. */
#include <assert.h>
#include <stdio.h>
#include "../main/usb_wake.c"
uint32_t ulp_watch[3], ulp_watch_magic;
const uint8_t usb_watch_bin_start[]={0}, usb_watch_bin_end[]={0};
static int64_t time_us;
static bool supply, host, locked, lp_running, rtc_pins, armed;
static int reset=ESP_RST_POWERON, fail, stops, restores;
static esp_sleep_wakeup_cause_t cause;
int64_t esp_timer_get_time(void) { return time_us; }
int esp_reset_reason(void) { return reset; }
esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause(void) { return cause; }
bool usb_serial_jtag_is_connected(void) { return host; }
const char *esp_err_to_name(esp_err_t e) { (void)e; return "injected"; }
esp_err_t rtc_gpio_deinit(int pin) { assert(!lp_running && (pin==2 || pin==3)); rtc_pins=false; return ESP_OK; }
void board_i2c_lock(void) { assert(!locked); locked=true; }
void board_i2c_unlock(void) { assert(locked); locked=false; }
void board_i2c_restore(void) { assert(locked && !lp_running); rtc_pins=false; restores++; }
esp_err_t axp2101_vbus_present(bool *p) { assert(!locked && !lp_running && !rtc_pins); *p=supply; return fail==1 ? ESP_FAIL : ESP_OK; }
void ulp_lp_core_stop(void) { lp_running=false; stops++; }
void lp_i2c_ll_reset_register(int p) { assert(p==0 && !lp_running); }
void lp_i2c_ll_enable_bus_clock(int p, bool enabled) { assert(p==0 && !enabled && !lp_running); }
esp_err_t ulp_lp_core_load_binary(const uint8_t *p, size_t n)
{ (void)p;(void)n; assert(locked); ulp_lp_core_stop(); ulp_watch_magic=0; return fail==2 ? ESP_FAIL : ESP_OK; }
esp_err_t lp_core_i2c_master_init(int p,const lp_core_i2c_cfg_t *c)
{ (void)p;(void)c; assert(locked); rtc_pins=true; return fail==3 ? ESP_FAIL : ESP_OK; }
esp_err_t esp_sleep_enable_ulp_wakeup(void) { armed=true; return fail==4 ? ESP_FAIL : ESP_OK; }
esp_err_t esp_sleep_disable_wakeup_source(esp_sleep_wakeup_cause_t c)
{ assert(c==ESP_SLEEP_WAKEUP_ULP); armed=false; return ESP_OK; }
esp_err_t ulp_lp_core_run(ulp_lp_core_cfg_t *c)
{ assert(locked && armed && c->lp_timer_sleep_duration_us==500000); lp_running=true; return fail==5 ? ESP_FAIL : ESP_OK; }
esp_err_t esp_pm_lock_create(int t,int n,const char *name,esp_pm_lock_handle_t *p)
{ (void)n;(void)name; assert(t==ESP_PM_NO_LIGHT_SLEEP); *p=(void*)1; return ESP_OK; }
esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t p) { assert(p && !s_pm_held); return ESP_OK; }
esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t p) { assert(p && s_pm_held); return ESP_OK; }
static void reboot(uint32_t event)
{
    s_boot=s_probe=s_fault=s_known=s_present=s_handoff=false;
    s_until_ms=s_poll_ms=0; s_pm_held=false; locked=false;
    ulp_watch_magic=USB_WATCH_MAGIC; watch()->present=1; watch()->event=event;
    reset=ESP_RST_DEEPSLEEP; cause=ESP_SLEEP_WAKEUP_ULP;
    lp_running=rtc_pins=true; usb_wake_boot_init();
    assert(!lp_running && !rtc_pins && ulp_watch_magic==0);
}
int main(void)
{
    runtime_set_ready(true); usb_wake_boot_init();
    assert(!usb_wake_boot() && !usb_wake_blocked());
    usb_wake_poll(); assert(usb_wake_prepare()==ESP_OK && lp_running && locked && s_pm_held);
    int before=restores; usb_wake_cancel();
    assert(restores==before+1 && !lp_running && !locked && !armed && !s_pm_held);
    usb_wake_cancel(); assert(restores==before+1); /* idempotent rollback */
    supply=true; time_us+=500000; usb_wake_poll(); assert(usb_wake_blocked());
    assert(usb_wake_prepare()==ESP_ERR_NOT_FINISHED && !locked);
    time_us+=5000000; assert(!usb_wake_blocked());
    assert(usb_wake_prepare()==ESP_OK && watch()->present==1); usb_wake_cancel();
    /* Same charging supply can reenter sleep immediately, without another probe. */
    assert(usb_wake_prepare()==ESP_OK); usb_wake_cancel();
    for(fail=1;fail<=5;fail++) {
        assert(usb_wake_prepare()!=ESP_OK);
        assert(!locked && !lp_running && !rtc_pins && !s_pm_held && !armed);
    }
    fail=0;
    reboot(USB_WATCH_INSERTED); assert(usb_wake_boot() && usb_wake_probe_only() && usb_wake_blocked());
    time_us+=3000000; usb_wake_ready(); /* enumeration grace starts after startup */
    time_us+=4999000; assert(usb_wake_blocked());
    time_us+=1000; assert(!usb_wake_blocked() && usb_wake_probe_only());
    supply=true; assert(usb_wake_prepare()==ESP_OK); usb_wake_cancel();
    host=true; assert(!usb_wake_probe_only());
    assert(usb_wake_prepare()==ESP_ERR_NOT_FINISHED); host=false;
    /* A scheduled wake on a persistent charger must immediately re-sleep,
     * rather than treating the existing supply as another cable insertion. */
    s_until_ms=s_poll_ms=0; supply=true;
    ulp_watch_magic=USB_WATCH_MAGIC; watch()->present=1; watch()->event=0;
    cause=ESP_SLEEP_WAKEUP_TIMER; usb_wake_boot_init();
    assert(!usb_wake_boot()); usb_wake_poll(); assert(!usb_wake_blocked());
    assert(usb_wake_prepare()==ESP_OK); usb_wake_cancel();
    reboot(USB_WATCH_INSERTED); usb_wake_ready(); usb_wake_activity(); assert(!usb_wake_probe_only());
    reboot(USB_WATCH_FAULT); assert(usb_wake_boot() && usb_wake_blocked() && !usb_wake_probe_only());
    reset=ESP_RST_TASK_WDT; usb_wake_boot_init(); assert(!usb_wake_boot());
    assert(stops>0);
    puts("OK actual USB handoff: probe deadline, persistent charger, host/activity, all failures, early LP stop");
    return 0;
}
