#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
typedef int TickType_t;
#define pdMS_TO_TICKS(x) (x)
#define portMAX_DELAY 1000
#define pdPASS 1
#define SCREEN_FRAME_BYTES 172800
#define TF_CARD_MOUNT_POINT "/sdcard"
typedef struct { int rx_buffer_size, tx_buffer_size; } usb_serial_jtag_driver_config_t;
typedef struct { char label[32]; } esp_partition_t;
typedef struct { int year, month, day, hour, minute, second; } pcf8563_time_t;
int usb_serial_jtag_write_bytes(const void *, size_t, int);
int usb_serial_jtag_read_bytes(void *, size_t, int);
esp_err_t usb_serial_jtag_driver_install(const usb_serial_jtag_driver_config_t *);
esp_err_t usb_serial_jtag_driver_uninstall(void);
int xTaskCreate(void (*)(void *), const char *, int, void *, int, void *);
void vTaskDelay(int);
void esp_restart(void);
int esp_reset_reason(void);
int64_t esp_timer_get_time(void);
esp_err_t esp_vfs_fat_info(const char *, uint64_t *, uint64_t *);
const esp_partition_t *esp_ota_get_running_partition(void);
esp_err_t axp2101_gauge_status(bool *, bool *, int *);
esp_err_t pcf8563_set_time(const pcf8563_time_t *);
bool tf_card_is_mounted(void);
