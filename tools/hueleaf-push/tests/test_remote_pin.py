import json
from http.server import ThreadingHTTPServer
from pathlib import Path
import sys
import threading
import urllib.error
import urllib.request

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import hlble
import hueleaf_gui as gui
import hueleaf_push as push


class Device:
    def __init__(self, fail=None):
        self.calls = []
        self.fail = fail

    def ok(self, name, payload=b"", **kwargs):
        self.calls.append((name, payload, kwargs))
        if self.fail == payload:
            raise TimeoutError("lost reply")
        return b'{"layout":"remote-v1"}'


@pytest.fixture
def endpoint(monkeypatch):
    device = Device()
    monkeypatch.setattr(gui, "device", lambda port: device)
    server = ThreadingHTTPServer(("127.0.0.1", 0), gui.H)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    origin = f"http://127.0.0.1:{server.server_port}"
    def post(data, *, request_origin=None):
        req = urllib.request.Request(origin + "/api/remote-pin", data=json.dumps(data).encode(),
                                     headers={"Content-Type": "application/json", "Origin": request_origin or origin}, method="POST")
        try:
            with urllib.request.urlopen(req, timeout=5) as response:
                return response.status, json.load(response)
        except urllib.error.HTTPError as error:
            return error.code, json.load(error)
    yield device, post
    server.shutdown()
    server.server_close()
    thread.join(timeout=5)


def test_pin_vector_and_validation():
    assert hlble.pin_key("0123").hex() == "444499cd627ce855496569777bea9ba3e96d9c4cdcd6001846343747b4d1db93"
    for pin in ("123", "１２３４", "abcd", 1234):
        with pytest.raises(ValueError):
            hlble.pin_key(pin)


def test_usb_pin_provisions_then_enables_without_returning_secret(endpoint):
    device, post = endpoint
    status, result = post({"port": "FAKE", "pin": "0123"})
    assert status == 200 and result == {"ok": True}
    assert device.calls == [("REMOTE", b"\x00", {"retries": 1}),
                            ("REMOTE", b"\x01" + hlble.pin_key("0123"), {"retries": 1}),
                            ("REMOTE", b"\x04\x01", {"retries": 1})]


def test_bad_pin_and_cross_origin_do_not_touch_usb(endpoint):
    device, post = endpoint
    assert post({"port": "FAKE", "pin": "12a4"})[0] == 400
    assert post({"port": "FAKE", "pin": "0123"}, request_origin="http://evil.example")[0] == 403
    assert device.calls == []


def test_uncertain_write_is_reported_and_does_not_enable(endpoint):
    device, post = endpoint
    device.fail = b"\x01" + hlble.pin_key("0123")
    status, result = post({"port": "FAKE", "pin": "0123"})
    assert status == 500
    assert "可能已写入" in result["error"]
    assert len(device.calls) == 2
