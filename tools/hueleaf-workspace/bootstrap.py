#!/usr/bin/env python3
"""Prepare only the host dependencies; ESP-IDF is a separate developer setup."""
from pathlib import Path
import subprocess
import sys
import venv

from paths import data_root, venv_python


def ensure_host(gui=None):
    if sys.version_info[:2] != (3, 12):
        raise RuntimeError("请安装 Python 3.12，或设置 HUELEAF_PYTHON 指向 Python 3.12")
    directory = data_root() / "venvs/hueleaf-host"
    python = venv_python(directory)
    if not python.is_file():
        print("首次运行：准备 HueLeaf Python 环境…", flush=True)
        venv.EnvBuilder(with_pip=True).create(directory)
    requirements = Path(__file__).with_name("requirements-install.txt")
    check = "import serial, esptool, espefuse, importlib.metadata as m; assert m.version('esptool') == '4.13.0'"
    if subprocess.run([str(python), "-c", check], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
        subprocess.run([str(python), "-m", "pip", "install", "-r", str(requirements)], check=True)
    if gui:
        check = "import PIL, serial, numpy, numba"
        if subprocess.run([str(python), "-c", check], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
            subprocess.run([str(python), "-m", "pip", "install", "-r", str(Path(gui) / "requirements.txt")], check=True)
    return python


def main():
    # Help and path inspection work without downloading dependencies.
    if any(arg in ("-h", "--help") for arg in sys.argv[1:]) or sys.argv[1:] == ["status"]:
        python = Path(sys.executable)
    else:
        python = ensure_host()
    if sys.argv[1:] == ["--setup-only"]:
        print("运行环境：", python)
        return 0
    return subprocess.run([str(python), str(Path(__file__).with_name("hueleaf.py")), *sys.argv[1:]]).returncode


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, OSError, subprocess.CalledProcessError) as exc:
        print("环境准备失败：", exc, file=sys.stderr)
        sys.exit(1)
