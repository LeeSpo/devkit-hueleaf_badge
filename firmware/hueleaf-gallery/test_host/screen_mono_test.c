#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "pin_map.h"
#include "screen_driver.h"
#include "screen_driver_priv.h"

TickType_t g_ticks;
uint32_t g_us;
static bool spi_locked, mutex_locked, ram_lut, initializing;
static unsigned busy_reads, dtms, refreshes, resets, dtm_bytes;
static uint8_t last_cmd, got[SCREEN_FRAME_BYTES];
static size_t psr_bytes;
static int fail_after = -1;
static bool stuck_otp_refresh;

esp_err_t board_init(void) { return ESP_OK; }
int board_spi_host(void) { return 2; }
void board_spi_lock(void) { assert(!spi_locked); spi_locked = true; }
void board_spi_unlock(void) { assert(spi_locked); spi_locked = false; }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return &mutex_locked; }
bool xSemaphoreTake(SemaphoreHandle_t m, unsigned t) { (void)m; (void)t; assert(!mutex_locked); mutex_locked = true; return true; }
bool xSemaphoreGive(SemaphoreHandle_t m) { (void)m; assert(mutex_locked); mutex_locked = false; return true; }
esp_err_t gpio_config(const gpio_config_t *c) { (void)c; return ESP_OK; }
void gpio_set_level(int pin, int level)
{
    if (pin == eink_rst && !level) { resets++; ram_lut = false; busy_reads = 0; }
}
int gpio_get_level(int pin)
{
    assert(pin == eink_busy);
    if (busy_reads) { busy_reads--; return 0; }
    return 1;
}
esp_err_t spi_bus_add_device(int host, const spi_device_interface_config_t *config, spi_device_handle_t *spi)
{ (void)host; (void)config; *spi = &spi_locked; return ESP_OK; }
esp_err_t spi_device_polling_transmit(spi_device_handle_t spi, spi_transaction_t *t)
{
    (void)spi; assert(spi_locked && (mutex_locked || initializing));
    const uint8_t *data = t->tx_buffer;
    size_t len = t->length/8;
    if (!t->user) {
        assert(len == 1); last_cmd = data[0];
        psr_bytes = 0;
        if (last_cmd == 0x10) { dtms++; dtm_bytes = 0; }
        if (last_cmd == 0x12) refreshes++;
        if (last_cmd == 0x04 || last_cmd == 0x02 || last_cmd == 0x12)
            busy_reads = stuck_otp_refresh && last_cmd == 0x12 && !ram_lut ? 0 : 1;
    } else if (last_cmd == 0x00) {
        for (size_t i=0;i<len;i++) if (++psr_bytes == 2) ram_lut = data[i] == 0xa9;
    } else if (last_cmd == 0x10) {
        if (ram_lut && fail_after >= 0 && fail_after-- == 0) return ESP_FAIL;
        assert(dtm_bytes + len <= SCREEN_FRAME_BYTES);
        memcpy(got + dtm_bytes, data, len); dtm_bytes += len;
    }
    return ESP_OK;
}

static uint8_t bitmap[SCREEN_MONO_BYTES];
static void assert_otp_bitmap(void)
{
    assert(dtm_bytes == SCREEN_FRAME_BYTES && !ram_lut && !mono_fast_active());
    for (int col = 0; col < SCREEN_WIDTH; col++) for (int y = 0; y < SCREEN_HEIGHT; y += 2) {
        size_t pixel = (SCREEN_WIDTH - 1 - col) * SCREEN_HEIGHT + y;
        uint8_t a = !!(bitmap[pixel/8] & (0x80 >> (pixel%8)));
        pixel++;
        uint8_t b = !!(bitmap[pixel/8] & (0x80 >> (pixel%8)));
        assert(got[col * (SCREEN_HEIGHT/2) + y/2] == (a << 4 | b));
    }
}
#if CONFIG_HL_MONO_FAST
static esp_err_t card_read(void *ctx, uint8_t *out, size_t cap, size_t *received)
{
    (void)ctx;
    /* TF can own SPI2 here; display mutex stays held across chunks. */
    assert(!spi_locked && mutex_locked);
    memset(out, 0x55, cap); *received = cap; return ESP_OK;
}
#endif
static void assert_unlocked(void) { assert(!spi_locked && !mutex_locked); }
int main(void)
{
    assert(screen_driver_show_mono_bitmap(bitmap, sizeof(bitmap)) == ESP_ERR_INVALID_STATE);
    initializing = true;
    assert(screen_driver_init() == ESP_OK); assert_unlocked();
    initializing = false;
    assert(screen_driver_show_mono_bitmap(NULL, sizeof(bitmap)) == ESP_ERR_INVALID_STATE);
    assert(screen_driver_show_mono_bitmap(bitmap, sizeof(bitmap)-1) == ESP_ERR_INVALID_SIZE);
    for (size_t i=0;i<sizeof(bitmap);i++) bitmap[i] = (uint8_t)(i*37+3);
#if !CONFIG_HL_MONO_FAST
    assert(!screen_driver_mono_fast_enabled());
    assert(screen_driver_show_mono_bitmap(bitmap, sizeof(bitmap)) == ESP_OK);
    assert_otp_bitmap(); assert_unlocked();
    screen_driver_set_mono_fast(true);
    assert(!screen_driver_mono_fast_enabled());
    assert(screen_driver_show_mono_bitmap(bitmap, sizeof(bitmap)) == ESP_OK);
    assert_otp_bitmap(); assert_unlocked();
    puts("OK compact UI with MonoFast compiled out: correctly oriented OTP stream");
#else
    uint32_t generation = screen_driver_generation();
    assert(screen_driver_show_mono_bitmap(bitmap, sizeof(bitmap)) == ESP_OK);
    assert(mono_fast_active() && ram_lut && dtms == 2 && refreshes == 2);
    assert(screen_driver_generation() != generation); assert_unlocked();
    unsigned before = dtms;
    assert(screen_driver_show_mono_bitmap(bitmap, sizeof(bitmap)) == ESP_OK);
    assert(dtms == before + 1); assert_unlocked();
    /* Card streaming must restore OTP after fast UI, release SPI for TF. */
    unsigned reset_before = resets;
    assert(screen_driver_show_card_stream(card_read, NULL) == ESP_OK);
    assert(!mono_fast_active() && !ram_lut && resets > reset_before);
    for (size_t i=0;i<sizeof(got);i++) assert(got[i] == 0x55);
    assert_unlocked(); before = dtms;
    assert(screen_driver_show_mono_bitmap(bitmap, sizeof(bitmap)) == ESP_OK);
    assert(dtms == before+2 && mono_fast_active()); assert_unlocked();
    screen_driver_set_mono_fast(false);
    assert(screen_driver_show_mono_bitmap(bitmap, sizeof(bitmap)) == ESP_OK);
    assert_otp_bitmap(); assert_unlocked();
    screen_driver_set_mono_fast(true);
    assert(screen_driver_show_mono_bitmap(bitmap, sizeof(bitmap)) == ESP_OK);
    /* Partial RAM plane triggers recovery and correctly oriented OTP fallback. */
    fail_after = 3;
    assert(screen_driver_show_mono_bitmap(bitmap, sizeof(bitmap)) == ESP_OK);
    assert_otp_bitmap(); assert_unlocked(); before = dtms;
    assert(screen_driver_show_mono_bitmap(bitmap, sizeof(bitmap)) == ESP_OK);
    assert(dtms == before+2 && mono_fast_active()); assert_unlocked();
    screen_driver_mono_stash_lost(); before = dtms;
    assert(screen_driver_show_mono_bitmap(bitmap, sizeof(bitmap)) == ESP_OK);
    assert(dtms == before+2);
    assert(screen_driver_deep_sleep() == ESP_OK && !mono_fast_active()); assert_unlocked();
    assert(screen_driver_resume() == ESP_OK && !mono_fast_active()); assert_unlocked();
    before = dtms;
    assert(screen_driver_show_mono_bitmap(bitmap, sizeof(bitmap)) == ESP_OK);
    assert(dtms == before+2);
    /* An unsuccessful physical OTP fallback must not report a completed UI. */
    fail_after = 0; stuck_otp_refresh = true;
    assert(screen_driver_show_mono_bitmap(bitmap, sizeof(bitmap)) == ESP_ERR_TIMEOUT);
    assert(!mono_fast_active()); assert_unlocked();
    stuck_otp_refresh = false;
    assert(screen_driver_show_mono_bitmap(bitmap, sizeof(bitmap)) == ESP_OK);
    assert(mono_fast_active()); assert_unlocked();
    puts("OK actual screen driver: compact UI/OTP fallback, shared TF SPI, colour transitions, sleep/resume, failure propagation");
#endif
    return 0;
}
