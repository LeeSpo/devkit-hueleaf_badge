#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#define BOARD_SPI_HOST  SPI2_HOST
#define BOARD_I2C_PORT  I2C_NUM_0

#define I2C_ADDR_AXP2101    0x34
#define I2C_ADDR_MSA321     0x62
#define I2C_ADDR_PCF8563    0x51
#define I2C_ADDR_ST25R3916  0x50

esp_err_t board_init(void);
esp_err_t board_sleep_prepare(void);
/* Startup before any bus/device init: drive idle levels, then release holds. */
void board_sleep_boot_release(void);
/* After a cancelled sleep: release holds only, keeping SPI pin routing. */
void board_sleep_release(void);
spi_host_device_t board_spi_host(void);
i2c_master_bus_handle_t board_i2c_bus(void);
void board_i2c_lock(void);
void board_i2c_unlock(void);
/* Caller holds I2C lock: restore HP routing after cancelled LP handoff. */
void board_i2c_restore(void);
void board_spi_lock(void);
void board_spi_unlock(void);
void board_tf_cs_idle(void);
int board_i2c_scan(const char *why);
void board_nfc_set_enable(bool enable);
int board_nfc_en_pin_level(void);

#endif /* BOARD_H */
