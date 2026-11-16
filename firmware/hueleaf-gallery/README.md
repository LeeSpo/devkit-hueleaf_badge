# HueLeaf 固件开发指南 (v2 蓝牙遥控版)

本目录为 HueLeaf 彩叶墨水屏相册固件源码，主控基于 ESP32-C5，支持双频 Wi-Fi 6 与 Bluetooth 5 (LE)，并驱动 3.7 英寸六色全彩电子墨水屏。

固件从 **v2.0.0** 开始全面采用独立全空间蓝牙遥控架构；传统保留出厂固件的版本已冻结归档于 `archive/gallery-stock-1.5.0`。

---

## 🏗️ 架构与核心功能

- **TF 相册**：读取 FAT32 分区 `/img/`，按文件名记忆当前展示进度，支持自定义排序与中文名称标签，支持机身按键翻页与选图菜单。
- **USB 传图**：分帧传输、CRC 校验与临时文件原子提交；整批传图期间开启按键防打断保护，上传完成后保持当前画面，120 秒无活动自动释放保护会话。
- **蓝牙无线遥控**：支持系统蓝牙配对与独立密钥认证；支持手机端分页查询图库（含中文标签）、切图翻页与等待物理刷新结果。
- **屏幕驱动与 MonoFast**：六色全彩画面使用原厂完整波形驱动刷新；黑白操作菜单与系统提示采用移植自 FrameFilm 的 MonoFast 极速快刷（单次约 0.2 秒），支持周期刷新清理与异常回退。
- **定时轮播与电源管理**：支持 1～10080 分钟定时自动翻页；换图间隙让 MCU 进入 Deep Sleep 深度休眠，支持 USB 接入自动唤醒或双击敲击唤醒。
- **OTA 升级与安全机制**：具备双 OTA 应用槽位，支持放入 TF 卡开机升级，具备首次启动回滚保护与跨休眠运行日志。

---

## 🛠️ 构建与测试

### 1. 推荐工作区构建
在仓库根目录直接运行自动化入口：

```bash
./tools/hueleaf-workspace/hueleaf.sh build
./tools/hueleaf-workspace/hueleaf.sh test
```

### 2. 基于 ESP-IDF 直接构建
若本机已配置 ESP-IDF v5.5.4 开发环境（`source $IDF_PATH/export.sh`），可在本目录下直接调用打包工具：

```bash
python tools/build_candidate.py --build-dir /path/to/build
bash tools/package.sh /path/to/build /path/to/output
```

面向最终用户的安装指南与整片 Flash 备份说明请参考 [安装与工作区工具](../../tools/hueleaf-workspace/README.md)。

---

## 💾 Flash 分区布局（4 MiB, remote-v1）

HueLeaf v2 固件使用独立分区布局，回收原厂应用分区以提供充足的空间存放双槽 OTA 应用与蓝牙凭据：

| 分区名称 | 偏移地址 | 大小 | 用途说明 |
| :--- | :--- | :--- | :--- |
| **bootloader** | 0x2000 | — | 二级引导程序与安全启动 |
| **nvs** | 0x9000 | 24 KiB | 基础系统配置存储 |
| **phy_init** | 0xF000 | 4 KiB | RF PHY 初始化数据 |
| **otadata** | 0x10000 | 8 KiB | OTA 启动引导标记槽位 |
| **remote_nvs** | 0x12000 | 56 KiB | 蓝牙遥控认证凭据与系统配对绑定信息 |
| **ota_0** | 0x20000 | 1984 KiB | 固件主应用程序槽位 0 |
| **ota_1** | 0x210000 | 1984 KiB | 固件备用升级槽位 1 |

---

## 📚 详细文档索引

- **系统使用**：[根目录说明文档](../../README.md) · [电脑 GUI 与传图工具](../../tools/hueleaf-push/README.md) · [Android 遥控端](../../android/hueleaf-remote/README.md)
- **协议与通信**：[BLE 蓝牙通信协议](docs/ble-protocol.md) · [遥控与休眠调度流程](docs/remote-control.md) · [Flash 分区与迁移指南](docs/flash-layout.md)
- **测试与验证**：[真机功能验证清单](docs/real_hw_test.md) · [蓝牙遥控真实验收记录](docs/remote-validation.md)
- **版本策略**：[分支与版本归档说明](docs/branches.md)
