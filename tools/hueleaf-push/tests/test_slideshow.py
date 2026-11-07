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


class SlideshowDevice(FakeHueLeaf):
    def __init__(self):
        super().__init__(["photo.bin"], fw="1.5.0")
        self.supported = True
        self.tap_supported, self.tap_wake = True, False
        self.payloads = []
        self.enabled, self.minutes, self.write_error = False, 60, 0

    def handle(self, cmd, seq, payload):
        if cmd == P.CMD["INFO"]:
            self.commands.append(cmd)
            return self._reply(cmd, seq, 0, json.dumps({
                "fw": self.fw, "ready": True, "upload_no_refresh": True,
                "upload_session_supported": True, "upload_session_active": self.session_active,
                "slideshow_supported": self.supported, "slideshow_enabled": self.enabled,
                "slideshow_interval_min": self.minutes, "slideshow_state": "usb_paused",
                "tap_wake_supported": self.tap_supported, "tap_wake": self.tap_wake,
            }).encode())
        if cmd == P.CMD["SLIDESHOW"]:
            self.commands.append(cmd)
            if self.session_active:
                return self._reply(cmd, seq, -9)
            if self.write_error:
                return self._reply(cmd, seq, self.write_error)
            self.payloads.append(payload)
            self.enabled, self.minutes = struct.unpack("<BI", payload[:5])
            if len(payload) == 6:
                assert self.tap_supported
                self.tap_wake = bool(payload[5])
            self.enabled = bool(self.enabled)
            return self._reply(cmd, seq, 0)
        return super().handle(cmd, seq, payload)


@pytest.fixture
def slideshow(monkeypatch):
    fake = SlideshowDevice()
    original = P.Device
    monkeypatch.setattr(P, "Device", lambda port: original(fake, timeout=0.02))
    monkeypatch.setattr(G, "_devs", {})
    monkeypatch.setattr(G, "_operations", {})
    server = ThreadingHTTPServer(("127.0.0.1", 0), G.H)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()

    def call(body):
        request = urllib.request.Request(
            f"http://127.0.0.1:{server.server_port}/api/slideshow",
            json.dumps(body).encode(), {"Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(request, timeout=5) as response:
                return response.status, json.load(response)
        except urllib.error.HTTPError as error:
            return error.code, json.load(error)

    yield fake, call
    server.shutdown()
    server.server_close()
    worker.join(timeout=5)
    for device in G._devs.values():
        device.close()


def test_slideshow_persists_without_refresh_or_reboot(slideshow):
    fake, call = slideshow
    for enabled, minutes in [(True, 30), (True, 10080), (False, 10080), (True, 1)]:
        assert call({"port": "FAKE", "enabled": enabled, "interval_min": minutes}) == (200, {"ok": True})
        assert fake.enabled == enabled and fake.minutes == minutes
    assert P.CMD["SHOW"] not in fake.commands and P.CMD["REBOOT"] not in fake.commands
    assert fake.closes == 0 and not G._operations


@pytest.mark.parametrize("body", [None, [], True, {},
    {"port": "FAKE", "enabled": 1, "interval_min": 30},
    {"port": "FAKE", "enabled": True, "interval_min": True},
    {"port": "FAKE", "enabled": True, "interval_min": "30"},
    *({"port": "FAKE", "enabled": True, "interval_min": v} for v in [0, -1, 10081, 1.5]),
    {"port": "FAKE", "enabled": True, "interval_min": 60, "tap_wake": 1},
    {"port": "FAKE", "enabled": True, "interval_min": 60, "tap_wake": None},
    {"port": "", "enabled": False, "interval_min": 60}])
def test_invalid_slideshow_does_not_touch_device(slideshow, body):
    fake, call = slideshow
    assert call(body)[0] == 400
    assert not fake.commands


def test_old_firmware_and_write_failure_keep_settings_and_connection(slideshow):
    fake, call = slideshow
    body = {"port": "FAKE", "enabled": True, "interval_min": 30}
    fake.supported = False
    assert call(body)[0] == 500
    assert P.CMD["SLIDESHOW"] not in fake.commands
    fake.supported = True
    fake.write_error = -7
    status, result = call(body)
    assert status == 500 and result["device_status"] == -7
    assert not fake.enabled and fake.minutes == 60 and not fake.closes
    fake.write_error = 0
    fake.session_active = True
    assert call(body)[0] == 409
    fake.session_active = False
    assert call(body)[0] == 200


def test_slideshow_command_encoding_and_validation():
    fake = SlideshowDevice()
    with_device = P.Device(fake, timeout=0.02)
    with_device.set_slideshow(True, 10080)
    assert fake.enabled and fake.minutes == 10080 and len(fake.payloads[-1]) == 5
    with_device.set_slideshow(True, 30, True)
    assert fake.tap_wake and fake.payloads[-1] == struct.pack("<BIB", 1, 30, 1)
    with_device.set_slideshow(False, 60)
    assert fake.tap_wake and len(fake.payloads[-1]) == 5  # legacy command preserves option
    for invalid in [1, "true", []]:
        before = len(fake.commands)
        with pytest.raises(ValueError):
            with_device.set_slideshow(True, 60, invalid)
        assert len(fake.commands) == before
    for enabled, minutes in [(1, 30), (True, True), (True, 0), (True, 1.5), (False, 10081)]:
        before = len(fake.commands)
        with pytest.raises(ValueError):
            with_device.set_slideshow(enabled, minutes)
        assert len(fake.commands) == before
