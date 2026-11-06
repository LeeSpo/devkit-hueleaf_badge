# 分支与归档标签（LeeSpo/devkit-hueleaf_badge）

相册固件、电脑 GUI、USB 协议与诊断工具采用 **双主干模型**：`main` 为封版稳定发布分支，`dev` 为日常集成开发分支。历史功能基准已打固定标签归档，其余中间分支已清理完毕。

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
- **内容：** 包含 TF 相册、电脑 GUI、名称选图、MonoFast 极速快刷、连续/整批传图会话保护、内存优化与出厂 LUT 诊断工具。
- **版本定位：** 当前处于 1.5.0 开发主线。
- **测试状态：** 日常开发在 `dev` 推进，真机测试验收通过后打标签并合入 `main`。

## 固定标签

| 标签 | 提交 | 用途与状态 |
|---|---|---|
| `v1.1.0` | `875e20c` | GUI + 固件 1.1.0 配套历史版本 |
| `archive/name-picker-1.2.0` | `0732a6d` | 名称选图历史快照（原 `feat/name-picker`，不含 MonoFast） |
| `archive/mono-fast-1.3.4` | `a1969a1` | MonoFast 1.3.4 快照 |
| `v1.4.0` | `30c102d` | 1.4.0 连续传图稳定发布版 |

需要查看固定历史版本源码时：
```bash
git switch --detach archive/name-picker-1.2.0
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
