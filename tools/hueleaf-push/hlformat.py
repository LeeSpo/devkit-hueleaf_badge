"""HueLeaf on-card image format (otp6-v1) — conversion, dithering, packing.

Frame = 172800 bytes, identical to what the official driver streams to the
EPD037A02 (see examples/3.7einkDvb/main/web_server/www/eink_pipeline.js
makeFrame() with panel rotation 0, and screen_driver.h):
  column-major, logical x from 479 down to 0; per column 360 bytes covering
  y = 0..719; byte = (ink[y even] << 4) | ink[y odd].
Ink codes: 0 black, 1 white, 2 yellow, 3 red, 5 blue, 6 green.
"""
from __future__ import annotations

from functools import lru_cache
from itertools import combinations

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
# Chroma weight for flat artwork and low-chroma photo pixels. In photos it
# fades to 1/12 of this weight for colourful sources; greys use black/white only.
# Tunable via --chroma.
CHROMA_W = 12.0
# Full linear-light diffusion preserves the mean ink coverage. Map source
# colours into the ink gamut first, rather than discarding error to hide it.
DIFFUSE = 1.0


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
_LUMA = (0.2126, 0.7152, 0.0722)
_PHOTO_PAL = [(ink, lin, lab, sum(c * w for c, w in zip(lin, _LUMA)))
              for (ink, lin), (_, _, lab) in zip(_PAL_LIN, _PAL_OK)]
_BLACK_Y, _WHITE_Y = _PHOTO_PAL[0][3], _PHOTO_PAL[1][3]
_GAMUT_WEIGHTS = (0.3 ** 0.5, 0.5 ** 0.5, 0.2 ** 0.5)


def _ink_gamut_faces():
    """Weighted linear RGB hull, matching the browser's photo gamut metric."""
    points = [tuple(c * w for c, w in zip(lin, _GAMUT_WEIGHTS)) for _, lin in _PAL_LIN]
    faces = []
    for a, b, c in combinations(points, 3):
        u, v = [b[i] - a[i] for i in range(3)], [c[i] - a[i] for i in range(3)]
        normal = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2],
                  u[0] * v[1] - u[1] * v[0])
        length = sum(n * n for n in normal) ** 0.5
        if length < 1e-12:
            continue
        normal = tuple(n / length for n in normal)
        limit = sum(n * c for n, c in zip(normal, a))
        sides = [sum(n * c for n, c in zip(normal, p)) - limit for p in points]
        if min(sides) < -1e-10 and max(sides) > 1e-10:
            continue
        if max(sides) > 1e-10:
            normal, limit = tuple(-n for n in normal), -limit
        uu, uv, vv = sum(c * c for c in u), sum(x * y for x, y in zip(u, v)), sum(c * c for c in v)
        edges = []
        for start, end in ((a, b), (b, c), (c, a)):
            direction = tuple(y - x for x, y in zip(start, end))
            edges.append((start, direction, sum(c * c for c in direction)))
        faces.append((normal, limit, a, u, v, uu, uv, vv, uu * vv - uv * uv, edges))
    return tuple(faces)


_GAMUT_FACES = _ink_gamut_faces()


def _map_photo_gamut(r: float, g: float, b: float):
    """Closest representable mixture; remove impossible error before diffusion."""
    x, y, z = r * _GAMUT_WEIGHTS[0], g * _GAMUT_WEIGHTS[1], b * _GAMUT_WEIGHTS[2]
    outside = [face for face in _GAMUT_FACES
               if face[0][0] * x + face[0][1] * y + face[0][2] * z > face[1] + 1e-10]
    if not outside:
        return r, g, b
    distance, closest = float("inf"), (x, y, z)
    # The closest boundary point belongs to a face visible from the source.
    for normal, limit, a, u, v, uu, uv, vv, det, edges in outside:
        height = normal[0] * x + normal[1] * y + normal[2] * z - limit
        qx, qy, qz = x - height * normal[0], y - height * normal[1], z - height * normal[2]
        tx, ty, tz = qx - a[0], qy - a[1], qz - a[2]
        tu, tv = tx * u[0] + ty * u[1] + tz * u[2], tx * v[0] + ty * v[1] + tz * v[2]
        s, t = (tu * vv - tv * uv) / det, (tv * uu - tu * uv) / det
        if s >= 0 and t >= 0 and s + t <= 1:
            if height * height < distance:
                distance, closest = height * height, (qx, qy, qz)
        else:
            for start, direction, length in edges:
                t = min(1.0, max(0.0, ((x - start[0]) * direction[0] + (y - start[1]) * direction[1]
                                       + (z - start[2]) * direction[2]) / length))
                qx, qy, qz = (start[i] + t * direction[i] for i in range(3))
                d = (x - qx) ** 2 + (y - qy) ** 2 + (z - qz) ** 2
                if d < distance:
                    distance, closest = d, (qx, qy, qz)
    return tuple(c / w for c, w in zip(closest, _GAMUT_WEIGHTS))


@lru_cache(maxsize=4096)
def _photo_source(rgb):
    """Source-based grey protection, independent of accumulated error.

    Cache only a bounded set of source colours, so flat areas are cheap while
    photographs cannot retain an unbounded per-colour buffer.
    """
    r, g, b = (_LUT[c] for c in rgb)
    y = _LUMA[0] * r + _LUMA[1] * g + _LUMA[2] * b
    chroma = max(rgb) - min(rgb)
    if chroma <= 6:
        y = min(_WHITE_Y, max(_BLACK_Y, y))
        return y, y, y, True, 1.0
    t = min(1.0, (chroma - 6) / 18)
    neutral_bias = 1.0 - t * t * (3.0 - 2.0 * t)
    r, g, b = _map_photo_gamut(r, g, b)
    return r, g, b, False, neutral_bias


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


# EPD037A02 3.7" panel, 480x720 px. Physical active area derived from the
# 3.7" diagonal: 3.7*25.4 / hypot(480,720) = 0.1086 mm per pixel.
PIXEL_MM = 3.7 * 25.4 / (WIDTH ** 2 + HEIGHT ** 2) ** 0.5
PANEL_MM = (WIDTH * PIXEL_MM, HEIGHT * PIXEL_MM)       # ~52.1 x 78.2 mm


def placement_box(src_size, scale: float, ox: float, oy: float):
    """Integer box (x, y, w, h) on the panel for a source of src_size drawn at
    `scale` panel px per source px with its top-left at (ox, oy). The GUI uses
    the same rounding so the frame shows exactly what gets rendered."""
    w, h = src_size
    return round(ox), round(oy), max(1, round(w * scale)), max(1, round(h * scale))


def preset_placement(src_size, mode: str = "crop"):
    """(scale, ox, oy) for the 'crop' (fill) / 'pad' (fit) presets, centred."""
    w, h = src_size
    s = (max if mode == "crop" else min)(WIDTH / w, HEIGHT / h)
    return s, (WIDTH - w * s) / 2, (HEIGHT - h * s) / 2


def place_image(img: Image.Image, rotate: int = 0, scale: float | None = None,
                ox: float = 0.0, oy: float = 0.0) -> Image.Image:
    """Free placement: EXIF-transpose, rotate (deg CCW, multiple of 90), resize
    by `scale` and paste at (ox, oy) on a white 480x720 panel. Parts outside the
    panel are cropped. scale=None -> centred fill (same as fit_image crop)."""
    img = ImageOps.exif_transpose(img).convert("RGB")
    if rotate % 360:
        img = img.rotate(rotate % 360, expand=True)
    if scale is None:
        scale, ox, oy = preset_placement(img.size, "crop")
    if not (0 < scale < 1000):
        raise ValueError("bad scale")
    x, y, w, h = placement_box(img.size, scale, ox, oy)
    canvas = Image.new("RGB", (WIDTH, HEIGHT), INK_RGB[1])
    # only resample the visible part (zoomed-in huge images stay cheap)
    vx0, vy0, vx1, vy1 = max(x, 0), max(y, 0), min(x + w, WIDTH), min(y + h, HEIGHT)
    if vx1 <= vx0 or vy1 <= vy0:
        return canvas
    sx, sy = img.width / w, img.height / h
    part = img.resize((vx1 - vx0, vy1 - vy0), Image.LANCZOS,
                      box=((vx0 - x) * sx, (vy0 - y) * sy, (vx1 - x) * sx, (vy1 - y) * sy))
    canvas.paste(part, (vx0, vy0))
    return canvas


def dither(img: Image.Image, dither: bool = True) -> list[int]:
    """Serpentine Floyd–Steinberg in linear light, with OKLab ink selection.

    Source greys use luminance-only black/white diffusion. Colourful sources
    are gamut-mapped before diffusion; two error rows bound working memory.
    Returns row-major ink ids, length WIDTH*HEIGHT. False selects flat artwork.
    """
    if img.size != (WIDTH, HEIGHT):
        raise ValueError("image must be 480x720")
    if not dither:
        return nearest(img)
    px = img.convert("RGB").load()
    w, h = WIDTH, HEIGHT
    out = [0] * (w * h)
    current, nxt = [0.0] * ((w + 2) * 3), [0.0] * ((w + 2) * 3)
    for y in range(h):
        rev = y & 1
        xs = range(w - 1, -1, -1) if rev else range(w)
        d = -1 if rev else 1
        for x in xs:
            r, g, b, mono, neutral_bias = _photo_source(px[x, y])
            e = (x + 1) * 3
            if mono:
                value = r + (_LUMA[0] * current[e] + _LUMA[1] * current[e + 1] + _LUMA[2] * current[e + 2])
                best = _PHOTO_PAL[0] if value <= (_BLACK_Y + _WHITE_Y) / 2 else _PHOTO_PAL[1]
                error = (value - best[3]) * DIFFUSE
                errors = (error, error, error)
            else:
                r, g, b = r + current[e], g + current[e + 1], b + current[e + 2]
                # Clamp for perceptual lookup only. Preserve the signed linear
                # residual, including overshoot, so ink coverage stays correct.
                L, A, B = _oklab(min(1.0, max(0.0, r)), min(1.0, max(0.0, g)), min(1.0, max(0.0, b)))
                chroma_weight = CHROMA_W * (neutral_bias + (1.0 - neutral_bias) / 12.0)
                best, distance = _PHOTO_PAL[0], float("inf")
                for entry in _PHOTO_PAL:
                    pl, pa, pb = entry[2]
                    score = (L - pl) ** 2 + chroma_weight * ((A - pa) ** 2 + (B - pb) ** 2)
                    if score < distance:
                        best, distance = entry, score
                errors = tuple((v - ink) * DIFFUSE for v, ink in zip((r, g, b), best[1]))
            bi = best[0]
            out[y * w + x] = bi
            for c, error in enumerate(errors):
                current[e + d * 3 + c] += error * (7 / 16)
                nxt[e - d * 3 + c] += error * (3 / 16)
                nxt[e + c] += error * (5 / 16)
                nxt[e + d * 3 + c] += error * (1 / 16)
        current, nxt = nxt, [0.0] * len(nxt)
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


MODES = ("photo", "illustration")
FITS = ("crop", "pad")
ORIENTATIONS = ("portrait", "landscape", "auto")


def orientation_rotate(img: Image.Image, orientation: str) -> int:
    """Degrees CCW for fit_image(). 'landscape' turns a wide picture 90 deg so it
    fills the 480x720 panel (view the device sideways, top edge to the left)."""
    if orientation == "auto":
        w, h = ImageOps.exif_transpose(img).size
        orientation = "landscape" if w > h else "portrait"
    if orientation == "portrait":
        return 0
    if orientation == "landscape":
        return 270          # PIL rotate() is CCW; 270 CCW == 90 clockwise
    raise ValueError(f"orientation must be one of {ORIENTATIONS}")


def nearest(img: Image.Image) -> list[int]:
    """'illustration' mode: per-pixel nearest ink, no dithering -> flat blocks.
    Cached per colour, so flat artwork converts fast."""
    if img.size != (WIDTH, HEIGHT):
        raise ValueError("image must be 480x720")
    cache: dict = {}
    out = []
    raw = img.convert("RGB").tobytes()
    for rgb in zip(raw[0::3], raw[1::3], raw[2::3]):
        ink = cache.get(rgb)
        if ink is None:
            L, A, B = _oklab(*(_LUT[c] for c in rgb))
            ink = min(_PAL_OK, key=lambda e: (L - e[2][0]) ** 2 + CHROMA_W * ((A - e[2][1]) ** 2 + (B - e[2][2]) ** 2))[0]
            cache[rgb] = ink
        out.append(ink)
    return out


def render_frame(frame: bytes) -> Image.Image:
    """Packed panel frame -> RGB image exactly as the panel lays it out
    (480x720 portrait, the 6 reference ink colours)."""
    return preview(unpack(frame))


def convert(path_or_img, rotate: int = 0, mode: str = "crop", use_dither: bool = True,
            style: str | None = None, orientation: str | None = None, place: dict | None = None):
    """Returns (row-major ink ids, packed frame).
    style: 'photo' (Floyd-Steinberg) | 'illustration' (nearest colour); when
    None, use_dither picks. orientation (portrait/landscape/auto) overrides rotate."""
    img = path_or_img if isinstance(path_or_img, Image.Image) else Image.open(path_or_img)
    if mode not in FITS:
        raise ValueError(f"fit must be one of {FITS}")
    if orientation:
        rotate = orientation_rotate(img, orientation)
    if style is None:
        style = "photo" if use_dither else "illustration"
    if place:   # free placement from the GUI: {rotate, scale, ox, oy}
        fitted = place_image(img, int(place.get("rotate", 0)), float(place["scale"]),
                             float(place["ox"]), float(place["oy"]))
    else:
        fitted = fit_image(img, rotate, mode)
    if style == "photo":
        ids = dither(fitted, True)
    elif style == "illustration":
        ids = nearest(fitted)
    else:
        raise ValueError(f"style must be one of {MODES}")
    return ids, pack(ids)
