"""A merged installation BIN with a small, unhashed write-map footer.

The footer is never flashed. Holes in the merged image (NVS, PHY, stock app)
are never written either. Plain application BINs need no footer.
"""
import binascii
from dataclasses import dataclass
import json
from pathlib import Path
import struct

from support import preflight, remote_layout

MAGIC = b"HUELEAF-INSTALL1\n"
MAX_METADATA = 65536
STOCK_ENTRIES = [
    ("nvs", 1, 2, 0x9000, 0x6000), ("phy_init", 1, 1, 0xF000, 0x1000),
    ("factory", 0, 0, 0x10000, 0x170000), ("otadata", 1, 0, 0x180000, 0x2000),
    ("ota_0", 0, 0x10, 0x190000, 0xB0000), ("ota_1", 0, 0x11, 0x240000, 0xB0000),
    ("image_cache", 1, 0x40, 0x300000, 0x100000),
]


def parse_table(data):
    # A table's fields are needed for addresses, not its optional MD5 record.
    return preflight.parse_ptable(data, verify_md5=False)


def layout_id(table):
    entries = [tuple(p[:5]) for p in table]
    if entries == remote_layout.ENTRIES:
        return "remote-v1"
    if entries == STOCK_ENTRIES:
        return "gallery-stock-v1"
    raise ValueError("设备或固件分区不受支持，无法自动确定安装位置")


def ota0_data():
    seq = 1
    crc = binascii.crc32(struct.pack("<I", seq), 0xFFFFFFFF) & 0xFFFFFFFF
    entry = struct.pack("<I20sII", seq, b"\xff" * 20, 0, crc)
    return entry + b"\xff" * (0x2000 - len(entry))


def write_offsets(layout):
    if layout == "remote-v1":
        return (0x2000, 0x8000, 0x10000, 0x20000)
    if layout == "gallery-stock-v1":
        return (0x2000, 0x8000, 0x180000, 0x190000)
    raise ValueError("未知固件分区布局")


@dataclass
class Firmware:
    path: Path
    data: bytes
    metadata: dict
    segments: list

    @property
    def complete(self):
        return bool(self.segments)

    @classmethod
    def read(cls, path):
        path = Path(path)
        data = path.read_bytes()
        if not data:
            raise ValueError("固件文件为空")
        if not data.endswith(MAGIC):
            # User-selected plain BIN: treat it as an application, without
            # guessing its type, checking its hash or comparing its version.
            return cls(path, data, {}, [])
        length_pos = len(data) - len(MAGIC) - 4
        if length_pos < 0:
            raise ValueError("固件安装信息不完整")
        length = struct.unpack_from("<I", data, length_pos)[0]
        start = length_pos - length
        if not 0 < length <= MAX_METADATA or start < 0:
            raise ValueError("固件安装信息长度无效")
        meta = json.loads(data[start:length_pos])
        if not isinstance(meta, dict) or meta.get("format") != 1 or meta.get("chip") != "esp32c5":
            raise ValueError("不支持的安装镜像格式或芯片")
        segments = meta.get("segments", [])
        offsets = write_offsets(meta.get("layout_id"))
        if (not isinstance(segments, list) or len(segments) != 4
                or not all(isinstance(p, dict) for p in segments)
                or [p.get("offset") for p in segments] != list(offsets)):
            raise ValueError("安装镜像的写入信息不完整")
        limits = (0x6000, 0x1000, 0x2000, 0x1F0000 if meta["layout_id"] == "remote-v1" else 0xB0000)
        end = 0
        for part, limit in zip(segments, limits):
            offset, size = part["offset"], part.get("size")
            if not isinstance(size, int) or not 0 < size <= limit or offset < end or offset + size > start:
                raise ValueError("安装镜像区域越界或重叠")
            end = offset + size
        table = segments[1]
        try:
            actual = layout_id(parse_table(data[table["offset"]:table["offset"] + table["size"]]))
        except preflight.ImageError as exc:
            raise ValueError(f"无法读取固件分区表：{exc}") from exc
        if actual != meta["layout_id"]:
            raise ValueError("安装信息与分区表不一致")
        return cls(path, data[:start], meta, segments)

    def payloads(self):
        return [(part["offset"], self.data[part["offset"]:part["offset"] + part["size"]])
                for part in self.segments]


def build_image(build, record):
    """Build a complete installation image from already provenance-checked outputs."""
    build = Path(build)
    table = (build / "partition_table/partition-table.bin").read_bytes()
    layout = layout_id(parse_table(table))
    if layout != record.get("layout_id", "gallery-stock-v1"):
        raise ValueError("构建记录与分区表不一致")
    payloads = [(build / "bootloader/bootloader.bin").read_bytes(), table, ota0_data(),
                (build / "hueleaf_gallery.bin").read_bytes()]
    app_size = preflight.app_image_size(payloads[-1])
    if app_size != len(payloads[-1]):
        raise ValueError("应用镜像长度不合法")
    offsets = write_offsets(layout)
    image = bytearray(b"\xff" * (offsets[-1] + len(payloads[-1])))
    parts = []
    for offset, data in zip(offsets, payloads):
        image[offset:offset + len(data)] = data
        parts.append({"offset": offset, "size": len(data)})
    meta = {"format": 1, "chip": "esp32c5", "layout_id": layout,
            "firmware_version": record["firmware_version"], "commit": record.get("base_commit", ""),
            "segments": parts}
    encoded = json.dumps(meta, separators=(",", ":")).encode()
    return bytes(image) + encoded + struct.pack("<I", len(encoded)) + MAGIC
