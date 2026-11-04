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

- 把图片拖进页面 → 左边是相框取景框（框固定，图片在框下移动），右边是屏幕实际效果（6 色预览）
- **自由取景**：拖动移动图片，滚轮或「缩放」滑块缩放；拖动时框外被裁掉的部分半透明显示。快捷按钮「铺满」「完整显示」「居中复位」「旋转 90°」。框内默认叠加 6 色抖动结果，发送到相框的画面与框内完全一致
- 每张图片的位置/缩放/旋转会记住（按图片内容识别），下次拖入同一张图自动恢复
- 点右侧预览看**实际尺寸**（面板约 52.1 × 78.2 mm，由 3.7 英寸对角线和 480×720 推算）或「像素 1:1」；第一次请点「校准尺寸」，用银行卡（85.6 × 53.98 mm）对齐蓝框
- 校准值和取景记录保存在 `~/.hueleaf/settings.json`（可用环境变量 `HUELEAF_SETTINGS` 改路径）
- **模式**：照片（抖动，适合照片）/ 插画（纯色块，适合线稿、海报、像素画）
- **照片抖色**：在线性光中扩散误差，保留中间调与肤色层次；灰色区域使用黑白混色，减少彩色杂点。超出六色混色范围的颜色先映射到可显示色域，避免误差积累成条纹。照片按原图色度调整选色权重，淡色不再统一套用强色度惩罚。插画模式仍使用纯色块。
- 选择端口（★ 为自动识别到的 ESP32）→「发送到设备」，有进度条
- 右侧列出相框里的图片：↑↓ 排序后「保存顺序」（写入 `img/index.txt`）、显示、删除
- 没插设备时会提示「未检测到 HueLeaf，请插上 USB」，插上后几秒内自动识别；点预览可看实际尺寸 / 像素 1:1 / 2×
- 出错时界面只显示简短中文提示，详细信息在终端窗口

Windows 相同：`py hueleaf_gui.py`。端口会显示为 `COMx`。

## 命令行

```bash
python3 hueleaf_push.py photo.jpg --preview out.png          # 只生成预览 PNG，不发送
python3 hueleaf_push.py art.png --mode illustration --orientation landscape --fit pad
python3 hueleaf_push.py photo.jpg                            # 自动找端口，发送并显示
python3 hueleaf_push.py --ports | --list | --delete a.bin | --order b.bin a.bin
```

`--chroma` 仍可调整选色的色度权重；照片模式会按原图色度自适应调整，真正的灰色保持黑白混色。预览与发送使用同一份六色帧，帧格式仍为 `otp6-v1`，无需升级相框固件。旧的卡上图片不会自动重算，重新转换并发送后才会使用新算法。预览颜色基于固定参考调色板，实际屏幕颜色仍需实屏测色校准。

## 测试

```bash
pip install pytest && python3 -m pytest -q tests
```

相框上长按 BOOT 可打开「选择图片」名称列表（固件 1.2.0+）。发送时会把「名称」一栏（可以是中文）渲染成小图一起存到卡上，相框无需中文字库。

注意：卡上图片顺序 = `index.txt` 中列出的文件，其余（如新上传的）按字母排在后面。
`--order` / 「保存顺序」需要固件 1.1.0 及以上（新增 `SET_ORDER` 命令）。
