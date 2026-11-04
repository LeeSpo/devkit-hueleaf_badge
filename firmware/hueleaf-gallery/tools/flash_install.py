#!/usr/bin/env python3
"""HueLeaf first install (Linux/macOS/Windows).
Usage: flash_install.py PORT [--backup-dir DIRECTORY]
Runs hl_preflight (full backup + stock-app size check) BEFORE writing; writes
bootloader, partition table, otadata(ota_0) and the gallery app only if it
passes. Never touches 0x10000 (stock app) or NVS."""
import argparse
import os
import subprocess
import sys

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import hl_preflight  # noqa: E402


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="串口，例如 /dev/cu.usbmodemXXXX 或 COM5")
    parser.add_argument("--backup-dir", default=here,
                        help="整片备份目录，相对路径基于调用目录（默认：安装包目录）")
    args = parser.parse_args(argv)
    # Resolve before changing subprocess cwd, so caller-relative paths work.
    backup_dir = os.path.abspath(args.backup_dir)
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
