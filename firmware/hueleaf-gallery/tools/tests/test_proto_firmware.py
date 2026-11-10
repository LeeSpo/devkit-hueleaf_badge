import os
from pathlib import Path
import subprocess


def test_proto_whole_source(tmp_path):
    root = Path(__file__).resolve().parents[2]
    exe = tmp_path / "proto_test"
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(root / "test_host/proto_stubs"),
        "-I", str(root / "main"),
        f'-DPROTO_SOURCE="{root / "main/proto.c"}"',
        str(root / "test_host/proto_test.c"), str(root / "main/runtime_state.c"),
        str(root / "main/slideshow_policy.c"), "-o", str(exe),
    ], check=True)
    subprocess.run([str(exe)], check=True, env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})
