# HueLeaf 安装与工作区工具

选择 GitHub Release 标签或本地 BIN，选择设备，备份并安装。安装时无需 Git、ESP-IDF、编译器或手工填写刷写地址。

当前固件主要维护 v2 蓝牙遥控版本。安装工具全面支持固件整片自动备份、分区自动识别与一键烧录升级（历史分支与归档见[版本政策](../../firmware/hueleaf-gallery/docs/branches.md)）。

## 下载和运行

从采用新发布格式的 Release 下载两个附件：

```text
hueleaf-installer.zip
hueleaf-firmware-版本.bin
```

安装工具通常只需下载一次；也可以只下载工具，让它按标签下载固件。旧 Release 保持原样，本工具的 Release 列表只显示带 BIN 附件的新版本。

安装 Python **3.12**，解压 `hueleaf-installer.zip`：

- Windows：双击 `install.bat`。
- macOS：双击 `install.command`，或运行 `bash hueleaf.sh`。
- Linux：运行 `bash hueleaf.sh`。

首次运行自动在数据目录建立虚拟环境并安装 `esptool==4.13.0` 和 pyserial。需要联网下载这些 Python 依赖；后续可以离线选择本地 BIN 安装。可用 `HUELEAF_PYTHON` 指定 Python 3.12。

选择“安装固件”，直接输入列表序号、Release 标签或本地 BIN 路径。正式版和预发布在同一列表，不需要选择版本通道或分区布局。GitHub 不可达时仍可输入本地路径。

公开发布默认匿名查询。遇到 GitHub API 限流，可选设置 `GH_TOKEN` 或 `GITHUB_TOKEN`；令牌仅用于 API，不写入文件，也不附加到固件下载请求。

## 命令行

通过启动器会自动准备运行环境：

```sh
bash hueleaf.sh releases
bash hueleaf.sh install
bash hueleaf.sh install v2.0.0
bash hueleaf.sh install "/path/with spaces/hueleaf-firmware-2.0.0.bin" --port /dev/cu.usbmodemXXXX
bash hueleaf.sh preflight ./hueleaf-firmware-2.0.0.bin --port COM5
bash hueleaf.sh gui
bash hueleaf.sh status
```

上面的版本号仅示范用法，实际以发布列表为准。Windows 可将 `bash hueleaf.sh` 换为 `install.bat`。

手工准备环境也可以：

```sh
python3.12 -m venv /path/to/venv
/path/to/venv/bin/python -m pip install -r requirements-install.txt
/path/to/venv/bin/python hueleaf.py install ./firmware.bin --port COM5
```

- 每次安装先检查设备安全状态，并保存完整 4 MiB 备份，再确认写入。
- `preflight` 只检查并备份，不擦除、不写 flash。
- `--yes` 可跳过最后的交互确认，仍执行检查和备份。
- `--backup-dir DIRECTORY` 可指定备份位置。
- 安装工具会根据目标设备 Flash 分区表自动计算写入地址。

## BIN 的安装约定

新发布的 `hueleaf-firmware-版本.bin` 包含 bootloader、分区表、OTA 启动数据、App，以及文件末尾的安装信息。工具自动提取需要写入的区域；文件中的空洞和末尾信息不会写入设备，不把整个文件从 0 地址直接刷入。

首次迁移自动处理，最终确认会说明是否替换原厂应用区。NVS/PHY 保留；同布局日常更新不擦除蓝牙绑定区。程序从设备/镜像分区信息确定地址，用户不填地址。

无安装信息的普通 `.bin` 默认作为应用固件，按设备 OTA 应用分区自动写入，并设置下次启动到该分区。用户负责选择文件；程序不做 BIN 类型、版本或应用兼容性拦截。首次安装使用发布的完整安装 BIN。

工具支持基于新版格式打包的固件 BIN 文件，通过统一的 Python/Shell 入口安全烧录。

## 传图 GUI

安装 ZIP 内含完整 `gui/` 及网页静态资源。菜单选择“打开传图 GUI”或执行 `gui`，首次使用时补齐图片转换依赖。纯刷机无需 Pillow/NumPy/Numba。

Android 源码独立位于 [`android/hueleaf-remote`](../../android/hueleaf-remote/README.md)。APK 独立发布，不混入固件 Release 的两个附件。

## 数据目录

源码、工具和本地数据分开：

- 本仓库源码运行：默认使用仓库相邻的 `local/`，兼容现有工作区。
- 独立安装 ZIP：macOS 使用 `~/Library/Application Support/HueLeaf`，Windows 使用 `%LOCALAPPDATA%\HueLeaf`，Linux 使用 `$XDG_DATA_HOME/hueleaf` 或 `~/.local/share/hueleaf`。
- `HUELEAF_DATA_DIR` 可覆盖数据目录；`HUELEAF_SOURCE` 可指定源码目录。

下载写入 `downloads/`，备份写入 `backups/`，虚拟环境写入 `venvs/`。不要求解压目录可写，不依赖工作目录或仓库名。备份沿用现有完整性记录，供恢复时使用。

## 源码开发

在仓库内执行：

```sh
./tools/hueleaf-workspace/setup-dev.sh
./tools/hueleaf-workspace/hueleaf.sh build
./tools/hueleaf-workspace/hueleaf.sh test
./tools/hueleaf-workspace/hueleaf.sh workspace
```

`setup-dev.sh` 准备 ESP-IDF v5.5.4 和 Python 3.12。v2 默认且仅构建蓝牙遥控版，拒绝 `HUELEAF_REMOTE=0`；复现传统版需切到归档源码。`remote-v1` 是兼容保留的布局标识，与固件 2.x 版本号无关。源码构建保留已有分支/项目/SDK/布局隔离及严格来源检查。旧工作区入口可继续转发到 `hueleaf-local.sh`；其 `install` 仍安装当前源码构建，与公开入口的 `install 标签或文件` 区分。

从已构建输出生成新发布附件：

```sh
python tools/hueleaf-workspace/package_release.py \
  --build-dir /absolute/path/to/build \
  --output-dir /absolute/path/to/new-release-assets
```

打包前检查构建来源与输出，输出目录必须是新目录。生成目录只有 `hueleaf-installer.zip` 和 `hueleaf-firmware-版本.bin`，不附带校验清单、恢复包或独立 GUI ZIP。安装 ZIP 使用固件目录中的同一份设备辅助代码，避免维护两个版本。

现有 CI 构建后上传这两个候选附件；后续推送新的 `v*` 标签时创建固件 Release。发布流程拒绝覆盖已有 Release，旧附件不调整。Android 使用独立工作流构建；正式签名 APK 发布需要配置签名 secrets，详见 Android README。

## 测试

```sh
python -m pytest -q tools/hueleaf-workspace/tests
```

测试覆盖 Release/本地输入、下载失败、安装镜像、自动地址、迁移、预检、取消，以及打包后的独立运行。设备 I/O 使用模拟，测试不会访问真实串口。

## License

管理工具遵循仓库 [GPL-3.0](../../LICENSE)。分发 ZIP 附带仓库许可文件；源码来自此仓库相应版本。
