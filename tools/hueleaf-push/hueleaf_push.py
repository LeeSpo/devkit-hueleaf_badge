#!/usr/bin/env python3
"""hueleaf-push — convert images and push them to a HueLeaf over USB (no Wi-Fi).

  hueleaf-push photo.jpg                 # convert, upload, show
  hueleaf-push photo.jpg --preview out.png   # local dithered preview only
  hueleaf-push photo.jpg --mode illustration --orientation landscape --fit pad
  hueleaf-push --gui                     # desktop GUI (browser)
  hueleaf-push --order b.bin a.bin       # display order (img/index.txt)
  hueleaf-push --list | --info | --delete NAME | --show NAME | --log
  hueleaf-push --firmware build/hueleaf_gallery.bin   # copies to TF update/ and reboots
"""
from __future__ import annotations

import argparse, datetime, json, os, re, struct, sys, time, zlib

import hlformat as F

MAGIC = b"HLP1"
CMD = dict(PING=0, INFO=1, LIST=2, UP_BEGIN=3, UP_DATA=4, UP_END=5, DELETE=6, SHOW=7,
           NAV=8, LOG=9, REBOOT=10, SET_TIME=11, STRIP=12, BOOT_STOCK=13, SET_ORDER=14, LABELS=15)
STATUS = {0: "ok", -1: "error", -2: "crc/verify mismatch", -3: "unknown command", -4: "not found",
          -5: "bad argument", -6: "no TF card", -7: "I/O error", -8: "device not ready"}
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
    def __init__(self, port, timeout: float = 3.0):
        """port: device path/COM name, or an already-open serial-like object."""
        if isinstance(port, str):
            import serial
            self.s = serial.Serial(port, 115200, timeout=0.2)
        else:
            self.s = port
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

    def close(self):
        try:
            self.s.close()
        except Exception:
            pass

    def list(self) -> list[str]:
        """Images on the card in display order (index.txt first, rest A-Z)."""
        return [l for l in self.ok("LIST").decode(errors="replace").splitlines() if l]

    def delete(self, name: str):
        self.ok("DELETE", name.encode())

    def set_order(self, names: list[str]):
        """Write img/index.txt. Empty list removes it (alphabetical order)."""
        for n in names:
            if not re.fullmatch(r"[A-Za-z0-9_-][A-Za-z0-9._-]{0,54}\.bin", n):
                raise ValueError(f"bad name: {n!r}")
        self.ok("SET_ORDER", "".join(n + "\n" for n in names).encode())

    def upload_image(self, name: str, fr: bytes, display: str | None = None, progress=True) -> bool:
        """Upload an image, then its picker name label. Returns False if the
        firmware is too old for labels (image is still uploaded)."""
        self.upload(0, name, fr, progress)
        return self.upload_label(name, display or os.path.splitext(name)[0])

    def upload_label(self, name: str, display: str) -> bool:
        import hlpicker
        try:
            self.upload(2, name, hlpicker.make_label(display), progress=False)
            return True
        except RuntimeError as e:
            if "bad argument" in str(e) or "unknown command" in str(e):
                return False        # firmware < 1.2.0
            raise

    def labels(self) -> dict[str, str]:
        """card name -> display name (images uploaded with a label)."""
        st, data = self.call("LABELS")
        if st != 0:
            return {}
        out = {}
        for line in data.decode(errors="replace").splitlines():
            if "\t" in line:
                k, v = line.split("\t", 1)
                out[k] = v
        return out

    def upload(self, kind: int, name: str, blob: bytes, progress=True):
        """progress: True = stderr bar, False = silent, callable(done, total)."""
        self.ok("UP_BEGIN", struct.pack("<BII", kind, len(blob), zlib.crc32(blob)) + name.encode())
        t0 = time.time()
        for off in range(0, len(blob), CHUNK):
            self.ok("UP_DATA", struct.pack("<I", off) + blob[off:off + CHUNK])
            if callable(progress):
                progress(min(off + CHUNK, len(blob)), len(blob))
            elif progress:
                done = min(off + CHUNK, len(blob))
                bar = int(30 * done / len(blob))
                rate = done / max(time.time() - t0, 1e-3) / 1024
                sys.stderr.write(f"\r[{'#' * bar}{'.' * (30 - bar)}] {done * 100 // len(blob):3d}% {rate:6.1f} KiB/s")
        if progress is True:
            sys.stderr.write("\n")
        self.ok("UP_END", timeout=10)


ESPRESSIF_VID = 0x303A


def find_ports():
    """[(device, description, likely_hueleaf)], likely ESP32 first."""
    from serial.tools import list_ports
    out = []
    for p in list_ports.comports():
        if any(k in p.device for k in ("Bluetooth", "debug-console", "wlan-debug")):
            continue        # macOS pseudo ports
        if p.device.startswith("/dev/tty.") and sys.platform == "darwin":
            continue        # macOS: use the /dev/cu.* twin (no DCD wait)
        hit = p.vid == ESPRESSIF_VID or "JTAG" in (p.description or "")
        out.append((p.device, p.description or "", hit))
    out.sort(key=lambda t: (not t[2], t[0]))
    return out


def auto_port():
    c = [d for d, _, hit in find_ports() if hit]
    return c[0] if c else None


def display_stem(path: str) -> str:
    return os.path.splitext(os.path.basename(path))[0].strip() or "img"


def card_name(display: str) -> str:
    """Card file name (ASCII) for a display name. Non-ASCII names (e.g.
    Chinese) get a short hash so different names don't collide; the real name
    lives in the picker label."""
    stem = display.strip() or "img"
    base = re.sub(r"[^A-Za-z0-9._-]+", "_", stem).strip("._")
    if not stem.isascii():
        base = (base[:40] + "-" if base else "img-") + f"{zlib.crc32(stem.encode()):08x}"[:6]
    return (base or "img")[:50] + ".bin"


def safe_name(path: str) -> str:
    return card_name(display_stem(path))


def main(argv=None):
    ap = argparse.ArgumentParser(prog="hueleaf-push", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", nargs="?")
    ap.add_argument("-p", "--port", default=os.environ.get("HUELEAF_PORT"),
                    help="serial port (e.g. /dev/ttyACM0, COM5); env HUELEAF_PORT")
    ap.add_argument("--name", help="name on card (default from file name)")
    ap.add_argument("--label", help="name shown in the on-device picker (default: file name, any language)")
    ap.add_argument("--rotate", type=int, default=0, choices=[0, 90, 180, 270], help="rotate CCW first")
    ap.add_argument("--fit", choices=["crop", "pad"], default="crop",
                    help="crop = fill panel and crop edges; pad = whole image with white border")
    ap.add_argument("--mode", choices=F.MODES, default=None,
                    help="photo = Floyd-Steinberg dithering (default); illustration = flat nearest colour")
    ap.add_argument("--orientation", choices=F.ORIENTATIONS, default=None,
                    help="landscape rotates 90 deg so a wide picture fills the panel (overrides --rotate)")
    ap.add_argument("--no-dither", action="store_true", help="same as --mode illustration")
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
    ap.add_argument("--order", nargs="*", metavar="NAME",
                    help="set display order (writes img/index.txt); no names = alphabetical")
    ap.add_argument("--ports", action="store_true", help="list serial ports and exit")
    ap.add_argument("--gui", action="store_true", help="open the desktop GUI in your browser")
    ap.add_argument("--stock", action="store_true", help="reboot once into the stock firmware")
    a = ap.parse_args(argv)
    F.CHROMA_W = a.chroma
    style = a.mode or ("illustration" if a.no_dither else "photo")
    if a.gui:
        import hueleaf_gui
        return hueleaf_gui.main(port=a.port)
    if a.ports:
        for dev, desc, hit in find_ports():
            print(f"{'*' if hit else ' '} {dev}  {desc}")
        return 0

    if a.image and (a.preview or a.save_bin):
        ids, fr = F.convert(a.image, a.rotate, a.fit, style=style, orientation=a.orientation)
        if a.preview:
            F.render_frame(fr).save(a.preview)
            print(f"preview -> {a.preview}")
        if a.save_bin:
            open(a.save_bin, "wb").write(fr)
            print(f"frame -> {a.save_bin} ({len(fr)} bytes)")
        return 0
    a.port = a.port or auto_port()
    if not a.port:
        ap.error("no HueLeaf found; pass --port (or set HUELEAF_PORT); see --ports")
    d = Device(a.port)
    if a.image:
        _, fr = F.convert(a.image, a.rotate, a.fit, style=style, orientation=a.orientation)
        name = a.name or safe_name(a.image)
        if not name.endswith(".bin"):
            name += ".bin"
        if not d.upload_image(name, fr, a.label or display_stem(a.image)):
            print("note: firmware < 1.2.0, no picker label uploaded")
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
    if a.order is not None:
        d.set_order(a.order)
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
