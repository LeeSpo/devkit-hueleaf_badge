#pragma once
#include <stddef.h>
/* Routes ESP_LOG into a RAM ring so USB-Serial-JTAG stays a clean data link. */
void logbuf_init(void);
size_t logbuf_read(char *dst, size_t cap); /* copies (and keeps) the newest bytes */
