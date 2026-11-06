import os
import struct
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
import hlserial


@pytest.fixture
def mac_serial(monkeypatch):
    fcntl = pytest.importorskip("fcntl")
    termios = pytest.importorskip("termios")
    import serial

    monkeypatch.setattr(hlserial.sys, "platform", "darwin")
    events = []
    current = [termios.TIOCM_DTR | termios.TIOCM_RTS | termios.TIOCM_CTS]

    class Serial:
        def __init__(self, **kwargs):
            assert kwargs["port"] is None
            self.fd = 42
            self._dtr_state = self._rts_state = True

        dtr = property(lambda self: self._dtr_state,
                       lambda self, value: setattr(self, "_dtr_state", value))
        rts = property(lambda self: self._rts_state,
                       lambda self, value: setattr(self, "_rts_state", value))

        def open(self):
            events.append(("open", self.port, self.dtr, self.rts))
            self._update_dtr_state()
            self._update_rts_state()

        def fileno(self):
            return self.fd

        def close(self):
            events.append("close")

    def ioctl(fd, request, arg=None):
        assert fd == 42
        events.append(request)
        if request == termios.TIOCMGET:
            return struct.pack("I", current[0])
        if request == termios.TIOCMSET:
            current[0] = struct.unpack("I", arg)[0]
            # No intermediate state may leave only one reset line asserted.
            assert current[0] & (termios.TIOCM_DTR | termios.TIOCM_RTS) == 0
            assert current[0] & termios.TIOCM_CTS
        else:
            assert request == termios.TIOCEXCL
        return 0

    monkeypatch.setattr(serial, "Serial", Serial)
    monkeypatch.setattr(fcntl, "ioctl", ioctl)
    return events, fcntl, termios, ioctl


def test_mac_open_sets_both_lines_atomically_and_excludes_other_readers(mac_serial):
    events, _, termios, _ = mac_serial
    s = hlserial.open_serial("FAKE")
    assert events == [("open", "FAKE", False, False), termios.TIOCMGET, termios.TIOCMSET,
                      termios.TIOCMGET, termios.TIOCMSET, termios.TIOCEXCL]
    s.close()
    assert events[-1] == "close"


def test_mac_exclusive_failure_closes_port(mac_serial, monkeypatch):
    events, fcntl, termios, original = mac_serial

    def fail_exclusive(fd, request, arg=None):
        if request == termios.TIOCEXCL:
            raise OSError(16, "Resource busy")
        return original(fd, request, arg)

    monkeypatch.setattr(fcntl, "ioctl", fail_exclusive)
    with pytest.raises(OSError, match="Resource busy"):
        hlserial.open_serial("FAKE")
    assert events[-1] == "close"
