#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "pcf8563.h"

static uint8_t regs[16] = {0, 0, 0x56, 0x34, 0x12, 0x29, 4, 0x02, 0x24};
static int writes, time_writes, adds, removes, locks;
static int fail_add, fail_read, fail_stop, fail_time, fail_restart;
static int bus_available = 1;

i2c_master_bus_handle_t board_i2c_bus(void) { return bus_available ? regs : NULL; }
void board_i2c_lock(void) { assert(locks++ == 0); }
void board_i2c_unlock(void) { assert(--locks == 0); }
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t *cfg, i2c_master_dev_handle_t *dev)
{
    assert(bus && cfg->device_address == 0x51 && cfg->scl_speed_hz == 400000);
    adds++;
    if (fail_add) { fail_add = 0; return ESP_FAIL; }
    *dev = regs;
    return ESP_OK;
}
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t dev)
{
    assert(dev == regs); removes++; return ESP_OK;
}
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t dev, const uint8_t *data, size_t len, int timeout)
{
    assert(dev == regs && locks == 1 && timeout == 50 && len >= 2 && data[0] + len - 1 <= sizeof(regs));
    writes++;
    if (data[0] == 2) { time_writes++; if (fail_time) { fail_time = 0; return ESP_FAIL; } }
    if (data[0] == 0 && data[1] == 0x20 && fail_stop) { fail_stop = 0; return ESP_FAIL; }
    if (data[0] == 0 && data[1] == 0 && fail_restart) { fail_restart = 0; return ESP_ERR_INVALID_STATE; }
    memcpy(regs + data[0], data + 1, len - 1);
    return ESP_OK;
}
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t dev, const uint8_t *addr, size_t alen, uint8_t *out, size_t len, int timeout)
{
    assert(dev == regs && locks == 1 && timeout == 50 && alen == 1 && *addr + len <= sizeof(regs));
    if (fail_read) { fail_read = 0; return ESP_FAIL; }
    memcpy(out, regs + *addr, len);
    return ESP_OK;
}
static void check_seed(void)
{
    pcf8563_time_t t;
    assert(pcf8563_init() == ESP_OK);
    assert(time_writes == 1 && writes == 3 && regs[0] == 0);
    assert(pcf8563_get_time(&t) == ESP_OK && t.year >= 2024 && t.year <= 2099);
    assert(!(regs[2] & 0x80));
}
static void check_set_invalid(pcf8563_time_t t)
{
    int before = writes;
    assert(pcf8563_set_time(&t) == ESP_ERR_INVALID_ARG);
    assert(writes == before);
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    const char *c = argv[1];
    pcf8563_time_t t = { .year = 2024, .month = 2, .day = 29, .weekday = 0, .hour = 12, .minute = 34, .second = 56 };
    if (!strcmp(c, "valid")) {
        uint8_t old[7]; memcpy(old, regs + 2, 7);
        assert(pcf8563_init() == ESP_OK && writes == 0);
        assert(pcf8563_init() == ESP_OK && adds == 1);
        assert(pcf8563_get_time(&t) == ESP_OK && writes == 0 && regs[0] == 0);
        assert(t.year == 2024 && t.month == 2 && t.day == 29 && t.weekday == 4);
        assert(!memcmp(old, regs + 2, 7));
    } else if (!strcmp(c, "stopped")) {
        uint8_t old[7]; memcpy(old, regs + 2, 7);
        regs[0] = 0x20;
        assert(pcf8563_init() == ESP_OK && regs[0] == 0);
        assert(writes == 1 && !memcmp(old, regs + 2, 7));
    } else if (!strcmp(c, "vl")) {
        regs[2] |= 0x80; check_seed();
    } else if (!strcmp(c, "invalid_feb")) {
        regs[8] = 0x23; check_seed();
    } else if (!strcmp(c, "invalid_april")) {
        regs[5] = 0x31; regs[7] = 0x04; check_seed();
    } else if (!strcmp(c, "zero_day")) {
        regs[5] = 0; check_seed();
    } else if (!strcmp(c, "zero_month")) {
        regs[7] = 0; check_seed();
    } else if (!strcmp(c, "invalid_bcd")) {
        regs[3] = 0x1a; check_seed();
    } else if (!strcmp(c, "set_calendar")) {
        assert(pcf8563_init() == ESP_OK);
        assert(pcf8563_set_time(&t) == ESP_OK && regs[6] == 4 && regs[0] == 0);
        t.year = 2023; check_set_invalid(t);
        t.year = 2024; t.month = 4; t.day = 31; check_set_invalid(t);
        t.day = 0; check_set_invalid(t);
        t.day = 1; t.month = 0; check_set_invalid(t);
        t.month = 1; t.hour = 24; check_set_invalid(t);
        t.hour = 0; t.minute = 60; check_set_invalid(t);
        t.minute = 0; t.second = 60; check_set_invalid(t);
        t.second = 0; t.year = 2100; check_set_invalid(t);
    } else if (!strcmp(c, "time_failure") || !strcmp(c, "stop_failure") || !strcmp(c, "restart_failure") || !strcmp(c, "both_fail")) {
        assert(pcf8563_init() == ESP_OK);
        fail_time = !strcmp(c, "time_failure") || !strcmp(c, "both_fail");
        fail_stop = !strcmp(c, "stop_failure");
        fail_restart = !strcmp(c, "restart_failure") || !strcmp(c, "both_fail");
        assert(pcf8563_set_time(&t) == (!strcmp(c, "restart_failure") ? ESP_ERR_INVALID_STATE : ESP_FAIL));
        assert(writes == (!strcmp(c, "stop_failure") ? 2 : 3));
        if (strcmp(c, "restart_failure") && strcmp(c, "both_fail")) assert(regs[0] == 0);
        assert(pcf8563_get_time(&t) == ESP_OK);
    } else if (!strcmp(c, "retry_add") || !strcmp(c, "retry_read") || !strcmp(c, "retry_seed") || !strcmp(c, "retry_bus")) {
        fail_add = !strcmp(c, "retry_add"); fail_read = !strcmp(c, "retry_read");
        if (!strcmp(c, "retry_seed")) { regs[2] |= 0x80; fail_time = 1; }
        if (!strcmp(c, "retry_bus")) bus_available = 0;
        assert(pcf8563_init() == (!strcmp(c, "retry_bus") ? ESP_ERR_INVALID_STATE : ESP_FAIL));
        assert(pcf8563_get_time(&t) == ESP_ERR_INVALID_STATE);
        assert(removes == (!strcmp(c, "retry_read") || !strcmp(c, "retry_seed") ? 1 : 0));
        assert(regs[0] == 0);
        bus_available = 1;
        assert(pcf8563_init() == ESP_OK);
        assert(pcf8563_get_time(&t) == ESP_OK);
    } else { fprintf(stderr, "unknown case: %s\n", c); return 2; }
    assert(locks == 0);
    return 0;
}
