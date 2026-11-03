#pragma once
#include <stdint.h>
#include "freertos/FreeRTOS.h"
extern TickType_t g_ticks; extern uint32_t g_us;
static inline void esp_rom_delay_us(uint32_t us){ g_us += us; while (g_us >= 1000) { g_us -= 1000; g_ticks++; } }
