#!/usr/bin/env python3
"""HueLeaf first install (Linux/macOS/Windows).
Usage: flash_install.py PORT [--backup-dir DIRECTORY]
Runs hl_preflight (full backup + stock-app size check) BEFORE writing; writes
bootloader, partition table, otadata(ota_0) and the gallery app only if it
passes. Never touches 0x10000 (stock app) or NVS."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import hl_preflight  # noqa: E402

PAYLOAD_LIMITS = {"bootloader.bin": 0x6000, "partition-table.bin": 0x1000,
                  "otadata_ota0.bin": 0x2000, "gallery_ota0.bin": 0xB0000}


def validate_package():
    """Reject incomplete/mixed/modified packages before touching a port."""
    root = Path(here).parent
    record = json.loads((root / "manifest.json").read_text())
    for name in (*PAYLOAD_LIMITS, "flash_install.py", "hl_preflight.py"):
        path = Path(here) / name
        if hashlib.sha256(path.read_bytes()).hexdigest() != record["files"]["install/" + name]:
            raise ValueError("安装包文件缺失或校验不匹配：" + name)
        if name in PAYLOAD_LIMITS and not 0 < path.stat().st_size <= PAYLOAD_LIMITS[name]:
            raise ValueError("安装镜像尺寸超出写入范围：" + name)
    app = (Path(here) / "gallery_ota0.bin").read_bytes()
    if hl_preflight.app_image_size(app) != len(app):
        raise ValueError("相册镜像长度不合法")
    print("候选固件：", record["firmware_version"], "源码指纹：", record["source_sha256"])


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="串口，例如 /dev/cu.usbmodemXXXX 或 COM5")
    parser.add_argument("--backup-dir", default=here, help="整片备份目录（默认：安装包目录）")
    args = parser.parse_args(argv)
    try:
        validate_package()
    except (OSError, KeyError, ValueError, hl_preflight.ImageError) as e:
        print("[中止] 安装包不完整或已被修改：", e, file=sys.stderr)
        return 2
    # Resolve before changing subprocess cwd, so caller-relative paths work.
    backup_dir = os.path.abspath(args.backup_dir)
    os.makedirs(backup_dir, exist_ok=True)
    print("== HueLeaf 安全状态检查（只读） ==")
    hl_preflight.check_security(args.port, backup_dir)
    print("== HueLeaf 安装预检（写入前） ==")
    rc = hl_preflight.main(["check", "--port", args.port, "--ptable", os.path.join(here, "partition-table.bin"),
                            "--backup-dir", backup_dir])
    if rc != 0:
        return rc
    print("== 写入 bootloader / 分区表 / otadata / ota_0 ==")
    return subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32c5", "-p", args.port, "-b", "460800",
                           "write_flash", "0x2000", "bootloader.bin", "0x8000", "partition-table.bin",
                           "0x180000", "otadata_ota0.bin", "0x190000", "gallery_ota0.bin"], cwd=here).returncode


if __name__ == "__main__":
    sys.exit(main())
