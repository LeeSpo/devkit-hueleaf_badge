#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct { int subtype; size_t size; const char *label; } esp_partition_t;
typedef int esp_ota_handle_t;
#define ESP_PARTITION_SUBTYPE_APP_FACTORY 0
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *);
esp_err_t esp_ota_begin(const esp_partition_t *, size_t, esp_ota_handle_t *);
esp_err_t esp_ota_write(esp_ota_handle_t, const void *, size_t);
esp_err_t esp_ota_end(esp_ota_handle_t);
esp_err_t esp_ota_abort(esp_ota_handle_t);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *);
