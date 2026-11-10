#!/usr/bin/env python3
"""HueLeaf desktop GUI: a local web page served by the Python stdlib.

    python3 hueleaf_gui.py            # opens http://127.0.0.1:<port>/ in your browser

Why a browser UI: no extra dependency (Tkinter is missing/broken on several
macOS Python builds and can't preview drag-and-drop without add-ons), it looks
the same on macOS and Windows, and drag-and-drop of images is native in every
browser. Frontend files live in web/ beside this script; no frontend build is
required. The server only listens on 127.0.0.1.
"""
from __future__ import annotations

import base64, io, json, os, re, sys, threading, uuid, webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from PIL import Image, ImageOps

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hlformat as F
import hlble
import hueleaf_push as P

_frames: dict[str, tuple[str, bytes]] = {}     # id -> (suggested name, frame)
_jobs: dict[str, dict] = {}
_dev_lock = threading.Lock()
_devs: dict[str, P.Device] = {}
_op_lock = threading.Lock()
_operations: dict[str, str] = {}

# Explicit routes keep unrelated files and local settings out of the web server.
WEB_DIR = Path(__file__).resolve().parent / "web"
WEB_ASSETS = {
    "/": ("index.html", "text/html; charset=utf-8"),
    "/web/styles.css": ("styles.css", "text/css; charset=utf-8"),
    "/web/app.js": ("app.js", "text/javascript; charset=utf-8"),
}


class PortBusy(RuntimeError):
    pass


class UpgradeRequired(RuntimeError):
    pass


class SettingsVerificationError(RuntimeError):
    pass


import traceback


def friendly_error(e: BaseException) -> str:
    """Plain-Chinese message for the UI; details go to the terminal only."""
    if isinstance(e, (PortBusy, UpgradeRequired, SettingsVerificationError)):
        return str(e)
    t = f"{type(e).__name__}: {e}".lower()
    if "device busy" in t:
        return "相框正在刷新、传图或维护，请稍后重试；传图期间请先点「结束传图」再操作显示设置"
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
    if "out of memory" in t:
        return "设备可用内存不足，定时设置未保存；请升级包含内存修复的固件"
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
        except (P.DeviceError, UpgradeRequired, SettingsVerificationError):
            # A valid error reply (e.g. not ready/not found) is not a broken
            # USB link. Reopening can reset the device on affected hosts.
            raise
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


def upload_session_device(port: str, action: int) -> dict:
    def go(d):
        info = json.loads(d.ok("INFO"))
        if not info.get("upload_session_supported"):
            return {"active": False, "supported": False}
        if action == 2 and not info.get("upload_session_active"):
            return {"active": False, "supported": True}
        d.upload_session(action)
        return {"active": action != 0, "supported": True}
    return change_device(port, go)


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
                    raise P.DeviceError("upload", -8)
                if info.get("upload_session_supported") and not show:
                    d.upload_session(2 if info.get("upload_session_active") else 1)
                    job["session_active"] = True
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
            if method == "GET" and r in WEB_ASSETS:
                filename, content_type = WEB_ASSETS[r]
                try:
                    content = (WEB_DIR / filename).read_bytes()
                except FileNotFoundError:
                    return self._send(404, {"error": "界面文件缺失，请确认 web/ 目录完整"})
                return self._send(200, content, content_type)
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
            if method == "POST" and r == "/api/remote-pin":
                host = self.headers.get("Host", "")
                allowed_hosts = {f"127.0.0.1:{self.server.server_port}", f"localhost:{self.server.server_port}"}
                if (host not in allowed_hosts or self.headers.get("Origin") != f"http://{host}"
                        or self.headers.get_content_type() != "application/json"):
                    return self._send(403, {"error": "请从本机 HueLeaf 界面设置 PIN"})
                if (not isinstance(body, dict) or not isinstance(body.get("port"), str)
                        or not body["port"].strip() or not isinstance(body.get("pin"), str)
                        or not re.fullmatch(r"[0-9]{4}", body["pin"])):
                    return self._send(400, {"error": "请选择设备并输入4位数字 PIN"})
                key = hlble.pin_key(body["pin"])
                def set_remote_pin(d):
                    current = json.loads(d.ok("REMOTE", b"\x00", retries=1))
                    if current.get("layout") != "remote-v1":
                        raise UpgradeRequired("设备未运行 remote-v1 固件，不能设置蓝牙遥控 PIN")
                    # A lost ACK or a failed enable can leave a partially applied setup.
                    try:
                        d.ok("REMOTE", b"\x01" + key, retries=1)
                        d.ok("REMOTE", b"\x04\x01", retries=1)
                    except (TimeoutError, OSError, P.DeviceError) as e:
                        raise SettingsVerificationError("PIN 可能已写入但未完全确认；请保持 USB 连接，重新设置相同 PIN 后再重启") from e
                change_device(body["port"], set_remote_pin)
                return self._send(200, {"ok": True})
            if method == "POST" and r == "/api/session":
                if (not isinstance(body, dict) or type(body.get("action")) is not int
                        or body["action"] not in (0, 2) or not isinstance(body.get("port"), str)
                        or not body["port"].strip()):
                    return self._send(400, {"error": "无效的传图会话参数"})
                return self._send(200, upload_session_device(body["port"], body["action"]))
            if method == "GET" and r == "/api/job":
                return self._send(200, _jobs[q["id"]])
            if method == "GET" and r == "/api/list":
                if not q.get("port"):
                    return self._send(200, {"connected": False, "items": []})
                def inventory(d):
                    info = json.loads(d.ok("INFO"))
                    if info.get("slideshow_selection_supported"):
                        selection = d.slideshow_selection()
                        labels = d.labels()
                        return {"connected": True, "selection": {k: selection[k] for k in ("token", "custom", "fault")},
                                "items": [{**it, "label": labels.get(it["id"]) or display_name(it["id"])}
                                          for it in selection["items"]]}
                    names, labels = d.list(), d.labels()
                    return {"connected": True, "items": [{"id": n, "label": labels.get(n) or display_name(n)} for n in names]}
                return self._send(200, with_device(q["port"], inventory))
            if method == "GET" and r == "/api/info":
                kw = {"retries": 1, "timeout": 1} if q.get("probe") else {}
                return self._send(200, json.loads(with_device(q["port"], lambda d: d.ok("INFO", **kw))))
            if method == "POST" and r == "/api/strip":
                if (not isinstance(body, dict) or not isinstance(body.get("on"), bool)
                        or not isinstance(body.get("port"), str) or not body["port"].strip()):
                    return self._send(400, {"error": "请选择设备，并提供有效的状态栏开关值"})
                change_device(body["port"], lambda d: d.ok("STRIP", bytes([body["on"]])))
                return self._send(200, {"ok": True})
            if method == "POST" and r == "/api/slideshow":
                if (not isinstance(body, dict) or type(body.get("enabled")) is not bool
                        or type(body.get("interval_min")) is not int
                        or not 1 <= body["interval_min"] <= 10080
                        or ("tap_wake" in body and type(body["tap_wake"]) is not bool)
                        or not isinstance(body.get("port"), str) or not body["port"].strip()):
                    return self._send(400, {"error": "请选择设备；换图间隔必须是1～10080的整数分钟"})
                def set_slideshow(d):
                    info = json.loads(d.ok("INFO"))
                    if not info.get("slideshow_supported"):
                        raise UpgradeRequired("设备固件尚不支持定时深睡轮播，请升级固件")
                    if "tap_wake" in body and not info.get("tap_wake_supported"):
                        raise UpgradeRequired("设备固件尚不支持拍击唤醒，请升级固件")
                    d.set_slideshow(body["enabled"], body["interval_min"], body.get("tap_wake"))
                    saved = json.loads(d.ok("INFO"))
                    expected = {"slideshow_enabled": body["enabled"],
                                "slideshow_interval_min": body["interval_min"]}
                    if "tap_wake" in body:
                        expected["tap_wake"] = body["tap_wake"]
                    if any(type(saved.get(key)) is not type(value) or saved[key] != value
                           for key, value in expected.items()):
                        raise SettingsVerificationError("设备读回的定时设置与提交值不一致，未确认保存成功；请重新读取设备状态后重试")
                    return saved
                saved = change_device(body["port"], set_slideshow)
                return self._send(200, {"ok": True, "info": saved})
            if method == "POST" and r == "/api/slideshow-selection":
                if (not isinstance(body, dict) or not isinstance(body.get("port"), str)
                        or not body["port"].strip() or not isinstance(body.get("token"), str)
                        or not re.fullmatch(r"[0-9a-f]{16}", body["token"])
                        or not isinstance(body.get("names"), list) or len(body["names"]) > 300
                        or any(not isinstance(n, str) for n in body["names"])
                        or len(set(body["names"])) != len(body["names"])):
                    return self._send(400, {"error": "无效的轮播名单，请刷新后重试"})
                def save_selection(d):
                    info = json.loads(d.ok("INFO"))
                    if not info.get("slideshow_selection_supported"):
                        raise UpgradeRequired("设备固件尚不支持指定轮播图片，请升级固件")
                    snapshot = d.slideshow_selection()
                    if snapshot["token"] != body["token"]:
                        raise P.DeviceError("SET_SLIDESHOW_SELECTION", -9)
                    if not set(body["names"]) <= {it["id"] for it in snapshot["items"]}:
                        raise P.DeviceError("SET_SLIDESHOW_SELECTION", -5)
                    d.set_slideshow_selection(snapshot, body["names"])
                    return d.slideshow_selection()
                selection = change_device(body["port"], save_selection)
                return self._send(200, {"ok": True, "selection": selection})
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
        except P.DeviceError as e:
            return self._send(409 if e.status == -9 else 500,
                              {"error": friendly_error(e), "device_status": e.status})
        except Exception as e:
            log_error(u.path, e)
            return self._send(500, {"error": friendly_error(e)})

    def do_GET(self):
        self._route("GET")

    def do_POST(self):
        self._route("POST")


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
    missing = [name for name, _ in WEB_ASSETS.values() if not (WEB_DIR / name).is_file()]
    if missing:
        print(f"界面文件缺失：{', '.join(missing)}；请保留 hueleaf_gui.py 同目录的 web/ 文件夹", file=sys.stderr)
        return 1
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
