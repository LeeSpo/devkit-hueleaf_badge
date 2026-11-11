#include "buttons.h"
#include "axp2101.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gallery.h"
#include "runtime_state.h"
#include "remote.h"
#include "pin_map.h"

static const char *TAG = "btn";
#define BOOT_GPIO      EINK_ROTATE_BUTTON   /* GPIO28, active low, strap pin */
#define POLL_MS        10
#define DEBOUNCE_MS    30
#define LONG_MS        1000
static TaskHandle_t s_pek_task, s_led_task, s_boot_task;
static bool s_prepared;
typedef struct {
    int stable, cnt, held_ms;
    bool long_fired;
} boot_state_t;

static button_action_t boot_poll(boot_state_t *s)
{
    int lvl = gpio_get_level(BOOT_GPIO);
    if (lvl != s->stable) {
        if ((s->cnt += POLL_MS) < DEBOUNCE_MS) return BUTTON_NONE;
        s->stable = lvl; s->cnt = 0;
        if (lvl == 0) { s->held_ms = 0; s->long_fired = false; }
        else if (!s->long_fired) return BUTTON_BOOT_SHORT;
    } else {
        s->cnt = 0;
        if (lvl == 0 && !s->long_fired && (s->held_ms += POLL_MS) >= LONG_MS) {
            s->long_fired = true;
            return BUTTON_BOOT_LONG;
        }
    }
    return BUTTON_NONE;
}

static esp_err_t buttons_prepare(void)
{
    if (s_prepared) return ESP_OK;
    gpio_config_t boot = { .pin_bit_mask = 1ULL << BOOT_GPIO, .mode = GPIO_MODE_INPUT,
                           .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config_t pek = { .pin_bit_mask = 1ULL << pmic_irq, .mode = GPIO_MODE_INPUT,
                          .pull_up_en = GPIO_PULLUP_ENABLE, .intr_type = GPIO_INTR_NEGEDGE };
    esp_err_t e = gpio_config(&boot);
    if (e != ESP_OK) return e;
    if ((e = gpio_config(&pek)) != ESP_OK) return e;
    if ((e = axp2101_pwron_init()) != ESP_OK) return e;
    s_prepared = true;
    return ESP_OK;
}

esp_err_t buttons_poll_wake(button_action_t *action)
{
    static boot_state_t boot = { .stable = 1 };
    *action = BUTTON_NONE;
    esp_err_t err = buttons_prepare();
    if (err != ESP_OK) return err;
    *action = boot_poll(&boot);
    if (*action == BUTTON_NONE && gpio_get_level(pmic_irq) == 0) {
        bool pressed = false;
        err = axp2101_take_pwron_short_press(&pressed);
        if (err == ESP_OK && pressed) *action = BUTTON_PEK_SHORT;
    }
    return err;
}

static void feedback(void) { if (s_led_task) xTaskNotifyGive(s_led_task); }
bool buttons_dispatch(button_action_t action)
{
    if (!gallery_is_ready()) return false;
    bool accepted;
    switch (action) {
    case BUTTON_BOOT_SHORT:
        ESP_LOGI(TAG, "BOOT short"); accepted = gallery_btn_boot_short(); break;
    case BUTTON_BOOT_LONG:
        ESP_LOGI(TAG, "BOOT long"); accepted = gallery_btn_boot_long(); break;
    case BUTTON_PEK_SHORT:
        ESP_LOGI(TAG, "PEK short"); accepted = gallery_btn_pek_short(); break;
    default: return false;
    }
    if (accepted) { remote_local_activity(); feedback(); }
    else ESP_LOGI(TAG, "ignored (busy)");
    return accepted;
}
static void led_task(void *a)
{
    (void)a;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (runtime_io_begin()) {
            (void)axp2101_led_pulse(80);
            runtime_io_end();
        }
    }
}

static void boot_task(void *a)
{
    (void)a;
    boot_state_t boot = { .stable = gpio_get_level(BOOT_GPIO), .long_fired = true };
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        if (!gallery_is_ready()) {
            boot = (boot_state_t){ .stable = gpio_get_level(BOOT_GPIO), .long_fired = true };
            continue;
        }
        buttons_dispatch(boot_poll(&boot));
    }
}

static void IRAM_ATTR pek_isr(void *a)
{
    (void)a;
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
        if (!runtime_io_begin()) continue;
        bool pressed = false;
        esp_err_t err = axp2101_take_pwron_short_press(&pressed);
        runtime_io_end();
        if (err != ESP_OK) { vTaskDelay(pdMS_TO_TICKS(500)); continue; }
        if (pressed) buttons_dispatch(BUTTON_PEK_SHORT);
    }
}

esp_err_t buttons_start(void)
{
    esp_err_t e = buttons_prepare();
    if (e != ESP_OK) return e;
    e = gpio_install_isr_service(0);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;
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
