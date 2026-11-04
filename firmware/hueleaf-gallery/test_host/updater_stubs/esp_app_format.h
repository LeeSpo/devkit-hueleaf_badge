#pragma once
#include <stdint.h>
typedef struct { uint8_t magic; uint16_t chip_id; } esp_image_header_t;
#define ESP_IMAGE_HEADER_MAGIC 0xe9
#define ESP_CHIP_ID_ESP32C5 23
