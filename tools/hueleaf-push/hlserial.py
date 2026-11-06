"""Open the USB protocol port without requesting an ESP32 reset."""
import struct
import sys


def open_serial(port):
    import serial

    serial_type = serial.Serial
    if sys.platform == "darwin":
        import fcntl
        import termios

        class MacSerial(serial.Serial):
            def _update_control_lines(self):
                # pySerial normally updates DTR then RTS separately. On macOS
                # that sequence resets the native USB Serial/JTAG controller,
                # even with both requested states False. Apply both together.
                bits = struct.unpack("I", fcntl.ioctl(self.fd, termios.TIOCMGET,
                                                    struct.pack("I", 0)))[0]
                bits &= ~(termios.TIOCM_DTR | termios.TIOCM_RTS)
                if self._dtr_state:
                    bits |= termios.TIOCM_DTR
                if self._rts_state:
                    bits |= termios.TIOCM_RTS
                fcntl.ioctl(self.fd, termios.TIOCMSET, struct.pack("I", bits))

            _update_dtr_state = _update_control_lines
            _update_rts_state = _update_control_lines

        serial_type = MacSerial

    s = serial_type(port=None, baudrate=115200, timeout=0.2, write_timeout=3)
    s.dtr = False
    s.rts = False
    s.port = port
    try:
        s.open()
        if sys.platform == "darwin":
            # Prevent another GUI/CLI/monitor from opening the same endpoint
            # and consuming replies or changing reset control lines.
            fcntl.ioctl(s.fileno(), termios.TIOCEXCL)
    except Exception:
        s.close()
        raise
    return s
