import importlib.util
from pathlib import Path
import sys
import pytest

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import remote_layout as R
import hl_preflight as H


def test_full_flash_layout_alignment_no_factory():
    assert R.matches([(*entry, 0) for entry in R.ENTRIES])
    assert R.ENTRIES[-1][3] + R.ENTRIES[-1][4] == 0x400000
    assert all(e[3] % (0x10000 if e[1] == 0 else 0x1000) == 0 for e in R.ENTRIES)
    assert not any(e[1:3] == (0, 0) for e in R.ENTRIES)
    assert R.migration_allowed([(*entry, 0) for entry in R.ENTRIES])


def test_reject_unknown_overlapping_or_encrypted_migration():
    table = [(*entry, 0) for entry in R.ENTRIES]
    assert not R.migration_allowed([])
    bad = list(table); bad[0] = (*bad[0][:3], 0xA000, 0x6000, 0)
    assert not R.migration_allowed(bad)
    bad = list(table); bad[0] = (*bad[0][:5], 1)
    assert not R.migration_allowed(bad)
    assert not R.migration_allowed(table + [("overlap", 1, 0x40, 0x20000, 0x1000, 0)])
    assert not R.matches(table[:-1])


@pytest.mark.parametrize("same,migrate,preflight,expected_erases", [
    (False, True, False, 1), (False, False, False, 0),
    (True, False, False, 0), (True, True, False, 1), (False, True, True, 0)])
def test_migration_backup_precedes_erase_and_nvs_preserved(tmp_path, monkeypatch, same, migrate, preflight, expected_erases):
    import argparse
    import hashlib
    import flash_install as F
    events = []
    table = [(*e, 0) for e in R.ENTRIES]
    if not same:
        table = table[:2] + [("factory", 0, 0, 0x10000, 0x3F0000, 0)]
    image = bytearray(b"\xff" * 0x400000)
    image[0x9000:0x10000] = b"N" * 0x7000
    monkeypatch.setattr(F.hl_preflight, "check_security", lambda *a: events.append("security"))
    def read(port, off, length, path):
        assert off == 0 and length == 0x400000
        Path(path).write_bytes(image)
        events.append("backup")
        return bytes(image)
    monkeypatch.setattr(F.hl_preflight, "read_region", read)
    monkeypatch.setattr(F.hl_preflight, "parse_ptable", lambda _: table)
    def erase(port, *args):
        assert events[:2] == ["security", "backup"]
        assert args == ("erase_region", "0x10000", "0x3f0000")
        events.append("erase")
        return 0, ""
    monkeypatch.setattr(F.hl_preflight, "esptool", erase)
    def write(cmd, **kwargs):
        import subprocess
        offsets = cmd[cmd.index("write_flash")+1::2]
        assert offsets == ["0x2000", "0x8000", "0x10000", "0x20000"]
        assert not any(int(off, 16) in range(0x9000, 0x10000) for off in offsets)
        events.append("write")
        return subprocess.CompletedProcess(cmd, 0)
    monkeypatch.setattr(F.subprocess, "run", write)
    args = argparse.Namespace(port="FAKE", migrate_replace_stock=migrate, preflight_only=preflight)
    if not same and not migrate:
        with pytest.raises(ValueError):
            F.install_remote(args, str(tmp_path))
        assert "write" not in events and "erase" not in events
    else:
        assert F.install_remote(args, str(tmp_path)) == 0
    assert events.count("erase") == expected_erases
    backup = next(tmp_path.glob("*.bin"))
    assert backup.read_bytes()[0x9000:0x10000] == b"N" * 0x7000
    assert backup.with_suffix(".bin.sha256").read_text().split()[0] == hashlib.sha256(image).hexdigest()
    if preflight:
        assert events == ["security", "backup"]
