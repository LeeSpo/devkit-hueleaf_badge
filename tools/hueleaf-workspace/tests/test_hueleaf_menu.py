"""Interactive workflows with real package validation and no device I/O."""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace

import pytest

spec = importlib.util.spec_from_file_location("menu_workspace", Path(__file__).resolve().parents[1] / "workspace.py")
workspace = importlib.util.module_from_spec(spec)
spec.loader.exec_module(workspace)


@pytest.fixture
def source(tmp_path, monkeypatch):
    root = tmp_path / "source"
    project = root / "firmware/hueleaf-gallery"
    for name in ("CMakeLists.txt", "tools/build_candidate.py", "tools/remote_layout.py", "sdkconfig.remote.defaults"):
        path = project / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("fixture")
    (project / "main").mkdir()
    (project / "main/version.h").write_text('#define HL_FW_VERSION "1.5.0"\n')
    monkeypatch.setattr(workspace, "LOCAL", tmp_path / "local")
    monkeypatch.setattr(workspace, "DEFAULT_SOURCE", root)
    monkeypatch.setattr(workspace, "git", lambda source, *args: "dev" if args[0] == "branch" else "a" * 40)
    monkeypatch.setattr(workspace, "source_root", lambda: root)
    monkeypatch.setattr(workspace, "fingerprint", lambda source: "source-sha")
    monkeypatch.delenv("HUELEAF_REMOTE", raising=False)
    monkeypatch.delenv("HUELEAF_MIGRATE_REPLACE_STOCK", raising=False)
    return root


def answers(monkeypatch, values):
    values = iter(values)
    monkeypatch.setattr("builtins.input", lambda prompt="": next(values))


def save_package(info):
    dest = info["artifacts"]
    payload = dest / "install/gallery_ota0.bin"
    payload.parent.mkdir(parents=True, exist_ok=True)
    payload.write_bytes(b"validated-test-payload")
    record = {"layout_id": info["target"], "firmware_version": "1.5.0", "source": str(info["source"]),
              "branch": info["branch"], "commit": info["commit"], "project": info["kind"],
              "idf_version": workspace.IDF_VERSION, "source_sha256": "source-sha",
              "files": {"install/gallery_ota0.bin": hashlib.sha256(payload.read_bytes()).hexdigest()}}
    (dest / "manifest.json").write_text(json.dumps(record))
    info["build"].mkdir(parents=True, exist_ok=True)
    (info["build"] / "build_provenance.json").write_text(json.dumps(record))
    return record


@pytest.fixture
def devices(monkeypatch):
    # Enumeration and path checks are mocked; no test can open a real serial port.
    monkeypatch.setattr(workspace, "serial_ports", lambda: [("/dev/cu.fixture", "Fixture USB", True)])
    monkeypatch.setattr(workspace, "check_port", lambda port: None)
    calls = []
    monkeypatch.setattr(workspace, "run", lambda args, **kwargs: calls.append((list(map(str, args)), kwargs)))
    return calls


def launcher_calls(calls):
    return [(args, kwargs) for args, kwargs in calls if args[0].endswith("hueleaf-local.sh")]


def test_targets_have_independent_paths_and_explicit_selection_beats_environment(source, monkeypatch):
    ordinary = workspace.layout(source, "gallery-stock-v1")
    remote = workspace.layout(source, "remote-v1")
    for key in ("build", "artifacts"):
        assert ordinary[key] != remote[key]
        assert ordinary[key].parent == remote[key].parent
        assert ordinary[key].name == "gallery-stock-v1"
        assert remote[key].name == "remote-v1"
    monkeypatch.setenv("HUELEAF_REMOTE", "1")
    assert workspace.layout(source)["target"] == "remote-v1"
    assert not workspace.remote_build(ordinary)
    assert workspace.remote_build(remote)
    other = workspace.layout(source.parent / "other", "remote-v1")
    assert other["build"] != remote["build"]
    monkeypatch.setattr(workspace, "IDF_VERSION", "vOther")
    assert workspace.layout(source, "remote-v1")["build"] != remote["build"]


def test_legacy_packages_stay_in_place_and_are_never_used_for_the_other_target(source):
    info = workspace.layout(source, "remote-v1")
    legacy = dict(info, artifacts=info["legacy_artifacts"], build=info["legacy_build"])
    save_package(legacy)
    assert workspace.existing_package(info)["artifacts"] == legacy["artifacts"]
    assert "旧目录" in workspace.package_summary(info)
    ordinary = workspace.layout(source, "gallery-stock-v1")
    assert workspace.existing_package(ordinary) == ordinary
    assert not ordinary["artifacts"].exists()
    save_package(info)
    assert workspace.existing_package(info)["artifacts"] == info["artifacts"]
    assert (legacy["artifacts"] / "manifest.json").is_file()


def test_mismatched_package_is_rejected_before_a_port_is_touched(source, monkeypatch):
    info = workspace.layout(source, "gallery-stock-v1")
    record = save_package(info)
    record["layout_id"] = "remote-v1"
    (info["artifacts"] / "manifest.json").write_text(json.dumps(record))
    monkeypatch.setattr(workspace, "check_port", lambda port: pytest.fail("must validate package first"))
    with pytest.raises(RuntimeError, match="固件类型"):
        workspace.hardware(info, "install", "/dev/never-open")


def test_legacy_install_uses_legacy_scripts_and_still_validates_payloads(source, devices, monkeypatch):
    info = workspace.layout(source, "gallery-stock-v1")
    legacy = dict(info, artifacts=info["legacy_artifacts"], build=info["legacy_build"])
    save_package(legacy)
    monkeypatch.setenv("IDF_PYTHON_ENV_PATH", "/fixture/idf-env")
    workspace.hardware(info, "install", "/dev/cu.fixture")
    assert devices[-1][0][1] == str(legacy["artifacts"] / "install/flash_install.py")
    (legacy["artifacts"] / "install/gallery_ota0.bin").write_bytes(b"tampered")
    devices.clear()
    with pytest.raises(RuntimeError, match="修改"):
        workspace.hardware(info, "install", "/dev/cu.fixture")
    assert not devices


def test_child_uses_selected_source_and_target_without_inheriting_migration(source, devices, monkeypatch):
    info = workspace.layout(source, "remote-v1")
    monkeypatch.setenv("HUELEAF_MIGRATE_REPLACE_STOCK", "1")
    monkeypatch.setenv("HUELEAF_REMOTE", "0")
    workspace.menu_command(info, "install", ["/dev/cu.fixture"])
    args, kwargs = launcher_calls(devices)[0]
    assert args[1:] == ["install", "/dev/cu.fixture"]
    assert kwargs["env"]["HUELEAF_SOURCE"] == str(source)
    assert kwargs["env"]["HUELEAF_REMOTE"] == "1"
    assert kwargs["env"]["HUELEAF_IDF_VERSION"] == workspace.IDF_VERSION
    assert "HUELEAF_MIGRATE_REPLACE_STOCK" not in kwargs["env"]
    assert kwargs["cwd"] == source
    assert workspace.os.environ["HUELEAF_MIGRATE_REPLACE_STOCK"] == "1"


def test_branch_change_aborts_before_launching_a_child(source, devices, monkeypatch):
    info = workspace.layout(source)
    monkeypatch.setattr(workspace, "git", lambda source, *args: "other" if args[0] == "branch" else "a" * 40)
    with pytest.raises(RuntimeError, match="已切换"):
        workspace.menu_command(info, "install", ["/dev/cu.fixture"])
    assert not devices


@pytest.mark.parametrize("target, inputs, expected_flag, advanced", [
    ("gallery-stock-v1", ["", "y"], None, False),
    ("remote-v1", ["", "y"], None, False),
    ("remote-v1", ["", "1", "2", "y"], "--force-backup", True),
    ("remote-v1", ["", "1", "3", "y"], "--skip-backup", True),
])
def test_install_passes_the_chosen_backup_policy(source, devices, monkeypatch, target, inputs, expected_flag, advanced):
    info = workspace.layout(source, target)
    save_package(info)
    answers(monkeypatch, inputs)
    workspace.device_workflow(info, "install", advanced=advanced)
    calls = launcher_calls(devices)
    assert len(calls) == 1
    args, kwargs = calls[0]
    assert args[1:3] == ["install", "/dev/cu.fixture"]
    assert args[3:] == ([expected_flag] if expected_flag else [])
    assert "HUELEAF_MIGRATE_REPLACE_STOCK" not in kwargs["env"]


@pytest.mark.parametrize("confirmation", ["", "n"])
def test_default_or_explicit_cancel_never_launches_install(source, devices, monkeypatch, confirmation):
    info = workspace.layout(source, "gallery-stock-v1")
    save_package(info)
    answers(monkeypatch, ["", confirmation])
    with pytest.raises(workspace.MenuCancelled):
        workspace.device_workflow(info, "install")
    assert not launcher_calls(devices)


@pytest.mark.parametrize("typed, allowed", [("", False), ("migrate", False), ("MIGRATE", True)])
def test_migration_needs_exact_confirmation_and_cannot_skip_backup(source, devices, monkeypatch, typed, allowed):
    info = workspace.layout(source, "remote-v1")
    save_package(info)
    answers(monkeypatch, ["", "2", "y", typed])
    if allowed:
        workspace.device_workflow(info, "install", advanced=True)
        args, kwargs = launcher_calls(devices)[0]
        assert args[1:] == ["install", "/dev/cu.fixture"]
        assert kwargs["env"]["HUELEAF_MIGRATE_REPLACE_STOCK"] == "1"
        assert "--skip-backup" not in args
    else:
        with pytest.raises(workspace.MenuCancelled):
            workspace.device_workflow(info, "install", advanced=True)
        assert not launcher_calls(devices)


def test_preflight_uses_read_only_command_even_in_migration_mode(source, devices, monkeypatch, capsys):
    info = workspace.layout(source, "remote-v1")
    save_package(info)
    answers(monkeypatch, ["", "2", "y"])
    workspace.device_workflow(info, "preflight", advanced=True)
    args, kwargs = launcher_calls(devices)[0]
    assert args[1:] == ["preflight", "/dev/cu.fixture"]
    assert kwargs["env"]["HUELEAF_MIGRATE_REPLACE_STOCK"] == "1"
    assert "不擦写设备" in capsys.readouterr().out


def test_missing_package_can_be_built_then_validated(source, devices, monkeypatch):
    info = workspace.layout(source, "gallery-stock-v1")
    def build(args, **kwargs):
        assert str(args[1]) == "build"
        devices.append((list(map(str, args)), kwargs))
        save_package(info)
    monkeypatch.setattr(workspace, "run", build)
    answers(monkeypatch, ["y"])
    assert workspace.prepare_package(info)["layout_id"] == info["target"]
    assert len(launcher_calls(devices)) == 1


def test_stale_package_cancellation_never_enumerates_ports(source, devices, monkeypatch):
    info = workspace.layout(source, "gallery-stock-v1")
    save_package(info)
    monkeypatch.setattr(workspace, "fingerprint", lambda source: "changed-source")
    monkeypatch.setattr(workspace, "serial_ports", lambda: pytest.fail("must validate package first"))
    answers(monkeypatch, [""])
    with pytest.raises(workspace.MenuCancelled):
        workspace.device_workflow(info, "install")
    assert not devices


def test_build_failure_returns_to_menu_without_entering_install(source, monkeypatch, capsys):
    calls = []
    def fail(args, **kwargs):
        calls.append(list(map(str, args)))
        raise subprocess.CalledProcessError(1, args)
    monkeypatch.setattr(workspace, "run", fail)
    monkeypatch.setattr(workspace, "select_port", lambda: pytest.fail("build failed, no device action allowed"))
    answers(monkeypatch, ["1", "0"])
    assert workspace.interactive(workspace.layout(source)) == 0
    assert len(calls) == 1 and calls[0][1] == "build"
    assert "操作失败，返回主菜单" in capsys.readouterr().out


def test_build_and_install_full_workflow(source, devices, monkeypatch, capsys):
    info = workspace.layout(source)
    def child(args, **kwargs):
        devices.append((list(map(str, args)), kwargs))
        if args[1] == "build":
            save_package(info)
    monkeypatch.setattr(workspace, "run", child)
    answers(monkeypatch, ["1", "", "y", "0"])
    assert workspace.interactive(info) == 0
    calls = launcher_calls(devices)
    assert [args[1] for args, _ in calls] == ["build", "install"]
    assert all(kwargs["env"]["HUELEAF_REMOTE"] == "1" for _, kwargs in calls)
    output = capsys.readouterr().out
    assert "当前开发版本：HueLeaf 1.5.0" in output
    for unwanted in ("选择固件版本", "普通相册版", "原厂固件", "选择设备操作方式", "\n备份策略\n"):
        assert unwanted not in output


def test_refresh_and_manual_port_selection(source, monkeypatch):
    snapshots = iter([[], [("/dev/cu.fixture", "Fixture", True)], []])
    monkeypatch.setattr(workspace, "serial_ports", lambda: next(snapshots))
    checked = []
    monkeypatch.setattr(workspace, "check_port", lambda port: checked.append(port))
    answers(monkeypatch, ["r", ""])
    assert workspace.select_port() == "/dev/cu.fixture"
    answers(monkeypatch, ["m", "/dev/cu.manual"])
    assert workspace.select_port() == "/dev/cu.manual"
    assert checked == ["/dev/cu.fixture", "/dev/cu.manual"]


def test_multiple_ports_require_explicit_selection(source, devices, monkeypatch, capsys):
    monkeypatch.setattr(workspace, "serial_ports", lambda: [("/dev/cu.a", "A", True), ("/dev/cu.b", "B", True)])
    answers(monkeypatch, ["", "2"])
    assert workspace.select_port() == "/dev/cu.b"
    assert "请输入列表中的选项" in capsys.readouterr().out


def test_port_enumeration_filters_pseudo_ports_and_sorts_candidates(monkeypatch):
    from serial.tools import list_ports
    def port(device, vid=None, desc="USB"):
        return SimpleNamespace(device=device, vid=vid, description=desc)
    monkeypatch.setattr(sys, "platform", "darwin")
    monkeypatch.setattr(list_ports, "comports", lambda: [
        port("/dev/cu.other"), port("/dev/cu.Bluetooth-Incoming-Port"), port("/dev/cu.debug-console"),
        port("/dev/tty.usbmodem"), port("/dev/cu.usbmodem", 0x303A),
    ])
    assert workspace.serial_ports() == [("/dev/cu.usbmodem", "USB", True), ("/dev/cu.other", "USB", False)]


def test_historical_source_uses_its_existing_configuration_without_a_selector(source, devices, monkeypatch):
    project = source / "firmware/hueleaf-gallery"
    (project / "sdkconfig.remote.defaults").unlink()
    answers(monkeypatch, ["2", "0"])
    assert workspace.interactive(workspace.layout(source)) == 0
    assert launcher_calls(devices)[0][1]["env"]["HUELEAF_REMOTE"] == "0"


def test_advanced_device_options_are_only_asked_for_explicitly(source, devices, monkeypatch):
    info = workspace.layout(source)
    save_package(info)
    answers(monkeypatch, ["8", "1", "", "1", "2", "y", "0"])
    assert workspace.interactive(info) == 0
    args, kwargs = launcher_calls(devices)[0]
    assert args[1:] == ["install", "/dev/cu.fixture", "--force-backup"]
    assert "HUELEAF_MIGRATE_REPLACE_STOCK" not in kwargs["env"]


def test_noninteractive_default_is_status_and_explicit_menu_fails(source, monkeypatch, capsys):
    monkeypatch.setattr(sys, "stdin", SimpleNamespace(isatty=lambda: False))
    monkeypatch.setattr("builtins.input", lambda prompt="": pytest.fail("must not prompt without a terminal"))
    assert workspace.main([]) == 0
    assert "build target: remote-v1" in capsys.readouterr().out
    with pytest.raises(RuntimeError, match="需要终端"):
        workspace.main(["menu"])


def test_terminal_default_enters_menu_and_explicit_status_does_not(source, monkeypatch, capsys):
    monkeypatch.setattr(sys, "stdin", SimpleNamespace(isatty=lambda: True))
    monkeypatch.setattr(sys.stdout, "isatty", lambda: True)
    answers(monkeypatch, ["0"])
    assert workspace.main([]) == 0
    assert "请选择操作" in capsys.readouterr().out
    monkeypatch.setattr("builtins.input", lambda prompt="": pytest.fail("explicit command must not prompt"))
    assert workspace.main(["status"]) == 0


@pytest.mark.parametrize("error", [EOFError, KeyboardInterrupt])
def test_closed_input_or_control_c_exits_at_main_menu(source, monkeypatch, error):
    def stop(prompt=""):
        raise error()
    monkeypatch.setattr("builtins.input", stop)
    assert workspace.interactive(workspace.layout(source)) == 0


def test_control_c_inside_an_action_returns_to_menu(source, monkeypatch, capsys):
    def stop(*args, **kwargs):
        raise KeyboardInterrupt()
    monkeypatch.setattr(workspace, "menu_command", stop)
    answers(monkeypatch, ["1", "0"])
    assert workspace.interactive(workspace.layout(source)) == 0
    assert "操作已取消，返回主菜单" in capsys.readouterr().out
