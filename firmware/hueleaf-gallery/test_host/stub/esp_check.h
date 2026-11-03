#pragma once
#include "esp_err.h"
#define ESP_RETURN_ON_ERROR(x,t,m) do{esp_err_t e_=(x); if(e_) return e_;}while(0)
