import os
from pathlib import Path
import subprocess


def test_file_store(tmp_path):
    root = Path(__file__).resolve().parents[2]
    compiler = os.environ.get("CC", "cc")
    obj = tmp_path / "file_store.o"
    exe = tmp_path / "file_store_test"
    common = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(root / "main")]
    hooks = tmp_path / "hooks.h"
    hooks.write_text(
        '#include <stdio.h>\n#include <unistd.h>\n#include <sys/stat.h>\n'
        'int test_rename(const char *, const char *);\n'
        'int test_unlink(const char *);\n'
        'int test_stat(const char *, struct stat *);\n'
        '#define rename test_rename\n#define unlink test_unlink\n'
        '#define stat(...) test_stat(__VA_ARGS__)\n'
    )
    subprocess.run(
        common + ["-include", str(hooks),
                  "-c", str(root / "main/file_store.c"), "-o", str(obj)],
        check=True,
    )
    subprocess.run(
        common + [str(root / "test_host/file_store_test.c"), str(obj), "-o", str(exe)],
        check=True,
    )
    subprocess.run([str(exe)], check=True)
