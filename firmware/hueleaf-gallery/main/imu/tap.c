/* MSA321 (MSA311 register map): hardware double-tap detection, polled.
 * Software guard (tap_guard.c) rejects taps unless the frame was still first. */
#include "tap.h"
#include "tap_guard.h"
#include "axp2101.h"
#include "board.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gallery.h"
#include "runtime_state.h"

static const char *TAG = "tap";
#define TMO 50
#define REG_SOFTRESET 0x00
#define REG_PARTID    0x01
#define REG_ACC_X_L   0x02
#define REG_MOTION    0x09   /* bit4 d_tap, bit5 s_tap */
#define REG_RANGE     0x0F
#define REG_ODR       0x10
#define REG_PWR       0x11
#define REG_INT_SET0  0x16   /* bit4 d_tap_int_en */
#define REG_INT_MAP0  0x19   /* bit4 int1_d_tap */
#define REG_INT_LATCH 0x21
#define REG_TAP_DUR   0x2A
#define REG_TAP_TH    0x2B
#define MOTION_DTAP   0x10

static bool s_on;
static i2c_master_dev_handle_t s_dev;

void tap_set_enabled(bool on) { s_on = on; }
bool tap_enabled(void) { return s_on; }

static esp_err_t wr(uint8_t r, uint8_t v)
{
    uint8_t b[2] = { r, v };
    board_i2c_lock();
    esp_err_t e = i2c_master_transmit(s_dev, b, 2, TMO);
    board_i2c_unlock();
    return e;
}
static esp_err_t rd(uint8_t r, uint8_t *d, size_t n)
{
    board_i2c_lock();
    esp_err_t e = i2c_master_transmit_receive(s_dev, &r, 1, d, n, TMO);
    board_i2c_unlock();
    return e;
}

static esp_err_t imu_init(void)
{
    i2c_device_config_t c = { .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                              .device_address = I2C_ADDR_MSA321, .scl_speed_hz = 400000 };
    esp_err_t e = i2c_master_bus_add_device(board_i2c_bus(), &c, &s_dev);
    if (e != ESP_OK) return e;
    if ((e = wr(REG_SOFTRESET, 0x24)) != ESP_OK) return e;
    vTaskDelay(pdMS_TO_TICKS(10));
    uint8_t id = 0;
    if ((e = rd(REG_PARTID, &id, 1)) != ESP_OK) return e;
    if (id != 0x13) ESP_LOGW(TAG, "PARTID 0x%02X (expected 0x13)", id);
    const uint8_t seq[][2] = {
        { REG_RANGE, 0x01 },      /* +-4g */
        { REG_ODR, 0x07 },        /* 125 Hz */
        { REG_PWR, 0x12 },        /* normal */
        { REG_TAP_DUR, 0x04 },    /* quiet 30ms, shock 50ms, 2nd tap within 250ms */
        { REG_TAP_TH, 0x0A },     /* 10 x 125mg = 1.25g at 4g range */
        { REG_INT_LATCH, 0x01 },  /* hold status 250 ms so a 50 ms poll sees it */
        { REG_INT_SET0, 0x10 },   /* double tap */
        { REG_INT_MAP0, 0x10 },
    };
    for (size_t i = 0; i < sizeof(seq) / sizeof(seq[0]); i++)
        if ((e = wr(seq[i][0], seq[i][1])) != ESP_OK) return e;
    return ESP_OK;
}

static int16_t mg(uint8_t lo, uint8_t hi)   /* 12-bit, 512 LSB/g at 4g */
{
    int16_t raw = (int16_t)(((int16_t)((hi << 8) | lo)) >> 4);
    return (int16_t)(raw * 1000 / 512);
}

static void tap_task(void *a)
{
    (void)a;
    static tap_guard_t g;
    tap_guard_init(&g);
    bool prev_dtap = false;
    int errs = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(TAP_GUARD_TICK_MS));
        if (!runtime_io_begin()) continue;
        uint8_t acc[6], mot = 0;
        if (rd(REG_ACC_X_L, acc, 6) != ESP_OK || rd(REG_MOTION, &mot, 1) != ESP_OK) {
            if (++errs == 20) ESP_LOGW(TAG, "IMU read failing");
            runtime_io_end();
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        errs = 0;
        const bool d = (mot & MOTION_DTAP) != 0;
        const bool edge = d && !prev_dtap;   /* latched for 250 ms: count once */
        prev_dtap = d;
        if (tap_guard_tick(&g, mg(acc[0], acc[1]), mg(acc[2], acc[3]), mg(acc[4], acc[5]), edge,
                           gallery_picker_open() ? TAP_GUARD_STILL_SHORT_MS : TAP_GUARD_STILL_MS)) {
            if (gallery_tap_next()) {
                ESP_LOGI(TAG, "double tap accepted");
                (void)axp2101_led_pulse(80);
            } else {
                ESP_LOGI(TAG, "double tap ignored (refreshing)");
            }
        } else if (edge) {
            ESP_LOGI(TAG, "double tap ignored (moving / cooldown)");
        }
        runtime_io_end();
    }
}

void tap_start(void)
{
    if (!s_on) { ESP_LOGI(TAG, "TapNext: OFF"); return; }
    esp_err_t e = imu_init();
    if (e != ESP_OK) { ESP_LOGE(TAG, "IMU init: %s, TapNext disabled", esp_err_to_name(e)); return; }
    xTaskCreate(tap_task, "tap", 3072, NULL, 4, NULL);
    ESP_LOGI(TAG, "TapNext: ON");
}
