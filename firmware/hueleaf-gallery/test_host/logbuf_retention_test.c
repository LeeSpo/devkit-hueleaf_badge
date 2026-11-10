#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "platform.h"
#include "../main/logbuf.c"
static int reason = ESP_RST_POWERON;
static int (*logger)(const char *, va_list);
int esp_reset_reason(void) { return reason; }
void esp_log_set_vprintf(int (*fn)(const char *, va_list)) { logger = fn; }
static void log_line(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt); logger(fmt, ap); va_end(ap);
}
int main(void)
{
    char out[LOG_RING + 1];
    logbuf_init(); log_line("sleep timer=-1 image=one.bin\n");
    reason = ESP_RST_DEEPSLEEP; logbuf_init();
    size_t n = logbuf_read(out, LOG_RING); out[n] = 0;
    assert(strstr(out, "sleep timer=-1 image=one.bin"));
    reason = ESP_RST_TASK_WDT; logbuf_init();
    n = logbuf_read(out, LOG_RING); out[n] = 0;
    assert(strstr(out, "image=one.bin"));
    for (unsigned i=0;i<LOG_RING;i++) log_line("x");
    assert(logbuf_read(out, sizeof(out)) == LOG_RING);
    s_log.head = LOG_RING; logbuf_init(); assert(logbuf_read(out, sizeof(out)) == 0);
    log_line("old"); reason = ESP_RST_POWERON; logbuf_init();
    assert(logbuf_read(out, sizeof(out)) == 0);
    log_line("old"); reason = ESP_RST_BROWNOUT; logbuf_init();
    assert(logbuf_read(out, sizeof(out)) == 0);
    puts("OK RTC diagnostics: sleep/watchdog retention, bounded ring, invalid/cold state rejection");
    return 0;
}
