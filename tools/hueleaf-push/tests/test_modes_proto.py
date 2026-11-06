import json, os, struct, sys, threading, urllib.request, zlib, base64, io
sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
from PIL import Image, ImageDraw
import pytest
import hlformat as F
import hueleaf_push as P


def gradient():
    img = Image.new("RGB", (600, 900))
    d = ImageDraw.Draw(img)
    for y in range(900):
        d.line([(0, y), (599, y)], fill=(y * 255 // 899, 120, 255 - y * 255 // 899))
    return img


def test_illustration_is_flat_and_photo_dithers():
    img = Image.new("RGB", (480, 720), (0xC6, 0x3B, 0x35))
    ImageDraw.Draw(img).rectangle([100, 100, 300, 500], fill=(0x31, 0x5E, 0xAE))
    ids, _ = F.convert(img, style="illustration")
    assert ids[0] == 3 and ids[300 * 480 + 200] == 5 and set(ids) == {3, 5}
    g = gradient()
    flat, _ = F.convert(g, style="illustration")
    dith, _ = F.convert(g, style="photo")
    changes = lambda a: sum(a[i] != a[i + 1] for i in range(0, len(a) - 1))
    assert changes(dith) > 5 * changes(flat)      # dithering -> many more transitions


def test_preview_uses_only_panel_palette_and_matches_frame():
    ids, fr = F.convert(gradient(), style="photo")
    png = F.render_frame(fr)
    assert png.size == (480, 720)
    assert {c for _, c in png.getcolors(10)} <= {rgb for _, rgb in F.PALETTE}
    assert png.tobytes() == bytes(c for i in ids for c in F.INK_RGB[i])


@pytest.mark.parametrize("style", ["photo", "fine"])
def test_gui_placement_preview_matches_stored_frame(monkeypatch, style):
    import hueleaf_gui as G
    monkeypatch.setattr(G, "_frames", {})
    image = io.BytesIO()
    Image.new("RGB", (120, 180), (128, 128, 128)).save(image, "PNG")
    result = G.convert("data:image/png;base64," + base64.b64encode(image.getvalue()).decode(),
                       "grey.png", style, "crop", "portrait",
                       place={"rotate": 0, "scale": 4, "ox": 0, "oy": 0})
    _, frame = G._frames[result["id"]]
    preview = Image.open(io.BytesIO(base64.b64decode(result["preview"].split(",", 1)[1])))
    assert preview.tobytes() == F.render_frame(frame).tobytes()
    ids = F.unpack(frame)
    assert set(ids) == {0, 1} and 0.22 < ids.count(1) / len(ids) < 0.25


@pytest.mark.parametrize("style", ["photo", "fine"])
def test_cli_preview_and_binary_match(tmp_path, monkeypatch, style):
    source, preview, binary = (tmp_path / name for name in ("grey.png", "preview.png", "frame.bin"))
    Image.new("RGB", (120, 180), (128, 128, 128)).save(source)
    monkeypatch.setattr(F, "CHROMA_W", F.CHROMA_W)
    assert P.main([str(source), "--mode", style, "--chroma", "18",
                   "--preview", str(preview), "--save-bin", str(binary)]) == 0
    frame = binary.read_bytes()
    assert len(frame) == 172800 and F.CHROMA_W == 18
    assert Image.open(preview).tobytes() == F.render_frame(frame).tobytes()
    ids = F.unpack(frame)
    assert set(ids) == {0, 1} and 0.22 < ids.count(1) / len(ids) < 0.25


def test_pack_unpack_roundtrip_random():
    import random
    rnd = random.Random(1)
    inks = sorted(F.VALID_INKS)
    ids = [rnd.choice(inks) for _ in range(F.WIDTH * F.HEIGHT)]
    assert F.unpack(F.pack(ids)) == ids


def test_orientation_and_fit():
    wide = Image.new("RGB", (1200, 800), (0, 0, 0))
    ImageDraw.Draw(wide).rectangle([0, 0, 50, 799], fill=(0xF4, 0xF3, 0xE8))  # white left edge
    assert F.orientation_rotate(wide, "auto") == 270
    assert F.orientation_rotate(wide, "portrait") == 0
    ids, _ = F.convert(wide, style="illustration", orientation="landscape", mode="pad")
    # landscape: image left edge -> panel top (rotated 90 deg clockwise), no border along the long side
    assert ids[5 * 480 + 240] == 1 and ids[700 * 480 + 240] == 0
    # pad of a 3:2 picture in a 2:3 panel without rotation -> white borders top/bottom
    ids, _ = F.convert(Image.new("RGB", (1200, 800), (0, 0, 0)), style="illustration", mode="pad")
    assert ids[5 * 480 + 240] == 1 and ids[360 * 480 + 240] == 0
    ids, _ = F.convert(Image.new("RGB", (1200, 800), (0, 0, 0)), style="illustration", mode="crop")
    assert set(ids) == {0}


class FakeHueLeaf:
    """Serial-like object emulating the firmware protocol (proto.c)."""
    def __init__(self, files=(), corrupt_first=False, fw="1.2.0"):
        self.fw = fw
        self.strip = True
        self.strip_values = []
        self.labels = {}
        self.files = {n: b"\0" * F.FRAME_BYTES for n in files}
        self.index = None
        self.rx, self.tx = b"", b""
        self.corrupt_first = corrupt_first
        self.up = None
        self.commands = []
        self.closes = 0

    @property
    def in_waiting(self):
        return len(self.tx)

    def read(self, n):
        out, self.tx = self.tx[:n], self.tx[n:]
        return out

    def close(self):
        self.closes += 1

    def order(self):
        listed = [n for n in (self.index or []) if n in self.files]
        return listed + sorted(n for n in self.files if n not in listed)

    def write(self, data):
        self.rx += data
        while len(self.rx) >= 16:
            ln = struct.unpack("<I", self.rx[8:12])[0]
            if len(self.rx) < 16 + ln:
                return
            fr, self.rx = self.rx[:16 + ln], self.rx[16 + ln:]
            cmd, seq = fr[4], fr[5]
            assert zlib.crc32(fr[4:12 + ln]) == struct.unpack("<I", fr[12 + ln:])[0]
            if self.corrupt_first:
                self.corrupt_first = False
                self._reply(cmd, seq, -2)
                continue
            self.handle(cmd, seq, fr[12:12 + ln])

    def _reply(self, cmd, seq, st, data=b""):
        self.tx += b"junk" + P.frame(cmd | 0x80, seq, struct.pack("<i", st) + data)

    def handle(self, cmd, seq, p):
        C = P.CMD
        self.commands.append(cmd)
        if cmd == C["INFO"]:
            return self._reply(cmd, seq, 0, json.dumps({"fw": self.fw, "ready": True, "strip": self.strip,
                               "upload_no_refresh": self.fw >= "1.4.0"}).encode())
        if cmd == C["STRIP"]:
            self.strip_values.append(p)
            self.strip = bool(p[0])
            return self._reply(cmd, seq, 0)
        if cmd == C["REBOOT"]:
            return self._reply(cmd, seq, 0)
        if cmd == C["LIST"]:
            return self._reply(cmd, seq, 0, "".join(n + "\n" for n in self.order()).encode())
        if cmd == C["DELETE"]:
            n = p.decode()
            self.labels.pop(n, None)
            return self._reply(cmd, seq, 0 if self.files.pop(n, None) is not None else -4)
        if cmd == C["LABELS"]:
            if self.fw < "1.2.0":
                return self._reply(cmd, seq, -3)
            import hlpicker
            out = "".join(f"{n}\t{hlpicker.parse_label(self.labels[n])[0]}\n" for n in self.order() if n in self.labels)
            return self._reply(cmd, seq, 0, out.encode())
        if cmd == C["SET_ORDER"]:
            names = [l for l in p.decode().split("\n") if l]
            self.index = names or None
            return self._reply(cmd, seq, 0)
        if cmd == C["UP_BEGIN"]:
            kind, size, crc = struct.unpack("<BII", p[:9])
            if kind == 2 and self.fw < "1.2.0":
                return self._reply(cmd, seq, -5)
            self.up = [p[9:].decode(), size, crc, bytearray(), kind]
            return self._reply(cmd, seq, 0)
        if cmd == C["UP_DATA"]:
            self.up[3] += p[4:]
            return self._reply(cmd, seq, 0)
        if cmd == C["UP_END"]:
            ok = zlib.crc32(self.up[3]) == self.up[2]
            if ok:
                (self.labels if self.up[4] == 2 else self.files)[self.up[0]] = bytes(self.up[3])
            return self._reply(cmd, seq, 0 if ok else -2)
        if cmd == C["SHOW"]:
            return self._reply(cmd, seq, 0 if p.decode() in self.files else -4)
        self._reply(cmd, seq, -3)


def test_protocol_list_delete_order_upload():
    fake = FakeHueLeaf(["a.bin", "b.bin", "c.bin"], corrupt_first=True)
    d = P.Device(fake, timeout=0.5)
    assert d.list() == ["a.bin", "b.bin", "c.bin"]       # survives one CRC-error resend
    d.set_order(["c.bin", "a.bin"])
    assert fake.index == ["c.bin", "a.bin"] and d.list() == ["c.bin", "a.bin", "b.bin"]
    d.delete("a.bin")
    assert d.list() == ["c.bin", "b.bin"]
    try:
        d.delete("nope.bin")
        raise AssertionError
    except RuntimeError as e:
        assert "not found" in str(e)
    _, fr = F.convert(gradient(), style="illustration")
    seen = []
    d.upload(0, "new.bin", fr, progress=lambda done, total: seen.append(done))
    assert fake.files["new.bin"] == fr and seen[-1] == len(fr)
    d.set_order([])
    assert fake.index is None
    for bad in ["../x.bin", "x.png", ".hidden.bin"]:
        try:
            d.set_order([bad])
            raise AssertionError(bad)
        except ValueError:
            pass


@pytest.mark.parametrize("style", ["illustration", "fine"])
def test_gui_server_convert_and_device_endpoints(monkeypatch, style):
    import hueleaf_gui as G
    from http.server import ThreadingHTTPServer
    fake = FakeHueLeaf(["a.bin", "b.bin"], fw="1.4.0")
    monkeypatch.setattr(G, "device", lambda port: P.Device(fake, timeout=0.5))
    srv = ThreadingHTTPServer(("127.0.0.1", 0), G.H)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    base = f"http://127.0.0.1:{srv.server_address[1]}"

    def call(path, body=None):
        req = urllib.request.Request(base + path, json.dumps(body).encode() if body is not None else None,
                                     {"Content-Type": "application/json"})
        return json.loads(urllib.request.urlopen(req).read())
    try:
        assert b"HueLeaf" in urllib.request.urlopen(base + "/").read()
        b = io.BytesIO(); gradient().save(b, "PNG")
        r = call("/api/convert", {"image": "data:image/png;base64," + base64.b64encode(b.getvalue()).decode(),
                                  "filename": "海边 My Photo.png", "mode": style, "fit": "pad",
                                  "orientation": "landscape"})
        assert r["name"] == "海边 My Photo" and r["coverage"] < 0.75 and r["preview"].startswith("data:image/png")
        CARD = P.card_name("海边 My Photo")
        job = call("/api/send", {"id": r["id"], "port": "FAKE"})["job"]
        import time
        for _ in range(100):
            j = call("/api/job?id=" + job)
            if j["state"] != "running":
                break
            time.sleep(0.05)
        assert j["state"] == "ok", j
        assert P.CMD["SHOW"] not in fake.commands and P.CMD["REBOOT"] not in fake.commands
        preview = Image.open(io.BytesIO(base64.b64decode(r["preview"].split(",", 1)[1])))
        assert preview.tobytes() == F.render_frame(fake.files[CARD]).tobytes()
        CARD = P.card_name("海边 My Photo")
        assert CARD.startswith("My_Photo-") and CARD.endswith(".bin")
        lst = call("/api/list?port=FAKE")
        assert lst["connected"] and [i["id"] for i in lst["items"]] == [CARD, "a.bin", "b.bin"]
        assert [i["label"] for i in lst["items"]] == ["海边 My Photo", "a", "b"]
        assert call("/api/list?port=") == {"connected": False, "items": []}
        call("/api/order", {"port": "FAKE", "names": ["b.bin", "a.bin"]})
        call("/api/delete", {"port": "FAKE", "name": "a.bin"})
        assert [i["id"] for i in call("/api/list?port=FAKE")["items"]] == ["b.bin", CARD]
        fake.files.clear()
        assert call("/api/list?port=FAKE") == {"connected": True, "items": []}
    finally:
        srv.shutdown()


def test_send_failure_is_plain_chinese(monkeypatch, capsys):
    import hueleaf_gui as G, serial
    def boom(port):
        raise serial.SerialException("[Errno 13] could not open port /dev/ttyS0: Permission denied")
    monkeypatch.setattr(G, "device", boom)
    fid = G.convert("data:," + base64.b64encode(_png()).decode(), "x.png", "photo", "crop", "portrait")["id"]
    job = G.start_send(fid, "", "/dev/ttyS0", True)
    import time
    for _ in range(100):
        if G._jobs[job]["state"] != "running":
            break
        time.sleep(0.02)
    assert G._jobs[job]["state"] == "error"
    assert G._jobs[job]["error"] == "连不上设备：没插好或没权限（也可能被其他程序占用）"
    assert "Traceback" in capsys.readouterr().err       # details in terminal only


def _png():
    b = io.BytesIO(); Image.new("RGB", (60, 90), (200, 50, 50)).save(b, "PNG"); return b.getvalue()


def test_error_mapping():
    import hueleaf_gui as G
    assert G.friendly_error(TimeoutError("LIST: no valid reply")).startswith("设备没有响应")
    assert G.friendly_error(RuntimeError("UP_END: crc/verify mismatch")).startswith("传输校验失败")
    assert G.friendly_error(OSError(16, "Resource busy")).startswith("连不上设备")
    assert G.friendly_error(FileNotFoundError("COM7")).startswith("连不上设备")
    assert G.friendly_error(RuntimeError("SET_ORDER: unknown command")).startswith("设备固件太旧")
    assert "Error" not in G.friendly_error(ZeroDivisionError("x"))


def test_port_preselection_only_hueleaf(monkeypatch):
    import hueleaf_gui as G
    monkeypatch.setattr(G, "DEFAULT_PORT", None)
    assert G.default_port([("/dev/ttyS0", "n/a", False), ("COM3", "Bluetooth", False)]) is None
    assert G.default_port([("/dev/ttyS0", "", False), ("/dev/cu.usbmodem1101", "USB JTAG", True)]) == "/dev/cu.usbmodem1101"
    monkeypatch.setattr(G, "DEFAULT_PORT", "COM9")
    assert G.default_port([]) == "COM9"


def test_card_names_unique_for_cjk():
    a, b = P.card_name("海边日落"), P.card_name("山间晨雾")
    assert a != b and a.isascii() and a.endswith(".bin")
    assert P.card_name("My Photo") == "My_Photo.bin" and P.safe_name("dir/My Photo.png") == "My_Photo.bin"


def test_label_roundtrip_and_ellipsis():
    import hlpicker as K
    name, w, h, data = K.parse_label(K.make_label("海边日落 2024"))
    assert name == "海边日落 2024" and h == K.LABEL_H and 20 < w <= K.LABEL_W
    long = "很长的名字" * 30
    name, w, h, _ = K.parse_label(K.make_label(long))
    assert long.startswith(name) and len(name.encode()) <= 255 and w <= K.LABEL_W   # ellipsis-truncated
    assert len(K.make_label(long)) <= K.LABEL_MAX_BYTES <= 4096


def test_picker_page_is_black_white_with_inverted_cursor():
    import hlpicker as K
    labels = [K.make_label(f"图片 {i}") for i in range(30)]
    im = K.render_page(labels, cursor=17)               # page 2, row 4
    assert {c for _, c in im.getcolors(10)} <= {F.INK_RGB[0], F.INK_RGB[1]}
    y = K.PK_Y0 + 3 * K.PK_ROW_H + 1
    row = [im.getpixel((x, y)) for x in range(F.WIDTH)]
    assert row.count(F.INK_RGB[0]) > 0.9 * F.WIDTH      # selected row = black bar
    y2 = K.PK_Y0 + 4 * K.PK_ROW_H + 1
    assert [im.getpixel((x, y2)) for x in range(F.WIDTH)].count(F.INK_RGB[1]) > 0.9 * F.WIDTH


def test_labels_upload_delete_and_old_firmware():
    fake = FakeHueLeaf(["a.bin"])
    d = P.Device(fake, timeout=0.5)
    _, fr = F.convert(Image.new("RGB", (480, 720), (255, 255, 255)), style="illustration")
    assert d.upload_image(P.card_name("日落"), fr, "日落", progress=False)
    assert d.labels() == {P.card_name("日落"): "日落"}
    d.delete(P.card_name("日落"))
    assert d.labels() == {}
    old = FakeHueLeaf([], fw="1.1.0")
    d = P.Device(old, timeout=0.5)
    assert d.upload_image("x.bin", fr, "x", progress=False) is False and "x.bin" in old.files
    assert d.labels() == {}
