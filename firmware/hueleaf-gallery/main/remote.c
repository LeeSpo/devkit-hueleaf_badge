#include "remote.h"
#include "ble_remote.h"
#include "board.h"
#include "gallery.h"
#include "runtime_state.h"
#include "slideshow.h"
#include "screen_driver.h"
#include "tf_card.h"
#include "tap.h"
#include "pin_map.h"
#include "usb_wake.h"
#include "driver/usb_serial_jtag.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_random.h"
#include "esp_rtc_time.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "mbedtls/sha256.h"
#include <stdio.h>
#include <string.h>

#define RETAIN_MAGIC 0x524D5431u
static const char *TAG = "remote";
static RTC_DATA_ATTR struct {
    uint32_t magic, inverse, version, missed, wakes, slide_minutes;
    int64_t slide_due_us, cooldown_due_us;
    uint8_t cooldown;
    char name[GALLERY_NAME_MAX];
    uint8_t digest[32];
} s_retained;
static char s_image[GALLERY_NAME_MAX];
static bool s_light, s_enabled, s_ready, s_success, s_fault, s_initializing;
static bool s_cooldown_boot, s_tx_wait;
static bool s_render_event, s_render_success;
static uint16_t s_render_id;
static uint32_t s_render_epoch;
static uint32_t s_missed, s_wakes, s_slide_minutes;
static int64_t s_slide_due_us, s_started_ms, s_cooldown_due_us;
static remote_policy_t s_policy;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_task;
static QueueHandle_t s_commands;
static esp_pm_lock_handle_t s_sleep_lock;
static uint8_t s_key[32];
static bool s_provisioned;
static uint32_t s_epoch;
typedef struct { uint32_t epoch; uint16_t len; uint8_t data[REMOTE_MESSAGE_MAX]; } command_t;
static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }
#define LOCK() portENTER_CRITICAL(&s_mux)
#define UNLOCK() portEXIT_CRITICAL(&s_mux)
static void retain_hash(uint8_t digest[32])
{
    mbedtls_sha256((const uint8_t *)&s_retained, offsetof(__typeof__(s_retained), digest), digest, 0);
}
bool remote_boot_init(bool pending)
{
    uint8_t digest[32]; retain_hash(digest);
    bool valid = !pending && esp_reset_reason() == ESP_RST_DEEPSLEEP &&
        s_retained.magic == RETAIN_MAGIC && s_retained.inverse == ~RETAIN_MAGIC &&
        s_retained.version == 2 && !memcmp(digest, s_retained.digest, 32) &&
        memchr(s_retained.name, 0, sizeof(s_retained.name));
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    bool tap = cause == ESP_SLEEP_WAKEUP_EXT1 && (esp_sleep_get_ext1_wakeup_status() & (1ULL << imu_irq));
    bool timer = cause == ESP_SLEEP_WAKEUP_TIMER;
    bool usb_boot = usb_wake_boot();
    bool slide_due = valid && timer && s_retained.slide_due_us > 0 &&
        (int64_t)esp_rtc_get_time_us() >= s_retained.slide_due_us;
    s_cooldown_boot = valid && s_retained.cooldown && timer && !slide_due;
    /* A stray/early timer is not permission to redraw the old image. */
    s_light = valid && (tap || usb_boot || (timer && !slide_due));
    if (valid && (tap || usb_boot || cause == ESP_SLEEP_WAKEUP_TIMER)) {
        strlcpy(s_image, s_retained.name, sizeof(s_image));
        s_missed = s_retained.missed; s_wakes = s_retained.wakes + 1;
        s_slide_due_us = s_retained.slide_due_us; s_slide_minutes = s_retained.slide_minutes;
        s_cooldown_due_us = usb_boot ? s_retained.cooldown_due_us : 0;
    }
    ESP_LOGI(TAG, "boot reset=%d wake=%d retained=%d light=%d slide_due=%d image=%s",
             esp_reset_reason(), cause, valid, s_light, slide_due, s_image);
    s_retained.magic = 0;
    return s_light;
}
bool remote_light_boot(void) { return s_light; }
bool remote_managed(void) { return s_enabled; }
void remote_image(char *out, size_t cap) { strlcpy(out, s_image, cap); }
void remote_frame_done(void)
{
    /* Name is captured atomically with gallery state at sleep admission. */
    if (s_enabled && s_ready) remote_activity();
}
void remote_render_done(bool success)
{
    LOCK(); s_render_event = true; s_render_success = success; UNLOCK();
    if (s_task) xTaskNotifyGive(s_task);
}
void remote_activity(void)
{
    usb_wake_activity();
    LOCK(); if (s_enabled) remote_policy_activity(&s_policy, now_ms()); UNLOCK();
    if (s_task) xTaskNotifyGive(s_task);
}
bool remote_connected(void)
{
    bool usb = usb_serial_jtag_is_connected();
    LOCK();
    /* USB deliberately keeps the board awake; each explicit phone connection
     * must remain usable after the battery discovery window or END expires. */
    if (s_enabled && usb && !s_policy.connected &&
        (s_policy.phase == REMOTE_WAIT || s_policy.phase == REMOTE_RECONNECT || s_policy.phase == REMOTE_SLEEP))
        remote_policy_start(&s_policy, s_policy.options, now_ms());
    bool ok = s_enabled && remote_policy_connect(&s_policy, now_ms());
    s_epoch++;
    UNLOCK();
    ESP_LOGI(TAG, "connect accepted=%d", ok);
    if (s_task) xTaskNotifyGive(s_task);
    return ok;
}
void remote_disconnected(void)
{
    LOCK(); remote_policy_disconnect(&s_policy, now_ms()); s_epoch++;
    remote_phase_t phase = s_policy.phase; UNLOCK();
    ESP_LOGI(TAG, "disconnected -> %s; keep image", remote_phase_name(phase));
    if (s_task) xTaskNotifyGive(s_task);
}
bool remote_authenticate(void)
{
    LOCK(); bool ok = remote_policy_auth(&s_policy, now_ms());
    if (ok) { s_success = true; s_missed = 0; if (s_ready) remote_policy_ready(&s_policy, now_ms()); }
    UNLOCK();
    ESP_LOGI(TAG, "authenticated=%d ready=%d; keep image", ok, s_ready);
    if (s_task) xTaskNotifyGive(s_task);
    return ok;
}
bool remote_submit(const uint8_t *data, size_t len)
{
    if (len > REMOTE_MESSAGE_MAX || !s_commands) return false;
    command_t cmd = {.len = len};
    LOCK(); cmd.epoch = s_epoch; bool ok = s_policy.authenticated; UNLOCK();
    memcpy(cmd.data, data, len);
    if (ok) ok = xQueueSend(s_commands, &cmd, 0) == pdTRUE;
    if (s_task) xTaskNotifyGive(s_task);
    return ok;
}
void remote_startup_failed(void)
{
    LOCK(); s_initializing = false; s_fault = true; s_policy.phase = REMOTE_FAULT; UNLOCK();
}
void remote_ready(void)
{
    LOCK(); s_ready = true; s_initializing = false;
    if (!s_light && s_policy.phase == REMOTE_WAIT) s_policy.deadline_ms = now_ms() + s_policy.options.wait_ms;
    if (s_enabled) remote_policy_ready(&s_policy, now_ms());
    UNLOCK();
    if (usb_wake_probe_only()) slideshow_resume_deadline(s_slide_due_us);
}
static esp_err_t load_config(remote_options_t *options)
{
    esp_err_t err = nvs_flash_init_partition("remote_nvs");
    if (err != ESP_OK) return err; /* Never erase credentials on NVS errors. */
    nvs_handle_t h;
    err = nvs_open_from_partition("remote_nvs", "remote", NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    size_t len = sizeof(s_key);
    s_provisioned = nvs_get_blob(h, "key", s_key, &len) == ESP_OK && len == sizeof(s_key);
    len = sizeof(*options);
    remote_options_t saved;
    if (nvs_get_blob(h, "options", &saved, &len) == ESP_OK && len == sizeof(saved) && remote_options_valid(&saved)) *options = saved;
    uint8_t enabled = 1;
    nvs_get_u8(h, "enabled", &enabled);
    s_enabled = enabled != 0;
    nvs_close(h); return ESP_OK;
}
esp_err_t remote_init(void)
{
    remote_options_t options = REMOTE_DEFAULT_OPTIONS;
    esp_err_t err = load_config(&options);
    if (err != ESP_OK) { s_fault = true; return err; }
    if (!s_enabled) return ESP_OK;
    s_enabled = false; /* Publish managed only once all prerequisites exist. */
    s_commands = xQueueCreate(1, sizeof(command_t));
    if (!s_commands) { s_fault = true; return ESP_ERR_NO_MEM; }
    err = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "remote_sleep", &s_sleep_lock);
    if (err != ESP_OK) { s_fault = true; return err; }
    esp_pm_config_t pm = {.max_freq_mhz=240, .min_freq_mhz=40, .light_sleep_enable=true};
    err = esp_pm_configure(&pm);
    if (err != ESP_OK) { s_fault = true; return err; }
    remote_policy_start(&s_policy, options, now_ms());
    s_started_ms = now_ms(); s_enabled = true;
    err = ble_remote_start(s_key, s_provisioned);
    if (err != ESP_OK) { s_enabled = false; s_fault = true; return err; }
    s_initializing = !s_light || usb_wake_boot();
    if (s_cooldown_boot) s_policy.phase = REMOTE_SLEEP;
    err = remote_start();
    if (err != ESP_OK) {
        ble_remote_stop(); s_enabled = false; s_fault = true;
    }
    return err;
}
/* The retained image stays physically visible. No panel/TF access on lightweight sleep. */
static bool enter_sleep(void)
{
    if (usb_wake_blocked()) return false;
    bool admitted = gallery_is_ready() ? gallery_sleep_begin() : runtime_remote_sleep_begin();
    if (!admitted) return false;
    LOCK();
    bool connected = s_policy.connected;
    bool due = s_policy.phase == REMOTE_SLEEP && !s_initializing;
    UNLOCK();
    if (!due || usb_serial_jtag_is_connected() || ble_remote_pending()) { runtime_sleep_end(); return false; }
    /* Tickless idle also programs TIMER. Hold the PM lock throughout all
     * blocking preparations and set the final timer only after LP handoff. */
    esp_err_t err = esp_pm_lock_acquire(s_sleep_lock);
    if (err != ESP_OK) { runtime_sleep_end(); return false; }
    bool probe = usb_wake_probe_only(), ble_stopped = false, retry = false;
    uint32_t missed_before = s_missed;
    runtime_snapshot_t snapshot; runtime_snapshot(&snapshot);
    if (gallery_is_ready()) {
        strlcpy(s_image, snapshot.name, sizeof(s_image));
        int64_t remaining = slideshow_remaining_ms();
        /* A slide may become due during USB identification. Preserve it as
         * an immediate timer wake instead of confusing it with "disabled". */
        if (remaining < 0 && slideshow_due()) remaining = 0;
        s_slide_due_us = remaining >= 0 ? (int64_t)esp_rtc_get_time_us() + remaining * 1000 : 0;
    }
    int64_t rtc_now = esp_rtc_get_time_us();
    bool resume_cooldown = probe && s_cooldown_due_us > rtc_now;
    bool cooldown = resume_cooldown || (!probe && !s_success && !s_cooldown_boot &&
        s_policy.options.cooldown_ms && s_missed + 1 >= s_policy.options.cooldown_after);
    int64_t cool_due = resume_cooldown ? s_cooldown_due_us :
        rtc_now + (int64_t)s_policy.options.cooldown_ms * 1000;
    if (cooldown && s_slide_due_us && s_slide_due_us <= cool_due) cooldown = false;
    if (!cooldown) {
        err = tap_wake_prepare();
        if (err == ESP_ERR_NOT_FINISHED) {
            esp_pm_lock_release(s_sleep_lock); runtime_sleep_end(); return false;
        }
        if (err != ESP_OK) goto fail;
    }
    if (connected) ble_remote_disconnect();
    err = ble_remote_stop(); if (err != ESP_OK) goto fail;
    ble_stopped = true;
    if (gallery_is_ready()) {
        if (screen_driver_initialized()) { err = screen_driver_deep_sleep(); if (err != ESP_OK) goto fail; }
        sd_lock(); err = tf_card_unmount(); sd_unlock(); if (err != ESP_OK) goto fail;
    }
    err = board_sleep_prepare(); if (err != ESP_OK) goto fail;
    if (usb_serial_jtag_is_connected()) { err = ESP_ERR_NOT_FINISHED; goto fail; }
    err = usb_wake_prepare(); if (err != ESP_OK) goto fail;
    int64_t target_us = cooldown ? cool_due : s_slide_due_us;
    int64_t remaining_us = target_us ? target_us - (int64_t)esp_rtc_get_time_us() : -1;
    if (target_us && remaining_us < 1000) remaining_us = 1000;
    err = esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) goto fail;
    if (remaining_us > 0) {
        err = esp_sleep_enable_timer_wakeup(remaining_us); if (err != ESP_OK) goto fail;
    }
    slideshow_snapshot_t slide; slideshow_snapshot(&slide);
    if (gallery_is_ready()) s_slide_minutes = slide.interval_min;
    slideshow_retain_remote(s_image, s_slide_minutes, remaining_us > 0 && !cooldown);
    if (!s_success && !s_cooldown_boot && !probe) s_missed++;
    memset(&s_retained, 0, sizeof(s_retained));
    s_retained.magic = RETAIN_MAGIC; s_retained.inverse = ~RETAIN_MAGIC; s_retained.version = 2;
    s_retained.missed = cooldown ? 0 : s_missed; s_retained.wakes = s_wakes;
    s_retained.slide_minutes = s_slide_minutes;
    s_retained.slide_due_us = s_slide_due_us; s_retained.cooldown = cooldown;
    s_retained.cooldown_due_us = cooldown ? cool_due : 0;
    strlcpy(s_retained.name, s_image, sizeof(s_retained.name)); retain_hash(s_retained.digest);
    ESP_LOGI(TAG, "deep sleep timer_us=%lld missed=%lu cooldown=%d image=%s; keep image",
             (long long)remaining_us, (unsigned long)s_missed, cooldown, s_image);
    esp_deep_sleep_start();
fail:
    /* A cable arriving during preparation is a normal retry. Restore every
     * resource first; a failed restore still latches diagnostic mode. */
    retry = err == ESP_ERR_NOT_FINISHED;
    usb_wake_cancel();
    board_sleep_release(); tap_wake_release();
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    if (gallery_is_ready()) {
        sd_lock(); esp_err_t mount = tf_card_is_mounted() ? ESP_OK : tf_card_mount(); sd_unlock();
        if (mount != ESP_OK) { err = mount; retry = false; }
        if (screen_driver_initialized()) {
            esp_err_t resume = screen_driver_resume();
            if (resume != ESP_OK) { err = resume; retry = false; }
        }
    }
    if (retry && ble_stopped) {
        esp_err_t restart = ble_remote_start(s_key, s_provisioned);
        if (restart != ESP_OK) { err = restart; retry = false; }
    }
    s_missed = missed_before;
    s_retained.magic = 0;
    runtime_sleep_end();
    esp_pm_lock_release(s_sleep_lock);
    if (!retry) {
        LOCK(); s_policy.phase = REMOTE_FAULT; s_fault = true; UNLOCK();
        ESP_LOGE(TAG, "sleep preparation failed: %s; remain awake", esp_err_to_name(err));
    }
    return false;
}
void remote_snapshot(char *out, size_t cap)
{
    LOCK(); remote_policy_t policy = s_policy; UNLOCK();
    snprintf(out, cap, "{\"layout\":\"remote-v1\",\"state\":\"%s\",\"provisioned\":%s,\"connected\":%s,"
        "\"authenticated\":%s,\"ready\":%s,\"fault\":%s,\"missed\":%lu,\"wakes\":%lu,"
        "\"wait_ms\":%lu,\"idle_ms\":%lu,\"reconnect_ms\":%lu,\"cooldown_ms\":%lu,\"cooldown_after\":%u,"
        "\"heap_free\":%u,\"heap_largest\":%u,\"remote_stack_min\":%u}",
        remote_phase_name(policy.phase), s_provisioned?"true":"false", policy.connected?"true":"false",
        policy.authenticated?"true":"false", gallery_is_ready()?"true":"false", s_fault?"true":"false",
        (unsigned long)s_missed, (unsigned long)s_wakes, (unsigned long)policy.options.wait_ms,
        (unsigned long)policy.options.idle_ms, (unsigned long)policy.options.reconnect_ms,
        (unsigned long)policy.options.cooldown_ms, policy.options.cooldown_after,
        (unsigned)esp_get_free_heap_size(), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
        s_task ? (unsigned)uxTaskGetStackHighWaterMark(s_task) : 0);
}
/* USB only. 0 query; 1 rotate key (32B), 2 revoke, 3 options (4*u32 + u8). Reboot applies. */
esp_err_t remote_configure(const uint8_t *data, size_t len)
{
    if (!len || data[0] == 0) return len <= 1 ? ESP_OK : ESP_ERR_INVALID_ARG;
    if (!usb_serial_jtag_is_connected()) return ESP_ERR_INVALID_STATE;
    nvs_handle_t h; esp_err_t err = nvs_open_from_partition("remote_nvs", "remote", NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    if (data[0] == 1 && len == 33) err = nvs_set_blob(h, "key", data + 1, 32);
    else if (data[0] == 2 && len == 1) {
        err = nvs_erase_key(h, "key"); if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    } else if (data[0] == 3 && len == 18) {
        remote_options_t o = {0};
        memcpy(&o.wait_ms, data + 1, 4); memcpy(&o.idle_ms, data + 5, 4);
        memcpy(&o.reconnect_ms, data + 9, 4); memcpy(&o.cooldown_ms, data + 13, 4); o.cooldown_after = data[17];
        err = remote_options_valid(&o) ? nvs_set_blob(h, "options", &o, sizeof(o)) : ESP_ERR_INVALID_ARG;
    } else if (data[0] == 4 && len == 2 && data[1] <= 1) err = nvs_set_u8(h, "enabled", data[1]);
    else err = ESP_ERR_INVALID_ARG;
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h); return err;
}
static uint16_t get16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }
static void json_name(char *out, size_t cap, const char *name)
{
    size_t pos = 0;
    for (const unsigned char *p = (const unsigned char *)name; *p && pos + 7 < cap; p++) {
        if (*p == '"' || *p == '\\') { out[pos++] = '\\'; out[pos++] = *p; }
        else if (*p < 32) pos += snprintf(out + pos, cap - pos, "\\u%04x", *p);
        else out[pos++] = *p;
    }
    out[pos] = 0;
}
static void execute(const command_t *cmd)
{
    static uint8_t cached_request[REMOTE_MESSAGE_MAX], cached_response[REMOTE_MESSAGE_MAX-6];
    static size_t request_len, response_len;
    static uint32_t epoch;
    static int last_status;
    uint8_t response[REMOTE_MESSAGE_MAX-6]; size_t len = 0;
    uint8_t op = cmd->data[1]; uint16_t id = get16(cmd->data+2);
    LOCK(); bool valid = cmd->epoch == s_epoch && s_policy.authenticated; UNLOCK();
    if (!valid) return;
    if (epoch == cmd->epoch && request_len >= 4 && id == get16(cached_request+2)) {
        if (request_len == cmd->len && !memcmp(cached_request, cmd->data, request_len))
            ble_remote_reply(id, op, last_status, cached_response, response_len);
        else ble_remote_reply(id, op, -5, NULL, 0);
        return;
    }
    int status = 0;
    if (!runtime_command_begin()) status = -9;
    else {
        runtime_snapshot_t snap; runtime_snapshot(&snap);
        if (op == REMOTE_STATUS && cmd->len == 4) {
            char text[506], escaped[GALLERY_NAME_MAX * 6];
            json_name(escaped, sizeof(escaped), snap.name);
            snprintf(text, sizeof(text), "{\"ready\":%s,\"state\":\"%s\",\"battery\":%d,\"n\":%d,\"cur\":%d,\"name\":\"%s\"}",
                gallery_is_ready()?"true":"false", runtime_state_name(&snap, tf_card_has_fault()), snap.battery, snap.n, snap.cur, escaped);
            len = strlen(text); memcpy(response, text, len);
        } else if (op == REMOTE_END && cmd->len == 4) {
            LOCK(); remote_policy_end(&s_policy); UNLOCK();
        } else if (!gallery_is_ready()) status = -8;
        else if ((op == REMOTE_LIST || op == REMOTE_LIST_LABELS) && cmd->len == 14) {
            if (!runtime_storage_begin(true)) status = -9;
            else {
                uint64_t token; memcpy(&token, cmd->data + 6, 8);
                esp_err_t err = op == REMOTE_LIST_LABELS
                    ? gallery_selection_labels_read(get16(cmd->data+4), token, response, sizeof(response), &len)
                    : gallery_selection_read(get16(cmd->data+4), token, response, sizeof(response), &len);
                status = err == ESP_OK ? 0 : err == ESP_ERR_INVALID_STATE ? -11 : -5;
                runtime_storage_end();
            }
        } else if (op == REMOTE_NAV || op == REMOTE_SHOW) {
            if (snap.display || snap.storage || snap.session || snap.file_upload || s_tx_wait) status = -9;
            else if (op == REMOTE_NAV && cmd->len == 5 && (cmd->data[4] == 1 || cmd->data[4] == 255)) {
                LOCK(); s_render_event = false; s_render_id = id; s_render_epoch = cmd->epoch; UNLOCK();
                esp_err_t err = gallery_remote_request((int8_t)cmd->data[4], NULL);
                status = err == ESP_OK ? 0 : err == ESP_ERR_NOT_FOUND ? -4 : -9; s_tx_wait = status == 0;
            } else if (op == REMOTE_SHOW && cmd->len > 4 && cmd->len < 4+GALLERY_NAME_MAX && !memchr(cmd->data+4, 0, cmd->len-4)) {
                char name[GALLERY_NAME_MAX]; memcpy(name, cmd->data+4, cmd->len-4); name[cmd->len-4]=0;
                LOCK(); s_render_event = false; s_render_id = id; s_render_epoch = cmd->epoch; UNLOCK();
                esp_err_t err = gallery_remote_request(0, name);
                status = err == ESP_OK ? 0 : err == ESP_ERR_NOT_FOUND ? -4 : -9; s_tx_wait = status == 0;
            } else status = -5;
        } else status = -3;
        if (status == 0) remote_activity();
        runtime_command_end();
    }
    epoch = cmd->epoch; request_len = cmd->len; memcpy(cached_request, cmd->data, request_len);
    last_status = status; response_len = len; memcpy(cached_response, response, len);
    ble_remote_reply(id, op, status, response, len);
}
static bool tick(void)
{
    if (!s_enabled || s_fault) return false;
    if (now_ms() - s_started_ms >= 3000) ble_remote_slow();
    usb_wake_poll();
    bool blocked = usb_wake_blocked();
    bool probe = usb_wake_probe_only();
    runtime_snapshot_t snap; runtime_snapshot(&snap);
    LOCK(); bool initializing = s_initializing; UNLOCK();
    bool busy = blocked || initializing || snap.slideshow_fault || tf_card_has_fault() || snap.display || snap.storage || snap.session || snap.file_upload || s_tx_wait || runtime_remote_operation_pending() || ble_remote_pending();
    LOCK();
    if (probe && !blocked && !s_policy.connected) s_policy.phase = REMOTE_SLEEP;
    if (!s_policy.connected && s_slide_due_us && (int64_t)esp_rtc_get_time_us() >= s_slide_due_us)
        s_policy.phase = REMOTE_SLEEP;
    bool due = remote_policy_step(&s_policy, now_ms(), busy, usb_serial_jtag_is_connected());
    UNLOCK();
    if (due) return enter_sleep();
    return false;
}
esp_err_t remote_wait(void)
{
    if (!s_enabled) return ESP_OK;

    for (;;) {
        LOCK(); bool authenticated = s_policy.authenticated; UNLOCK();
        if (authenticated || usb_wake_boot() || usb_serial_jtag_is_connected()) {
            /* Reserve against a concurrent sleep preparation before main
             * starts touching buses. Fault rollback releases this guard. */
            if (runtime_io_begin()) {
                LOCK(); s_initializing = true; UNLOCK();
                runtime_io_end();
                return ESP_OK;
            }
        }
        if (s_fault) return ESP_OK; /* full startup provides USB diagnosis */
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
static void task(void *arg)
{
    (void)arg;
    command_t cmd;
    for (;;) {
        if (!ble_remote_pending() && xQueueReceive(s_commands, &cmd, 0) == pdTRUE) execute(&cmd);
        if (s_tx_wait && !ble_remote_pending()) {
            LOCK(); bool done = s_render_event; bool success = s_render_success;
            uint16_t id = s_render_id; bool same = s_render_epoch == s_epoch && s_policy.authenticated;
            if (done) s_render_event = false;
            UNLOCK();
            if (done) {
                s_tx_wait = false;
                if (same) ble_remote_reply(id, 0x40, success ? 0 : -7, NULL, 0);
            }
        }
        tick();
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(200));
    }
}
esp_err_t remote_start(void)
{
    if (!s_enabled || s_task) return ESP_OK;
    return xTaskCreate(task, "remote", 6144, NULL, 3, &s_task) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
