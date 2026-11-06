# HueLeaf Gallery 1.5.0

- 启用动态 FatFS 缓冲，按 SD 实际 512 字节扇区分配，避免为五个文件槽预留各 4 KiB 缓存；不改变卡格式、分区或图片格式。
- 状态缓存任务在 TF 挂载、首图和主要工作任务启动后才创建，降低挂载阶段资源压力；仍在 OTA 验证前检查创建结果。
- 构建时拒绝关闭动态 FatFS 缓冲的配置，避免静默重现内存回归。
- INFO 增加 heap_free、heap_minimum、heap_largest，启动日志记录 TF 挂载前和工作任务启动后的堆余量。

## 本版变化

- GUI 以「结束传图」释放整批会话，保持当前图片，不自动刷新或重启；图片与名称上传成功即已保存。
- 默认选图菜单提示改为「BOOT down · Hold select · PWR page」，把长按选择紧跟 BOOT 操作，避免误认为需要长按电源键。
- 首次发送开始整批保护，期间忽略 BOOT、电源短按与拍击，不排队；显示、状态栏、删除、排序等冲突操作回复忙碌。结束后恢复操作。
- 会话每 10 秒保活，120 秒无活动后释放；未完成的临时文件清理，已提交图片保留。旧客户端的单文件上传也有保护。
- INFO 改读短临界区保护的快照，后台空闲时更新存储缓存。刷屏时存储命令快速回复忙碌，屏幕时序和 SPI 锁范围未调整。
- 新增 INFO 运行状态/缓存新鲜度、0x10 会话命令和 -9 忙碌回复；旧图片格式、命令编号、出厂分区和 NVS 保留。
- Mac 连接同时设置 DTR/RTS 并独占串口，避免意外复位和多个程序抢读；GUI 不因有效错误回复关闭连接。USB 校时会等待启动完成后读取电脑当前时间。

## 验证与安装

本版需完成主机回归、ESP-IDF v5.5.4 构建、安装包来源及 SHA256 校验。设备安装前保留完整 4 MiB 备份，并按 real_hw_test.md 的 1.5.0 检查项验收；未完成真机验收前，不宣称新版固件已通过实机验证。

使用带 manifest、SHA256SUMS 和配套安装器的候选包，运行 install/flash_install.py 并指定 local/backups。安装前校验工件和安全 eFuse，不覆盖原厂 App，不擦除 NVS。请同时更新电脑端程序并刷新浏览器。

# HueLeaf Gallery 1.4.0

本版包含 TF 相册、名称选图、MonoFast 黑白列表快刷，以及电脑 GUI 连续传图。

## 本版变化

- GUI 上传每张图片后立即保存图片与名称，屏幕保持原样，可继续发送下一张。
- 全部发送后点击「完成并重启」，确认后重启；重新连接并等待首图刷新完成后恢复操作。
- 上传与重启互斥，重启命令只发送一次；旧固件缺少静默上传能力时明确提示升级。
- 打开串口前配置 DTR/RTS 空闲状态，减少连接时意外复位的风险。
- INFO 增加 upload_no_refresh、uptime_ms 和 reset_reason，便于检测能力和排查断连。
- 保留六色图片格式、名称标签、原厂固件回退和安装前完整备份。

## 安装与快刷

解压固件包，使用 ESP-IDF 5.5.4 的 Python 环境或 Python 3.12 + esptool 4.13.0：

```bash
python3 install/flash_install.py /dev/cu.usbmodemXXXX --backup-dir /absolute/path/to/backups
```

Windows 将串口替换为 COM5 等实际端口。安装器先验证文件、安全 eFuse、完整 4 MiB 备份和原厂镜像尺寸，再写入配套 bootloader、分区表、otadata 和 ota_0。正常安装保留出厂 App 与 NVS。

首次启动前插好 FAT32 TF 卡，移走卡上的 /update/firmware.bin。图片放在 /img/。TF 根目录 hueleaf.cfg 中写 mono_fast=1 并重启即可开启快刷；关闭写 mono_fast=0。快刷用于列表和提示页，六色照片使用原厂刷新。

电脑工具包解压后安装 requirements.txt，运行 python3 hueleaf_gui.py。建议固件与电脑工具一起升级。

## 验证范围

发布前运行 Python/GUI 与 C 主机回归测试，并使用 ESP-IDF 5.5.4 构建、校验分区与安装包 SHA256。

基线 dev b7a1ee9 已完成启动、两张 TF 图片识别、首图刷新及 MonoFast 配置读取验证。2026-11-06，用户确认本版已实机验证可用。后续回归可参考 real_hw_test.md。
