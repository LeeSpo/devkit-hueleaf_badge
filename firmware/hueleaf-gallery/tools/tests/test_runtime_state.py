import os
from pathlib import Path
import subprocess


def test_runtime_activity_and_snapshot(tmp_path):
    root = Path(__file__).resolve().parents[2]
    exe = tmp_path / "runtime_test"
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra",
        "-I", str(root / "test_host/gallery_stubs"), "-I", str(root / "main"),
        str(root / "test_host/runtime_state_test.c"), str(root / "main/runtime_state.c"),
        "-o", str(exe),
    ], check=True)
    subprocess.run([str(exe)], check=True)


def test_status_cache_only_probes_idle_storage(tmp_path):
    root = Path(__file__).resolve().parents[2]
    exe = tmp_path / "cache_test"
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra",
        "-I", str(root / "test_host/proto_stubs"), "-I", str(root / "main"),
        f'-DSTATUS_CACHE_SOURCE="{root / "main/status_cache.c"}"',
        str(root / "test_host/status_cache_test.c"), str(root / "main/runtime_state.c"),
        "-o", str(exe),
    ], check=True)
    subprocess.run([str(exe)], check=True)


def test_gallery_refuses_static_fatfs_buffers(tmp_path):
    root = Path(__file__).resolve().parents[2]
    (tmp_path / "sdkconfig.h").write_text("#define CONFIG_FATFS_USE_DYN_BUFFERS 0\n")
    result = subprocess.run([
        os.environ.get("CC", "cc"), "-std=gnu11", "-I", str(tmp_path),
        "-I", str(root / "test_host/proto_stubs"), "-I", str(root / "main"),
        "-c", str(root / "main/status_cache.c"), "-o", str(tmp_path / "bad.o"),
    ], capture_output=True, text=True)
    assert result.returncode != 0
    assert "requires dynamic FatFS buffers" in result.stderr
