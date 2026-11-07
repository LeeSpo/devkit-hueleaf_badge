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

### 8. 状态响应与整批传图（1.5.0）

- [ ] 使用包含内存修复的 1.5.0 构建；确认 TF 成功挂载，启动日志无 `mount_to_vfs failed (0x101)`，记录挂载前/工作任务启动后堆余量及 INFO 的 `heap_free`、`heap_minimum`、`heap_largest`。
- [ ] 记录源码 commit、未提交源码 SHA256、安装包 SHA256 和完整 4 MiB 备份；确认设备 `fw=1.5.0`、`upload_session_supported=true`，记录测试前 `uptime_ms`。
- [ ] 主动显示当前同一张图片。在全刷期间反复请求 INFO、PING、LOG，正常 USB 下每次应答 <500 ms；INFO 显示 refreshing，状态查询不等待约 30 秒的 SPI 刷屏。存储命令立即返回 -9，不复位、不排队。
- [ ] 新版 GUI 连续发送至少三张不同图片。每张图片和名称立即保存，图片之间保持会话；BOOT 短/长按、电源短按和拍击都不刷新、不补执行。uptime 持续增加。
- [ ] 传输过程中结束按钮不可用；图片之间显示、删除、排序、状态栏和端口切换不可用。继续取景和转换正常。
- [ ] 点「结束传图」后当前画面、图片序号及 uptime 保持；USB 不重连；再次结束安全。随后按键和主动显示恢复。
- [ ] 同名覆盖当前图片后结束：屏幕不变，主动点「显示」才绘制新内容。空相册上传后结束不自动重画，下一次电源键短按显示首张。
- [ ] 会话期间关闭后台或拔 USB，最后一次活动后最多 120 秒恢复按键，过期临时文件不替换正式图片；已保存文件保留。重连后可重新建立会话并重发。
- [ ] 上传失败、TF 错误、文件写入/同步/关闭失败均不得误报保存成功；文件保护正确释放，显式批次可重试或结束。
- [ ] 旧 1.4.x 固件与新 GUI 提示缺少整批保护，但仍支持静默上传和结束；新固件与旧客户端每个文件传输期间屏蔽按键，原协议保持兼容。
- [ ] 重启一次检查 NVS、按文件名恢复、空卡/无卡和 OTA 待验证行为保持原有保护；无需通过重启结束日常传图。

## 定时深睡轮播验收

此功能的真机电流、唤醒间隔和恢复连接尚需实测。填写测试提交 SHA、构建来源指纹、板号、电池型号/容量、USB 供电状态；先保留完整 Flash 备份。

- [ ] 用至少三张图片，在 GUI 保存开启、间隔 2 分钟；重连读取一致，重启后仍开启。卡上两个轮播配置项同步，其他配置和注释保留；关闭功能后恢复常醒。
- [ ] 电脑连接时至少等待两个间隔：不轮播、不深睡，上传/设置可用。拔掉电脑后重新计时，普通充电器或充电宝不暂停轮播。
- [ ] 电池独立运行连续至少三个周期：按顺序循环，每次仅刷下一张一次，不先刷旧图，不补刷错过周期。记录每次刷新结束、下次启动刷屏的时间及误差。
- [ ] 定时唤醒换图结束后立即再次深睡。按复位或重新开机后不跳图，设备就绪起留 60 秒；被接受的按键操作延长窗口。深睡期间插 USB、短按翻页键和拍击不会唤醒；复位后能重新连接 GUI。
- [ ] 单图连续两个周期不重复刷屏；空图库常醒。排序变更、删除当前图、同名图片覆盖后，下一次选择符合现有图库顺序，失败不保存新的位置。
- [ ] 开启选图列表、连续上传两张图、写卡和 OTA 期间不深睡；会话结束或过期并完成清理后才允许深睡。OTA 待验证启动仍完成原有检查。
- [ ] 在固件日志中核对定时唤醒原因、间隔和前一张名称；用调试注入验证屏幕睡眠、卸载卡或 GPIO 准备失败时保持唤醒，标记不会在普通复位后误跳图。
- [ ] 在电池端测量关闭轮播时的待机电流、深睡平台电流、启动与刷屏阶段电流；确认 PMIC、RTC、绿灯和外设残余耗电，不用绿灯是否熄灭代替电流测量。记录实测数据，不从芯片规格推算整板续航。

| 提交 SHA / 来源指纹 | 板号 / 电池 | 设定间隔 / 实测间隔 | 原待机电流 | 深睡电流 | 三周期 / USB恢复结果 |
|---|---|---|---|---|---|
| 待测 | 待测 | 待测 | 待测 | 待测 | 待测 |

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
