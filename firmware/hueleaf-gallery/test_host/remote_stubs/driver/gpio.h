#pragma once
#include "platform.h"
typedef int gpio_num_t;
#define GPIO_NUM_25 25
#define GPIO_NUM_28 28
#define GPIO_MODE_INPUT 1
#define GPIO_PULLUP_ENABLE 1
#define GPIO_INTR_NEGEDGE 2
#define IRAM_ATTR
typedef struct {
    uint64_t pin_bit_mask;
    int mode, pull_up_en, intr_type;
} gpio_config_t;
int gpio_get_level(gpio_num_t);
esp_err_t gpio_config(const gpio_config_t *);
esp_err_t gpio_install_isr_service(int);
esp_err_t gpio_isr_handler_add(gpio_num_t, void (*)(void *), void *);
