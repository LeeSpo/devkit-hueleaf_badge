# remote-v1 MonoFast 软件验证记录

验证日期：2026-11-10。分支 remote-control，基线提交 `01c3bfb`，修订位于未提交工作树；
具体源码 SHA256、base commit 和 SDK 配置由安装包 build_provenance.json 记录。
安装包 manifest 的 hardware_validated 为 false。

## 已执行

- 基线：Python 246 passed、2 skipped；C 主机检查通过；两种旧构建源码指纹检查通过。
- 修订版：ESP-IDF v5.5.4 / ESP32-C5，remote-v1 与传统保留原厂配置均构建通过，无编译警告。
  两种构建均为 `CONFIG_HL_MONO_FAST=y`，remote-v1 保留 BLE 与省电设置。
- Python：252 passed，2 skipped。跳过项依赖源码目录 build/ 的旧预检镜像；实际产物在工作区
  local/builds/remote-control/monofast/，其布局、provenance 和包内哈希另行检查。
- C 主机检查通过；新增真实编码器/屏幕驱动检查以 AddressSanitizer 和 UndefinedBehaviorSanitizer 运行通过。
- 紧凑编码：45 帧及所有 full_clear/mono_clear 传输平面逐字节匹配 FrameFilm 参考实现；
  覆盖全黑、全白、随机图案、非对称边缘像素、只读当前帧与前后保护字节。
- 故障恢复：SPI 部分写入、REF 不拉 BUSY、POF 不释放 BUSY，均使旧基线失效；下一帧清理恢复。
- 实际屏幕驱动：快刷 → 共享 SPI 读卡彩色 OTP → 快刷；运行时禁用和编译禁用的流式 OTP；
  部分快刷失败后的 OTP 回退、回退失败错误传播、深睡/恢复后的首次清理，均通过。
- 实际图库：传统与 remote-v1 两种路径的整文件测试均通过 ASan/UBSan；验证选择器与
  外部 UI 缓冲均进入 MonoFast 接口，显示失败释放准入，彩色图片继续走流式读取。
- USB：真实 RX/响应地址重叠、最大负载、重复上传 ACK、长列表、二进制响应及 CRC 通过；
  HLP1 wire 和最大负载保持兼容，升级场景继续通过。
- 原遥控策略、图库 300 图分页、迁移备份/擦写边界等回归继续通过。
- 新候选包包含完整文档、源码指纹与文件 SHA256；无设备刷写。

## 内存预算

| 项目 | 原 remote-v1 | 保留 MonoFast 后 |
|---|---:|---:|
| UI 当前帧 | 43,200 B | 43,200 B |
| MonoFast 上一帧 | 0 | 43,200 B |
| MonoFast 编码批次 | 未链接 | 1,440 B |
| USB RX/TX/文本常驻区合计 | 24,608 B | 8,208 B |
| TF 升级缓冲 | 4,096 B | 1,024 B |
| HP SRAM 已链接 | 229,858 B | 255,162 B |
| HP SRAM 链接后余量 | 91,070 B | 65,766 B |

波形、驱动代码和少量元数据也影响 SRAM。链接余量不是运行时堆保证；尚未获得实际
BLE 动态分配、所有任务栈与初始化临时分配数据，不能宣称已通过内存压力真机验收。

## 待真机

2026-11-10 枚举本机串口仅见 debug-console 和 Bluetooth-Incoming-Port，未发现 HueLeaf/ESP32 USB 设备。
未进行刷写、无线连接、目视屏幕或电流测量。按 real_hw_test.md 的 remote-v1 专项表执行：

- BLE 保持连接期间连续 45 次以上菜单快刷，核对周期清理、方向、残影和原有 MonoFast 时序。
- 快刷/彩色切换、上传与 UI 互斥、睡醒 100 次；记录 heap 最低值、最大块、任务栈最低余量。
- BLE 配对/指示确认/断连时序、共享 SPI 流式像素完整性、屏幕 BUSY 故障恢复。
- 双击误唤醒、整板深睡/广播/连接/刷新电流、OTA 回滚、迁移掉电恢复。

完整真机验收前，第 12 步保持未完成，不把此候选当作稳定发布。
