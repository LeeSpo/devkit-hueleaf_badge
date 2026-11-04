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
static TaskHandle_t s_pek_task, s_led_task, s_boot_task;

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
    int stable = 1, cnt = 0, held_ms = 0;
    bool long_fired = false;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        if (!gallery_is_ready()) { stable = 1; cnt = held_ms = 0; long_fired = true; continue; }
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
    for (;;) {
        ulTaskNotifyTake(pdTRUE, gpio_get_level(pmic_irq) == 0 ? pdMS_TO_TICKS(10) : pdMS_TO_TICKS(2000));
        if (gpio_get_level(pmic_irq) != 0) continue;
        bool pressed = false;
        if (axp2101_take_pwron_short_press(&pressed) != ESP_OK) { vTaskDelay(pdMS_TO_TICKS(500)); continue; }
        if (pressed && gallery_is_ready()) { ESP_LOGI(TAG, "PEK short"); feedback(); gallery_btn_pek_short(); }
    }
}

esp_err_t buttons_start(void)
{
    gpio_config_t boot = { .pin_bit_mask = 1ULL << BOOT_GPIO, .mode = GPIO_MODE_INPUT,
                           .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config_t pek = { .pin_bit_mask = 1ULL << pmic_irq, .mode = GPIO_MODE_INPUT,
                          .pull_up_en = GPIO_PULLUP_ENABLE, .intr_type = GPIO_INTR_NEGEDGE };
    esp_err_t e = gpio_config(&boot);
    if (e != ESP_OK) return e;
    if ((e = gpio_config(&pek)) != ESP_OK) return e;
    e = gpio_install_isr_service(0);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;
    if ((e = axp2101_pwron_init()) != ESP_OK) return e;
    if (xTaskCreate(led_task, "led", 2048, NULL, 2, &s_led_task) != pdPASS) goto fail;
    if (xTaskCreate(boot_task, "btn_boot", 2560, NULL, 5, &s_boot_task) != pdPASS) goto fail;
    if (xTaskCreate(pek_task, "btn_pek", 3072, NULL, 5, &s_pek_task) != pdPASS) goto fail;
    e = gpio_isr_handler_add(pmic_irq, pek_isr, NULL);
    if (e != ESP_OK) goto fail;
    return ESP_OK;
fail:
    if (s_pek_task) { vTaskDelete(s_pek_task); s_pek_task = NULL; }
    if (s_boot_task) { vTaskDelete(s_boot_task); s_boot_task = NULL; }
    if (s_led_task) { vTaskDelete(s_led_task); s_led_task = NULL; }
    return e == ESP_OK ? ESP_ERR_NO_MEM : e;
}
