#include "logbuf.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#define LOG_RING 4096
static char s_ring[LOG_RING];
static size_t s_head, s_count;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static int ring_vprintf(const char *fmt, va_list ap)
{
    char line[160];
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    if (n <= 0) return n;
    size_t len = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1;
    portENTER_CRITICAL(&s_mux);
    for (size_t i = 0; i < len; i++) {
        s_ring[s_head] = line[i];
        s_head = (s_head + 1) % LOG_RING;
        if (s_count < LOG_RING) s_count++;
    }
    portEXIT_CRITICAL(&s_mux);
    return n;
}
void logbuf_init(void) { esp_log_set_vprintf(ring_vprintf); }
size_t logbuf_read(char *dst, size_t cap)
{
    portENTER_CRITICAL(&s_mux);
    size_t n = s_count < cap ? s_count : cap;
    size_t start = (s_head + LOG_RING - n) % LOG_RING;
    for (size_t i = 0; i < n; i++) dst[i] = s_ring[(start + i) % LOG_RING];
    portEXIT_CRITICAL(&s_mux);
    return n;
}
