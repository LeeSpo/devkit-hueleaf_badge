#pragma once
#include <stdint.h>
/* Shared by app and bootloader hook (bootloader_components/hl_boot_select). */
#define HL_STOCK_MAGIC 0x4B545331u /* "1STK" */
#ifndef BOOTLOADER_BUILD
/* Arm the one-shot flag and restart into the stock factory app. */
void boot_select_restart_to_stock(void) __attribute__((noreturn));
#endif
