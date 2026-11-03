#pragma once
extern TickType_t g_ticks;   /* host: advances only when the code waits */
static inline TickType_t xTaskGetTickCount(void){return g_ticks;}
static inline void vTaskDelay(TickType_t t){g_ticks += t;}
