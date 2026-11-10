#pragma once
#include "platform.h"
#define LP_I2C_NUM_0 1
typedef struct { int dummy; } lp_core_i2c_cfg_t;
#define LP_CORE_I2C_DEFAULT_CONFIG() {0}
esp_err_t lp_core_i2c_master_init(int, const lp_core_i2c_cfg_t *);
