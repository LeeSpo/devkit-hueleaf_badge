# 真机对比测试清单（Sameat）

对照板：A = MonoFast 开；B = 对照（`mono_fast=0`）。目的：验证预检备份、快刷可用性、回退计数、时序日志，以及退回原厂刷新后的色彩/残影是否可接受。  
**仅刷 `dev` 分支工件**（当前 tip 含 1.4.0 连续传图 + 分阶段时序日志）。数字正常仍要肉眼看屏：毛刺、错位、残影。

## 前置

| 项 | 说明 |
|---|---|
| 板子 | 两块 HueLeaf Base（ESP32-C5 + EPD037A02），记为 A / B |
| USB | 串口 `PORT`：Linux/macOS 多为 `/dev/ttyACM0`，Windows 为 `COMx`；进下载模式可按住 BOOT 再插 USB（或按住 BOOT 按复位） |
| TF | FAT32；根目录可放 `hueleaf.cfg` |
| 工具 | `esptool`（或 `python -m esptool`）、本机构建或 Actions 工件中的 `dist/`（`dev` 分支） |
| 固件 | 刷写前确认工件来自 `dev`，不是 `feat/*` |

## 刷坏后恢复（先抄好再测）

整片备份回写（预检生成的 4 MiB 文件；路径按你机器改，示例为 flash_install 写在 `dist/install/` 旁）：

```bash
# 例：备份在 dist/install/ 下（idf.py flash 时多在 firmware/hueleaf-gallery/backups/）
esptool.py --chip esp32c5 -p PORT write_flash 0 /path/to/dist/install/hueleaf_backup_{MAC}_{time}.bin
```

恢复官方整包（会擦除整片，含 NVS）。**在仓库根目录**执行（相对路径才找得到该 bin）：

```bash
# 先 cd 到克隆下来的 devkit-hueleaf_base 根目录，再：
esptool.py --chip esp32c5 erase_flash && esptool.py --chip esp32c5 write_flash 0x0 examples/3.7einkDvb/firmware/3.7einkDvb_factory_0x0.bin
```

## 串口 / 看日志（任何「看日志」步骤之前先开好）

本固件 `CONFIG_ESP_CONSOLE_NONE=y`：**应用日志不走 UART 控制台**，进内存环形缓冲；日常请用 USB 协议读日志。`sdkconfig` 里 monitor 波特率仍是 **115200**（`CONFIG_ESPTOOLPY_MONITOR_BAUD` / `CONFIG_MONITOR_BAUD`）。

**推荐（读 ESP_LOG / MonoFast 行）：** 固件已起来、未占着下载模式时：

```bash
# 仓库根目录
python tools/hueleaf-push/hueleaf_push.py -p PORT --log
# 或：cd tools/hueleaf-push && python hueleaf_push.py -p PORT --log
```

**可选（IDF monitor / 终端；ROM 下载日志可见，应用 LOG 通常看不到）：**

```bash
# 在 firmware/hueleaf-gallery/ 且已 export ESP-IDF
idf.py -p PORT monitor
# 退出：Ctrl+]

picocom -b 115200 PORT
# 退出：Ctrl+A Ctrl+X
```

Windows：设备管理器看 `COMx`；可用 `idf.py -p COMx monitor`，或安装后用 PuTTY 串口 **115200 8N1**。读应用日志仍优先 `hueleaf_push.py -p COMx --log`。

`PORT` = `/dev/ttyACM0` 或 `COMx`；进下载模式可按住 BOOT 再插 USB。

## 清单（按顺序勾选）

### 1. 预检：整片备份 + 原厂大小检查

- [ ] 对板 A、板 B 各跑一次安装预检（`dist/install/flash_install.sh PORT` / `flash_install.bat COMx`，或 `python tools/hl_preflight.py check --port PORT --ptable … --backup-dir …`）
- **看日志：** `[1/3] 整片备份 …` → `备份完成：4194304 字节`；`[OK] 预检通过，可以写入。`；过大则 `[中止] …超过新 factory 分区上限…未写入任何内容。`
- **正常：** 脚本旁（或 `backups/`）出现约 4 MiB 的 `hueleaf_backup_{MAC}_{时间}.bin`；失败时 flash 未被改写。记下备份路径填表。

### 2. 两板刷 `dev`

- [ ] 预检通过后写入（`flash_install` 或 `idf.py flash`，均会再跑预检）
- **看日志：** 写入 bootloader@0x2000、分区表@0x8000、otadata@0x180000、App@0x190000；不要碰 0x10000 出厂 App。
- **正常：** 能进相册；用 `hueleaf_push.py -p PORT --log` 能看到 HueLeaf 启动相关行。

### 3. 配置对照

- [ ] **板 A：** TF 根目录不要 `mono_fast=0`（或显式 `mono_fast=1`）
- [ ] **板 B：** TF 根目录 `hueleaf.cfg` 含一行 `mono_fast=0`，然后重启
- **看日志：** 见下一步开机行。
- **正常：** A 快刷开、B 关；改 cfg 后重启才生效；删掉 cfg 行不会自动开回快刷（需 `mono_fast=1`）。

### 4. 开机 MonoFast 开关行

- [ ] 重启后确认
- **看日志：** `MonoFast: ON (...)` 或 `MonoFast: OFF (...)`；来源可为 `hueleaf.cfg` / `NVS, last hueleaf.cfg` / `Kconfig default`
- **正常：** A = ON，B = OFF（含「无 TF 卡」提示页也符合该开关）。

### 5. 各约 100 次菜单翻页

名称列表按键（详见 [../README.md](../README.md)「选择图片」）：**BOOT 长按**打开列表；列表内 **BOOT 短按**光标下移、**电源短按**下一页、**BOOT 长按**选中退出。

- [ ] 板 A、板 B 各在名称列表上翻约 100 次（翻页/换条均可）
- **看日志（A）：** `MonoFast timing: upload=…ms pon=…ms ref=…ms pof=…ms total=…ms`；偶发全清/清理时带 `(incl. full_clear)` / `(incl. mono_clear)`；若退回：`MonoFast fallback: … (fallbacks since boot: N)`
- **看日志（B）：** `Stock timing: upload=…ms pon=…ms ref=…ms pof=…ms total=…ms (upload incl. reset/init)`；不应出现 MonoFast timing / fallback
- **数字大致正常（A，约 2 MHz SPI）：** `upload≈690ms` 量级；`fallbacks since boot` 理想为 **0**；有 fallback 记原因字符串
- **还要看屏：** 翻页有无撕裂、错位、局部不刷新、异常残影（不能只看数字）。以后若提高 SPI 时钟，同样必须过目。

### 6. mono_clear 后再显彩图（色卡 / 残影）

- [ ] 板 A：等到一次带 `(incl. mono_clear)` 的 MonoFast timing（约每 20 帧快刷一次清理），再打开一张六色图
- [ ] 板 B：同样打开同图作对照
- **看日志：** A 进彩图前应完成 stock 恢复（可有 `left MonoFast: reset + boot init + OTP PSR`）；随后为 Stock timing / 原厂刷新路径
- **看屏：** 相对 B，色卡是否偏色、发灰、拖影、上一帧菜单残影；记差异（可接受 / 不可接受 + 简述）

### 7. 冷启动 / 偏冷环境再跑一轮

- [ ] 断电片刻冷启，或在偏冷环境再各翻若干次 + 再显同色图
- **看日志：** 同第 4–6 步；fallback 计数是否从 0 重新累加
- **看屏：** 冷态下是否更易 ghost、超时或 fallback；与室温对比记一笔

### 8. 连续传图与确认重启（1.4.0+）

- [ ] 记录源码 commit、未提交改动对应的源码 SHA256、安装包 SHA256、设备 `--info` / `--log`；确认固件 1.4.0、`upload_no_refresh=true`，保留一张当前图片作为对照。
- [ ] 新版 GUI 逐张取景并发送至少 3 张不同图片：每张图片和名称都保存成功，列表能立即看到，屏幕不自动刷新，USB 不断连，无需重新插拔；前后 `uptime_ms` 持续增加。
- [ ] 上传过程中「完成并重启」不可用；上传成功后点该按钮再取消，不出现 USB 断连或启动时长重置，仍可继续上传。
- [ ] 确认重启一次，界面等待重新连接并完成启动；`uptime_ms` 重置，记录 `reset_reason` 和日志。重启后仍显示原图，新图片可通过按键切换，名称与顺序正确。
- [ ] 同名覆盖当前图片：上传后屏幕不变，重启后显示该文件新内容；从空相册连续上传后，重启显示首张。
- [ ] 上传中拔 USB：界面明确报告失败，预览保留；插回后重新检测并重发可成功。记录断连前后的日志与复位原因，区分实际复位与连接故障。
- [ ] 旧固件搭配新 GUI 提示升级并禁用发送；新固件搭配命令行默认仍显示，`--no-show` 不刷新。

## 结果表（空白，测完手填）

| | 板 A（快刷开） | 板 B（对照 OFF） |
|---|---|---|
| MAC | | |
| 备份路径（4 MiB） | | |
| MonoFast ON/OFF（开机行） | | |
| ~100 次翻页后 fallbacks | | （期望无） |
| 样例 timing upload/pon/ref/pof/total | | |
| 色卡 / 残影 vs 对照 | | （基准） |
| 冷启 / 偏冷备注 | | |

## 相关文档

- 分支与归档标签：[branches.md](branches.md)
- 出厂固件 LUT 扫描结论：[factory_lut_scan.md](factory_lut_scan.md)
- 刷写与预检说明：上级 [README.md](../README.md)
