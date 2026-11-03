#!/bin/sh
# Host check: MonoFast stream (bit-3 stash) == FrameFilm encoder with m_mono_prev[].
set -e
cd "$(dirname "$0")"
M=../main/screen_driver
cc -O1 -Istub -I$M mono_fast_test.c $M/mono_fast.c -o /tmp/mono_fast_test
/tmp/mono_fast_test
