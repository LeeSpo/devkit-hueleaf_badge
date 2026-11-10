# Remote-v1 全空间布局与迁移

此配置明确回收设备内原厂 App / image_cache；默认旧配置仍保留原厂保护。
不得通过旧 TF OTA 或裸写分区表迁移。新固件仍保留 A/B OTA、待验证回滚及现有 NVS。

|名称|偏移|大小|
|---|---|---|
|nvs|0x9000|0x6000|
|phy_init|0xF000|0x1000|
|otadata|0x10000|0x2000|
|remote_nvs|0x12000|0xE000|
|ota_0|0x20000|0x1F0000|
|ota_1|0x210000|0x1F0000|

首次迁移须显式 `--migrate-replace-stock`，安装器先读取只读 eFuse 安全状态，
保存全 4 MiB 备份和 SHA256，再核对旧分区 NVS/PHY 边界。未知布局拒绝自动迁移。
迁移擦除 0x10000..0x400000，保留 NVS/PHY；写入新 bootloader、分区表、OTA 元数据、App。
不得中途断电。迁移中断可使用同一已校验安装包重试；保留首次迁移前备份。
后续同布局重装不擦除 remote_nvs；首次绑定凭据不会因日常升级而清空。

恢复原厂/旧布局需在 USB 下载模式按备份 SHA256 核对后写回完整备份（包含旧分区表），
恢复操作会覆盖迁移后设置和绑定。不能仅使用旧版 App 镜像覆盖新分区。

Flash 扩容不增加 SRAM，BLE/图库同时运行仍须检查堆与栈；无需沿用旧 704 KiB 限制。

remote App 的 ESP descriptor project_name 固定为 `hueleaf_remote_v1`，安装器、构建检查和TF升级
均检查标识；新布局拒绝旧 gallery App。标识不是数字签名，不用于抵抗恶意固件。
`--preflight-only` 只备份/检查不写设备。显式对同布局使用 `--migrate-replace-stock` 是迁移重试，
会再次擦除应用区和remote_nvs；日常重装不要带此参数，以保留绑定。
