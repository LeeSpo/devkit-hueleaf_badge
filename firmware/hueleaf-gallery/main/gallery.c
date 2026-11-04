#include "gallery.h"
#include "tap.h"
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
static volatile bool s_rendering;            /* image refresh in progress */
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
    screen_driver_mono_stash_lost();   /* s_frame is about to hold image data */
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

/* Black/white UI screens (picker, messages) go through here. */
esp_err_t gallery_show_ui_frame(uint8_t *f)
{
    return screen_driver_show_mono(f, SCREEN_FRAME_BYTES);
}

static void show_bitmaps(const msg_bitmap_t *a, const msg_bitmap_t *b)
{
    frame_fill(s_frame, SCREEN_COLOR_WHITE);
    frame_bitmap_centered(s_frame, a, SCREEN_HEIGHT / 2 - 30, SCREEN_COLOR_BLACK);
    if (b) frame_bitmap_centered(s_frame, b, SCREEN_HEIGHT / 2 + 50, SCREEN_COLOR_BLACK);
    (void)gallery_show_ui_frame(s_frame);
}
esp_err_t gallery_show_message_no_tf(void) { show_bitmaps(&k_msg_no_tf, &k_msg_hint); return ESP_OK; }

/* ---- name picker (black/white page composed from host-rendered labels) ---- */
/* Layout: keep in sync with tools/hueleaf-push/hlpicker.py */
#define PK_ROWS       14
#define PK_ROW_H      40
#define PK_Y0         80
#define PK_NUM_RIGHT  58
#define PK_LABEL_X    68
#define PK_COALESCE_MS 1200
#define PK_IDLE_MS    60000
#define PK_LABEL_MAX  (5 + 255 + 4 + 51 * 32)
static bool pk_on, pk_drawn, pk_dirty, pk_confirm;
static int pk_cur;
static TickType_t pk_last;

void gallery_label_path(char *dst, size_t cap, const char *bin)
{
    size_t l = strlen(bin);
    if (l > 4 && !strcasecmp(bin + l - 4, ".bin")) l -= 4;
    snprintf(dst, cap, GALLERY_LABEL_DIR "/%.*s.lbl", (int)l, bin);
}

static void draw_label_or_name(const char *name, int y, uint8_t ink)
{
    static uint8_t buf[PK_LABEL_MAX];
    char path[GALLERY_NAME_MAX + 32];
    gallery_label_path(path, sizeof(path), name);
    sd_lock();
    FILE *f = fopen(path, "rb");
    size_t got = f ? fread(buf, 1, sizeof(buf), f) : 0;
    if (f) fclose(f);
    sd_unlock();
    if (got >= 9 && !memcmp(buf, "HLL1", 4) && got >= 9u + buf[4]) {
        const uint8_t *q = buf + 5 + buf[4];
        int w = q[0] | q[1] << 8, h = q[2] | q[3] << 8;
        if (w > 0 && h > 0 && w <= SCREEN_WIDTH - PK_LABEL_X && h < PK_ROW_H &&
            (size_t)(q + 4 - buf) + (size_t)((w + 7) / 8) * h <= got) {
            frame_bits(s_frame, PK_LABEL_X, y + (PK_ROW_H - 2 - h) / 2, w, h, q + 4, ink);
            return;
        }
    }
    /* no label (uploaded by an older tool / copied by hand): ASCII file stem */
    char t[40];
    size_t l = strlen(name);
    if (l > 4 && !strcasecmp(name + l - 4, ".bin")) l -= 4;
    if (l > 33) snprintf(t, sizeof(t), "%.31s..", name);
    else snprintf(t, sizeof(t), "%.*s", (int)l, name);
    frame_text(s_frame, PK_LABEL_X, y + (PK_ROW_H - 2 - 14) / 2, t, 2, ink);
}

static void render_picker(int cur, int n, char (*names)[GALLERY_NAME_MAX])
{
    const int page = cur / PK_ROWS, pages = (n + PK_ROWS - 1) / PK_ROWS;
    frame_fill(s_frame, SCREEN_COLOR_WHITE);
    frame_bitmap_centered(s_frame, &k_msg_pick_title, 38, SCREEN_COLOR_BLACK);
    frame_rect(s_frame, 0, PK_Y0 - 8, SCREEN_WIDTH, 2, SCREEN_COLOR_BLACK);
    for (int r = 0; r < PK_ROWS; r++) {
        int i = page * PK_ROWS + r;
        if (i >= n) break;
        int y = PK_Y0 + r * PK_ROW_H;
        bool sel = i == cur;
        uint8_t fg = sel ? SCREEN_COLOR_WHITE : SCREEN_COLOR_BLACK;
        if (sel) frame_rect(s_frame, 0, y, SCREEN_WIDTH, PK_ROW_H - 2, SCREEN_COLOR_BLACK);
        char num[16];
        snprintf(num, sizeof(num), "%d.", i + 1);
        frame_text(s_frame, PK_NUM_RIGHT - frame_text_width(num, 2), y + (PK_ROW_H - 2 - 14) / 2, num, 2, fg);
        draw_label_or_name(names[r], y, fg);
    }
    const int fy = PK_Y0 + PK_ROWS * PK_ROW_H + 8;
    frame_rect(s_frame, 0, fy, SCREEN_WIDTH, 1, SCREEN_COLOR_BLACK);
    char pg[32];
    snprintf(pg, sizeof(pg), "%d/%d", page + 1, pages);
    frame_text(s_frame, (SCREEN_WIDTH - frame_text_width(pg, 2)) / 2, fy + 10, pg, 2, SCREEN_COLOR_BLACK);
    if (tap_enabled()) {
        frame_bitmap_centered(s_frame, &k_msg_pick_hint_tap, 690, SCREEN_COLOR_BLACK);
        frame_bitmap_centered(s_frame, &k_msg_pick_hint_tap2, 728, SCREEN_COLOR_BLACK);
    } else frame_bitmap_centered(s_frame, &k_msg_pick_hint, 698, SCREEN_COLOR_BLACK);
    ESP_LOGI(TAG, "picker page %d/%d cursor %d", page + 1, pages, cur + 1);
    (void)gallery_show_ui_frame(s_frame);
}

/* Returns ticks to wait before the picker needs attention, 0 = act now via
 * normal path, or portMAX_DELAY when the picker is idle-closed/inactive. */
static TickType_t picker_step(void)
{
    static char names[PK_ROWS][GALLERY_NAME_MAX];
    xSemaphoreTake(s_state, portMAX_DELAY);
    if (!pk_on) { xSemaphoreGive(s_state); return portMAX_DELAY; }
    TickType_t el = xTaskGetTickCount() - pk_last;
    if (pk_confirm) {
        pk_on = pk_confirm = false;
        if (pk_cur != s_cur && pk_cur < s_n) { strcpy(s_request, s_names[pk_cur]); s_force = true; }
        else if (pk_drawn) s_force = true;      /* screen shows the list: full-colour redraw */
        /* else: picker never drawn, current image still on screen -> nothing */
        pk_drawn = false;
        xSemaphoreGive(s_state);
        return portMAX_DELAY;
    }
    if (pk_dirty && el >= pdMS_TO_TICKS(PK_COALESCE_MS)) {
        int n = s_n, cur = pk_cur < s_n ? pk_cur : 0, base = cur / PK_ROWS * PK_ROWS;
        for (int r = 0; r < PK_ROWS && base + r < n; r++) strcpy(names[r], s_names[base + r]);
        pk_dirty = false;
        pk_drawn = true;
        xSemaphoreGive(s_state);
        render_picker(cur, n, names);           /* presses during this coalesce */
        return 1;
    }
    if (pk_dirty) { xSemaphoreGive(s_state); return pdMS_TO_TICKS(PK_COALESCE_MS) - el; }
    if (el >= pdMS_TO_TICKS(PK_IDLE_MS)) {
        pk_on = false;
        if (pk_drawn) s_force = true;           /* back to the current image */
        pk_drawn = false;
        xSemaphoreGive(s_state);
        ESP_LOGI(TAG, "picker idle -> exit");
        return portMAX_DELAY;
    }
    TickType_t w = pdMS_TO_TICKS(PK_IDLE_MS) - el;
    xSemaphoreGive(s_state);
    return w;
}

static void gallery_task(void *arg)
{
    (void)arg;
    bool first = true, empty_shown = false;
    TickType_t wait = portMAX_DELAY;
    for (;;) {
        if (!first) ulTaskNotifyTake(pdTRUE, wait);
        if (!first && (wait = picker_step()) != portMAX_DELAY) continue;
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
            s_rendering = true;
            if (render_index(target, n, name) == ESP_OK) nvs_save();
            s_rendering = false;
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

/* caller holds s_state */
static void picker_close_locked(void)
{
    if (pk_on && pk_drawn) s_force = true;
    pk_on = pk_drawn = pk_dirty = pk_confirm = false;
}

/* Browsing: one step per refresh; presses while an image is being drawn are
 * dropped (same rule as double-tap), so nothing piles up behind a slow refresh. */
static bool nav_locked(int delta)
{
    if (s_rendering || s_pending != 0) return false;
    s_pending = delta;
    return true;
}

static void pk_page_next_locked(void)
{
    int next = (pk_cur / PK_ROWS + 1) * PK_ROWS;
    pk_cur = next >= s_n ? 0 : next;
    pk_dirty = true; pk_last = xTaskGetTickCount();
}

bool gallery_picker_open(void) { return pk_on; }

bool gallery_btn_boot_short(void)
{
    bool ok = true;
    xSemaphoreTake(s_state, portMAX_DELAY);
    if (pk_on && s_n > 0) { pk_cur = (pk_cur + 1) % s_n; pk_dirty = true; pk_last = xTaskGetTickCount(); }
    else ok = nav_locked(+1);
    xSemaphoreGive(s_state);
    if (ok) kick();
    return ok;
}

/* Picker: with tap_next on, PEK moves the cursor up (double-tap pages);
 * with it off, PEK keeps paging so nobody is stuck stepping one row at a time. */
bool gallery_btn_pek_short(void)
{
    bool ok = true;
    xSemaphoreTake(s_state, portMAX_DELAY);
    if (pk_on && s_n > 0) {
        if (tap_enabled()) { pk_cur = (pk_cur - 1 + s_n) % s_n; pk_dirty = true; pk_last = xTaskGetTickCount(); }
        else pk_page_next_locked();
    } else ok = nav_locked(-1);
    xSemaphoreGive(s_state);
    if (ok) kick();
    return ok;
}

bool gallery_btn_boot_long(void)
{
    bool ok = true;
    xSemaphoreTake(s_state, portMAX_DELAY);
    if (pk_on) { pk_confirm = true; pk_last = xTaskGetTickCount(); }
    else if (s_n > 0) {
        /* open on the page of the current image, cursor on it; drawn after the
         * coalescing delay, so open+confirm in quick succession never refreshes */
        pk_on = true; pk_drawn = false; pk_confirm = false; pk_dirty = true;
        pk_cur = (s_cur >= 0 && s_cur < s_n) ? s_cur : 0;
        pk_last = xTaskGetTickCount();
    } else ok = false;
    xSemaphoreGive(s_state);
    kick();
    return ok;
}

bool gallery_tap_next(void)
{
    xSemaphoreTake(s_state, portMAX_DELAY);
    bool ok;
    if (pk_on) { ok = s_n > 0; if (ok) pk_page_next_locked(); }
    else ok = nav_locked(+1);
    xSemaphoreGive(s_state);
    if (ok) kick();
    return ok;
}

void gallery_nav(int delta)
{
    xSemaphoreTake(s_state, portMAX_DELAY);
    picker_close_locked();
    s_pending += delta;
    xSemaphoreGive(s_state);
    kick();
}

esp_err_t gallery_show_name(const char *name)
{
    xSemaphoreTake(s_state, portMAX_DELAY);
    int found = -1;
    for (int i = 0; i < s_n; i++) if (!strcmp(s_names[i], name)) found = i;
    if (found >= 0) { picker_close_locked(); strlcpy(s_request, name, sizeof(s_request)); s_pending = 0; s_force = true; }
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
