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


import traceback


def friendly_error(e: BaseException) -> str:
    """Plain-Chinese message for the UI; details go to the terminal only."""
    t = f"{type(e).__name__}: {e}".lower()
    if isinstance(e, TimeoutError) or "no valid reply" in t:
        return "设备没有响应：请确认相框已开机、已刷新版固件，然后重试"
    if "crc" in t or "verify mismatch" in t:
        return "传输校验失败（数据在 USB 线上出错），请重试或换一根数据线"
    if any(k in t for k in ("errno 13", "permission", "access is denied", "errno 2", "no such file",
                            "could not open port", "errno 16", "busy", "filenotfound",
                            "device not configured", "errno 6", "serialexception", "write failed")):
        return "连不上设备：没插好或没权限（也可能被其他程序占用）"
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


def with_device(port: str, fn):
    with _dev_lock:
        try:
            return fn(device(port))
        except Exception:
            d = _devs.pop(port, None)        # reopen next time (unplugged etc.)
            if d:
                d.close()
            raise


def convert(data_url: str, filename: str, mode: str, fit: str, orientation: str) -> dict:
    raw = base64.b64decode(data_url.split(",", 1)[-1])
    img = Image.open(io.BytesIO(raw))
    img.load()
    rot = F.orientation_rotate(img, orientation)
    ids, fr = F.convert(img, mode=fit, style=mode, orientation=orientation)
    fid = uuid.uuid4().hex
    _frames[fid] = (P.display_stem(filename or "img"), fr)
    if len(_frames) > 50:
        _frames.pop(next(iter(_frames)))
    fitted_img = F.fit_image(img, rot, fit)
    cov = 1.0
    if fit == "pad":
        w, h = ImageOps.exif_transpose(img).size
        if rot % 180:
            w, h = h, w
        rw, rh = F.WIDTH / w, F.HEIGHT / h
        cov = min(rw, rh) / max(rw, rh)     # share of the panel covered by the picture
    return {"id": fid, "name": _frames[fid][0], "coverage": round(cov, 3),
            "fitted": _png_data_url(fitted_img),
            "preview": _png_data_url(F.render_frame(fr))}


def start_send(fid: str, name: str, port: str, show: bool) -> str:
    if fid not in _frames:
        raise KeyError("convert first")
    display = (name or "").strip() or _frames[fid][0]
    name = P.card_name(display)
    fr = _frames[fid][1]
    jid = uuid.uuid4().hex
    job = _jobs[jid] = {"done": 0, "total": len(fr), "state": "running", "name": name, "error": None}

    def run():
        def prog(done, total):
            job["done"] = done
        try:
            def go(d):
                d.upload_image(name, fr, display, progress=prog)
                if show:
                    d.ok("SHOW", name.encode())
            with_device(port, go)
            job["state"] = "ok"
        except Exception as e:
            log_error("send", e)
            job["state"], job["error"] = "error", friendly_error(e)
    threading.Thread(target=run, daemon=True).start()
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
                ports = [{"device": d, "desc": s, "hueleaf": h} for d, s, h in P.find_ports()]
                return self._send(200, {"ports": ports, "default": default_port(P.find_ports())})
            if method == "POST" and r == "/api/convert":
                return self._send(200, convert(body["image"], body.get("filename", ""),
                                               body.get("mode", "photo"), body.get("fit", "crop"),
                                               body.get("orientation", "portrait")))
            if method == "POST" and r == "/api/send":
                return self._send(200, {"job": start_send(body["id"], body.get("name"), body["port"],
                                                          body.get("show", True))})
            if method == "GET" and r == "/api/job":
                return self._send(200, _jobs[q["id"]])
            if method == "GET" and r == "/api/list":
                if not q.get("port"):
                    return self._send(200, {"connected": False, "items": []})
                names, labels = with_device(q["port"], lambda d: (d.list(), d.labels()))
                return self._send(200, {"connected": True,
                                        "items": [{"id": n, "label": labels.get(n) or display_name(n)} for n in names]})
            if method == "GET" and r == "/api/info":
                return self._send(200, json.loads(with_device(q["port"], lambda d: d.ok("INFO"))))
            if method == "POST" and r == "/api/delete":
                with_device(body["port"], lambda d: d.delete(body["name"]))
                return self._send(200, {"ok": True})
            if method == "POST" and r == "/api/show":
                with_device(body["port"], lambda d: d.ok("SHOW", body["name"].encode()))
                return self._send(200, {"ok": True})
            if method == "POST" and r == "/api/order":
                with_device(body["port"], lambda d: d.set_order(body["names"]))
                return self._send(200, {"ok": True})
            return self._send(404, {"error": "not found"})
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
<label>模式 <select id="mode"><option value="photo">照片（抖动）</option><option value="illustration">插画（纯色块）</option></select></label>
<label>适配 <select id="fit"><option value="crop">裁切铺满</option><option value="pad">完整显示（留白边）</option></select></label>
<label>方向 <select id="orientation"><option value="portrait">竖屏</option><option value="landscape">横屏（旋转 90°）</option><option value="auto">自动</option></select></label>
</div>
<div id="hint"></div>
<div class="pair"><figure><div class="pane"><img id="orig" alt="裁切后"><span>还没有图片</span></div><figcaption>裁切后</figcaption></figure>
<figure><div class="pane"><img id="prev" alt="屏幕效果"><span>还没有图片</span></div><figcaption>屏幕效果（6 色，点击放大）</figcaption></figure></div>
<div class="ctl"><label>名称 <input id="name" size="22"></label>
<button class="primary" id="send" disabled>发送到相框</button><progress id="bar" max="1" value="0"></progress><span id="pct"></span></div>
<div id="msg"></div>
</section><aside><h3>相框里的图片</h3>
<button id="refresh">刷新</button> <button id="saveorder" disabled>保存顺序</button>
<ul id="cards"></ul><p style="color:#666">用 ↑↓ 调整播放顺序，然后点「保存顺序」。</p></aside></main>
<div id="zoom"><div class="zb"><button data-z="1">1:1</button><button data-z="2">2×</button><button data-z="0">关闭</button></div><img id="zimg"></div>
<script>
const $=id=>document.getElementById(id);let cur=null,src=null,fname="",items=[],connected=false;
const msg=t=>$("msg").textContent=t||"";
const NOPORT="未检测到 HueLeaf，请插上 USB";
async function api(path,body){let r;try{r=await fetch(path,body?{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(body)}:{});}
 catch(e){throw new Error("界面和后台程序断开了，请重新运行启动脚本");}
 const j=await r.json().catch(()=>({error:"操作失败，请重试"}));if(!r.ok)throw new Error(j.error||"操作失败，请重试");return j;}
let lastPorts="";
async function ports(manual){let j;try{j=await api("/api/ports");}catch(e){return;}
 const key=JSON.stringify(j.ports);const s=$("port");const had=s.value;
 if(key!==lastPorts||manual){lastPorts=key;s.innerHTML="";
  const ph=document.createElement("option");ph.value="";ph.textContent=j.ports.length?"— 选择端口 —":"（无可用端口）";s.appendChild(ph);
  for(const p of j.ports){const o=document.createElement("option");o.value=p.device;o.textContent=(p.hueleaf?"★ HueLeaf ":"")+p.device+(p.desc&&p.desc!=="n/a"?" "+p.desc:"");s.appendChild(o);}
  s.value=[...s.options].some(o=>o.value===had)&&had?had:"";}
 if(!s.value&&j.default){s.value=j.default;}
 $("notice").textContent=j.ports.some(p=>p.hueleaf)?"":NOPORT;
 if(s.value!==had||manual)refresh();}
function setPane(img,url){img.src=url;img.nextElementSibling.style.display="none";}
let cseq=0;
async function convert(){if(!src)return;const my=++cseq;msg("");$("hint").textContent="转换中…";$("send").disabled=true;
 try{const j=await api("/api/convert",{image:src,filename:fname,mode:$("mode").value,fit:$("fit").value,orientation:$("orientation").value});
  if(my!==cseq)return;   /* a newer conversion is in flight */
  cur=j.id;setPane($("orig"),j.fitted);setPane($("prev"),j.preview);if(!$("name").dataset.touched)$("name").value=j.name;$("send").disabled=false;
  $("hint").textContent=j.coverage<0.75?"留白较多，画面会显得很小：可以改成「裁切铺满」"+($("orientation").value!=="portrait"?"或「竖屏」":"或「横屏」")+"。":"";}
 catch(e){if(my===cseq){$("hint").textContent="";msg(e.message);}}}
function load(f){if(!f)return;if(!f.type.startsWith("image/"))return msg("请拖入图片文件（JPG/PNG 等）");fname=f.name;$("name").dataset.touched="";
 const r=new FileReader();r.onload=()=>{src=r.result;convert();};r.readAsDataURL(f);}
$("drop").onclick=()=>$("file").click();$("file").onchange=e=>load(e.target.files[0]);
["dragover","dragenter"].forEach(ev=>$("drop").addEventListener(ev,e=>{e.preventDefault();$("drop").classList.add("over");}));
["dragleave","drop"].forEach(ev=>$("drop").addEventListener(ev,e=>{e.preventDefault();$("drop").classList.remove("over");}));
document.addEventListener("dragover",e=>e.preventDefault());document.addEventListener("drop",e=>{e.preventDefault();load(e.dataTransfer.files[0]);});
["mode","fit","orientation"].forEach(id=>$(id).onchange=convert);$("name").oninput=()=>$("name").dataset.touched=1;
$("rescan").onclick=()=>ports(true);
function zoom(z){if(!z){$("zoom").classList.remove("on");return;}$("zimg").style.width=480*z+"px";$("zimg").style.height=720*z+"px";$("zoom").classList.add("on");}
["orig","prev"].forEach(id=>$(id).onclick=()=>{$("zimg").src=$(id).src;zoom(1);});
$("zoom").onclick=e=>{const z=e.target.dataset&&e.target.dataset.z;if(z!==undefined)zoom(+z);else if(e.target.id==="zoom")zoom(0);else zoom($("zimg").style.width==="480px"?2:1);};
document.addEventListener("keydown",e=>{if(e.key==="Escape")zoom(0);});
$("send").onclick=async()=>{const port=$("port").value,bar=$("bar");if(!port)return msg(NOPORT);$("send").disabled=true;msg("");
 bar.classList.remove("failed");bar.value=0;$("pct").textContent="连接中…";
 try{const {job}=await api("/api/send",{id:cur,name:$("name").value,port,show:true});
  for(;;){const j=await api("/api/job?id="+job);bar.value=j.done/j.total;$("pct").textContent=Math.floor(100*j.done/j.total)+"%";
   if(j.state==="ok"){$("pct").textContent="✓ 已发送并显示";refresh();break;}
   if(j.state==="error")throw new Error(j.error); await new Promise(r=>setTimeout(r,250));}}
 catch(e){bar.classList.add("failed");bar.value=0;$("pct").textContent="✗ 发送失败";msg(e.message);}
 $("send").disabled=false;};
function empty(text,cls){$("cards").innerHTML="";const li=document.createElement("li");li.className="empty "+(cls||"");li.textContent=text;$("cards").appendChild(li);}
function draw(){const ul=$("cards");if(!items.length)return empty("卡上还没有图片");ul.innerHTML="";items.forEach((it,i)=>{const li=document.createElement("li");
 li.innerHTML="<span class=n></span><span class=t></span><button title='上移'>↑</button><button title='下移'>↓</button><button title='在相框上显示'>显示</button><button title='删除'>✕</button>";
 li.children[0].textContent=(i+1)+".";li.children[1].textContent=it.label;li.children[1].title=it.label;const b=li.querySelectorAll("button");
 const mv=d=>{const k=i+d;if(k<0||k>=items.length)return;[items[k],items[i]]=[items[i],items[k]];draw();$("saveorder").disabled=false;};
 b[0].onclick=()=>mv(-1);b[1].onclick=()=>mv(1);
 b[2].onclick=()=>api("/api/show",{port:$("port").value,name:it.id}).then(()=>msg("")).catch(e=>msg(e.message));
 b[3].onclick=async()=>{if(!confirm("从相框删除「"+it.label+"」？"))return;try{await api("/api/delete",{port:$("port").value,name:it.id});refresh();}catch(e){msg(e.message);}};
 ul.appendChild(li);});}
async function refresh(){const port=$("port").value;$("saveorder").disabled=true;$("dev").textContent="";
 if(!port){connected=false;items=[];return empty("未连接设备");}
 empty("读取中…");
 try{const j=await api("/api/list?port="+encodeURIComponent(port));connected=j.connected;items=j.items;draw();
  try{const i=await api("/api/info?port="+encodeURIComponent(port));$("dev").textContent=`固件 ${i.fw} · 电量 ${i.battery<0?"—":i.battery+"%"} · 剩余 ${Math.round(i.free_kb/1024)} MB`;}catch(e){}}
 catch(e){connected=false;empty("读取失败："+e.message,"err");}}
$("refresh").onclick=refresh;$("port").onchange=refresh;
$("saveorder").onclick=async()=>{try{await api("/api/order",{port:$("port").value,names:items.map(x=>x.id)});$("saveorder").disabled=true;msg("");}catch(e){msg(e.message);}};
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
