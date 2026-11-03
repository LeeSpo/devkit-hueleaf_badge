#pragma once
static inline TickType_t xTaskGetTickCount(void){return 0;}
static inline void vTaskDelay(TickType_t t){(void)t;}
