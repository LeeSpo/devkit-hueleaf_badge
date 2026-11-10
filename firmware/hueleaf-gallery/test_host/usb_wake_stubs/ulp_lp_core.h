#pragma once
#include "platform.h"
#define ULP_LP_CORE_WAKEUP_SOURCE_LP_TIMER 16
#define CONFIG_PM_ENABLE 1
typedef struct { uint32_t wakeup_source, lp_timer_sleep_duration_us; } ulp_lp_core_cfg_t;
void ulp_lp_core_stop(void);
esp_err_t ulp_lp_core_load_binary(const uint8_t *, size_t);
esp_err_t ulp_lp_core_run(ulp_lp_core_cfg_t *);
