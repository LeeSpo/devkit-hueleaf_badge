#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
static inline size_t test_strlcpy(char *d, const char *s, size_t cap)
{
    size_t n = strlen(s), k = cap && n >= cap ? cap - 1 : n;
    if (cap) { memcpy(d, s, k); d[k] = 0; }
    return n;
}
#undef strlcpy
#define strlcpy test_strlcpy
#define CONFIG_HL_REMOTE 1
#define RTC_DATA_ATTR
#define RTC_NOINIT_ATTR
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_NOT_FINISHED 0x10c
#define ESP_RST_POWERON 1
#define ESP_RST_DEEPSLEEP 8
#define ESP_RST_BROWNOUT 9
#define ESP_RST_TASK_WDT 6
static inline void test_log(const char *tag, const char *fmt, ...) { (void)tag; (void)fmt; }
#define ESP_LOGI(...) test_log(__VA_ARGS__)
#define ESP_LOGW(...) test_log(__VA_ARGS__)
#define ESP_LOGE(...) test_log(__VA_ARGS__)
#define GPIO_NUM_4 4
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
typedef unsigned TickType_t;
typedef int BaseType_t;
typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY 0xffffffffu
#define portYIELD_FROM_ISR() ((void)0)
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
int xTaskCreate(void (*)(void *), const char *, unsigned, void *, unsigned, TaskHandle_t *);
void xTaskNotifyGive(TaskHandle_t);
void vTaskNotifyGiveFromISR(TaskHandle_t, BaseType_t *);
void vTaskDelete(TaskHandle_t);
uint32_t ulTaskNotifyTake(int, TickType_t);
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t);
void vTaskDelay(TickType_t);
QueueHandle_t xQueueCreate(unsigned, unsigned);
int xQueueSend(QueueHandle_t, const void *, unsigned);
int xQueueReceive(QueueHandle_t, void *, unsigned);
typedef void *esp_pm_lock_handle_t;
typedef struct { int max_freq_mhz, min_freq_mhz; bool light_sleep_enable; } esp_pm_config_t;
#define ESP_PM_NO_LIGHT_SLEEP 2
esp_err_t esp_pm_configure(const esp_pm_config_t *);
esp_err_t esp_pm_lock_create(int, int, const char *, esp_pm_lock_handle_t *);
esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t);
esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t);
typedef enum { ESP_SLEEP_WAKEUP_UNDEFINED, ESP_SLEEP_WAKEUP_TIMER, ESP_SLEEP_WAKEUP_EXT1, ESP_SLEEP_WAKEUP_ULP } esp_sleep_wakeup_cause_t;
esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause(void);
uint64_t esp_sleep_get_ext1_wakeup_status(void);
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t);
esp_err_t esp_sleep_disable_wakeup_source(esp_sleep_wakeup_cause_t);
void esp_deep_sleep_start(void);
int esp_reset_reason(void);
int64_t esp_timer_get_time(void);
uint64_t esp_rtc_get_time_us(void);
unsigned esp_get_free_heap_size(void);
#define MALLOC_CAP_8BIT 1
unsigned heap_caps_get_largest_free_block(int);
void esp_log_set_vprintf(int (*)(const char *, va_list));
const char *esp_err_to_name(esp_err_t);
bool usb_serial_jtag_is_connected(void);
bool screen_driver_initialized(void);
esp_err_t screen_driver_deep_sleep(void);
esp_err_t screen_driver_resume(void);
esp_err_t tf_card_mount(void);
esp_err_t tf_card_unmount(void);
bool tf_card_is_mounted(void);
bool tf_card_has_fault(void);
esp_err_t tap_wake_prepare(void);
void tap_wake_release(void);
esp_err_t board_sleep_prepare(void);
void board_sleep_release(void);
typedef int nvs_handle_t;
#define NVS_READWRITE 1
#define ESP_ERR_NVS_NOT_FOUND 0x1102
esp_err_t nvs_flash_init_partition(const char *);
esp_err_t nvs_open_from_partition(const char *, const char *, int, nvs_handle_t *);
esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *, size_t *);
esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *, size_t);
esp_err_t nvs_get_u8(nvs_handle_t, const char *, uint8_t *);
esp_err_t nvs_set_u8(nvs_handle_t, const char *, uint8_t);
esp_err_t nvs_erase_key(nvs_handle_t, const char *);
esp_err_t nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
int mbedtls_sha256(const uint8_t *, size_t, uint8_t *, int);
