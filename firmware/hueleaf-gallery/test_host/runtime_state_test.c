#include <assert.h>
#include <string.h>
#include "runtime_state.h"

int main(void)
{
    runtime_snapshot_t s;
    assert(!runtime_session_begin(0));
    runtime_set_ready(true);
    assert(runtime_storage_begin(true));
    assert(!runtime_display_begin() && !runtime_session_begin(0));
    runtime_storage_end();
    assert(runtime_display_begin());
    assert(!runtime_storage_begin(false) && !runtime_session_begin(0));
    runtime_snapshot(&s); assert(!strcmp(runtime_state_name(&s, false), "refreshing"));
    runtime_display_end();
    assert(runtime_session_begin(0) && runtime_input_blocked());
    assert(!runtime_display_begin() && !runtime_storage_begin(true));
    assert(runtime_storage_begin(false)); runtime_storage_end();
    assert(runtime_session_keepalive(10000));
    assert(!runtime_expire(120001) && runtime_input_blocked());
    assert(runtime_file_begin(120001));
    assert(!runtime_session_end());
    runtime_file_touch(130000);
    assert(!runtime_expire(240002));
    assert(runtime_expire(250000) && runtime_input_blocked());
    runtime_snapshot(&s); assert(!s.session && s.file_upload);
    runtime_file_end(); assert(!runtime_input_blocked());
    assert(runtime_session_end());
    runtime_publish_gallery(2, 3, "photo.bin", false);
    runtime_publish_battery(true, 88);
    runtime_publish_space(100, 75, 123);
    runtime_snapshot(&s);
    assert(s.cur==2 && s.n==3 && !strcmp(s.name,"photo.bin") && !s.strip);
    assert(s.cache_valid && s.free_kb==75 && s.cache_updated_ms==123 && s.battery==88);
    runtime_invalidate_space(); runtime_snapshot(&s);
    assert(!s.cache_valid && !strcmp(runtime_state_name(&s, false),"idle"));
    assert(!strcmp(runtime_state_name(&s, true),"fault"));
    return 0;
}
