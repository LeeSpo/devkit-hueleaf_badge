#pragma once
#include "esp_err.h"
#define FW_UPDATE_PATH "/sdcard/update/firmware.bin"
/* If FW_UPDATE_PATH exists: write it to the next OTA slot (never factory),
 * verify, set boot partition, retire it to .done (old .done deleted first;
 * if rename fails the file is deleted) and reboot. On failure -> .failed. Size +
 * SHA-256 of the handled file is kept in NVS so it is never applied twice even
 * if it cannot be renamed or deleted. On failure the file
 * is renamed to .failed so it can't loop. Returns only if nothing to do/failed. */
esp_err_t fw_update_from_tf(void);
