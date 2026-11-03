#pragma once
#include <stdint.h>
#include "freertos/FreeRTOS.h"
extern TickType_t g_ticks; extern uint32_t g_us;
static inline int64_t esp_timer_get_time(void){ return (int64_t)g_ticks * 1000 + g_us; }
