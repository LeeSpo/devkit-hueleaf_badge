import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))

import numpy as np
from PIL import Image, ImageDraw
import pytest

import hlformat as F
import hlfine as Q


def _energy(target, palette, ids, w, h, kernel):
    """Independent full convolution, rather than the search's incremental field."""
    error = (palette[ids] - target).reshape((h, w, 3))
    radius = kernel.shape[0] // 2
    padded = np.pad(error, ((radius, radius), (radius, radius), (0, 0)))
    filtered = np.zeros_like(error)
    for y in range(kernel.shape[0]):
        for x in range(kernel.shape[1]):
            filtered += kernel[y, x] * padded[y:y + h, x:x + w]
    return np.sum(error * filtered) / (w * h)


def test_search_decreases_independently_computed_error_and_is_repeatable():
    w, h = 32, 24
    rng = np.random.default_rng(1927)
    palette = np.array([Q._opponent(rgb) for _, rgb in F._PAL_LIN])
    mono = np.arange(w * h) % 3 == 0
    a, b = rng.integers(0, 6, size=(2, w * h), dtype=np.uint8)
    a[mono], b[mono] = 0, 1
    mix = rng.random((w * h, 1))
    target = palette[a] * mix + palette[b] * (1 - mix)
    kernel = Q._kernel(Q._COARSE)
    results = [Q._search(target, palette, mono, w, h, a, kernel, 8, 20261104) for _ in range(2)]
    ids, energies, changes = results[0]
    assert np.array_equal(ids, results[1][0])
    assert np.array_equal(a[mono], np.zeros(np.count_nonzero(mono)))  # input not mutated
    assert set(ids) <= set(range(6)) and set(ids[mono]) <= {0, 1}
    assert changes.sum() > 0
    assert np.all(np.diff(energies) <= 1e-11) and energies[-1] < energies[0]
    assert energies[0] == pytest.approx(_energy(target, palette, a, w, h, kernel), abs=1e-10)
    assert energies[-1] == pytest.approx(_energy(target, palette, ids, w, h, kernel), abs=1e-10)


def test_fine_mixed_color_and_gray_frame_preserves_neutral_regions():
    img = Image.new("RGB", (F.WIDTH, F.HEIGHT), (128, 128, 128))
    draw = ImageDraw.Draw(img)
    draw.rectangle((0, 0, 239, 239), fill=(220, 170, 150))
    draw.rectangle((240, 0, 479, 239), fill=(80, 135, 180))
    ids, frame = F.convert(img, style="fine")
    assert len(frame) == F.FRAME_BYTES and F.unpack(frame) == ids
    assert set(ids) <= F.VALID_INKS
    assert set(ids[240 * F.WIDTH:]) <= {0, 1}
    # Check mean color in the interior of each representable patch, not the
    # optimizer's own score. Edge diffusion must not cause a material tone shift.
    linear = np.array([dict(F._PAL_LIN)[ink] for ink in ids]).reshape((F.HEIGHT, F.WIDTH, 3))
    for x, rgb in [(0, (220, 170, 150)), (240, (80, 135, 180))]:
        actual = linear[40:200, x + 40:x + 200].mean(axis=(0, 1))
        expected = F._photo_source(rgb)[:3]
        assert np.max(np.abs(actual - expected)) < .008
    gray_y = linear[300:660, 40:440] @ np.array(F._LUMA)
    assert abs(gray_y.mean() - F._lin(128)) < .001


def test_fine_rejects_wrong_image_size():
    with pytest.raises(ValueError, match="480x720"):
        Q.dither_fine(Image.new("RGB", (10, 10)))


def test_missing_fine_components_leave_existing_modes_available(monkeypatch):
    monkeypatch.delitem(sys.modules, "hlfine", raising=False)
    monkeypatch.setitem(sys.modules, "numpy", None)
    monkeypatch.setitem(sys.modules, "numba", None)
    img = Image.new("RGB", (F.WIDTH, F.HEIGHT), F.INK_RGB[0])
    assert set(F.convert(img)[0]) == {0}  # photo is still the default
    assert set(F.convert(img, style="illustration")[0]) == {0}
    with pytest.raises(RuntimeError, match="fine mode dependencies.*requirements.txt") as exc:
        F.convert(img, style="fine")
    import hueleaf_gui as G
    assert "精细模式组件未安装" in G.friendly_error(exc.value)
