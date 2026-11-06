#!/usr/bin/env python3
"""HueLeaf desktop GUI: a local web page served by the Python stdlib.

    python3 hueleaf_gui.py            # opens http://127.0.0.1:<port>/ in your browser

Why a browser UI: no extra dependency (Tkinter is missing/broken on several
macOS Python builds and can't preview drag-and-drop without add-ons), it looks
the same on macOS and Windows, and drag-and-drop of images is native in every
browser. The server only listens on 127.0.0.1.
"""
from __future__ import annotations

import base64, io, json, os, sys, threading, uuid, webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from PIL import Image, ImageOps

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hlformat as F
import hueleaf_push as P

_frames: dict[str, tuple[str, bytes]] = {}     # id -> (suggested name, frame)
_jobs: dict[str, dict] = {}
_dev_lock = threading.Lock()
_devs: dict[str, P.Device] = {}
_op_lock = threading.Lock()
_operations: dict[str, str] = {}


class PortBusy(RuntimeError):
    pass


class UpgradeRequired(RuntimeError):
    pass


import traceback


def friendly_error(e: BaseException) -> str:
    """Plain-Chinese message for the UI; details go to the terminal only."""
    if isinstance(e, (PortBusy, UpgradeRequired)):
        return str(e)
    t = f"{type(e).__name__}: {e}".lower()
    if "fine mode dependencies" in t:
        return "精细模式组件未安装，请重新运行环境安装脚本，或安装 requirements.txt 中的依赖"
    if isinstance(e, TimeoutError) or "no valid reply" in t:
        return "设备没有响应：请确认相框已开机、已刷新版固件，然后重试"
    if "crc" in t or "verify mismatch" in t:
        return "传输校验失败（数据在 USB 线上出错），请重试或换一根数据线"
    if any(k in t for k in ("errno 13", "permission", "access is denied", "errno 2", "no such file",
                            "could not open port", "errno 16", "busy", "filenotfound",
                            "device not configured", "errno 6", "serialexception", "write failed")):
        return "连不上设备：没插好或没权限（也可能被其他程序占用）"
    if "device not ready" in t:
        return "设备正在启动或等待 TF 卡，请稍后刷新；持续失败可读取设备日志"
    if "no tf card" in t:
        return "设备里没有检测到 TF 卡"
    if "not found" in t:
        return "卡上找不到这张图片（可能已被删除），请刷新列表"
    if "unknown command" in t:
        return "设备固件太旧，不支持这个操作，请先升级固件"
    if "i/o error" in t:
        return "TF 卡读写失败，请检查卡是否插好或已满"
    if "convert first" in t:
        return "请先拖入一张图片"
    if "cannot identify image" in t or "unidentifiedimage" in t:
        return "无法识别这个图片文件"
    return "操作失败，请重试（详细信息见终端窗口）"


def log_error(where: str, e: BaseException):
    sys.stderr.write(f"[hueleaf-gui] {where} failed:\n")
    traceback.print_exception(e, file=sys.stderr)


def display_name(card_name: str) -> str:
    return card_name[:-4] if card_name.lower().endswith(".bin") else card_name


def _png_data_url(img: Image.Image) -> str:
    b = io.BytesIO()
    img.save(b, "PNG")
    return "data:image/png;base64," + base64.b64encode(b.getvalue()).decode()


def device(port: str) -> P.Device:
    d = _devs.get(port)
    if d is None:
        d = _devs[port] = P.Device(port)
    return d


def forget_device(port: str):
    """Caller holds _dev_lock."""
    d = _devs.pop(port, None)
    if d:
        d.close()


def claim_port(port: str, operation: str):
    # Reserve before starting a worker, so a reboot cannot overtake an upload.
    with _op_lock:
        if port in _operations:
            raise PortBusy("设备正在传输或执行其他操作，请完成后再试")
        _operations[port] = operation


def release_port(port: str):
    with _op_lock:
        _operations.pop(port, None)


def with_device(port: str, fn):
    with _dev_lock:
        try:
            return fn(device(port))
        except Exception:
            forget_device(port)            # reopen next time (unplugged etc.)
            raise


def change_device(port: str, fn):
    claim_port(port, "change")
    try:
        return with_device(port, fn)
    finally:
        release_port(port)


def reboot_device(port: str) -> dict:
    claim_port(port, "reboot")
    try:
        with _dev_lock:
            try:
                device(port).ok("REBOOT", retries=1)
                return {"ok": True}
            except (TimeoutError, OSError) as e:
                # The ACK may have been lost during USB re-enumeration. Probe
                # readiness/uptime in the UI; never resend this command.
                log_error("reboot confirmation", e)
                return {"ok": False, "uncertain": True}
            finally:
                forget_device(port)
    finally:
        release_port(port)


def detected_ports():
    found = P.find_ports()
    # Do not queue behind a transfer merely to enumerate ports.
    if _dev_lock.acquire(blocking=False):
        try:
            present = {p for p, _, _ in found}
            for port in list(_devs):
                if port not in present:
                    forget_device(port)
        finally:
            _dev_lock.release()
    return {"ports": [{"device": p, "desc": s, "hueleaf": h} for p, s, h in found],
            "default": default_port(found)}


def convert(data_url: str, filename: str, mode: str, fit: str, orientation: str,
            place: dict | None = None) -> dict:
    raw = base64.b64decode(data_url.split(",", 1)[-1])
    img = Image.open(io.BytesIO(raw))
    img.load()
    if place:
        rot = int(place.get("rotate", 0)) % 360
        ids, fr = F.convert(img, style=mode, place=place)
        fitted_img = F.place_image(img, rot, float(place["scale"]), float(place["ox"]), float(place["oy"]))
    else:
        rot = F.orientation_rotate(img, orientation)
        ids, fr = F.convert(img, mode=fit, style=mode, orientation=orientation)
        fitted_img = F.fit_image(img, rot, fit)
    fid = uuid.uuid4().hex
    _frames[fid] = (P.display_stem(filename or "img"), fr)
    if len(_frames) > 50:
        _frames.pop(next(iter(_frames)))
    w, h = ImageOps.exif_transpose(img).size
    if rot % 180:
        w, h = h, w
    if place:
        x, y, bw, bh = F.placement_box((w, h), float(place["scale"]), float(place["ox"]), float(place["oy"]))
        vis = max(0, min(x + bw, F.WIDTH) - max(x, 0)) * max(0, min(y + bh, F.HEIGHT) - max(y, 0))
        cov = vis / (F.WIDTH * F.HEIGHT)
    elif fit == "pad":
        rw, rh = F.WIDTH / w, F.HEIGHT / h
        cov = min(rw, rh) / max(rw, rh)     # share of the panel covered by the picture
    else:
        cov = 1.0
    return {"id": fid, "name": _frames[fid][0], "coverage": round(cov, 3),
            "fitted": _png_data_url(fitted_img),
            "preview": _png_data_url(F.render_frame(fr))}


# ---- local settings (screen calibration + per-image placement) ----
SETTINGS_PATH = os.environ.get("HUELEAF_SETTINGS") or os.path.join(os.path.expanduser("~"), ".hueleaf", "settings.json")
_settings_lock = threading.Lock()
MAX_PLACEMENTS = 500


def load_settings(path: str | None = None) -> dict:
    try:
        with open(path or SETTINGS_PATH, encoding="utf-8") as f:
            d = json.load(f)
        return d if isinstance(d, dict) else {}
    except (OSError, ValueError):
        return {}


def save_settings(patch: dict, path: str | None = None) -> dict:
    """Merge patch into the settings file. 'placements' entries merge by key
    (value None deletes); oldest entries are dropped beyond MAX_PLACEMENTS."""
    path = path or SETTINGS_PATH
    with _settings_lock:
        d = load_settings(path)
        for k, v in patch.items():
            if k == "placements" and isinstance(v, dict):
                pl = d.get("placements") or {}
                for pk, pv in v.items():
                    pl.pop(pk, None)
                    if pv is not None:
                        pl[pk] = pv
                while len(pl) > MAX_PLACEMENTS:
                    pl.pop(next(iter(pl)))
                d["placements"] = pl
            else:
                d[k] = v
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        tmp = path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(d, f, ensure_ascii=False)
        os.replace(tmp, path)
        return d


def start_send(fid: str, name: str, port: str, show: bool = False) -> str:
    if fid not in _frames:
        raise KeyError("convert first")
    display = (name or "").strip() or _frames[fid][0]
    name = P.card_name(display)
    fr = _frames[fid][1]
    claim_port(port, "send")
    jid = uuid.uuid4().hex
    job = _jobs[jid] = {"done": 0, "total": len(fr), "state": "running", "name": name, "error": None}

    def run():
        state, error = "ok", None
        def prog(done, total):
            job["done"] = done
        try:
            def go(d):
                info = json.loads(d.ok("INFO"))
                if not info.get("upload_no_refresh"):
                    raise UpgradeRequired("设备固件不支持静默上传，请先升级到 1.4.0 或更新版本")
                if not info.get("ready"):
                    raise RuntimeError("device not ready")
                if not d.upload_image(name, fr, display, progress=prog):
                    raise UpgradeRequired("图片已保存，但名称写入失败，请升级设备固件")
                if show:
                    d.ok("SHOW", name.encode())
            with_device(port, go)
        except Exception as e:
            log_error("send", e)
            state, error = "error", friendly_error(e)
        finally:
            release_port(port)
            job.update(state=state, error=error)
    try:
        threading.Thread(target=run, daemon=True).start()
    except Exception:
        release_port(port)
        _jobs.pop(jid, None)
        raise
    return jid


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def _send(self, code, body, ctype="application/json"):
        if not isinstance(body, bytes):
            body = json.dumps(body).encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _route(self, method):
        from urllib.parse import urlparse, parse_qs
        u = urlparse(self.path)
        q = {k: v[0] for k, v in parse_qs(u.query).items()}
        body = {}
        if method == "POST":
            # same-origin only: block other web pages from driving the device
            origin = self.headers.get("Origin")
            if origin and origin != f"http://{self.headers.get('Host')}":
                return self._send(403, {"error": "cross-origin"})
            n = int(self.headers.get("Content-Length") or 0)
            body = json.loads(self.rfile.read(n) or b"{}")
        try:
            r = u.path
            if method == "GET" and r == "/":
                return self._send(200, PAGE.encode(), "text/html; charset=utf-8")
            if method == "GET" and r == "/api/ports":
                return self._send(200, detected_ports())
            if method == "POST" and r == "/api/convert":
                return self._send(200, convert(body["image"], body.get("filename", ""),
                                               body.get("mode", "photo"), body.get("fit", "crop"),
                                               body.get("orientation", "portrait"), body.get("place")))
            if method == "GET" and r == "/api/settings":
                d = load_settings()
                return self._send(200, {"settings": d, "panel_mm": F.PANEL_MM,
                                        "panel_px": [F.WIDTH, F.HEIGHT]})
            if method == "POST" and r == "/api/settings":
                save_settings(body)
                return self._send(200, {"ok": True})
            if method == "POST" and r == "/api/send":
                return self._send(200, {"job": start_send(body["id"], body.get("name"), body["port"],
                                                          body.get("show", False))})
            if method == "POST" and r == "/api/reboot":
                return self._send(200, reboot_device(body["port"]))
            if method == "GET" and r == "/api/job":
                return self._send(200, _jobs[q["id"]])
            if method == "GET" and r == "/api/list":
                if not q.get("port"):
                    return self._send(200, {"connected": False, "items": []})
                names, labels = with_device(q["port"], lambda d: (d.list(), d.labels()))
                return self._send(200, {"connected": True,
                                        "items": [{"id": n, "label": labels.get(n) or display_name(n)} for n in names]})
            if method == "GET" and r == "/api/info":
                kw = {"retries": 1, "timeout": 1} if q.get("probe") else {}
                return self._send(200, json.loads(with_device(q["port"], lambda d: d.ok("INFO", **kw))))
            if method == "POST" and r == "/api/delete":
                change_device(body["port"], lambda d: d.delete(body["name"]))
                return self._send(200, {"ok": True})
            if method == "POST" and r == "/api/show":
                change_device(body["port"], lambda d: d.ok("SHOW", body["name"].encode()))
                return self._send(200, {"ok": True})
            if method == "POST" and r == "/api/order":
                change_device(body["port"], lambda d: d.set_order(body["names"]))
                return self._send(200, {"ok": True})
            return self._send(404, {"error": "not found"})
        except PortBusy as e:
            return self._send(409, {"error": friendly_error(e)})
        except Exception as e:
            log_error(u.path, e)
            return self._send(500, {"error": friendly_error(e)})

    def do_GET(self):
        self._route("GET")

    def do_POST(self):
        self._route("POST")


PAGE = r"""<!doctype html><html lang="zh"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>HueLeaf 相框</title><style>
*{box-sizing:border-box}
body{font:14px -apple-system,"PingFang SC","Microsoft YaHei",sans-serif;margin:0;background:#f4f3e8;color:#111412}
header{background:#111412;color:#f4f3e8;padding:10px 20px}
.bar{max-width:1400px;margin:0 auto;display:flex;gap:12px;align-items:center;flex-wrap:wrap}
header h1{font-size:17px;margin:0 16px 0 0}
#notice{color:#e7c83f}
main{max-width:1400px;margin:0 auto;display:grid;grid-template-columns:minmax(0,1fr) 320px;gap:20px;padding:16px 20px}
@media (max-width:1000px){main{grid-template-columns:1fr}}
#drop{border:2px dashed #888;border-radius:8px;padding:14px;text-align:center;cursor:pointer;background:#fff}
#drop.over{border-color:#315eae;background:#eef}
.ctl{display:flex;gap:16px;flex-wrap:wrap;margin:12px 0;align-items:center;justify-content:center}
.pair{display:flex;gap:20px;justify-content:center;flex-wrap:wrap}
.pair figure{margin:0;text-align:center}
/* 2:3 panel; as large as fits (max 480x720 = 1 CSS px per panel px) */
.pane{height:min(720px,calc(100vh - 290px));min-height:360px;aspect-ratio:2/3;background:#e2e0d4;border:1px solid #999;
 display:flex;align-items:center;justify-content:center;color:#888;overflow:hidden}
.pane img{width:100%;height:100%;image-rendering:pixelated;cursor:zoom-in;display:block}
.pane img:not([src]){display:none}
figcaption{margin-top:6px;color:#444}
button{padding:6px 12px;border-radius:6px;border:1px solid #555;background:#fff;cursor:pointer;font:inherit}
button.primary{background:#315eae;color:#fff;border-color:#315eae}
button:disabled{opacity:.5;cursor:default}
progress{width:220px}
progress.failed{accent-color:#c63b35}
#hint{color:#8a6d00;text-align:center;min-height:1.2em}
#msg{min-height:1.2em;color:#c63b35;text-align:center}
#ok{color:#398451}
aside h3{margin:4px 0 8px}
#cards{list-style:none;padding:0;margin:8px 0;max-height:65vh;overflow:auto;background:#fff;border:1px solid #ccc;min-height:60px}
#cards li{display:flex;align-items:center;gap:4px;padding:5px 6px;border-bottom:1px solid #eee}
#cards li .n{color:#888;width:2em;text-align:right}
#cards li span.t{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
#cards li button{padding:2px 6px}
#cards li.empty{color:#777;justify-content:center;padding:18px}
#cards li.err{color:#c63b35}
.edit{position:relative;overflow:hidden;padding:36px;background:#bdbbb0;border-radius:6px;touch-action:none;cursor:grab;user-select:none}
.edit.drag{cursor:grabbing}
.edit .pane{position:relative;overflow:visible;background:#fff;height:min(720px,calc(100vh - 360px))}
#raw,#dimg{position:absolute;max-width:none;pointer-events:none;image-rendering:auto;cursor:inherit}
#dimg{left:0;top:0;width:100%;height:100%;image-rendering:pixelated;z-index:2}
#mask{position:absolute;inset:0;z-index:3;pointer-events:none;outline:2px solid #111412;box-shadow:0 0 0 3000px rgba(189,187,176,.93);transition:box-shadow .15s}
.edit.drag #mask{box-shadow:0 0 0 3000px rgba(189,187,176,.5)}
.edit.drag #dimg,#dimg.stale{display:none}
.tools{display:flex;gap:6px;flex-wrap:wrap;justify-content:center;align-items:center;margin-top:8px}
#calib{position:fixed;inset:0;background:rgba(0,0,0,.6);display:none;z-index:10;align-items:center;justify-content:center}
#calib.on{display:flex}
#calib>div{background:#fff;padding:20px;border-radius:8px;max-width:90vw;text-align:center}
#card{margin:14px auto;border:2px solid #315eae;border-radius:3.18mm;background:#eef;display:flex;align-items:center;justify-content:center;color:#315eae}
#zoom .zc{position:fixed;top:12px;left:16px;color:#ddd}
#zoom{position:fixed;inset:0;background:rgba(0,0,0,.85);display:none;overflow:auto;z-index:9;cursor:zoom-out}
#zoom.on{display:block}
#zoom img{image-rendering:pixelated;display:block;margin:20px auto}
#zoom .zb{position:fixed;top:12px;right:16px;display:flex;gap:8px}
</style></head><body>
<header><div class="bar"><h1>HueLeaf 相框</h1>
<label>设备 <select id="port"></select></label><button id="rescan">重新检测</button>
<span id="notice"></span><span id="dev"></span></div></header>
<main><section>
<div id="drop">把图片拖到这里，或点击选择文件<input id="file" type="file" accept="image/*" hidden></div>
<div class="ctl">
<label>模式 <select id="mode"><option value="photo">照片（抖动）</option><option value="fine">精细模式（实验）</option><option value="illustration">插画（纯色块）</option></select></label>
<label><input type="checkbox" id="showd" checked> 框内显示 6 色效果</label>
</div>
<div id="hint"></div>
<div class="pair"><figure><div class="edit" id="edit"><div class="pane" id="frame"><img id="raw" alt=""><img id="dimg" class="stale" alt=""><div id="mask"></div><span id="ph">还没有图片</span></div></div>
<div class="tools"><button id="bfill">铺满</button><button id="bfit">完整显示</button><button id="breset">居中复位</button><button id="brot">旋转 90°</button>
<label>缩放 <input type="range" id="zs" min="-230" max="300" value="0" style="width:120px"></label><span id="zpct"></span></div>
<figcaption>拖动移动图片，滚轮/滑块缩放；框内即为相框显示范围</figcaption></figure>
<figure><div class="pane"><img id="prev" alt="屏幕效果"><span>还没有图片</span></div><figcaption>屏幕效果（6 色，点击看实际尺寸）</figcaption></figure></div>
<div class="ctl"><label>名称 <input id="name" size="22"></label>
<button class="primary" id="send" disabled>发送到相框</button><button id="reboot" disabled>完成并重启</button>
<progress id="bar" max="1" value="0"></progress><span id="pct"></span></div>
<p id="uploadhint" style="color:#666;text-align:center">可逐张连续发送，上传时屏幕保持原样。全部完成后点「完成并重启」，重启后继续显示原图。</p>
<div id="msg"></div>
</section><aside><h3>相框里的图片</h3>
<button id="refresh">刷新</button> <button id="saveorder" disabled>保存顺序</button>
<ul id="cards"></ul><p style="color:#666">用 ↑↓ 调整播放顺序，然后点「保存顺序」。</p></aside></main>
<div id="zoom"><div class="zc" id="zcap"></div><div class="zb"><button data-z="mm">实际尺寸</button><button data-z="px">像素 1:1</button><button data-z="2">2×</button><button data-z="cal">校准尺寸</button><button data-z="0">关闭</button></div><img id="zimg"></div>
<div id="calib"><div><b>校准屏幕尺寸</b><p>拿一张银行卡（85.6 × 53.98 mm）贴在屏幕上，拖动滑块直到蓝框和卡片一样大。</p>
<div id="card">银行卡</div><input type="range" id="calr" min="1.5" max="12" step="0.01" style="width:320px"> <span id="calv"></span>
<p><button id="calreset">恢复默认</button> <button class="primary" id="calok">保存</button> <button id="calno">取消</button></p></div></div>
<script>
const $=id=>document.getElementById(id);let cur=null,src=null,fname="",items=[],connected=false,sending=false,restarting=false;
let deviceInfo=null,infoSeenAt=0,orderDirty=false,refreshSeq=0;
const msg=t=>$("msg").textContent=t||"";
const NOPORT="未检测到 HueLeaf，请插上 USB";
const busy=()=>sending||restarting;
function syncControls(){const ready=connected&&deviceInfo&&deviceInfo.ready!==false;
 $("send").disabled=!cur||busy()||!ready||!deviceInfo.upload_no_refresh;
 $("reboot").disabled=busy()||!ready;$("port").disabled=busy();$("rescan").disabled=busy();$("refresh").disabled=busy();
 $("saveorder").disabled=busy()||!ready||!orderDirty;
 $("uploadhint").textContent=connected&&deviceInfo&&!deviceInfo.upload_no_refresh?"设备固件不支持静默上传，请先升级到 1.4.0 或更新版本":"可逐张连续发送，上传时屏幕保持原样。全部完成后点「完成并重启」，重启后继续显示原图。";
 for(const b of $("cards").querySelectorAll("button"))b.disabled=busy()||!ready;}
async function api(path,body){let r;try{r=await fetch(path,body?{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(body)}:{});}
 catch(e){throw new Error("界面和后台程序断开了，请重新运行启动脚本");}
 const j=await r.json().catch(()=>({error:"操作失败，请重试"}));if(!r.ok)throw new Error(j.error||"操作失败，请重试");return j;}
let lastPorts="";
async function ports(manual){let j;try{j=await api("/api/ports");}catch(e){return;}
 if(busy())return j;
 const key=JSON.stringify(j.ports);const s=$("port");const had=s.value;
 if(key!==lastPorts||manual){lastPorts=key;s.innerHTML="";
  const ph=document.createElement("option");ph.value="";ph.textContent=j.ports.length?"— 选择端口 —":"（无可用端口）";s.appendChild(ph);
  for(const p of j.ports){const o=document.createElement("option");o.value=p.device;o.textContent=(p.hueleaf?"★ HueLeaf ":"")+p.device+(p.desc&&p.desc!=="n/a"?" "+p.desc:"");s.appendChild(o);}
  s.value=[...s.options].some(o=>o.value===had)&&had?had:"";}
 if(!s.value&&j.default){s.value=j.default;}
 $("notice").textContent=j.ports.some(p=>p.hueleaf)?"":NOPORT;
 if(s.value!==had||manual)await refresh();return j;}
function setPane(img,url){img.src=url;if(img.nextElementSibling&&img.nextElementSibling.tagName==="SPAN")img.nextElementSibling.style.display="none";}
/* ---- free placement editor: panel px coordinates, same rounding as hlformat.placement_box ---- */
const PW=480,PH=720;let PMM=[52.1,78.2],pxmm=96/25.4,settings={};
let rawImg=null,rot=0,sc=1,ox=0,oy=0,ikey="",rotUrl="",rw=0,rh=0;
function rotDims(){return rot%180?[rawImg.naturalHeight,rawImg.naturalWidth]:[rawImg.naturalWidth,rawImg.naturalHeight];}
function preset(m){[rw,rh]=rotDims();sc=(m==="pad"?Math.min:Math.max)(PW/rw,PH/rh);ox=(PW-rw*sc)/2;oy=(PH-rh*sc)/2;}
function fillScale(){return Math.max(PW/rw,PH/rh);}
function rotImage(){const c=document.createElement("canvas");[rw,rh]=rotDims();c.width=rw;c.height=rh;const g=c.getContext("2d");
 g.translate(rw/2,rh/2);g.rotate(rot*Math.PI/180);g.drawImage(rawImg,-rawImg.naturalWidth/2,-rawImg.naturalHeight/2);
 $("raw").src=rot?c.toDataURL("image/jpeg",.9):rawImg.src;}
function layout(){if(!rawImg)return;const k=$("frame").clientWidth/PW,r=$("raw");
 const x=Math.round(ox),y=Math.round(oy),w=Math.max(1,Math.round(rw*sc)),h=Math.max(1,Math.round(rh*sc));
 Object.assign(r.style,{left:x*k+"px",top:y*k+"px",width:w*k+"px",height:h*k+"px"});
 const z=sc/fillScale();$("zs").value=Math.round(Math.log(z)*100);$("zpct").textContent=Math.round(z*100)+"%";}
let ctimer=0;
function changed(){layout();invalidate();clearTimeout(ctimer);ctimer=setTimeout(()=>{savePlace();convert();},350);}
function savePlace(){if(!ikey)return;const v={rot,scale:sc,ox,oy};settings.placements=settings.placements||{};settings.placements[ikey]=v;
 api("/api/settings",{placements:{[ikey]:v}}).catch(()=>{});}
function zoomAt(f,px,py){const s2=Math.min(Math.max(sc*f,fillScale()*0.1),fillScale()*20);f=s2/sc;ox=px-(px-ox)*f;oy=py-(py-oy)*f;sc=s2;changed();}
(()=>{const e=$("edit");let d=null;
 e.addEventListener("pointerdown",ev=>{if(!rawImg)return;d={x:ev.clientX,y:ev.clientY,ox,oy};e.setPointerCapture(ev.pointerId);e.classList.add("drag");});
 e.addEventListener("pointermove",ev=>{if(!d)return;const k=$("frame").clientWidth/PW;ox=d.ox+(ev.clientX-d.x)/k;oy=d.oy+(ev.clientY-d.y)/k;layout();});
 const up=()=>{if(!d)return;d=null;e.classList.remove("drag");changed();};e.addEventListener("pointerup",up);e.addEventListener("pointercancel",up);
 e.addEventListener("wheel",ev=>{if(!rawImg)return;ev.preventDefault();const b=$("frame").getBoundingClientRect(),k=b.width/PW;
  zoomAt(Math.exp(-ev.deltaY*0.0015),(ev.clientX-b.left)/k,(ev.clientY-b.top)/k);},{passive:false});})();
$("zs").oninput=()=>{if(!rawImg)return;zoomAt(fillScale()*Math.exp($("zs").value/100)/sc,PW/2,PH/2);};
$("bfill").onclick=()=>{if(rawImg){preset("crop");changed();}};$("bfit").onclick=()=>{if(rawImg){preset("pad");changed();}};
$("breset").onclick=()=>{if(rawImg){rot=0;rotImage();preset("crop");changed();}};
$("brot").onclick=()=>{if(!rawImg)return;const cx=(PW/2-ox)/sc,cy=(PH/2-oy)/sc,[w0,h0]=[rw,rh];  /* keep the frame centre on the same picture point */
 rot=(rot+90)%360;rotImage();const nx=h0-cy,ny=cx;ox=PW/2-nx*sc;oy=PH/2-ny*sc;changed();};
window.addEventListener("resize",layout);
$("showd").onchange=()=>$("dimg").style.visibility=$("showd").checked?"":"hidden";
async function imgKey(dataUrl,f){try{const h=await crypto.subtle.digest("SHA-1",new TextEncoder().encode(dataUrl));
 return [...new Uint8Array(h)].map(b=>b.toString(16).padStart(2,"0")).join("");}catch(e){return f.name+":"+f.size+":"+f.lastModified;}}
let cseq=0,converting=false,pendingConversion=false,loading=false,loadseq=0;
function invalidate(){++cseq;cur=null;syncControls();$("dimg").classList.add("stale");}
async function convert(){if(!src||loading)return;invalidate();if(converting){pendingConversion=true;return;}converting=true;const my=cseq;msg("");$("hint").textContent=$("mode").value==="fine"?"精细转换中，请稍候（首次使用需要准备计算组件）…":"转换中…";
 try{const j=await api("/api/convert",{image:src,filename:fname,mode:$("mode").value,place:{rotate:(360-rot)%360,scale:sc,ox,oy}});
  if(my!==cseq)return;   /* a newer conversion is in flight */
  cur=j.id;$("dimg").src=j.preview;$("dimg").classList.remove("stale");setPane($("prev"),j.preview);if(!$("name").dataset.touched)$("name").value=j.name;syncControls();
  $("hint").textContent=j.coverage<0.75?"留白较多，画面会显得很小：可以点「铺满」或「旋转 90°」。":"";}
 catch(e){if(my===cseq){$("hint").textContent="";msg(e.message);}}
 finally{converting=false;if(pendingConversion){pendingConversion=false;convert();}}}
function load(f){if(!f)return;if(!f.type.startsWith("image/"))return msg("请拖入图片文件（JPG/PNG 等）");const mine=++loadseq;loading=true;invalidate();clearTimeout(ctimer);$("name").dataset.touched="";
 const fail=()=>{if(mine===loadseq){loading=false;src=null;msg("无法识别这个图片文件");}};
 const r=new FileReader();r.onerror=fail;r.onload=async()=>{const data=r.result,key=await imgKey(data,f);if(mine!==loadseq)return;const im=new Image();
  im.onload=()=>{if(mine!==loadseq)return;loading=false;src=data;fname=f.name;ikey=key;rawImg=im;$("ph").style.display="none";const p=(settings.placements||{})[ikey];
   if(p&&p.scale>0){rot=p.rot||0;rotImage();sc=p.scale;ox=p.ox;oy=p.oy;}else{rot=0;rotImage();preset("crop");}
   layout();$("dimg").classList.add("stale");convert();};
  im.onerror=fail;im.src=data;};r.readAsDataURL(f);}
$("drop").onclick=()=>$("file").click();$("file").onchange=e=>load(e.target.files[0]);
["dragover","dragenter"].forEach(ev=>$("drop").addEventListener(ev,e=>{e.preventDefault();$("drop").classList.add("over");}));
["dragleave","drop"].forEach(ev=>$("drop").addEventListener(ev,e=>{e.preventDefault();$("drop").classList.remove("over");}));
document.addEventListener("dragover",e=>e.preventDefault());document.addEventListener("drop",e=>{e.preventDefault();load(e.dataTransfer.files[0]);});
$("mode").onchange=convert;$("name").oninput=()=>$("name").dataset.touched=1;
$("rescan").onclick=()=>ports(true);
let zmode="mm";
function zoom(z){if(z==="0"||z===0){$("zoom").classList.remove("on");return;}if(z==="cal")return openCal();zmode=z;let w,h,cap;
 if(z==="mm"){w=PMM[0]*pxmm;h=PMM[1]*pxmm;cap=`实际尺寸 ${PMM[0].toFixed(1)} × ${PMM[1].toFixed(1)} mm（不准请点「校准尺寸」）`;}
 else if(z==="px"){w=PW/devicePixelRatio;h=PH/devicePixelRatio;cap="像素 1:1：一个屏幕像素 = 相框一个像素";}
 else{w=PW*2;h=PH*2;cap="2× 放大";}
 $("zimg").style.width=w+"px";$("zimg").style.height=h+"px";$("zcap").textContent=cap;$("zoom").classList.add("on");}
$("prev").onclick=()=>{$("zimg").src=$("prev").src;zoom("mm");};
$("zoom").onclick=e=>{const z=e.target.dataset&&e.target.dataset.z;if(z!==undefined)zoom(z);else if(e.target.id==="zoom")zoom(0);else zoom(zmode==="mm"?"px":"mm");};
function calShow(v){$("card").style.width=85.6*v+"px";$("card").style.height=53.98*v+"px";$("calv").textContent=(v*25.4).toFixed(0)+" px/英寸";}
function openCal(){$("calr").value=pxmm;calShow(pxmm);$("calib").classList.add("on");}
$("calr").oninput=()=>calShow(+$("calr").value);$("calreset").onclick=()=>{$("calr").value=96/25.4;calShow(96/25.4);};
$("calno").onclick=()=>$("calib").classList.remove("on");
$("calok").onclick=()=>{pxmm=+$("calr").value;settings.px_per_mm=pxmm;api("/api/settings",{px_per_mm:pxmm}).catch(e=>msg(e.message));
 $("calib").classList.remove("on");if($("zoom").classList.contains("on"))zoom(zmode);};
api("/api/settings").then(j=>{settings=j.settings||{};PMM=j.panel_mm;if(settings.px_per_mm>0)pxmm=settings.px_per_mm;}).catch(()=>{});
document.addEventListener("keydown",e=>{if(e.key==="Escape"){zoom(0);$("calib").classList.remove("on");}});
$("send").onclick=async()=>{const port=$("port").value,bar=$("bar");if(!port)return msg(NOPORT);if($("send").disabled||busy())return;sending=true;++refreshSeq;syncControls();msg("");
 bar.classList.remove("failed");bar.value=0;$("pct").textContent="连接中…";
 try{const {job}=await api("/api/send",{id:cur,name:$("name").value,port,show:false});
  for(;;){const j=await api("/api/job?id="+job);bar.value=j.done/j.total;$("pct").textContent=j.done===j.total?"校验并保存中…":Math.floor(100*j.done/j.total)+"%";
   if(j.state==="ok"){$("pct").textContent="✓ 已保存，可继续发送下一张";break;}
   if(j.state==="error")throw new Error(j.error); await new Promise(r=>setTimeout(r,250));}}
 catch(e){bar.classList.add("failed");bar.value=0;$("pct").textContent="✗ 发送失败";msg(e.message);}
 finally{sending=false;syncControls();await refresh();}};
$("reboot").onclick=async()=>{if(busy()||$("reboot").disabled)return;
 if(!confirm("照片已逐张保存。确定现在重启相框吗？重启后继续显示原来的图片。"))return;
 const originalPort=$("port").value,baseline=deviceInfo&&deviceInfo.uptime_ms,seenAt=infoSeenAt;
 restarting=true;++refreshSeq;connected=false;syncControls();msg("");$("pct").textContent="正在请求重启…";
 try{const result=await api("/api/reboot",{port:originalPort});
  $("pct").textContent=result.uncertain?"重启确认未收到，正在检测设备…":"相框正在重启，等待重新连接…";
  const deadline=Date.now()+180000;let found=false;
  while(Date.now()<deadline){await new Promise(r=>setTimeout(r,2000));if(Date.now()>=deadline)break;
   try{const p=await api("/api/ports"),hits=p.ports.filter(x=>x.hueleaf);const candidate=p.ports.some(x=>x.device===originalPort)?originalPort:hits.length===1?hits[0].device:null;if(!candidate)continue;
    const i=await api("/api/info?probe=1&port="+encodeURIComponent(candidate));
    const reset=Number.isFinite(baseline)&&Number.isFinite(i.uptime_ms)?i.uptime_ms<baseline+Date.now()-seenAt-500:!result.uncertain;
    if(i.ready===true&&reset){found=true;$("pct").textContent="✓ 已重启并重新连接";break;}}
   catch(e){/* USB may disappear and return while the first frame is drawn. */}}
  if(!found){$("pct").textContent="重启结果未确认";msg("等待设备重连超时，请点「重新检测」。重启命令不会自动重发。");}}
 catch(e){$("pct").textContent="✗ 重启请求失败";msg(e.message);}
 finally{restarting=false;syncControls();await ports(true);}};
function empty(text,cls){$("cards").innerHTML="";const li=document.createElement("li");li.className="empty "+(cls||"");li.textContent=text;$("cards").appendChild(li);}
function draw(){const ul=$("cards");if(!items.length)return empty("卡上还没有图片");ul.innerHTML="";items.forEach((it,i)=>{const li=document.createElement("li");
 li.innerHTML="<span class=n></span><span class=t></span><button title='上移'>↑</button><button title='下移'>↓</button><button title='在相框上显示'>显示</button><button title='删除'>✕</button>";
 li.children[0].textContent=(i+1)+".";li.children[1].textContent=it.label;li.children[1].title=it.label;const b=li.querySelectorAll("button");
 const mv=d=>{const k=i+d;if(busy()||k<0||k>=items.length)return;[items[k],items[i]]=[items[i],items[k]];orderDirty=true;draw();syncControls();};
 b[0].onclick=()=>mv(-1);b[1].onclick=()=>mv(1);
 b[2].onclick=()=>api("/api/show",{port:$("port").value,name:it.id}).then(()=>msg("")).catch(e=>msg(e.message));
 b[3].onclick=async()=>{if(!confirm("从相框删除「"+it.label+"」？"))return;try{await api("/api/delete",{port:$("port").value,name:it.id});refresh();}catch(e){msg(e.message);}};
 ul.appendChild(li);});syncControls();}
async function refresh(){if(busy())return;const mine=++refreshSeq,port=$("port").value;orderDirty=false;connected=false;deviceInfo=null;syncControls();$("dev").textContent="";
 if(!port){items=[];return empty("未连接设备");}
 empty("读取中…");
 try{const i=await api("/api/info?port="+encodeURIComponent(port));if(mine!==refreshSeq||busy())return;
  deviceInfo=i;infoSeenAt=Date.now();connected=true;syncControls();$("dev").textContent=`固件 ${i.fw} · 电量 ${i.battery<0?"—":i.battery+"%"} · 剩余 ${Math.round(i.free_kb/1024)} MB`;
  if(i.ready===false)return empty("设备正在启动，请稍后刷新");
  if(!i.upload_no_refresh)msg("设备固件不支持静默上传，请先升级到 1.4.0 或更新版本");
  const j=await api("/api/list?port="+encodeURIComponent(port));if(mine!==refreshSeq||busy())return;connected=j.connected;items=j.items;draw();}
 catch(e){if(mine!==refreshSeq||busy())return;connected=false;syncControls();empty("读取失败："+e.message,"err");}}
$("refresh").onclick=refresh;$("port").onchange=refresh;
$("saveorder").onclick=async()=>{try{await api("/api/order",{port:$("port").value,names:items.map(x=>x.id)});orderDirty=false;syncControls();msg("");}catch(e){msg(e.message);}};
ports(true);setInterval(()=>ports(false),3000);
</script></body></html>"""

DEFAULT_PORT = None


def default_port(ports) -> str | None:
    """Preselect only an explicit --port or a likely HueLeaf; never a random system port."""
    if DEFAULT_PORT:
        return DEFAULT_PORT
    hits = [d for d, _, hit in ports if hit]
    return hits[0] if hits else None


def main(port=None, listen_port=0, open_browser=True):
    global DEFAULT_PORT
    DEFAULT_PORT = port
    srv = ThreadingHTTPServer(("127.0.0.1", listen_port), H)
    url = f"http://127.0.0.1:{srv.server_address[1]}/"
    print(f"HueLeaf GUI: {url}  (Ctrl+C 退出)")
    if open_browser:
        threading.Timer(0.3, webbrowser.open, (url,)).start()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser(description="HueLeaf desktop GUI (local web page)")
    ap.add_argument("-p", "--port", default=os.environ.get("HUELEAF_PORT"), help="serial port")
    ap.add_argument("--listen", type=int, default=0, help="HTTP port (default: random free)")
    ap.add_argument("--no-browser", action="store_true")
    a = ap.parse_args()
    sys.exit(main(a.port, a.listen, not a.no_browser))
