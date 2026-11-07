#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define UPLOAD_LEASE_MS 120000
typedef struct {
    bool ready, display, storage, session, file_upload, sleeping;
    bool cache_valid, bat_present, strip;
    int battery, cur, n, slideshow_n;
    bool slideshow_current, slideshow_custom, slideshow_fault;
    char name[64];
    uint64_t total_kb, free_kb;
    int64_t cache_updated_ms;
} runtime_snapshot_t;

/* All locks protect only copies/flags, never hardware or filesystem calls. */
void runtime_snapshot(runtime_snapshot_t *out);
void runtime_set_ready(bool ready);
void runtime_publish_gallery(int cur, int n, const char *name, bool strip);
void runtime_publish_slideshow(int n, bool current, bool custom, bool fault);
void runtime_publish_battery(bool present, int battery);
void runtime_publish_space(uint64_t total_kb, uint64_t free_kb, int64_t now_ms);
void runtime_invalidate_space(void);
const char *runtime_state_name(const runtime_snapshot_t *s, bool fault);
bool runtime_display_begin(void);
void runtime_display_end(void);
bool runtime_storage_begin(bool maintenance);
void runtime_storage_end(void);
bool runtime_input_blocked(void);
bool runtime_session_begin(int64_t now_ms);
bool runtime_session_end(void);
bool runtime_session_keepalive(int64_t now_ms);
bool runtime_file_begin(int64_t now_ms);
void runtime_file_touch(int64_t now_ms);
void runtime_file_end(void);
/* Expiring an incomplete file keeps its display guard until cleanup ends. */
bool runtime_expire(int64_t now_ms);
/* Sleep admission excludes in-flight protocol replies and background I/O.
 * Once reserved, new inputs, transfers, refreshes and I/O are rejected. */
bool runtime_sleep_begin(void);
void runtime_sleep_end(void);
bool runtime_command_begin(void);
void runtime_command_end(void);
bool runtime_io_begin(void);
void runtime_io_end(void);
esp_err_t status_cache_start(void);
