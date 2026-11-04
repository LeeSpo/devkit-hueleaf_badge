#pragma once
typedef struct {int slot; int max_freq_khz;} sdmmc_host_t;
typedef struct {int host_id; int gpio_cs; int wait_for_miso;} sdspi_device_config_t;
#define SDSPI_HOST_DEFAULT() (sdmmc_host_t){0}
#define SDSPI_DEVICE_CONFIG_DEFAULT() (sdspi_device_config_t){0}
