"""remote-v1 BLE framing; no Bluetooth dependency, usable by client implementations."""
import struct

SERVICE = "8f510001-6c8f-4d84-9af8-6e1f9000a001"
RX = "8f510002-6c8f-4d84-9af8-6e1f9000a001"
TX = "8f510003-6c8f-4d84-9af8-6e1f9000a001"
CHALLENGE = "8f510004-6c8f-4d84-9af8-6e1f9000a001"


def message(op, request, payload=b""):
    result = struct.pack("<BBH", 1, op, request) + payload
    if len(result) > 512:
        raise ValueError("Message exceeds 512 bytes")
    return result


def fragments(data, stream, mtu=23):
    if not 4 <= len(data) <= 512 or mtu < 23:
        raise ValueError("Invalid message/MTU")
    room = min(mtu - 7, 244)
    for seq, offset in enumerate(range(0, len(data), room)):
        chunk = data[offset:offset + room]
        flags = (1 if offset == 0 else 0) | (2 if offset + len(chunk) == len(data) else 0)
        yield bytes((stream & 255, seq, flags, 0)) + chunk


class Reassembler:
    def __init__(self):
        self.data = bytearray()
        self.stream = None
        self.sequence = 0

    def feed(self, packet):
        if len(packet) < 5 or packet[3] or packet[2] & ~3:
            raise ValueError("Invalid fragment")
        stream, sequence, flags = packet[:3]
        if flags & 1:
            if sequence:
                raise ValueError("Start sequence must be zero")
            self.data.clear()
            self.stream, self.sequence = stream, 0
        if self.stream != stream or self.sequence != sequence or len(self.data) + len(packet) - 4 > 512:
            raise ValueError("Fragment sequence/size mismatch")
        self.data.extend(packet[4:])
        self.sequence += 1
        if flags & 2:
            data = bytes(self.data)
            self.stream = None
            if len(data) < 6 or data[0] != 1:
                raise ValueError("Invalid reply")
            version, op, request, status = struct.unpack_from("<BBHh", data)
            return op, request, status, data[6:]
        return None


def inventory(data):
    if len(data) < 15:
        raise ValueError("Short inventory")
    token, total, start, count, flags = struct.unpack_from("<QHHHB", data)
    pos, names = 15, []
    for _ in range(count):
        if pos + 2 > len(data):
            raise ValueError("Short record")
        selected, length = data[pos:pos+2]
        pos += 2
        if pos + length > len(data):
            raise ValueError("Short filename")
        names.append((data[pos:pos+length].decode("utf-8"), bool(selected)))
        pos += length
    if pos != len(data):
        raise ValueError("Trailing inventory bytes")
    return token, total, start, flags, names
