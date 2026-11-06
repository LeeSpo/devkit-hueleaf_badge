import datetime
import json
import os
import struct
import sys
from types import SimpleNamespace

import pytest

sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
import hueleaf_push as P
from test_modes_proto import FakeHueLeaf


@pytest.fixture
def elapsed(monkeypatch):
    seconds = [0.0]
    monkeypatch.setattr(P.time, "monotonic", lambda: seconds[0])
    monkeypatch.setattr(P.time, "sleep", lambda delay: seconds.__setitem__(0, seconds[0] + delay))
    start = datetime.datetime(2026, 11, 6, 12, 34, 59)

    class Clock:
        @staticmethod
        def now():
            return start + datetime.timedelta(seconds=seconds[0])

    monkeypatch.setattr(P, "datetime", SimpleNamespace(datetime=Clock))
    return seconds


@pytest.mark.parametrize("states", [[{"ready": False}, {"ready": False}, {"ready": True}], [{}]])
def test_cli_sets_current_time_after_startup_on_same_connection(monkeypatch, elapsed, capsys, states):
    fake = FakeHueLeaf()
    payloads, opened = [], []
    states = iter(states)

    def handle(cmd, seq, payload):
        fake.commands.append(cmd)
        if cmd == P.CMD["INFO"]:
            return fake._reply(cmd, seq, 0, json.dumps(next(states)).encode())
        assert cmd == P.CMD["SET_TIME"]
        payloads.append(payload)
        fake._reply(cmd, seq, 0)

    fake.handle = handle
    original_device = P.Device

    def connect(port):
        opened.append(port)
        return original_device(fake, timeout=0.02)

    monkeypatch.setattr(P, "Device", connect)
    assert P.main(["--port", "FAKE", "--set-time"]) == 0
    expected = datetime.datetime(2026, 11, 6, 12, 34, 59) + datetime.timedelta(seconds=elapsed[0])
    assert payloads == [struct.pack("<HBBBBB", expected.year, expected.month, expected.day,
                                   expected.hour, expected.minute, expected.second)]
    assert opened == ["FAKE"]
    assert fake.commands == [P.CMD["INFO"]] * (int(elapsed[0]) + 1) + [P.CMD["SET_TIME"]]
    assert "已同步电脑本地时间" in capsys.readouterr().out


def test_wait_ready_tolerates_initial_missing_reply(monkeypatch, elapsed):
    d = P.Device(FakeHueLeaf())
    replies = iter([TimeoutError("INFO: no valid reply"), b'{"ready": true}'])

    def reply(name, **kwargs):
        assert name == "INFO" and kwargs["retries"] == 1
        result = next(replies)
        if isinstance(result, Exception):
            raise result
        return result

    monkeypatch.setattr(d, "ok", reply)
    d.wait_ready(timeout=3)
    assert elapsed[0] == 1


def test_cli_startup_timeout_explains_failure_without_setting_clock(monkeypatch, elapsed, capsys):
    fake = FakeHueLeaf()

    def handle(cmd, seq, payload):
        fake.commands.append(cmd)
        assert cmd == P.CMD["INFO"]
        fake._reply(cmd, seq, 0, b'{"ready": false}')

    fake.handle = handle
    d = P.Device(fake)
    monkeypatch.setattr(P, "Device", lambda port: d)
    with pytest.raises(SystemExit) as error:
        P.main(["--port", "FAKE", "--set-time"])
    assert error.value.code == 1 and elapsed[0] == 180
    assert P.CMD["SET_TIME"] not in fake.commands
    output = capsys.readouterr()
    assert "校时失败" in output.err and "TF 卡" in output.err
    assert "已同步" not in output.out
