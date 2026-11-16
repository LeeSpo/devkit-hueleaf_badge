# HueLeaf 遥控 Android

HueLeaf v2 彩叶相框的配套 Android 蓝牙遥控客户端，支持 Android 6.0（API 23）及以上系统。

配合 HueLeaf v2 固件，手机可作为相框的无线随身遥控器，实现浏览图库、远程切图、状态查看与一键休眠，完全无需 Wi-Fi 或局域网。

---

## ✨ 主要功能

- 📱 **原生 Material 3 设计**：自适应深浅色模式，支持 Android 12+ 动态取色壁纸主题（其余设备使用 HueLeaf 清新绿主题）。
- 🖼️ **图库浏览与搜索**：
  - 支持显示相片标题（由电脑端上传时渲染的中文/多语言标签）；
  - 支持按文件名或标题实时搜索；
  - 支持按「全部 / 轮播名单」筛选，直观标识正在显示的图片与已选轮播照片。
- 🎮 **无线遥控切图**：
  - 上一张、下一张、点击图片即可直接远程切换相框画面；
  - 具备防抖与物理刷新等待保护，切图过程中自动锁定操作，避免重复连击。
- 🔋 **状态与电量监控**：实时显示相框当前电量百分比、运行状态与图片序号。
- 💤 **一键结束休眠**：遥控完毕后支持一键通知相框进入深度休眠省电。

---

## 🚀 使用指南

### 1. 配对与连接

1. **设置 PIN 码**：首次使用前，使用电脑 Web GUI 连接相框，在「相框显示 → 手机蓝牙遥控 PIN」中输入 4 位数字（例如 `1234`）并保存，随后重启相框。
2. **连接设备**：
   - 打开手机 App，点击「连接相框」面板；
   - 输入相同的 4 位 PIN 码；
   - 确保手机已开启蓝牙与定位权限（Android 6–11 扫描 BLE 设备必须开启系统定位），点击「扫描相框」；
   - 在列表中点击搜索到的 HueLeaf 设备，按系统提示完成配对与连接（相框开机广播窗口默认约为 30 秒）。

### 2. 日常操作

- 在「遥控」页签可直观看到当前相框正在展示的图片信息与电量，点击下方按钮切图；
- 在「图库」页签可以翻阅所有照片，点击任意图片即可指挥相框切换并展示；
- 遥控结束后，点击「结束遥控并休眠」断开连接让相框休眠。

> [!TIP]
> - **标签显示说明**：图片标签由电脑端工具上传时生成并保存在卡中，若图片没有额外标签则默认显示原始文件名。
> - **安全与凭据说明**：App 仅保存一台相框的连接凭据，使用 Android Keystore 硬件安全加密存储。如需更高安全级别，可使用电脑工具 `hueleaf_remote_config.py` 配置随机独立密钥并导入 App。

---

## 🛠️ 构建与开发

### 环境要求
- JDK 17 或 21
- Android SDK Platform 35 及对应 Build Tools
- 仓库自带 Gradle Wrapper

### 常用命令

```bash
cd android/hueleaf-remote

# 运行单元测试与 Lint 检查
./gradlew testDebugUnitTest lintDebug

# 构建 Debug APK（输出至 app/build/outputs/apk/debug/app-debug.apk）
./gradlew assembleDebug

# 本地签名构建 Release APK
./gradlew assembleRelease -PhueleafVersionName=1.0.0 -PhueleafVersionCode=1
```

### UI 测试与截图回归

连接测试手机或模拟器后运行：

```bash
./gradlew assembleDebug assembleDebugAndroidTest
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb install -r app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk
adb shell am instrument -w -e prefix dark -e night yes com.hueleaf.remote.test/com.hueleaf.remote.UiSmokeInstrumentation
adb pull /sdcard/Android/data/com.hueleaf.remote/files/ui-review/
```
