# 分支与归档标签（LeeSpo/devkit-hueleaf_base）

相册固件、电脑 GUI、USB 协议与诊断工具在 **`dev` 统一开发**。保留 `feat/name-picker` 作为不含 MonoFast 的名称选图基线，其余已包含在 `dev` 中的旧功能分支不再维护。

真机对比请两板都刷 **同一提交的 `dev`**；对照板用 `hueleaf.cfg` 写 `mono_fast=0`，不要用旧分支当对照。详见 [real_hw_test.md](real_hw_test.md)。

## 保留的分支

| 分支 | 基线 / tip | 用途 |
|---|---|---|
| `main` | `e1b0e09`（本次整理时） | 原厂 / 上游基线，暂不合入社区固件；仍为默认分支 |
| `dev` | **以 `origin/dev` tip 为准**（会前移） | 相册固件与电脑 GUI 的统一开发、真机测试入口 |
| `feat/name-picker` | `0732a6d`（本次整理时），固件 1.2.0 | 按需保留的名称选图基线，不含 MonoFast；本次不修改其提交 |

以上功能尚未合入 `main`。切到 `main` 看不到社区相册固件与电脑 GUI，请使用 `dev`。

```bash
git fetch origin --prune --tags
git switch dev
git rev-parse --short origin/dev
```

### `feat/name-picker`

- **内容：** 固件 1.2.0，BOOT 长按打开名称列表，支持中文名称、分页与超时退出；非图片 UI 仍走原厂六色整屏刷新。配套电脑工具已支持名称标签渲染与上传。
- **定位：** 保留用于单独查看名称选图实现；固定历史快照另见 `archive/name-picker-1.2.0`，不要把会前移的分支当作不可变版本。
- **真机：** 不用本分支做 MonoFast 对照，因为它也缺少后续安装预检等改动；对照请刷同一提交的 `dev` + `mono_fast=0`。

### `dev`

- **内容：** 包含 TF 相册、电脑 GUI、名称选图、MonoFast、`hueleaf.cfg` + NVS 设置、安装预检与整片备份、失败回退计数，以及分阶段时序日志和出厂 LUT 扫描工具。
- **时序日志：** `MonoFast timing: upload=… pon=… ref=… pof=… total=…`、`Stock timing: …`。
- **扫描工具：** `tools/scan_factory_lut.py` 与 [factory_lut_scan.md](factory_lut_scan.md)（库存固件中未找到 FrameFilm 波形表，波形应在控制器 OTP）。
- **版本定位：** 本次整理时仍报告固件 1.3.4，但代码与 `archive/mono-fast-1.3.4` 不同；测试记录必须同时写提交 SHA，不能只记版本号。
- **测试状态：** CI 通过不代表真机验收通过；真机结果按 [real_hw_test.md](real_hw_test.md) 记录。

## 固定标签

| 标签 | 提交 | 用途与状态 |
|---|---|---|
| `v1.1.0` | `875e20c` | 已有附注标签，保留不移动；GUI + 固件 1.1.0 的配套历史版本，不代表已完成真机验收 |
| `archive/name-picker-1.2.0` | `0732a6d` | 名称选图历史快照；引入 MonoFast 及其 GPL 许可变更前的节点，非真机验收发布版 |
| `archive/mono-fast-1.3.4` | `a1969a1` | MonoFast 1.3.4 候选快照；含运行时开关、NVS、安装预检与回退 WARN，不含后续分阶段计时日志，非真机验收发布版 |

归档标签使用附注记录用途，不作为正式稳定版发布，也不为电脑 GUI 单独建立版本体系。需要查看固定源码时：

```bash
git switch --detach archive/mono-fast-1.3.4
# 查看完毕后返回开发分支
git switch dev
```

标签保存的是源码定位，不是固件下载包。当前 CI 不监听标签推送，补标签不会自动生成 Actions 工件或 GitHub Release；需要持久下载的版本应另行归档配套产物，并记录提交 SHA。

## 已清理的功能分支

| 原分支 | 历史定位 |
|---|---|
| `feat/v1-tf-gallery` | `9bbead4`；全部历史保留在 `v1.1.0` 与 `dev` 中，不额外补 1.0.0 标签 |
| `feat/desktop-gui` | `v1.1.0`；后续 GUI 随 `dev` 中的固件与协议共同演进 |
| `feat/mono-fast-picker` | `archive/mono-fast-1.3.4`；后续开发在 `dev` 继续 |

这三条分支清理前均无 `dev` 之外的独有提交，删除分支不删除功能或提交历史。没有需要单独合并的 GUI 开发线。

后续小改动在 `dev` 推进，复杂改动可临时开功能分支，合入后删除；`feat/name-picker` 是本次明确保留的例外。正式发布须先完成相应验收，再建立配套版本标签与发布产物，不补齐开发过程中的每个中间版本标签。

## 推荐刷法（摘要）

1. 记录 `origin/dev` 的提交 SHA，从该提交对应的 Actions 工件或本地构建取 `dist/install/`，两板都跑 `flash_install`（写入前自动预检备份）。
2. 板 A：TF 根目录 `hueleaf.cfg` 写 `mono_fast=1`；板 B：写 `mono_fast=0`，两板均重启。设置会保存在 NVS，不能靠删除配置行恢复默认。
3. 按 [real_hw_test.md](real_hw_test.md) 勾选并填结果表。
