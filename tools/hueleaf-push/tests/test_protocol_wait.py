import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
import hueleaf_push as P
from test_modes_proto import FakeHueLeaf


def test_info_can_wait_through_full_screen_refresh_without_resending(monkeypatch):
    fake = FakeHueLeaf()
    d = P.Device(fake)
    clock = [1000.0]
    monkeypatch.setattr(P.time, "time", lambda: clock[0])
    original = d._read_frame

    def after_refresh(deadline):
        clock[0] += 31
        return original(deadline)

    monkeypatch.setattr(d, "_read_frame", after_refresh)
    assert b'"ready": true' in d.ok("INFO")
    assert fake.commands == [P.CMD["INFO"]]


@pytest.mark.parametrize("name, explicit, expected", [
    ("INFO", None, 60), ("UP_DATA", None, 60), ("UP_END", None, 60),
    ("PING", None, 3), ("REBOOT", None, 3), ("INFO", 1, 1),
])
def test_command_timeout_keeps_fast_reboot_probes(monkeypatch, name, explicit, expected):
    fake = FakeHueLeaf()
    d = P.Device(fake)
    monkeypatch.setattr(P.time, "time", lambda: 1000.0)

    def handle(cmd, seq, payload):
        fake._reply(cmd, seq, 0)

    fake.handle = handle
    original = d._read_frame

    def check_deadline(deadline):
        assert deadline == 1000.0 + expected
        return original(deadline)

    monkeypatch.setattr(d, "_read_frame", check_deadline)
    assert d.ok(name, timeout=explicit) == b""
