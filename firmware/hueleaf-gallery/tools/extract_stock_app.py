#!/usr/bin/env python3
"""Extract the stock app (factory @0x10000) from the official merged image.
Usage: extract_stock_app.py 3.7einkDvb_factory_0x0.bin stock_app.bin"""
import sys
d = open(sys.argv[1], "rb").read()
app = d[0x10000:].rstrip(b"\xff")
app += b"\xff" * (-len(app) % 16)
assert app[0] == 0xE9, "no app image at 0x10000"
assert len(app) <= 0x170000, "stock app larger than new factory partition"
open(sys.argv[2], "wb").write(app)
print(f"stock app {len(app):#x} bytes")
