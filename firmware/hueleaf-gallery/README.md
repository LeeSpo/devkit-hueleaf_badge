# HueLeaf 彩叶 TF 相册固件

## 文档

- **使用说明**（备份、刷写、放图、按键、`hueleaf.cfg`、恢复）：[根目录 README](../../README.md)
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
- **1.5.0 连续传图**：USB 图片上传完成只更新图库列表，不自动刷新或重启。新版 GUI 建立整批上传会话，期间忽略按键和拍击；点「结束传图」后恢复操作，画面保持原样。每张上传成功即已落盘，无需重启保存。
- **刷屏时仍能查询状态**：`INFO` 只读快照，后台空闲时检查 TF 和更新容量。刷屏期间存储命令立即回复忙碌，屏幕原有 SPI 时序保持不变。
- `INFO` 新增 `state`、`upload_session_supported`、`upload_session_active`、`storage_cache_valid`、`storage_cache_age_ms`；原有 `upload_no_refresh`、`uptime_ms`、`reset_reason` 和命令编号保留。新会话命令为 `0x10`，忙碌错误码 `-9`。
- 上传会话每 10 秒保活，最后一次活动后 120 秒自动释放；断连后只清理未提交临时文件，保留成功上传的图片。旧客户端每个文件传输期间也受到保护。

## 定时深睡轮播

可选轮播功能由 `slideshow.c` 管理，`slideshow_policy.c` 的纯 C 策略用主机测试验证。默认关闭，配置、操作窗口、USB 暂停与使用限制见[使用说明](../../README.md#定时深睡轮播)，协议见[电脑工具说明](../../tools/hueleaf-push/README.md#定时轮播协议)。

只有有效定时唤醒及匹配的 RTC 保留标记才会直接选择下一张；复位/看门狗/冷启动不会复用标记跳图。保留数据独立于 bootloader 的原厂启动标记。可选 `tap_wake` 使用 MSA321 正常 125 Hz 双击检测、GPIO4 低电平 EXT1 唤醒；与 `tap_next` 独立。只有保留标记、配置及唤醒 GPIO 均匹配才直接恢复原图位置，不刷屏、不推进，也不重写 NVS 位置；就绪后重新开始完整间隔及 60 秒窗口。原图已从卡上消失时回退到正常初始渲染。OTA 待验证始终执行完整显示验证。启动检查和 OTA 验证完成前不启动休眠任务，屏幕刷新成功后才保存当前位置。

休眠通过图库锁和运行状态原子准入，排除待处理换图、列表、上传、写卡、协议应答及后台 I/O。休眠准备卸载 TF、将屏幕深睡并保持 SPI/片选/复位 GPIO；错误或临近休眠时检测到电脑接入则撤销准备、恢复外设。运行中取消休眠只解除 GPIO hold，不改变 SPI 片选路由；启用的 IMU EXT1 也会撤销并恢复数字 GPIO。IMU 中断已置位时推迟休眠，初始化/唤醒配置失败时保持唤醒。没有修改 CPU 频率、PMIC 电源轨、充电、原厂固件、分区或 NVS 其他命名空间。

测试包含策略、保留标记、配置 I/O 故障、准备失败恢复、图库推进、互斥与 GUI 状态；真机验证步骤见 [docs/real_hw_test.md](docs/real_hw_test.md#定时深睡轮播验收)。

## 分区（4 MB）

| 分区        | 偏移     | 大小  | 内容                                                               |
| ----------- | -------- | ----- | ------------------------------------------------------------------ |
| bootloader  | 0x2000   | –    | 本仓库构建（含钩子，开启回滚）                                     |
| nvs         | 0x9000   | 24K   | 与出厂共用（相册用独立命名空间 `hlgal`）                         |
| phy_init    | 0xF000   | 4K    |                                                                    |
| factory     | 0x10000  | 1472K | **出厂 App（0x151380 字节，约 92K 余量），偏移不变无需重刷** |
| otadata     | 0x180000 | 8K    | 指向 ota_0                                                         |
| ota_0       | 0x190000 | 704K  | 相册固件（约 374K）                                                |
| ota_1       | 0x240000 | 704K  | TF 升级目标                                                        |
| image_cache | 0x300000 | 1M    | 出厂固件的缓存分区，偏移不变                                       |

## 刷写预检

安装脚本与 `idf.py flash` / `app-flash` / `partition-table-flash` 在**写入任何内容之前**自动预检（`tools/hl_preflight.py`）：

1. 整片备份 `read_flash 0 0x400000` 到脚本旁（idf.py 时为 `backups/`）的 `hueleaf_backup_<MAC>_<时间>.bin`，校验大小为 4 MiB，失败则中止；同一 MAC 已有完整备份则复用。
2. 读取 0x8000 分区表；若已是 HueLeaf 布局则跳过第 3 步。
3. 按镜像头与段表计算 0x10000 处原厂固件的实际大小，超过新 factory 分区 0x170000 则中文提示并中止，不写任何内容。

跳过预检（不推荐）：`HUELEAF_SKIP_PREFLIGHT=1 idf.py flash`。直接用 esptool / Flash Download Tool 写入不会经过预检。

`idf.py flash` 可以用：它写 bootloader@0x2000、分区表@0x8000、otadata@0x180000、App@0x190000（ota_0），不碰 0x10000 的出厂固件，且写入前同样执行上面的预检；`idf.py app-flash` 只写 0x190000。只有 `erase-flash` 被拦截。
空片/已擦除芯片可改用整包：`esptool.py --chip esp32c5 write_flash 0x0 dist/recovery/hueleaf_full_0x0.bin`（含出厂 App，NVS 会被清空）。

## 帧格式

帧格式（otp6-v1，与官方驱动一致）：列优先，逻辑 x 从 479 到 0，每列 360 字节覆盖 y=0..719，字节=(偶数行色号<<4)|奇数行色号；色号 0黑 1白 2黄 3红 5蓝 6绿。

## `idf.py` 刷写保护（结构性，不只是文字提醒）

- `CMakeLists.txt` 在 `project()` 之后改写 `flash` / `app-flash` / `otadata-flash` 目标：应用偏移从 factory（0x10000）改为 ota_0（0x190000，从分区表读取），并把空白 `ota_data_initial.bin` 换成选中 ota_0 的 `otadata_ota0.bin`（构建时由 `tools/mk_otadata.py` 生成）。如果任何参数里仍含 0x10000，配置阶段直接 FATAL_ERROR。
- 因此 `idf.py flash` = bootloader@0x2000 + 分区表@0x8000 + otadata@0x180000 + 应用@0x190000；`idf.py app-flash` 只写 0x190000。可检查 `build/flash_args`、`build/flasher_args.json`。
- `idf_ext.py` 拦截 `idf.py erase-flash` / `erase_flash`（退出码 2）。确需整片擦除：`HUELEAF_ALLOW_ERASE=1 idf.py erase-flash`，之后必须用 `dist/recovery/hueleaf_full_0x0.bin` 或官方固件恢复 factory。
- 无法拦截的途径：直接运行 `esptool.py erase_flash` / `write_flash 0x10000 ...`、`ninja erase_flash`（CMake 目标）、Flash Download Tool 等。这些请勿使用。

安装包带 `manifest.json` 与 `SHA256SUMS`，`flash_install.py` 写入前校验文件哈希并做只读 eFuse 安全检查；启动、存储与安装加固的细节见 [docs/preflash-fixes.md](docs/preflash-fixes.md)。
