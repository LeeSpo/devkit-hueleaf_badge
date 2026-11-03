#!/usr/bin/env python3
"""hueleaf-push — convert images and push them to a HueLeaf over USB (no Wi-Fi).

  hueleaf-push photo.jpg                 # convert, upload, show
  hueleaf-push photo.jpg --preview out.png   # local dithered preview only
  hueleaf-push --list | --info | --delete NAME | --show NAME | --log
  hueleaf-push --firmware build/hueleaf_gallery.bin   # copies to TF update/ and reboots
"""
from __future__ import annotations

import argparse, datetime, json, os, re, struct, sys, time, zlib

import hlformat as F

MAGIC = b"HLP1"
CMD = dict(PING=0, INFO=1, LIST=2, UP_BEGIN=3, UP_DATA=4, UP_END=5, DELETE=6, SHOW=7,
           NAV=8, LOG=9, REBOOT=10, SET_TIME=11, STRIP=12, BOOT_STOCK=13)
STATUS = {0: "ok", -1: "error", -2: "crc/verify mismatch", -3: "unknown command", -4: "not found",
          -5: "bad argument", -6: "no TF card", -7: "I/O error"}
CHUNK = 4096


def frame(cmd: int, seq: int, payload: bytes = b"") -> bytes:
    body = struct.pack("<BBHI", cmd, seq & 0xFF, 0, len(payload)) + payload
    return MAGIC + body + struct.pack("<I", zlib.crc32(body))


def parse_reply(raw: bytes):
    if raw[:4] != MAGIC:
        raise ValueError("magic")
    cmd, seq, _, ln = struct.unpack("<BBHI", raw[4:12])
    body = raw[4:12 + ln]
    if zlib.crc32(body) != struct.unpack("<I", raw[12 + ln:16 + ln])[0]:
        raise ValueError("crc")
    status = struct.unpack("<i", raw[12:16])[0]
    return cmd, seq, status, raw[16:12 + ln]


class Device:
    def __init__(self, port: str, timeout: float = 3.0):
        import serial
        self.s = serial.Serial(port, 115200, timeout=0.2)
        self.timeout, self.seq = timeout, 0

    def _read_frame(self, deadline):
        buf = b""
        while time.time() < deadline:
            buf += self.s.read(max(1, self.s.in_waiting))
            i = buf.find(MAGIC)
            if i < 0:
                buf = buf[-3:]
                continue
            buf = buf[i:]
            if len(buf) >= 12:
                ln = struct.unpack("<I", buf[8:12])[0]
                if len(buf) >= 16 + ln:
                    return buf[:16 + ln]
        return None

    def call(self, name: str, payload: bytes = b"", retries: int = 4, timeout=None):
        cmd = CMD[name]
        for _ in range(retries):
            self.seq = (self.seq + 1) & 0xFF
            self.s.write(frame(cmd, self.seq, payload))
            deadline = time.time() + (timeout or self.timeout)
            while True:
                raw = self._read_frame(deadline)
                if raw is None:
                    break
                try:
                    rc, rs, st, data = parse_reply(raw)
                except ValueError:
                    continue
                if rc == cmd | 0x80 and rs == self.seq:
                    if st == -2 and name != "UP_END":
                        break           # corrupted on the wire: resend
                    return st, data
        raise TimeoutError(f"{name}: no valid reply")

    def ok(self, name, payload=b"", **kw):
        st, data = self.call(name, payload, **kw)
        if st != 0:
            raise RuntimeError(f"{name}: {STATUS.get(st, st)}")
        return data

    def upload(self, kind: int, name: str, blob: bytes, progress=True):
        self.ok("UP_BEGIN", struct.pack("<BII", kind, len(blob), zlib.crc32(blob)) + name.encode())
        t0 = time.time()
        for off in range(0, len(blob), CHUNK):
            self.ok("UP_DATA", struct.pack("<I", off) + blob[off:off + CHUNK])
            if progress:
                done = min(off + CHUNK, len(blob))
                bar = int(30 * done / len(blob))
                rate = done / max(time.time() - t0, 1e-3) / 1024
                sys.stderr.write(f"\r[{'#' * bar}{'.' * (30 - bar)}] {done * 100 // len(blob):3d}% {rate:6.1f} KiB/s")
        if progress:
            sys.stderr.write("\n")
        self.ok("UP_END", timeout=10)


def safe_name(path: str) -> str:
    base = os.path.splitext(os.path.basename(path))[0]
    base = re.sub(r"[^A-Za-z0-9._-]+", "_", base).strip("._") or "img"
    return base[:50] + ".bin"


def main(argv=None):
    ap = argparse.ArgumentParser(prog="hueleaf-push", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", nargs="?")
    ap.add_argument("-p", "--port", default=os.environ.get("HUELEAF_PORT"),
                    help="serial port (e.g. /dev/ttyACM0, COM5); env HUELEAF_PORT")
    ap.add_argument("--name", help="name on card (default from file name)")
    ap.add_argument("--rotate", type=int, default=0, choices=[0, 90, 180, 270], help="rotate CCW first")
    ap.add_argument("--fit", choices=["crop", "pad"], default="crop")
    ap.add_argument("--no-dither", action="store_true")
    ap.add_argument("--chroma", type=float, default=F.CHROMA_W,
                    help="ink-selection chroma weight (lower = more colourful greys)")
    ap.add_argument("--preview", metavar="OUT.png", help="write dithered preview and exit")
    ap.add_argument("--save-bin", metavar="OUT.bin", help="write packed frame (copy to TF img/ manually)")
    ap.add_argument("--no-show", action="store_true")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--info", action="store_true")
    ap.add_argument("--log", action="store_true")
    ap.add_argument("--delete", metavar="NAME")
    ap.add_argument("--show", metavar="NAME")
    ap.add_argument("--next", action="store_true")
    ap.add_argument("--prev", action="store_true")
    ap.add_argument("--strip", choices=["on", "off"])
    ap.add_argument("--set-time", action="store_true", help="set device RTC to this PC's local time")
    ap.add_argument("--firmware", metavar="APP.bin", help="upload app to TF update/firmware.bin and reboot")
    ap.add_argument("--stock", action="store_true", help="reboot once into the stock firmware")
    a = ap.parse_args(argv)
    F.CHROMA_W = a.chroma

    if a.image and (a.preview or a.save_bin):
        ids, fr = F.convert(a.image, a.rotate, a.fit, not a.no_dither)
        if a.preview:
            F.preview(ids).save(a.preview)
            print(f"preview -> {a.preview}")
        if a.save_bin:
            open(a.save_bin, "wb").write(fr)
            print(f"frame -> {a.save_bin} ({len(fr)} bytes)")
        return 0
    if not a.port:
        ap.error("--port required (or set HUELEAF_PORT)")
    d = Device(a.port)
    if a.image:
        _, fr = F.convert(a.image, a.rotate, a.fit, not a.no_dither)
        name = a.name or safe_name(a.image)
        if not name.endswith(".bin"):
            name += ".bin"
        d.upload(0, name, fr)
        print(f"uploaded {name}")
        if not a.no_show:
            d.ok("SHOW", name.encode())
    if a.firmware:
        blob = open(a.firmware, "rb").read()
        if blob[0] != 0xE9:
            sys.exit("not an ESP app image")
        d.upload(1, "firmware.bin", blob)
        d.ok("REBOOT")
        print("firmware staged; device rebooting to apply")
    if a.set_time:
        n = datetime.datetime.now()
        d.ok("SET_TIME", struct.pack("<HBBBBB", n.year, n.month, n.day, n.hour, n.minute, n.second))
    if a.strip:
        d.ok("STRIP", bytes([a.strip == "on"]))
    if a.delete:
        d.ok("DELETE", a.delete.encode())
    if a.show:
        d.ok("SHOW", a.show.encode())
    if a.next or a.prev:
        d.ok("NAV", struct.pack("<b", 1 if a.next else -1))
    if a.list:
        print(d.ok("LIST").decode(errors="replace"), end="")
    if a.info:
        print(json.dumps(json.loads(d.ok("INFO")), indent=2, ensure_ascii=False))
    if a.log:
        print(d.ok("LOG").decode(errors="replace"))
    if a.stock:
        d.ok("BOOT_STOCK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
