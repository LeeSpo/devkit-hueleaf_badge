# HueLeaf 彩叶 TF 相册固件 v1（社区固件，非官方）

> 未在真机上验证。请先完成「备份」再刷写。本 fork 含 MonoFast 衍生代码，以 [GPL-3.0](../../LICENSE) 分发；上游代码保留原始 [Apache-2.0](../../LICENSE-APACHE-2.0) 声明，硬件资料仍为 CC BY-NC-SA。

## 文档

- 真机对比测试清单：[docs/real_hw_test.md](docs/real_hw_test.md)
- 分支与归档标签（`main` / `dev` / `feat/name-picker`）：[docs/branches.md](docs/branches.md)
- 电脑 GUI 与命令行工具：[../../tools/hueleaf-push/README.md](../../tools/hueleaf-push/README.md)

相册固件与电脑 GUI 在 `dev` 统一开发；`feat/name-picker` 保留为不含 MonoFast 的 1.2.0 基线。旧 GUI、TF 相册和 MonoFast 功能分支已由历史提交或标签保存，不再单独维护。

## 工作方式

- **保留出厂固件**：`factory`(0x10000) 仍是官方出厂 App，本固件只写 `ota_0`/`ota_1`。
- **开机选择**：插 TF 卡 → 本相册固件；**无 TF 卡 → 自动一次性重启进出厂固件**（行为与原厂一致，多约 1 秒开机时间）。
  机制：App 在 RTC 保留内存写一次性标志后软重启，自定义 bootloader 钩子（`bootloader_components/hl_boot_select`）读到标志即清除并跳到 factory。断电/下次复位又回到相册固件判断。
- 首次刷入/从 TF 升级后的第一次启动处于「待验证」状态：此时若无卡，屏幕显示「未检测到 TF 卡」并等待插卡（**不要按复位**，复位会触发回滚到出厂固件）。EPD 与 TF 初始化成功后才标记有效。
- Wi-Fi/蓝牙不启用。USB(USB-Serial-JTAG) 仅用于下面的分帧协议，日志进内存环形缓冲（`hueleaf-push --log` 读取）。

## 分区（4 MB）

| 分区 | 偏移 | 大小 | 内容 |
|---|---|---|---|
| bootloader | 0x2000 | – | 本仓库构建（含钩子，开启回滚） |
| nvs | 0x9000 | 24K | 与出厂共用（相册用独立命名空间 `hlgal`）|
| phy_init | 0xF000 | 4K | |
| factory | 0x10000 | 1472K | **出厂 App（0x151380 字节，约 92K 余量），偏移不变无需重刷** |
| otadata | 0x180000 | 8K | 指向 ota_0 |
| ota_0 | 0x190000 | 704K | 相册固件（约 374K）|
| ota_1 | 0x240000 | 704K | TF 升级目标 |
| image_cache | 0x300000 | 1M | 出厂固件的缓存分区，偏移不变 |

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

安装脚本与 `idf.py flash` / `app-flash` / `partition-table-flash` 在**写入任何内容之前**自动预检（`tools/hl_preflight.py`）：
1. 整片备份 `read_flash 0 0x400000` 到脚本旁（idf.py 时为 `backups/`）的 `hueleaf_backup_<MAC>_<时间>.bin`，校验大小为 4 MiB，失败则中止；同一 MAC 已有完整备份则复用。
2. 读取 0x8000 分区表；若已是 HueLeaf 布局则跳过第 3 步。
3. 按镜像头与段表计算 0x10000 处原厂固件的实际大小，超过新 factory 分区 0x170000 则中文提示并中止，不写任何内容。

跳过预检（不推荐）：`HUELEAF_SKIP_PREFLIGHT=1 idf.py flash`。直接用 esptool / Flash Download Tool 写入不会经过预检。

`idf.py flash` 可以用：它写 bootloader@0x2000、分区表@0x8000、otadata@0x180000、App@0x190000（ota_0），不碰 0x10000 的出厂固件，且写入前同样执行上面的预检；`idf.py app-flash` 只写 0x190000。只有 `erase-flash` 被拦截。
空片/已擦除芯片可改用整包：`esptool.py --chip esp32c5 write_flash 0x0 dist/hueleaf_full_0x0.bin`（含出厂 App，NVS 会被清空）。

## 3. TF 卡布局（FAT32）

```
/img/*.bin          每张 172800 字节的预抖动六色帧（hueleaf-push 生成）
/img/index.txt      可选，每行一个文件名，决定顺序；否则按文件名排序
/update/firmware.bin 可选，开机写入 ota 槽，成功后改名 firmware.bin.done（旧 .done 先删除；改名失败则删除 firmware.bin），失败改名 .failed；已处理过的镜像（大小+SHA-256 记在 NVS）不会重复写入，不会反复重启
```
帧格式（otp6-v1，与官方驱动一致）：列优先，逻辑 x 从 479 到 0，每列 360 字节覆盖 y=0..719，字节=(偶数行色号<<4)|奇数行色号；色号 0黑 1白 2黄 3红 5蓝 6绿。当前图片按**文件名**保存在 NVS，断电/换电池后恢复。

## 4. 按键

| 按键 | 动作 |
|---|---|
| BOOT 短按 | 下一张（末张后回到第一张）|
| 电源键短按（AXP2101 PEK 短按中断）| 上一张（循环）|
| BOOT 长按（约 1 秒，按住时触发）| 打开「选择图片」名称列表 |
| 电源键长按 | **不要用**：PMIC 硬件关机 |
| 复位 | 重启 |

刷新中的连续按键会累加，刷新结束后直接跳到最终目标。每次被接受的按键闪一下 AXP2101 CHGLED（蓝色充电灯，未验证）。底部状态条：电量%、n/N、RTC 时间（`--strip off` 关闭），只在显示图片时随整屏 6 色刷新一起绘制。

### 选择图片（名称列表，固件 1.2.0+）

只显示名称（无缩略图），黑白绘制；一页 14 行，打开时直接定位到当前图片所在页、光标在当前图片上。

| 在列表中 | 动作 |
|---|---|
| BOOT 短按 | 光标下移（到底回到第一项）|
| 电源键短按 | 下一页（最后一页后回到第一页）|
| BOOT 长按 | 选中：退出列表并以 6 色显示该图片（记入 NVS，断电后保持）|
| 60 秒无操作 | 自动退出，回到当前图片 |

- 连按会合并：最后一次按键后约 1.2 秒才刷新一次（连按 5 下 = 只刷新 1 次）。刷新期间的按键同样累加。
- 打开列表后若在列表画出来之前就长按确认当前图片，屏幕不刷新；列表已显示时退出总是整屏 6 色刷新回图片。
- 名称（包括中文）由电脑端工具在上传时渲染成 1-bit 小图存到 `img/.labels/`；用旧工具上传或手动拷贝的图片显示为文件名（仅英文数字）。
- 黑白快刷（固件 1.3.0+，未经实机验证）：列表与「无 TF 卡」「无图片」等所有非图片页面使用移植自 [FrameFilm](https://github.com/kirmisaki/FrameFilm)（GPL-3.0）的 MonoFast RAM 波形：从图片进入时先做一次全屏清理（约 3.3 秒，FrameFilm 实测），之后每次约 0.2 秒；每 20 次快刷插入一次 FrameFilm 的 mono_clear 清理。选中图片后恢复原厂寄存器（复位 + 原初始化 + PSR 0F 29）并用原厂六色波形整屏刷新。
- 关闭快刷：`idf.py menuconfig` → HueLeaf gallery → 取消 `HL_MONO_FAST`（或在 sdkconfig 设 `CONFIG_HL_MONO_FAST=n`），即回到 1.2.0 的整屏刷新行为；清理间隔为 `HL_MONO_FAST_CLEAN_EVERY`。
- 免编译关闭（固件 1.3.1+）：在 TF 卡根目录放 `hueleaf.cfg`，写一行 `mono_fast=0`，重启后全部使用原厂整屏刷新；`mono_fast=1` 重新开启。设置会保存到 NVS（1.3.2+，仅在变化时写入）：之后删掉文件或该行**不会**恢复快刷，必须写 `mono_fast=1`；从未设置过时按编译默认。NVS 中的值在开机最早阶段（任何画面之前）生效，所以「无 TF 卡」提示也遵循它。格式为 `key=value`，`#` 开头为注释，容忍空格与 Windows 换行。启动日志打印 `MonoFast: ON/OFF (...)`，来源为 `hueleaf.cfg`、`NVS, last hueleaf.cfg` 或 `Kconfig default`。
- 拍拍换图（`tap_next`，默认关）：`hueleaf.cfg` 写 `tap_next=1` 并重启后，**连拍两下**相框 = 下一张（同 BOOT 短按，CHGLED 闪一下表示收到）。防误触：拍之前需静止约 1.5 秒、两次触发至少隔 1.5 秒，走路/包晃动时不会换图。`tap_next=0` 关闭；和 `mono_fast` 一样保存到 NVS，删掉该行不会改变当前状态。启动日志打印 `TapNext: ON/OFF (...)`；被拒绝的拍击打印 `double tap ignored`。

## 5. hueleaf-push（无需路由器/App）

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

## 6. 恢复

1. 拔卡开机即为出厂固件（相册固件不影响）。
2. 刷坏/起不来：按住 BOOT 再插 USB（或按住 BOOT 按复位）进入 ROM 下载模式，写回备份：
   `esptool.py --chip esp32c5 write_flash 0 backup.bin`
   或官方出厂镜像：`esptool.py --chip esp32c5 erase_flash && esptool.py --chip esp32c5 write_flash 0x0 examples/3.7einkDvb/firmware/3.7einkDvb_factory_0x0.bin`
3. 待验证期间复位导致回到出厂固件：重新运行 `flash_install.sh`，或 `otatool.py -p PORT switch_ota_partition --slot 0`。

## `idf.py` 刷写保护（结构性，不只是文字提醒）

- `CMakeLists.txt` 在 `project()` 之后改写 `flash` / `app-flash` / `otadata-flash` 目标：应用偏移从 factory（0x10000）改为 ota_0（0x190000，从分区表读取），并把空白 `ota_data_initial.bin` 换成选中 ota_0 的 `otadata_ota0.bin`（构建时由 `tools/mk_otadata.py` 生成）。如果任何参数里仍含 0x10000，配置阶段直接 FATAL_ERROR。
- 因此 `idf.py flash` = bootloader@0x2000 + 分区表@0x8000 + otadata@0x180000 + 应用@0x190000；`idf.py app-flash` 只写 0x190000。可检查 `build/flash_args`、`build/flasher_args.json`。
- `idf_ext.py` 拦截 `idf.py erase-flash` / `erase_flash`（退出码 2）。确需整片擦除：`HUELEAF_ALLOW_ERASE=1 idf.py erase-flash`，之后必须用 `dist/hueleaf_full_0x0.bin` 或官方固件恢复 factory。
- 无法拦截的途径：直接运行 `esptool.py erase_flash` / `write_flash 0x10000 ...`、`ninja erase_flash`（CMake 目标）、Flash Download Tool 等。这些请勿使用。
