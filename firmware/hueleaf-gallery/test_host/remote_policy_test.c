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
    assert(p.auth_deadline_ms == 30000);
    remote_policy_disconnect(&p, 26000);
    assert(p.deadline_ms == 30000); /* unauthorised retries cannot extend */
    assert(remote_policy_connect(&p, 29999));
    assert(!remote_policy_auth(&p, 30000));
    assert(!remote_policy_step(&p, 30000, true, false));
    assert(!remote_policy_step(&p, 30000, false, true));
    assert(remote_policy_step(&p, 30000, false, false));
    remote_policy_disconnect(&p, 31000);
    assert(p.phase == REMOTE_SLEEP);
    remote_policy_start(&p, o, 0);
    assert(remote_policy_connect(&p, 100));
    assert(remote_policy_auth(&p, 200));
    assert(p.phase == REMOTE_PREPARING);
    remote_policy_ready(&p, 1000);
    assert(p.phase == REMOTE_ACTIVE && p.deadline_ms == 121000);
    remote_policy_activity(&p, 10000);
    assert(p.deadline_ms == 130000);
    remote_policy_disconnect(&p, 20000);
    assert(p.phase == REMOTE_RECONNECT && p.deadline_ms == 30000);
    assert(remote_policy_connect(&p, 21000));
    remote_policy_disconnect(&p, 22000);
    assert(p.deadline_ms == 30000);
    assert(remote_policy_connect(&p, 23000) && remote_policy_auth(&p, 24000));
    remote_policy_ready(&p, 25000);
    remote_policy_end(&p);
    assert(!remote_policy_step(&p, 26000, true, false));
    assert(remote_policy_step(&p, 26000, false, false));
    o.wait_ms = 4999; assert(!remote_options_valid(&o));
    o = REMOTE_DEFAULT_OPTIONS; o.cooldown_after = 0; assert(!remote_options_valid(&o));
    puts("OK remote policy: fixed wait, bounded auth, authenticated idle, reconnect, USB/busy guards");
}
