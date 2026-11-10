import hashlib
import os
import struct
import sys
import time

import pytest

HERE = os.path.dirname(__file__)
sys.path.insert(0, os.path.join(HERE, ".."))
import hl_preflight as hp  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
OFFICIAL = os.path.join(ROOT, "..", "..", "examples", "3.7einkDvb", "firmware", "3.7einkDvb_factory_0x0.bin")
BUILD_DIR = os.environ.get("HUELEAF_TEST_BUILD", os.path.join(ROOT, "build"))
BUILT = os.path.join(BUILD_DIR, "hueleaf_gallery.bin")


def make_image(seg_lens, hash_appended=True):
    hdr = bytearray(24)
    hdr[0], hdr[1] = 0xE9, len(seg_lens)
    hdr[23] = 1 if hash_appended else 0
    body = bytes(hdr)
    for i, n in enumerate(seg_lens):
        body += struct.pack("<II", 0x40000000 + i, n) + bytes([i & 0xFF]) * n
    pad = (-(len(body) + 1)) % 16
    body += b"\0" * pad + b"\xAB"            # checksum byte ends 16-aligned
    if hash_appended:
        body += hashlib.sha256(body).digest()
    return body


@pytest.mark.parametrize("segs", [[1], [15], [16], [100, 3, 7], [0x1000] * 5, list(range(1, 17))])
@pytest.mark.parametrize("h", [True, False])
def test_synthetic_sizes(segs, h):
    img = make_image(segs, h)
    assert hp.app_image_size(img + b"\xff" * 4096) == len(img)
    assert hp.app_image_size(b"\0" * 0x10 + img, 0x10) == len(img)


def test_truncated_reports_lower_bound():
    img = make_image([0x2000, 0x2000])
    with pytest.raises(hp.Truncated) as e:
        hp.app_image_size(img[:0x1000])
    assert e.value.min_size > 0x1000


def test_oversize_detected_beyond_window():
    big = make_image([0x100000, 0x80000])          # > 0x170000
    window = big[:hp.FACTORY_LIMIT + 0x1000]
    with pytest.raises(hp.Truncated) as e:
        hp.app_image_size(window)
    assert e.value.min_size > hp.FACTORY_LIMIT
    assert hp.app_image_size(big) > hp.FACTORY_LIMIT


@pytest.mark.parametrize("bad", [b"\xff" * 64, b"\0" * 64, bytes([0xE9, 0]) + b"\0" * 62,
                                 bytes([0xE9, 40]) + b"\0" * 62])
def test_rejects_non_images(bad):
    with pytest.raises(hp.ImageError):
        hp.app_image_size(bad)


@pytest.mark.skipif(not os.path.exists(OFFICIAL), reason="official image not present")
def test_official_stock_app():
    d = open(OFFICIAL, "rb").read()
    size = hp.app_image_size(d, 0x10000)
    assert size == 0x151380 and size <= hp.FACTORY_LIMIT
    assert d[0x10000 + size:].strip(b"\xff") == b""
    t = hp.parse_ptable(d[0x8000:0x8C00])
    assert hp.factory_of(t) == (0x10000, 0x2F0000)


@pytest.mark.skipif(not os.path.exists(BUILT), reason="firmware not built")
def test_built_gallery_image():
    d = open(BUILT, "rb").read()
    assert hp.app_image_size(d) == len(d)


def ptable(entries, md5=True):
    out = b""
    for label, t, st, off, size in entries:
        out += b"\xaa\x50" + struct.pack("<BBII", t, st, off, size) + label.encode().ljust(16, b"\0") + b"\0" * 4
    if md5:
        out += b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(out).digest()
    return out + b"\xff" * (0xC00 - len(out))


OURS = [("nvs", 1, 2, 0x9000, 0x6000), ("phy_init", 1, 1, 0xF000, 0x1000), ("factory", 0, 0, 0x10000, 0x170000),
        ("otadata", 1, 0, 0x180000, 0x2000), ("ota_0", 0, 0x10, 0x190000, 0xB0000),
        ("ota_1", 0, 0x11, 0x240000, 0xB0000), ("image_cache", 1, 0x40, 0x300000, 0x100000)]
STOCK = [("nvs", 1, 2, 0x9000, 0x6000), ("phy_init", 1, 1, 0xF000, 0x1000), ("factory", 0, 0, 0x10000, 0x2F0000),
         ("image_cache", 1, 0x40, 0x300000, 0x100000)]


def test_ptable_parse_and_layout():
    a, b = hp.parse_ptable(ptable(OURS)), hp.parse_ptable(ptable(STOCK))
    assert [e[0] for e in a] == [e[0] for e in OURS]
    assert hp.same_layout(a, hp.parse_ptable(ptable(OURS, md5=False)))
    assert not hp.same_layout(a, b)
    bad = bytearray(ptable(OURS)); bad[40] ^= 1
    with pytest.raises(hp.ImageError):
        hp.parse_ptable(bytes(bad))


@pytest.mark.skipif(not os.path.exists(os.path.join(BUILD_DIR, "partition_table", "partition-table.bin")),
                    reason="not built")
def test_built_ptable_matches_ours():
    d = open(os.path.join(BUILD_DIR, "partition_table", "partition-table.bin"), "rb").read()
    assert hp.same_layout(hp.parse_ptable(d), hp.parse_ptable(ptable(OURS)))


# ---- end-to-end check() against a fake device (no serial port) ----
class FakeDevice:
    def __init__(self, flash):
        self.flash, self.writes = flash, []

    def esptool(self, port, *args, capture=False):
        if args[0] == "read_mac":
            return 0, "MAC: aa:bb:cc:dd:ee:ff\n"
        if args[0] == "read_flash":
            off, n, path = int(args[1], 16), int(args[2], 16), args[3]
            open(path, "wb").write(self.flash[off:off + n])
            return 0, ""
        self.writes.append(args)
        return 0, ""


def flash_with(table, app):
    f = bytearray(b"\xff" * hp.FLASH_SIZE)
    f[0x8000:0x8C00] = table
    f[0x10000:0x10000 + len(app)] = app
    return bytes(f)


def run_check(tmp_path, monkeypatch, flash, *flags, reads=None):
    dev = FakeDevice(flash)
    def esptool(port, *args, capture=False):
        if reads is not None and args[0] == "read_flash":
            reads.append((int(args[1], 16), int(args[2], 16)))
        return dev.esptool(port, *args, capture=capture)
    monkeypatch.setattr(hp, "esptool", esptool)
    (tmp_path / "pt.bin").write_bytes(ptable(OURS))
    rc = hp.main(["check", "--port", "X", "--ptable", str(tmp_path / "pt.bin"), "--backup-dir", str(tmp_path), *flags])
    return rc, dev


def test_check_stock_small_ok(tmp_path, monkeypatch):
    rc, dev = run_check(tmp_path, monkeypatch, flash_with(ptable(STOCK), make_image([0x100000])))
    assert rc == 0 and not dev.writes
    backups = list(tmp_path.glob("hueleaf_backup_aabbccddeeff_*.bin"))
    assert len(backups) == 1 and backups[0].stat().st_size == hp.FLASH_SIZE


def test_check_stock_too_big_aborts(tmp_path, monkeypatch, capsys):
    with pytest.raises(SystemExit) as e:
        run_check(tmp_path, monkeypatch, flash_with(ptable(STOCK), make_image([0x100000, 0x80000])))
    assert e.value.code == 3
    assert "超过新 factory 分区上限" in capsys.readouterr().err


def write_backup(tmp_path, name="hueleaf_backup_aabbccddeeff_20260101-000000_x.bin", sidecar=True, age_hours=1):
    path = tmp_path / name
    path.write_bytes(b"\0" * hp.FLASH_SIZE)
    if sidecar:
        (tmp_path / (name + ".sha256")).write_text(hashlib.sha256(path.read_bytes()).hexdigest() + "  " + name + "\n")
    old = time.time() - age_hours * 3600
    os.utime(path, (old, old))
    return path


def test_check_ours_new_backup_still_checks_stock_size(tmp_path, monkeypatch, capsys):
    rc, _ = run_check(tmp_path, monkeypatch, flash_with(ptable(OURS), make_image([0x100000])))
    out = capsys.readouterr().out
    assert rc == 0 and "备份完成" in out and "原厂固件" in out
    assert len(list(tmp_path.glob("hueleaf_backup_*.bin"))) == 1
    assert len(list(tmp_path.glob("hueleaf_backup_*.bin.sha256"))) == 1
    assert not list(tmp_path.glob(".hl_*")) and not list(tmp_path.glob("*.part"))


def test_check_ours_reuses_recent_verified_backup(tmp_path, monkeypatch, capsys):
    old = write_backup(tmp_path)
    reads = []
    rc, dev = run_check(tmp_path, monkeypatch, flash_with(ptable(OURS), make_image([0x100000])), reads=reads)
    assert rc == 0 and "复用" in capsys.readouterr().out
    assert list(tmp_path.glob("hueleaf_backup_*.bin")) == [old]
    assert reads == [(0x8000, hp.PTABLE_LEN)]          # only the partition table is read


@pytest.mark.parametrize("variant", ["no-sidecar", "stale", "corrupt", "short"])
def test_check_ours_does_not_reuse_unverified_or_old_backup(tmp_path, monkeypatch, variant):
    if variant == "no-sidecar": write_backup(tmp_path, sidecar=False)
    if variant == "stale": write_backup(tmp_path, age_hours=hp.BACKUP_REUSE_HOURS + 1)
    if variant == "corrupt":
        path = write_backup(tmp_path)
        path.write_bytes(b"\1" + path.read_bytes()[1:])
    if variant == "short":
        path = write_backup(tmp_path)
        path.write_bytes(b"\0" * 16)
    rc, _ = run_check(tmp_path, monkeypatch, flash_with(ptable(OURS), make_image([0x100000])))
    assert rc == 0 and len(list(tmp_path.glob("hueleaf_backup_*.bin"))) == 2


def test_check_ignores_other_chip_backup(tmp_path, monkeypatch):
    write_backup(tmp_path, name="hueleaf_backup_112233445566_20260101-000000_x.bin")
    run_check(tmp_path, monkeypatch, flash_with(ptable(OURS), make_image([0x100000])))
    assert len(list(tmp_path.glob("hueleaf_backup_aabbccddeeff_*.bin"))) == 1


def test_check_force_backup_ignores_recent_backup(tmp_path, monkeypatch):
    write_backup(tmp_path)
    run_check(tmp_path, monkeypatch, flash_with(ptable(OURS), make_image([0x100000])), "--force-backup")
    assert len(list(tmp_path.glob("hueleaf_backup_*.bin"))) == 2


def test_check_skip_backup_reads_no_backup_when_layout_is_ours(tmp_path, monkeypatch):
    reads = []
    rc, _ = run_check(tmp_path, monkeypatch, flash_with(ptable(OURS), make_image([0x100000])), "--skip-backup", reads=reads)
    assert rc == 0 and not list(tmp_path.glob("hueleaf_backup_*"))
    assert reads == [(0x8000, hp.PTABLE_LEN)]


def test_check_first_install_always_backs_up_even_with_recent_backup(tmp_path, monkeypatch):
    write_backup(tmp_path)
    run_check(tmp_path, monkeypatch, flash_with(ptable(STOCK), make_image([0x100000])))
    assert len(list(tmp_path.glob("hueleaf_backup_*.bin"))) == 2


def test_check_skip_backup_refused_for_first_install(tmp_path, monkeypatch, capsys):
    with pytest.raises(SystemExit) as e:
        run_check(tmp_path, monkeypatch, flash_with(ptable(STOCK), make_image([0x100000])), "--skip-backup")
    assert e.value.code == 3 and "必须先整片备份" in capsys.readouterr().err
    assert not list(tmp_path.glob("hueleaf_backup_*"))


def test_check_garbage_app_aborts(tmp_path, monkeypatch):
    with pytest.raises(SystemExit) as e:
        run_check(tmp_path, monkeypatch, flash_with(ptable(STOCK), b"\x00" * 64))
    assert e.value.code == 3


def test_check_read_failure_aborts(tmp_path, monkeypatch):
    def broken(port, *args, capture=False):
        return (0, "MAC: aa:bb:cc:dd:ee:ff\n") if args[0] == "read_mac" else (1, "")
    monkeypatch.setattr(hp, "esptool", broken)
    (tmp_path / "pt.bin").write_bytes(ptable(OURS))
    with pytest.raises(SystemExit):
        hp.main(["check", "--ptable", str(tmp_path / "pt.bin"), "--backup-dir", str(tmp_path)])
    assert not list(tmp_path.glob("hueleaf_backup_*.bin"))
