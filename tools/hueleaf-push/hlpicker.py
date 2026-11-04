"""On-device name picker: per-image 1-bit name labels + a host-side page preview.

The device has only a 5x7 ASCII font, and card file names are ASCII-only, so
the desktop tool renders each image's display name (any script, e.g. Chinese)
into a small 1-bit bitmap and uploads it next to the image as
img/.labels/<stem>.lbl. The firmware composes the picker page from these
labels (no CJK font in flash). Layout constants below mirror gallery.c
(PK_*); render_page() follows the firmware drawing code step by step.

Label file (little-endian):
  "HLL1" | u8 name_len | name (UTF-8) | u16 w | u16 h | rows, MSB first, 1 = ink
"""
from __future__ import annotations

import os, struct

from PIL import Image, ImageDraw, ImageFont

import hlformat as F

MAGIC = b"HLL1"
FONT_DIRS = ["/usr/share/fonts/opentype/noto", "/System/Library/Fonts", "/Library/Fonts", "C:/Windows/Fonts"]
FONT_FILES = ["NotoSansCJK-Bold.ttc", "NotoSansCJK-Regular.ttc", "PingFang.ttc", "Hiragino Sans GB.ttc", "STHeiti Medium.ttc",
              "msyhbd.ttc", "msyh.ttc", "simhei.ttf"]
# Picker layout (keep in sync with PK_* in firmware/hueleaf-gallery/main/gallery.c)
PK_ROWS, PK_ROW_H, PK_Y0 = 14, 40, 80
PK_NUM_RIGHT, PK_LABEL_X = 58, 68
LABEL_H, LABEL_W, LABEL_PX = 32, 404, 24
TITLE, TITLE_PX = "Select image", 34
HINT, HINT_PX = "BOOT down · PWR page · Hold select", 24
# tap_next on: two lines (one line would need < 20 px)
HINT_TAP, HINT_TAP2, HINT_TAP_PX = "BOOT down · PWR up", "Tap×2 page · Hold select", 24
LABEL_MAX_BYTES = 5 + 255 + 4 + ((LABEL_W + 7) // 8) * LABEL_H


def find_font(path: str | None = None) -> str:
    if path:
        return path
    env = os.environ.get("HUELEAF_FONT")
    if env:
        return env
    for d in FONT_DIRS:
        for f in FONT_FILES:
            p = os.path.join(d, f)
            if os.path.exists(p):
                return p
    return ""       # Pillow's built-in font (Latin only); set HUELEAF_FONT for CJK


def text_image(text: str, px: int, font: str | None = None, max_w: int | None = None,
               height: int | None = None) -> Image.Image:
    """1-bit image (1 = ink) of text; truncated with an ellipsis to max_w."""
    path = find_font(font)
    f = ImageFont.truetype(path, px) if path else ImageFont.load_default(px)
    if max_w and f.getlength(text) > max_w - 4:
        while text and f.getlength(text + "…") > max_w - 4:
            text = text[:-1]
        text = text.rstrip() + "…"
    l, t, r, b = f.getbbox(text or " ")
    w = max(1, r - l + 4)
    if height is None:
        h, oy = b - t + 4, 2 - t
    else:   # fixed box, baseline placed from the font's metrics so rows line up
        asc, desc = f.getmetrics()
        h, oy = height, (height - (asc + desc)) // 2
    im = Image.new("L", (w, h), 0)
    ImageDraw.Draw(im).text((2 - l, oy), text, font=f, fill=255)
    return im.point(lambda v: 255 if v >= 110 else 0).convert("1")


def bits(im: Image.Image) -> bytes:
    w, h = im.size
    stride = (w + 7) // 8
    out = bytearray(stride * h)
    px = im.load()
    for y in range(h):
        for x in range(w):
            if px[x, y]:
                out[y * stride + x // 8] |= 0x80 >> (x % 8)
    return bytes(out)


def make_label(display: str, font: str | None = None) -> bytes:
    name = display.encode()[:255]
    while True:     # don't cut a UTF-8 sequence in half
        try:
            name.decode(); break
        except UnicodeDecodeError:
            name = name[:-1]
    im = text_image(display, LABEL_PX, font, max_w=LABEL_W, height=LABEL_H)
    return MAGIC + bytes([len(name)]) + name + struct.pack("<HH", *im.size) + bits(im)


def parse_label(blob: bytes):
    """-> (display name, w, h, bitmap bytes)."""
    if blob[:4] != MAGIC:
        raise ValueError("not a label")
    n = blob[4]
    name = blob[5:5 + n].decode()
    w, h = struct.unpack("<HH", blob[5 + n:9 + n])
    data = blob[9 + n:]
    if len(data) != ((w + 7) // 8) * h:
        raise ValueError("label size")
    return name, w, h, data


def _blit(ids, w, h, data, x0, y0, ink):
    stride = (w + 7) // 8
    for y in range(h):
        for x in range(w):
            if data[y * stride + x // 8] & (0x80 >> (x % 8)):
                xx, yy = x0 + x, y0 + y
                if 0 <= xx < F.WIDTH and 0 <= yy < F.HEIGHT:
                    ids[yy * F.WIDTH + xx] = ink


def _ascii(ids, x, y, s, scale, ink):
    """Same 5x7 font table as frame.c (only what the picker uses: digits . /)."""
    font = {"0": (14,17,19,21,25,17,14), "1": (4,12,4,4,4,4,14), "2": (14,17,1,2,4,8,31),
            "3": (31,2,4,2,1,17,14), "4": (2,6,10,18,31,2,2), "5": (31,16,30,1,1,17,14),
            "6": (6,8,16,30,17,17,14), "7": (31,1,2,4,8,8,8), "8": (14,17,17,14,17,17,14),
            "9": (14,17,17,15,1,2,12), "/": (1,1,2,4,8,16,16), ".": (0,0,0,0,0,12,12)}
    for ch in s:
        g = font.get(ch, (0,) * 7)
        for r in range(7):
            for c in range(5):
                if g[r] & (16 >> c):
                    for dy in range(scale):
                        for dx in range(scale):
                            ids[(y + r * scale + dy) * F.WIDTH + x + c * scale + dx] = ink
        x += 6 * scale


def render_page(labels: list[bytes], cursor: int, font: str | None = None) -> Image.Image:
    """Preview of the picker page the firmware draws (black/white only)."""
    n = len(labels)
    page, pages = cursor // PK_ROWS, max(1, (n + PK_ROWS - 1) // PK_ROWS)
    ids = [1] * (F.WIDTH * F.HEIGHT)
    t = text_image(TITLE, TITLE_PX, font)
    _blit(ids, *t.size, bits(t), (F.WIDTH - t.size[0]) // 2, 38 - t.size[1] // 2, 0)
    for x in range(F.WIDTH):
        for y in (PK_Y0 - 8, PK_Y0 - 7):
            ids[y * F.WIDTH + x] = 0
    for r in range(PK_ROWS):
        i = page * PK_ROWS + r
        if i >= n:
            break
        y = PK_Y0 + r * PK_ROW_H
        sel = i == cursor
        fg = 1 if sel else 0
        if sel:
            for yy in range(y, y + PK_ROW_H - 2):
                ids[yy * F.WIDTH:(yy + 1) * F.WIDTH] = [0] * F.WIDTH
        num = f"{i + 1}."
        _ascii(ids, PK_NUM_RIGHT - len(num) * 12, y + (PK_ROW_H - 2 - 14) // 2, num, 2, fg)
        _, w, h, data = parse_label(labels[i])
        _blit(ids, w, h, data, PK_LABEL_X, y + (PK_ROW_H - 2 - h) // 2, fg)
    fy = PK_Y0 + PK_ROWS * PK_ROW_H + 8
    for x in range(F.WIDTH):
        ids[fy * F.WIDTH + x] = 0
    pg = f"{page + 1}/{pages}"
    _ascii(ids, (F.WIDTH - len(pg) * 12) // 2, fy + 10, pg, 2, 0)
    hb = text_image(HINT, HINT_PX, font)
    _blit(ids, *hb.size, bits(hb), (F.WIDTH - hb.size[0]) // 2, 698 - hb.size[1] // 2, 0)
    return F.preview(ids)
