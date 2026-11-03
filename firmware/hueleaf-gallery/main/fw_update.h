#pragma once
#include "esp_err.h"
#define FW_UPDATE_PATH "/sdcard/update/firmware.bin"
/* If FW_UPDATE_PATH exists: write it to the next OTA slot (never factory),
 * verify, rename to .done, set boot partition and reboot. On failure the file
 * is renamed to .failed so it can't loop. Returns only if nothing to do/failed. */
esp_err_t fw_update_from_tf(void);
