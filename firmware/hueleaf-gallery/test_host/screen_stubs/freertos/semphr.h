#pragma once
#include <stdbool.h>
typedef void *SemaphoreHandle_t;
#define pdTRUE 1
#define portMAX_DELAY 0xffffffff
SemaphoreHandle_t xSemaphoreCreateMutex(void);
bool xSemaphoreTake(SemaphoreHandle_t mutex, unsigned timeout);
bool xSemaphoreGive(SemaphoreHandle_t mutex);
