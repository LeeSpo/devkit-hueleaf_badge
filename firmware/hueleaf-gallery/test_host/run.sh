#!/bin/sh
# Host check: MonoFast stream (bit-3 stash) == FrameFilm encoder with m_mono_prev[].
set -e
cd "$(dirname "$0")"
M=../main/screen_driver
cc -O1 -Istub -I$M mono_fast_test.c $M/mono_fast.c -o /tmp/mono_fast_test
/tmp/mono_fast_test
cc -O1 -Wall -I../main hl_cfg_test.c ../main/hl_cfg.c -o /tmp/hl_cfg_test
/tmp/hl_cfg_test
cc -O1 -Wall -I../main fw_retire_test.c ../main/fw_retire.c -o /tmp/fw_retire_test && /tmp/fw_retire_test
