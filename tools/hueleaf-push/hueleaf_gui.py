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

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hlformat as F
import hueleaf_push as P

_frames: dict[str, tuple[str, bytes]] = {}     # id -> (suggested name, frame)
_jobs: dict[str, dict] = {}
_dev_lock = threading.Lock()
_devs: dict[str, P.Device] = {}


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
    _frames[fid] = (P.safe_name(filename or "img"), fr)
    if len(_frames) > 50:
        _frames.pop(next(iter(_frames)))
    return {"id": fid, "name": _frames[fid][0],
            "fitted": _png_data_url(F.fit_image(img, rot, fit)),
            "preview": _png_data_url(F.render_frame(fr))}


def start_send(fid: str, name: str, port: str, show: bool) -> str:
    if fid not in _frames:
        raise KeyError("convert first")
    name = P.safe_name(name or _frames[fid][0])
    fr = _frames[fid][1]
    jid = uuid.uuid4().hex
    job = _jobs[jid] = {"done": 0, "total": len(fr), "state": "running", "name": name, "error": None}

    def run():
        def prog(done, total):
            job["done"] = done
        try:
            def go(d):
                d.upload(0, name, fr, progress=prog)
                if show:
                    d.ok("SHOW", name.encode())
            with_device(port, go)
            job["state"] = "ok"
        except Exception as e:
            job["state"], job["error"] = "error", str(e)
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
                return self._send(200, {"ports": ports, "default": DEFAULT_PORT or P.auto_port()})
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
                return self._send(200, {"names": with_device(q["port"], lambda d: d.list())})
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
            return self._send(500, {"error": f"{type(e).__name__}: {e}"})

    def do_GET(self):
        self._route("GET")

    def do_POST(self):
        self._route("POST")


PAGE = r"""<!doctype html><html lang="zh"><head><meta charset="utf-8">
<title>HueLeaf 相框</title><style>
body{font:14px -apple-system,"PingFang SC","Microsoft YaHei",sans-serif;margin:0;background:#f4f3e8;color:#111412}
header{background:#111412;color:#f4f3e8;padding:10px 16px;display:flex;gap:12px;align-items:center;flex-wrap:wrap}
header h1{font-size:16px;margin:0 12px 0 0}
main{display:grid;grid-template-columns:1fr 300px;gap:16px;padding:16px}
#drop{border:2px dashed #888;border-radius:8px;padding:18px;text-align:center;cursor:pointer;background:#fff}
#drop.over{border-color:#315eae;background:#eef}
.pair{display:flex;gap:12px;margin-top:12px;justify-content:center}
.pair figure{margin:0;text-align:center}
.pair img{width:320px;height:480px;object-fit:contain;background:#ddd;border:1px solid #999;image-rendering:pixelated}
.ctl{display:flex;gap:14px;flex-wrap:wrap;margin:12px 0;align-items:center}
button{padding:6px 12px;border-radius:6px;border:1px solid #555;background:#fff;cursor:pointer}
button.primary{background:#315eae;color:#fff;border-color:#315eae}
button:disabled{opacity:.5;cursor:default}
progress{width:220px}
#cards li{display:flex;align-items:center;gap:4px;padding:4px;border-bottom:1px solid #ddd}
#cards li span{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
#cards li button{padding:2px 6px}
#cards{list-style:none;padding:0;margin:0;max-height:70vh;overflow:auto;background:#fff;border:1px solid #ccc}
#msg{min-height:1.2em;color:#c63b35}
</style></head><body>
<header><h1>HueLeaf 相框</h1>
<label>设备端口 <select id="port"></select></label><button id="rescan">刷新端口</button>
<span id="dev"></span></header>
<main><section>
<div id="drop">把图片拖到这里，或点击选择文件<input id="file" type="file" accept="image/*" hidden></div>
<div class="ctl">
<label>模式 <select id="mode"><option value="photo">照片（抖动）</option><option value="illustration">插画（纯色块）</option></select></label>
<label>适配 <select id="fit"><option value="crop">裁切铺满</option><option value="pad">完整显示（留白边）</option></select></label>
<label>方向 <select id="orientation"><option value="portrait">竖屏</option><option value="landscape">横屏（旋转 90°）</option><option value="auto">自动</option></select></label>
</div>
<div class="pair"><figure><img id="orig"><figcaption>原图（按裁切/方向适配后）</figcaption></figure>
<figure><img id="prev"><figcaption>屏幕效果预览（6 色）</figcaption></figure></div>
<div class="ctl"><label>文件名 <input id="name" size="24"></label>
<button class="primary" id="send" disabled>发送到设备</button><progress id="bar" max="1" value="0"></progress><span id="pct"></span></div>
<div id="msg"></div>
</section><aside><h3>卡上的图片（显示顺序）</h3>
<button id="refresh">刷新列表</button> <button id="saveorder" disabled>保存顺序</button>
<ul id="cards"></ul><p style="color:#666">↑↓ 调整顺序后点“保存顺序”（写入 img/index.txt）。</p></aside></main>
<script>
const $=id=>document.getElementById(id);let cur=null,src=null,fname="",names=[];
const msg=t=>$("msg").textContent=t||"";
async function api(path,body){const r=await fetch(path,body?{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(body)}:{});
 const j=await r.json();if(!r.ok)throw new Error(j.error||r.status);return j;}
async function ports(){const j=await api("/api/ports");const s=$("port");s.innerHTML="";
 for(const p of j.ports){const o=document.createElement("option");o.value=p.device;o.textContent=(p.hueleaf?"★ ":"")+p.device+" "+p.desc;s.appendChild(o);}
 if(j.default)s.value=j.default; if(!j.ports.length)s.innerHTML="<option value=''>未找到设备</option>";}
async function convert(){if(!src)return;msg("转换中…");$("send").disabled=true;
 try{const j=await api("/api/convert",{image:src,filename:fname,mode:$("mode").value,fit:$("fit").value,orientation:$("orientation").value});
  cur=j.id;$("orig").src=j.fitted;$("prev").src=j.preview;if(!$("name").dataset.touched)$("name").value=j.name;$("send").disabled=false;msg("");}
 catch(e){msg("转换失败："+e.message);}}
function load(f){if(!f||!f.type.startsWith("image/"))return msg("请拖入图片文件");fname=f.name;$("name").dataset.touched="";
 const r=new FileReader();r.onload=()=>{src=r.result;convert();};r.readAsDataURL(f);}
$("drop").onclick=()=>$("file").click();$("file").onchange=e=>load(e.target.files[0]);
["dragover","dragenter"].forEach(ev=>$("drop").addEventListener(ev,e=>{e.preventDefault();$("drop").classList.add("over");}));
["dragleave","drop"].forEach(ev=>$("drop").addEventListener(ev,e=>{e.preventDefault();$("drop").classList.remove("over");}));
$("drop").addEventListener("drop",e=>load(e.dataTransfer.files[0]));
document.addEventListener("dragover",e=>e.preventDefault());document.addEventListener("drop",e=>{e.preventDefault();if(e.target.id!=="drop")load(e.dataTransfer.files[0]);});
["mode","fit","orientation"].forEach(id=>$(id).onchange=convert);$("name").oninput=()=>$("name").dataset.touched=1;
$("rescan").onclick=ports;
$("send").onclick=async()=>{const port=$("port").value;if(!port)return msg("请先连接设备并选择端口");$("send").disabled=true;msg("");
 try{const {job}=await api("/api/send",{id:cur,name:$("name").value,port,show:true});
  for(;;){const j=await api("/api/job?id="+job);$("bar").value=j.done/j.total;$("pct").textContent=Math.floor(100*j.done/j.total)+"%";
   if(j.state==="ok"){$("pct").textContent="已发送 "+j.name;refresh();break;}
   if(j.state==="error"){msg("发送失败："+j.error);break;} await new Promise(r=>setTimeout(r,250));}}
 catch(e){msg("发送失败："+e.message);} $("send").disabled=false;};
function draw(){const ul=$("cards");ul.innerHTML="";names.forEach((n,i)=>{const li=document.createElement("li");
 li.innerHTML="<span></span><button title='上移'>↑</button><button title='下移'>↓</button><button title='在设备上显示'>显示</button><button title='删除'>✕</button>";
 li.firstChild.textContent=n;const b=li.querySelectorAll("button");
 b[0].onclick=()=>{if(i>0){[names[i-1],names[i]]=[names[i],names[i-1]];draw();$("saveorder").disabled=false;}};
 b[1].onclick=()=>{if(i<names.length-1){[names[i+1],names[i]]=[names[i],names[i+1]];draw();$("saveorder").disabled=false;}};
 b[2].onclick=()=>api("/api/show",{port:$("port").value,name:n}).catch(e=>msg(e.message));
 b[3].onclick=async()=>{if(!confirm("从 TF 卡删除 "+n+"？"))return;try{await api("/api/delete",{port:$("port").value,name:n});refresh();}catch(e){msg("删除失败："+e.message);}};
 ul.appendChild(li);});}
async function refresh(){const port=$("port").value;if(!port)return;
 try{names=(await api("/api/list?port="+encodeURIComponent(port))).names;draw();$("saveorder").disabled=true;
  const i=await api("/api/info?port="+encodeURIComponent(port));$("dev").textContent=`固件 ${i.fw} · 电量 ${i.battery}% · 剩余 ${Math.round(i.free_kb/1024)} MB`;msg("");}
 catch(e){msg("读取设备失败："+e.message);}}
$("refresh").onclick=refresh;$("port").onchange=refresh;
$("saveorder").onclick=async()=>{try{await api("/api/order",{port:$("port").value,names});$("saveorder").disabled=true;}catch(e){msg("保存失败："+e.message);}};
ports().then(refresh);
</script></body></html>"""

DEFAULT_PORT = None


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
