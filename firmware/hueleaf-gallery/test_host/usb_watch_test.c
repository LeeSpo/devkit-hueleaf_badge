#include <assert.h>
#include <stdio.h>
#define main lp_sample
#include "../main/lp_core/usb_watch.c"
#undef main
static bool good = true;
static uint8_t supply;
static int reads, wakes;
esp_err_t lp_core_i2c_master_write_read_device(int port, uint16_t address,
    const uint8_t *reg, size_t w, uint8_t *out, size_t r, int32_t timeout)
{
    assert(port==LP_I2C_NUM_0 && address==0x34 && *reg==0 && w==1 && r==1);
    assert(timeout>0 && timeout<=160000); reads++; *out=supply;
    return good ? ESP_OK : ESP_FAIL;
}
void ulp_lp_core_wakeup_main_processor(void) { wakes++; }
int main(void)
{
    lp_sample(); assert(!reads && !wakes); /* invalid/unarmed program */
    watch_magic=USB_WATCH_MAGIC;
    for(int i=0;i<5;i++) lp_sample();
    assert(reads==5 && !wakes);
    supply=USB_WATCH_VBUS_GOOD; lp_sample(); assert(wakes==1 && watch.event==USB_WATCH_INSERTED);
    lp_sample(); assert(wakes==2); /* insertion racing HP sleep stays pending */
    watch=(usb_watch_t){.present=1}; wakes=0;
    for(int i=0;i<20;i++) lp_sample(); assert(!wakes); /* power bank stays asleep */
    supply=0; lp_sample(); assert(!wakes);
    supply=USB_WATCH_VBUS_GOOD; lp_sample(); assert(wakes==1); /* unplug/replug */
    watch=(usb_watch_t){0}; wakes=0; supply=0; good=false;
    lp_sample(); lp_sample(); assert(!wakes && watch.errors==2);
    good=true; lp_sample(); assert(watch.errors==0 && !wakes);
    good=false; lp_sample(); lp_sample(); lp_sample();
    assert(wakes==1 && watch.event==USB_WATCH_FAULT);
    puts("OK actual LP USB watcher: rising edge, persistent supply, rearm, bounded errors, latched wake");
    return 0;
}
