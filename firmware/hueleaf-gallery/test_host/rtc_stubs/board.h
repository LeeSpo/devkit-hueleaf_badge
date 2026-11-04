#ifndef BOARD_H
#define BOARD_H
#include <stdbool.h>
#include "driver/i2c_master.h"
#define I2C_ADDR_PCF8563 0x51
i2c_master_bus_handle_t board_i2c_bus(void);
void board_i2c_lock(void);
void board_i2c_unlock(void);
#endif
