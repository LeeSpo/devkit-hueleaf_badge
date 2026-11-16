# HueLeaf 彩叶 v2 · 蓝牙遥控相册固件

给 OSPTEK 彩叶 HueLeaf（ESP32-C5 · 3.7″ 六色墨水屏）打造的离线电子相册固件，挂在包上当个性挂件或放置于桌面。

<p align="center">
  <img src="docs/images/banner.png" alt="OSPTEK 彩叶 HueLeaf 墨水屏开发套件 基础版" width="100%">
</p>

<p align="center">
  <img src="https://img.shields.io/badge/MCU-ESP32--C5-1E1E1E?style=for-the-badge" alt="MCU">
  <img src="https://img.shields.io/badge/PMU-AXP2101-D93A2B?style=for-the-badge" alt="PMU">
  <img src="https://img.shields.io/badge/E--Paper-3.7%22%206--Color-F0BE32?style=for-the-badge" alt="Display">
  <img src="https://img.shields.io/badge/Wi--Fi%206-2.4%20%2F%205%20GHz-2058C4?style=for-the-badge" alt="Wi-Fi">
  <img src="https://img.shields.io/badge/RTC-PCF8563-2E8B4A?style=for-the-badge" alt="RTC">
</p>

<p align="center">
  <b>简体中文</b> &nbsp;|&nbsp; <a href="README_EN.md">English</a>
</p>

<img src="docs/images/divider.svg" alt="" width="100%" height="6">

## 📖 简介

本项目为 **OSPTEK 彩叶 HueLeaf** 开发套件（ESP32-C5 主控，搭配 3.7 英寸 480×720 六色电子墨水屏）量身定制的离线电子相册固件。

照片存放在 TF 卡中，支持电脑通过 USB 批量管理与传输。你可以直接使用机身实体按键翻页、轻拍外壳换图，也可以通过配套的 Android 手机 App 进行低功耗蓝牙（BLE）无线遥控，无需 Wi-Fi 或局域网。

<p align="center"><img src="docs/images/device_photo.jpg" alt="HueLeaf 实物" width="420"></p>

> [!NOTE]
> - **版本说明**：固件自 v2.0 起全面转向蓝牙遥控架构；传统保留出厂固件的版本已在 v1.5.0 归档冻结（详见[分支与归档说明](firmware/hueleaf-gallery/docs/branches.md)）。
> - **分区调整**：v2 固件使用独立分区（`remote-v1`）以释放更充裕的 Flash 空间，首次刷入会替换出厂应用区；安装工具会在写入前自动进行 Flash 完整备份，可随时按需恢复。
> - **开源声明**：本项目为社区开源作品，非 OSPTEK 官方出品。

---

## ✨ 核心特性

- 🎨 **六色墨水屏显色**：专为 3.7″ 480×720 六色屏（黑、白、黄、红、蓝、绿）优化的抖动算法，色彩鲜艳细腻。
- ⚡ **MonoFast 极速快刷**：选图菜单与提示界面采用黑白快刷（单次刷新约 0.2 秒），流畅选图无频繁全屏闪烁。
- 📱 **蓝牙无线遥控**：配套 Android App（Material 3 设计），支持无线浏览相册列表（支持中文标签）、远程遥控切图、查看电量及设备状态。
- 🖥️ **跨平台电脑工具**：提供免安装驱动的 Web 图形化工具与 Python CLI，支持拖拽裁切、实时预览、批量上传与时钟同步。
- ⏱️ **定时深睡轮播**：支持设定定时自动翻页，轮播间隔期间进入深度休眠，极低功耗待机，断电保留画面。
- 📳 **敲击感应交互**：利用板载三轴加速度传感器，支持双击外壳换图及休眠时轻拍唤醒。
- 🛡️ **安全安装与升级**：跨平台一键安装脚本，自动备份 Flash；支持将固件放入 TF 卡开机自动安全升级。

---

## 🚀 快速上手

### 1. 获取安装包与固件

前往 GitHub Releases 页面下载：
- **安装工具包**：`hueleaf-installer.zip`
- **固件文件**：`hueleaf-firmware-<版本>.bin`
- **手机 App**（可选）：在配套 Release 中下载 Android APK。

### 2. 刷写固件

解压 `hueleaf-installer.zip`，使用 USB 数据线将相框连接至电脑（需安装 Python 3.12+）：

- **Windows**：双击运行 `install.bat`
- **macOS**：双击运行 `install.command`
- **Linux**：终端执行 `bash hueleaf.sh`

按照脚本交互提示选择「安装固件」，指定下载的固件 BIN 文件与串口端口，确认后即可自动备份并完成烧录。

也可以通过命令行直接安装：
```bash
bash hueleaf.sh install ./hueleaf-firmware-<版本>.bin --port /dev/ttyACM0
# Windows 示例：install.bat install .\hueleaf-firmware-<版本>.bin --port COM5
```

详细说明请参考 [公开安装与工作区工具说明](tools/hueleaf-workspace/README.md)。

---

## 📂 TF 卡目录结构（FAT32）

相框需要配合 FAT32 格式的 MicroSD / TF 卡使用，目录规范如下：

```text
/img/*.bin           每张 172800 字节的六色点阵帧（由电脑工具转换生成）
/img/index.txt       （可选）自定义播放顺序清单，每行一个文件名；若无则按字母排序
/img/slideshow.txt   （可选）参与定时轮播的图片清单（可在电脑 GUI 中直接勾选生成）
/img/.labels/        （可选）图片中文标题小图（电脑工具上传时自动生成，供快刷菜单显示）
/update/firmware.bin （可选）TF 卡升级固件。开机自动校验并烧录，成功后自动更名为 firmware.bin.done
/hueleaf.cfg         （可选）配置文件，手动开关特定硬件特性
```

- **断电记忆**：设备断电或更换电池后，开机自动恢复展示最后一次浏览的图片。
- **传图保护**：USB 传图期间会自动屏蔽物理按键，传输完成后保持画面，避免传图被意外打断。

---

## 🖥️ 传图与电脑工具（hueleaf-push）

项目提供易用的图形界面（Web GUI）与命令行工具，位于 `tools/hueleaf-push`。详细介绍见 [hueleaf-push 文档](tools/hueleaf-push/README.md)。

### 启动图形界面 (Web GUI)

在安装工具或工作区中启动 GUI：
```bash
./hueleaf.sh gui
# 或在 tools/hueleaf-push 目录下：python -m gui
```

在浏览器中打开 GUI 后，你可以：
- 拖拽图片直接转换六色点阵，支持自由裁切、旋转、缩放与抖动算法调优；
- 一键批量上传到相框 TF 卡，自动生成中文标签；
- 查看相框内所有图片列表，自由勾选参与自动轮播的照片；
- 同步电脑时间至相框硬件 RTC、配置手机遥控 4 位 PIN 码。

### 命令行常用操作

```bash
cd tools/hueleaf-push && pip install -r requirements.txt

# 转换图片并预览效果
python hueleaf_push.py photo.jpg --preview out.png

# 转换、上传并立即显示（带进度条）
python hueleaf_push.py -p /dev/ttyACM0 photo.jpg --rotate 90 --fit pad

# 管理相框文件与状态
python hueleaf_push.py -p /dev/ttyACM0 --list            # 查看卡内图片
python hueleaf_push.py -p /dev/ttyACM0 --show photo.bin  # 切换显示指定图片
python hueleaf_push.py -p /dev/ttyACM0 --set-time        # 同步电脑时间到 RTC
python hueleaf_push.py -p /dev/ttyACM0 --info            # 查询设备状态与电量
python hueleaf_push.py -p /dev/ttyACM0 --log             # 读取固件运行日志

# 离线转换：仅生成 .bin 文件，自行拷贝到 TF 卡 /img/ 目录
python hueleaf_push.py photo.jpg --save-bin output.bin
```

---

## 📱 手机蓝牙遥控（Android）

HueLeaf v2 固件原生支持 BLE 低功耗蓝牙遥控：

1. **设置 PIN 码**：在电脑 Web GUI 的「显示设置」中配置 4 位数字 PIN（例如 `1234`）并保存。
2. **连接相框**：打开手机 App，授予蓝牙权限，输入相同的 4 位 PIN，点击「扫描相框」。
3. **开始遥控**：
   - **切图控制**：上一张、下一张、指定图片跳转；
   - **图库浏览**：显示相册全部图片与中文标签，查看哪张图片正在显示，标记轮播状态；
   - **状态监控**：实时查看电池电量与相框状态；
   - **省电退出**：支持一键休眠断开连接。

更多说明请查看 [Android 遥控端说明文档](android/hueleaf-remote/README.md)。

---

## 🎮 机身操作说明

开机到首张图片加载完成前，按键暂不响应，属正常初始化流程。

### 1. 正常看图模式

| 操作 | 响应动作 |
| :--- | :--- |
| **BOOT 键短按** | 切换至上一张（首张前循环至末张） |
| **电源键短按** | 切换至下一张（末张后循环至首张） |
| **BOOT 键长按**（约 1 秒） | 打开「图片选择菜单」（MonoFast 黑白快刷） |
| **连拍两下相框** | 切换至下一张（需在配置中开启 `tap_next=1`） |
| **电源键长按** | 硬件关机（关机后定时轮播停止，需手动按键开机） |
| **复位键（RST）** | 软重启设备 |

- **防抖保护**：六色全屏物理刷新耗时约十余秒，刷新期间的按键和敲击会自动忽略，不会连跳。
- **状态指示**：有效按键时机身蓝色充电指示灯闪烁一下；底部状态条可显示电量、序号和时间（可在设置中隐藏）。

### 2. 快刷选图菜单（MonoFast）

长按 BOOT 键进入黑白快刷菜单（每页 14 行，无缩略图，滚动约 0.2 秒），支持中文文件名：

| 操作 | 默认模式 (`tap_next=0`) | 敲击模式 (`tap_next=1`) |
| :--- | :--- | :--- |
| **BOOT 键短按** | 光标向下移动一行 | 光标向下移动一行 |
| **电源键短按** | 向下翻页 | 光标向上移动一行 |
| **连拍两下** | — | 向下翻页 |
| **BOOT 键长按** | 确认选中并全屏刷出该图 | 确认选中并全屏刷出该图 |

- 超过 60 秒无操作将自动退出菜单，恢复显示原图片。

---

## ⚙️ 硬件配置与定时轮播

### 1. 定时深睡轮播

支持在设定的时间间隔自动换图，并在换图间隙让 MCU 进入深度休眠（Deep Sleep），大幅延长电池续航。墨水屏双稳态特性使得休眠期间画面持续清晰显示，零耗电。

- **开启方式**：
  - 推荐在电脑 Web GUI「显示设置」中勾选 **定时换图（深睡省电）** 并设置间隔（如 60 分钟），保存即生效；
  - 也可以在 TF 卡根目录的 `hueleaf.cfg` 中写入：
    ```ini
    slideshow_enabled=1
    slideshow_interval_min=60
    tap_wake=0   # 设为 1 可在休眠状态下敲击外壳唤醒
    ```
- **挑选轮播图片**：在 Web GUI 的相框图片列表中，直接勾选/取消勾选想轮播的图片即可。未勾选的照片不会自动播放，但依然可通过按键或手机遥控手动调出。
- **休眠与唤醒**：连接电脑 USB 时自动暂停轮播休眠；拔出后继续倒计时。休眠中可按复位键唤醒，或开启 `tap_wake=1` 轻拍相框唤醒。

### 2. `hueleaf.cfg` 配置选项

在 TF 卡根目录放置 `hueleaf.cfg`（`#` 开头为注释）：

| 配置项 | 默认值 | 说明 |
| :--- | :---: | :--- |
| `slideshow_enabled` | `0` | `1` 启用定时自动换图，`0` 关闭 |
| `slideshow_interval_min` | `60` | 定时换图间隔（1～10080 分钟） |
| `tap_wake` | `0` | `1` 允许在深睡时双击外壳唤醒相框 |
| `tap_next` | `0` | `1` 开启双击外壳翻页，并优化选图菜单按键映射 |
| `mono_fast` | `1` | `1` 选图菜单启用黑白极速快刷（~0.2s），`0` 使用原厂全刷 |

---

## 🛠️ 常见问题与恢复

### 1. 串口无法识别 / 进入刷机模式
- ESP32-C5 原生 USB 在正常情况下可通过脚本自动复位进入下载模式；
- 如遇无法识别或异常，可**按住机身 BOOT 键不放，然后插入 USB 线**（或按住 BOOT 键短按一下 RST 复位键），松开后即可强制进入 ROM 下载模式。

### 2. 恢复出厂设置或救援
- **使用备份恢复**：首次安装固件时，安装工具会在本地生成备份文件（如 `backup.bin`）。若需完全复原，进入 ROM 下载模式后运行：
  ```bash
  esptool.py --chip esp32c5 write_flash 0 backup.bin
  ```
- **刷回官方出厂固件**：
  ```bash
  esptool.py --chip esp32c5 erase_flash
  esptool.py --chip esp32c5 write_flash 0x0 examples/3.7einkDvb/firmware/3.7einkDvb_factory_0x0.bin
  ```

### 3. 六色屏刷新较慢属于故障吗？
属于正常现象。六色全彩色墨水屏具有多个粒子驱动相位的物理特性，全屏彻底刷新通常需要 10～15 秒。在刷新过程中，为了保护屏幕粒子状态，固件会自动屏蔽按键与拍击，请等待刷新完成即可。

---

## 📋 硬件规格

| 项目 | 参数 |
| :--- | :--- |
| **主控芯片** | ESP32-C5（ESP32-C5HF4，RISC-V 架构） |
| **显示屏幕** | 3.7″ 六色全彩电子墨水屏 EPD037A02，分辨率 480 × 720（黑、白、红、黄、蓝、绿） |
| **无线连接** | 双频 Wi-Fi 6（2.4 / 5 GHz）+ Bluetooth 5 (LE) |
| **电源管理** | AXP2101 PMIC（充放电管理、电量计） |
| **硬件时钟** | PCF8563 独立实时时钟（RTC） |
| **动作感应** | MSA321 三轴加速度传感器 |
| **板载存储** | 4 MB Flash，支持 MicroSD / TF 卡扩展 |
| **接口按键** | Type-C USB（原生 USB-Serial-JTAG）、复位键（RST）、电源键（PWR）、BOOT 键 |
| **状态指示** | 充电指示灯（蓝）、运行指示灯（绿） |

原理图详见 [hardware/HueLeaf_Base_Schematic_V1.0.pdf](hardware/HueLeaf_Base_Schematic_V1.0.pdf)，元件位号图详见 [hardware/HueLeaf_Base_Placement.pdf](hardware/HueLeaf_Base_Placement.pdf)。

---

## 📚 开发者参考

- **固件开发与分区说明**：[firmware/hueleaf-gallery/README.md](firmware/hueleaf-gallery/README.md)
- **蓝牙协议与休眠规范**：[firmware/hueleaf-gallery/docs/ble-protocol.md](firmware/hueleaf-gallery/docs/ble-protocol.md) · [remote-control.md](firmware/hueleaf-gallery/docs/remote-control.md)
- **真机验收与测试清单**：[firmware/hueleaf-gallery/docs/real_hw_test.md](firmware/hueleaf-gallery/docs/real_hw_test.md)
- **分支与历史版本归档**：[firmware/hueleaf-gallery/docs/branches.md](firmware/hueleaf-gallery/docs/branches.md)
- **上游原始仓库**：[osptek/devkit-hueleaf_base](https://github.com/osptek/devkit-hueleaf_base)

---

## 📄 开源许可证

本项目以 [GPL-3.0](LICENSE) 协议开源。
- `firmware/hueleaf-gallery/main/screen_driver/mono_fast.c` 黑白快刷波形驱动移植自 kirmisaki 的 [FrameFilm](https://github.com/kirmisaki/FrameFilm)（GPL-3.0-or-later），因此衍生固件按 GPL-3.0 分发；
- 上游代码（如 `examples/`）保留其原有的 [Apache-2.0](LICENSE-APACHE-2.0) 许可；
- 硬件设计资料与文档（`hardware/`、`docs/`）遵循 [CC BY-NC-SA 4.0](LICENSE-CC-BY-NC-SA-4.0)；
- 其余第三方组件沿用各自原有许可证。
