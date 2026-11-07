#pragma once
#include "platform.h"
esp_err_t board_sleep_prepare(void);
void board_sleep_boot_release(void);
void board_sleep_release(void);
const char *esp_err_to_name(esp_err_t);
