# BLE 挂件遥控 remote-v1

实现范围：固件、USB配置、电脑BLE调试示例、协议文档；不包含手机App。
硬件：ESP32-C5 + MSA321 INT1 GPIO4；无需改板。仅使用独立全空间配置，见[布局](flash-layout.md)。

## 状态与功耗

深睡保图（BLE关闭，MSA321正常125Hz双击检测）→双击→精简启动→BLE等待→加密连接/密钥认证→
挂载TF及恢复图库→按需显示→断连重连窗口→安全深睡。
无人连接默认30秒：前3秒广播100..150ms，随后800..1000ms；后续震动不续时。
认证限10秒且不超过原广播期限。断连重连10秒；有效操作空闲120秒。
BLE modem sleep+主晶振+自动light sleep；精简等待不启动TF、屏幕及轮询任务。
连接后的按键/传感器任务仍可能周期唤醒CPU，但空闲间隙允许省电；不是深睡电流。
MSA321/PMIC/指示灯等仍耗电，须量整板电流；不承诺具体续航。

冷启动和OTA待验证走完整显示验证，不能靠蓝牙快捷启动确认固件有效。
遥控模式独立于轮播开启。USB主机连接暂停深睡。写卡/上传/刷新/待确认indication阻止深睡。
等待阶段没有图库ready也能回睡；已初始化外设按实际状态恢复/卸载。
RTC保留信息有版本和SHA256校验，复位/无效标记不复用拍醒路径。

## 内存与显示

remote-v1 **保留 MonoFast**，默认启用；名称选择器和黑白提示页使用原 FrameFilm 波形。
六色照片仍走原 OTP 刷新，不将彩色内容转换成快刷黑白图。底部状态条保持黑白。

为适配 ESP32-C5 内部 SRAM，使用以下固定缓冲：

| 用途 | 大小 | 布局/说明 |
|---|---:|---|
| 黑白 UI 当前帧 | 43,200 B（42.19 KiB） | 逻辑 x 正序、y 正序，MSB 优先，1 为白 |
| MonoFast 上一帧状态 | 43,200 B | 独立平面，控制器顺序；成功后才作为有效基线 |
| MonoFast 编码批次 | 1,440 B | 分块编码 old/new 状态，直接写面板 |
| 彩色 SPI 暂存 | 2,048 B | TF 流式读取，不申请 172,800 B 完整彩色帧 |
| USB 协议工作区 | 8,208 B | 单请求串行复用 RX/TX/文本，HLP1 最大负载仍为 8,192 B |
| TF 升级暂存 | 1,024 B | 固定小块用于 SHA256 和 OTA 写入，无新增临时大块分配 |

首次 MonoFast 的 full_clear、每 20 帧 mono_clear、BUSY 校验和彩色切换时的复位/OTP 恢复保持。
SPI/刷新失败使上一帧基线失效；物理 OTP 回退成功才可报告成功，下次快刷重新完整初始化。
`hueleaf.cfg` 的 `mono_fast=0` 或 NVS 中的旧设置仍有效，禁用时把 1bpp UI 分块扩展为 OTP 流。
传统保留原厂配置继续使用原 4bpp 位内暂存方案。

当前双平面版静态链接 HP SRAM 余量为 65,766 B（约 64.22 KiB）；这是链接余量，不是运行时
可用堆保证。BLE 动态分配、任务栈、TF 挂载和加密峰值必须在真机检查，见[验证记录](remote-validation.md)。
TF/屏幕共享 SPI2：每块读卡前释放 panel SPI 锁，读后重新获取；gallery SD 锁与屏幕锁贯穿操作。
无人连接的轻量等待阶段仍不访问 TF、不初始化/刷新屏幕；MonoFast 只在明确需要显示 UI 时调用。

## 构建（工作区根目录）

```bash
source local/bin/dev-env.sh
python devkit-hueleaf_base/firmware/hueleaf-gallery/tools/build_candidate.py --remote \
  --build-dir "$PWD/local/builds/remote-control/gallery/v5.5.4"
bash devkit-hueleaf_base/firmware/hueleaf-gallery/tools/package.sh \
  "$PWD/local/builds/remote-control/gallery/v5.5.4" "$PWD/local/artifacts/remote-v1-candidate"
```

也可 `HUELEAF_REMOTE=1 ./local/bin/hueleaf-local.sh build`。使用新的输出目录，打包拒绝混入旧包。
安装器路径在产物install/flash_install.py；首次加`--migrate-replace-stock --backup-dir "$PWD/local/backups"`。
备份恢复与断电风险见[布局与迁移](flash-layout.md)。不要用裸idf flash迁移。

## USB设置与电脑调试

```bash
cd devkit-hueleaf_base/tools/hueleaf-push
python hueleaf_remote_config.py /dev/cu.usbmodemXXX --provision /安全目录/pendant-key.json --reboot
python hueleaf_remote_config.py /dev/cu.usbmodemXXX
python hueleaf_remote_config.py /dev/cu.usbmodemXXX --options 30000 120000 10000 0 3 --reboot
# 单独环境安装可选依赖：pip install bleak
python hueleaf_ble.py --key /安全目录/pendant-key.json list
python hueleaf_ble.py --key /安全目录/pendant-key.json next
python hueleaf_ble.py --key /安全目录/pendant-key.json show --name photo.bin
```

配置写入remote_nvs，重启应用；撤销`--revoke --reboot`，停用`--enable no --reboot`。
停用BLE遥控后回到传统USB/实体操作和轮播策略；再启用需USB。凭据不写进源码或提交。
OS首次配对可能耗时，建议先插USB完成配置和BLE验证；BLE密钥认证不是OS系统配对码。
多设备时用`--address`选择，macOS使用OS UUID而非MAC。

## 误唤醒与故障

双击硬件判定可能被走路触发。窗口固定、无卡访问、不刷屏降低代价，不等于消除误唤醒。
可设置cooldown_ms>0：连续after次无人认证后暂时关闭IMU唤醒，用定时器恢复。
冷却时双击无效；优先保证轮播期限，不将冷却定时唤醒当换图。默认关闭。
计数在RTC，不在每次误唤醒时写Flash。
无卡在认证后报告not-ready并保留原图；睡眠准备/外设故障保持唤醒，需USB诊断或复位。
不在故障状态反复睡醒。NVS读取异常不擦除凭据。

协议、帧示例及客户端约束见[BLE协议](ble-protocol.md)。

## 验证状态

构建/主机测试由执行记录报告；无线时序、实际RAM余量和整板功耗仍需真机。
不得把编译通过、软件计时器设置成功当作已证明省电或续航验收。

打包产物包含 docs/ 完整协议与验收文件。USB查询额外提供remote任务栈最低余量；实际任务堆栈与
控制器动态分配必须真机验证。USB远程版本驱动环形缓冲缩为1024/512字节，HLP1仍分块收发。
