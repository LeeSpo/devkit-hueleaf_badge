import importlib.util
import json
from pathlib import Path
import subprocess

import pytest

spec = importlib.util.spec_from_file_location("workspace_hueleaf", Path(__file__).resolve().parents[1] / "workspace.py")
workspace = importlib.util.module_from_spec(spec)
spec.loader.exec_module(workspace)


def info_for(tmp_path):
    build = tmp_path / "build"
    build.mkdir()
    (build / "build_provenance.json").write_text(json.dumps({"layout_id": "remote-v1"}))
    return {"build": build, "artifacts": tmp_path / "artifacts" / "v5.5.4", "project": tmp_path / "project"}


def test_remote_package_stages_in_new_directory_and_preserves_previous(tmp_path, monkeypatch):
    info = info_for(tmp_path)
    dest = info["artifacts"]
    dest.mkdir(parents=True)
    (dest / "manifest.json").write_text("old")
    def package(args):
        staged = Path(args[-1])
        assert not staged.exists()
        assert (dest / "manifest.json").read_text() == "old"
        staged.mkdir()
        (staged / "manifest.json").write_text("new")
    monkeypatch.setattr(workspace, "run", package)
    workspace.package(info, "unused")
    assert (dest / "manifest.json").read_text() == "new"
    previous = list(dest.parent.glob("v5.5.4.previous-*"))
    assert len(previous) == 1 and (previous[0] / "manifest.json").read_text() == "old"
    assert not list(dest.parent.glob(".v5.5.4-package-*"))


def test_remote_packaging_failure_keeps_previous_package(tmp_path, monkeypatch):
    info = info_for(tmp_path)
    dest = info["artifacts"]
    dest.mkdir(parents=True)
    (dest / "manifest.json").write_text("old")
    def fail(args):
        staged = Path(args[-1])
        staged.mkdir()
        (staged / "partial").write_text("incomplete")
        raise subprocess.CalledProcessError(1, args)
    monkeypatch.setattr(workspace, "run", fail)
    with pytest.raises(subprocess.CalledProcessError):
        workspace.package(info, "unused")
    assert (dest / "manifest.json").read_text() == "old"
    assert not list(dest.parent.glob(".v5.5.4-package-*"))
    assert not list(dest.parent.glob("v5.5.4.previous-*"))


def test_failed_swap_restores_previous_package(tmp_path, monkeypatch):
    info = info_for(tmp_path)
    dest = info["artifacts"]
    dest.mkdir(parents=True)
    (dest / "manifest.json").write_text("old")
    def package(args):
        staged = Path(args[-1])
        staged.mkdir()
        (staged / "manifest.json").write_text("new")
    original_rename = Path.rename
    def rename(path, target):
        if path.name == "package":
            raise OSError("simulated rename failure")
        return original_rename(path, target)
    monkeypatch.setattr(workspace, "run", package)
    monkeypatch.setattr(Path, "rename", rename)
    with pytest.raises(OSError, match="simulated"):
        workspace.package(info, "unused")
    assert (dest / "manifest.json").read_text() == "old"
    assert not list(dest.parent.glob(".v5.5.4-package-*"))


def test_target_defaults_to_current_development_layout(monkeypatch):
    monkeypatch.delenv("HUELEAF_REMOTE", raising=False)
    monkeypatch.setattr(workspace, "remote_supported", lambda info: True)
    assert workspace.remote_build({"branch": "dev"})
    assert workspace.remote_build({"branch": "remote-control"})
    monkeypatch.setenv("HUELEAF_REMOTE", "0")
    assert not workspace.remote_build({"branch": "dev"})
    monkeypatch.setenv("HUELEAF_REMOTE", "1")
    assert workspace.remote_build({"branch": "dev"})
    monkeypatch.delenv("HUELEAF_REMOTE")
    monkeypatch.setattr(workspace, "remote_supported", lambda info: False)
    assert not workspace.remote_build({"branch": "dev"})


def test_stock_build_invalidates_old_remote_provenance_even_if_build_fails(tmp_path, monkeypatch):
    info = info_for(tmp_path)
    info["source"] = tmp_path / "source"
    info["kind"] = "gallery"
    info["branch"] = "dev"
    monkeypatch.delenv("HUELEAF_REMOTE", raising=False)
    monkeypatch.setenv("IDF_PATH", str(tmp_path / "idf"))
    monkeypatch.setenv("IDF_PYTHON_ENV_PATH", str(tmp_path / "python"))
    monkeypatch.setattr(workspace, "fingerprint", lambda source: "same")
    def fail(args):
        assert not (info["build"] / "build_provenance.json").exists()
        assert "HL_REMOTE_BUILD=OFF" in args
        raise subprocess.CalledProcessError(1, args)
    monkeypatch.setattr(workspace, "run", fail)
    with pytest.raises(subprocess.CalledProcessError):
        workspace.build(info)
    assert not (info["build"] / "build_provenance.json").exists()


def make_previous(dest, count):
    import os
    names = []
    for index in range(count):
        path = dest.parent / f"{dest.name}.previous-{index:032d}"
        path.mkdir()
        (path / "manifest.json").write_text(str(index))
        os.utime(path, (1_000_000 + index, 1_000_000 + index))
        names.append(path.name)
    return names


def test_only_the_newest_previous_packages_are_kept(tmp_path, monkeypatch):
    info = info_for(tmp_path)
    dest = info["artifacts"]
    dest.mkdir(parents=True)
    (dest / "manifest.json").write_text("old")
    names = make_previous(dest, 5)
    def package(args):
        staged = Path(args[-1])
        staged.mkdir()
        (staged / "manifest.json").write_text("new")
    monkeypatch.setattr(workspace, "run", package)
    workspace.package(info, "unused")
    kept = sorted(p.name for p in dest.parent.glob("v5.5.4.previous-*"))
    # the package that was just superseded plus the newest older one
    assert len(kept) == workspace.KEEP_PREVIOUS_PACKAGES == 2
    assert names[-1] in kept
    assert any((p / "manifest.json").read_text() == "old" for p in dest.parent.glob("v5.5.4.previous-*"))
    assert (dest / "manifest.json").read_text() == "new"


def git_repo(tmp_path):
    subprocess.run(["git", "-C", str(tmp_path), "init", "-q"], check=True)
    return tmp_path


def put(root, name, text):
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


@pytest.mark.parametrize("layout", ["gallery", "stock"])
def test_fingerprint_ignores_files_that_cannot_change_the_firmware(tmp_path, layout):
    root = git_repo(tmp_path)
    project = "firmware/hueleaf-gallery" if layout == "gallery" else "examples/3.7einkDvb"
    put(root, project + "/CMakeLists.txt", "project")
    put(root, project + "/main/main.c", "int x;")
    put(root, "examples/3.7einkDvb/firmware/3.7einkDvb_factory_0x0.bin", "stock")
    unrelated = ["android/app/Main.kt", "tools/hueleaf-push/gui.py", "README.md", "docs/images/a.png", "new/untracked.txt"]
    if layout == "gallery":
        unrelated += [project + "/docs/a.md", project + "/test_host/t.c", project + "/tools/tests/test_a.py", project + "/README.md"]
    for name in unrelated:
        put(root, name, "v1")
    base = workspace.fingerprint(root)
    for name in unrelated:
        put(root, name, "v2")
    assert workspace.fingerprint(root) == base
    put(root, project + "/main/main.c", "int y;")
    assert workspace.fingerprint(root) != base
    if layout == "gallery":
        changed = workspace.fingerprint(root)
        put(root, "examples/3.7einkDvb/firmware/3.7einkDvb_factory_0x0.bin", "other stock")
        assert workspace.fingerprint(root) != changed
        changed = workspace.fingerprint(root)
        put(root, project + "/tools/flash_install.py", "installer")      # shipped in the package
        assert workspace.fingerprint(root) != changed


def test_port_check_only_rejects_known_non_serial_devices(tmp_path, monkeypatch):
    monkeypatch.setattr(workspace.stat, "S_ISCHR", lambda mode: True)
    for name in ("cu.usbmodem1101", "cu.wchusbserial1410", "cu.SLAB_USBtoUART", "ttyACM0"):
        (tmp_path / name).touch()
        workspace.check_port(str(tmp_path / name))
    for name in ("cu.Bluetooth-Incoming-Port", "cu.debug-console"):
        (tmp_path / name).touch()
        with pytest.raises(RuntimeError, match="不是 USB 串口"):
            workspace.check_port(str(tmp_path / name))
    with pytest.raises(RuntimeError, match="实际设备串口"):
        workspace.check_port(str(tmp_path / "missing"))
    monkeypatch.setattr(workspace.stat, "S_ISCHR", lambda mode: False)
    with pytest.raises(RuntimeError, match="实际设备串口"):
        workspace.check_port(str(tmp_path / "ttyACM0"))


def test_device_args_accept_one_port_and_at_most_one_backup_flag():
    assert workspace.device_args(["/dev/cu.x"]) == ("/dev/cu.x", [])
    assert workspace.device_args(["/dev/cu.x", "--skip-backup"]) == ("/dev/cu.x", ["--skip-backup"])
    assert workspace.device_args(["--force-backup", "/dev/cu.x"]) == ("/dev/cu.x", ["--force-backup"])
    for bad in ([], ["a", "b"], ["a", "--bogus"], ["a", "--skip-backup", "--force-backup"]):
        with pytest.raises(RuntimeError, match="用法"):
            workspace.device_args(bad)


def install_info(tmp_path, layout_id):
    info = info_for(tmp_path)
    info["kind"] = "gallery"
    (info["artifacts"] / "install").mkdir(parents=True)
    return info, {"layout_id": layout_id} if layout_id else {}


def run_hardware(tmp_path, monkeypatch, layout_id, flags=(), command="install"):
    info, record = install_info(tmp_path, layout_id)
    calls = []
    monkeypatch.setattr(workspace, "checked_manifest", lambda info: record)
    monkeypatch.setattr(workspace, "check_port", lambda port: None)
    monkeypatch.setattr(workspace, "run", lambda args, **kw: calls.append([str(a) for a in args]))
    monkeypatch.setenv("IDF_PYTHON_ENV_PATH", str(tmp_path / "python"))
    monkeypatch.delenv("HUELEAF_MIGRATE_REPLACE_STOCK", raising=False)
    workspace.hardware(info, command, "/dev/cu.usbmodem1", flags)
    return calls


def test_remote_install_reads_efuse_once_inside_the_installer(tmp_path, monkeypatch):
    calls = run_hardware(tmp_path, monkeypatch, "remote-v1", ["--skip-backup"])
    assert len(calls) == 1 and "espefuse" not in " ".join(calls[0])
    assert calls[0][-1] == "--skip-backup" and "/dev/cu.usbmodem1" in calls[0]


def test_remote_preflight_forwards_flags_and_preflight_only(tmp_path, monkeypatch):
    calls = run_hardware(tmp_path, monkeypatch, "remote-v1", ["--force-backup"], command="preflight")
    assert "--force-backup" in calls[0] and "--preflight-only" in calls[0]


def test_stock_layout_install_keeps_its_efuse_summary(tmp_path, monkeypatch):
    calls = run_hardware(tmp_path, monkeypatch, None)
    assert "espefuse" in " ".join(calls[0]) and "flash_install.py" in " ".join(calls[1])

def test_stock_layout_install_rejects_remote_only_flags(tmp_path, monkeypatch):
    with pytest.raises(RuntimeError, match="仅 remote-v1"):
        run_hardware(tmp_path, monkeypatch, None, ["--skip-backup"])


def test_stale_package_is_a_warning_for_the_test_command(tmp_path, monkeypatch, capsys):
    info = info_for(tmp_path)
    info.update(source=tmp_path / "source", project=tmp_path / "project", kind="gallery")
    (info["artifacts"]).mkdir(parents=True)
    (info["artifacts"] / "manifest.json").write_text("{}")
    def stale(info):
        raise subprocess.CalledProcessError(1, ["build_candidate.py", "--check"])
    monkeypatch.setattr(workspace, "checked_manifest", stale)
    workspace.tests(info)
    assert "重新 build" in capsys.readouterr().out
    def mismatch(info):
        raise RuntimeError("源码已变化")
    monkeypatch.setattr(workspace, "checked_manifest", mismatch)
    workspace.tests(info)
    assert "源码已变化" in capsys.readouterr().out
