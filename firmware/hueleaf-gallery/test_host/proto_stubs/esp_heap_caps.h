#pragma once
#include <stddef.h>
#include <stdint.h>
#define MALLOC_CAP_8BIT 4
size_t heap_caps_get_largest_free_block(uint32_t caps);
uint32_t esp_get_free_heap_size(void);
uint32_t esp_get_minimum_free_heap_size(void);
