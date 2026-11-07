#pragma once
#include "platform.h"
bool tf_card_has_fault(void);
esp_err_t tf_card_unmount(void);
esp_err_t tf_card_mount(void);
