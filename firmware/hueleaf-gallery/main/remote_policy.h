#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    REMOTE_OFF, REMOTE_WAIT, REMOTE_AUTH, REMOTE_PREPARING,
    REMOTE_ACTIVE, REMOTE_RECONNECT, REMOTE_SLEEP, REMOTE_FAULT
} remote_phase_t;
typedef struct {
    uint32_t wait_ms, idle_ms, reconnect_ms, cooldown_ms;
    uint8_t cooldown_after;
} remote_options_t;
typedef struct {
    remote_phase_t phase;
    remote_options_t options;
    int64_t deadline_ms, auth_deadline_ms, local_deadline_ms;
    bool connected, authenticated;
} remote_policy_t;
#define REMOTE_DEFAULT_OPTIONS ((remote_options_t){30000, 120000, 10000, 0, 3})
bool remote_options_valid(const remote_options_t *o);
void remote_policy_start(remote_policy_t *p, remote_options_t o, int64_t now);
bool remote_policy_connect(remote_policy_t *p, int64_t now);
bool remote_policy_auth(remote_policy_t *p, int64_t now);
void remote_policy_ready(remote_policy_t *p, int64_t now);
void remote_policy_disconnect(remote_policy_t *p, int64_t now);
void remote_policy_activity(remote_policy_t *p, int64_t now);
void remote_policy_local_activity(remote_policy_t *p, int64_t now);
void remote_policy_end(remote_policy_t *p);
/* busy defers sleep but never extends a deadline. USB prevents sleep. */
bool remote_policy_step(remote_policy_t *p, int64_t now, bool busy, bool usb);
const char *remote_phase_name(remote_phase_t phase);
