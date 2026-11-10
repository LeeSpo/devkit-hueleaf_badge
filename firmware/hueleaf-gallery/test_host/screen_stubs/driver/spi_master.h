#pragma once
#include <stddef.h>
#include "esp_err.h"
typedef void *spi_device_handle_t;
typedef struct { size_t length; const void *tx_buffer; void *user; } spi_transaction_t;
typedef struct { int clock_speed_hz, mode, spics_io_num, queue_size; void (*pre_cb)(spi_transaction_t *); int flags; } spi_device_interface_config_t;
#define SPI_DEVICE_NO_DUMMY 0
esp_err_t spi_device_polling_transmit(spi_device_handle_t spi, spi_transaction_t *t);
esp_err_t spi_bus_add_device(int host, const spi_device_interface_config_t *config, spi_device_handle_t *spi);
