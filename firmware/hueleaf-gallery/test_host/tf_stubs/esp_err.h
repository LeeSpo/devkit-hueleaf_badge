#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 259
#define ESP_ERR_INVALID_ARG 258
const char *esp_err_to_name(int);
