#ifndef SCREEN_DRIVER_H
#define SCREEN_DRIVER_H

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* EPD037A02 3.7" E6 480x720，历史标称 JD7601；面板参数见 panel_profile.h。 */
#define SCREEN_WIDTH            480
#define SCREEN_HEIGHT           720
#define SCREEN_FRAME_BYTES      (SCREEN_WIDTH * SCREEN_HEIGHT / 2)
#define SCREEN_MONO_BYTES       (SCREEN_WIDTH * SCREEN_HEIGHT / 8)

typedef enum {
    SCREEN_COLOR_BLACK  = 0x0,
    SCREEN_COLOR_WHITE  = 0x1,
    SCREEN_COLOR_YELLOW = 0x2,
    SCREEN_COLOR_RED    = 0x3,
    SCREEN_COLOR_BLUE   = 0x5,
    SCREEN_COLOR_GREEN  = 0x6,
} screen_color_t;

/* Reader runs under display/SPI locks; do not re-enter display or board SPI.
 * It may return fewer bytes than capacity, but must make positive progress. */
typedef esp_err_t (*screen_frame_read_cb_t)(void *context, uint8_t *buffer,
                                            size_t capacity, size_t *received);
/* Historical name retained for callers; invoked after successful refresh,
 * with both display/SPI locks released. */
typedef void (*screen_before_refresh_cb_t)(void *context);

esp_err_t screen_driver_init(void);
bool screen_driver_initialized(void);
/* Caller owns gallery SD lock; hands SPI2 to read_cb between panel writes. */
esp_err_t screen_driver_show_card_stream(screen_frame_read_cb_t read_cb, void *context);
esp_err_t screen_driver_fill(screen_color_t color);
/* 推送与面板同格式的帧：列优先，每字节 2 个纵向像素（高半字节=偶 y） */
esp_err_t screen_driver_show_frame(const uint8_t *frame, size_t len);
esp_err_t screen_driver_show_stream(screen_frame_read_cb_t read_cb, void *context);
esp_err_t screen_driver_show_stream_ex(screen_frame_read_cb_t read_cb, void *context,
                                       screen_before_refresh_cb_t before_refresh, void *refresh_ctx);
/* head：已经从 HTTP 收下的前缀，先写再继续 read_cb，避免上电时把 TCP 饿死 */
esp_err_t screen_driver_show_stream_prefaced(const uint8_t *head, size_t head_len,
                                             screen_frame_read_cb_t read_cb, void *context);
/* Runtime upload result; hashes describe transmitted source bytes. */
typedef struct {
    uint32_t total_ms, refresh_ms, display_generation, frame_fnv;
    bool completed;
} screen_upload_report_t;
uint32_t screen_driver_generation(void);
typedef enum {
    SCREEN_UPLOAD_RECEIVING, SCREEN_UPLOAD_REFRESH,
} screen_upload_phase_t;
/* Return a failure to cancel before the next physical step, never mid-BUSY. */
typedef esp_err_t (*screen_upload_progress_cb_t)(void *context, screen_upload_phase_t phase);
esp_err_t screen_driver_upload(uint32_t expected_generation,
                                uint8_t *buffer, size_t capacity,
                                screen_frame_read_cb_t read_cb, void *context,
                                screen_upload_progress_cb_t progress,
                                screen_upload_report_t *report);
/* White fill with the same six-ink OTP waveform as ordinary uploads. */
esp_err_t screen_driver_clear(screen_upload_report_t *report);
/* Black/white UI frame (same 4bpp layout; ink 0 = black, else white) via the
 * FrameFilm-derived MonoFast waveform; falls back to show_frame when
 * CONFIG_HL_MONO_FAST is off or on error. The driver stores the previous
 * frame in bit 3 of each nibble of `frame`; compose the next UI frame in the
 * same buffer with frame_fill/frame_set (they preserve that bit). */
esp_err_t screen_driver_show_mono(uint8_t *frame, size_t len);
/* Available in remote-v1 builds: logical x ascending, y ascending, MSB first, 1 white.
 * Driver owns a separate previous-state plane. Same waveform/recovery as the
 * 4bpp path; disabled/failed MonoFast falls back to streamed six-ink OTP. */
esp_err_t screen_driver_show_mono_bitmap(const uint8_t *bitmap, size_t len);
/* Runtime switch (hueleaf.cfg mono_fast=0); no effect if compiled out. */
void screen_driver_set_mono_fast(bool on);
bool screen_driver_mono_fast_enabled(void);
/* Call before overwriting the UI frame buffer with non-UI data that may not
 * reach the panel (forces a full clear on the next MonoFast frame). */
void screen_driver_mono_stash_lost(void);
esp_err_t screen_driver_deep_sleep(void);
/* Undo a cancelled sleep preparation without repainting the retained image. */
esp_err_t screen_driver_resume(void);

#endif /* SCREEN_DRIVER_H */
