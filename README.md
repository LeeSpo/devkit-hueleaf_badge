# HueLeaf 彩叶 · TF 卡相册固件

给 OSPTEK 彩叶 HueLeaf（ESP32-C5 · 3.7″ 六色墨水屏）做的离线相册固件，挂在包上当挂件用

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
  <b>简体中文</b>  |  <a href="README_EN.md">English</a>
</p>

<img src="docs/images/divider.svg" alt="" width="100%" height="6">

## 简介

图片存在 TF 卡里，BOOT 键和电源键翻页、连拍两下换图，用 USB 线从电脑传图。不需要 Wi-Fi、路由器或 App。

- **不碰出厂固件**：出厂固件留在 factory 分区，**拔掉 TF 卡开机就回到出厂固件**。
- 本固件是社区作品，非 OSPTEK 官方，很多功能还没在真机上验证过（文中标「未实测」）。

<p align="center"><img src="docs/images/device_photo.jpg" alt="HueLeaf 实物" width="420"></p>

## 1. 备份（必做）

```bash
pip install esptool
# 按住 BOOT 插 USB 进入下载模式（若自动复位不生效）
espefuse.py --chip esp32c5 -p /dev/ttyACM0 summary      # 确认未开启 Secure Boot / Flash 加密、无写保护
esptool.py --chip esp32c5 -p /dev/ttyACM0 read_flash 0 0x400000 backup.bin
```

妥善保存 `backup.bin`（含你的设置）。若 summary 显示已开启 Secure Boot 或 Flash Encryption，**停止**，本固件不支持。

## 2. 刷写

从 Actions 工件或本地构建获取 `dist/`：

```bash
cd firmware/hueleaf-gallery && idf.py set-target esp32c5 build && ./tools/package.sh
./dist/install/flash_install.sh /dev/ttyACM0     # 只写 bootloader/分区表/otadata/ota_0，不碰 0x10000 与 NVS
# Windows: dist\install\flash_install.bat COM5   （或 python flash_install.py COM5）
```

安装脚本写入前会自动整片备份并检查出厂固件大小，细节见[开发者说明](firmware/hueleaf-gallery/README.md#刷写预检)。

## 3. TF 卡布局（FAT32）

```
/img/*.bin          每张 172800 字节的预抖动六色帧（hueleaf-push 生成）
/img/index.txt      可选，每行一个文件名，决定顺序；否则按文件名排序
/update/firmware.bin 可选，开机写入 ota 槽，成功后改名 firmware.bin.done（旧 .done 先删除；改名失败则删除 firmware.bin），失败改名 .failed；已处理过的镜像（大小+SHA-256 记在 NVS）不会重复写入，不会反复重启
```

当前图片按**文件名**记住，断电/换电池后恢复。

固件 1.5.0 配合新版电脑 GUI 支持整批上传保护：上传期间忽略按键换图，全部完成后点「结束传图」恢复操作，保持当前画面，不自动重启。刷屏期间仍可及时读取设备状态。

## 4. 放图和选图工具（hueleaf-push）

图形界面和完整命令见 [tools/hueleaf-push/README.md](tools/hueleaf-push/README.md)。也可以用 `--save-bin` 生成 `.bin` 后直接拷进 TF 卡 `/img/`。

```bash
cd tools/hueleaf-push && pip install -r requirements.txt
python hueleaf_push.py photo.jpg --preview out.png          # 仅本地预览
python hueleaf_push.py -p /dev/ttyACM0 photo.jpg             # 转换+上传(进度条)+显示
python hueleaf_push.py -p COM5 photo.jpg --rotate 90 --fit pad
python hueleaf_push.py -p /dev/ttyACM0 --list | --info | --log | --delete NAME | --show NAME
python hueleaf_push.py -p /dev/ttyACM0 --set-time            # 用电脑时间设置 RTC
python hueleaf_push.py -p /dev/ttyACM0 --firmware build/hueleaf_gallery.bin   # 经 USB 放入 TF 升级并重启
python hueleaf_push.py -p /dev/ttyACM0 --stock               # 临时进入出厂固件一次
python hueleaf_push.py photo.jpg --save-bin a.bin            # 生成 .bin，自行拷到 TF 卡 /img/
```

## 5. 按键与拍拍换图

开机到第一张图显示出来之前，按键和双击都不响应，这是正常的。

> 标「未实测」的项目还没在真机上验证过。

### 看图时

| 操作                                      | 动作                               |
| ----------------------------------------- | ---------------------------------- |
| BOOT 短按                                 | 上一张（首张前回到最后一张）       |
| 电源键短按                                | 下一张（末张后回到第一张）         |
| BOOT 长按（约 1 秒，按住时触发）          | 打开「选择图片」列表               |
| 连拍两下相框（需 `tap_next=1`，未实测） | 下一张                             |
| 电源键长按                                | 硬件关机，定时轮播停止，需手动开机 |
| 复位                                      | 重启                               |

- 一次只走一步：图片刷新期间（六色整屏刷新要十几秒）的按键和拍击**直接忽略**，不会排队连跳。
- 按键或拍击被接受时，蓝色充电灯闪一下；被忽略时不闪（未实测：电池供电时是否会亮、白天是否看得见）。
- 底部状态条：电量%、n/N、RTC 时间（`--strip off` 关闭）。开启状态条时，开机/唤醒会刷新当前图片以更新这些信息；关闭时若图片未变化可保留电子纸旧画面。

### 选择图片列表里

只显示名称（无缩略图），黑白绘制，一页 14 行；打开时定位到当前图片。底部提示行随 `tap_next` 切换。

| 操作       | `tap_next` 关（默认）                  | `tap_next` 开                                          |
| ---------- | ---------------------------------------- | -------------------------------------------------------- |
| BOOT 短按  | 光标下移                                 | 光标下移                                                 |
| 电源键短按 | 下一页                                   | 光标上移                                                 |
| 连拍两下   | —                                       | 下一页                                                   |
| BOOT 长按  | 选中并显示                               | 选中并显示                                               |
| 底部提示   | `BOOT down · Hold select · PWR page` | `BOOT down · PWR up` / `Tap×2 page · Hold select` |

- 到底/到顶、最后一页都绕回另一头；60 秒无操作自动退出，回到当前图片。
- 列表里连按会合并：最后一次操作后约 1.2 秒才刷新一次。
- 若列表还没画出来就长按确认当前图片，屏幕不刷新；列表已显示时退出会整屏六色刷回图片。
- 名称（含中文）由电脑端工具上传时渲染成小图存到 `img/.labels/`；手动拷贝的图片显示文件名（仅英文数字）。

### 拍拍换图怎么判定（未实测）

用板载 MSA321 加速度计的双击检测。为防止走路、包晃动误触：

- 看图时，拍之前相框要**静止约 1.5 秒**；列表里放宽到 **0.5 秒**（列表多半拿在手上用）。
- 两次被接受的拍击至少间隔 1.5 秒。
- 灵敏度需要真机调：日志里被接受打印 `double tap accepted`，被拒打印 `double tap ignored (...)`（`hueleaf_push.py --log` 读取）。

## 6. `hueleaf.cfg` 开关

TF 卡根目录放 `hueleaf.cfg`，每行 `key=value`，`#` 开头为注释，手动编辑后重启生效。

| 开关          | 默认  | 作用                                                                                                                          |
| ------------- | ----- | ----------------------------------------------------------------------------------------------------------------------------- |
| `mono_fast` | `1` | 列表和提示页用黑白快刷（约 0.2 秒/次，移植自 [FrameFilm](https://github.com/kirmisaki/FrameFilm)）；`0` = 全部用原厂整屏刷新 |
| `tap_next`  | `0` | `1` = 开启拍拍换图，并切换列表里的按键（见上表）                                                                            |

- 设置会保存到板子里：**删掉文件或某一行不会恢复默认**，要关掉必须明确写 `=0`（开同理写 `=1`）。

### 定时深睡轮播

支持此功能的固件可在电脑 GUI 的「显示设置」中开启 **定时换图（深睡省电）**，输入间隔分钟数并保存；也可以编辑 TF 卡配置：

```ini
slideshow_enabled=1
slideshow_interval_min=60
tap_wake=0 # 可选：设为1，允许双击外壳从深睡唤醒
```

- 默认关闭，默认间隔 60 分钟，范围为 1～10080 的整数分钟。关闭时轮播任务停止定期检查，重新开启后恢复。GUI 保存立即生效并同步更新卡上定时及拍击唤醒设置，保留其他配置及注释，重启不会被旧值覆盖。
- **指定轮播图片**：在 GUI「相框里的图片」列表勾选参与轮播的图片，点「保存轮播名单」生效（支持全选/全不选）。自动轮播沿用图库顺序并跳过未选图片；未选图片仍可通过按键、设备选图列表或 GUI「显示」手动展示，不删除、不隐藏。保存不刷屏、不重启，名单在卡上持久保存。首次升级、尚未保存名单时默认全部参与；保存自定义名单后，新上传图片默认不加入，覆盖同名图片保留勾选。
- 从上一次成功刷新结束计时，到期直接刷新下一张，再进入深睡。手动展示未选图片后重新计时，到期从它在图库中的位置向后找下一张已选图片。例如 A/B/C/D 只选 A/C，自动 A→C→A；手动显示 B 后，下次自动切到 C。只选一张时到期切到它，已显示它则不重复刷屏；全不选时停止自动换图，但已有图片的相框仍可深睡和拍醒（需开启）。空图库保持唤醒。内部休眠时钟存在温度相关误差，适合间隔轮播，不保证每天固定时刻准点。
- 开机或按复位后，设备就绪起有 60 秒操作窗口；接受手动按键操作后重新计时。定时唤醒后没有额外操作窗口，刷新完成直接再睡。
- **连电脑期间暂停轮播和深睡**，断开后重新开始完整间隔。普通充电器或充电宝供电时继续运行。深睡期间插 USB、短按翻页键不会直接唤醒：先按复位，或开启下面的拍击唤醒后双击外壳，再连接电脑或操作按键。
- **双击外壳唤醒**：GUI 显示设置中可单独勾选并点「保存定时设置」，默认关闭（`tap_wake=0`），只在定时深睡时生效，与 `tap_next` 拍击翻页独立。拍醒只恢复操作，不刷屏、不跳图；设备就绪起提供 60 秒窗口，并重新计算完整换图间隔。窗口里可按键或连接电脑；若 `tap_next=1`，后续双击仍可翻页。IMU 使用正常 125 Hz 模式以保证检测，开启会增加待机耗电，搬动/磕碰可能误唤醒。设置保存不需要重启，IMU 不可用时不进入深睡，便于排查。
- 长按电源键是硬件关机，无法定时自动开机；待传图、升级和刷屏结束后再关机。深睡期间屏幕保留图片，绿灯仍可能亮；首版保留 PMIC 供电轨和充电设置，续航以整板实测为准。
- 写卡、刷屏、上传会话和选图列表期间不会进入深睡。配置或休眠准备失败时保持唤醒，通过 `--info` / `--log` 排查；修复后重新保存设置或复位。
- 轮播名单文件为 TF 卡 `/img/slideshow.txt`，首行 `HLSEL1`、后续每行一个已选图片文件名；只有首行表示明确全不选。名单损坏/不可读时暂停自动换图和深睡，可在 GUI 重新勾选保存修复。删除图片时忽略不存在的文件名，保存名单时清理这些名字；移走名单文件则恢复默认全部参与。
- 旧固件的 GUI 定时/名单控件会按能力禁用并提示升级；只更新电脑 GUI 不会自动开启或获得固件功能。
- 启动日志打印 `MonoFast: ON/OFF (...)`、`TapNext: ON/OFF (...)`，括号里是来源（`hueleaf.cfg` / 上次保存的值 / 默认）。
- 编译期彻底关闭快刷：`CONFIG_HL_MONO_FAST=n`；快刷清理间隔为 `HL_MONO_FAST_CLEAN_EVERY`。快刷从图片进入时先全屏清理一次（约 3.3 秒），之后每 20 次插入一次清理。

## 7. 出问题怎么办

1. 拔卡开机即为出厂固件（相册固件不影响）。
2. 进不了下载模式：一般 USB 会自动进入；不行就按住 BOOT 再插 USB，或按住 BOOT 再按一下复位。
3. 刷坏/起不来：按住 BOOT 再插 USB（或按住 BOOT 按复位）进入 ROM 下载模式，写回备份：
   `esptool.py --chip esp32c5 write_flash 0 backup.bin`
   或官方出厂镜像：`esptool.py --chip esp32c5 erase_flash && esptool.py --chip esp32c5 write_flash 0x0 examples/3.7einkDvb/firmware/3.7einkDvb_factory_0x0.bin`
4. 待验证期间复位导致回到出厂固件：重新运行 `flash_install.sh`，或 `otatool.py -p PORT switch_ota_partition --slot 0`。

## 硬件规格

| 项目     | 参数                                                             |
| :------- | :--------------------------------------------------------------- |
| 主控     | ESP32-C5（ESP32-C5HF4）                                          |
| 显示屏   | 3.7″ 六色墨水屏 EPD037A02，480 × 720（黑、白、红、黄、蓝、绿） |
| 无线     | 双频 Wi-Fi 6，2.4 / 5 GHz                                        |
| 电源管理 | AXP2101                                                          |
| RTC 时钟 | PCF8563                                                          |
| 加速度计 | MSA321                                                           |
| 存储     | 4 MB Flash，TF 卡                                                |
| USB      | Type-C，ESP32-C5 原生 USB                                        |
| 按键     | 复位键、电源键、BOOT 键                                          |
| 指示灯   | 充电指示（蓝）、开机指示（绿）                                   |

完整原理图见 [hardware/HueLeaf_Base_Schematic_V1.0.pdf](hardware/HueLeaf_Base_Schematic_V1.0.pdf)，元件位置见位号图 [hardware/HueLeaf_Base_Placement.pdf](hardware/HueLeaf_Base_Placement.pdf)。

## 开发者资料

- 固件工作方式、分区表、`idf.py` 刷写保护：[firmware/hueleaf-gallery/README.md](firmware/hueleaf-gallery/README.md)
- 真机测试清单：[firmware/hueleaf-gallery/docs/real_hw_test.md](firmware/hueleaf-gallery/docs/real_hw_test.md)
- 分支与归档标签：[firmware/hueleaf-gallery/docs/branches.md](firmware/hueleaf-gallery/docs/branches.md)
- 引脚分配、出厂固件（网页传图、配网、出厂按键）：见上游 [osptek/devkit-hueleaf_base](https://github.com/osptek/devkit-hueleaf_base)，本仓库 `main` 分支保留原厂内容

## 许可证

> **本 fork（LeeSpo/devkit-hueleaf_badge）整体以 [GPL-3.0](LICENSE) 发布。** 原因：`firmware/hueleaf-gallery/main/screen_driver/mono_fast.c` 的黑白快刷（MonoFast）波形表与命令序列移植自 kirmisaki 的 [FrameFilm](https://github.com/kirmisaki/FrameFilm)（GPL-3.0-or-later），衍生固件因此必须按 GPL-3.0 分发。上游 osptek 的原始代码仍保留其 Apache-2.0 许可声明，原文见 [LICENSE-APACHE-2.0](LICENSE-APACHE-2.0)；硬件资料与文档许可不变。

- 上游代码（`examples/` 等）：原始授权为 [Apache-2.0](LICENSE-APACHE-2.0)，在本 fork 中与 GPL-3.0 代码合并分发
- 硬件资料与文档（`hardware/`、`docs/`）：[CC BY-NC-SA 4.0](LICENSE-CC-BY-NC-SA-4.0)
- 第三方组件（如 Vue）沿用其原有许可证
