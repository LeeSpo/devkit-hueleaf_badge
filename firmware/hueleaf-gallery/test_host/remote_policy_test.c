#include <assert.h>
#include <stdio.h>
#include "remote_policy.h"

int main(void)
{
    remote_options_t o = REMOTE_DEFAULT_OPTIONS;
    assert(remote_options_valid(&o));
    remote_policy_t p;
    remote_policy_start(&p, o, 0);
    assert(!remote_policy_step(&p, 29999, false, false));
    assert(remote_policy_connect(&p, 25000));
    assert(p.auth_deadline_ms == 85000);
    remote_policy_disconnect(&p, 26000);
    assert(p.deadline_ms == 30000); /* unauthorised retries cannot extend */
    assert(remote_policy_connect(&p, 29999));
    assert(!remote_policy_step(&p, 30000, false, false)); /* pairing dialog still open */
    assert(!remote_policy_step(&p, 89998, false, false));
    assert(!remote_policy_auth(&p, 89999));
    assert(!remote_policy_step(&p, 89999, true, false));
    assert(!remote_policy_step(&p, 89999, false, true));
    assert(remote_policy_step(&p, 89999, false, false));
    remote_policy_disconnect(&p, 90000);
    assert(p.phase == REMOTE_SLEEP);
    remote_policy_start(&p, o, 0);
    assert(remote_policy_connect(&p, 100));
    assert(remote_policy_auth(&p, 45100)); /* user confirmation took over 10 seconds */
    assert(p.phase == REMOTE_PREPARING);
    remote_policy_ready(&p, 46000);
    assert(p.phase == REMOTE_ACTIVE && p.deadline_ms == 166000);
    remote_policy_activity(&p, 47000);
    assert(p.deadline_ms == 167000);
    remote_policy_disconnect(&p, 48000);
    assert(p.phase == REMOTE_RECONNECT && p.deadline_ms == 58000);
    assert(remote_policy_connect(&p, 49000));
    remote_policy_disconnect(&p, 50000);
    assert(p.deadline_ms == 58000);
    assert(remote_policy_connect(&p, 51000) && remote_policy_auth(&p, 52000));
    remote_policy_ready(&p, 53000);
    remote_policy_end(&p);
    assert(!remote_policy_step(&p, 54000, true, false));
    assert(remote_policy_step(&p, 54000, false, false));
    remote_policy_start(&p, REMOTE_DEFAULT_OPTIONS, 0);
    assert(remote_policy_connect(&p, 100));
    remote_policy_disconnect(&p, 40000);
    assert(!remote_policy_connect(&p, 40001)); /* failed pairing never opens a new window */
    assert(remote_policy_step(&p, 40001, false, false));
    /* Physical input opens a local idle window without granting BLE access. */
    remote_policy_start(&p, REMOTE_DEFAULT_OPTIONS, 0);
    remote_policy_local_activity(&p, 25000);
    assert(!p.authenticated && p.local_deadline_ms == 145000);
    assert(!remote_policy_step(&p, 55000, false, false));
    assert(remote_policy_connect(&p, 56000));
    remote_policy_disconnect(&p, 57000);
    assert(p.local_deadline_ms == 145000); /* unauthorised peer cannot renew it */
    remote_policy_activity(&p, 60000); /* accepted key/frame completion */
    assert(p.local_deadline_ms == 180000);
    assert(!remote_policy_step(&p, 179999, false, false));
    assert(remote_policy_step(&p, 180000, false, false));
    remote_policy_local_activity(&p, 180001); /* new physical action wins expired wait */
    assert(p.phase == REMOTE_WAIT && remote_policy_connect(&p, 180002));
    assert(remote_policy_auth(&p, 180003)); remote_policy_ready(&p, 180004);
    remote_policy_disconnect(&p, 180005);
    assert(!remote_policy_step(&p, 190005, false, false)); /* keep local window after disconnect */
    assert(remote_policy_connect(&p, 190006) && remote_policy_auth(&p, 190007));
    remote_policy_end(&p);
    assert(p.local_deadline_ms == 0 && remote_policy_step(&p, 190008, false, false));
    o.wait_ms = 4999; assert(!remote_options_valid(&o));
    o = REMOTE_DEFAULT_OPTIONS; o.cooldown_after = 0; assert(!remote_options_valid(&o));
    puts("OK remote policy: fixed wait, bounded auth, authenticated idle, reconnect, USB/busy guards");
}
