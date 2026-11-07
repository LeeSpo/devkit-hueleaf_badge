#pragma once
#include "driver/i2c_master.h"
#define I2C_ADDR_MSA321 0x62
i2c_master_bus_handle_t board_i2c_bus(void);
void board_i2c_lock(void);
void board_i2c_unlock(void);
const char *esp_err_to_name(esp_err_t);
