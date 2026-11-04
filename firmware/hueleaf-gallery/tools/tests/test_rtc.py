import os
from pathlib import Path
import subprocess

import pytest


@pytest.fixture(scope="module")
def rtc_exe(tmp_path_factory):
    root = Path(__file__).resolve().parents[2]
    exe = tmp_path_factory.mktemp("rtc") / "rtc_test"
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
        "-I", str(root / "test_host/rtc_stubs"),
        "-I", str(root / "main/rtc"),
        str(root / "main/rtc/pcf8563.c"),
        str(root / "test_host/rtc_test.c"), "-o", str(exe),
    ], check=True)
    return exe


@pytest.mark.parametrize("case", [
    "valid", "stopped", "vl", "invalid_feb", "invalid_april", "zero_day", "zero_month",
    "invalid_bcd", "set_calendar", "time_failure", "stop_failure",
    "restart_failure", "both_fail", "retry_add", "retry_read", "retry_seed", "retry_bus",
])
def test_rtc(rtc_exe, case):
    subprocess.run([str(rtc_exe), case], check=True)
