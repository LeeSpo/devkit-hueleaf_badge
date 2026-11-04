# 分支说明（LeeSpo/devkit-hueleaf_base）

本 fork 上与相册固件相关的三条功能线。真机对比请两板都刷 **`dev`**；对照板用 `hueleaf.cfg` 写 `mono_fast=0`，不要用旧分支当对照。详见 [real_hw_test.md](real_hw_test.md)。

| 分支 | tip | 固件 | 状态 |
|---|---|---|---|
| `feat/name-picker` | `0732a6d`（截至 e83acb5 时核对） | 1.2.0 | 未合入 `main` |
| `feat/mono-fast-picker` | `a1969a1`（截至 e83acb5 时核对） | 1.3.4 | 稳定候选，未合入 `main` |
| `dev` | **以 `origin/dev` tip 为准**（会前移） | 同 1.3.4 体验 + 实验日志/工具 | 真机测试用 tip |

查看当前 `dev` tip：`git fetch origin && git rev-parse --short origin/dev`。  
`main` 仍接近上游 osptek 库存（约 `e1b0e09`）。仓库另有 tag `v1.1.0`；与上表关系不是重点。

## `feat/name-picker` @ `0732a6d`

- **内容：** 固件 **1.2.0**，名称列表选图（picker），非图片 UI 仍走原厂六色整屏刷新。
- **定位：** 选图交互的基线；**未合入 `main`**。
- **真机：** 不要用本分支做 MonoFast 对照；对照请刷 `dev` + `mono_fast=0`。

## `feat/mono-fast-picker` @ `a1969a1`

- **内容：** 固件 **1.3.4** 稳定候选：所有非图片屏 MonoFast；`hueleaf.cfg` + NVS；安装预检（整片备份 + 原厂大小）；失败时 `MonoFast fallback: …` WARN；MonoFast 相关为 GPL-3.0 衍生。
- **定位：** 功能完整的快刷候选线；**未合入 `main`**。`feat/*` 请勿在本任务中改动。
- **相对 `dev`：** `dev` 在此之上加了分阶段时序日志与出厂 LUT 扫描文档/工具，交互与开关语义与 1.3.4 相同。

## `dev`（tip 以 `origin/dev` 为准）

- **内容：** 叠在 1.3.4 上的实验 tip：`MonoFast timing: upload=… pon=… ref=… pof=… total=…`、`Stock timing: …`；`tools/scan_factory_lut.py` 与 [factory_lut_scan.md](factory_lut_scan.md)（库存固件中未找到 FrameFilm 波形表，波形应在控制器 OTP）。
- **定位：** **推荐两板都刷本分支**做对比；板 A 快刷开，板 B `mono_fast=0`。
- **注意：** 不要死记旧 sha；勾选清单前用 `git rev-parse --short origin/dev` 并对齐 Actions/`dist` 工件。

## 推荐刷法（摘要）

1. 从 `dev` 取 `dist/install/`，两板都跑 `flash_install`（写入前自动预检备份）。
2. 板 A：`mono_fast=1` 或不写关闭项；板 B：`hueleaf.cfg` → `mono_fast=0` 后重启。
3. 按 [real_hw_test.md](real_hw_test.md) 勾选并填结果表。
