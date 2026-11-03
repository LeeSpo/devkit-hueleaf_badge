#!/usr/bin/env python3
"""Offline scan of a stock HueLeaf firmware dump for FrameFilm-style RAM
waveform tables (799 bytes, loaded with `00 0F A9` + `20` + LUT).

  scan_factory_lut.py --tables hal_epd_370.c FILE [FILE...]

* parses all `static const uint8_t NAME[799] = {...}` arrays from the C file
  (FrameFilm d411d93 frame_film_ark/.../hal_epd_370.c), dedups them
* exact search of every distinct table
* fuzzy: every window that starts with 7 zero bytes and is not mostly
  0x00/0xFF filler; bit Hamming distance to each table; best few reported
* RISC-V `addi rd, rs, 0x31F` (32-bit) and 0x031F LE16 data constants, flagged
  when a 0x20 immediate / byte is within +-64 bytes
* ASCII strings mentioning fast/quick/lut/partial/mono/wave/otp
* ESP image segments (at 0x0 app or 0x10000 in a merged image) to label offsets
"""
import argparse, hashlib, re, struct, sys

TLEN = 799


def parse_tables(src):
    out = {}
    for m in re.finditer(r"static\s+const\s+uint8_t\s+(\w+)\s*\[\s*799\s*\]\s*=\s*\{(.*?)\};", src, re.S):
        vals = [int(v, 0) for v in re.findall(r"0x[0-9a-fA-F]+|\d+", m.group(2))]
        if len(vals) != TLEN:
            raise SystemExit(f"{m.group(1)}: {len(vals)} values")
        out[m.group(1)] = bytes(vals)
    return out


def dedup(tables):
    seen, res = {}, {}
    for n, b in tables.items():
        if b in seen:
            res[seen[b]].append(n)
        else:
            seen[b] = n; res[n] = [n]
    return {"/".join(v): tables[k] for k, v in res.items()}


def segments(data):
    """Return list of (file_off, len, load_addr, label) for app images found."""
    res = []
    merged = len(data) > 0x10000 and data[0x10000] == 0xE9
    for base in (0x0, 0x2000, 0x10000):
        if base + 24 > len(data) or data[base] != 0xE9 or (base and not merged):
            continue
        n = data[base + 1]
        label0 = "app" if base == 0x10000 or not merged else "bootloader"
        p = base + 24
        for i in range(n):
            if p + 8 > len(data):
                break
            addr, ln = struct.unpack_from("<II", data, p)
            res.append((p + 8, ln, addr, f"{label0}@{base:#x} seg{i}"))
            p += 8 + ln
    return res


def label(segs, off):
    for so, ln, addr, name in segs:
        if so <= off < so + ln:
            return f"{name} load {addr + off - so:#010x}"
    if 0x8000 <= off < 0x8C00:
        return "partition table"
    if 0x9000 <= off < 0xF000:
        return "nvs (0x9000)"
    return "outside image segments"


POP = bytes(bin(i).count("1") for i in range(256))


def ham(a, b):
    x = int.from_bytes(a, "little") ^ int.from_bytes(b, "little")
    return bin(x).count("1")


def scan(path, tables, top):
    data = open(path, "rb").read()
    print(f"\n=== {path}: {len(data)} bytes sha256 {hashlib.sha256(data).hexdigest()}")
    segs = segments(data)
    for so, ln, addr, name in segs:
        print(f"  {name}: file {so:#08x}+{ln:#x} load {addr:#010x}")

    print("-- exact table search")
    for n, t in tables.items():
        hits = [m.start() for m in re.finditer(re.escape(t), data)]
        print(f"  {n}: {len(hits)} hit(s)" + "".join(f"\n    {h:#x} ({label(segs, h)})" for h in hits))
        sub = t[7:7 + 88]
        sh = [m.start() for m in re.finditer(re.escape(sub), data)]
        print(f"     first 88-byte block alone: {len(sh)} hit(s) {[hex(h) for h in sh[:5]]}")

    print("-- fuzzy (7 zero prefix, non-filler windows)")
    cands = []
    z7 = b"\0" * 7
    i = data.find(z7)
    while i != -1 and i + TLEN <= len(data):
        w = data[i:i + TLEN]
        body = w[7:]
        filler = body.count(0) + body.count(0xFF)
        if w[7] != 0 and filler < 0.97 * len(body):
            cands.append(i)
        i = data.find(z7, i + 1)
    print(f"  candidate windows: {len(cands)}")
    for n, t in tables.items():
        best = sorted((ham(data[c:c + TLEN], t), c) for c in cands)[:top]
        ff = ham(b"\xff" * 7 + b"\xff" * (TLEN - 7), t)
        print(f"  {n}: (ref: all-0xFF window = {ff} bits of {TLEN*8})")
        for d, c in best:
            print(f"    dist {d:5d} bits @ {c:#x} ({label(segs, c)})")

    print("-- 0x31F constants (+-64 B of a 0x20)")
    hits = []
    for o in range(0, len(data) - 4, 2):
        w = struct.unpack_from("<I", data, o)[0]
        if (w & 0x3) == 0x3 and (w & 0xFFF0707F) == 0x31F00013:
            hits.append((o, "addi ..,0x31f (rv32)"))
    for m in re.finditer(re.escape(b"\x1f\x03"), data):
        if m.start() % 2 == 0 and data[m.start() + 2:m.start() + 4] in (b"\0\0",):
            hits.append((m.start(), "LE32 data 0x0000031f"))
    for o, kind in hits:
        lo, hi = max(0, o - 64), o + 64
        near = False
        for p in range(lo - lo % 2, hi, 2):
            if p + 4 > len(data):
                break
            w = struct.unpack_from("<I", data, p)[0]
            if (w & 3) == 3 and (w & 0xFFF0707F) == 0x02000013:
                near = True
        print(f"  {o:#x} {kind} ({label(segs, o)}){'  [addi 0x20 nearby]' if near else ''}")
    if not hits:
        print("  none")

    print("-- strings (fast|quick|lut|partial|mono|wave|otp)")
    pat = re.compile(rb"[\x20-\x7e]{4,}")
    kw = re.compile(rb"fast|quick|\blut|lut_|partial|mono|wave|otp", re.I)
    n = 0
    for m in pat.finditer(data):
        if kw.search(m.group()):
            n += 1
            print(f"  {m.start():#x} [{label(segs, m.start())}]: {m.group()[:90].decode()}")
    if not n:
        print("  none")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tables", required=True)
    ap.add_argument("--top", type=int, default=3)
    ap.add_argument("files", nargs="+")
    a = ap.parse_args()
    raw = parse_tables(open(a.tables, encoding="utf-8").read())
    t = dedup(raw)
    print(f"tables: {len(raw)} arrays, {len(t)} distinct: {', '.join(t)}")
    for f in a.files:
        scan(f, t, a.top)


if __name__ == "__main__":
    main()
