# HueLeaf 遥控 Android

remote-v1 的 BLE 控制端，Android 6.0（API 23）及以上。支持读取状态和分页图库、查看轮播名单标记、上一张/下一张、按文件名显示、等待物理刷新结果以及结束遥控。图库变化时重新读取，断线后不会自动重发显示命令。应用切到后台会断开连接。

本应用使用现有 BLE 协议，不传图；USB 网页中的删除、排序、轮播和显示设置未通过 BLE 暴露，因此不在应用中。设备必须使用启用 remote-v1 的固件及对应的独立全空间布局。请参考 [remote-v1 文档](../../firmware/hueleaf-gallery/docs/remote-control.md) 和 [BLE 协议](../../firmware/hueleaf-gallery/docs/ble-protocol.md)。

## 界面

- 原生 Material 3，深浅色跟随系统，支持的设备使用壁纸动态配色；其余设备使用 HueLeaf 绿色后备主题。
- 「遥控」「图库」支持点击页签或左右滑动切换；页签切换保留搜索、筛选和列表位置，不会重新连接或发送切图命令。
- 遥控页显示当前文件名、序号、电量与相框状态；发送切图后等待物理刷新完成，期间阻止重复操作。
- 图库按文件名搜索，支持「全部／轮播名单」筛选，分别标记正在显示的图片和已加入轮播的图片。协议没有缩略图，轮播标记只读。
- 顶部蓝牙图标或「连接相框」打开连接面板。PIN、扫描、设备选择和取消操作集中在面板；随机密钥导入位于「高级连接与安全说明」。

## 使用

1. 在电脑运行 HueLeaf 图形界面，通过 USB 连接已安装 remote-v1 固件的相框，在「相框显示 → 手机蓝牙遥控 PIN」输入 4 位数字并保存，然后重启相框。设置会覆盖原密钥并启用遥控，其他手机的原凭据将失效。
2. 打开手机 App，点击「连接相框」，在面板输入相同的 4 位 PIN，点击「扫描相框」；以后可留空使用已保存的凭据。打开手机蓝牙并授予扫描、连接权限；Android 6–11 还需要定位权限，部分手机需要开启系统定位。扫描后双击相框，发现设备后即可点击连接，并根据系统提示完成配对；发现上次连接的设备会自动连接。相框默认广播窗口为 30 秒，尽快选择设备。
3. **安全性：4 位 PIN 只有 10000 种，攻击者旁听一次认证即可离线穷举，相同 PIN 的设备密钥相同。**仅在可信环境使用；如需更强保护，可继续使用电脑工具 `python tools/hueleaf-push/hueleaf_remote_config.py PORT --provision /安全目录/pendant-key.json --reboot` 配置随机密钥，并在 App 中导入 JSON。配置会覆盖设备原密钥，已经配置过的设备应使用原有密钥文件。应用仅保存一台设备的凭据，使用 Android Keystore 加密保存，不参与应用备份；原 JSON 文件仍须自行妥善保管。
4. 首次配对应在可信环境中进行。设备使用 Just Works 系统配对及独立密钥认证，不提供首次配对的中间人保护。固件持久保存系统 bond；首次连接确认系统配对后，App 再用 PIN 派生密钥认证。4 位 PIN 只在 App 输入，不是系统蓝牙配对码。旧固件未保存过系统密钥时，升级后可能需要在手机蓝牙设置中忘记 HueLeaf 旧配对一次。
5. 结束时点击“结束遥控并休眠”。显示命令的成功应答仅表示被接受；应用会等待物理刷新完成事件。切后台、连接中断或重新打开应用时，需再次扫描并双击设备。

## 构建与测试

需要 JDK 17 或 21、Android SDK Platform 35 和对应构建工具。仓库提供 Gradle wrapper；首次构建需要下载依赖。`ANDROID_HOME` 指向 SDK：

```sh
cd android/hueleaf-remote
./gradlew testDebugUnitTest lintDebug assembleDebug
```

调试 APK 位于 `app/build/outputs/apk/debug/app-debug.apk`；它使用本机构建环境的调试签名，不是正式发布签名。Gradle 配置同时包含 Google/Maven Central 及其 Aliyun 镜像，便于网络受限时获取构建依赖。

### 手机 UI 回归与截图

解锁连接的测试手机，构建并安装应用及独立的 UI 测试 APK：

```sh
./gradlew assembleDebug assembleDebugAndroidTest
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb install -r app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk
adb shell am instrument -w -e prefix dark -e night yes com.hueleaf.remote.test/com.hueleaf.remote.UiSmokeInstrumentation
adb shell am instrument -w -e prefix light -e night no com.hueleaf.remote.test/com.hueleaf.remote.UiSmokeInstrumentation
adb pull /sdcard/Android/data/com.hueleaf.remote/files/ui-review/
```

检查仪器输出的 `Passed` 结果；失败信息通过 `stream` 输出。测试使用界面状态样例，不读取或修改连接凭据，不连接 BLE，不代表相框实际连接成功。覆盖左右滑动及页签同步、切页保留搜索、搜索与轮播筛选、清除筛选、刷新期间禁用控件、初始化与断线提示，以及 PIN 行内校验。截图文件名含 `fixture` 的是样例连接状态。

深浅色切换仅在测试进程生效，不修改系统主题。手机锁定时，测试向应用视图派发手势，并通过 Android 原生视图绘制截图；手机解锁时使用系统输入事件和屏幕截图。两种方式的区别记录在测试输出中。

## 验证限制

协议单元测试和 Android 构建不能证明无线配对、屏幕刷新、功耗或后台行为在真实设备上正常；发布前应使用 Android 手机和 remote-v1 相框按固件文档完成真机测试。默认固件的认证期限很短，首次系统配对可能超时，此时请重新唤醒并重试。
