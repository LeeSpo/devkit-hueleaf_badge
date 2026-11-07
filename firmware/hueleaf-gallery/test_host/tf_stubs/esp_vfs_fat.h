#pragma once
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
typedef struct {int format_if_mount_failed; int max_files; int allocation_unit_size;} esp_vfs_fat_sdmmc_mount_config_t;
esp_err_t esp_vfs_fat_sdspi_mount(const char *, const sdmmc_host_t *, const sdspi_device_config_t *, const esp_vfs_fat_sdmmc_mount_config_t *, sdmmc_card_t **);
esp_err_t esp_vfs_fat_sdcard_unmount(const char *, sdmmc_card_t *);
