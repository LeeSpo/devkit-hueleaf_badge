import struct
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import pytest
from hlble import Reassembler, fragments, inventory, message


@pytest.mark.parametrize("mtu", [23, 64, 247, 517])
def test_long_indicated_reply_roundtrip(mtu):
    reply = struct.pack("<BBHh", 1, 0x83, 65535, 0) + bytes(range(250))*2
    parser = Reassembler()
    packets = list(fragments(reply, 255, mtu))
    result = None
    for packet in packets:
        assert len(packet) <= min(mtu-3, 248)
        result = parser.feed(packet)
    assert result == (0x83, 65535, 0, reply[6:])


def test_request_vector_and_signed_status():
    assert list(fragments(message(4, 2, b"\1"), 2)) == [bytes.fromhex("020003000104020001")]
    parser = Reassembler()
    assert parser.feed(bytes.fromhex("0200030001840200f7ff")) == (0x84, 2, -9, b"")


def test_reject_missing_start_wrong_sequence_and_overflow():
    parser = Reassembler()
    with pytest.raises(ValueError):
        parser.feed(bytes.fromhex("01000200010000000000"))
    data = struct.pack("<BBHh", 1, 0x83, 1, 0) + bytes(50)
    packets = list(fragments(data, 1))
    parser.feed(packets[0])
    with pytest.raises(ValueError):
        parser.feed(packets[2])
    with pytest.raises(ValueError):
        message(1, 1, bytes(509))


def test_inventory_pages_and_truncation():
    data = struct.pack("<QHHHB", 123, 300, 297, 2, 1) + b"\1\5a.bin\0\5b.bin"
    assert inventory(data) == (123, 300, 297, 1, [("a.bin", True), ("b.bin", False)])
    for i in range(len(data)):
        with pytest.raises(ValueError):
            inventory(data[:i])
