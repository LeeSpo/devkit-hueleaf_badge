# HueLeaf 历史功能修复候选：试机与安装

这是作者自制相册固件的 `fix1` 修复候选，不是历史原包，也未完成实机验收。三个功能层次分别保留：

| 版本 | 功能 | 对应分支 |
|---|---|---|
| 1.1.0-fix1 | TF 相册、USB/GUI 传图及排序，不含名称选图 | `fix/preflash-1.1.0` |
| 1.2.0-fix1 | 上述功能 + 名称选图，使用原厂刷新 | `fix/preflash-1.2.0` |
| 1.3.4-fix1 | 上述功能 + 可显式开启的实验性 MonoFast | `fix/preflash-1.3.4` |

历史标签保留不动，当前 dev 不属于这次修复。查看包内 manifest.json 的实际源码提交和 SHA256SUMS，不能只凭屏幕上的版本号识别构建。

## 修复内容

- 开机同步绘制并检查首张图或空图库提示；修复 1.1.0 首图跳过和后续版空图库忙循环。
- 在 USB/升级器使用前创建互斥锁；启动中只开放 PING、INFO、LOG、REBOOT、BOOT_STOCK，其余返回 `-8 device not ready`。
- 首帧成功及图库/按键任务创建成功后，才确认待验证 OTA 镜像；初始失败保留回滚/原厂回退路径。
- TF 升级处理实际读取错误、尺寸变化、错误芯片、OTA 校验和重复文件；在可防止重复处理前不选择新启动分区。
- 图片、标签、排序覆盖使用 `.bak` 恢复策略；写入结果包含 flush/sync/close 检查，启动先恢复已知目录中的备份。不承诺抵御 FAT 本身损坏或存储硬件失效。
- 保留有效 RTC 时间，校验日期、闰年及星期；错误时尽力清除 STOP，避免每次开机改成编译时间。
- TF 检测到媒体错误后锁定为错误状态，避免静默继续使用旧 mounted 状态；更换卡需停机并重新启动，不支持热拔插自动恢复。
- 安装前只读查询安全 eFuse、每次新建完整 4 MiB 备份和 SHA256、检查实机原厂 App 尺寸；正常安装不写原厂 App/NVS。

## 从源码重建

在共享 ESP-IDF 环境已加载的终端里，进入对应 worktree 的 `firmware/hueleaf-gallery/`：

```bash
python tools/build_candidate.py --build-dir /absolute/build/path
./tools/package.sh /absolute/build/path /absolute/new-output/path
```

构建器记录源码指纹、实际 sdkconfig 和产物 SHA256；打包会拒绝过期、其它 checkout 或被修改的构建，并拒绝往已有输出目录混入旧文件。普通 `idf.py build` 仍可做开发编译，但正式候选打包前要运行该构建器。

## 包结构

```text
install/                     日常安装与逐版试机用
  flash_install.py           跨平台安装器
  flash_install.sh/.bat      启动入口
  hl_preflight.py            备份和布局检查
  bootloader.bin
  partition-table.bin
  otadata_ota0.bin
  gallery_ota0.bin
recovery/                    仅空白芯片/恢复用
  hueleaf_full_0x0.bin        完整镜像，会覆盖写入范围中的 NVS
  stock_app.bin
manifest.json
SHA256SUMS
```

## 安装和换版本

需要 Python 3.12 与包：`python -m pip install esptool==4.13.0`。使用支持 ESP32-C5 的工具版本；打包验证使用 ESP-IDF 5.5.4。

macOS/Linux：

```bash
python3 install/flash_install.py /dev/cu.usbmodemXXXX --backup-dir /path/to/backups
```

Windows：

```powershell
python install/flash_install.py COM5 --backup-dir C:\HueLeaf\backups
```

安装器检查完毕后才写 `0x2000` bootloader、`0x8000` 分区表、`0x180000` otadata、`0x190000` 相册。不要用 `erase_flash` 或把 `gallery_ota0.bin` 写到 `0x10000`。

三个修复版共用相同分区和图片格式，可依次使用各自的安装器切换，不需要格式化 TF 卡或擦除整片 Flash。每次安装都保留新的备份，原有图片和设置保留；先把卡上 `/update/firmware.bin` 移出，避免换版后自动执行残留升级。

建议试验次序：**1.1.0-fix1 → 1.2.0-fix1 → 1.3.4-fix1（快刷关）→ 最后再单独验证快刷**。每版至少检查有图启动、空图库、USB 上传与覆盖、排序、前后翻图、复位恢复；1.2+ 再检查名称选图。空图库可以正常启动并接收上传。

初次安装后的待验证启动如果没有 TF 卡，会显示提示并继续等待插卡；这是保留的诊断行为，不设自动超时。需要退出等待时复位可触发 bootloader 回滚；不要把“未确认”当成升级完成。回滚 API 若没有可用镜像会失败，届时可用 USB 日志诊断或安装器重刷。

### TF 升级中断

升级用 `/update/firmware.bin`。已处理文件会被改名 `.done` / `.failed`（或无法改名时删除），同时记录尺寸与 SHA256 以防重复刷新。若在“已准备、尚未完成启动选择”之间断电，日志会报告 interrupted/prepared，保留原文件但不自动反复刷写。使用 USB 安装器恢复最直接；不要删除整个 NVS 来强行重试。显式再次上传同内容也不自动清除防重复记录。

## MonoFast 默认关闭

仅 1.3.4-fix1 含快刷支持。即使旧固件在 NVS 里保存过 ON，修复版也不会自动启用；每次开机必须能完整读到当前 TF 根目录 `hueleaf.cfg` 的明确值：

```ini
mono_fast=1
```

删除该行、设置 `mono_fast=0`、文件缺失/读取失败都会关闭快刷。无卡提示也先使用原厂刷新。照片始终使用原厂六色刷新。

波形表与固定上游版本逐字节一致，不等于已验证本板屏幕批次、温度范围或寿命。缺少电气/实机证据时，不能承诺不会产生异常残影或长期损伤；软件测试并不能替代这些验证。

## 已知边界

- 这些修复版没有新增 MCU 自动深睡/唤醒设计，不能承诺挂包长续航。刷新完成后手动关机可利用墨水屏断电保图特性。
- 电池/电源轨沿用原厂基础代码，没有改电压或充电策略；会继承原厂共享 NVS 里的电池容量与充电档位。
- 不能靠备份恢复硬件损伤或 eFuse；安装器只读检查并保守拒绝受保护/无法识别的设备，不执行烧写 eFuse。
- `recovery/` 的完整镜像会覆盖设置，不能替代正常安装器。
- 主机测试覆盖实际 C 模块控制流和错误注入，不代表已经通过屏幕、电池、USB 接口的实机验收。
