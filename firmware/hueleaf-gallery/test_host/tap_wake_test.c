#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../main/imu/tap.c"

static uint8_t registers[256];
static int adds, writes, tasks, io_locks, rtc_stage, fail_rtc, irq_level = 1;
static bool rtc_mux, armed;
static int init_error, write_error;

const char *esp_err_to_name(esp_err_t e) { (void)e; return "injected"; }
i2c_master_bus_handle_t board_i2c_bus(void) { return (void *)1; }
void board_i2c_lock(void) { assert(!io_locks++); }
void board_i2c_unlock(void) { assert(io_locks-- == 1); }
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus,
        const i2c_device_config_t *c, i2c_master_dev_handle_t *dev)
{
    assert(bus == (void *)1 && c->device_address == 0x62 && c->scl_speed_hz == 400000);
    adds++; *dev = (void *)2; return ESP_OK;
}
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t dev, const uint8_t *b, size_t n, int t)
{
    assert(dev == (void *)2 && io_locks == 1 && n == 2 && t == 50);
    writes++;
    if (write_error) return ESP_FAIL;
    registers[b[0]] = b[1]; return ESP_OK;
}
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t dev, const uint8_t *r,
        size_t rn, uint8_t *out, size_t n, int t)
{
    assert(dev == (void *)2 && io_locks == 1 && rn == 1 && t == 50);
    memcpy(out, registers + *r, n); return ESP_OK;
}
void vTaskDelay(int ticks) { assert(ticks > 0); }
int xTaskCreate(void (*f)(void *), const char *name, int stack, void *arg, int p, void *h)
{ (void)f; (void)name; (void)stack; (void)arg; (void)p; (void)h; tasks++; return pdPASS; }
bool gallery_picker_open(void) { return false; }
bool gallery_tap_next(void) { assert(!"wake must not navigate"); return false; }
esp_err_t axp2101_led_pulse(uint32_t ms) { (void)ms; return ESP_OK; }
int gpio_get_level(gpio_num_t pin) { assert(pin == 4 && !rtc_mux); return irq_level; }
esp_err_t rtc_gpio_init(gpio_num_t pin)
{ assert(pin == 4); if (init_error) return ESP_FAIL; rtc_mux = true; rtc_stage = 0; return ESP_OK; }
static esp_err_t config_step(gpio_num_t pin)
{ assert(pin == 4 && rtc_mux); return ++rtc_stage == fail_rtc ? ESP_FAIL : ESP_OK; }
esp_err_t rtc_gpio_set_direction(gpio_num_t pin, int mode)
{ assert(mode == RTC_GPIO_MODE_INPUT_ONLY); return config_step(pin); }
esp_err_t rtc_gpio_pulldown_dis(gpio_num_t pin) { return config_step(pin); }
esp_err_t rtc_gpio_pullup_en(gpio_num_t pin) { return config_step(pin); }
esp_err_t esp_sleep_enable_ext1_wakeup_io(uint64_t mask, esp_sleep_ext1_wakeup_mode_t mode)
{
    assert(mask == (1ULL << 4) && mode == ESP_EXT1_WAKEUP_ANY_LOW && rtc_mux);
    esp_err_t e = config_step(4); if (e == ESP_OK) armed = true; return e;
}
esp_err_t esp_sleep_disable_ext1_wakeup_io(uint64_t mask)
{ assert(mask == (1ULL << 4)); armed = false; return ESP_OK; }
esp_err_t rtc_gpio_hold_dis(gpio_num_t pin) { assert(pin == 4); return ESP_OK; }
esp_err_t rtc_gpio_deinit(gpio_num_t pin) { assert(pin == 4); rtc_mux = false; return ESP_OK; }

int main(void)
{
    tap_start(); assert(!adds && !tasks && !tap_enabled());
    registers[REG_PARTID] = 0x13;
    assert(tap_wake_prepare() == ESP_OK);
    assert(adds == 1 && tasks == 0 && armed && rtc_mux && !tap_enabled());
    assert(registers[REG_PWR] == 0x12 && registers[REG_ODR] == 0x07);
    assert(registers[REG_INT_CONFIG] == 0 && registers[REG_INT_LATCH] == 1);
    assert(registers[REG_INT_SET0] == 0x10 && registers[REG_INT_MAP0] == 0x10);
    tap_wake_release(); assert(!armed && !rtc_mux);
    int before = writes;
    assert(tap_wake_prepare() == ESP_OK && writes == before && adds == 1);
    tap_wake_release();
    tap_set_enabled(true); tap_start();
    assert(tasks == 1 && writes == before && tap_enabled()); /* independently reuse initialized IMU */
    irq_level = 0;
    assert(tap_wake_prepare() == ESP_ERR_NOT_FINISHED && !armed && !rtc_mux);
    irq_level = 1;
    for (fail_rtc = 1; fail_rtc <= 4; fail_rtc++) {
        assert(tap_wake_prepare() == ESP_FAIL && !armed && !rtc_mux && !s_wake_prepared);
    }
    fail_rtc = 0; init_error = 1;
    assert(tap_wake_prepare() == ESP_FAIL && !armed && !rtc_mux);
    init_error = 0; s_initialized = false; registers[REG_PARTID] = 0;
    assert(tap_wake_prepare() == ESP_ERR_INVALID_RESPONSE && !armed && !rtc_mux);
    registers[REG_PARTID] = 0x13; write_error = 1;
    assert(tap_wake_prepare() == ESP_FAIL && !armed && !rtc_mux);
    write_error = 0;
    assert(tap_wake_prepare() == ESP_OK && adds == 1);
    tap_wake_release(); tap_wake_release();
    puts("OK IMU tap wake: normal mode, independent init, active-low EXT1, rollback");
    return 0;
}
