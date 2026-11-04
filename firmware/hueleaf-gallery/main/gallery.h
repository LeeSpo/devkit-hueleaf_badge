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
void gallery_nav(int delta);             /* USB NAV; also closes the picker */
/* Buttons. Outside the picker: BOOT short = next, PEK short = prev,
 * BOOT long = open picker. Inside: BOOT short = cursor down, PEK short = next
 * page, BOOT long = confirm. Return true if the press was accepted. */
bool gallery_btn_boot_short(void);
bool gallery_btn_pek_short(void);
bool gallery_btn_boot_long(void);
/* Double tap: next image, but ignored while a refresh is running/queued or the
 * picker is open (no queued multi-skips). Returns true if accepted. */
bool gallery_tap_next(void);
#define GALLERY_LABEL_DIR GALLERY_DIR "/.labels"
/* "<GALLERY_LABEL_DIR>/<stem>.lbl" for card file <stem>.bin */
void gallery_label_path(char *dst, size_t cap, const char *bin_name);
esp_err_t gallery_show_name(const char *name);  /* request; async */
void gallery_set_strip(bool on);
void gallery_get_state(int *cur, int *n, char *name, size_t cap, bool *strip);
void gallery_list(void (*cb)(const char *name, void *ctx), void *ctx);
esp_err_t gallery_show_ui_frame(uint8_t *frame);  /* B/W UI screens */
/* Full-screen CJK message pages (blocking). */
esp_err_t gallery_show_message_no_tf(void);
