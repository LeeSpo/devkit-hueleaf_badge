# hueleaf-push

把图片转换成 HueLeaf（EPD037A02，480×720，6 色）可显示的格式，并通过 USB 发送到 TF 卡 `/img/`。

电脑 GUI、命令行工具与相册固件在 `dev` 统一开发，无需切换独立 GUI 分支。历史 GUI 版本由 `v1.1.0` 标签保存，`feat/name-picker` 仍保留名称选图基线；详见[分支与归档标签](../../firmware/hueleaf-gallery/docs/branches.md)。

## Mac 上打开图形界面

**最简单：** 在访达里双击 `tools/hueleaf-push/HueLeaf.command`。第一次会自动准备环境（需联网，约 1 分钟），之后秒开。
如果提示“无法验证开发者”：右键该文件 →「打开」→「打开」（只需一次）。Windows 双击 `HueLeaf.bat`。

手动方式：

```bash
cd tools/hueleaf-push
python3 -m venv .venv && source .venv/bin/activate   # 只需第一次
pip install -r requirements.txt                       # 只需第一次
python3 hueleaf_gui.py
```

浏览器会自动打开一个本地页面（只监听 127.0.0.1）：

- 界面按任务分为 **「传图片」「图库与轮播」「相框显示」** 三个页签。切换页签保留当前图片、取景和设置草稿；设备连接与传图会话状态始终在页面顶部可见
- 在「传图片」拖入图片 → 左边「取景与裁切」（框固定，图片在框下移动），右边「发送效果」（6 色预览）；窄窗口上下排列。取景工具集中在预览下方，名称、发送与结束传图独立成操作栏
- **自由取景**：拖动移动图片，滚轮或「缩放」滑块缩放；拖动时框外被裁掉的部分半透明显示。快捷按钮「铺满」「完整显示」「居中复位」「旋转 90°」。框内默认叠加 6 色抖动结果，发送到相框的画面与框内完全一致
- **智能吸附**：默认开启。拖动图片接近取景框的水平/垂直中轴或上、下、左、右边缘时，自动对齐并显示洋红色参考线；松手后保留对齐位置，参考线不会进入发送图片。继续拖远可脱离吸附；按住 Alt（Mac 为 Option）临时关闭，也可取消勾选「智能吸附」自由微调。吸附距离按电脑预览像素计算，缩小预览时手感保持一致
- 每张图片的位置/缩放/旋转会记住（按图片内容识别），下次拖入同一张图自动恢复
- 点右侧预览看**实际尺寸**（面板约 52.1 × 78.2 mm，由 3.7 英寸对角线和 480×720 推算）或「像素 1:1」；第一次请点「校准尺寸」，用银行卡（85.6 × 53.98 mm）对齐蓝框
- 校准值和取景记录保存在 `~/.hueleaf/settings.json`（可用环境变量 `HUELEAF_SETTINGS` 改路径）
- **模式**：照片（默认抖动）/ 精细模式（实验）/ 插画（纯色块，适合线稿、海报、像素画）
- **照片抖色**：在线性光中扩散误差，保留中间调与肤色层次；灰色区域使用黑白混色，减少彩色杂点。超出六色混色范围的颜色先映射到可显示色域，避免误差积累成条纹。照片按原图色度调整选色权重，淡色不再统一套用强色度惩罚。插画模式仍使用纯色块。
- **精细模式（实验）**：从照片抖色结果开始，按多个观看尺度反复调整、交换墨点，降低区域混色误差；灰色区域继续只使用黑白墨点。参数来自软件模拟测试，仍需实屏对比，尚未设为默认。切换模式即可用相同取景比较，转换完成后再发送。
- 选择端口（★ 为自动识别到的 ESP32）→「发送到相框」，有进度条。**需相册固件 1.4.0+**：每张图片和名称校验保存后提示「已保存，可继续发送下一张」，不自动刷新屏幕或重启；继续选择下一张图片、调整取景并发送即可
- **固件 1.5.0+ 整批保护**：第一次发送建立上传会话，两张图片之间按键和拍击也会被忽略；可继续选图、取景和发送。会话期间不能显示、删除、保存排序或修改状态栏，避免触发刷屏。仍可刷新图片列表
- 全部传完后点 **「结束传图」**，确认释放会话后恢复相框按键，当前画面保持原样，USB 不重连，不自动刷新或重启。每张成功发送的图片已保存，不依赖这个按钮“最后保存”
- 新图片可在结束后通过相框按键或「图库与轮播」中的「显示」切换；同名覆盖当前图片后，主动点「显示」才能重画新内容。空相册结束后也保留原提示页，下一次按键或「显示」会打开图片
- GUI 每 10 秒保活，关闭工具或断开 USB 后，设备在最后一次保活/上传数据后最多 120 秒恢复按键。未提交的临时上传会被清理，已保存图片保留；上传失败可在同一会话中重试。固件 1.4.x 保留静默上传兼容，但缺少整批按键保护，界面会提示升级
- 「图库与轮播」顶部显示设备已保存的计划、实际暂停原因、当前照片与计划下一张。勾选、↑↓ 排序和定时选项均为草稿，统一点「保存更改」；顺序同时影响手动浏览和自动轮播。摘要不采用草稿，断开后标为最后确认的计划。
- 「相框显示」可勾选 **「纯图片显示（隐藏底部状态栏）」**：统一隐藏时间、电量和图片序号，恢复原本被遮住的图片底部；取消勾选恢复状态栏。切换后相框会刷新当前图片，请等待屏幕完成刷新；设置保存在相框中，换图、重启或拔掉 USB 后继续生效，已有图片无需重新上传。连接时读取相框原有设置，不会自动修改；未连接、设备未就绪或正在操作时开关不可用
- 「图库与轮播 → 定时与唤醒」支持 1～10080 整数分钟、默认关闭。一个「保存更改」按顺序、名单、定时设置提交实际修改的部分，并逐项读回确认；部分失败明确显示已保存和未确认的部分，保留剩余草稿。仅修改间隔不改变默认全部参与模式。刷新、切换设备和离开页面统一保护未保存修改。电脑数据连接暂停轮播，拔线重新计时；新固件的菜单浏览、拍醒和状态读取不重新计时，实际切换/更新照片成功后重新计时。普通轮播模式的拍醒选项与拍击翻页独立；遥控版自动管理拍醒，界面禁用该开关并说明作用范围。旧固件显示原计时与刷新规则。
- 没插设备时会提示「未检测到 HueLeaf，请插上 USB」，插上后几秒内自动识别；点预览可看实际尺寸 / 像素 1:1 / 2×
- 操作反馈统一放在页签下方，成功与错误用不同颜色区分；出错时界面只显示简短中文提示，详细信息在终端窗口

Windows 相同：`py hueleaf_gui.py`。端口会显示为 `COMx`。

## 界面代码与维护

```text
hueleaf_gui.py       本地 HTTP 服务、图像转换调用、设备 API
web/
  index.html        页面结构、任务分区与说明文字
  styles.css        样式、取景框与响应式布局
  app.js            页签、取景预览、传图和设备设置交互
```

前端使用原生 HTML/CSS/JavaScript，无需 Node.js、npm 或前端构建步骤即可启动。双击启动器和 `python3 hueleaf_gui.py` 的用法不变，分发工具时须保留完整 `web/` 目录。静态文件相对 Python 脚本定位，不依赖启动目录；修改后刷新页面即可加载。

服务只公开 `/`、`/web/styles.css` 和 `/web/app.js` 三个静态路由，不提供目录浏览或任意文件读取；新增资源时同步维护 `hueleaf_gui.py` 的 `WEB_ASSETS`。API 保持在 `/api/*`。前端交互回归直接读取 `web/app.js`，可选的 Node.js 仅用于运行该测试，不是 GUI 运行依赖。

## 命令行

```bash
python3 hueleaf_push.py photo.jpg --preview out.png          # 只生成预览 PNG，不发送
python3 hueleaf_push.py photo.jpg --mode fine --preview fine.png --save-bin fine.bin
python3 hueleaf_push.py art.png --mode illustration --orientation landscape --fit pad
python3 hueleaf_push.py photo.jpg                            # 自动找端口，发送并显示
python3 hueleaf_push.py photo.jpg --no-show                  # 固件 1.4.0+：只保存，不刷新屏幕
python3 hueleaf_push.py --ports | --list | --delete a.bin | --order b.bin a.bin
python3 hueleaf_push.py --strip off                         # 纯图片显示；--strip on 恢复底部状态栏
python3 hueleaf_push.py --set-time                          # 通过 USB 同步电脑本地时间
```

`--set-time` 会保持同一串口连接，等待相框启动就绪后再读取并发送电脑当前时间，最多等待 3 分钟。部分连接会触发 USB 复位，首张图片刷新约需 30 秒；等待期间不要反复重新运行命令。成功后会显示同步的时间，底部时间在下次图片刷新时更新，无需升级固件。

`--chroma` 仍可调整选色的色度权重；照片模式会按原图色度自适应调整，真正的灰色保持黑白混色。预览与发送使用同一份六色帧，帧格式仍为 `otp6-v1`，无需升级相框固件。旧的卡上图片不会自动重算，重新转换并发送后才会使用新算法。预览颜色基于固定参考调色板，实际屏幕颜色仍需实屏测色校准。

精细模式使用 NumPy/Numba，已列入 `requirements.txt`。首次选择时会编译并缓存计算代码，后续直接使用缓存，不需要安装系统 C 编译器。已有手动安装环境请重新运行 `pip install -r requirements.txt`；工作区环境运行 `./local/bin/setup-local.sh`；双击启动器会检查并补齐组件。照片和插画模式仍可在未补齐精细组件时通过 Python 直接运行。

精细模式先执行最多 32 轮区域搜索，再执行最多 56 轮精细搜索，无可改进操作时提前停止。`--chroma` 影响起始照片结果，后续多尺度模型权重固定。搜索在固定六色中选择，不能恢复超出屏幕混色色域的饱和颜色，也不保证找到全局最优解。算法参考 [DBS 研究](https://docs.lib.purdue.edu/dissertations/AAI3018459/)，本实现使用固定六色和线性亮度/色差模型，未复现打印机的全部物理模型。

## 测试

```bash
pip install pytest && python3 -m pytest -q tests
```

相框上长按 BOOT 可打开「选择图片」名称列表（固件 1.2.0+）。发送时会把「名称」一栏（可以是中文）渲染成小图一起存到卡上，相框无需中文字库。

注意：卡上图片顺序 = `index.txt` 中列出的文件，其余（如新上传的）按字母排在后面。
`--order` / 「保存顺序」需要固件 1.1.0 及以上（新增 `SET_ORDER` 命令）。

结束传图后，主动点击「显示」、排序或操作相框按键仍会按对应操作刷新。命令行默认仍发送并显示，`--firmware` 仍在暂存升级文件后重启应用升级。升级固件后请重新启动电脑端程序并刷新页面。

排查异常断连可用 `--info` 和 `--log`：1.4.0 的 `INFO` 增加 `upload_no_refresh`（静默上传能力）、`uptime_ms`（本次启动时长）与 `reset_reason`（ESP-IDF `esp_reset_reason()` 枚举值）。连续上传期间启动时长应持续增加；实际复位原因需结合日志和真机现象判断。

Mac 上电脑端同时设置 DTR/RTS，避免分开设置 USB 控制线触发相框复位、重画首图和断连；GUI 收到「设备未就绪」「图片不存在」等有效错误回复时保留连接，只有通信故障才重新连接。旧固件整屏刷新时可能让状态查询和传图等待约 30 秒，电脑端保留 60 秒应答时间作为兼容；1.5.0 的状态查询改读缓存，刷屏期间存储命令直接回复“忙碌”，不再等待屏幕占用的 SPI 总线。Mac 串口采用独占访问：GUI、命令行与串口监视器请一次只运行一个，使用命令行前先关闭 GUI 后台进程。Mac 连接修复只需重启电脑端程序；状态缓存和整批会话保护需要安装 1.5.0 固件。


## 1.5.0 状态与会话协议

`INFO` 保留旧字段，并增加 `state`（`starting` / `idle` / `refreshing` / `uploading` / `maintenance` / `fault`）、`upload_session_supported`、`upload_session_active`、`storage_cache_valid` 和 `storage_cache_age_ms`。缓存未取得或已失效时年龄为 `null`；容量值仅在缓存有效时展示。USB 状态查询不访问 TF 或 I²C，后台只在空闲时探测存储。

`UPLOAD_SESSION` 命令编号 `0x10`，载荷一个字节：`0` 结束（幂等）、`1` 开始/续期、`2` 保活。租期 120 秒；开始要求设备空闲且退出选图菜单，文件传输期间不能主动结束或重启。错误码 `-9` 表示忙碌，`-8` 继续表示启动未就绪。旧命令编号及图片格式不变，旧客户端每个文件传输期间也会屏蔽刷新输入。

## 指定轮播图片

在「相框里的图片」列表逐张勾选「参与定时轮播」，点「保存更改」。提供全选、全不选和数量提示；勾选草稿不会被自动刷新覆盖，保存失败保留草稿。传图会话期间设置锁定，点「结束传图」后再保存。设置不刷新画面、不重启。未勾选图片依然显示在完整图库里，可点「显示」、用相框按键或选图菜单展示。

名单按文件名保存，沿用图库顺序；调整顺序也用「保存更改」。初次使用未保存名单时全部参与；保存自定义名单后，新图默认不加入，同名覆盖保留勾选。全不选意味着不自动换图，但仍可深睡/拍醒；只选一张不会重复刷它。删除的图片不参与；若保存时图库已变化，刷新列表后重试。旧固件不发送新协议命令，禁用勾选和名单按钮，手动显示照常。

## 定时轮播协议

支持指定名单时，`INFO` 还返回 `slideshow_selection_supported:true`、`slideshow_selected_count`、`slideshow_selection_custom` 和 `slideshow_selection_fault`（故障时停止自动换图和深睡，但可手动显示、保存修复）。

`SLIDESHOW_SELECTION` 编号 `0x12`：请求 `start:u16LE, token:u64LE`，首次token为0；响应 `token:u64LE, total:u16LE, start:u16LE, count:u16LE, flags:u8`，flags bit0=自定义名单、bit1=名单故障；接着count条 `selected:u8, filename_length:u8, filename[]`，每页最多64条。total最多300，后续页必须带同一token；图库或勾选变化返回忙碌-9，重新从第0页读取，不能拼接不同快照。

`SET_SLIDESHOW_SELECTION` 编号 `0x13`：请求 `token:u64LE, total:u16LE, bits[ceil(total/8)]`；第i个图库文件对应 `bits[i/8]` 的第 `i%8` 位，最后字节未用位必须0。最大请求48字节，一次原子提交全部名单，避免分块写入半份名单。保存前校验完整快照，过期返回-9，参数非法-5、无卡-6、I/O错误-7、未就绪-8。只有成功落盘才替换运行中名单；保留最近成功事务以幂等确认丢失应答后的重试。名单独立于0x11设置。

`GET /api/list?port=...` 在新固件上通过上述分页拿完整图库，返回每项 `selected` 及 `selection:{token,custom,fault}`；token用16位小写十六进制字符串避免JS大整数丢精度。`POST /api/slideshow-selection` 接收 `port, token, names[]`，后台将按文件名的选择映射成快照位图，拒绝过期/未知文件，返回保存后的完整selection。旧固件仍使用原LIST，不提供选择能力。

`INFO` 增加 `slideshow_supported`、`slideshow_enabled`、`slideshow_interval_min` 和 `slideshow_state`；支持拍击唤醒时还返回 `tap_wake_supported:true` 和布尔 `tap_wake`。轮播状态为 `off`、`usb_paused`、`fault`、`empty`、`busy`、`awake_window`、`next` 或 `sleeping`；总运行状态也可返回 `sleeping`，表示已冻结新操作、正在准备休眠。

`SLIDESHOW` 编号 `0x11`，基础载荷 5 字节：`enabled:u8`（0/1）和 `interval_min:u32LE`（1～10080）。仅协商到 `tap_wake_supported:true` 后可附加第 6 字节 `tap_wake:u8`（0/1），同时原子保存三项配置；省略时保留已有拍击唤醒设置，兼容旧客户端。其他长度或非0/1的开关无效。成功返回空数据，非法参数 `-5`、无 TF `-6`、保存失败 `-7`、未就绪 `-8`、设备忙 `-9`。先协商能力再发送；先原子更新 `/sdcard/hueleaf.cfg`，再同步 NVS 缓存，文件明确值优先。NVS 写入失败会记录警告，已落盘的 TF 配置仍有效。禁用时仍保存指定间隔。HTTP 入口为 `POST /api/slideshow`，JSON 参数为 `port`、布尔 `enabled`、整数 `interval_min`，以及可选布尔 `tap_wake`。没有拍击唤醒能力时不得发送该字段。配置文件键为 `tap_wake`，默认0、独立于 `tap_next`；只在轮播深睡时生效。


## remote-v1 BLE 调试（不含手机 App）

电脑 GUI 的「相框显示 → 手机蓝牙遥控 PIN」可通过 USB 设置 4 位 PIN，重启后在 Android App 输入相同 PIN；设置会替换原密钥并启用遥控。4 位 PIN 只有 10000 种，旁听认证的攻击者可离线穷举，同 PIN 设备使用相同密钥，仅适合可信环境。

`hueleaf_remote_config.py` 通过 USB 配置更安全的随机独立密钥、时限、误唤醒冷却及启停；
`hueleaf_ble.py` 是可选 Bleak 电脑调试客户端；`hlble.py` 提供无蓝牙依赖的分包示例。
仅用于 remote-v1 全空间布局，默认旧版固件不支持 CMD_REMOTE。
见 [固件使用与构建](../../firmware/hueleaf-gallery/docs/remote-control.md) 和
[完整 BLE 协议](../../firmware/hueleaf-gallery/docs/ble-protocol.md)。不要提交设备密钥。

### 组合保存与状态兼容

电脑后台 `POST /api/slideshow-plan` 接收 `port`，以及可选的 `order`（完整顺序）、`names`（参与图片）、`settings`（enabled/interval_min/可选tap_wake）。修改顺序或名单时携带读取名单的 `token`；旧固件仅改顺序可省略 token。只提交有修改的部分，名单省略时保持默认全部参与或现有自定义模式。
返回 `ok`、`confirmed`（order/selection/settings）、`failed`、`error` 及最终读取的 `info`、`selection`、`order`；读回失败时返回 `state_unknown`，不附带过期的写前状态。复用原 USB 命令，不保证跨文件事务，不自动回滚已确认步骤。

USB INFO 追加 `slideshow_behavior_version=2`、`slideshow_status`（off/fault/starting/empty/no_selection/single_hold/usb_paused/busy/waiting）和 `remote_sleep_managed`。状态查询不访问 TF 或屏幕；旧字段和写命令继续兼容。电脑每两秒读取在线状态，拔线后不模拟实时倒计时。
