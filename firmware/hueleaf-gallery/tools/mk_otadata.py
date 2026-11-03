#!/usr/bin/env python3
"""Write an 8 KiB otadata image that selects ota_0 (state NEW -> rollback-monitored).
Same entry format/CRC as ESP-IDF otatool.py. Usage: mk_otadata.py out.bin"""
import binascii, struct, sys
seq = 1                                   # (seq-1) % 2 == 0 -> ota_0
crc = binascii.crc32(struct.pack("<I", seq), 0xFFFFFFFF) & 0xFFFFFFFF
entry = struct.pack("<I20sII", seq, b"\xff" * 20, 0x0, crc)   # ESP_OTA_IMG_NEW
img = entry + b"\xff" * (0x1000 - len(entry)) + b"\xff" * 0x1000
open(sys.argv[1], "wb").write(img)
