#include "remote_policy.h"

bool remote_options_valid(const remote_options_t *o)
{
    return o && o->wait_ms >= 5000 && o->wait_ms <= 120000 &&
        o->idle_ms >= 10000 && o->idle_ms <= 600000 &&
        o->reconnect_ms <= 60000 && o->cooldown_ms <= 3600000 &&
        o->cooldown_after >= 1 && o->cooldown_after <= 20;
}
void remote_policy_start(remote_policy_t *p, remote_options_t o, int64_t now)
{
    *p = (remote_policy_t){ .phase = REMOTE_WAIT, .options = o, .deadline_ms = now + o.wait_ms };
}
bool remote_policy_connect(remote_policy_t *p, int64_t now)
{
    if ((p->phase != REMOTE_WAIT && p->phase != REMOTE_RECONNECT) || now >= p->deadline_ms)
        return false;
    p->connected = true; p->authenticated = false; p->phase = REMOTE_AUTH;
    p->auth_deadline_ms = now + 10000;
    if (p->auth_deadline_ms > p->deadline_ms) p->auth_deadline_ms = p->deadline_ms;
    return true;
}
bool remote_policy_auth(remote_policy_t *p, int64_t now)
{
    if (p->phase != REMOTE_AUTH || now >= p->auth_deadline_ms) return false;
    p->authenticated = true; p->phase = REMOTE_PREPARING;
    p->deadline_ms = now + p->options.idle_ms;
    return true;
}
void remote_policy_ready(remote_policy_t *p, int64_t now)
{
    if (p->authenticated && p->phase == REMOTE_PREPARING) {
        p->phase = REMOTE_ACTIVE; p->deadline_ms = now + p->options.idle_ms;
    }
}
void remote_policy_disconnect(remote_policy_t *p, int64_t now)
{
    bool trusted = p->authenticated;
    p->connected = p->authenticated = false;
    if (p->phase == REMOTE_SLEEP || p->phase == REMOTE_FAULT || p->phase == REMOTE_OFF) return;
    if (trusted) { p->phase = REMOTE_RECONNECT; p->deadline_ms = now + p->options.reconnect_ms; }
    else p->phase = REMOTE_WAIT; /* never renew unauthorised connection's window */
}
void remote_policy_activity(remote_policy_t *p, int64_t now)
{
    if (p->phase == REMOTE_ACTIVE && p->authenticated) p->deadline_ms = now + p->options.idle_ms;
}
void remote_policy_end(remote_policy_t *p)
{
    if (p->authenticated) p->phase = REMOTE_SLEEP;
}
bool remote_policy_step(remote_policy_t *p, int64_t now, bool busy, bool usb)
{
    if (p->phase == REMOTE_OFF || p->phase == REMOTE_FAULT) return false;
    bool due = p->phase == REMOTE_SLEEP || now >= p->deadline_ms ||
        (p->phase == REMOTE_AUTH && now >= p->auth_deadline_ms);
    if (due && !busy && !usb) { p->phase = REMOTE_SLEEP; return true; }
    return false;
}
const char *remote_phase_name(remote_phase_t phase)
{
    static const char *const names[] = {"off", "waiting", "auth", "preparing", "active", "reconnect", "sleeping", "fault"};
    return (unsigned)phase < sizeof(names)/sizeof(names[0]) ? names[phase] : "fault";
}
