#pragma once
#include <stdio.h>
#include "esp_err.h"
typedef struct {int unused;} sdmmc_card_t;
esp_err_t sdmmc_get_status(sdmmc_card_t *); void sdmmc_card_print_info(FILE *, sdmmc_card_t *);
