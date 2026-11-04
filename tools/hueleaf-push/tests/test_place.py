import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
from PIL import Image
import hlformat as F
import hueleaf_gui as G


def test_preset_crop_matches_fit_image():
    img = Image.new("RGB", (1000, 500), (255, 0, 0))
    s, ox, oy = F.preset_placement((1000, 500), "crop")
    assert F.place_image(img, 0, s, ox, oy).size == (F.WIDTH, F.HEIGHT)
    assert abs(s - 720 / 500) < 1e-9 and oy == 0


def test_place_offset_and_white_fill():
    img = Image.new("RGB", (100, 100), (0, 0, 0))
    out = F.place_image(img, 0, 1.0, 10, 20)
    assert out.getpixel((10, 20)) == (0, 0, 0) and out.getpixel((109, 119)) == (0, 0, 0)
    assert out.getpixel((9, 20)) == F.INK_RGB[1] and out.getpixel((110, 20)) == F.INK_RGB[1]
    assert F.place_image(img, 0, 1.0, 900, 0).getpixel((0, 0)) == F.INK_RGB[1]   # fully outside


def test_place_rotation_is_ccw_like_fit_image():
    img = Image.new("RGB", (200, 100), (255, 255, 255))
    img.paste((0, 0, 0), (0, 0, 10, 10))           # black top-left
    out = F.place_image(img, 270, 1.0, 0, 0)       # 90 deg clockwise -> black at top-right of 100x200
    assert out.getpixel((95, 3)) == (0, 0, 0) and out.getpixel((3, 3)) != (0, 0, 0)


def test_convert_with_place_and_panel_mm():
    img = Image.new("RGB", (300, 300), F.INK_RGB[3])
    ids, fr = F.convert(img, style="illustration", place={"rotate": 0, "scale": 1, "ox": 0, "oy": 0})
    assert ids[0] == 3 and ids[400] == 1 and len(fr) == F.FRAME_BYTES
    assert 51 < F.PANEL_MM[0] < 53 and 77 < F.PANEL_MM[1] < 79


def test_settings_roundtrip(tmp_path):
    p = str(tmp_path / "s.json")
    G.save_settings({"px_per_mm": 4.2, "placements": {"a": {"scale": 1}}}, p)
    G.save_settings({"placements": {"b": {"scale": 2}, "a": None}}, p)
    d = G.load_settings(p)
    assert d["px_per_mm"] == 4.2 and d["placements"] == {"b": {"scale": 2}}
