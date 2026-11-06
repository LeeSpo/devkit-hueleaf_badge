import json
import os
import sys
import threading
import time
import urllib.error
import urllib.request
from http.server import ThreadingHTTPServer

import pytest

sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
import hueleaf_gui as G
import hueleaf_push as P
from test_modes_proto import FakeHueLeaf


@pytest.fixture
def session(monkeypatch):
    fake = FakeHueLeaf(["original.bin"], fw="1.5.0")
    original_device = P.Device
    monkeypatch.setattr(P, "Device", lambda port: original_device(fake, timeout=0.02))
    monkeypatch.setattr(G, "_devs", {})
    monkeypatch.setattr(G, "_operations", {})
    monkeypatch.setattr(G, "_jobs", {})
    monkeypatch.setattr(G, "_frames", {"frame": ("photo", b"\x55" * 172800)})
    srv = ThreadingHTTPServer(("127.0.0.1", 0), G.H)
    thread = threading.Thread(target=srv.serve_forever, daemon=True)
    thread.start()

    def call(path, body=None):
        req = urllib.request.Request(f"http://127.0.0.1:{srv.server_address[1]}" + path,
                                     json.dumps(body).encode() if body is not None else None,
                                     {"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=5) as response:
            return json.load(response)

    yield fake, call
    srv.shutdown()
    srv.server_close()
    thread.join(timeout=5)
    for d in G._devs.values():
        d.close()


def wait_job(call, jid):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        job = call("/api/job?id=" + jid)
        if job["state"] != "running":
            return job
        time.sleep(0.01)
    pytest.fail("upload worker did not finish")


def test_three_uploads_then_finish_without_refresh_or_reboot(session):
    fake, call = session
    for name in ["one", "two", "three"]:
        jid = call("/api/send", {"id": "frame", "name": name, "port": "FAKE"})["job"]
        assert wait_job(call, jid)["state"] == "ok"
        assert fake.files[P.card_name(name)] == b"\x55" * 172800
        assert P.card_name(name) in fake.labels
    assert P.CMD["SHOW"] not in fake.commands
    assert P.CMD["REBOOT"] not in fake.commands
    assert len(call("/api/list?port=FAKE")["items"]) == 4
    assert fake.session_active
    cached = G._devs["FAKE"]
    assert call("/api/session", {"port": "FAKE", "action": 2})["active"]
    assert call("/api/session", {"port": "FAKE", "action": 0}) == {"active": False, "supported": True}
    assert call("/api/session", {"port": "FAKE", "action": 0})["active"] is False
    assert not fake.session_active and G._devs["FAKE"] is cached and fake.closes == 0
    assert P.CMD["REBOOT"] not in fake.commands and P.CMD["SHOW"] not in fake.commands


def test_pending_upload_reserves_port_before_worker_starts(session, monkeypatch):
    fake, call = session
    from types import SimpleNamespace
    workers = []

    class DeferredThread:
        def __init__(self, target, daemon):
            self.target = target

        def start(self):
            workers.append(self.target)

    monkeypatch.setattr(G, "threading", SimpleNamespace(Thread=DeferredThread))
    jid = call("/api/send", {"id": "frame", "port": "FAKE"})["job"]
    for path, body in [("/api/reboot", {"port": "FAKE"}),
                       ("/api/send", {"id": "frame", "port": "FAKE"}),
                       ("/api/strip", {"port": "FAKE", "on": False}),
                       ("/api/session", {"port": "FAKE", "action": 0}),
                       ("/api/show", {"name": "original.bin", "port": "FAKE"})]:
        with pytest.raises(urllib.error.HTTPError) as error:
            call(path, body)
        assert error.value.code == 409
    assert fake.commands == []
    workers.pop()()
    assert wait_job(call, jid)["state"] == "ok"
    assert call("/api/reboot", {"port": "FAKE"})["ok"] is True


def test_strip_switch_uses_existing_command_and_device_state(session):
    fake, call = session
    original_files = dict(fake.files)
    assert call("/api/info?port=FAKE")["strip"] is True
    for on in (False, True):
        assert call("/api/strip", {"port": "FAKE", "on": on}) == {"ok": True}
        assert call("/api/info?port=FAKE")["strip"] is on
    assert fake.strip_values == [b"\x00", b"\x01"]
    assert set(fake.commands) == {P.CMD["INFO"], P.CMD["STRIP"]}
    assert fake.files == original_files and not G._operations


@pytest.mark.parametrize("body", [
    {}, {"port": "FAKE"}, {"on": False}, {"port": "", "on": False},
    {"port": "  ", "on": False}, {"port": 1, "on": False},
    *({"port": "FAKE", "on": value} for value in (None, 0, 1, "off", [], {})),
    [], True,
])
def test_strip_rejects_invalid_arguments_without_device_access(session, body):
    fake, call = session
    with pytest.raises(urllib.error.HTTPError) as error:
        call("/api/strip", body)
    assert error.value.code == 400
    assert fake.commands == [] and not G._operations


def test_strip_disconnect_releases_port_and_can_be_read_again(session, monkeypatch):
    fake, call = session
    original = fake.handle

    def unplug(cmd, seq, payload):
        if cmd == P.CMD["STRIP"]:
            raise OSError("device not configured")
        return original(cmd, seq, payload)

    monkeypatch.setattr(fake, "handle", unplug)
    with pytest.raises(urllib.error.HTTPError) as error:
        call("/api/strip", {"port": "FAKE", "on": False})
    assert error.value.code == 500
    assert "FAKE" not in G._devs and not G._operations
    monkeypatch.setattr(fake, "handle", original)
    assert call("/api/info?port=FAKE")["strip"] is True
    assert call("/api/strip", {"port": "FAKE", "on": False}) == {"ok": True}


def test_old_firmware_rejected_before_writing(session):
    fake, call = session
    fake.fw = "1.3.4"
    jid = call("/api/send", {"id": "frame", "port": "FAKE"})["job"]
    job = wait_job(call, jid)
    assert job["state"] == "error" and "升级" in job["error"]
    assert fake.commands == [P.CMD["INFO"]]
    assert "FAKE" in G._devs and fake.closes == 0
    assert not G._operations


def test_valid_error_reply_keeps_connection_for_next_operation(session):
    fake, call = session
    call("/api/info?port=FAKE")
    cached = G._devs["FAKE"]
    with pytest.raises(urllib.error.HTTPError) as error:
        call("/api/show", {"port": "FAKE", "name": "missing.bin"})
    assert error.value.code == 500
    assert G._devs["FAKE"] is cached and fake.closes == 0
    assert call("/api/info?port=FAKE")["ready"] is True


def test_upload_during_startup_does_not_reopen_port(session, monkeypatch):
    fake, call = session
    original = fake.handle

    def starting(cmd, seq, payload):
        if cmd == P.CMD["INFO"]:
            fake.commands.append(cmd)
            return fake._reply(cmd, seq, 0, b'{"ready":false,"upload_no_refresh":true}')
        return original(cmd, seq, payload)

    monkeypatch.setattr(fake, "handle", starting)
    jid = call("/api/send", {"id": "frame", "port": "FAKE"})["job"]
    job = wait_job(call, jid)
    assert job["state"] == "error" and "启动" in job["error"]
    assert "FAKE" in G._devs and fake.closes == 0
    cached = G._devs["FAKE"]
    monkeypatch.setattr(fake, "handle", original)
    jid = call("/api/send", {"id": "frame", "port": "FAKE"})["job"]
    assert wait_job(call, jid)["state"] == "ok"
    assert G._devs["FAKE"] is cached and fake.closes == 0
    assert P.CMD["REBOOT"] not in fake.commands and P.CMD["SHOW"] not in fake.commands


def test_lost_reboot_ack_is_not_retried(session, monkeypatch):
    fake, call = session
    original = fake.handle

    def lose_ack(cmd, seq, payload):
        if cmd == P.CMD["REBOOT"]:
            fake.commands.append(cmd)
            return
        return original(cmd, seq, payload)

    monkeypatch.setattr(fake, "handle", lose_ack)
    assert call("/api/reboot", {"port": "FAKE"}) == {"ok": False, "uncertain": True}
    assert fake.commands.count(P.CMD["REBOOT"]) == 1
    assert "FAKE" not in G._devs and not G._operations


def test_upload_disconnect_keeps_preview_and_releases_connection(session, monkeypatch):
    fake, call = session
    original = fake.handle

    def unplug(cmd, seq, payload):
        if cmd == P.CMD["UP_DATA"]:
            raise OSError("device not configured")
        return original(cmd, seq, payload)

    monkeypatch.setattr(fake, "handle", unplug)
    jid = call("/api/send", {"id": "frame", "port": "FAKE"})["job"]
    assert wait_job(call, jid)["state"] == "error"
    assert "frame" in G._frames and "FAKE" not in G._devs and not G._operations
    monkeypatch.setattr(fake, "handle", original)
    jid = call("/api/send", {"id": "frame", "port": "FAKE"})["job"]
    assert wait_job(call, jid)["state"] == "ok"
    assert P.CMD["REBOOT"] not in fake.commands


def test_port_removal_closes_cached_connection(session, monkeypatch):
    fake, call = session
    call("/api/info?port=FAKE")
    monkeypatch.setattr(P, "find_ports", lambda: [])
    assert call("/api/ports")["ports"] == []
    assert "FAKE" not in G._devs and fake.closes == 1


def test_control_lines_are_idle_before_serial_open(monkeypatch):
    import serial
    import hlserial
    monkeypatch.setattr(hlserial.sys, "platform", "win32")
    events = []

    class Serial:
        def __init__(self, **kwargs):
            assert kwargs["port"] is None
            self.dtr = self.rts = True

        def open(self):
            events.append((self.port, self.dtr, self.rts))

        def close(self):
            events.append("close")

    monkeypatch.setattr(serial, "Serial", Serial)
    d = P.Device("FAKE")
    assert events == [("FAKE", False, False)]
    d.close()
    assert events[-1] == "close"


def test_expired_heartbeat_does_not_start_new_session(session):
    fake, call = session
    assert call("/api/session", {"port": "FAKE", "action": 2}) == {"active": False, "supported": True}
    assert fake.commands == [P.CMD["INFO"]]


def test_display_stays_blocked_until_finish(session):
    fake, call = session
    jid = call("/api/send", {"id": "frame", "port": "FAKE"})["job"]
    assert wait_job(call, jid)["state"] == "ok"
    with pytest.raises(urllib.error.HTTPError):
        call("/api/show", {"port": "FAKE", "name": "original.bin"})
    assert fake.closes == 0 and fake.session_active
    call("/api/session", {"port": "FAKE", "action": 0})
    assert call("/api/show", {"port": "FAKE", "name": "original.bin"}) == {"ok": True}


def test_old_firmware_finishes_without_new_command(session):
    fake, call = session
    fake.fw = "1.4.0"
    jid = call("/api/send", {"id": "frame", "port": "FAKE"})["job"]
    assert wait_job(call, jid)["state"] == "ok"
    assert call("/api/session", {"port": "FAKE", "action": 0}) == {"active": False, "supported": False}
    assert P.CMD["UPLOAD_SESSION"] not in fake.commands
    assert P.CMD["REBOOT"] not in fake.commands and P.CMD["SHOW"] not in fake.commands


@pytest.mark.parametrize("body", [
    {}, {"port": "FAKE", "action": 1}, {"port": "FAKE", "action": True},
    {"port": "FAKE", "action": "end"}, {"port": "", "action": 0}, [],
])
def test_session_invalid_arguments_do_not_touch_device(session, body):
    fake, call = session
    with pytest.raises(urllib.error.HTTPError) as error:
        call("/api/session", body)
    assert error.value.code == 400 and not fake.commands
