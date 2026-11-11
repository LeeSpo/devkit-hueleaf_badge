#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#define GALLERY_DIR      "/sdcard/img"
#define GALLERY_INDEX    GALLERY_DIR "/index.txt"
#define GALLERY_MAX      300
#define GALLERY_NAME_MAX 64
#define GALLERY_SELECTION_PATH GALLERY_DIR "/slideshow.txt"
#define GALLERY_SELECTION_BYTES ((GALLERY_MAX + 7) / 8)
/* Paged inventory + selection snapshot (max 64 records), little endian:
 * u64 token, u16 total/start/count, u8 flags(bit0 custom, bit1 fault),
 * records: u8 selected, u8 filename_length, filename bytes.
 * Token 0 starts a read; other tokens must match the current full snapshot. */
esp_err_t gallery_selection_read(uint16_t start, uint64_t token, uint8_t *out, size_t cap, size_t *len);
/* Same snapshot/header, records append u8 label_length + UTF-8 label bytes.
 * Labels are live metadata, not part of the inventory/selection token.
 * Missing or invalid HLL1 labels have length zero. Owns SD then state locks. */
esp_err_t gallery_selection_labels_read(uint16_t start, uint64_t token, uint8_t *out, size_t cap, size_t *len);
/* Compact bitset refers to the exact snapshot token, never stale indices.
 * Caller owns runtime storage admission; saves an explicit list, even empty. */
esp_err_t gallery_selection_save(uint64_t token, uint16_t total, const uint8_t *bits, size_t len);
/* Serialises all TF access (gallery render, USB protocol, firmware update). */
void sd_lock(void);
void sd_unlock(void);
esp_err_t gallery_prepare(void);         /* stable locks, before USB/update */
/* Before tasks start: validate RTC state and prepare display retention. */
void gallery_boot_init(bool retained);
bool gallery_is_ready(void);
void gallery_set_ready(bool ready);
bool gallery_upload_session_begin(void);
bool gallery_upload_file_begin(void);
esp_err_t gallery_init(void);            /* load NVS, scan card */
esp_err_t gallery_render_initial(void);  /* forced display; checked before OTA validation */
esp_err_t gallery_retain_initial(void);  /* retain unchanged frame when status strip is off */
esp_err_t gallery_render_scheduled(const char *previous);
esp_err_t gallery_restore_retained(const char *previous); /* status strip decides redraw */
bool gallery_auto_next(void);
bool gallery_sleep_begin(void);
esp_err_t gallery_start_task(void);      /* render/navigation task */
int  gallery_rescan(bool redraw);        /* returns N; false updates list only */
/* Navigation input (any source: BOOT, PEK, USB, future tilt/BLE). Presses are
 * accumulated; the task renders only the final target after a refresh ends. */
void gallery_nav(int delta);             /* USB NAV; also closes the picker */
/* Buttons. Outside the picker: BOOT short = prev, PEK short = next,
 * BOOT long = open picker. Inside: BOOT short = cursor down, PEK short = next
 * page (cursor up with tap_next on), BOOT long = confirm.
 * Return true if the press was accepted. */
bool gallery_btn_boot_short(void);
bool gallery_btn_pek_short(void);
bool gallery_btn_boot_long(void);
/* Double tap: next image, ignored while an image refresh is running/queued
 * (no queued multi-skips). Inside the picker: next page. True if accepted. */
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

/* true while the name picker is open */
bool gallery_picker_open(void);

/* Atomic navigation admission for one remote request, never coalesced with keys. */
esp_err_t gallery_remote_request(int delta, const char *name);

/* Restore the remote image position and refresh only when startup policy requires it. */
esp_err_t gallery_restore_remote(const char *previous);
