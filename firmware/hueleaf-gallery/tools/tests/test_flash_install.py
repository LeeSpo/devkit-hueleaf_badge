import hashlib
import importlib.util
import json
import struct
from pathlib import Path
import subprocess
import sys

import pytest

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
spec = importlib.util.spec_from_file_location("flash_install", TOOLS / "flash_install.py")
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


def test_external_backup_and_protected_write_offsets(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    monkeypatch.setattr(installer, "validate_package", lambda: None)
    calls = []
    monkeypatch.setattr(installer.hl_preflight, "check_security", lambda *args: calls.append("security"))
    monkeypatch.setattr(installer.hl_preflight, "main", lambda args: calls.append(args) or 0)

    def run(cmd, **kwargs):
        calls.append((cmd, kwargs))
        return subprocess.CompletedProcess(cmd, 0)

    monkeypatch.setattr(installer.subprocess, "run", run)
    assert installer.main(["/dev/test", "--backup-dir", "backups"]) == 0
    assert calls[0] == "security"
    assert calls[1] == ["check", "--port", "/dev/test", "--ptable",
                        str(TOOLS / "partition-table.bin"), "--backup-dir", str(tmp_path / "backups")]
    cmd, kwargs = calls[2]
    assert kwargs["cwd"] == str(TOOLS)
    assert cmd[cmd.index("write_flash") + 1::2] == ["0x2000", "0x8000", "0x180000", "0x190000"]
    assert Path.cwd() == tmp_path


@pytest.mark.parametrize("result", [1, 3])
def test_preflight_failure_prevents_write(result, monkeypatch):
    monkeypatch.setattr(installer, "validate_package", lambda: None)
    monkeypatch.setattr(installer.hl_preflight, "check_security", lambda *args: None)
    monkeypatch.setattr(installer.hl_preflight, "main", lambda args: result)

    def unexpected_write(*args, **kwargs):
        pytest.fail("flash must not run after a failed preflight")

    monkeypatch.setattr(installer.subprocess, "run", unexpected_write)
    assert installer.main(["/dev/test"]) == result


def make_app():
    header = bytearray(24)
    header[0], header[1], header[23] = 0xE9, 1, 1
    segment = b"synthetic ESP app"
    checksum = 0xEF
    for byte in segment:
        checksum ^= byte
    image = bytes(header) + struct.pack("<II", 0x40000000, len(segment)) + segment
    image += b"\0" * (-(len(image) + 1) % 16) + bytes([checksum])
    return image + hashlib.sha256(image).digest()


@pytest.fixture
def package(tmp_path, monkeypatch):
    install = tmp_path / "install"
    install.mkdir()
    contents = {
        "bootloader.bin": b"bootloader",
        "partition-table.bin": b"partition table",
        "otadata_ota0.bin": b"otadata",
        "gallery_ota0.bin": make_app(),
        "flash_install.py": b"mock installer script",
        "hl_preflight.py": b"mock preflight script",
    }
    for name, data in contents.items():
        (install / name).write_bytes(data)
    manifest = {
        "firmware_version": "test",
        "source_sha256": "test-source",
        "files": {"install/" + name: hashlib.sha256(data).hexdigest()
                  for name, data in contents.items()},
    }
    (tmp_path / "manifest.json").write_text(json.dumps(manifest))
    monkeypatch.setattr(installer, "here", str(install))
    return install


def rehash(package, name):
    path = package.parent / "manifest.json"
    manifest = json.loads(path.read_text())
    manifest["files"]["install/" + name] = hashlib.sha256((package / name).read_bytes()).hexdigest()
    path.write_text(json.dumps(manifest))


def test_validate_package_valid(package):
    assert installer.validate_package() is None


def test_validate_package_rejects_tampered_app(package):
    (package / "gallery_ota0.bin").write_bytes(make_app() + b"tampered")
    with pytest.raises(ValueError, match="gallery_ota0.bin"):
        installer.validate_package()


def test_validate_package_rejects_rehashed_oversized_bootloader(package):
    name = "bootloader.bin"
    (package / name).write_bytes(b"x" * (installer.PAYLOAD_LIMITS[name] + 1))
    rehash(package, name)
    with pytest.raises(ValueError, match=name):
        installer.validate_package()


def test_validate_package_rejects_rehashed_app_with_trailing_data(package):
    name = "gallery_ota0.bin"
    (package / name).write_bytes(make_app() + b"extra")
    rehash(package, name)
    with pytest.raises(ValueError):
        installer.validate_package()


def no_hardware(monkeypatch):
    def unexpected(*args, **kwargs):
        pytest.fail("invalid package or failed security must not touch hardware")

    monkeypatch.setattr(installer.hl_preflight, "check_security", unexpected)
    monkeypatch.setattr(installer.hl_preflight, "main", unexpected)
    monkeypatch.setattr(installer.subprocess, "run", unexpected)


@pytest.mark.parametrize("missing", ["manifest.json", "install/hl_preflight.py"])
def test_main_rejects_missing_package_file(package, monkeypatch, missing):
    (package.parent / missing).unlink()
    no_hardware(monkeypatch)
    assert installer.main(["/dev/test"]) == 2


def test_main_rejects_mixed_package_before_security(package, monkeypatch):
    (package / "partition-table.bin").write_bytes(b"from another package")
    no_hardware(monkeypatch)
    assert installer.main(["/dev/test"]) == 2


def test_security_failure_prevents_preflight_and_write(package, tmp_path, monkeypatch):
    no_hardware(monkeypatch)

    def fail_security(*args):
        raise SystemExit(1)

    monkeypatch.setattr(installer.hl_preflight, "check_security", fail_security)
    with pytest.raises(SystemExit) as exc:
        installer.main(["/dev/test", "--backup-dir", str(tmp_path / "backup")])
    assert exc.value.code == 1
