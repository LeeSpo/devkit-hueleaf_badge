#!/usr/bin/env python3
"""HueLeaf first install (Linux/macOS/Windows).
Usage: flash_install.py PORT [--backup-dir DIRECTORY] [--skip-backup | --force-backup]
Runs hl_preflight (full backup + stock-app size check) BEFORE writing; writes
bootloader, partition table, otadata(ota_0) and the gallery app only if it
passes. Never touches 0x10000 (stock app) or NVS.
Backups: first install / layout migration always takes a new full backup.
When the layout is already ours, a verified backup of the same chip younger
than hl_preflight.BACKUP_REUSE_HOURS is reused; --force-backup takes a new one,
--skip-backup takes none."""
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
    remote = record.get("layout_id") == "remote-v1"
    limits = dict(PAYLOAD_LIMITS)
    if remote:
        from remote_layout import matches
        if not matches(hl_preflight.parse_ptable((Path(here) / "partition-table.bin").read_bytes())):
            raise ValueError("remote-v1 partition mismatch")
        limits["gallery_ota0.bin"] = 0x1F0000
    scripts = ("flash_install.py", "hl_preflight.py") + (("remote_layout.py",) if remote else ())
    for name in (*limits, *scripts):
        path = Path(here) / name
        if hashlib.sha256(path.read_bytes()).hexdigest() != record["files"]["install/" + name]:
            raise ValueError("安装包文件缺失或校验不匹配：" + name)
        if name in limits and not 0 < path.stat().st_size <= limits[name]:
            raise ValueError("安装镜像尺寸超出写入范围：" + name)
    app = (Path(here) / "gallery_ota0.bin").read_bytes()
    if remote and app[80:112].split(b"\0", 1)[0] != b"hueleaf_remote_v1":
        raise ValueError("App layout identifier mismatch")
    if hl_preflight.app_image_size(app) != len(app):
        raise ValueError("相册镜像长度不合法")
    print("候选固件：", record["firmware_version"], "源码指纹：", record["source_sha256"])
    return record


def install_remote(args, backup_dir):
    from remote_layout import matches, migration_allowed
    mac = hl_preflight.check_security(args.port)
    try:
        table = hl_preflight.parse_ptable(hl_preflight.read_live(args.port, 0x8000, 0x1000))
    except hl_preflight.ImageError:
        table = []
    same = matches(table)
    destructive = not same or args.migrate_replace_stock   # erases the app area (and remote_nvs on a retry)
    if args.skip_backup and destructive:
        raise ValueError("迁移/未知布局/--migrate-replace-stock 会擦除应用区，必须先整片备份，不能使用 --skip-backup")
    if args.skip_backup:
        print("已按 --skip-backup 跳过整片备份（布局已是 remote-v1，只更新 bootloader/分区表/otadata/App）。")
    else:
        prefix = "hueleaf_remote_" + (mac or "unknown") + "_"
        recent = None
        if mac and not destructive and not args.force_backup:
            recent = hl_preflight.find_recent_backup(backup_dir, prefix)
        if recent:
            print(f"复用 {hl_preflight.BACKUP_REUSE_HOURS} 小时内已校验的本芯片备份：{recent}（--force-backup 可强制新备份）")
        else:
            hl_preflight.make_backup(args.port, backup_dir, prefix)
    if not same and (not args.migrate_replace_stock or not migration_allowed(table)):
        raise ValueError("Layout migration needs known NVS/PHY and --migrate-replace-stock; backup saved")
    if args.preflight_only:
        return 0
    if args.migrate_replace_stock and same:
        # Explicit retry also clears partially initialised credentials after power loss.
        rc, _ = hl_preflight.esptool(args.port, "erase_region", "0x10000", "0x3f0000")
        if rc: return rc
    if not same:
        # Interrupted remote migration: new table may already exist; if not,
        # the original known table remains valid. Truly unknown tables fail closed.
        if not args.migrate_replace_stock or not migration_allowed(table):
            raise ValueError("Layout migration needs known NVS/PHY and --migrate-replace-stock; backup saved")
        rc, _ = hl_preflight.esptool(args.port, "erase_region", "0x10000", "0x3f0000")
        if rc:
            return rc
    offsets = ("0x2000", "0x8000", "0x10000", "0x20000")
    names = ("bootloader.bin", "partition-table.bin", "otadata_ota0.bin", "gallery_ota0.bin")
    command = [sys.executable, "-m", "esptool", "--chip", "esp32c5", "-p", args.port,
               "-b", "460800", "write_flash"]
    for offset, name in zip(offsets, names):
        command.extend((offset, str(Path(here) / name)))
    return subprocess.run(command).returncode


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="串口，例如 /dev/cu.usbmodemXXXX 或 COM5")
    parser.add_argument("--backup-dir", default=here, help="整片备份目录（默认：安装包目录）")
    backup = parser.add_mutually_exclusive_group()
    backup.add_argument("--skip-backup", action="store_true",
                        help="Do not take a full backup (refused unless the layout is already ours)")
    backup.add_argument("--force-backup", action="store_true",
                        help="Always take a new full backup instead of reusing a recent one")
    parser.add_argument("--preflight-only", action="store_true", help="Backup/check only, no erase or write")
    parser.add_argument("--migrate-replace-stock", action="store_true", help="Explicitly replace stock layout after full backup")
    args = parser.parse_args(argv)
    try:
        record = validate_package()
    except (OSError, KeyError, ValueError, hl_preflight.ImageError) as e:
        print("[中止] 安装包不完整或已被修改：", e, file=sys.stderr)
        return 2
    # Resolve before changing subprocess cwd, so caller-relative paths work.
    backup_dir = os.path.abspath(args.backup_dir)
    os.makedirs(backup_dir, exist_ok=True)
    if record.get("layout_id") == "remote-v1":
        try:
            return install_remote(args, backup_dir)
        except (ValueError, OSError, hl_preflight.ImageError) as e:
            print("[中止] remote migration:", e, file=sys.stderr)
            return 3
    print("== HueLeaf 安全状态检查（只读） ==")
    hl_preflight.check_security(args.port)
    print("== HueLeaf 安装预检（写入前） ==")
    rc = hl_preflight.main(["check", "--port", args.port, "--ptable", os.path.join(here, "partition-table.bin"),
                            "--backup-dir", backup_dir,
                            *(["--skip-backup"] if args.skip_backup else []),
                            *(["--force-backup"] if args.force_backup else [])])
    if rc != 0:
        return rc
    print("== 写入 bootloader / 分区表 / otadata / ota_0 ==")
    return subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32c5", "-p", args.port, "-b", "460800",
                           "write_flash", "0x2000", "bootloader.bin", "0x8000", "partition-table.bin",
                           "0x180000", "otadata_ota0.bin", "0x190000", "gallery_ota0.bin"], cwd=here).returncode


if __name__ == "__main__":
    sys.exit(main())
