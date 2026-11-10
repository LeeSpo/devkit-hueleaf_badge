# HueLeaf BLE remote-v1 协议（v1）

Android App 位于 `android/hueleaf-remote`；电脑调试客户端参考 `tools/hueleaf-push/hueleaf_ble.py` 和 `hlble.py`。
所有整数小端，最大逻辑消息512字节，同时仅一个请求；一条应答所有 indication 确认后再发下一请求。

## 发现及 GATT

设备名 `HueLeaf Remote`，以 service UUID 发现，不依赖名字或固定地址。
UUID 公共后缀 `-6c8f-4d84-9af8-6e1f9000a001`：

|前缀|用途|属性|
|---|---|---|
|8f510001|service|primary|
|8f510002|RX|write with response，要求加密链路|
|8f510003|TX|indicate（必须订阅）|
|8f510004|challenge|read，33字节，支持长读|

连接后先完成 OS BLE 配对（LE Secure Connections Just Works），订阅 TX indication，读取 challenge，发送认证。
Just Works 没有 MITM 保护，因此不能在不可信现场首次配对；32字节设备密钥的挑战响应阻止未知客户端直接操作，
但不声称能抵抗主动中间人转发。日常流量受链路加密保护。OS 蓝牙 bond 通过 NimBLE NVS 持久保存，
避免手机保存密钥而相框睡醒后丢失导致 KEY_MISSING。手机丢弃旧 bond 后允许重新配对；重新配对仍需独立 HMAC 认证。
首次连接预留最多60秒完成系统配对和认证；未认证断连不会续原发现窗口。

Challenge=`provisioned:u8 || nonce:32bytes`。认证摘要=`HMAC-SHA256(key, nonce)`。
nonce 每次连接、每次认证尝试随机更新；认证尝试不会续时。一个连接只认证一次；失败重读 challenge 后可重试。
设备密钥只能通过物理 USB 配置，不通过 BLE 读取或写入；每台设备应使用独立随机密钥。

## 分片

每次 RX write / TX indication：`stream:u8, sequence:u8, flags:u8, reserved=0:u8, bytes[]`。
flags bit0 START，bit1 END，其余0；START sequence必须0，同一消息序号逐片加1。
分片至少1字节数据；总消息4..512字节；RX 自START起5秒内结束，错误返回 ATT error。
最大数据长度=min(MTU-7,244)；MTU23下每片16字节。
TX 为逐片确认的 indication；丢片、乱序、超限应放弃该消息并重新连接/重试，不能拼接旧连接数据。

## 逻辑消息

请求：`version=1:u8, op:u8, request_id:u16, payload[]`。
应答：`version=1:u8, op|0x80:u8, request_id:u16, status:i16, payload[]`。
请求ID用于串行去重：**只缓存同一连接最近一条完整请求及应答**。超时重发必须保持完全相同的ID和内容；
同ID不同内容被拒绝。跨连接不重试 NAV，先查状态；不能承诺跨断电恰好执行一次。

|op|请求payload|应答payload|
|---|---|---|
|1 AUTH|32字节HMAC|空|
|2 STATUS|空|UTF-8 JSON：ready,state,battery,n,cur,name|
|3 LIST|start:u16,token:u64（首次0）|下述分页图库|
|4 NAV|delta:i8，仅+1/-1|空，表示已接受|
|5 SHOW|文件名UTF-8，不含NUL，1..63字节，精确匹配卡文件名|空，表示已接受|
|6 END|空|空，发送结束后安全回睡|
|7 LIST_LABELS|start:u16,token:u64（首次0）|带显示标签的分页图库，见下文|

显示请求完成后再发异步事件：op=0xC0、原request_id、status=0/-7、空payload。
命令应答成功不代表物理刷新成功。只允许一个在途刷新，刷新期间第二个显示命令返回忙碌。
STATUS 在刷新中可用；LIST 和 LIST_LABELS 在存储/刷新忙时返回忙碌。
鉴权后的 STATUS 可在图库初始化期间返回ready=false；不要紧密轮询，建议500ms一次。

LIST 应答：`token:u64,total:u16,start:u16,count:u16,flags:u8,records[]`；
record=`selected:u8,name_length:u8,name_bytes[]`。flags bit0自定义轮播名单，bit1名单故障。
token覆盖名字/顺序/名单，下一页携带同token，start+=count。列表变化返回-11，客户端从token=0重读。
支持300张长文件名，不能假设一页容纳全部图库。

LIST_LABELS 沿用 LIST 的请求和响应头；record 为
`selected:u8,name_length:u8,name_bytes[],label_length:u8,label_utf8[]`。
文件名仍为精确匹配 SHOW 的标识；标签仅用于显示和搜索，允许重名，最长255字节。
标签文本读取自现有 `img/.labels/<stem>.lbl` 的 HLL1 文件，不需要重新上传图片。
缺失、空标签、无效 UTF-8、损坏的文件头或位图尺寸/长度返回零长度标签；客户端对空白标签也回退显示文件名。
读取采用与图库相同的存储互斥和忙碌检查，不触发刷新。逻辑消息仍不超过512字节，记录不跨页。
token 仍只覆盖名字/顺序/名单；标签是读取当时的元数据，单独更新标签不会使分页 token 过期。
客户端应在完整列表读取成功后更新界面；-11 时丢弃部分结果并从头重读。
旧固件对 op7 返回 -3，新客户端在该连接内回退到 op3，并提示固件升级；其他错误不能作为降级依据。
原 op3、STATUS 和 SHOW 的格式不变，因此旧客户端可继续连接新固件。

错误：0成功，-3未知命令，-4文件不存在/空图库，-5参数错误，-7I/O错误，-8未就绪，-9忙碌，
-10未授权，-11过期快照。ATT错误属于传输层，不是以上逻辑错误。

例：NAV下一张，ID=2：逻辑 `01 04 02 00 01`，MTU23单片 `02 00 03 00 01 04 02 00 01`。
成功应答逻辑 `01 84 02 00 00 00`；完成事件 `01 c0 02 00 00 00`。

## USB 配置命令

HLP1 CMD_REMOTE=0x14；原USB status仍为i32。查询返回 remote-v1 JSON。
payload：0（查询）；1+key32（覆盖密钥）；2（撤销）；3+wait_ms:u32+idle_ms:u32+reconnect_ms:u32+
cooldown_ms:u32+cooldown_after:u8；4+enabled:u8。改变均保存后重启生效，当前会话不热切换。
wait 5000..120000，idle 10000..600000，reconnect 0..60000，cooldown 0..3600000，after 1..20。
密钥不出现在日志/查询；文件凭据必须保密。Windows下额外检查文件ACL，POSIX创建权限0600。

## 后续手机接入

iOS使用CoreBluetooth，处理系统配对、蓝牙授权及连接状态；Android使用BLE GATT并处理相应系统版本的
扫描/连接权限。先订阅indication再读nonce，写RX使用有响应写入，按实际MTU分片。
切后台/系统断连后需重新发现、读取新nonce并认证；不依赖手机后台一直扫描。
发现窗口默认30秒，故应先打开控制端再双击挂件；窗口内接入后最多60秒完成系统配对及认证。
USB 连接期间相框保持唤醒，允许用户再次连接，不受已过期的电池发现窗口或上次 END 影响。
END后设备可能立即断连，这是正常退出。


重复 SHOW 当前图及单图 NAV 在画面内容与设置均未变化时可不执行物理刷新。
请求的成功 ACK 和 `0x40` 完成事件仍按原顺序发送；完成表示目标画面已满足请求，
不保证每次请求都发出面板 DRF。连接、认证、STATUS、LIST、LIST_LABELS 不构成刷新指令。
