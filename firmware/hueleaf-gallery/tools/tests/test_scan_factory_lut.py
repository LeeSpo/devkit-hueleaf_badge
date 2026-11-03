import os, subprocess, sys
HERE = os.path.dirname(__file__)
SCAN = os.path.join(HERE, "..", "scan_factory_lut.py")
sys.path.insert(0, os.path.join(HERE, ".."))
import scan_factory_lut as s  # noqa: E402

TBL = bytes(7) + bytes((i * 37 + 11) & 0xFF for i in range(792))


def src(*tables):
    return "\n".join(f"static const uint8_t t{i}[799] = {{ {', '.join(hex(b) for b in t)} }};"
                     for i, t in enumerate(tables))


def test_parse_and_dedup():
    t = s.dedup(s.parse_tables(src(TBL, TBL, bytes(799))))
    assert list(t) == ["t0/t1", "t2"]


def test_exact_and_fuzzy(tmp_path):
    (tmp_path / "t.c").write_text(src(TBL))
    near = bytearray(TBL); near[100] ^= 0x01
    img = b"\xff" * 0x1000 + TBL + b"\x00" * 64 + bytes(near) + b"\xff" * 64
    (tmp_path / "img.bin").write_bytes(img)
    out = subprocess.run([sys.executable, SCAN, "--tables", str(tmp_path / "t.c"), str(tmp_path / "img.bin")],
                         capture_output=True, text=True, check=True).stdout
    assert "t0: 1 hit(s)\n    0x1000" in out
    assert "dist     0 bits @ 0x1000" in out and "dist     1 bits @ 0x135f" in out


def test_addi_799():
    w = (0x31F << 20) | (11 << 7) | 0x13          # addi a1, zero, 799
    data = b"\0" * 8 + w.to_bytes(4, "little") + b"\0" * 8
    assert any(True for o in range(0, len(data) - 4, 2)
               if (int.from_bytes(data[o:o + 4], "little") & 0xFFF0707F) == 0x31F00013)
