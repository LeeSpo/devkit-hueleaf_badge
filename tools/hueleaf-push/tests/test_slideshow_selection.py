import json
import os
import struct
import sys
import threading
import urllib.error
import urllib.request
from http.server import ThreadingHTTPServer

import pytest

sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
import hueleaf_gui as G
import hueleaf_push as P
from test_modes_proto import FakeHueLeaf


class SelectionDevice(FakeHueLeaf):
    def __init__(self, names=None):
        self.inventory = names or ["a.bin", "b.bin", "c.bin", "d.bin"]
        super().__init__(self.inventory)
        self.selected = set(self.inventory)
        self.token, self.custom, self.fault = 1, False, False
        self.selection_supported = True
        self.pages, self.saved = [], []
        self.write_error = 0
        self.mutate_after_page = False
        self.corrupt_reply = None

    def handle(self, cmd, seq, payload):
        if cmd == P.CMD["INFO"]:
            self.commands.append(cmd)
            return self._reply(cmd, seq, 0, json.dumps({
                "ready": True, "upload_no_refresh": True,
                "upload_session_supported": True, "upload_session_active": self.session_active,
                "slideshow_selection_supported": self.selection_supported,
            }).encode())
        if cmd == P.CMD["SLIDESHOW_SELECTION"]:
            self.commands.append(cmd)
            assert self.selection_supported
            start, token = struct.unpack("<HQ", payload)
            self.pages.append((start, token))
            if token and token != self.token:
                return self._reply(cmd, seq, -9)
            page = self.inventory[start:start + 64]
            raw = struct.pack("<QHHHB", self.token, len(self.inventory), start, len(page),
                              int(self.custom) | int(self.fault) << 1)
            for name in page:
                encoded = name.encode()
                raw += bytes([int(name in self.selected), len(encoded)]) + encoded
            if self.mutate_after_page:
                self.token += 1
            if self.corrupt_reply:
                raw = self.corrupt_reply(raw)
            return self._reply(cmd, seq, 0, raw)
        if cmd == P.CMD["SET_SLIDESHOW_SELECTION"]:
            self.commands.append(cmd)
            if self.write_error:
                return self._reply(cmd, seq, self.write_error)
            if self.session_active:
                return self._reply(cmd, seq, -9)
            token, total = struct.unpack_from("<QH", payload)
            if token != self.token or total != len(self.inventory):
                return self._reply(cmd, seq, -9)
            assert len(payload) == 10 + (total + 7) // 8
            self.selected = {name for i, name in enumerate(self.inventory)
                             if payload[10 + i // 8] & (1 << (i % 8))}
            self.custom, self.fault = True, False
            self.token += 1
            self.saved.append(payload)
            return self._reply(cmd, seq, 0)
        return super().handle(cmd, seq, payload)


@pytest.fixture
def selection_server(monkeypatch):
    fake = SelectionDevice()
    original = P.Device
    monkeypatch.setattr(P, "Device", lambda port: original(fake, timeout=0.02))
    monkeypatch.setattr(G, "_devs", {})
    monkeypatch.setattr(G, "_operations", {})
    server = ThreadingHTTPServer(("127.0.0.1", 0), G.H)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()

    def call(body=None, path="/api/slideshow-selection"):
        request = urllib.request.Request(
            f"http://127.0.0.1:{server.server_port}{path}",
            json.dumps(body).encode() if body is not None else None,
            {"Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(request, timeout=5) as response:
                return response.status, json.load(response)
        except urllib.error.HTTPError as error:
            return error.code, json.load(error)

    yield fake, call
    server.shutdown()
    server.server_close()
    worker.join(timeout=5)
    for d in G._devs.values():
        d.close()


def test_full_300_image_inventory_and_compact_atomic_save():
    names = [f"{i:054d}.bin" for i in range(300)]
    fake = SelectionDevice(names)
    d = P.Device(fake, timeout=0.02)
    snapshot = d.slideshow_selection()
    assert [it["id"] for it in snapshot["items"]] == names
    assert all(it["selected"] for it in snapshot["items"])
    assert len(fake.pages) == 5 and fake.pages[-1][0] == 256
    assert sum(len(n) + 1 for n in names) > 8192
    d.set_slideshow_selection(snapshot, names[::2])
    assert fake.selected == set(names[::2]) and len(fake.saved[-1]) == 48
    # Deleting the GUI object doesn't lose on-device state.
    restarted = P.Device(fake, timeout=0.02).slideshow_selection()
    assert restarted["custom"] and {it["id"] for it in restarted["items"] if it["selected"]} == fake.selected
    for names in (["missing.bin"], ["a.bin", "a.bin"], [1], "a.bin"):
        before = len(fake.commands)
        with pytest.raises(ValueError):
            d.set_slideshow_selection(restarted, names)
        assert len(fake.commands) == before


def test_stale_snapshot_is_never_committed_or_joined():
    fake = SelectionDevice([f"{i}.bin" for i in range(100)])
    d = P.Device(fake, timeout=0.02)
    fake.mutate_after_page = True
    with pytest.raises(P.DeviceError) as caught:
        d.slideshow_selection()
    assert caught.value.status == -9
    fake.mutate_after_page = False
    snapshot = d.slideshow_selection()
    fake.inventory.reverse()
    fake.token += 1
    with pytest.raises(P.DeviceError) as caught:
        d.set_slideshow_selection(snapshot, ["1.bin"])
    assert caught.value.status == -9 and not fake.saved


@pytest.mark.parametrize("corrupt", [lambda raw: raw[:10], lambda raw: raw[:-1],
    lambda raw: raw + b"junk", lambda raw: raw[:15] + b"\x02" + raw[16:],
    lambda raw: raw[:12] + b"\0\0" + raw[14:]])
def test_bad_pages_fail_instead_of_silently_truncating(corrupt):
    fake = SelectionDevice()
    fake.corrupt_reply = corrupt
    with pytest.raises(ValueError):
        P.Device(fake, timeout=0.02).slideshow_selection()


def test_http_inventory_save_none_manual_display_and_failures(selection_server):
    fake, call = selection_server
    code, listing = call(path="/api/list?port=FAKE")
    assert code == 200 and len(listing["items"]) == 4
    assert all(it["selected"] for it in listing["items"])
    body = {"port": "FAKE", "token": listing["selection"]["token"], "names": ["a.bin", "c.bin"]}
    code, saved = call(body)
    assert code == 200 and saved["selection"]["custom"] and fake.selected == {"a.bin", "c.bin"}
    assert not any(cmd in fake.commands for cmd in (P.CMD["SHOW"], P.CMD["REBOOT"], P.CMD["SET_ORDER"]))
    # Unselected images are still present, and an explicit manual show is allowed.
    assert call({"port": "FAKE", "name": "b.bin"}, path="/api/show")[0] == 200
    assert len(fake.inventory) == 4
    stale = body
    body = {**body, "token": saved["selection"]["token"], "names": []}
    assert call(stale)[0] == 409 and fake.selected == {"a.bin", "c.bin"}
    fake.write_error = -7
    assert call(body)[0] == 500 and fake.selected == {"a.bin", "c.bin"}
    fake.write_error = 0
    code, saved = call(body)
    assert code == 200 and not fake.selected and saved["selection"]["custom"]
    fake.fault = True
    body = {**body, "token": saved["selection"]["token"], "names": ["d.bin"]}
    assert call(body)[0] == 200 and not fake.fault and fake.selected == {"d.bin"}


def test_empty_gallery_selection_is_explicit_and_savable(selection_server):
    fake, call = selection_server
    fake.inventory = []
    code, listing = call(path="/api/list?port=FAKE")
    assert code == 200 and not listing["items"]
    body = {"port": "FAKE", "token": listing["selection"]["token"], "names": []}
    code, saved = call(body)
    assert code == 200 and saved["selection"]["custom"] and not saved["selection"]["items"]
    assert len(fake.saved[-1]) == 10


def test_old_firmware_negotiates_without_new_commands(selection_server):
    fake, call = selection_server
    fake.selection_supported = False
    code, listing = call(path="/api/list?port=FAKE")
    assert code == 200 and "selection" not in listing
    assert call({"port": "FAKE", "token": "0000000000000001", "names": []})[0] == 500
    assert not fake.pages and not fake.saved


@pytest.mark.parametrize("body", [{}, {"port": "", "token": "0" * 16, "names": []},
    {"port": "FAKE", "token": "bad", "names": []},
    {"port": "FAKE", "token": "0" * 16, "names": [1]},
    {"port": "FAKE", "token": "0" * 16, "names": ["a.bin", "a.bin"]}])
def test_bad_http_selection_does_not_open_device(selection_server, body):
    fake, call = selection_server
    assert call(body)[0] == 400
    assert not fake.commands
