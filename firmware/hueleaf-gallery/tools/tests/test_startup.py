import pathlib
import subprocess
import pytest


@pytest.mark.parametrize("remote", [False, True])
def test_startup(tmp_path, remote):
    main = pathlib.Path(__file__).resolve().parents[2] / "main" / "main.c"
    (tmp_path / "main.c").write_text(main.read_text())
    headers = ["axp2101.h", "board.h", "boot_select.h", "bootloader_common.h", "buttons.h", "esp_log.h", "esp_ota_ops.h", "esp_system.h", "esp_heap_caps.h", "freertos/FreeRTOS.h", "freertos/task.h", "fw_update.h", "gallery.h", "logbuf.h", "nvs_flash.h", "pcf8563.h", "proto.h", "runtime_state.h", "screen_driver.h", "tf_card.h", "version.h", "file_store.h", "hl_cfg.h", "sdkconfig.h", "nvs.h", "tap.h"]
    headers.extend(["slideshow.h", "remote.h", "usb_wake.h"])
    for header in headers:
        path = tmp_path / header
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#include "mock.h"\n')
    (tmp_path / "mock.h").write_text(r"""
#ifndef MOCK_H
#define MOCK_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define CONFIG_HL_MONO_FAST 0
#define ESP_RST_DEEPSLEEP 8
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_PARTITION_TYPE_APP 0
#define ESP_PARTITION_SUBTYPE_APP_FACTORY 0
#define ESP_OTA_IMG_PENDING_VERIFY 1
#define HL_STOCK_MAGIC 123
#define HL_FW_VERSION "test"
#define GALLERY_DIR "/sdcard/img"
#define GALLERY_LABEL_DIR "/sdcard/img/.labels"
#define GALLERY_NAME_MAX 64
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
typedef int esp_err_t;
typedef int esp_ota_img_states_t;
typedef struct { int x; } esp_partition_t;
typedef struct { int x; } esp_app_desc_t;
typedef struct { uint32_t custom[4]; } rtc_retain_mem_t;
rtc_retain_mem_t *bootloader_common_get_rtc_retain_mem(void);
void esp_restart(void); void vTaskDelay(int);
const esp_partition_t *esp_partition_find_first(int,int,const char *);
const esp_partition_t *esp_ota_get_running_partition(void);
int esp_ota_get_partition_description(const esp_partition_t *, esp_app_desc_t *);
int esp_ota_get_state_partition(const esp_partition_t *, esp_ota_img_states_t *);
int esp_ota_mark_app_invalid_rollback_and_reboot(void);
int esp_ota_mark_app_valid_cancel_rollback(void);
int tf_card_mount(void); int nvs_flash_init(void); const char *esp_err_to_name(int);
int board_init(void); int pcf8563_init(void); int gallery_prepare(void);
int esp_reset_reason(void); void gallery_boot_init(bool);
void gallery_set_ready(bool); int proto_start(void); int status_cache_start(void); int screen_driver_init(void);
int gallery_show_message_no_tf(void); void sd_lock(void); void sd_unlock(void);
bool file_store_recover_tree(const char *); int gallery_init(void);
bool file_store_recover(const char *);
int gallery_render_initial(void); int gallery_retain_initial(void); int gallery_start_task(void); int buttons_start(void); static inline void tap_start(void) {}
void slideshow_boot_init(void); int slideshow_load_config(void);
bool slideshow_boot_name(char *, size_t); int slideshow_start(void); void slideshow_inhibit(void);
bool slideshow_tap_boot_name(char *, size_t);
bool slideshow_usb_boot_name(char *, size_t);
void usb_wake_boot_init(void); void usb_wake_ready(void);
int gallery_render_scheduled(const char *); int gallery_restore_retained(const char *);
int fw_update_from_tf(void); void logbuf_init(void);
#ifdef CONFIG_HL_REMOTE
bool remote_boot_init(bool); bool remote_light_boot(void); int remote_init(void);
int remote_wait(void); void remote_image(char *, size_t); int gallery_restore_remote(const char *);
void remote_ready(void); int remote_start(void); void remote_startup_failed(void);
#endif
typedef int nvs_handle_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define HL_CFG_PATH "/sdcard/hueleaf.cfg"
static inline int nvs_open(const char *n, int m, nvs_handle_t *h) { (void)n; (void)m; (void)h; return -1; }
static inline int nvs_get_u8(nvs_handle_t h, const char *k, uint8_t *v) { (void)h; (void)k; (void)v; return -1; }
static inline int nvs_set_u8(nvs_handle_t h, const char *k, uint8_t v) { (void)h; (void)k; (void)v; return 0; }
static inline int nvs_commit(nvs_handle_t h) { (void)h; return 0; }
static inline void nvs_close(nvs_handle_t h) { (void)h; }
static inline int hl_cfg_get_bool(const char *b, size_t n, const char *k, int d) { (void)b; (void)n; (void)k; return d; }
static inline void tap_set_enabled(bool on) { (void)on; }
#endif
""")
    if remote:
        mock = tmp_path / "mock.h"
        mock.write_text("#define CONFIG_HL_REMOTE 1\n" + mock.read_text())
    (tmp_path / "test.c").write_text(r"""
#include "mock.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static char events[256]; static int len, fail, mounts, pending, ready, locked;
static bool boot_init, loaded, scheduled, tap_woke, inhibited, usb_woke, usb_init, usb_ready;
static int reset_reason;
static bool view_retained, light, remote_waited;
int esp_reset_reason(void) { return reset_reason; }
void gallery_boot_init(bool retain) { view_retained=retain; }
#ifdef CONFIG_HL_REMOTE
bool remote_boot_init(bool p) { if (p) light=false; return light; }
bool remote_light_boot(void) { return light; }
int remote_init(void) { return 0; }
int remote_wait(void) { remote_waited=true; return 0; }
void remote_image(char *p,size_t n) { assert(n>=6); strcpy(p,"a.bin"); }
int gallery_restore_remote(const char *p) { assert(!strcmp(p,"a.bin")); events[len++]='H'; events[len]=0; return ESP_OK; }
void remote_ready(void) {}
int remote_start(void) { return 0; }
void remote_startup_failed(void) {}
#endif
static void ev(char c) { events[len++] = c; events[len] = 0; }
static int step(char c) { ev(c); return fail == c ? ESP_FAIL : ESP_OK; }
void usb_wake_boot_init(void) { usb_init=true; } void usb_wake_ready(void) { assert(!usb_ready); usb_ready=true; }
void logbuf_init(void) {} int nvs_flash_init(void) { return 0; }
const char *esp_err_to_name(int n) { (void)n; return "error"; }
void slideshow_boot_init(void) { boot_init=true; }
int slideshow_load_config(void) { loaded=true; return 0; }
bool slideshow_boot_name(char *p,size_t n) { assert(n>=6); strcpy(p,"a.bin"); return scheduled; }
bool slideshow_tap_boot_name(char *p,size_t n) { assert(n>=6); strcpy(p,"a.bin"); return tap_woke; }
bool slideshow_usb_boot_name(char *p,size_t n) { assert(n>=6); strcpy(p,"a.bin"); return usb_woke; }
int slideshow_start(void) { assert(ready && loaded); return step('a'); }
void slideshow_inhibit(void) { inhibited=true; }
int board_init(void) { assert(boot_init && usb_init); return step('b'); } int pcf8563_init(void) { return 0; }
int gallery_prepare(void) { return step('p'); }
void gallery_set_ready(bool r) { ready=r; ev(r?'Y':'N'); }
int proto_start(void) { assert(!ready); return step('u'); }
int status_cache_start(void) { assert(!ready); return step('c'); }
int tf_card_mount(void) { ev('m'); return mounts-- > 0 ? -1 : 0; }
int screen_driver_init(void) { return step('s'); }
int gallery_show_message_no_tf(void) { return step('n'); }
void sd_lock(void) { assert(!locked); locked=1; ev('L'); }
void sd_unlock(void) { assert(locked); locked=0; ev('U'); }
bool file_store_recover_tree(const char *p) { (void)p; assert(locked); return step('r') == 0; }
bool file_store_recover(const char *p) { assert(!strcmp(p, HL_CFG_PATH) && locked); return step('r') == 0; }
int gallery_init(void) { assert(loaded); return step('i'); }
int gallery_render_initial(void) { return step('f'); }
int gallery_retain_initial(void) { return step('R'); }
int gallery_render_scheduled(const char *p) { assert(!strcmp(p,"a.bin")); return step('F'); }
int gallery_restore_retained(const char *p) { assert(!strcmp(p,"a.bin")); return step('K'); }
int gallery_start_task(void) { return step('g'); }
int buttons_start(void) { return step('t'); }
int fw_update_from_tf(void) { assert(!ready && !locked); return step('w'); }
void vTaskDelay(int x) { (void)x; }
static rtc_retain_mem_t mem; rtc_retain_mem_t *bootloader_common_get_rtc_retain_mem(void) { return &mem; }
void esp_restart(void) { ev('R'); }
const esp_partition_t *esp_partition_find_first(int a,int b,const char *c) { (void)a;(void)b;(void)c; return 0; }
const esp_partition_t *esp_ota_get_running_partition(void) { return 0; }
int esp_ota_get_partition_description(const esp_partition_t *a,esp_app_desc_t *b) { (void)a;(void)b; return 0; }
int esp_ota_get_state_partition(const esp_partition_t *a,esp_ota_img_states_t *b) { (void)a; *b=pending; return 0; }
int esp_ota_mark_app_invalid_rollback_and_reboot(void) { ev('X'); return 0; }
int esp_ota_mark_app_valid_cancel_rollback(void) { return step('v'); }
void app_main(void);
static void reset(void) { len=0; events[0]=0; fail=0; mounts=0; pending=1; ready=0; locked=0; boot_init=loaded=scheduled=tap_woke=inhibited=false; reset_reason=0; view_retained=light=remote_waited=usb_woke=usb_init=usb_ready=false; }
int main(void) {
 reset(); app_main(); assert(!view_retained); assert(strstr(events,"ifgtcvwY")); assert(strstr(events,"pNu"));
 const char *failures="pcusrifgt";
 for (const char *f=failures; *f; f++) { reset(); fail=*f; app_main(); assert(strchr(events,'X')); assert(!strchr(events,'v')); assert(!ready); }
 reset(); mounts=3; app_main(); assert(strstr(events,"nm")); assert(strstr(events,"ifgtcvwY"));
 reset(); pending=0; fail='R'; app_main(); assert(!strchr(events,'v') && !ready && !strchr(events,'X'));
 reset(); pending=0; app_main(); assert(strchr(events,'R') && !strchr(events,'f') && ready);
 reset(); pending=0; scheduled=true; app_main(); assert(strchr(events,'F') && !strchr(events,'f') && strstr(events,"wYa"));
 reset(); scheduled=true; app_main(); assert(strchr(events,'f') && !strchr(events,'F')); /* pending OTA still validates a full render */
 reset(); pending=0; tap_woke=true; app_main(); assert(strchr(events,'K') && !strchr(events,'f') && !strchr(events,'F') && ready);
 reset(); tap_woke=true; app_main(); assert(strchr(events,'f') && !strchr(events,'K')); /* OTA validation still checks the display */
 reset(); pending=0; usb_woke=true; app_main(); assert(strchr(events,'K') && !strchr(events,'f') && !strchr(events,'F') && usb_ready);
 reset(); usb_woke=true; app_main(); assert(strchr(events,'f') && !strchr(events,'K')); /* pending OTA always validates */
 reset(); fail='w'; app_main(); assert(inhibited && ready && strchr(events,'a'));
 reset(); fail='a'; app_main(); assert(ready); /* failure to create scheduler stays awake */
 reset(); pending=0; reset_reason=ESP_RST_DEEPSLEEP; tap_woke=true; app_main(); assert(view_retained);
 reset(); reset_reason=ESP_RST_DEEPSLEEP; app_main(); assert(!view_retained && strchr(events,'f')); /* pending OTA */
#ifdef CONFIG_HL_REMOTE
 reset(); pending=0; reset_reason=ESP_RST_DEEPSLEEP; light=true; app_main();
 assert(view_retained && remote_waited && strchr(events,'H') && !strchr(events,'f') && !strchr(events,'s') && ready);
 reset(); reset_reason=ESP_RST_DEEPSLEEP; light=true; app_main();
 assert(!view_retained && !remote_waited && strchr(events,'f') && !strchr(events,'H'));
#endif
 return 0;
}
""")
    subprocess.run(["cc", "-std=c11", "-I", str(tmp_path), str(tmp_path / "main.c"), str(tmp_path / "test.c"), "-o", str(tmp_path / "test")], check=True)
    subprocess.run([str(tmp_path / "test")], check=True)
