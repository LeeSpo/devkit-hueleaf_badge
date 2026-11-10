#!/usr/bin/env python3
"""HueLeaf first-install preflight: back up flash, check the stock app fits.

Our partitions.csv shrinks `factory` (stock app, 0x10000) to 0x170000 and puts
otadata at 0x180000 / ota_0 at 0x190000. If the stock app on the device is
larger, writing those would destroy its tail. Before anything is written:

  1. partition table @0x8000 is read first;
  2. full 4 MiB backup (read_flash 0 0x400000) with a SHA-256 sidecar. First
     install / unknown layout: always a new snapshot. Layout already ours
     (routine update): a verified backup of the same chip younger than
     BACKUP_REUSE_HOURS is reused; --force-backup always takes a new one,
     --skip-backup takes none (refused unless the layout is already ours);
  3. a new snapshot is walked at 0x10000 (ESP app header/segments, checksum
     padding + optional SHA-256) to get the real stock image size, aborting if
     > 0x170000. Not repeated when a routine update reuses/skips the backup.

Exit 0 = safe to write; any other exit = nothing may be written.

  hl_preflight.py check  --port P --ptable partition-table.bin [--backup-dir D]
                         [--skip-backup | --force-backup]
  hl_preflight.py image-size FILE [--expect-file-size]
  hl_preflight.py ptable FILE
"""
import argparse
import glob
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
import time
import tempfile

FLASH_SIZE = 0x400000
PTABLE_OFF, PTABLE_LEN = 0x8000, 0xC00
APP_OFF = 0x10000
FACTORY_LIMIT = 0x170000
CHIP = "esp32c5"
BACKUP_REUSE_HOURS = 24

IMAGE_MAGIC = 0xE9
HDR_LEN = 24          # esp_image_header_t (8 common + 16 extended)
SEG_HDR_LEN = 8
MAX_SEGMENTS = 16


class ImageError(Exception):
    pass


class Truncated(ImageError):
    """Image extends past the data we have; min_size is a lower bound."""
    def __init__(self, min_size):
        super().__init__(f"image extends past available data (>= {min_size:#x})")
        self.min_size = min_size


def app_image_size(data, offset=0):
    """Size in bytes of the ESP image starting at data[offset:].

    Same layout as esptool/bootloader: 24-byte header, segment_count x
    (8-byte header + data), pad so that the checksum byte ends on a 16-byte
    boundary, then 32-byte SHA-256 if header byte 23 (hash_appended) == 1.
    A secure-boot signature block (not used on stock HueLeaf) is not counted.
    """
    d = memoryview(data)[offset:]
    if len(d) < HDR_LEN:
        raise Truncated(HDR_LEN)
    if d[0] != IMAGE_MAGIC:
        raise ImageError(f"no ESP image (magic {d[0]:#04x}, expected 0xe9)")
    nseg = d[1]
    if not 1 <= nseg <= MAX_SEGMENTS:
        raise ImageError(f"implausible segment count {nseg}")
    hash_appended = d[23] == 1
    pos = HDR_LEN
    for i in range(nseg):
        if pos + SEG_HDR_LEN > len(d):
            raise Truncated(pos + SEG_HDR_LEN)
        _addr, seg_len = struct.unpack_from("<II", d, pos)
        if seg_len > FLASH_SIZE:
            raise ImageError(f"segment {i} length {seg_len:#x} implausible")
        pos += SEG_HDR_LEN + seg_len
        if pos > len(d):
            raise Truncated(pos)
    size = pos + 1                      # checksum byte
    size = (size + 15) & ~15            # padded so it ends 16-aligned
    if hash_appended:
        size += 32
    if size > len(d):
        raise Truncated(size)
    return size


PT_MAGIC = b"\xaa\x50"
PT_MD5_MAGIC = b"\xeb\xeb"


def parse_ptable(data):
    """List of (label, type, subtype, offset, size, flags). Verifies MD5 if present."""
    out = []
    for i in range(0, min(len(data), PTABLE_LEN), 32):
        e = data[i:i + 32]
        if len(e) < 32 or e == b"\xff" * 32:
            break
        if e[:2] == PT_MD5_MAGIC:
            if hashlib.md5(data[:i]).digest() != e[16:32]:
                raise ImageError("partition table MD5 mismatch")
            break
        if e[:2] != PT_MAGIC:
            raise ImageError(f"bad partition entry magic at +{i:#x}")
        t, st, off, size = struct.unpack_from("<BBII", e, 2)
        label = e[12:28].split(b"\0")[0].decode("ascii", "replace")
        flags = struct.unpack_from("<I", e, 28)[0]
        out.append((label, t, st, off, size, flags))
    return out


def same_layout(a, b):
    return [p[:5] for p in a] == [p[:5] for p in b]


def factory_of(table):
    for label, t, st, off, size, _ in table:
        if t == 0 and st == 0x00:
            return off, size
    return None


# ---------------------------------------------------------------- device I/O
SECURITY_FIELDS = ("SECURE_BOOT_EN", "SPI_BOOT_CRYPT_CNT", "RD_DIS",
                   "DIS_DOWNLOAD_MODE", "ENABLE_SECURITY_DOWNLOAD",
                   "DIS_USB_SERIAL_JTAG_DOWNLOAD_MODE")


def check_security(port):
    """Read-only eFuse summary; fail closed for protected/unknown devices.

    Returns the chip's eFuse MAC as 12 lower-case hex digits, or None when the
    summary does not carry one. The summary lives in a temporary directory and
    is removed again.
    """
    with tempfile.TemporaryDirectory(prefix="hueleaf_efuse_") as tmp:
        path = os.path.join(tmp, "summary.json")
        cmd = [sys.executable, "-m", "espefuse", "--chip", CHIP, "--port", port,
               "summary", "--format", "json", "--file", path]
        if subprocess.run(cmd).returncode != 0:
            fail("无法读取 eFuse 安全状态。")
        try:
            with open(path) as f:
                summary = json.load(f)
            for name in SECURITY_FIELDS:
                field = summary[name]
                if not field.get("readable", False):
                    raise ValueError(name + " 不可读")
                # espefuse uses human strings such as "Disable" for some value
                # fields; raw_value is the unambiguous bit pattern in current IDF.
                value = field.get("raw_value", field.get("value"))
                if isinstance(value, bool):
                    enabled = value
                elif isinstance(value, int):
                    enabled = value != 0
                elif isinstance(value, str):
                    enabled = int(value, 0) != 0
                else:
                    raise ValueError(name + " 无法解析")
                if enabled:
                    fail("检测到安全/读取保护设置 " + name + "，拒绝自动安装。")
        except (KeyError, ValueError, TypeError, OSError) as e:
            fail("eFuse 状态不完整或无法解析：" + str(e))
    print("[OK] 安全状态检查通过（仅查询，没有写入 eFuse）。")
    mac = summary.get("MAC")
    found = re.search(r"(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}",
                      str(mac.get("value", "")) if isinstance(mac, dict) else "")
    return found.group(0).replace(":", "").lower() if found else None


def esptool(port, *args, capture=False):
    cmd = [sys.executable, "-m", "esptool", "--chip", CHIP]
    if port:
        cmd += ["-p", port]
    cmd += ["-b", "460800", *args]
    print("  $", " ".join(cmd), flush=True)
    if capture:
        r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        sys.stdout.write(r.stdout)
        return r.returncode, r.stdout
    return subprocess.run(cmd).returncode, ""


def read_region(port, off, length, path):
    if os.path.exists(path):
        os.remove(path)
    rc, _ = esptool(port, "read_flash", hex(off), hex(length), path)
    if rc != 0 or not os.path.exists(path) or os.path.getsize(path) != length:
        got = os.path.getsize(path) if os.path.exists(path) else 0
        raise SystemExit(f"\n[中止] 读取 Flash {off:#x}+{length:#x} 失败（esptool 返回 {rc}，"
                         f"得到 {got} 字节）。未写入任何内容。")
    with open(path, "rb") as f:
        return f.read()


def fail(msg):
    print("\n[中止] " + msg + "\n未写入任何内容。", file=sys.stderr)
    raise SystemExit(3)


def read_live(port, off, length):
    """Read a flash region into memory without leaving files behind."""
    with tempfile.TemporaryDirectory(prefix="hueleaf_read_") as tmp:
        return read_region(port, off, length, os.path.join(tmp, "region.bin"))


def make_backup(port, directory, prefix):
    """Full 4 MiB read -> {prefix}{time}_{rand}.bin + .sha256. Returns (path, image)."""
    os.makedirs(directory, exist_ok=True)
    fd, tmp = tempfile.mkstemp(prefix=f"{prefix}{time.strftime('%Y%m%d-%H%M%S')}_", suffix=".part", dir=directory)
    os.close(fd)
    path = tmp[:-5] + ".bin"
    print(f"      整片备份 4 MiB → {path}（约 1–2 分钟）")
    try:
        image = read_region(port, 0, FLASH_SIZE, tmp)
    except BaseException:
        if os.path.exists(tmp):
            os.remove(tmp)
        raise
    os.replace(tmp, path)
    digest = hashlib.sha256(image).hexdigest()
    with open(path + ".sha256", "w") as f:
        f.write(digest + "  " + os.path.basename(path) + "\n")
    with open(path, "rb") as f:
        if hashlib.sha256(f.read()).hexdigest() != digest:
            fail("备份文件校验失败：" + path)
    print(f"      备份完成：{len(image)} 字节，SHA256 {digest}")
    return path, image


def find_recent_backup(directory, prefix, max_age_hours=BACKUP_REUSE_HOURS):
    """Newest complete backup `{prefix}*.bin` whose SHA-256 sidecar verifies and
    that is younger than max_age_hours; None if there is none."""
    cutoff = time.time() - max_age_hours * 3600
    candidates = []
    for path in glob.glob(os.path.join(glob.escape(directory), glob.escape(prefix) + "*.bin")):
        try:
            candidates.append((os.path.getmtime(path), path))
        except OSError:
            continue
    for mtime, path in sorted(candidates, reverse=True):
        if mtime < cutoff:
            break
        try:
            if os.path.getsize(path) != FLASH_SIZE:
                continue
            with open(path + ".sha256") as f:
                expected = f.read().split()[0]
            with open(path, "rb") as f:
                if hashlib.sha256(f.read()).hexdigest() == expected:
                    return path
        except (OSError, IndexError):
            continue
    return None


def cmd_check(a):
    ours = parse_ptable(open(a.ptable, "rb").read())
    bdir = a.backup_dir or os.getcwd()
    skip, force = bool(getattr(a, "skip_backup", False)), bool(getattr(a, "force_backup", False))
    os.makedirs(bdir, exist_ok=True)

    print("[1/3] 读取芯片 MAC …")
    rc, out = esptool(a.port, "read_mac", capture=True)
    m = re.search(r"MAC:\s*([0-9a-fA-F:]{17})", out or "")
    if rc != 0 or not m:
        fail("无法连接芯片或读取 MAC（检查端口、按住 BOOT 再插 USB）。")
    mac = m.group(1).lower().replace(":", "")

    print("[2/3] 读取当前分区表 @0x8000 …")
    try:
        cur = parse_ptable(read_live(a.port, PTABLE_OFF, PTABLE_LEN))
    except ImageError as e:
        cur = []
        print(f"      分区表无法解析（{e}），按原厂/未知处理")
    is_ours = bool(cur) and same_layout(cur, ours)
    if is_ours:
        print("      已是 HueLeaf 分区布局（日常更新）。")
    elif cur:
        f = factory_of(cur)
        print(f"      当前为原厂/其它布局（factory {f[0]:#x}+{f[1]:#x}）" if f else "      当前布局无 factory 分区")

    image = None
    prefix = f"hueleaf_backup_{mac}_"
    if skip and not is_ours:
        fail("首次安装/未知布局必须先整片备份，不能使用 --skip-backup。")
    if skip:
        print("      已按 --skip-backup 跳过整片备份。")
    else:
        recent = find_recent_backup(bdir, prefix) if is_ours and not force else None
        if recent:
            print(f"      复用 {BACKUP_REUSE_HOURS} 小时内已校验的本芯片备份：{recent}（--force-backup 可强制新备份）")
        else:
            _, image = make_backup(a.port, bdir, prefix)

    if image is None:
        print("[3/3] 已是 HueLeaf 分区布局且未新建备份，不重复检查原厂固件大小。")
        print("[OK] 预检通过，可以写入。")
        return 0
    print(f"[3/3] 计算 0x10000 处原厂固件实际大小（上限 {FACTORY_LIMIT:#x} = {FACTORY_LIMIT} 字节）…")
    try:
        size = app_image_size(image[APP_OFF:])
    except Truncated as e:
        fail(f"原厂固件大小至少 {e.min_size:#x}（{e.min_size} 字节），超过新 factory 分区上限 "
             f"{FACTORY_LIMIT:#x}（{FACTORY_LIMIT} 字节）。继续安装会覆盖原厂固件尾部。")
    except ImageError as e:
        fail(f"0x10000 处不是有效的 ESP 固件镜像（{e}）。无法确认原厂固件大小，"
             f"为安全起见不继续。若确认该处为空，可先恢复官方固件再安装。")
    if size > FACTORY_LIMIT:
        fail(f"原厂固件实际大小 {size:#x}（{size} 字节）超过新 factory 分区上限 "
             f"{FACTORY_LIMIT:#x}（{FACTORY_LIMIT} 字节），超出 {size - FACTORY_LIMIT} 字节。"
             f"继续安装会覆盖原厂固件尾部。")
    print(f"      原厂固件 {size:#x}（{size} 字节） ≤ 上限 {FACTORY_LIMIT:#x}，余量 {FACTORY_LIMIT - size} 字节。")
    print("[OK] 预检通过，可以写入。")
    return 0


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("check")
    c.add_argument("--port")
    c.add_argument("--ptable", required=True)
    c.add_argument("--backup-dir")
    backup = c.add_mutually_exclusive_group()
    backup.add_argument("--skip-backup", action="store_true", help="No full backup (only when the layout is already ours)")
    backup.add_argument("--force-backup", action="store_true", help="Always take a new backup instead of reusing a recent one")
    s = sub.add_parser("image-size")
    s.add_argument("file")
    s.add_argument("--offset", type=lambda x: int(x, 0), default=0)
    s.add_argument("--expect-file-size", action="store_true")
    t = sub.add_parser("ptable")
    t.add_argument("file")
    a = p.parse_args(argv)
    if a.cmd == "check":
        return cmd_check(a)
    data = open(a.file, "rb").read()
    if a.cmd == "ptable":
        for e in parse_ptable(data[PTABLE_OFF:] if len(data) > PTABLE_LEN else data):
            print("%-12s type=%#04x sub=%#04x off=%#08x size=%#08x" % e[:5])
        return 0
    size = app_image_size(data, a.offset)
    print(f"{size:#x} {size}")
    if a.expect_file_size and size != len(data) - a.offset:
        print(f"MISMATCH: file is {len(data) - a.offset} bytes", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
