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


MAC = "14c19f9aef74"


class RemoteDevice:
    """Fake device for install_remote(): records security/backup/erase/write order."""
    def __init__(self, monkeypatch, tmp_path, same, mac=MAC):
        import flash_install as F
        self.F, self.events, self.reads = F, [], []
        table = [(*e, 0) for e in R.ENTRIES]
        if not same:
            table = table[:2] + [("factory", 0, 0, 0x10000, 0x3F0000, 0)]
        self.image = bytearray(b"\xff" * 0x400000)
        self.image[0x9000:0x10000] = b"N" * 0x7000
        monkeypatch.setattr(F.hl_preflight, "check_security", lambda *a: self.events.append("security") or mac)
        monkeypatch.setattr(F.hl_preflight, "parse_ptable", lambda _: table)
        monkeypatch.setattr(F.hl_preflight, "read_region", self.read)
        monkeypatch.setattr(F.hl_preflight, "esptool", self.erase)
        monkeypatch.setattr(F.subprocess, "run", self.write)

    def read(self, port, off, length, path):
        self.reads.append((off, length))
        Path(path).write_bytes(self.image[off:off + length])
        if length == 0x400000:
            self.events.append("backup")
        return bytes(self.image[off:off + length])

    def erase(self, port, *args):
        assert "backup" in self.events          # nothing is erased before a backup exists
        assert args == ("erase_region", "0x10000", "0x3f0000")
        self.events.append("erase")
        return 0, ""

    def write(self, cmd, **kwargs):
        import subprocess
        offsets = cmd[cmd.index("write_flash")+1::2]
        assert offsets == ["0x2000", "0x8000", "0x10000", "0x20000"]
        assert not any(int(off, 16) in range(0x9000, 0x10000) for off in offsets)
        self.events.append("write")
        return subprocess.CompletedProcess(cmd, 0)

    def install(self, tmp_path, migrate=False, preflight=False, skip=False, force=False):
        import argparse
        args = argparse.Namespace(port="FAKE", migrate_replace_stock=migrate, preflight_only=preflight,
                                  skip_backup=skip, force_backup=force)
        return self.F.install_remote(args, str(tmp_path))


def recent_backup(tmp_path, mac=MAC, age_hours=1):
    import hashlib
    import os
    import time
    path = tmp_path / f"hueleaf_remote_{mac}_20260101-000000_x.bin"
    path.write_bytes(b"\0" * 0x400000)
    (tmp_path / (path.name + ".sha256")).write_text(hashlib.sha256(path.read_bytes()).hexdigest() + "  " + path.name + "\n")
    old = time.time() - age_hours * 3600
    os.utime(path, (old, old))
    return path


@pytest.mark.parametrize("same,migrate,preflight,expected_erases", [
    (False, True, False, 1), (False, False, False, 0),
    (True, False, False, 0), (True, True, False, 1), (False, True, True, 0)])
def test_migration_backup_precedes_erase_and_nvs_preserved(tmp_path, monkeypatch, same, migrate, preflight, expected_erases):
    import hashlib
    dev = RemoteDevice(monkeypatch, tmp_path, same)
    if not same and not migrate:
        with pytest.raises(ValueError):
            dev.install(tmp_path, migrate, preflight)
        assert "write" not in dev.events and "erase" not in dev.events
    else:
        assert dev.install(tmp_path, migrate, preflight) == 0
    assert dev.events.count("erase") == expected_erases
    backup = next(tmp_path.glob("*.bin"))
    assert backup.name.startswith(f"hueleaf_remote_{MAC}_")
    assert backup.read_bytes()[0x9000:0x10000] == b"N" * 0x7000
    assert backup.with_suffix(".bin.sha256").read_text().split()[0] == hashlib.sha256(dev.image).hexdigest()
    assert not list(tmp_path.glob("*.part"))
    if preflight:
        assert dev.events == ["security", "backup"]


def test_routine_update_reuses_recent_backup_and_reads_only_the_ptable(tmp_path, monkeypatch, capsys):
    old = recent_backup(tmp_path)
    dev = RemoteDevice(monkeypatch, tmp_path, same=True)
    assert dev.install(tmp_path) == 0
    assert dev.events == ["security", "write"] and dev.reads == [(0x8000, 0x1000)]
    assert list(tmp_path.glob("*.bin")) == [old] and "复用" in capsys.readouterr().out


@pytest.mark.parametrize("case", ["stale", "other-chip", "no-sidecar", "force"])
def test_routine_update_takes_new_backup_when_reuse_is_not_safe(tmp_path, monkeypatch, case):
    if case == "stale": recent_backup(tmp_path, age_hours=25)
    if case == "other-chip": recent_backup(tmp_path, mac="112233445566")
    if case == "no-sidecar":
        recent_backup(tmp_path).with_name(f"hueleaf_remote_{MAC}_20260101-000000_x.bin.sha256").unlink()
    if case == "force": recent_backup(tmp_path)
    dev = RemoteDevice(monkeypatch, tmp_path, same=True)
    assert dev.install(tmp_path, force=case == "force") == 0
    assert dev.events == ["security", "backup", "write"]
    assert len(list(tmp_path.glob(f"hueleaf_remote_{MAC}_*.bin"))) == (2 if case != "other-chip" else 1)


def test_skip_backup_only_reads_the_ptable_on_routine_update(tmp_path, monkeypatch):
    dev = RemoteDevice(monkeypatch, tmp_path, same=True)
    assert dev.install(tmp_path, skip=True) == 0
    assert dev.events == ["security", "write"] and dev.reads == [(0x8000, 0x1000)]
    assert not list(tmp_path.glob("*.bin"))


@pytest.mark.parametrize("same,migrate", [(False, True), (False, False), (True, True)])
def test_skip_backup_refused_when_app_area_would_be_erased(tmp_path, monkeypatch, same, migrate):
    dev = RemoteDevice(monkeypatch, tmp_path, same)
    with pytest.raises(ValueError, match="必须先整片备份"):
        dev.install(tmp_path, migrate=migrate, skip=True)
    assert dev.events == ["security"]


@pytest.mark.parametrize("same,migrate", [(False, True), (True, True)])
def test_migration_never_reuses_an_old_backup(tmp_path, monkeypatch, same, migrate):
    recent_backup(tmp_path)
    dev = RemoteDevice(monkeypatch, tmp_path, same)
    assert dev.install(tmp_path, migrate=migrate) == 0
    assert dev.events[:2] == ["security", "backup"]
