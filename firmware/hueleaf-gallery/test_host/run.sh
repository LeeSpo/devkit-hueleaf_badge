#!/bin/sh
# Host check: MonoFast stream (bit-3 stash) == FrameFilm encoder with m_mono_prev[].
set -e
cd "$(dirname "$0")"
M=../main/screen_driver
cc -O1 -Istub -I$M mono_fast_test.c $M/mono_fast.c -o /tmp/mono_fast_test
/tmp/mono_fast_test
cc -O1 -Wall -I../main hl_cfg_test.c ../main/hl_cfg.c -o /tmp/hl_cfg_test
/tmp/hl_cfg_test
cc -O1 -Wall -I../main slideshow_policy_test.c ../main/slideshow_policy.c -o /tmp/slideshow_policy_test
/tmp/slideshow_policy_test
cc -O1 -Wall -Islideshow_stubs -Iproto_stubs -I../main slideshow_test.c ../main/slideshow_policy.c ../main/hl_cfg.c ../main/runtime_state.c -o /tmp/slideshow_test
/tmp/slideshow_test
cc -O1 -Wall -Itap_wake_stubs -Islideshow_stubs -Iproto_stubs -Irtc_stubs -I../main \
    tap_wake_test.c ../main/imu/tap_guard.c ../main/runtime_state.c -o /tmp/tap_wake_test
/tmp/tap_wake_test
cc -O1 -Wall -I../main fw_retire_test.c ../main/fw_retire.c -o /tmp/fw_retire_test && /tmp/fw_retire_test
cc -O1 -Wall -I../main/imu tap_guard_test.c ../main/imu/tap_guard.c -o /tmp/tap_guard_test && /tmp/tap_guard_test
cc -O1 -Wall -Igallery_stubs -I../main runtime_state_test.c ../main/runtime_state.c -o /tmp/runtime_state_test
/tmp/runtime_state_test
cc -O1 -Wall -Iproto_stubs -I../main -DSTATUS_CACHE_SOURCE='"../main/status_cache.c"' \
    status_cache_test.c ../main/runtime_state.c -o /tmp/status_cache_test
/tmp/status_cache_test
cc -O1 -Wall -Iproto_stubs -I../main -DPROTO_SOURCE='"../main/proto.c"' \
    proto_test.c ../main/runtime_state.c ../main/slideshow_policy.c -o /tmp/proto_test
/tmp/proto_test
cc -O1 -Wall -Igallery_stubs -I../main -I../main/pmic -I../main/rtc -I../main/imu -I../main/screen_driver \
    -DGALLERY_SOURCE='"../main/gallery.c"' gallery_test.c ../main/runtime_state.c -o /tmp/gallery_test
/tmp/gallery_test
cc -O1 -Wall -Igallery_stubs -I../main -I../main/pmic -I../main/rtc -I../main/imu -I../main/screen_driver \
    gallery_refresh_test.c ../main/runtime_state.c -o /tmp/gallery_refresh_test
/tmp/gallery_refresh_test
cc -O1 -Wall -DCONFIG_HL_REMOTE=1 -Igallery_stubs -I../main -I../main/pmic -I../main/rtc -I../main/imu -I../main/screen_driver \
    gallery_refresh_test.c ../main/runtime_state.c -o /tmp/gallery_remote_refresh_test
/tmp/gallery_remote_refresh_test
cc -O1 -Wall -Igallery_stubs -I../main -I../main/pmic -I../main/rtc -I../main/imu -I../main/screen_driver \
    gallery_selection_test.c ../main/runtime_state.c -o /tmp/gallery_selection_test
/tmp/gallery_selection_test
echo "OK runtime/cache/protocol/gallery session checks"
cc -O1 -Wall -Itf_stubs tf_card_test.c -o /tmp/tf_card_test
/tmp/tf_card_test

cc -O1 -Wall -Wextra -I../main remote_policy_test.c ../main/remote_policy.c -o /tmp/remote_policy_test
/tmp/remote_policy_test
cc -O1 -Wall -Wextra -Iremote_stubs -I../main remote_lifecycle_test.c ../main/remote_policy.c ../main/runtime_state.c -o /tmp/remote_lifecycle_test
/tmp/remote_lifecycle_test
cc -O1 -Wall -Wextra -Iremote_stubs -I../main logbuf_retention_test.c -o /tmp/logbuf_retention_test
/tmp/logbuf_retention_test

cc -O1 -Wall -DCONFIG_HL_REMOTE -Istub -I../main -I../main/screen_driver remote_frame_test.c ../main/frame.c -o /tmp/remote_frame_test
/tmp/remote_frame_test

cc -O1 -Wall -Wextra -Istub -I$M mono_bitmap_test.c $M/mono_fast.c -o /tmp/mono_bitmap_test
/tmp/mono_bitmap_test

cc -O1 -Wall -Wextra -DCONFIG_HL_REMOTE=1 -DCONFIG_HL_MONO_FAST=1 -Iscreen_stubs -Istub -I$M \
    screen_mono_test.c $M/screen_driver.c $M/mono_fast.c -o /tmp/screen_mono_test
/tmp/screen_mono_test

cc -O1 -Wall -Wextra -DCONFIG_HL_REMOTE=1 -DCONFIG_HL_MONO_FAST=0 -Iscreen_stubs -Istub -I$M \
    screen_mono_test.c $M/screen_driver.c $M/mono_fast.c -o /tmp/screen_mono_disabled_test
/tmp/screen_mono_disabled_test
