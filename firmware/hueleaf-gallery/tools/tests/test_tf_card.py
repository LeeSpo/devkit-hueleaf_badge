import pathlib
import subprocess


def test_tf_card(tmp_path):
    root = pathlib.Path(__file__).resolve().parents[2]
    exe = tmp_path / "tf_card_test"
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-I", str(root / "test_host/tf_stubs"),
        str(root / "test_host/tf_card_test.c"), "-o", str(exe),
    ], check=True)
    subprocess.run([str(exe)], check=True)
