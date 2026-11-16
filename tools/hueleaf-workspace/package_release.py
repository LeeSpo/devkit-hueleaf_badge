#!/usr/bin/env python3
"""Generate exactly two new-format release assets, without accessing hardware."""
import argparse
import importlib.util
from pathlib import Path
import re
import sys
import tempfile
import zipfile

from firmware_image import Firmware, build_image

HERE = Path(__file__).resolve().parent
SOURCE = HERE.parents[1]
TOOLS = SOURCE / "firmware/hueleaf-gallery/tools"
INSTALLER_FILES = ("hueleaf.py", "bootstrap.py", "paths.py", "support.py", "firmware_image.py",
                   "installer.py", "release_client.py", "requirements-install.txt", "hueleaf.sh",
                   "install.command", "install.bat", "README.md")


def installer_zip(path):
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
        def add(source, target):
            archive.write(source, "hueleaf-installer/" + target)
        for name in INSTALLER_FILES:
            add(HERE / name, name)
        for name in ("hl_preflight.py", "remote_layout.py"):
            add(TOOLS / name, "_" + name)
        for name in ("LICENSE", "LICENSE-APACHE-2.0", "LICENSE-CC-BY-NC-SA-4.0"):
            add(SOURCE / name, "licenses/" + name)
        # Keep the GUI usable from the installer ZIP, including all web assets.
        gui = SOURCE / "tools/hueleaf-push"
        for source in sorted(gui.glob("*.py")):
            add(source, "gui/" + source.name)
        for name in ("requirements.txt", "README.md"):
            add(gui / name, "gui/" + name)
        for source in sorted((gui / "web").rglob("*")):
            if source.is_file() and not any(part.startswith(".") for part in source.relative_to(gui).parts):
                add(source, "gui/" + source.relative_to(gui).as_posix())


def checked_build(build):
    sys.path.insert(0, str(TOOLS))
    try:
        spec = importlib.util.spec_from_file_location("hueleaf_release_build", TOOLS / "build_candidate.py")
        checker = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(checker)
        return checker.check(build)
    finally:
        sys.path.remove(str(TOOLS))


def package_release(build, output):
    build, output = Path(build).resolve(), Path(output).resolve()
    # Developer packaging still requires an up-to-date build. No source or hash
    # checks are performed by the downloadable installer's firmware install path.
    record = checked_build(build)
    version = record["firmware_version"]
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", version):
        raise ValueError("固件版本不能用作附件文件名")
    if output.exists():
        raise ValueError(f"输出目录已存在，请使用新目录：{output}")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".hueleaf-release-", dir=output.parent) as temporary:
        stage = Path(temporary) / "assets"
        stage.mkdir()
        firmware = stage / f"hueleaf-firmware-{version}.bin"
        firmware.write_bytes(build_image(build, record))
        Firmware.read(firmware)
        installer_zip(stage / "hueleaf-installer.zip")
        # Refuse artifacts whose bound sources changed during packaging.
        checked_build(build)
        stage.rename(output)
    print("新格式附件：", output / "hueleaf-installer.zip")
    print("固件安装镜像：", output / firmware.name)
    return output


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    package_release(args.build_dir, args.output_dir)
