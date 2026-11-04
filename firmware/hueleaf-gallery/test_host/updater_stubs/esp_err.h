#pragma once
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NOT_FOUND 1
#define ESP_ERR_INVALID_SIZE 2
#define ESP_ERR_INVALID_VERSION 3
typedef int esp_err_t;
const char *esp_err_to_name(int);
