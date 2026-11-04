import os, sys, zlib
from collections import Counter

import pytest
sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
from PIL import Image
import hlformat as F
import hueleaf_push as P


def test_pack_layout_matches_official_makeFrame():
    ids = [1] * (F.WIDTH * F.HEIGHT)
    ids[0 * F.WIDTH + 0] = 3        # (x0,y0) red
    ids[1 * F.WIDTH + 0] = 5        # (x0,y1) blue
    ids[0 * F.WIDTH + 479] = 0      # (x479,y0) black
    ids[719 * F.WIDTH + 479] = 6    # (x479,y719) green
    fr = F.pack(ids)
    assert len(fr) == 172800
    assert fr[479 * 360] == 0x35            # x=0 is the LAST column, high nibble = even y
    assert fr[0] == 0x01                    # x=479 first column, y0 black / y1 white
    assert fr[359] == 0x16                  # y718 white / y719 green
    assert F.unpack(fr) == ids


def test_pack_rejects_invalid_ink():
    ids = [4] * (F.WIDTH * F.HEIGHT)
    try:
        F.pack(ids)
    except ValueError:
        return
    raise AssertionError


def test_solid_palette_colours_dither_to_themselves():
    for ink, rgb in F.PALETTE:
        ids = F.dither(Image.new("RGB", (F.WIDTH, F.HEIGHT), rgb))
        assert set(ids) == {ink}


def test_grey_dithers_to_black_white():
    ids = F.dither(Image.new("RGB", (F.WIDTH, F.HEIGHT), (128, 128, 128)))
    assert set(ids) == {0, 1}
    # A middle sRGB grey needs about 24% white ink, not 50%: ink coverage
    # averages reflected light, which is linear rather than sRGB encoded.
    assert 0.22 < ids.count(1) / len(ids) < 0.25


def linear_rgb(rgb):
    return tuple(v / 255 / 12.92 if v / 255 <= 0.04045
                 else ((v / 255 + 0.055) / 1.055) ** 2.4 for v in rgb)


def mean_ink_rgb(ids):
    counts = Counter(ids)
    return tuple(sum(counts[ink] * linear_rgb(rgb)[c] for ink, rgb in F.PALETTE) / len(ids)
                 for c in range(3))


def luminance(rgb):
    return sum(c * w for c, w in zip(rgb, (0.2126, 0.7152, 0.0722)))


@pytest.mark.parametrize("value", [32, 64, 96, 128, 160, 192, 224])
def test_photo_preserves_grey_luminance(monkeypatch, value):
    monkeypatch.setattr(F, "WIDTH", 96)
    monkeypatch.setattr(F, "HEIGHT", 80)
    rgb = (value,) * 3
    ids = F.dither(Image.new("RGB", (F.WIDTH, F.HEIGHT), rgb))
    assert set(ids) <= {0, 1}
    assert abs(luminance(mean_ink_rgb(ids)) - luminance(linear_rgb(rgb))) < 0.004


@pytest.mark.parametrize("rgb", [(220, 170, 150), (217, 202, 199), (182, 193, 192)])
def test_photo_preserves_representable_colours(monkeypatch, rgb):
    monkeypatch.setattr(F, "WIDTH", 96)
    monkeypatch.setattr(F, "HEIGHT", 80)
    ids = F.dither(Image.new("RGB", (F.WIDTH, F.HEIGHT), rgb))
    assert max(abs(a - b) for a, b in zip(mean_ink_rgb(ids), linear_rgb(rgb))) < 0.012


def test_gamut_mapping_preserves_mixtures_and_bounds_saturated_colours():
    # Every convex mixture of reference inks is representable, regardless of
    # its saturation. Gamut mapping must leave these colours alone.
    for i in range(len(F.PALETTE)):
        for j in range(i + 1, len(F.PALETTE)):
            a, b = linear_rgb(F.PALETTE[i][1]), linear_rgb(F.PALETTE[j][1])
            rgb = tuple(0.4 * x + 0.6 * y for x, y in zip(a, b))
            assert F._map_photo_gamut(*rgb) == pytest.approx(rgb, abs=1e-9)
    for rgb in [(1., 0., 0.), (0., 1., 0.), (0., 0., 1.), (0., 1., 1.), (1., 0., 1.)]:
        mapped = F._map_photo_gamut(*rgb)
        assert mapped != pytest.approx(rgb)
        # A projection must be inside the hull, so projecting it again is a no-op.
        assert F._map_photo_gamut(*mapped) == pytest.approx(mapped, abs=1e-9)


def test_no_dither_keeps_flat_artwork_behaviour(monkeypatch):
    monkeypatch.setattr(F, "WIDTH", 96)
    monkeypatch.setattr(F, "HEIGHT", 80)
    img = Image.new("RGB", (F.WIDTH, F.HEIGHT), (217, 202, 199))
    assert F.dither(img, False) == F.nearest(img)
    assert len(set(F.dither(img, False))) == 1


def test_saturated_colours_do_not_add_coloured_noise_to_greys(monkeypatch):
    monkeypatch.setattr(F, "WIDTH", 96)
    monkeypatch.setattr(F, "HEIGHT", 80)
    img = Image.new("RGB", (F.WIDTH, F.HEIGHT), (128, 128, 128))
    img.paste((0, 0, 255), (0, 0, F.WIDTH // 2, F.HEIGHT))
    ids = F.dither(img)
    grey_ids = [ids[y * F.WIDTH + x] for y in range(F.HEIGHT)
                for x in range(F.WIDTH // 2, F.WIDTH)]
    assert set(grey_ids) <= {0, 1}
    assert abs(luminance(mean_ink_rgb(grey_ids)) - luminance(linear_rgb((128,) * 3))) < 0.008


def test_fit_portrait_and_rotate():
    img = Image.new("RGB", (1000, 500), (255, 0, 0))
    assert F.fit_image(img).size == (480, 720)
    assert F.fit_image(img, rotate=90, mode="pad").size == (480, 720)


def test_frame_crc_and_header():
    f = P.frame(0x01, 7, b"abc")
    assert f[:4] == b"HLP1" and f[4] == 1 and f[5] == 7
    assert int.from_bytes(f[-4:], "little") == zlib.crc32(f[4:-4])
    cmd, seq, status, data = P.parse_reply(P.frame(0x81, 7, (0).to_bytes(4, "little") + b"ok"))
    assert (cmd, seq, status, data) == (0x81, 7, 0, b"ok")
