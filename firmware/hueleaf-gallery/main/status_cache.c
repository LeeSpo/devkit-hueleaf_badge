#include "runtime_state.h"
#include "axp2101.h"
#include "gallery.h"
#include "tf_card.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if !CONFIG_FATFS_USE_DYN_BUFFERS
#error "Gallery requires dynamic FatFS buffers to fit the ESP32-C5 internal RAM budget"
#endif

static void status_cache_update(void)
{
    bool enabled = false, present = false;
    int battery = -1;
    if (axp2101_gauge_status(&enabled, &present, &battery) == ESP_OK)
        runtime_publish_battery(present, battery);
    if (!runtime_storage_begin(true)) return;
    uint64_t total = 0, free = 0;
    sd_lock();
    esp_err_t err = tf_card_check();
    if (err == ESP_OK) err = esp_vfs_fat_info(TF_CARD_MOUNT_POINT, &total, &free);
    sd_unlock();
    if (err == ESP_OK)
        runtime_publish_space(total / 1024, free / 1024, esp_timer_get_time() / 1000);
    else runtime_invalidate_space();
    runtime_storage_end();
}
static void status_cache_task(void *arg)
{
    (void)arg;
    for (;;) {
        status_cache_update();
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
esp_err_t status_cache_start(void)
{
    return xTaskCreate(status_cache_task, "status_cache", 3072, NULL, 2, NULL) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}
