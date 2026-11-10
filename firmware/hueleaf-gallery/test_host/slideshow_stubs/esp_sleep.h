#pragma once
#include "platform.h"
typedef enum { ESP_SLEEP_WAKEUP_UNDEFINED, ESP_SLEEP_WAKEUP_TIMER, ESP_SLEEP_WAKEUP_EXT1, ESP_SLEEP_WAKEUP_ULP } esp_sleep_wakeup_cause_t;
typedef enum { ESP_EXT1_WAKEUP_ANY_LOW, ESP_EXT1_WAKEUP_ANY_HIGH } esp_sleep_ext1_wakeup_mode_t;
uint64_t esp_sleep_get_ext1_wakeup_status(void);
esp_err_t esp_sleep_enable_ext1_wakeup_io(uint64_t, esp_sleep_ext1_wakeup_mode_t);
esp_err_t esp_sleep_disable_ext1_wakeup_io(uint64_t);
esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause(void);
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t);
esp_err_t esp_sleep_disable_wakeup_source(esp_sleep_wakeup_cause_t);
void esp_deep_sleep_start(void);
