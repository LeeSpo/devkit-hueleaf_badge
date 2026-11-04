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

- 把图片拖进页面 → 左边是原图（已裁切/旋转），右边是屏幕实际效果（6 色预览）
- **模式**：照片（抖动，适合照片）/ 插画（纯色块，适合线稿、海报、像素画）
- **适配**：裁切铺满 / 完整显示（留白边）；**方向**：竖屏 / 横屏（旋转 90°）/ 自动
- 选择端口（★ 为自动识别到的 ESP32）→「发送到设备」，有进度条
- 右侧列出相框里的图片：↑↓ 排序后「保存顺序」（写入 `img/index.txt`）、显示、删除
- 没插设备时会提示「未检测到 HueLeaf，请插上 USB」，插上后几秒内自动识别；点预览可放大 1:1 / 2×
- 出错时界面只显示简短中文提示，详细信息在终端窗口

Windows 相同：`py hueleaf_gui.py`。端口会显示为 `COMx`。

## 命令行

```bash
python3 hueleaf_push.py photo.jpg --preview out.png          # 只生成预览 PNG，不发送
python3 hueleaf_push.py art.png --mode illustration --orientation landscape --fit pad
python3 hueleaf_push.py photo.jpg                            # 自动找端口，发送并显示
python3 hueleaf_push.py --ports | --list | --delete a.bin | --order b.bin a.bin
```

## 测试

```bash
pip install pytest && python3 -m pytest -q tests
```

相框上长按 BOOT 可打开「选择图片」名称列表（固件 1.2.0+）。发送时会把「名称」一栏（可以是中文）渲染成小图一起存到卡上，相框无需中文字库。

注意：卡上图片顺序 = `index.txt` 中列出的文件，其余（如新上传的）按字母排在后面。
`--order` / 「保存顺序」需要固件 1.1.0 及以上（新增 `SET_ORDER` 命令）。
