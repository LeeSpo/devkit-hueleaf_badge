#!/usr/bin/env python3
"""Branch-independent entry points for this HueLeaf workspace."""
import argparse
import datetime
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import uuid

TOOL_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOL_DIR))
from paths import data_root, source_root as configured_source, venv_python

LOCAL = data_root()
WORKSPACE = LOCAL.parent
DEFAULT_SOURCE = configured_source() or TOOL_DIR.parents[1]
HOST_PYTHON = venv_python(LOCAL / "venvs/hueleaf-host")
IDF_VERSION = os.environ.get("HUELEAF_IDF_VERSION", "v5.5.4")


def run(args, **kwargs):
    print("$", " ".join(map(str, args)), flush=True)
    return subprocess.run(list(map(str, args)), check=True, **kwargs)


def git(source, *args):
    return subprocess.check_output(["git", "-C", str(source), *args], stderr=subprocess.DEVNULL).decode().strip()


def source_root():
    configured = os.environ.get("HUELEAF_SOURCE")
    if configured:
        return Path(git(Path(configured), "rev-parse", "--show-toplevel")).resolve()
    try:
        here = Path(git(Path.cwd(), "rev-parse", "--show-toplevel")).resolve()
        if (here / "examples/3.7einkDvb/CMakeLists.txt").exists():
            return here
    except subprocess.CalledProcessError:
        pass
    return DEFAULT_SOURCE.resolve()


def branch_key(branch):
    key = re.sub(r"[^A-Za-z0-9._-]", "_", branch)
    if key in ("", ".", "..") or key != branch:
        key = (key.strip(".") or "branch") + "-" + hashlib.sha256(branch.encode()).hexdigest()[:8]
    return key


GALLERY_LAYOUTS = ("gallery-stock-v1", "remote-v1")


def layout(source, target=None):
    commit = git(source, "rev-parse", "HEAD")
    branch = git(source, "branch", "--show-current") or "detached-" + commit[:12]
    if (source / "firmware/hueleaf-gallery/CMakeLists.txt").exists():
        project = source / "firmware/hueleaf-gallery"
        kind = "gallery"
    else:
        project = source / "examples/3.7einkDvb"
        kind = "stock"
    prefix = Path()
    if source != DEFAULT_SOURCE.resolve():
        prefix = Path("worktrees") / (source.name + "-" + hashlib.sha256(str(source).encode()).hexdigest()[:8])
    suffix = prefix / branch_key(branch) / kind / IDF_VERSION
    info = {"source": source, "project": project, "kind": kind, "branch": branch, "commit": commit}
    if kind == "gallery":
        target = target or ("remote-v1" if remote_build(info) else "gallery-stock-v1")
        if target not in GALLERY_LAYOUTS:
            raise RuntimeError("未知固件类型：" + target)
        if ble_only(info) and target != "remote-v1":
            raise RuntimeError("v2 仅支持蓝牙遥控版；传统 v1 请使用 archive/gallery-stock-1.5.0")
        info.update(target=target, legacy_build=LOCAL / "builds" / suffix,
                    legacy_artifacts=LOCAL / "artifacts" / suffix)
        suffix /= target
    info.update(build=LOCAL / "builds" / suffix, artifacts=LOCAL / "artifacts" / suffix)
    return info


def read_manifest(path):
    record = json.loads(path.read_text())
    if not isinstance(record, dict):
        raise RuntimeError("安装包清单格式异常，请重新编译：" + str(path))
    return record


def existing_package(info):
    """Read old packages in place; moving CMake caches breaks absolute paths."""
    legacy = info.get("legacy_artifacts")
    if (info["artifacts"] / "manifest.json").exists() or legacy is None:
        return info
    path = legacy / "manifest.json"
    if path.exists():
        try:
            record = read_manifest(path)
        except (OSError, ValueError, RuntimeError):
            return info
        if record.get("layout_id", "gallery-stock-v1") == info["target"]:
            return dict(info, build=info["legacy_build"], artifacts=legacy)
    return info


def remote_supported(info):
    return info["kind"] == "gallery" and all((info["project"] / name).is_file() for name in (
        "tools/build_candidate.py", "tools/remote_layout.py", "sdkconfig.remote.defaults"))


def firmware_version(info):
    path = info["project"] / "main/version.h"
    version = re.search(r'#define\s+HL_FW_VERSION\s+"([^"]+)"', path.read_text()) if path.exists() else None
    return version.group(1) if version else "未记录"


# Source binding covers what can change the firmware: the project directory
# (minus docs/tests) and the stock image merged by non-remote packaging.
# Android app, PC tools and unrelated docs must not invalidate a build.
UNBOUND = ("docs/", "test_host/", "tools/tests/", "README.md")
STOCK_IMAGE = "examples/3.7einkDvb/firmware/3.7einkDvb_factory_0x0.bin"


def fingerprint(source):
    """Hash tracked modifications and non-ignored new files that affect the build."""
    gallery = (source / "firmware/hueleaf-gallery/CMakeLists.txt").exists()
    project = "firmware/hueleaf-gallery" if gallery else "examples/3.7einkDvb"
    names = subprocess.check_output(["git", "-C", str(source), "ls-files", "-z", "--cached", "--others",
                                     "--exclude-standard", "--", project, *([STOCK_IMAGE] if gallery else [])])
    digest = hashlib.sha256()
    for name in sorted(set(names.split(b"\0")) - {b""}):
        text = os.fsdecode(name)
        if gallery and text != STOCK_IMAGE and text[len(project) + 1:].startswith(UNBOUND):
            continue
        path = source / text
        digest.update(name + b"\0")
        if path.is_symlink():
            digest.update(b"link\0" + os.fsencode(os.readlink(path)))
        elif path.is_file():
            digest.update(b"file\0" + path.read_bytes())
        else:
            digest.update(b"missing\0")
    return digest.hexdigest()


def load_preflight():
    spec = importlib.util.spec_from_file_location("hl_workspace_preflight", DEFAULT_SOURCE / "firmware/hueleaf-gallery/tools/hl_preflight.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


KEEP_PREVIOUS_PACKAGES = 2


def prune_previous(dest, keep=KEEP_PREVIOUS_PACKAGES, protect=None):
    """Keep only the newest `keep` superseded packages next to `dest`."""
    old = sorted((p for p in dest.parent.glob(dest.name + ".previous-*") if p != protect),
                 key=lambda p: p.stat().st_mtime, reverse=True)
    for path in old[max(keep - (protect is not None), 0):]:
        print("清理旧产物：", path)
        shutil.rmtree(path, ignore_errors=True)


def package(info, source_fingerprint):
    build, dest = info["build"], info["artifacts"]
    remote = (build / "build_provenance.json").exists() and json.loads((build / "build_provenance.json").read_text()).get("layout_id") == "remote-v1"
    if remote:
        # package.sh requires a new output directory and verifies provenance.
        # Build it separately before moving any previous package out of the way.
        dest.parent.mkdir(parents=True, exist_ok=True)
        staging_root = Path(tempfile.mkdtemp(prefix=f".{dest.name}-package-", dir=dest.parent))
        staged = staging_root / "package"
        previous = None
        try:
            run([info["project"] / "tools/package.sh", build, staged])
            if dest.exists() or dest.is_symlink():
                previous = dest.with_name(f"{dest.name}.previous-{uuid.uuid4().hex}")
                dest.rename(previous)
            try:
                staged.rename(dest)
            except OSError:
                if previous is not None:
                    previous.rename(dest)
                raise
            if previous is not None:
                print("保留先前产物：", previous)
            prune_previous(dest, protect=previous)
            print("安装包：", dest)
        finally:
            shutil.rmtree(staging_root)
        return
    dest.mkdir(parents=True, exist_ok=True)
    manifest = dest / "manifest.json"
    manifest.unlink(missing_ok=True)
    args = json.loads((build / "flasher_args.json").read_text())
    install = dest / "install"
    install.mkdir(parents=True, exist_ok=True)
    if info["kind"] == "gallery":
        expected = {"0x2000": "bootloader/bootloader.bin", "0x8000": "partition_table/partition-table.bin",
                    "0x180000": "otadata_ota0.bin", "0x190000": "hueleaf_gallery.bin"}
        if args["flash_files"] != expected:
            raise RuntimeError("gallery 刷写地址与保留原厂 App 的布局不符，拒绝打包")
        hp = load_preflight()
        app = (build / "hueleaf_gallery.bin").read_bytes()
        if hp.app_image_size(app) != len(app) or len(app) > 0xB0000:
            raise RuntimeError("相册 App 镜像大小不合法")
        for source_name, dest_name in (("bootloader/bootloader.bin", "bootloader.bin"),
                                       ("partition_table/partition-table.bin", "partition-table.bin"),
                                       ("otadata_ota0.bin", "otadata_ota0.bin"),
                                       ("hueleaf_gallery.bin", "gallery_ota0.bin")):
            shutil.copy2(build / source_name, install / dest_name)
        for name in ("flash_install.py", "hl_preflight.py"):
            shutil.copy2(info["project"] / "tools" / name, install / name)
        stock = info["source"] / "examples/3.7einkDvb/firmware/3.7einkDvb_factory_0x0.bin"
        stock_size = hp.app_image_size(stock.read_bytes(), 0x10000)
        if stock_size > hp.FACTORY_LIMIT:
            raise RuntimeError("原厂 App 超过新 factory 分区上限")
        stock_app = dest / "stock_app.bin"
        stock_app.write_bytes(stock.read_bytes()[0x10000:0x10000 + stock_size])
        run([sys.executable if os.environ.get("IDF_PYTHON_ENV_PATH") is None else Path(os.environ["IDF_PYTHON_ENV_PATH"]) / "bin/python",
             "-m", "esptool", "--chip", "esp32c5", "merge_bin", "-o", dest / "hueleaf_full_0x0.bin",
             "--flash_mode", "dio", "--flash_size", "4MB", "0x2000", install / "bootloader.bin",
             "0x8000", install / "partition-table.bin", "0x10000", stock_app,
             "0x180000", install / "otadata_ota0.bin", "0x190000", install / "gallery_ota0.bin"])
        merged = (dest / "hueleaf_full_0x0.bin").read_bytes()
        if merged[0x10000:0x10000 + stock_size] != stock_app.read_bytes() or merged[0x190000:] != app:
            raise RuntimeError("合并镜像内容验证失败")
    else:
        for name in args["flash_files"].values():
            target = install / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(build / name, target)
    shutil.copy2(build / "flasher_args.json", install / "flasher_args.json")
    record = {"source": str(info["source"]), "branch": info["branch"], "commit": info["commit"],
              "layout_id": info.get("target", "stock"), "firmware_version": firmware_version(info),
              "project": info["kind"], "idf_version": IDF_VERSION, "source_sha256": source_fingerprint,
              "built_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "files": {str(p.relative_to(dest)): hashlib.sha256(p.read_bytes()).hexdigest()
                        for p in sorted(dest.rglob("*")) if p.is_file() and p != manifest}}
    manifest.write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n")
    print("安装包：", dest)


def ble_only(info):
    """v2+ source cannot be built as the frozen traditional v1 layout."""
    if "project" not in info:
        return False
    version = firmware_version(info).split(".", 1)[0]
    return version.isdigit() and int(version) >= 2


def remote_build(info):
    """Keep historical checkouts usable; current v2+ is always BLE remote."""
    if info.get("kind") == "stock":
        return False
    if ble_only(info):
        if os.environ.get("HUELEAF_REMOTE") not in (None, "1") or info.get("target", "remote-v1") != "remote-v1":
            raise RuntimeError("v2 仅支持蓝牙遥控版；请移除 HUELEAF_REMOTE=0，传统 v1 请使用 archive/gallery-stock-1.5.0")
        return True
    if "target" in info:
        return info["target"] == "remote-v1"
    configured = os.environ.get("HUELEAF_REMOTE")
    return configured == "1" if configured is not None else remote_supported(info)


def build(info):
    if remote_build(info):
        if not remote_supported(info):
            raise RuntimeError("当前源码缺少开发版构建文件，请检查源码分支和配置")
        out = info["build"]
        run([Path(os.environ["IDF_PYTHON_ENV_PATH"]) / "bin/python",
             info["project"] / "tools/build_candidate.py", "--remote", "--build-dir", out])
        package(info, fingerprint(info["source"]))
        return
    before = fingerprint(info["source"])
    out = info["build"]
    out.mkdir(parents=True, exist_ok=True)
    # Do not let CMake modify the sdkconfig tracked by the current branch.
    source_config = info["project"] / "sdkconfig"
    seed = out / "source-sdkconfig.sha256"
    config = out / "sdkconfig"
    if source_config.exists():
        config_hash = hashlib.sha256(source_config.read_bytes()).hexdigest()
        if not config.exists() or not seed.exists() or seed.read_text() != config_hash:
            shutil.copy2(source_config, config)
            seed.write_text(config_hash)
    # Also invalidate provenance in directories created by older launchers.
    (out / "build_provenance.json").unlink(missing_ok=True)
    idf = Path(os.environ["IDF_PATH"]) / "tools/idf.py"
    idf_python = Path(os.environ["IDF_PYTHON_ENV_PATH"]) / "bin/python"
    command = [idf_python, idf, "-C", info["project"], "-B", out, "-D", "SDKCONFIG=" + str(config)]
    if info["kind"] == "gallery":
        command += ["-D", "HL_REMOTE_BUILD=OFF"]
    run([*command, "build"])
    if fingerprint(info["source"]) != before:
        raise RuntimeError("编译期间源码发生变化，未生成可安装清单；请重新运行 build")
    package(info, before)


def checked_manifest(info):
    info = existing_package(info)
    path = info["artifacts"] / "manifest.json"
    if not path.exists():
        raise RuntimeError("当前分支没有构建产物，请先运行 build")
    manifest = read_manifest(path)
    if info.get("target") and manifest.get("layout_id", "gallery-stock-v1") != info["target"]:
        raise RuntimeError("安装包固件类型与当前构建配置不匹配，请重新 build")
    if manifest.get("layout_id") == "remote-v1":
        script = info["project"] / "tools/build_candidate.py"
        run([HOST_PYTHON, script, "--check", info["build"]])
        record = json.loads((info["build"] / "build_provenance.json").read_text())
        if manifest["source_sha256"] != record["source_sha256"]:
            raise RuntimeError("remote package/source mismatch")
        for name, sha in manifest["files"].items():
            p = info["artifacts"] / name
            if not p.is_file() or hashlib.sha256(p.read_bytes()).hexdigest() != sha:
                raise RuntimeError("remote package missing/modified: " + name)
        return manifest
    for key, value in (("source", str(info["source"])), ("branch", info["branch"]),
                       ("commit", info["commit"]), ("idf_version", IDF_VERSION), ("project", info["kind"])):
        if manifest[key] != value:
            raise RuntimeError("构建来源不匹配，请重新 build：" + key)
    if manifest["source_sha256"] != fingerprint(info["source"]):
        raise RuntimeError("源码已变化，当前安装包过期；请先重新 build")
    for name, sha in manifest["files"].items():
        p = info["artifacts"] / name
        if not p.is_file() or hashlib.sha256(p.read_bytes()).hexdigest() != sha:
            raise RuntimeError("安装包缺失或已被修改：" + name)
    return manifest


NOT_SERIAL_DEVICES = ("bluetooth", "debug-console", "wlan-debug")
BACKUP_FLAGS = ("--skip-backup", "--force-backup")


def check_port(port):
    if not port or not Path(port).exists() or not stat.S_ISCHR(Path(port).stat().st_mode):
        raise RuntimeError("请指定实际设备串口，例如 /dev/cu.usbmodemXXXX")
    if any(word in Path(port).name.lower() for word in NOT_SERIAL_DEVICES):
        raise RuntimeError("这不是 USB 串口设备，拒绝自动连接：" + port)


def device_args(remaining):
    """Split `<port> [--skip-backup|--force-backup]` into (port, flags)."""
    flags = [item for item in remaining if item.startswith("--")]
    ports = [item for item in remaining if not item.startswith("--")]
    if len(ports) != 1 or any(flag not in BACKUP_FLAGS for flag in flags) or len(set(flags)) > 1:
        raise RuntimeError("用法：install|preflight <实际 USB 串口> [--skip-backup | --force-backup]")
    return ports[0], flags


def hardware(info, command, port, flags=()):
    if info["kind"] != "gallery":
        raise RuntimeError("此安装入口仅用于相册固件；原厂固件请按其文档操作")
    info = existing_package(info)
    record = checked_manifest(info)
    check_port(port)
    idf_python = Path(os.environ["IDF_PYTHON_ENV_PATH"]) / "bin/python"
    backup = LOCAL / "backups"
    backup.mkdir(parents=True, exist_ok=True)
    install = info["artifacts"] / "install"
    remote = record.get("layout_id") == "remote-v1"
    if flags and not remote:
        raise RuntimeError("--skip-backup/--force-backup 仅 remote-v1 安装包支持")
    if not remote:
        # The stock-layout installer has no eFuse gate of its own; remote-v1's does.
        run([idf_python, "-m", "espefuse", "--chip", "esp32c5", "--port", port, "summary"])
    if remote:
        arguments = [idf_python, install / "flash_install.py", port, "--backup-dir", backup, *flags]
        if command == "preflight": arguments.append("--preflight-only")
        if os.environ.get("HUELEAF_MIGRATE_REPLACE_STOCK") == "1": arguments.append("--migrate-replace-stock")
        run(arguments)
        return
    if command == "preflight":
        run([idf_python, info["project"] / "tools/hl_preflight.py", "check", "--port", port,
             "--ptable", install / "partition-table.bin", "--backup-dir", backup])
    else:
        run([idf_python, install / "flash_install.py", port, "--backup-dir", backup])


def host_command(info, command, args):
    host = info["source"] / "tools/hueleaf-push"
    script = host / ("hueleaf_gui.py" if command == "gui" else "hueleaf_push.py")
    if not script.exists():
        if command == "ports":
            from serial.tools import list_ports
            for p in list_ports.comports():
                print(p.device, p.description)
            return
        raise RuntimeError("当前分支没有电脑端工具，请切换到 dev 或 feat/name-picker")
    if command == "gui":
        from bootstrap import ensure_host
        ensure_host(host)
    run([HOST_PYTHON, script, *(["--ports"] if command == "ports" else []), *args])


def tests(info):
    source = info["source"]
    tests = [p for p in (source / "tools/hueleaf-push/tests", source / "tools/hueleaf-workspace/tests",
                         info["project"] / "tools/tests") if p.is_dir()]
    if tests:
        run([HOST_PYTHON, "-m", "pytest", "-q", "-o", "cache_dir=" + str(LOCAL / "cache/pytest"), *tests], cwd=source)
    c_runner = info["project"] / "test_host/run.sh"
    if c_runner.exists():
        run([c_runner], cwd=source)
    info = existing_package(info)
    if (info["artifacts"] / "manifest.json").exists():
        try:
            manifest = checked_manifest(info)
        except (RuntimeError, subprocess.CalledProcessError) as exc:
            # Stale packages are normal while developing; install/preflight stay strict.
            print("⚠ 已有安装包与当前源码不一致，跳过产物核对（install 前请重新 build）：", exc)
            manifest = None
        if manifest is not None:
            if info["kind"] == "gallery":
                hp = load_preflight()
                app = (info["build"] / "hueleaf_gallery.bin").read_bytes()
                assert hp.app_image_size(app) == len(app)
                table = hp.parse_ptable((info["build"] / "partition_table/partition-table.bin").read_bytes())
                if manifest.get("layout_id") == "remote-v1":
                    spec = importlib.util.spec_from_file_location("hl_remote_layout", info["project"] / "tools/remote_layout.py")
                    remote_layout = importlib.util.module_from_spec(spec)
                    spec.loader.exec_module(remote_layout)
                    assert remote_layout.matches(table), "remote-v1 分区布局不匹配"
                else:
                    assert hp.factory_of(table) == (0x10000, 0x170000)
                    assert next(p for p in table if p[0] == "ota_0")[3:5] == (0x190000, 0xB0000)
            print("外部构建镜像、分区布局、来源和文件 SHA256：通过")
    if not tests and not c_runner.exists():
        print("当前分支未提供主机端测试。")


class MenuCancelled(Exception):
    pass


MENU_ERRORS = (RuntimeError, OSError, ValueError, KeyError, subprocess.CalledProcessError)


def choose(title, options, default=None, back="返回"):
    print("\n" + title)
    for key, label in options:
        print(f"  {key}. {label}")
    print(f"  0. {back}")
    keys = {key for key, _ in options}
    while True:
        answer = input("请选择" + (f" [{default}]" if default else "") + "：").strip().lower()
        answer = answer or default
        if answer == "0":
            raise MenuCancelled()
        if answer in keys:
            return answer
        print("请输入列表中的选项。")


def confirm(message):
    while True:
        answer = input(message + " [y/N]：").strip().lower()
        if answer in ("y", "yes", "是"):
            return True
        if answer in ("", "n", "no", "否", "0"):
            return False
        print("请输入 y 或 n；回车取消。")


def serial_ports():
    """Enumerate only: never open a port while displaying the menu."""
    from serial.tools import list_ports
    found = []
    for port in list_ports.comports():
        if any(word in port.device.lower() for word in NOT_SERIAL_DEVICES):
            continue
        if sys.platform == "darwin" and port.device.startswith("/dev/tty."):
            continue
        likely = port.vid == 0x303A or "JTAG" in (port.description or "")
        found.append((port.device, port.description or "未知设备", likely))
    return sorted(found, key=lambda item: (not item[2], item[0]))


def select_port():
    while True:
        ports = serial_ports()
        options = [(str(i), f"{device} — {description}" + (" [Espressif 候选]" if likely else ""))
                   for i, (device, description, likely) in enumerate(ports, 1)]
        if not ports:
            print("\n未发现 USB 串口；连接设备后选择刷新。")
        answer = choose("选择设备串口（此步骤不会连接设备）", options + [("r", "刷新"), ("m", "手动输入")],
                        default="1" if len(ports) == 1 else None)
        if answer == "r":
            continue
        if answer == "m":
            port = input("串口路径（0 返回）：").strip()
            if port in ("", "0"):
                continue
        else:
            port = ports[int(answer) - 1][0]
        try:
            check_port(port)
        except RuntimeError as exc:
            print("错误：", exc)
            continue
        return port


def menu_command(info, command, args=(), migrate=False):
    """Use the original launcher so SDK setup stays lazy and in a child shell."""
    current = layout(info["source"], info.get("target"))
    if any(current[key] != info[key] for key in ("branch", "commit", "kind")):
        raise RuntimeError("源码分支或提交已切换，请返回菜单重新选择")
    env = os.environ.copy()
    env.update(HUELEAF_SOURCE=str(info["source"]), HUELEAF_IDF_VERSION=IDF_VERSION,
               HUELEAF_REMOTE="1" if remote_build(info) else "0")
    # Migration authority is per operation, never inherited from the user's shell.
    env.pop("HUELEAF_MIGRATE_REPLACE_STOCK", None)
    if migrate:
        env["HUELEAF_MIGRATE_REPLACE_STOCK"] = "1"
    run([TOOL_DIR / "hueleaf-local.sh", command, *args], env=env, cwd=info["source"])


def prepare_package(info):
    try:
        return checked_manifest(info)
    except MENU_ERRORS as exc:
        print("\n安装包暂不可用：", exc)
        if not confirm("是否先编译并打包当前开发版本？"):
            raise MenuCancelled()
        menu_command(info, "build")
        return checked_manifest(info)


def device_workflow(info, command, advanced=False):
    record = prepare_package(info)
    remote = record.get("layout_id") == "remote-v1"
    port = select_port()
    migrate = False
    flags = []
    backup = "自动（按安装器规则备份或复用已校验备份）"
    if remote and advanced:
        mode = choose("选择设备操作方式", [
            ("1", "日常更新：设备已经是 remote-v1，保留绑定信息"),
            ("2", "首次迁移／迁移中断重试：替换原厂 App，擦除应用区及遥控绑定")], default="1")
        migrate = mode == "2"
        if migrate:
            backup = "强制新建完整 4 MiB 备份（迁移不可跳过）"
        else:
            policy = choose("备份策略", [("1", "自动（推荐）"), ("2", "强制新建完整备份"),
                                         ("3", "跳过整片备份（仅已是相同布局时允许）")], default="1")
            if policy == "2":
                flags = ["--force-backup"]
                backup = "强制新建完整 4 MiB 备份"
            elif policy == "3":
                flags = ["--skip-backup"]
                backup = "跳过整片备份（安装器仍会检查布局）"
    print("\n== " + ("安装确认" if command == "install" else "预检确认（只备份／检查，不擦写设备）") + " ==")
    print("固件：HueLeaf", record.get("firmware_version", "旧包未记录版本"))
    print("构建提交：", record.get("commit", record.get("base_commit", "未记录")))
    print("安装包：", existing_package(info)["artifacts"])
    print("设备：", port)
    print("备份策略：", backup)
    print("备份目录：", LOCAL / "backups")
    if remote:
        print("操作：", "迁移／重试（正式安装将清除应用区及遥控绑定）" if migrate else "同布局更新（不启用迁移擦除）")
    if not confirm("继续安装？" if command == "install" else "继续预检与备份？"):
        raise MenuCancelled()
    if migrate and command == "install":
        if input("确认替换原厂 App／清除遥控绑定，请输入 MIGRATE：").strip() != "MIGRATE":
            raise MenuCancelled()
    menu_command(info, command, [port, *flags], migrate=migrate)
    print("\n" + ("安装完成。" if command == "install" else "预检完成，未擦写设备。"))


def package_summary(info):
    package = existing_package(info)
    path = package["artifacts"] / "manifest.json"
    if not path.exists():
        return "尚无安装包"
    try:
        record = read_manifest(path)
        if record.get("layout_id", "gallery-stock-v1") != info.get("target", "stock"):
            return "类型不匹配，请重新编译"
        text = f'{record.get("firmware_version", "旧包未记录版本")} / {record.get("commit", "未记录")[:12]}'
        return text + ("（旧目录）" if package["artifacts"] != info["artifacts"] else "")
    except (OSError, ValueError, RuntimeError, AttributeError, TypeError):
        return "安装包元数据异常，请重新编译"


def show_status(info):
    for key in ("source", "branch", "commit", "kind", "project", "build", "artifacts"):
        print(f"{key}: {info[key]}")
    if info["kind"] == "gallery":
        print("build target:", info["target"])
        print("existing package (not yet verified):", package_summary(info))
        print("install artifacts:", existing_package(info)["artifacts"])
    print("shared SDK:", LOCAL / "esp-idf" / IDF_VERSION)
    print("shared host venv:", HOST_PYTHON.parent.parent)
    print("backups:", LOCAL / "backups")


def interactive(info):
    source = info["source"]
    while True:
        info = layout(source)
        print("\nHueLeaf 本机工具\n" + "─" * 44)
        print(f'当前开发版本：HueLeaf {firmware_version(info)}')
        print(f'源码分支：{info["branch"]} / {info["commit"][:12]}')
        print("已有产物（安装前校验）：", package_summary(info))
        try:
            action = choose("请选择操作", [("1", "编译并安装"), ("2", "仅编译并打包"),
                ("3", "安装已有构建"), ("4", "设备预检与备份"), ("5", "打开传图 GUI"),
                ("6", "运行测试"), ("7", "查看详细状态"), ("8", "高级设备操作")], back="退出")
        except (MenuCancelled, EOFError, KeyboardInterrupt):
            print("\n已退出。")
            return 0
        try:
            if action in ("1", "3", "4", "8") and info["kind"] != "gallery":
                raise RuntimeError("当前工程不支持此安装入口")
            if action in ("1", "2"):
                menu_command(info, "build")
                print("\n编译打包完成：", info["artifacts"])
            if action in ("1", "3", "4"):
                device_workflow(info, "preflight" if action == "4" else "install")
            elif action in ("5", "6"):
                menu_command(info, "gui" if action == "5" else "test")
            elif action == "7":
                show_status(info)
            elif action == "8":
                operation = choose("高级设备操作", [("1", "安装（自定义备份策略／首次迁移）"),
                                                  ("2", "预检与备份（高级）")])
                device_workflow(info, "install" if operation == "1" else "preflight", advanced=True)
        except EOFError:
            print("\n输入已结束，退出。")
            return 0
        except (MenuCancelled, KeyboardInterrupt):
            print("\n操作已取消，返回主菜单。")
        except MENU_ERRORS as exc:
            print("\n操作失败，返回主菜单：", exc)


def main(argv=None):
    parser = argparse.ArgumentParser(description="HueLeaf 本机共享工具；终端无参数启动交互菜单，非交互环境默认显示状态。")
    parser.add_argument("command", nargs="?",
                        choices=("menu", "status", "build", "test", "ports", "gui", "push", "preflight", "install"))
    parser.add_argument("args", nargs=argparse.REMAINDER, help="传给 GUI/传图工具的参数，或设备串口")
    args = parser.parse_args(argv)
    terminal = sys.stdin.isatty() and sys.stdout.isatty()
    args.command = args.command or ("menu" if terminal else "status")
    if args.command == "menu" and not terminal:
        raise RuntimeError("交互菜单需要终端，请直接运行入口；脚本中请使用显式子命令")
    remaining = args.args
    info = layout(source_root())
    if args.command == "menu":
        return interactive(info)
    if args.command == "status":
        show_status(info)
    elif args.command == "build":
        build(info)
    elif args.command == "test":
        tests(info)
    elif args.command in ("ports", "gui", "push"):
        host_command(info, args.command, remaining)
    else:
        port, flags = device_args(remaining)
        hardware(info, args.command, port, flags)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, OSError, ValueError, subprocess.CalledProcessError) as exc:
        print("错误：", exc, file=sys.stderr)
        sys.exit(1)
