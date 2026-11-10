#include "usb_wake.h"
#include "usb_wake_shared.h"
#include "ulp_usb_watch.h"
#include "ulp_lp_core.h"
#include "lp_core_i2c.h"
#include "axp2101.h"
#include "board.h"
#include "pin_map.h"
#include "runtime_state.h"
#include "driver/rtc_io.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_private/periph_ctrl.h"
#include "hal/i2c_ll.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#define PROBE_MS 5000
extern const uint8_t usb_watch_bin_start[] asm("_binary_ulp_usb_watch_bin_start");
extern const uint8_t usb_watch_bin_end[] asm("_binary_ulp_usb_watch_bin_end");
static const char *TAG = "usb_wake";
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_boot, s_probe, s_fault, s_known, s_present, s_handoff;
static int64_t s_until_ms, s_poll_ms;
#ifdef CONFIG_PM_ENABLE
static esp_pm_lock_handle_t s_pm_lock;
static bool s_pm_held;
#endif
#define LOCK() portENTER_CRITICAL(&s_mux)
#define UNLOCK() portEXIT_CRITICAL(&s_mux)
static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }
static volatile usb_watch_t *watch(void) { return (volatile usb_watch_t *)&ulp_watch; }
static void stop_watcher(void)
{
    ulp_lp_core_stop();
    /* Also terminate any outstanding LP controller command before the HP
     * driver takes the shared lines. This never writes a PMIC register. */
    PERIPH_RCC_ATOMIC() {
        lp_i2c_ll_reset_register(0);
        lp_i2c_ll_enable_bus_clock(0, false);
    }
}

void usb_wake_boot_init(void)
{
    /* Stop on every reset, not only ULP wakes. LP code must never access the
     * shared pins while the HP I2C driver is being initialized. */
    stop_watcher();
    bool armed = esp_reset_reason() == ESP_RST_DEEPSLEEP && ulp_watch_magic == USB_WATCH_MAGIC;
    bool valid = armed && esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_ULP;
    uint32_t event = valid ? watch()->event : 0;
    s_boot = event == USB_WATCH_INSERTED || event == USB_WATCH_FAULT;
    s_probe = s_boot && event == USB_WATCH_INSERTED;
    s_fault = s_boot && event == USB_WATCH_FAULT;
    /* Timer/tap wakes also inherit the last sample. An already connected
     * charger is not a new insertion and must not open another probe window. */
    s_known = armed; s_present = armed && watch()->present;
    ulp_watch_magic = 0;
    /* Release RTC routing before the normal HP driver configures GPIO2/3. */
    rtc_gpio_deinit(i2c0_scl);
    rtc_gpio_deinit(i2c0_sda);
    if (s_boot) ESP_LOGI(TAG, "wake event=%lu; keep image", (unsigned long)event);
    if (s_fault) ESP_LOGE(TAG, "LP I2C failed three times; remain awake for diagnosis");
}
bool usb_wake_boot(void) { return s_boot; }
void usb_wake_ready(void)
{
    LOCK(); if (s_probe) s_until_ms = now_ms() + PROBE_MS; UNLOCK();
}
void usb_wake_activity(void)
{
    LOCK(); s_probe = false; UNLOCK();
}
bool usb_wake_probe_only(void)
{
    bool host = usb_serial_jtag_is_connected();
    LOCK(); if (host) s_probe = false; bool probe = s_probe; UNLOCK();
    return probe;
}
static void observe(bool present)
{
    LOCK();
    if ((!s_known || !s_present) && present) s_until_ms = now_ms() + PROBE_MS;
    s_known = true; s_present = present;
    UNLOCK();
}
void usb_wake_poll(void)
{
    int64_t now = now_ms();
    LOCK(); bool due = now >= s_poll_ms; if (due) s_poll_ms = now + 500; UNLOCK();
    if (!due || !runtime_io_begin()) return;
    bool present;
    esp_err_t err = axp2101_vbus_present(&present);
    runtime_io_end();
    if (err == ESP_OK) observe(present);
    /* A failed read cannot establish that a newly inserted host is absent.
     * Final sleep preparation retries it and reports a persistent error. */
}
bool usb_wake_blocked(void)
{
    LOCK(); bool blocked = s_fault || (s_probe && !s_until_ms) || now_ms() < s_until_ms; UNLOCK();
    return blocked;
}
void usb_wake_cancel(void)
{
    if (s_handoff) {
        stop_watcher();
        ulp_watch_magic = 0;
        (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ULP);
        board_i2c_restore();
        s_handoff = false;
        board_i2c_unlock();
    }
#ifdef CONFIG_PM_ENABLE
    if (s_pm_held) { esp_pm_lock_release(s_pm_lock); s_pm_held = false; }
#endif
}
esp_err_t usb_wake_prepare(void)
{
    bool present;
    esp_err_t err = axp2101_vbus_present(&present);
    if (err != ESP_OK) return err;
    observe(present);
    if (usb_serial_jtag_is_connected() || usb_wake_blocked()) return ESP_ERR_NOT_FINISHED;
#ifdef CONFIG_PM_ENABLE
    if (!s_pm_lock) {
        err = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "usb_watch", &s_pm_lock);
        if (err != ESP_OK) return err;
    }
    err = esp_pm_lock_acquire(s_pm_lock);
    if (err != ESP_OK) return err;
    s_pm_held = true;
#endif
    board_i2c_lock();
    s_handoff = true; /* restore even if LP init only partially changes pins */
    err = ulp_lp_core_load_binary(usb_watch_bin_start, usb_watch_bin_end - usb_watch_bin_start);
    if (err != ESP_OK) goto fail;
    watch()->present = present; watch()->errors = 0; watch()->event = 0;
    ulp_watch_magic = USB_WATCH_MAGIC;
    const lp_core_i2c_cfg_t i2c = LP_CORE_I2C_DEFAULT_CONFIG();
    err = lp_core_i2c_master_init(LP_I2C_NUM_0, &i2c);
    if (err != ESP_OK) goto fail;
    err = esp_sleep_enable_ulp_wakeup();
    if (err != ESP_OK) goto fail;
    ulp_lp_core_cfg_t cfg = {
        .wakeup_source = ULP_LP_CORE_WAKEUP_SOURCE_LP_TIMER,
        .lp_timer_sleep_duration_us = USB_WATCH_PERIOD_US,
    };
    err = ulp_lp_core_run(&cfg);
    if (err != ESP_OK) goto fail;
    /* A pre-sleep insertion stays latched; LP repeats the wake request until
     * boot/cancel stops it. No HP I2C calls are allowed after this point. */
    return ESP_OK;
fail:
    usb_wake_cancel();
    ESP_LOGE(TAG, "prepare failed: %s", esp_err_to_name(err));
    return err;
}
