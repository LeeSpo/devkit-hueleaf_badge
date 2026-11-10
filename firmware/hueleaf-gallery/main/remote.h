#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif
#include "remote_policy.h"
#ifdef CONFIG_HL_REMOTE
/* Called after board hold release, before board init. Validated RTC marker only. */
bool remote_boot_init(bool pending);
bool remote_light_boot(void);
esp_err_t remote_init(void);
/* Wait on BLE only; returns after authenticated connection or USB attachment. */
esp_err_t remote_wait(void);
esp_err_t remote_start(void);
void remote_ready(void);
void remote_startup_failed(void);
void remote_activity(void);
bool remote_managed(void);
void remote_frame_done(void);
void remote_render_done(bool success);
void remote_snapshot(char *out, size_t cap);
esp_err_t remote_configure(const uint8_t *data, size_t len);
void remote_image(char *out, size_t cap);
#else
static inline bool remote_light_boot(void) { return false; }
static inline bool remote_managed(void) { return false; }
static inline void remote_activity(void) {}
static inline void remote_frame_done(void) {}
static inline void remote_render_done(bool success) { (void)success; }
#endif
