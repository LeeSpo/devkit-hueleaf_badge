# HueLeaf 彩叶 TF 相册固件 v1（社区固件，非官方）

> 未在真机上验证。请先完成「备份」再刷写。代码 Apache-2.0；硬件资料仍为 CC BY-NC-SA。

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
```
**不要用 `idf.py flash`**（它会把 App 写到 0x10000 覆盖出厂固件）。
空片/已擦除芯片可改用整包：`esptool.py --chip esp32c5 write_flash 0x0 dist/hueleaf_full_0x0.bin`（含出厂 App，NVS 会被清空）。

## 3. TF 卡布局（FAT32）

```
/img/*.bin          每张 172800 字节的预抖动六色帧（hueleaf-push 生成）
/img/index.txt      可选，每行一个文件名，决定顺序；否则按文件名排序
/update/firmware.bin 可选，开机写入 ota 槽，成功后改名 firmware.bin.done，失败改名 .failed
```
帧格式（otp6-v1，与官方驱动一致）：列优先，逻辑 x 从 479 到 0，每列 360 字节覆盖 y=0..719，字节=(偶数行色号<<4)|奇数行色号；色号 0黑 1白 2黄 3红 5蓝 6绿。当前图片按**文件名**保存在 NVS，断电/换电池后恢复。

## 4. 按键

| 按键 | 动作 |
|---|---|
| BOOT 短按 | 下一张（末张后回到第一张）|
| 电源键短按（AXP2101 PEK 短按中断）| 上一张（循环）|
| 长按 | v1 保留不分配；电源键长按仍为 PMIC 硬件关机 |
| 复位 | 重启 |

刷新中的连续按键会累加，刷新结束后直接跳到最终目标。每次按键闪一下 AXP2101 CHGLED（蓝色充电灯，未验证）。底部状态条：电量%、n/N、RTC 时间（`--strip off` 关闭）。

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
