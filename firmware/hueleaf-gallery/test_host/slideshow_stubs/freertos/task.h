#pragma once
#include "platform.h"
typedef void *TaskHandle_t;
#define pdTRUE 1
uint32_t ulTaskNotifyTake(int clear, TickType_t timeout);
void xTaskNotifyGive(TaskHandle_t task);
