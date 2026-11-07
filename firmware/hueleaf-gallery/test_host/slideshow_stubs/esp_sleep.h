#pragma once
#include "platform.h"
typedef enum { ESP_SLEEP_WAKEUP_UNDEFINED, ESP_SLEEP_WAKEUP_TIMER } esp_sleep_wakeup_cause_t;
esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause(void);
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t);
esp_err_t esp_sleep_disable_wakeup_source(esp_sleep_wakeup_cause_t);
void esp_deep_sleep_start(void);
