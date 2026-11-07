import os
from pathlib import Path
import subprocess
import pytest


@pytest.mark.parametrize("source", ["gallery_test.c", "gallery_selection_test.c"])
def test_gallery_whole_source(tmp_path, source):
    root = Path(__file__).resolve().parents[2]
    stubs = root / "test_host/gallery_stubs"
    exe = tmp_path / "gallery_test"
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra",
        "-I", str(stubs), "-I", str(root / "main"),
        "-I", str(root / "main/pmic"), "-I", str(root / "main/rtc"),
        "-I", str(root / "main/screen_driver"), "-I", str(root / "main/imu"),
        f'-DGALLERY_SOURCE="{root / "main/gallery.c"}"',
        str(root / "test_host" / source), str(root / "main/runtime_state.c"), "-o", str(exe),
    ], check=True)
    subprocess.run([str(exe)], check=True)
