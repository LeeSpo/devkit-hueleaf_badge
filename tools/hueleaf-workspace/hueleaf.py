#!/usr/bin/env python3
"""HueLeaf: choose a Release tag or a local BIN and install."""
import argparse
from pathlib import Path
import subprocess
import sys

from paths import data_root, source_root
from release_client import ReleaseClient, resolve_source, show_releases


def run_gui():
    from bootstrap import ensure_host
    root = source_root()
    gui = root / "tools/hueleaf-push" if root else Path(__file__).resolve().parent / "gui"
    if not (gui / "hueleaf_gui.py").is_file():
        raise RuntimeError("当前工具包缺少 GUI 文件")
    python = ensure_host(gui)
    return subprocess.run([str(python), str(gui / "hueleaf_gui.py")]).returncode


def run_developer(command, args):
    if source_root() is None:
        raise RuntimeError("此命令需要源码仓库；安装发布固件只需选择标签或本地 BIN")
    launcher = Path(__file__).resolve().parent / "hueleaf-local.sh"
    return subprocess.run(["bash", str(launcher), command, *args]).returncode


def install_command(args):
    from installer import install
    source = resolve_source(args.source, data_root() / "downloads")
    if source is None:
        return 0
    return install(source, args.backup_dir or data_root() / "backups", args.port, args.yes,
                   preflight_only=args.command == "preflight")


def menu():
    while True:
        print("\nHueLeaf\n  1. 安装固件\n  2. 打开传图 GUI\n  3. 编译当前源码\n  4. 查看状态\n  0. 退出")
        try:
            choice = input("请选择：").strip()
            if choice in ("", "0"):
                return 0
            if choice == "1":
                main(["install"])
            elif choice == "2":
                run_gui()
            elif choice == "3":
                run_developer("build", [])
            elif choice == "4":
                print("源码：", source_root() or "独立安装工具")
                print("数据：", data_root())
            else:
                print("请输入列表中的序号。")
        except EOFError:
            print("\n已退出。")
            return 0
        except KeyboardInterrupt:
            print("\n已取消。")
        except (RuntimeError, ValueError, OSError, subprocess.CalledProcessError) as exc:
            print("操作失败：", exc, file=sys.stderr)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command")
    sub.add_parser("menu", help="打开交互菜单")
    sub.add_parser("releases", help="列出带新格式固件 BIN 的 Release")
    sub.add_parser("status", help="查看源码与数据路径")
    sub.add_parser("gui", help="打开传图 GUI")
    for name in ("install", "preflight"):
        child = sub.add_parser(name, help="选择标签或本地 BIN" if name == "install" else "只检查设备并备份")
        child.add_argument("source", nargs="?", help="Release 标签或本地 BIN 路径")
        child.add_argument("--port", help="例如 COM5 或 /dev/cu.usbmodemXXXX")
        child.add_argument("--backup-dir", type=Path)
        child.add_argument("--yes", action="store_true", help="备份后直接确认安装")
    for name in ("build", "test", "workspace"):
        child = sub.add_parser(name, help="源码开发命令")
        child.add_argument("args", nargs=argparse.REMAINDER)
    package = sub.add_parser("package", help="从已验证构建生成安装 ZIP 和固件 BIN")
    package.add_argument("--build-dir", type=Path, required=True)
    package.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args(argv)
    if args.command is None:
        if not sys.stdin.isatty():
            parser.print_help()
            return 0
        return menu()
    if args.command == "menu":
        return menu()
    if args.command == "releases":
        show_releases(ReleaseClient().releases())
        return 0
    if args.command in ("install", "preflight"):
        if args.source is None and not sys.stdin.isatty():
            raise ValueError("非交互安装请指定 Release 标签或本地 BIN 路径")
        return install_command(args)
    if args.command == "gui":
        return run_gui()
    if args.command == "status":
        print("源码：", source_root() or "独立安装工具")
        print("数据：", data_root())
        return 0
    if args.command == "package":
        if source_root() is None:
            raise RuntimeError("生成发布附件需要源码仓库和构建输出")
        from package_release import package_release
        package_release(args.build_dir, args.output_dir)
        return 0
    return run_developer("menu" if args.command == "workspace" else args.command, args.args)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, ValueError, OSError, subprocess.CalledProcessError) as exc:
        print("错误：", exc, file=sys.stderr)
        sys.exit(1)
    except (EOFError, KeyboardInterrupt):
        print("\n已取消。", file=sys.stderr)
        sys.exit(130)
