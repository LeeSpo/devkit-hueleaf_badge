#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#define GALLERY_DIR      "/sdcard/img"
#define GALLERY_INDEX    GALLERY_DIR "/index.txt"
#define GALLERY_MAX      300
#define GALLERY_NAME_MAX 64
/* Serialises all TF access (gallery render, USB protocol, firmware update). */
void sd_lock(void);
void sd_unlock(void);
esp_err_t gallery_init(void);            /* load NVS, scan card */
void gallery_start_task(void);           /* render/navigation task */
int  gallery_rescan(void);               /* returns N */
/* Navigation input (any source: BOOT, PEK, USB, future tilt/BLE). Presses are
 * accumulated; the task renders only the final target after a refresh ends. */
void gallery_nav(int delta);
esp_err_t gallery_show_name(const char *name);  /* request; async */
void gallery_set_strip(bool on);
void gallery_get_state(int *cur, int *n, char *name, size_t cap, bool *strip);
void gallery_list(void (*cb)(const char *name, void *ctx), void *ctx);
/* Full-screen CJK message pages (blocking). */
esp_err_t gallery_show_message_no_tf(void);
