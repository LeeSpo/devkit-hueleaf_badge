"""HueLeaf on-card image format (otp6-v1) — conversion, dithering, packing.

Frame = 172800 bytes, identical to what the official driver streams to the
EPD037A02 (see examples/3.7einkDvb/main/web_server/www/eink_pipeline.js
makeFrame() with panel rotation 0, and screen_driver.h):
  column-major, logical x from 479 down to 0; per column 360 bytes covering
  y = 0..719; byte = (ink[y even] << 4) | ink[y odd].
Ink codes: 0 black, 1 white, 2 yellow, 3 red, 5 blue, 6 green.
"""
from __future__ import annotations

from PIL import Image, ImageOps

WIDTH, HEIGHT = 480, 720
FRAME_BYTES = WIDTH * HEIGHT // 2
# (ink id, sRGB) — same reference colours as the official web pipeline.
PALETTE = [
    (0, (0x11, 0x14, 0x12)),
    (1, (0xF4, 0xF3, 0xE8)),
    (2, (0xE7, 0xC8, 0x3F)),
    (3, (0xC6, 0x3B, 0x35)),
    (5, (0x31, 0x5E, 0xAE)),
    (6, (0x39, 0x84, 0x51)),
]
INK_RGB = dict(PALETTE)
VALID_INKS = frozenset(INK_RGB)
# Chroma weight in ink selection. >1 keeps neutrals black/white instead of
# blue/green speckle (the 6 inks have no grey). Tunable via --chroma.
CHROMA_W = 12.0


def _lin(v: float) -> float:
    v /= 255.0
    return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4


_LUT = [_lin(i) for i in range(256)]
_PAL_LIN = [(ink, tuple(_LUT[c] for c in rgb)) for ink, rgb in PALETTE]


def _oklab(r: float, g: float, b: float):
    """Linear sRGB -> OKLab (perceptual distance for ink selection)."""
    l = (0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b)
    m = (0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b)
    s = (0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b)
    l, m, s = (max(v, 0.0) ** (1 / 3) for v in (l, m, s))
    return (0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
            1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
            0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s)


_PAL_OK = [(ink, tuple(float(c) for c in rgb), _oklab(*(_LUT[c] for c in rgb))) for ink, rgb in PALETTE]


def _lin_f(v: float) -> float:
    return _LUT[min(255, max(0, int(v + 0.5)))]


def fit_image(img: Image.Image, rotate: int = 0, mode: str = "crop") -> Image.Image:
    """Rotate (degrees CCW, multiple of 90), then fit to 480x720 portrait.
    mode 'crop' fills and center-crops; 'pad' letterboxes on white."""
    img = ImageOps.exif_transpose(img).convert("RGB")
    if rotate % 360:
        img = img.rotate(rotate, expand=True)
    if mode == "pad":
        img = ImageOps.pad(img, (WIDTH, HEIGHT), Image.LANCZOS, color=INK_RGB[1])
    else:
        img = ImageOps.fit(img, (WIDTH, HEIGHT), Image.LANCZOS)
    return img


def dither(img: Image.Image, dither: bool = True) -> list[int]:
    """Floyd–Steinberg (serpentine): error diffused in sRGB values (like common
    e-paper tools; greys stay black/white), ink chosen by nearest OKLab distance (avoids colour noise in greys).
    Returns row-major ink ids, length WIDTH*HEIGHT."""
    if img.size != (WIDTH, HEIGHT):
        raise ValueError("image must be 480x720")
    px = img.convert("RGB").load()
    w, h = WIDTH, HEIGHT
    buf = [[list(map(float, px[x, y])) for x in range(w)] for y in range(h)]
    out = [0] * (w * h)
    for y in range(h):
        rev = y & 1
        xs = range(w - 1, -1, -1) if rev else range(w)
        d = -1 if rev else 1
        row, nxt = buf[y], buf[y + 1] if y + 1 < h else None
        for x in xs:
            r, g, b = row[x]
            L, A, B = _oklab(_lin_f(r), _lin_f(g), _lin_f(b))
            best, bi, bd = None, 0, 1e9
            for ink, lin, (pl, pa, pb) in _PAL_OK:
                dd = (L - pl) ** 2 + CHROMA_W * ((A - pa) ** 2 + (B - pb) ** 2)
                if dd < bd:
                    bd, bi, best = dd, ink, lin
            out[y * w + x] = bi
            if not dither:
                continue
            er, eg, eb = r - best[0], g - best[1], b - best[2]
            for dx, dy, k in ((d, 0, 7 / 16), (-d, 1, 3 / 16), (0, 1, 5 / 16), (d, 1, 1 / 16)):
                xx = x + dx
                if 0 <= xx < w:
                    tgt = row if dy == 0 else nxt
                    if tgt is not None:
                        p = tgt[xx]
                        p[0] += er * k; p[1] += eg * k; p[2] += eb * k
    return out


def pack(ids: list[int]) -> bytes:
    """Row-major ink ids -> panel frame bytes (official makeFrame order)."""
    if len(ids) != WIDTH * HEIGHT:
        raise ValueError("pixel count")
    out = bytearray(FRAME_BYTES)
    o = 0
    for x in range(WIDTH - 1, -1, -1):
        for y in range(0, HEIGHT, 2):
            a, b = ids[y * WIDTH + x], ids[(y + 1) * WIDTH + x]
            if a not in VALID_INKS or b not in VALID_INKS:
                raise ValueError("invalid ink")
            out[o] = (a << 4) | b
            o += 1
    return bytes(out)


def unpack(frame: bytes) -> list[int]:
    if len(frame) != FRAME_BYTES:
        raise ValueError("frame size")
    ids = [0] * (WIDTH * HEIGHT)
    for i, byte in enumerate(frame):
        x = WIDTH - 1 - i // (HEIGHT // 2)
        y = (i % (HEIGHT // 2)) * 2
        ids[y * WIDTH + x] = byte >> 4
        ids[(y + 1) * WIDTH + x] = byte & 15
    return ids


def preview(ids: list[int]) -> Image.Image:
    img = Image.new("RGB", (WIDTH, HEIGHT))
    img.putdata([INK_RGB.get(i, (255, 0, 255)) for i in ids])
    return img


def convert(path_or_img, rotate: int = 0, mode: str = "crop", use_dither: bool = True):
    img = path_or_img if isinstance(path_or_img, Image.Image) else Image.open(path_or_img)
    ids = dither(fit_image(img, rotate, mode), use_dither)
    return ids, pack(ids)
