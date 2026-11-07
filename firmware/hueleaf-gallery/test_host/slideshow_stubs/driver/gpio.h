#pragma once
#include "platform.h"
typedef int gpio_num_t;
#define GPIO_NUM_4 4
int gpio_get_level(gpio_num_t pin);
