"""Exercise real encoders and screen driver with byte/transition oracles."""
import os
from pathlib import Path
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


@pytest.mark.parametrize("case", ["legacy", "compact", "driver", "disabled"])
def test_monofast(case, tmp_path):
    host = ROOT / "test_host"
    driver = ROOT / "main/screen_driver"
    command = [os.environ.get("CC", "cc"), "-O1", "-g", "-Wall", "-Wextra",
               "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    if case in ("driver", "disabled"):
        command += ["-DCONFIG_HL_REMOTE=1", f"-DCONFIG_HL_MONO_FAST={int(case == 'driver')}",
                    "-I", str(host / "screen_stubs"), str(host / "screen_mono_test.c"),
                    str(driver / "screen_driver.c")]
    else:
        command += [str(host / ("mono_fast_test.c" if case == "legacy" else "mono_bitmap_test.c"))]
    exe = tmp_path / case
    command += ["-I", str(host / "stub"), "-I", str(driver), str(driver / "mono_fast.c"), "-o", str(exe)]
    subprocess.run(command, check=True)
    subprocess.run([str(exe)], check=True, env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})
