#!/usr/bin/env python3
"""HueLeaf first-install preflight: back up flash, check the stock app fits.

Our partitions.csv shrinks `factory` (stock app, 0x10000) to 0x170000 and puts
otadata at 0x180000 / ota_0 at 0x190000. If the stock app on the device is
larger, writing those would destroy its tail. Before anything is written:

  1. full 4 MiB backup (read_flash 0 0x400000) next to the install script,
     size verified; reused if a complete backup for this chip MAC exists;
  2. partition table @0x8000: already our layout -> stock check skipped;
  3. otherwise walk the ESP app image header/segments @0x10000 to get the real
     image size (checksum padding + optional SHA-256) and abort if > 0x170000.

Exit 0 = safe to write; any other exit = nothing may be written.

  hl_preflight.py check  --port P --ptable partition-table.bin [--backup-dir D]
  hl_preflight.py image-size FILE [--expect-file-size]
  hl_preflight.py ptable FILE
"""
import argparse
import glob
import hashlib
import os
import re
import struct
import subprocess
import sys
import time

FLASH_SIZE = 0x400000
PTABLE_OFF, PTABLE_LEN = 0x8000, 0xC00
APP_OFF = 0x10000
FACTORY_LIMIT = 0x170000
CHIP = "esp32c5"

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


def cmd_check(a):
    ours = parse_ptable(open(a.ptable, "rb").read())
    bdir = a.backup_dir or os.getcwd()
    os.makedirs(bdir, exist_ok=True)

    print("[1/3] 读取芯片 MAC …")
    rc, out = esptool(a.port, "read_mac", capture=True)
    m = re.search(r"MAC:\s*([0-9a-fA-F:]{17})", out or "")
    if rc != 0 or not m:
        fail("无法连接芯片或读取 MAC（检查端口、按住 BOOT 再插 USB）。")
    mac = m.group(1).lower().replace(":", "")

    existing = [p for p in sorted(glob.glob(os.path.join(bdir, f"hueleaf_backup_{mac}_*.bin")))
                if os.path.getsize(p) == FLASH_SIZE]
    image = None
    if existing:
        print(f"[1/3] 已有本芯片完整备份，跳过重新备份：{existing[-1]}")
    else:
        path = os.path.join(bdir, f"hueleaf_backup_{mac}_{time.strftime('%Y%m%d-%H%M%S')}.bin")
        print(f"[1/3] 整片备份 4 MiB → {path}（约 1–2 分钟）")
        tmp = path + ".part"
        image = read_region(a.port, 0, FLASH_SIZE, tmp)
        os.replace(tmp, path)
        print(f"      备份完成：{len(image)} 字节，SHA256 {hashlib.sha256(image).hexdigest()[:16]}…")

    print("[2/3] 读取当前分区表 @0x8000 …")
    pt = image[PTABLE_OFF:PTABLE_OFF + PTABLE_LEN] if image else \
        read_region(a.port, PTABLE_OFF, PTABLE_LEN, os.path.join(bdir, ".hl_ptable.tmp"))
    try:
        cur = parse_ptable(pt)
    except ImageError as e:
        cur = []
        print(f"      分区表无法解析（{e}），按原厂/未知处理")
    if cur and same_layout(cur, ours):
        print("      已是 HueLeaf 分区布局，跳过原厂固件大小检查。")
        print("[OK] 预检通过，可以写入。")
        return 0
    if cur:
        f = factory_of(cur)
        print(f"      当前为原厂/其它布局（factory {f[0]:#x}+{f[1]:#x}）" if f else "      当前布局无 factory 分区")

    print(f"[3/3] 计算 0x10000 处原厂固件实际大小（上限 {FACTORY_LIMIT:#x} = {FACTORY_LIMIT} 字节）…")
    if image:
        app = image[APP_OFF:]
    else:
        app = read_region(a.port, APP_OFF, FACTORY_LIMIT + 0x1000, os.path.join(bdir, ".hl_app.tmp"))
    try:
        size = app_image_size(app)
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
