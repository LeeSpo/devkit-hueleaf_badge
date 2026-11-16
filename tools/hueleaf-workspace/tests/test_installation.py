"""Exercise the complete write plan and public entry without opening a serial port."""
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import zipfile

import pytest

from firmware_image import Firmware, MAGIC, STOCK_ENTRIES, build_image, ota0_data, parse_table
from installer import install, plan_install
from package_release import HERE, TOOLS, installer_zip, package_release
from support import remote_layout


def app_image(remote=True):
    data = bytearray(192)
    data[0:2] = bytes((0xE9, 1))
    struct.pack_into("<II", data, 24, 0x40000000, 144)
    name = b"hueleaf_remote_v1" if remote else b"hueleaf_gallery"
    data[80:80 + len(name)] = name
    return bytes(data)


def partition_table(remote=True):
    entries = remote_layout.ENTRIES if remote else STOCK_ENTRIES
    data = b"".join(struct.pack("<2sBBII16sI", b"\xaa\x50", typ, sub, offset, size, name.encode(), 0)
                    for name, typ, sub, offset, size in entries)
    return data.ljust(0x1000, b"\xff")


def snapshot(remote=True):
    data = bytearray(b"\xff" * 0x400000)
    data[0x8000:0x9000] = partition_table(remote)
    if not remote:
        data[0x10000:0x10000 + 192] = app_image(False)
    data[0x9000:0xF000] = b"N" * 0x6000
    data[0x12000:0x20000] = b"B" * 0xE000 if remote else data[0x12000:0x20000]
    return bytes(data)


def build_dir(tmp_path, remote=True):
    build = tmp_path / "build"
    (build / "bootloader").mkdir(parents=True)
    (build / "partition_table").mkdir()
    (build / "bootloader/bootloader.bin").write_bytes(app_image(remote))
    (build / "partition_table/partition-table.bin").write_bytes(partition_table(remote))
    (build / "hueleaf_gallery.bin").write_bytes(app_image(remote))
    return build


def firmware_file(tmp_path, remote=True):
    build = build_dir(tmp_path, remote)
    path = tmp_path / "firmware.bin"
    path.write_bytes(build_image(build, {"firmware_version": "2.0.0", "layout_id": "remote-v1" if remote else "gallery-stock-v1"}))
    return path


@pytest.mark.parametrize("remote,addresses", [(True, [0x2000, 0x8000, 0x10000, 0x20000]),
                                             (False, [0x2000, 0x8000, 0x180000, 0x190000])])
def test_complete_bin_extracts_only_install_regions(tmp_path, remote, addresses):
    firmware = Firmware.read(firmware_file(tmp_path, remote))
    payloads, migrate = plan_install(firmware, snapshot(remote))
    assert [offset for offset, _ in payloads] == addresses
    assert not migrate
    # No NVS/PHY, factory App, remote credentials, or footer bytes are written.
    assert all(not (offset < 0x10000 and offset + len(data) > 0x9000) for offset, data in payloads)
    assert all(not data.endswith(MAGIC) for _, data in payloads)
    assert payloads[-1][1] == app_image(remote)


def test_remote_migration_and_normal_update_have_different_erase_plans(tmp_path):
    firmware = Firmware.read(firmware_file(tmp_path))
    assert plan_install(firmware, snapshot(False))[1] is True
    assert plan_install(firmware, snapshot(True))[1] is False


def test_ordinary_update_refuses_reverse_layout_change(tmp_path):
    firmware = Firmware.read(firmware_file(tmp_path, False))
    with pytest.raises(ValueError, match="不能直接切换"):
        plan_install(firmware, snapshot(True))


@pytest.mark.parametrize("remote,offset", [(True, 0x20000), (False, 0x190000)])
def test_arbitrary_plain_bin_uses_device_application_address(tmp_path, remote, offset):
    path = tmp_path / "user-selected.bin"
    path.write_bytes(b"user takes responsibility for this file")
    firmware = Firmware.read(path)
    payloads, migrate = plan_install(firmware, snapshot(remote))
    assert payloads[-1] == (offset, path.read_bytes())
    assert payloads[0][1] == ota0_data()
    assert not migrate


def test_firmware_payload_changes_do_not_require_hashes(tmp_path):
    path = firmware_file(tmp_path)
    data = bytearray(path.read_bytes())
    data[0x20000 + 150] ^= 0xFF
    path.write_bytes(data)
    firmware = Firmware.read(path)
    assert firmware.payloads()[-1][1][150] == data[0x20000 + 150]


def test_partition_digest_is_not_an_install_requirement():
    # A malformed optional MD5 entry is not compared by the public installer.
    table = partition_table()
    length = 32 * len(remote_layout.ENTRIES)
    table = table[:length] + b"\xeb\xeb" + bytes(30) + table[length + 32:]
    assert len(parse_table(table)) == len(remote_layout.ENTRIES)


def test_invalid_footer_offsets_are_not_written(tmp_path):
    path = firmware_file(tmp_path)
    data = path.read_bytes()
    position = len(data) - len(MAGIC) - 4
    length = struct.unpack_from("<I", data, position)[0]
    meta = json.loads(data[position - length:position])
    meta["segments"][0]["offset"] = 0x9000
    encoded = json.dumps(meta).encode()
    path.write_bytes(data[:position - length] + encoded + struct.pack("<I", len(encoded)) + MAGIC)
    with pytest.raises(ValueError, match="写入信息"):
        Firmware.read(path)


@pytest.mark.parametrize("mode,answer,writes", [("preflight", None, 0), ("install", "n", 0), ("install", "y", 1)])
def test_install_backup_confirm_and_preflight(tmp_path, monkeypatch, mode, answer, writes):
    path = firmware_file(tmp_path)
    events = []
    class Device:
        def __init__(self, port):
            assert port == "COM5"
        def backup(self, directory):
            events.append("backup")
            return directory / "backup.bin", snapshot()
        def write(self, payloads, migrate):
            events.append("write")
            assert migrate is False
            assert [offset for offset, _ in payloads] == [0x2000, 0x8000, 0x10000, 0x20000]
    def prompt(message):
        assert events == ["backup"]
        assert answer is not None, "preflight must not request permission to write"
        return answer
    monkeypatch.setattr("builtins.input", prompt)
    assert install(path, tmp_path / "backups", "COM5", preflight_only=mode == "preflight", device_factory=Device) == 0
    assert events.count("write") == writes


def test_failed_backup_never_writes(tmp_path):
    path = firmware_file(tmp_path)
    class Device:
        def __init__(self, port):
            pass
        def backup(self, directory):
            raise RuntimeError("backup failed")
        def write(self, *args):
            pytest.fail("must not write")
    with pytest.raises(RuntimeError, match="backup failed"):
        install(path, tmp_path / "backups", "COM5", yes=True, device_factory=Device)


def test_device_writes_segments_and_only_migration_erases(tmp_path, monkeypatch):
    from installer import Device, preflight
    calls = []
    def esptool(port, *args):
        calls.append(args)
        if args[0] == "write_flash":
            assert args[5] == "0x20000"
            assert Path(args[6]).read_bytes() == b"app"
        return 0, ""
    monkeypatch.setattr(preflight, "esptool", esptool)
    Device("COM5").write([(0x20000, b"app")], False)
    assert [args[0] for args in calls] == ["write_flash"]
    calls.clear()
    Device("COM5").write([(0x20000, b"app")], True)
    assert calls[0] == ("erase_region", "0x10000", "0x3f0000")


def test_standalone_zip_runs_from_arbitrary_directory_without_source(tmp_path):
    archive = tmp_path / "installer.zip"
    installer_zip(archive)
    with zipfile.ZipFile(archive) as zipped:
        names = zipped.namelist()
        assert "hueleaf-installer/gui/web/app.js" in names
        assert "hueleaf-installer/_hl_preflight.py" in names
        assert not any("__pycache__" in name or "/tests/" in name for name in names)
        zipped.extractall(tmp_path / "unpacked with spaces")
    tool = tmp_path / "unpacked with spaces/hueleaf-installer"
    env = {k: v for k, v in os.environ.items() if k not in ("HUELEAF_SOURCE", "PYTHONPATH")}
    env["HUELEAF_DATA_DIR"] = str(tmp_path / "external data")
    run = subprocess.run([sys.executable, str(tool / "hueleaf.py"), "status"], cwd=tmp_path,
                         env=env, text=True, capture_output=True)
    assert run.returncode == 0, run.stdout + run.stderr
    assert "独立安装工具" in run.stdout
    # Import the packaged firmware reader too, so its canonical support files
    # must be present and usable without checkout-relative imports.
    run = subprocess.run([sys.executable, "-c", "import installer; print('ok')"], cwd=tool,
                         env=env, text=True, capture_output=True)
    assert run.returncode == 0, run.stdout + run.stderr
    firmware = firmware_file(tmp_path)
    backup = tmp_path / "backup.bin"
    backup.write_bytes(snapshot())
    code = f"""
import hueleaf
from support import preflight
from pathlib import Path
preflight.check_security = lambda port: 'fixture'
preflight.make_backup = lambda *args: ({str(backup)!r}, Path({str(backup)!r}).read_bytes())
calls = []
def write(port, *args):
    assert port == 'COM5'
    assert args[0] == 'write_flash'
    assert list(args[5::2]) == ['0x2000', '0x8000', '0x10000', '0x20000']
    assert all(Path(name).is_file() for name in args[6::2])
    calls.append(args)
    return 0, ''
preflight.esptool = write
assert hueleaf.main(['install', {str(firmware)!r}, '--port', 'COM5', '--yes']) == 0
assert len(calls) == 1
"""
    run = subprocess.run([sys.executable, "-c", code], cwd=tool, env=env, text=True, capture_output=True)
    assert run.returncode == 0, run.stdout + run.stderr


def test_menu_eof_exits_instead_of_looping(monkeypatch):
    import hueleaf
    def eof(message):
        raise EOFError
    monkeypatch.setattr("builtins.input", eof)
    assert hueleaf.main(["menu"]) == 0


def test_release_packager_outputs_two_assets_and_refuses_stale_build(tmp_path):
    build = build_dir(tmp_path)
    spec = importlib.util.spec_from_file_location("test_release_builder", TOOLS / "build_candidate.py")
    checker = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(checker)
    (build / "otadata_ota0.bin").write_bytes(ota0_data())
    (build / "flasher_args.json").write_text(json.dumps({"flash_files": remote_layout.FLASH_FILES}))
    config = build / "sdkconfig"
    config.write_text("test config")
    record = {"source": str(checker.SOURCE), "source_sha256": checker.fingerprint(),
              "config_file": str(config), "sdkconfig_sha256": checker.digest(config),
              "outputs": {name: checker.digest(build / name) for name in checker.OUTPUTS},
              "layout_id": "remote-v1", "firmware_version": "2.0.0"}
    (build / "build_provenance.json").write_text(json.dumps(record))
    output = tmp_path / "release"
    package_release(build, output)
    assert sorted(p.name for p in output.iterdir()) == ["hueleaf-firmware-2.0.0.bin", "hueleaf-installer.zip"]
    assert Firmware.read(output / "hueleaf-firmware-2.0.0.bin").complete
    with pytest.raises(ValueError, match="已存在"):
        package_release(build, output)
    (build / "hueleaf_gallery.bin").write_bytes(b"changed")
    with pytest.raises(RuntimeError, match="output"):
        package_release(build, tmp_path / "stale")
