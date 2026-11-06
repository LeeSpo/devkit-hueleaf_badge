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
