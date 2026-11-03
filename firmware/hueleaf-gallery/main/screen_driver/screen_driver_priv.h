/* Internal helpers shared between screen_driver.c and mono_fast.c.
 * Callers hold the display and board SPI locks. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

esp_err_t sd_write_cmd(uint8_t cmd);
esp_err_t sd_write_data(const uint8_t *data, size_t len);
esp_err_t sd_write_data_byte(uint8_t data);
esp_err_t sd_wait_busy(void);
int sd_busy_level(void);          /* raw BUSY pin level (0 = busy) */

/* mono_fast.c */
esp_err_t mono_fast_show_locked(uint8_t *frame4bpp);
/* true while the controller holds a RAM waveform / non-stock registers */
bool mono_fast_active(void);
void mono_fast_forget(void);
void mono_fast_drop_stash(void);  /* frame buffer reused: full clear next */      /* controller was reset / slept */
