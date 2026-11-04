#include "proto.h"
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "axp2101.h"
#include "boot_select.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "fw_update.h"
#include "file_store.h"
#include "gallery.h"

/* Startup publishes readiness only after gallery initialization completes. */
extern bool gallery_is_ready(void);
#include "logbuf.h"
#include "pcf8563.h"
#include "screen_driver.h"
#include "tf_card.h"
#include "version.h"

static uint8_t s_rx[12 + PROTO_MAX_PAYLOAD + 4];
static uint8_t s_tx[12 + PROTO_MAX_PAYLOAD + 4];

unsigned proto_crc32(unsigned crc, const void *data, unsigned len)
{
    const uint8_t *p = data;
    crc = ~crc;
    while (len--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return ~crc;
}
static void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static uint32_t get32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static void reply(uint8_t cmd, uint8_t seq, int32_t status, const void *data, size_t len)
{
    if (len > PROTO_MAX_PAYLOAD - 4) len = PROTO_MAX_PAYLOAD - 4;
    memcpy(s_tx, "HLP1", 4);
    s_tx[4] = cmd | 0x80; s_tx[5] = seq; s_tx[6] = s_tx[7] = 0;
    put32(s_tx + 8, len + 4);
    put32(s_tx + 12, (uint32_t)status);
    if (len) memcpy(s_tx + 16, data, len);
    put32(s_tx + 16 + len, proto_crc32(0, s_tx + 4, 12 + len));
    size_t total = 20 + len, off = 0;
    while (off < total) {
        int w = usb_serial_jtag_write_bytes(s_tx + off, total - off, pdMS_TO_TICKS(500));
        if (w <= 0) return;      /* host gone; drop */
        off += w;
    }
}

static bool read_exact(uint8_t *dst, size_t n, TickType_t to)
{
    size_t got = 0;
    while (got < n) {
        int r = usb_serial_jtag_read_bytes(dst + got, n - got, to);
        if (r <= 0) return false;
        got += r;
    }
    return true;
}

static bool safe_name(const char *n)
{
    size_t l = strlen(n);
    if (l < 1 || l > GALLERY_NAME_MAX - 5 || n[0] == '.') return false;
    for (size_t i = 0; i < l; i++) {
        char c = n[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'))
            return false;
    }
    return true;
}
static void norm_name(char *dst, size_t cap, const uint8_t *src, size_t len)
{
    if (len >= cap - 4) len = cap - 5;
    memcpy(dst, src, len); dst[len] = 0;
    size_t l = strlen(dst);
    if (l < 4 || strcasecmp(dst + l - 4, ".bin")) strcat(dst, ".bin");
}

static void labels_cb(const char *name, void *ctx)
{
    char *out = ctx, path[GALLERY_NAME_MAX + 32];
    uint8_t hdr[5 + 255];
    gallery_label_path(path, sizeof(path), name);
    sd_lock();
    FILE *f = fopen(path, "rb");
    size_t got = f ? fread(hdr, 1, sizeof(hdr), f) : 0;
    if (f) fclose(f);
    sd_unlock();
    if (got < 5 || memcmp(hdr, "HLL1", 4) || got < 5u + hdr[4]) return;
    size_t l = strlen(out), need = strlen(name) + hdr[4] + 2;
    if (l + need + 1 >= PROTO_MAX_PAYLOAD - 8) return;
    char *o = out + l;
    strcpy(o, name); o += strlen(name); *o++ = '\t';
    for (int i = 0; i < hdr[4]; i++) { char c = hdr[5 + i]; *o++ = (c == '\n' || c == '\t') ? ' ' : c; }
    *o++ = '\n'; *o = 0;
}

/* upload state */
static FILE *s_up;
static uint32_t s_up_size, s_up_crc, s_up_off, s_up_run;
static uint8_t s_up_kind;
static char s_up_final[96];
#define UP_TMP_IMG GALLERY_DIR "/.upload.tmp"
#define UP_TMP_FW  "/sdcard/update/.upload.tmp"
#define UP_TMP_LBL GALLERY_LABEL_DIR "/.upload.tmp"
static const char *up_tmp(void) { return s_up_kind == 1 ? UP_TMP_FW : s_up_kind == 2 ? UP_TMP_LBL : UP_TMP_IMG; }

static void list_cb(const char *name, void *ctx)
{
    char *out = ctx; size_t l = strlen(out);
    if (l + strlen(name) + 2 < PROTO_MAX_PAYLOAD - 8) { strcat(out, name); strcat(out, "\n"); }
}

static void handle(uint8_t cmd, uint8_t seq, const uint8_t *p, uint32_t len)
{
    static char txt[PROTO_MAX_PAYLOAD];
    char name[GALLERY_NAME_MAX];
    const bool ready = gallery_is_ready();
    if (ready && (cmd == CMD_INFO || cmd == CMD_UP_BEGIN || cmd == CMD_SHOW ||
                  cmd == CMD_DELETE || cmd == CMD_SET_ORDER)) {
        sd_lock();
        (void)tf_card_check();
        sd_unlock();
    }
    const bool tf = tf_card_is_mounted();
    if (ready && tf_card_has_fault() && cmd != CMD_PING && cmd != CMD_INFO &&
        cmd != CMD_LOG && cmd != CMD_REBOOT && cmd != CMD_BOOT_STOCK) {
        reply(cmd, seq, -7, NULL, 0);
        return;
    }
    if (!ready && cmd != CMD_PING && cmd != CMD_INFO &&
        cmd != CMD_LOG && cmd != CMD_REBOOT && cmd != CMD_BOOT_STOCK) {
        reply(cmd, seq, -8, NULL, 0);
        return;
    }
    switch (cmd) {
    case CMD_PING: reply(cmd, seq, 0, "HLP1 " HL_FW_VERSION, sizeof("HLP1 " HL_FW_VERSION) - 1); return;
    case CMD_INFO: {
        bool en = false, present = false; int soc = -1;
        (void)axp2101_gauge_status(&en, &present, &soc);
        uint64_t total = 0, fre = 0;
        if (tf && gallery_is_ready()) { sd_lock(); esp_vfs_fat_info(TF_CARD_MOUNT_POINT, &total, &fre); sd_unlock(); }
        int cur = -1, n = 0; bool strip = false;
        name[0] = 0;
        if (gallery_is_ready()) gallery_get_state(&cur, &n, name, sizeof(name), &strip);
        const esp_partition_t *run = esp_ota_get_running_partition();
        int l = snprintf(txt, sizeof(txt),
            "{\"fw\":\"%s\",\"part\":\"%s\",\"battery\":%d,\"bat_present\":%s,\"tf\":%s,"
            "\"total_kb\":%llu,\"free_kb\":%llu,\"n\":%d,\"cur\":%d,\"name\":\"%s\",\"strip\":%s,\"ready\":%s}",
            HL_FW_VERSION, run ? run->label : "?", soc, present ? "true" : "false", tf ? "true" : "false",
            (unsigned long long)(total / 1024), (unsigned long long)(fre / 1024), n, cur, name,
            strip ? "true" : "false", gallery_is_ready() ? "true" : "false");
        reply(cmd, seq, 0, txt, l);
        return;
    }
    case CMD_LIST: txt[0] = 0; gallery_list(list_cb, txt); reply(cmd, seq, 0, txt, strlen(txt)); return;
    case CMD_UP_BEGIN: {
        if (!tf) { reply(cmd, seq, -6, NULL, 0); return; }
        if (len < 10) { reply(cmd, seq, -5, NULL, 0); return; }
        uint8_t kind = p[0]; uint32_t size = get32(p + 1), crc = get32(p + 5);
        char final[sizeof(s_up_final)];
        if (kind == 0) {
            norm_name(name, sizeof(name), p + 9, len - 9);
            if (!safe_name(name) || size != SCREEN_FRAME_BYTES) { reply(cmd, seq, -5, NULL, 0); return; }
            snprintf(final, sizeof(final), GALLERY_DIR "/%s", name);
        } else if (kind == 1) {
            if (size == 0 || size > 0xB0000) { reply(cmd, seq, -5, NULL, 0); return; }
            strcpy(final, FW_UPDATE_PATH);
        } else if (kind == 2) {          /* picker name label for image name[] */
            norm_name(name, sizeof(name), p + 9, len - 9);
            if (!safe_name(name) || size < 9 || size > 4096) { reply(cmd, seq, -5, NULL, 0); return; }
            gallery_label_path(final, sizeof(final), name);
        } else { reply(cmd, seq, -5, NULL, 0); return; }
        sd_lock();
        if (s_up) {
            fclose(s_up); s_up = NULL;
            unlink(up_tmp());
        }
        s_up_kind = kind; s_up_size = size; s_up_crc = crc;
        strcpy(s_up_final, final);
        mkdir(GALLERY_LABEL_DIR, 0777);
        mkdir(GALLERY_DIR, 0777); mkdir("/sdcard/update", 0777);
        s_up = fopen(up_tmp(), "wb");
        sd_unlock();
        s_up_off = 0; s_up_run = 0;
        reply(cmd, seq, s_up ? 0 : -7, NULL, 0);
        return;
    }
    case CMD_UP_DATA: {
        if (!s_up || len < 4) { reply(cmd, seq, -5, NULL, 0); return; }
        uint32_t off = get32(p), n = len - 4;
        if (off <= s_up_off && n == s_up_off - off) { reply(cmd, seq, 0, NULL, 0); return; }   /* duplicate (lost ACK) */
        if (off != s_up_off || n > s_up_size - s_up_off) { reply(cmd, seq, -5, &s_up_off, 4); return; }
        sd_lock();
        size_t w = fwrite(p + 4, 1, n, s_up);
        sd_unlock();
        if (w != n) {
            sd_lock(); fclose(s_up); s_up = NULL; unlink(up_tmp()); sd_unlock();
            reply(cmd, seq, -7, NULL, 0); return;
        }
        s_up_run = proto_crc32(s_up_run, p + 4, n);
        s_up_off += n;
        reply(cmd, seq, 0, NULL, 0);
        return;
    }
    case CMD_UP_END: {
        if (!s_up) { reply(cmd, seq, -5, NULL, 0); return; }
        sd_lock();
        int flush_err = fflush(s_up);
        int sync_err = fsync(fileno(s_up));
        int close_err = fclose(s_up); s_up = NULL;
        const char *tmp = up_tmp();
        int32_t st = (s_up_off == s_up_size && s_up_run == s_up_crc) ? 0 : -2;
        if (flush_err || sync_err || close_err) st = -7;
        if (st == 0 && !file_store_replace(tmp, s_up_final)) st = -7;
        else unlink(tmp);
        sd_unlock();
        if (st == 0 && s_up_kind == 0) gallery_rescan();
        reply(cmd, seq, st, NULL, 0);
        return;
    }
    case CMD_DELETE: case CMD_SHOW:
        norm_name(name, sizeof(name), p, len);
        if (!safe_name(name)) { reply(cmd, seq, -5, NULL, 0); return; }
        if (cmd == CMD_SHOW) { reply(cmd, seq, gallery_show_name(name) == ESP_OK ? 0 : -4, NULL, 0); return; }
        snprintf(txt, sizeof(txt), GALLERY_DIR "/%s", name);
        sd_lock(); int r = unlink(txt);
        if (r == 0) { gallery_label_path(txt, sizeof(txt), name); unlink(txt); }
        sd_unlock();
        if (r == 0) gallery_rescan();
        reply(cmd, seq, r == 0 ? 0 : -4, NULL, 0);
        return;
    case CMD_NAV: gallery_nav(len ? (int8_t)p[0] : 1); reply(cmd, seq, 0, NULL, 0); return;
    case CMD_LOG: { size_t n = logbuf_read(txt, PROTO_MAX_PAYLOAD - 8); reply(cmd, seq, 0, txt, n); return; }
    case CMD_STRIP: gallery_set_strip(len && p[0]); reply(cmd, seq, 0, NULL, 0); return;
    case CMD_SET_TIME: {
        if (len < 7) { reply(cmd, seq, -5, NULL, 0); return; }
        pcf8563_time_t t = { .year = p[0] | p[1] << 8, .month = p[2], .day = p[3], .hour = p[4], .minute = p[5], .second = p[6] };
        reply(cmd, seq, pcf8563_set_time(&t) == ESP_OK ? 0 : -7, NULL, 0);
        return;
    }
    case CMD_LABELS: {   /* "<card name>\t<display name>\n" for images that have a label */
        txt[0] = 0;
        gallery_list(labels_cb, txt);
        reply(cmd, seq, 0, txt, strlen(txt));
        return;
    }
    case CMD_SET_ORDER: {
        if (!tf) { reply(cmd, seq, -6, NULL, 0); return; }
        /* validate every name before touching the card */
        size_t i = 0;
        while (i < len) {
            size_t j = i;
            while (j < len && p[j] != '\n') j++;
            if (j > i) {
                if (j - i >= GALLERY_NAME_MAX) { reply(cmd, seq, -5, NULL, 0); return; }
                memcpy(name, p + i, j - i); name[j - i] = 0;
                size_t l = strlen(name);
                if (l != j - i || !safe_name(name) || l < 5 || strcasecmp(name + l - 4, ".bin")) { reply(cmd, seq, -5, NULL, 0); return; }
            }
            i = j + 1;
        }
        int32_t st = 0;
        sd_lock();
        if (len == 0) {
            if (!file_store_recover(GALLERY_INDEX) ||
                (unlink(GALLERY_INDEX) != 0 && errno != ENOENT)) st = -7;
        } else {
            mkdir(GALLERY_DIR, 0777);
            FILE *f = fopen(GALLERY_DIR "/.index.tmp", "wb");
            if (!f) st = -7;
            else {
                if (fwrite(p, 1, len, f) != len) st = -7;
                if (p[len - 1] != '\n' && fputc('\n', f) == EOF) st = -7;
                int fe = fflush(f), se = fsync(fileno(f)), ce = fclose(f);
                if (fe || se || ce) st = -7;
                if (st == 0 && !file_store_replace(GALLERY_DIR "/.index.tmp", GALLERY_INDEX)) st = -7;
                else unlink(GALLERY_DIR "/.index.tmp");
            }
        }
        sd_unlock();
        if (st == 0) gallery_rescan();
        reply(cmd, seq, st, NULL, 0);
        return;
    }
    case CMD_REBOOT: reply(cmd, seq, 0, NULL, 0); vTaskDelay(pdMS_TO_TICKS(200)); esp_restart();
    case CMD_BOOT_STOCK: reply(cmd, seq, 0, NULL, 0); vTaskDelay(pdMS_TO_TICKS(200)); boot_select_restart_to_stock();
    default: reply(cmd, seq, -3, NULL, 0);
    }
}

static void proto_task(void *a)
{
    (void)a;
    static const uint8_t magic[4] = { 'H', 'L', 'P', '1' };
    for (;;) {
        /* hunt for sync */
        int m = 0;
        while (m < 4) {
            uint8_t c;
            if (usb_serial_jtag_read_bytes(&c, 1, portMAX_DELAY) != 1) continue;
            m = (c == magic[m]) ? m + 1 : (c == magic[0]);
        }
        uint8_t *h = s_rx;
        if (!read_exact(h + 4, 8, pdMS_TO_TICKS(500))) continue;
        uint32_t len = get32(h + 8);
        if (len > PROTO_MAX_PAYLOAD) { reply(h[4], h[5], -5, NULL, 0); continue; }
        if (!read_exact(h + 12, len + 4, pdMS_TO_TICKS(1000))) continue;
        if (proto_crc32(0, h + 4, 8 + len) != get32(h + 12 + len)) { reply(h[4], h[5], -2, NULL, 0); continue; }
        handle(h[4], h[5], h + 12, len);
    }
}

esp_err_t proto_start(void)
{
    usb_serial_jtag_driver_config_t cfg = { .rx_buffer_size = 16384, .tx_buffer_size = 4096 };
    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    if (err != ESP_OK) return err;
    if (xTaskCreate(proto_task, "proto", 6144, NULL, 3, NULL) != pdPASS) {
        usb_serial_jtag_driver_uninstall();
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
