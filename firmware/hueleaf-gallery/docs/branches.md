# 分支与归档标签（LeeSpo/devkit-hueleaf_badge）

固件、Android App、电脑 GUI、协议与诊断工具采用 **双主干模型**：`main` 为封版稳定发布分支，`dev` 为日常集成开发分支。**后续开发统一进入 v2，仅维护蓝牙遥控版；传统保留原厂相册版在 v1.5.0 冻结。**

## 版本政策

- **v1 传统保留原厂版**：冻结归档于 `archive/gallery-stock-1.5.0`（`45f6e8d`）。当前主线不再构建或增加传统版功能，需复现或维护可从该归档标签检出。
- **v2 开发主线：2.0.0 起**：后续固件提交和发布统一使用 `2.x` 版本，全面支持蓝牙遥控架构。
- `remote-v1` 是既有分区与协议标识，`hueleaf_remote_v1` 为 OTA 校验使用的应用标识；它们保持稳定以兼容现有设备的 OTA 升级与 Android App 连接。
- 电脑安装工具继续保持对历史分区布局的识别、整片备份与平滑迁移支持。
- Android 遥控 APK 保持独立版本与 Release 发布（使用 `android-v*` 标签）。

## 活跃分支

| 分支 | 用途 | 说明 |
|---|---|---|
| `main` | 封版稳定发布分支（默认分支） | 包含已验证的完整相册固件、电脑 GUI 与安装脚本 |
| `dev` | 日常开发与集成分支 | 最新特性与真机联调主线，稳定后并入 `main` |
| `upstream-factory` | 原厂 / 上游基线（`e1b0e09`） | 官方原始出厂工程与 demo 备份，用于比对与上游追踪 |

```bash
git fetch origin --prune --tags
git switch dev
```

### `dev`
- **内容：** v2 蓝牙遥控固件、Android App、USB 图库管理、名称选图、MonoFast、整批传图、深睡轮播及诊断工具。
- **版本定位：** 当前处于 2.0.0 开发主线，仅构建 `remote-v1` 布局。
- **测试状态：** 日常开发在 `dev` 推进，真机测试验收通过后打标签并合入 `main`。

## 固定标签

| 标签 | 提交 | 用途与状态 |
|---|---|---|
| `v1.1.0` | `875e20c` | GUI + 固件 1.1.0 配套历史版本 |
| `archive/name-picker-1.2.0` | `0732a6d` | 名称选图历史快照（原 `feat/name-picker`，不含 MonoFast） |
| `archive/mono-fast-1.3.4` | `a1969a1` | MonoFast 1.3.4 快照 |
| `v1.4.0` | `30c102d` | 1.4.0 连续传图稳定发布版 |
| `archive/gallery-stock-1.5.0` | `45f6e8d` | v1.5.0 传统版最终源码基线；非正式 Release |

需要查看固定历史版本源码时：
```bash
git switch --detach archive/gallery-stock-1.5.0
# 在归档源码上复现传统版（先准备 ESP-IDF 环境）：
# HUELEAF_REMOTE=0 ./tools/hueleaf-workspace/hueleaf.sh build
# 查看完毕后切回开发分支
git switch dev
```

## 已清理的分支

- `feat/name-picker`：已由 `archive/name-picker-1.2.0` 标签永久固定并删除分支；
- `feat/release-1.4.0`：已合并入 `dev` 并由 `v1.4.0` 标签固定；
- `fix/preflash-*`：历史预刷修复测试分支，已分别由 `v1.1.0-fix1-rc1`、`v1.2.0-fix1-rc1`、`v1.3.4-fix1-rc1` 标签固定并删除分支。

## 推荐刷法（摘要）

1. 记录 `origin/dev` 的提交 SHA，从该提交对应的 Actions 工件或本地构建取 `dist/install/`，两板都跑 `flash_install`（写入前自动预检备份）。
2. 板 A：TF 根目录 `hueleaf.cfg` 写 `mono_fast=1`；板 B：写 `mono_fast=0`，两板均重启。设置会保存在 NVS，不能靠删除配置行恢复默认。
3. 按 [real_hw_test.md](real_hw_test.md) 勾选并填结果表。
