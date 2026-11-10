#pragma once
#include "esp_err.h"
esp_err_t board_init(void);
int board_spi_host(void);
void board_spi_lock(void);
void board_spi_unlock(void);
