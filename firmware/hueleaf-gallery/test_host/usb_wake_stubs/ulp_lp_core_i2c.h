#pragma once
#include "platform.h"
#define LP_I2C_NUM_0 1
esp_err_t lp_core_i2c_master_write_read_device(int, uint16_t, const uint8_t *, size_t, uint8_t *, size_t, int32_t);
