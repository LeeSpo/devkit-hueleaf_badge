#include "gallery.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "axp2101.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "frame.h"
#include "nvs.h"
#include "pcf8563.h"
#include "screen_driver.h"

static const char *TAG = "gallery";
#define NVS_NS "hlgal"            /* own namespace inside the shared stock NVS */

static char s_names[GALLERY_MAX][GALLERY_NAME_MAX];
static int s_n, s_cur = -1;
static bool s_strip = true;
static uint8_t s_frame[SCREEN_FRAME_BYTES];
static SemaphoreHandle_t s_sd, s_state;
static TaskHandle_t s_task;
static int s_pending;                       /* accumulated nav delta */
static char s_request[GALLERY_NAME_MAX];    /* explicit show(name) */
static bool s_force;                         /* re-render even if same index */
static char s_last_time[24] = "--:--";

void sd_lock(void) { xSemaphoreTakeRecursive(s_sd, portMAX_DELAY); }
void sd_unlock(void) { xSemaphoreGiveRecursive(s_sd); }

static int cmp_name(const void *a, const void *b) { return strcmp(a, b); }

static bool valid_entry(const char *n)
{
    size_t l = strlen(n);
    if (l < 5 || l >= GALLERY_NAME_MAX || n[0] == '.' || strcasecmp(n + l - 4, ".bin")) return false;
    char path[GALLERY_NAME_MAX + 16];
    struct stat st;
    snprintf(path, sizeof(path), GALLERY_DIR "/%s", n);
    return stat(path, &st) == 0 && st.st_size == SCREEN_FRAME_BYTES;
}

static void nvs_save(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (s_cur >= 0 && s_cur < s_n) nvs_set_str(h, "cur", s_names[s_cur]);
    nvs_set_i32(h, "curi", s_cur);
    nvs_set_u8(h, "strip", s_strip);
    nvs_commit(h);
    nvs_close(h);
}

/* Optional index.txt (one file name per line) sets the order; files not
 * listed follow alphabetically. Unknown/invalid lines are skipped. */
int gallery_rescan(void)
{
    sd_lock();
    xSemaphoreTake(s_state, portMAX_DELAY);
    char keep[GALLERY_NAME_MAX] = "";
    if (s_cur >= 0 && s_cur < s_n) strcpy(keep, s_names[s_cur]);
    mkdir(GALLERY_DIR, 0777);
    s_n = 0;
    FILE *ix = fopen(GALLERY_INDEX, "r");
    if (ix) {
        char line[GALLERY_NAME_MAX + 8];
        while (s_n < GALLERY_MAX && fgets(line, sizeof(line), ix)) {
            line[strcspn(line, "\r\n")] = 0;
            bool dup = false;
            for (int i = 0; i < s_n && !dup; i++) dup = !strcmp(s_names[i], line);
            if (!dup && valid_entry(line)) strcpy(s_names[s_n++], line);
        }
        fclose(ix);
    }
    /* Files not named in index.txt (e.g. freshly uploaded) follow, sorted. */
    int listed = s_n;
    DIR *d = opendir(GALLERY_DIR);
    struct dirent *e;
    while (d && s_n < GALLERY_MAX && (e = readdir(d))) {
        if (!valid_entry(e->d_name)) continue;
        bool dup = false;
        for (int i = 0; i < listed && !dup; i++) dup = !strcmp(s_names[i], e->d_name);
        if (!dup) strcpy(s_names[s_n++], e->d_name);
    }
    if (d) closedir(d);
    qsort(s_names[listed], s_n - listed, GALLERY_NAME_MAX, cmp_name);
    int found = -1;
    for (int i = 0; keep[0] && i < s_n; i++) if (!strcmp(s_names[i], keep)) found = i;
    if (found >= 0) s_cur = found;
    else if (s_cur >= s_n) s_cur = s_n - 1;
    int n = s_n;
    xSemaphoreGive(s_state);
    sd_unlock();
    ESP_LOGI(TAG, "scan: %d images", n);
    return n;
}

esp_err_t gallery_init(void)
{
    s_sd = xSemaphoreCreateRecursiveMutex();
    s_state = xSemaphoreCreateMutex();
    if (!s_sd || !s_state) return ESP_ERR_NO_MEM;
    gallery_rescan();
    char name[GALLERY_NAME_MAX] = "";
    int32_t idx = 0;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(name);
        nvs_get_str(h, "cur", name, &len);
        nvs_get_i32(h, "curi", &idx);
        uint8_t st = 1;
        if (nvs_get_u8(h, "strip", &st) == ESP_OK) s_strip = st;
        nvs_close(h);
    }
    /* Keyed by file name: deleting other files doesn't shift the position.
     * If the file itself is gone, fall back to the old numeric slot. */
    s_cur = -1;
    for (int i = 0; i < s_n; i++) if (!strcmp(s_names[i], name)) s_cur = i;
    if (s_cur < 0 && s_n) s_cur = idx >= 0 && idx < s_n ? idx : 0;
    return ESP_OK;
}

static void draw_strip(int cur, int n)
{
    bool en = false, present = false;
    int soc = -1;
    (void)axp2101_gauge_status(&en, &present, &soc);
    pcf8563_time_t t;
    if (pcf8563_get_time(&t) == ESP_OK && t.year >= 2024)
        snprintf(s_last_time, sizeof(s_last_time), "%02u-%02u %02u:%02u", t.month, t.day, t.hour, t.minute);
    char bat[12], pos[16];
    if (soc >= 0) snprintf(bat, sizeof(bat), "%d%%", soc); else strcpy(bat, "--%");
    snprintf(pos, sizeof(pos), "%d/%d", cur + 1, n);
    const int y0 = SCREEN_HEIGHT - FRAME_STRIP_H, ty = y0 + 7;
    frame_rect(s_frame, 0, y0, SCREEN_WIDTH, FRAME_STRIP_H, SCREEN_COLOR_WHITE);
    frame_rect(s_frame, 0, y0, SCREEN_WIDTH, 1, SCREEN_COLOR_BLACK);
    /* battery glyph + % (left), n/N (centre), RTC time (right) */
    frame_rect(s_frame, 8, ty, 22, 14, SCREEN_COLOR_BLACK);
    frame_rect(s_frame, 10, ty + 2, 18, 10, SCREEN_COLOR_WHITE);
    if (soc > 0) frame_rect(s_frame, 11, ty + 3, 16 * soc / 100 + 1, 8,
                            soc <= 15 ? SCREEN_COLOR_RED : SCREEN_COLOR_BLACK);
    frame_rect(s_frame, 30, ty + 4, 3, 6, SCREEN_COLOR_BLACK);
    frame_text(s_frame, 40, ty, bat, 2, SCREEN_COLOR_BLACK);
    frame_text(s_frame, (SCREEN_WIDTH - frame_text_width(pos, 2)) / 2, ty, pos, 2, SCREEN_COLOR_BLACK);
    frame_text(s_frame, SCREEN_WIDTH - 8 - frame_text_width(s_last_time, 2), ty, s_last_time, 2, SCREEN_COLOR_BLACK);
}

static esp_err_t render_index(int i, int n, const char *name)
{
    char path[GALLERY_NAME_MAX + 16];
    snprintf(path, sizeof(path), GALLERY_DIR "/%s", name);
    sd_lock();
    FILE *f = fopen(path, "rb");
    size_t got = f ? fread(s_frame, 1, SCREEN_FRAME_BYTES, f) : 0;
    if (f) fclose(f);
    sd_unlock();
    if (got != SCREEN_FRAME_BYTES) {
        ESP_LOGE(TAG, "read %s: %u bytes", path, (unsigned)got);
        return ESP_FAIL;
    }
    if (s_strip) draw_strip(i, n);
    ESP_LOGI(TAG, "show %d/%d %s", i + 1, n, name);
    return screen_driver_show_frame(s_frame, SCREEN_FRAME_BYTES);
}

static void show_bitmaps(const msg_bitmap_t *a, const msg_bitmap_t *b)
{
    frame_fill(s_frame, SCREEN_COLOR_WHITE);
    frame_bitmap_centered(s_frame, a, SCREEN_HEIGHT / 2 - 30, SCREEN_COLOR_BLACK);
    if (b) frame_bitmap_centered(s_frame, b, SCREEN_HEIGHT / 2 + 50, SCREEN_COLOR_RED);
    (void)screen_driver_show_frame(s_frame, SCREEN_FRAME_BYTES);
}
esp_err_t gallery_show_message_no_tf(void) { show_bitmaps(&k_msg_no_tf, &k_msg_hint); return ESP_OK; }

static void gallery_task(void *arg)
{
    (void)arg;
    bool first = true, empty_shown = false;
    for (;;) {
        if (!first) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        first = false;
        for (;;) {      /* drain: presses during a refresh coalesce */
            xSemaphoreTake(s_state, portMAX_DELAY);
            int n = s_n, target = s_cur;
            if (s_request[0]) {
                for (int i = 0; i < n; i++) if (!strcmp(s_names[i], s_request)) target = i;
                s_request[0] = 0;
            }
            if (n > 0) {
                if (target < 0) target = 0;
                target = ((target + s_pending) % n + n) % n;  /* wrap both ends */
            }
            s_pending = 0;
            bool changed = n > 0 && (target != s_cur || first || empty_shown || s_force);
            s_force = false;
            char name[GALLERY_NAME_MAX] = "";
            if (n > 0) { s_cur = target; strcpy(name, s_names[target]); }
            xSemaphoreGive(s_state);
            if (n == 0) {
                if (!empty_shown) { show_bitmaps(&k_msg_no_img, NULL); empty_shown = true; }
                break;
            }
            empty_shown = false;
            if (!changed && !first) break;
            first = false;
            if (render_index(target, n, name) == ESP_OK) nvs_save();
            xSemaphoreTake(s_state, portMAX_DELAY);
            bool more = s_pending != 0 || s_request[0] || s_force;
            xSemaphoreGive(s_state);
            if (!more) break;
        }
    }
}

void gallery_start_task(void)
{
    xTaskCreate(gallery_task, "gallery", 6144, NULL, 4, &s_task);
}

static void kick(void) { if (s_task) xTaskNotifyGive(s_task); }

void gallery_nav(int delta)
{
    xSemaphoreTake(s_state, portMAX_DELAY);
    s_pending += delta;
    xSemaphoreGive(s_state);
    kick();
}

esp_err_t gallery_show_name(const char *name)
{
    xSemaphoreTake(s_state, portMAX_DELAY);
    int found = -1;
    for (int i = 0; i < s_n; i++) if (!strcmp(s_names[i], name)) found = i;
    if (found >= 0) { strlcpy(s_request, name, sizeof(s_request)); s_pending = 0; s_force = true; }
    xSemaphoreGive(s_state);
    if (found < 0) return ESP_ERR_NOT_FOUND;
    kick();
    return ESP_OK;
}

void gallery_set_strip(bool on)
{
    xSemaphoreTake(s_state, portMAX_DELAY);
    s_strip = on;
    s_force = true;
    xSemaphoreGive(s_state);
    nvs_save();
    kick();
}

void gallery_get_state(int *cur, int *n, char *name, size_t cap, bool *strip)
{
    xSemaphoreTake(s_state, portMAX_DELAY);
    *cur = s_cur; *n = s_n; *strip = s_strip;
    strlcpy(name, (s_cur >= 0 && s_cur < s_n) ? s_names[s_cur] : "", cap);
    xSemaphoreGive(s_state);
}

void gallery_list(void (*cb)(const char *, void *), void *ctx)
{
    xSemaphoreTake(s_state, portMAX_DELAY);
    for (int i = 0; i < s_n; i++) cb(s_names[i], ctx);
    xSemaphoreGive(s_state);
}
