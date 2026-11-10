#include "logbuf.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#define LOG_RING 4096
#define LOG_MAGIC 0x4c4f4731u
/* Preserve the last sleep/refresh evidence across deep sleep and watchdog
 * resets, without writing credentials/NVS or wearing flash on every event. */
static RTC_NOINIT_ATTR struct {
    uint32_t magic, inverse;
    size_t head, count;
    char ring[LOG_RING];
} s_log;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static int ring_vprintf(const char *fmt, va_list ap)
{
    char line[160];
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    if (n <= 0) return n;
    size_t len = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1;
    portENTER_CRITICAL(&s_mux);
    for (size_t i = 0; i < len; i++) {
        s_log.ring[s_log.head] = line[i];
        s_log.head = (s_log.head + 1) % LOG_RING;
        if (s_log.count < LOG_RING) s_log.count++;
    }
    portEXIT_CRITICAL(&s_mux);
    return n;
}
void logbuf_init(void)
{
    int reset = esp_reset_reason();
    if (reset == ESP_RST_POWERON || reset == ESP_RST_BROWNOUT ||
        s_log.magic != LOG_MAGIC || s_log.inverse != ~LOG_MAGIC ||
        s_log.head >= LOG_RING || s_log.count > LOG_RING)
        memset(&s_log, 0, sizeof(s_log));
    s_log.magic = LOG_MAGIC; s_log.inverse = ~LOG_MAGIC;
    esp_log_set_vprintf(ring_vprintf);
    ESP_LOGI("logbuf", "boot reset=%d; retained log bytes=%u", reset, (unsigned)s_log.count);
}
size_t logbuf_read(char *dst, size_t cap)
{
    portENTER_CRITICAL(&s_mux);
    size_t n = s_log.count < cap ? s_log.count : cap;
    size_t start = (s_log.head + LOG_RING - n) % LOG_RING;
    for (size_t i = 0; i < n; i++) dst[i] = s_log.ring[(start + i) % LOG_RING];
    portEXIT_CRITICAL(&s_mux);
    return n;
}
