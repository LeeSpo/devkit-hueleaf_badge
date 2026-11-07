#include "slideshow.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "board.h"
#include "driver/usb_serial_jtag.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "file_store.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gallery.h"
#include "hl_cfg.h"
#include "nvs.h"
#include "runtime_state.h"
#include "screen_driver.h"
#include "tf_card.h"

#define CFG_LIMIT 4096
#define CFG_TMP "/sdcard/.hueleaf.cfg.tmp"
#define SLIDE_MAGIC 0x534C4431u  /* SLD1; separate from bootloader stock flag */
static const char *TAG = "slideshow";
static RTC_DATA_ATTR struct {
    uint32_t magic, inverse, minutes;
    char name[GALLERY_NAME_MAX];
} s_retained;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static slideshow_policy_t s_policy = { .interval_min = SLIDESHOW_DEFAULT_MIN };
static slideshow_action_t s_action = SLIDE_OFF;
static bool s_loaded, s_timer_boot, s_scheduled_boot;
static uint32_t s_boot_minutes;
static char s_boot_name[GALLERY_NAME_MAX];
static TaskHandle_t s_task;

#define LOCK() portENTER_CRITICAL(&s_mux)
#define UNLOCK() portEXIT_CRITICAL(&s_mux)
static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

void slideshow_boot_init(void)
{
    board_sleep_boot_release();
    s_timer_boot = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER &&
        s_retained.magic == SLIDE_MAGIC && s_retained.inverse == ~SLIDE_MAGIC &&
        slideshow_interval_valid(s_retained.minutes) &&
        memchr(s_retained.name, 0, sizeof(s_retained.name)) && s_retained.name[0];
    if (s_timer_boot) {
        s_boot_minutes = s_retained.minutes;
        memcpy(s_boot_name, s_retained.name, sizeof(s_boot_name));
    }
    /* A subsequent watchdog/reset must never reuse an unfinished advance. */
    s_retained.magic = s_retained.inverse = 0;
}

static esp_err_t read_config(char *buf, size_t *len)
{
    *len = 0;
    FILE *f = fopen(HL_CFG_PATH, "rb");
    if (!f) return errno == ENOENT ? ESP_OK : ESP_FAIL;
    *len = fread(buf, 1, CFG_LIMIT, f);
    bool bad = ferror(f) || fgetc(f) != EOF;
    if (ferror(f)) bad = true;
    if (fclose(f) != 0) bad = true;
    buf[*len] = 0;
    return bad ? ESP_FAIL : ESP_OK;   /* never silently truncate a config */
}

static void save_nvs(bool enabled, uint32_t minutes)
{
    uint32_t packed = minutes | (enabled ? 0x80000000u : 0), old = 0;
    nvs_handle_t h;
    if (nvs_open("hlgal", NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "NVS unavailable; settings remain saved on TF"); return;
    }
    if (nvs_get_u32(h, "slide_cfg", &old) != ESP_OK || old != packed) {
        if (nvs_set_u32(h, "slide_cfg", packed) != ESP_OK || nvs_commit(h) != ESP_OK)
            ESP_LOGW(TAG, "NVS write failed; settings remain saved on TF");
    }
    nvs_close(h);
}

esp_err_t slideshow_load_config(void)
{
    bool enabled = false;
    uint32_t minutes = SLIDESHOW_DEFAULT_MIN, packed;
    nvs_handle_t h;
    if (nvs_open("hlgal", NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_u32(h, "slide_cfg", &packed) == ESP_OK &&
            slideshow_interval_valid(packed & 0x7fffffffu)) {
            enabled = (packed >> 31) != 0; minutes = packed & 0x7fffffffu;
        }
        nvs_close(h);
    }
    char *buf = malloc(CFG_LIMIT + 1);
    size_t len = 0;
    esp_err_t err = buf ? read_config(buf, &len) : ESP_ERR_NO_MEM;
    if (err == ESP_OK) {
        enabled = hl_cfg_get_bool(buf, len, "slideshow_enabled", enabled) != 0;
        minutes = (uint32_t)hl_cfg_get_int(buf, len, "slideshow_interval_min", minutes, 1, SLIDESHOW_MAX_MIN);
        save_nvs(enabled, minutes);
    }
    free(buf);
    LOCK();
    slideshow_policy_init(&s_policy, enabled, minutes, now_ms(), false);
    s_policy.fault = err != ESP_OK;
    s_loaded = true;
    UNLOCK();
    ESP_LOGI(TAG, "%s interval=%lu min%s", enabled ? "ON" : "OFF",
             (unsigned long)minutes, err == ESP_OK ? "" : " (config fault; sleep inhibited)");
    return err;
}

bool slideshow_boot_name(char *name, size_t cap)
{
    LOCK();
    bool candidate = s_timer_boot && s_loaded && s_policy.enabled && !s_policy.fault &&
                     s_policy.interval_min == s_boot_minutes;
    UNLOCK();
    bool host = candidate && usb_serial_jtag_is_connected();
    LOCK();
    s_scheduled_boot = candidate && !host;
    bool scheduled = s_scheduled_boot;
    UNLOCK();
    if (scheduled) strlcpy(name, s_boot_name, cap);
    return scheduled;
}

void slideshow_activity(void)
{
    LOCK(); if (s_loaded && s_policy.enabled) slideshow_policy_activity(&s_policy, now_ms()); UNLOCK();
}
bool slideshow_due(void)
{
    LOCK();
    bool due = s_loaded && s_policy.enabled && !s_policy.fault;
    if (due) {
        int64_t now = now_ms();
        due = now >= s_policy.awake_until_ms && now >= s_policy.deadline_ms;
    }
    UNLOCK(); return due && !usb_serial_jtag_is_connected();
}
void slideshow_frame_done(bool success)
{
    LOCK(); if (s_loaded && s_policy.enabled) slideshow_policy_rendered(&s_policy, now_ms(), success); UNLOCK();
}
void slideshow_inhibit(void)
{
    LOCK(); s_policy.fault = true; s_action = SLIDE_FAULT; UNLOCK();
}
void slideshow_snapshot(slideshow_snapshot_t *out)
{
    LOCK();
    *out = (slideshow_snapshot_t) { s_policy.enabled, s_policy.interval_min,
                                 slideshow_action_name(s_action) };
    UNLOCK();
}

esp_err_t slideshow_set(bool enabled, uint32_t minutes)
{
    if (!slideshow_interval_valid(minutes)) return ESP_ERR_INVALID_ARG;
    char *buf = malloc(2 * (CFG_LIMIT + 1));
    if (!buf) return ESP_ERR_NO_MEM;
    char *next = buf + CFG_LIMIT + 1;
    size_t len = 0;
    esp_err_t err = read_config(buf, &len);
    if (err == ESP_OK) {
        size_t n = hl_cfg_set_value(buf, len, "slideshow_enabled", enabled ? "1" : "0", next, CFG_LIMIT + 1);
        char value[16]; snprintf(value, sizeof(value), "%lu", (unsigned long)minutes);
        size_t m = n ? hl_cfg_set_value(next, n, "slideshow_interval_min", value, buf, CFG_LIMIT + 1) : 0;
        if (!m) err = ESP_FAIL;
        else {
            FILE *f = fopen(CFG_TMP, "wb");
            if (!f) err = ESP_FAIL;
            else {
                bool good = fwrite(buf, 1, m, f) == m;
                if (fflush(f) != 0) good = false;
                if (fsync(fileno(f)) != 0) good = false;
                if (fclose(f) != 0) good = false;
                if (!good || !file_store_replace(CFG_TMP, HL_CFG_PATH)) err = ESP_FAIL;
            }
            if (err != ESP_OK) unlink(CFG_TMP);
        }
    }
    free(buf);
    if (err != ESP_OK) return err;
    save_nvs(enabled, minutes);
    bool host = enabled && usb_serial_jtag_is_connected();
    LOCK();
    slideshow_policy_init(&s_policy, enabled, minutes, now_ms(), false);
    s_policy.usb = host;
    s_loaded = true;
    s_action = !enabled ? SLIDE_OFF : host ? SLIDE_USB : SLIDE_WINDOW;
    TaskHandle_t task = s_task;
    UNLOCK();
    /* A notification cannot be lost between checking enabled and blocking.
     * Disabling also interrupts the active task's timed wait immediately. */
    if (task) xTaskNotifyGive(task);
    return ESP_OK;
}

static void sleep_until(void)
{
    if (!gallery_sleep_begin()) return;   /* atomic admission, not just a snapshot */
    bool card_unmounted = false, panel_asleep = false;
    esp_err_t err = ESP_OK;
    bool host = usb_serial_jtag_is_connected();
    runtime_snapshot_t v; runtime_snapshot(&v);
    /* A manual input may have run after the task's first policy snapshot.
     * Recheck after admission freezes inputs and configuration changes. */
    LOCK();
    s_action = slideshow_policy_step(&s_policy, now_ms(), host, v.n, false);
    bool should_sleep = s_action == SLIDE_SLEEP;
    int64_t deadline_ms = s_policy.deadline_ms;
    UNLOCK();
    if (!should_sleep) goto resume;
    int64_t remaining = deadline_ms - now_ms();
    if (remaining <= 0) goto resume;
    err = esp_sleep_enable_timer_wakeup((uint64_t)remaining * 1000);
    if (err != ESP_OK) goto resume;
    panel_asleep = true;  /* recover even if only half of the sleep command was sent */
    err = screen_driver_deep_sleep();
    if (err != ESP_OK) goto resume;
    sd_lock(); err = tf_card_unmount(); sd_unlock();
    if (err != ESP_OK) goto resume;
    card_unmounted = true;
    err = board_sleep_prepare();
    if (err != ESP_OK) goto resume;
    host = usb_serial_jtag_is_connected();
    if (host) goto resume;
    remaining = deadline_ms - now_ms();
    if (remaining <= 0) goto resume;
    err = esp_sleep_enable_timer_wakeup((uint64_t)remaining * 1000);
    if (err != ESP_OK) goto resume;
    LOCK(); s_retained.minutes = s_policy.interval_min; UNLOCK();
    strlcpy(s_retained.name, v.name, sizeof(s_retained.name));
    s_retained.inverse = ~SLIDE_MAGIC; s_retained.magic = SLIDE_MAGIC;
    ESP_LOGI(TAG, "deep sleep %lld ms; next after %s", (long long)remaining, v.name);
    esp_deep_sleep_start();

resume:
    s_retained.magic = s_retained.inverse = 0;
    board_sleep_release();
    (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    if (card_unmounted) {
        sd_lock(); esp_err_t e = tf_card_mount(); sd_unlock();
        if (e != ESP_OK) err = e;
    }
    if (panel_asleep) {
        esp_err_t e = screen_driver_resume();
        if (e != ESP_OK) err = e;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sleep preparation failed: %s; staying awake", esp_err_to_name(err));
        slideshow_inhibit();
    }
    runtime_sleep_end();
}

static void slideshow_task(void *arg)
{
    (void)arg;
    for (;;) {
        LOCK();
        bool enabled = s_policy.enabled;
        if (!enabled) s_action = SLIDE_OFF;
        UNLOCK();
        if (!enabled) {
            /* No clock, USB, gallery or TF polling while the feature is off.
             * Only a saved configuration change wakes this task. */
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        runtime_snapshot_t v; runtime_snapshot(&v);
        bool host = usb_serial_jtag_is_connected();
        bool busy = !v.ready || v.display || v.storage || v.session || v.file_upload ||
                    v.sleeping || gallery_picker_open();
        LOCK();
        slideshow_action_t action = slideshow_policy_step(&s_policy, now_ms(), host, v.n, busy);
        s_action = action;
        UNLOCK();
        if (tf_card_has_fault()) {
            LOCK(); s_policy.fault = true; s_action = SLIDE_FAULT; UNLOCK();
        } else if (action == SLIDE_NEXT) (void)gallery_auto_next();
        else if (action == SLIDE_SLEEP) sleep_until();
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(250));
    }
}

esp_err_t slideshow_start(void)
{
    LOCK();
    s_policy.awake_until_ms = now_ms() + (s_scheduled_boot ? 0 : SLIDESHOW_WINDOW_MS);
    UNLOCK();
    return xTaskCreate(slideshow_task, "slideshow", 4096, NULL, 2, &s_task) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}
