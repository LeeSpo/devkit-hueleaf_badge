import os, sys, zlib
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
    bw = (ids.count(0) + ids.count(1)) / len(ids)
    assert bw > 0.85                      # neutral grey -> black/white mix, no colour noise
    assert 0.35 < ids.count(0) / len(ids) < 0.65


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
