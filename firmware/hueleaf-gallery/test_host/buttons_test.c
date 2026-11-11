/* Exercise the real startup poller and the same decoder used by button tasks. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include "platform.h"
#include "../main/buttons.c"

static int boot_level=1, irq_level=1, configs, pmic_inits, reads, tasks, notifications;
static int input_error, actions, activity, task_ticks;
static bool pressed, ready, blocked, run_boot, script_boot;
static button_action_t last_action;
static jmp_buf escape;
int gpio_get_level(gpio_num_t pin) { return pin==BOOT_GPIO ? boot_level : irq_level; }
esp_err_t gpio_config(const gpio_config_t *c) { assert(c->mode==GPIO_MODE_INPUT); configs++; return ESP_OK; }
esp_err_t gpio_install_isr_service(int flags) { (void)flags; return ESP_OK; }
esp_err_t gpio_isr_handler_add(gpio_num_t p,void (*f)(void *),void *a)
{ assert(p==pmic_irq && f==pek_isr); (void)a; return ESP_OK; }
esp_err_t axp2101_pwron_init(void) { pmic_inits++; return input_error; }
esp_err_t axp2101_take_pwron_short_press(bool *out)
{ reads++; *out=pressed; pressed=false; irq_level=1; return input_error; }
esp_err_t axp2101_led_pulse(uint32_t ms) { assert(ms==80); return ESP_OK; }
bool gallery_is_ready(void) { return ready; }
static bool accept(button_action_t action)
{ if (blocked) return false; last_action=action; actions++; return true; }
bool gallery_btn_boot_short(void) { return accept(BUTTON_BOOT_SHORT); }
bool gallery_btn_boot_long(void) { return accept(BUTTON_BOOT_LONG); }
bool gallery_btn_pek_short(void) { return accept(BUTTON_PEK_SHORT); }
void remote_local_activity(void) { activity++; }
bool runtime_io_begin(void) { return true; }
void runtime_io_end(void) {}
int xTaskCreate(void (*f)(void *),const char *n,unsigned st,void *a,unsigned p,TaskHandle_t *out)
{ (void)f;(void)n;(void)st;(void)a;(void)p; tasks++; *out=(void *)1; return pdPASS; }
void vTaskDelete(TaskHandle_t t) { (void)t; }
void xTaskNotifyGive(TaskHandle_t t) { assert(t); notifications++; }
void vTaskNotifyGiveFromISR(TaskHandle_t t,BaseType_t *w) { assert(t); *w=pdFALSE; }
uint32_t ulTaskNotifyTake(int clear,TickType_t ticks) { (void)clear;(void)ticks; return 0; }
void vTaskDelay(TickType_t t)
{
    assert(t==POLL_MS);
    if (run_boot) {
        task_ticks++;
        if (script_boot) {
            if (task_ticks==10 || task_ticks==30) boot_level=1;
            if (task_ticks==20) boot_level=0;
        }
        if (task_ticks>150) longjmp(escape,1);
    }
}
static button_action_t poll(void)
{
    button_action_t action;
    assert(buttons_poll_wake(&action)==ESP_OK);
    return action;
}
static void no_action(int n) { while (n--) assert(poll()==BUTTON_NONE); }

int main(void)
{
    no_action(4);
    assert(configs==2 && pmic_inits==1 && tasks==0 && reads==0);
    /* Contact bounce never becomes an action. Short press fires on release. */
    boot_level=0; no_action(2); boot_level=1; no_action(3);
    boot_level=0; no_action(3+8); boot_level=1; no_action(2);
    assert(poll()==BUTTON_BOOT_SHORT); no_action(4);
    /* Long press fires while held, once, with no trailing short press. */
    boot_level=0; no_action(3+99); assert(poll()==BUTTON_BOOT_LONG);
    no_action(120); boot_level=1; no_action(4);
    irq_level=0; pressed=true; assert(poll()==BUTTON_PEK_SHORT);
    no_action(3); assert(reads==1 && tasks==0 && actions==0);
    irq_level=0; pressed=false; assert(poll()==BUTTON_NONE);
    irq_level=0; input_error=ESP_FAIL;
    button_action_t action; assert(buttons_poll_wake(&action)==ESP_FAIL);
    input_error=0;
    assert(!buttons_dispatch(BUTTON_PEK_SHORT) && actions==0);
    ready=true;
    for (action=BUTTON_BOOT_SHORT; action<=BUTTON_PEK_SHORT; action++) {
        assert(buttons_dispatch(action) && last_action==action);
    }
    assert(actions==3 && activity==3 && notifications==0);
    blocked=true; assert(!buttons_dispatch(BUTTON_BOOT_SHORT)); blocked=false;
    assert(activity==3);
    /* A long gesture consumed during startup must not fire again after handoff,
     * even if the user still holds BOOT for more than a second. */
    assert(buttons_start()==ESP_OK && tasks==3 && configs==2 && pmic_inits==1);
    boot_level=0; run_boot=true;
    if (!setjmp(escape)) boot_task(NULL);
    run_boot=false; assert(actions==3);
    task_ticks=0; script_boot=run_boot=true;
    if (!setjmp(escape)) boot_task(NULL);
    run_boot=false;
    assert(actions==4 && last_action==BUTTON_BOOT_SHORT && notifications==1 && activity==4);
    puts("OK actual buttons: bounce, short/long/PEK wake, no early workers, busy gating, held-key handoff");
    return 0;
}
