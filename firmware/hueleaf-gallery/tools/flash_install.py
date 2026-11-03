#!/usr/bin/env python3
"""HueLeaf first install (Linux/macOS/Windows). Usage: flash_install.py PORT
Runs hl_preflight (full backup + stock-app size check) BEFORE writing; writes
bootloader, partition table, otadata(ota_0) and the gallery app only if it
passes. Never touches 0x10000 (stock app) or NVS."""
import os
import subprocess
import sys

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import hl_preflight  # noqa: E402

if len(sys.argv) != 2:
    sys.exit("用法: flash_install.py 串口   (例如 /dev/ttyACM0 或 COM5)")
port = sys.argv[1]
os.chdir(here)
print("== HueLeaf 安装预检（写入前） ==")
rc = hl_preflight.main(["check", "--port", port, "--ptable", "partition-table.bin", "--backup-dir", here])
if rc != 0:
    sys.exit(rc)
print("== 写入 bootloader / 分区表 / otadata / ota_0 ==")
rc = subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32c5", "-p", port, "-b", "460800",
                     "write_flash", "0x2000", "bootloader.bin", "0x8000", "partition-table.bin",
                     "0x180000", "otadata_ota0.bin", "0x190000", "gallery_ota0.bin"]).returncode
sys.exit(rc)
