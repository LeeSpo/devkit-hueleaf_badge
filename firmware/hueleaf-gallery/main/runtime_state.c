#include "runtime_state.h"
#include <string.h>
#include "freertos/FreeRTOS.h"

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static runtime_snapshot_t s = { .battery = -1, .cur = -1, .strip = true };
static int64_t s_session_deadline, s_file_deadline;
static bool s_command;
static unsigned s_io;

#define LOCK() portENTER_CRITICAL(&s_mux)
#define UNLOCK() portEXIT_CRITICAL(&s_mux)

void runtime_snapshot(runtime_snapshot_t *out)
{
    LOCK(); *out = s; UNLOCK();
}
void runtime_set_ready(bool ready)
{
    LOCK(); s.ready = ready; UNLOCK();
}
void runtime_publish_gallery(int cur, int n, const char *name, bool strip)
{
    LOCK();
    s.cur = cur; s.n = n; s.strip = strip;
    size_t len = strlen(name);
    if (len >= sizeof(s.name)) len = sizeof(s.name) - 1;
    memcpy(s.name, name, len); s.name[len] = 0;
    UNLOCK();
}
void runtime_publish_battery(bool present, int battery)
{
    LOCK(); s.bat_present = present; s.battery = battery; UNLOCK();
}
void runtime_publish_space(uint64_t total_kb, uint64_t free_kb, int64_t now_ms)
{
    LOCK();
    s.total_kb = total_kb; s.free_kb = free_kb;
    s.cache_valid = true; s.cache_updated_ms = now_ms;
    UNLOCK();
}
void runtime_invalidate_space(void)
{
    LOCK(); s.cache_valid = false; UNLOCK();
}
const char *runtime_state_name(const runtime_snapshot_t *v, bool fault)
{
    if (!v->ready) return "starting";
    if (fault) return "fault";
    if (v->sleeping) return "sleeping";
    if (v->display) return "refreshing";
    if (v->session || v->file_upload) return "uploading";
    if (v->storage) return "maintenance";
    return "idle";
}
bool runtime_display_begin(void)
{
    LOCK();
    bool ok = !s.sleeping && !s.display && !s.storage && !s.session && !s.file_upload;
    if (ok) s.display = true;
    UNLOCK(); return ok;
}
void runtime_display_end(void)
{
    LOCK(); s.display = false; UNLOCK();
}
bool runtime_storage_begin(bool maintenance)
{
    LOCK();
    bool ok = s.ready && !s.sleeping && !s.display && !s.storage &&
              (!maintenance || (!s.session && !s.file_upload));
    if (ok) s.storage = true;
    UNLOCK(); return ok;
}
void runtime_storage_end(void)
{
    LOCK(); s.storage = false; UNLOCK();
}
bool runtime_input_blocked(void)
{
    LOCK(); bool blocked = s.sleeping || s.session || s.file_upload; UNLOCK(); return blocked;
}
bool runtime_session_begin(int64_t now_ms)
{
    LOCK();
    bool ok = s.ready && !s.sleeping && !s.display && !s.storage && !s.file_upload;
    if (ok) { s.session = true; s_session_deadline = now_ms + UPLOAD_LEASE_MS; }
    UNLOCK(); return ok;
}
bool runtime_session_end(void)
{
    LOCK();
    bool ok = !s.file_upload;
    if (ok) s.session = false;
    UNLOCK(); return ok;
}
bool runtime_session_keepalive(int64_t now_ms)
{
    LOCK(); bool ok = s.session;
    if (ok) s_session_deadline = now_ms + UPLOAD_LEASE_MS;
    UNLOCK(); return ok;
}
bool runtime_file_begin(int64_t now_ms)
{
    LOCK();
    bool ok = s.ready && !s.sleeping && !s.display;
    if (ok) {
        s.file_upload = true; s_file_deadline = now_ms + UPLOAD_LEASE_MS;
        if (s.session) s_session_deadline = s_file_deadline;
    }
    UNLOCK(); return ok;
}
void runtime_file_touch(int64_t now_ms)
{
    LOCK();
    if (s.file_upload) s_file_deadline = now_ms + UPLOAD_LEASE_MS;
    if (s.session) s_session_deadline = now_ms + UPLOAD_LEASE_MS;
    UNLOCK();
}
void runtime_file_end(void)
{
    LOCK(); s.file_upload = false; UNLOCK();
}
bool runtime_expire(int64_t now_ms)
{
    LOCK();
    if (s.session && now_ms >= s_session_deadline) s.session = false;
    bool file_expired = s.file_upload && now_ms >= s_file_deadline;
    UNLOCK(); return file_expired;
}

bool runtime_sleep_begin(void)
{
    LOCK();
    bool ok = s.ready && !s.sleeping && !s.display && !s.storage &&
              !s.session && !s.file_upload && !s_command && !s_io;
    if (ok) s.sleeping = true;
    UNLOCK(); return ok;
}
void runtime_sleep_end(void)
{
    LOCK(); s.sleeping = false; UNLOCK();
}
bool runtime_command_begin(void)
{
    LOCK(); bool ok = !s.sleeping && !s_command;
    if (ok) s_command = true;
    UNLOCK(); return ok;
}
void runtime_command_end(void)
{
    LOCK(); s_command = false; UNLOCK();
}
bool runtime_io_begin(void)
{
    LOCK(); bool ok = !s.sleeping;
    if (ok) s_io++;
    UNLOCK(); return ok;
}
void runtime_io_end(void)
{
    LOCK(); if (s_io) s_io--; UNLOCK();
}
