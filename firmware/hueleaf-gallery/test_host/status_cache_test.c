#include <assert.h>
#include <stdint.h>
#include "platform.h"
#include STATUS_CACHE_SOURCE

static int probes, fat_reads, locks;
static bool fail;
esp_err_t axp2101_gauge_status(bool *enabled, bool *present, int *soc)
{ *enabled=true; *present=true; *soc=87; return ESP_OK; }
void sd_lock(void) { locks++; }
void sd_unlock(void) { locks--; }
esp_err_t tf_card_check(void) { probes++; return fail ? ESP_FAIL : ESP_OK; }
esp_err_t esp_vfs_fat_info(const char *p, uint64_t *total, uint64_t *free)
{ (void)p; fat_reads++; *total=102400; *free=51200; return ESP_OK; }
int64_t esp_timer_get_time(void) { return 1000000; }
int xTaskCreate(void (*f)(void *), const char *n, int stack, void *arg, int priority, void *h)
{ (void)f; (void)n; (void)stack; (void)arg; (void)priority; (void)h; return pdPASS; }
void vTaskDelay(int ticks) { (void)ticks; }

int main(void)
{
    runtime_snapshot_t s;
    status_cache_update(); assert(probes==0 && fat_reads==0);
    runtime_set_ready(true);
    assert(runtime_display_begin()); status_cache_update();
    assert(probes==0 && fat_reads==0); runtime_display_end();
    assert(runtime_session_begin(0)); status_cache_update();
    assert(probes==0 && fat_reads==0); runtime_session_end();
    status_cache_update(); runtime_snapshot(&s);
    assert(probes==1 && fat_reads==1 && locks==0);
    assert(s.cache_valid && s.total_kb==100 && s.free_kb==50 && s.battery==87);
    fail=true; status_cache_update(); runtime_snapshot(&s);
    assert(!s.cache_valid && probes==2 && fat_reads==1 && !s.storage && locks==0);
    assert(status_cache_start()==ESP_OK);
    return 0;
}
