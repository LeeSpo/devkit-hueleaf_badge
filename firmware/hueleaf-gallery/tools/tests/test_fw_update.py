import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def updater(tmp_path_factory):
    binary = tmp_path_factory.mktemp("updater-build") / "updater"
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-I", str(ROOT / "test_host/updater_stubs"),
        "-I", str(ROOT / "main"),
        str(ROOT / "test_host/updater_test.c"), "-o", str(binary),
    ], check=True)
    return binary


@pytest.mark.parametrize("scenario", [
    "missing", "success", "oversize", "wrong", "short", "truncated", "readerror", "badimage",
    "stuck", "prepared", "unsecured", "bootfail",
])
def test_updater(updater, scenario, tmp_path):
    subprocess.run([str(updater), scenario], cwd=tmp_path, check=True)
