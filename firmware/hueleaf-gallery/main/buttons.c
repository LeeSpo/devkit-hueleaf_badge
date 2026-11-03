#include "buttons.h"
#include "axp2101.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gallery.h"
#include "pin_map.h"

static const char *TAG = "btn";
#define BOOT_GPIO      EINK_ROTATE_BUTTON   /* GPIO28, active low, strap pin */
#define POLL_MS        10
#define DEBOUNCE_MS    30
#define LONG_MS        1000
static TaskHandle_t s_pek_task, s_led_task;

static void feedback(void) { if (s_led_task) xTaskNotifyGive(s_led_task); }
static void led_task(void *a)
{
    (void)a;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        (void)axp2101_led_pulse(80);
    }
}

static void boot_task(void *a)
{
    (void)a;
    gpio_config_t io = { .pin_bit_mask = 1ULL << BOOT_GPIO, .mode = GPIO_MODE_INPUT,
                         .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config(&io);
    int stable = 1, cnt = 0, held_ms = 0;
    bool long_fired = false;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        int lvl = gpio_get_level(BOOT_GPIO);
        if (lvl != stable) {
            if ((cnt += POLL_MS) < DEBOUNCE_MS) continue;
            stable = lvl; cnt = 0;
            if (stable == 0) { held_ms = 0; long_fired = false; feedback(); }
            else if (!long_fired) { ESP_LOGI(TAG, "BOOT short"); gallery_btn_boot_short(); }
        } else {
            cnt = 0;
            if (stable == 0 && !long_fired && (held_ms += POLL_MS) >= LONG_MS) {
                long_fired = true;   /* fires while held: open picker / confirm */
                ESP_LOGI(TAG, "BOOT long");
                if (gallery_btn_boot_long()) feedback();
            }
        }
    }
}

static void IRAM_ATTR pek_isr(void *a)
{
    BaseType_t w = pdFALSE;
    vTaskNotifyGiveFromISR(s_pek_task, &w);
    if (w) portYIELD_FROM_ISR();
}

/* Same mechanism the stock firmware uses for its PWRON short-press action:
 * INTSTS2 bit3 (short press) via nIRQ on GPIO25; I2C read in task context. */
static void pek_task(void *a)
{
    (void)a;
    gpio_config_t io = { .pin_bit_mask = 1ULL << pmic_irq, .mode = GPIO_MODE_INPUT,
                         .pull_up_en = GPIO_PULLUP_ENABLE, .intr_type = GPIO_INTR_NEGEDGE };
    gpio_config(&io);
    esp_err_t e = gpio_install_isr_service(0);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) goto fail;
    gpio_isr_handler_add(pmic_irq, pek_isr, NULL);
    if ((e = axp2101_pwron_init()) != ESP_OK) goto fail;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, gpio_get_level(pmic_irq) == 0 ? pdMS_TO_TICKS(10) : pdMS_TO_TICKS(2000));
        if (gpio_get_level(pmic_irq) != 0) continue;
        bool pressed = false;
        if (axp2101_take_pwron_short_press(&pressed) != ESP_OK) { vTaskDelay(pdMS_TO_TICKS(500)); continue; }
        if (pressed) { ESP_LOGI(TAG, "PEK short"); feedback(); gallery_btn_pek_short(); }
    }
fail:
    ESP_LOGE(TAG, "PEK unavailable: %s", esp_err_to_name(e));
    vTaskDelete(NULL);
}

void buttons_start(void)
{
    xTaskCreate(led_task, "led", 2048, NULL, 2, &s_led_task);
    xTaskCreate(boot_task, "btn_boot", 2560, NULL, 5, NULL);
    xTaskCreate(pek_task, "btn_pek", 3072, NULL, 5, &s_pek_task);
}
