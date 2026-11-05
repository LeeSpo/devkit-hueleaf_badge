"""Experimental six-ink direct search, using the tested multiscale model.

Starting from the photo converter, replace one ink or swap two nearby inks
only when the filtered error decreases. This is a local search, not a promise
of a global optimum or a measured model of the physical EPD.

Numba compiles the numeric loops without a system C compiler. Keep all JIT
helpers here so its file cache is invalidated when the model changes.
"""
from __future__ import annotations

from functools import lru_cache
import math

import numpy as np
from numba import njit

# (Gaussian sigma in panel pixels, weight, finite filter radius). Their
# autocorrelations define a positive semidefinite filtered-error objective.
_COARSE = ((.65, .015, 2), (1.3, .085, 4), (3., .9, 7))
_QUALITY = ((.65, .005, 2), (1.3, .045, 4), (3., .45, 7), (6., .5, 12))
_SWAPS = ((-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (1, -1),
          (-1, 1), (1, 1), (-2, 0), (2, 0), (0, -2), (0, 2))


def _opponent(rgb):
    r, g, b = rgb
    return (.2126 * r + .7152 * g + .0722 * b, .65 * (r - g), .65 * (b - g))


@lru_cache(maxsize=2)
def _kernel(scales):
    radius = max(2 * r for _, _, r in scales)
    side = 2 * radius + 1
    values = [0.] * side**2
    for sigma, weight, r in scales:
        g = [math.exp(-j * j / (2 * sigma * sigma)) for j in range(-r, r + 1)]
        total = sum(g)
        g = [v / total for v in g]
        ac = [sum(g[t] * g[t + j] for t in range(len(g)) if 0 <= t + j < len(g))
              for j in range(-2 * r, 2 * r + 1)]
        for dy in range(-2 * r, 2 * r + 1):
            for dx in range(-2 * r, 2 * r + 1):
                values[(dy + radius) * side + dx + radius] += weight * ac[dy + 2 * r] * ac[dx + 2 * r]
    return np.array(values, dtype=np.float64).reshape((side, side))


def _prepare(img, palette):
    import hlformat as F
    count = img.width * img.height
    target = np.empty((count, 3), dtype=np.float64)
    mono = np.empty(count, dtype=np.bool_)
    black_y, white_y = F._BLACK_Y, F._WHITE_Y
    raw = img.convert("RGB").tobytes()
    for p, rgb in enumerate(zip(raw[0::3], raw[1::3], raw[2::3])):
        r, g, b, gray, _ = F._photo_source(rgb)
        if gray:
            # The reference white is slightly warm. Use the physical B/W
            # mixture at this luminance instead of an unreachable neutral RGB.
            coverage = (r - black_y) / (white_y - black_y)
            color = tuple(palette[0, c] + coverage * (palette[1, c] - palette[0, c]) for c in range(3))
        else:
            color = (r, g, b)
        target[p] = _opponent(color)
        mono[p] = gray
    return target, mono


@njit(cache=True, nogil=True)
def _update(field, w, h, p, delta, kernel):
    radius = kernel.shape[0] // 2
    x, y = p % w, p // w
    for dy in range(-radius, radius + 1):
        yy = y + dy
        if yy < 0 or yy >= h:
            continue
        for dx in range(-radius, radius + 1):
            xx = x + dx
            if xx < 0 or xx >= w:
                continue
            k = kernel[dy + radius, dx + radius]
            q = yy * w + xx
            for c in range(3):
                field[q, c] += delta[c] * k


@njit(cache=True, nogil=True)
def _search(target, palette, mono, w, h, initial, kernel, passes, seed):
    """Minimize sum(error * (kernel convolved with error)), zero outside.

    Return energy history for independent regression checks. No fastmath:
    signed residuals and deterministic move acceptance must be preserved.
    """
    n = w * h
    ids = initial.copy()
    field = np.zeros((n, 3), dtype=np.float64)
    order = np.arange(n)
    delta = np.empty(3, dtype=np.float64)
    for p in range(n):
        for c in range(3):
            delta[c] = palette[ids[p], c] - target[p, c]
        _update(field, w, h, p, delta, kernel)
    radius = kernel.shape[0] // 2
    k0 = kernel[radius, radius]
    energies = np.empty(passes + 1, dtype=np.float64)
    changes = np.zeros(passes, dtype=np.int64)
    state = np.uint32(seed)
    for iteration in range(passes + 1):
        energy = 0.
        for p in range(n):
            for c in range(3):
                energy += (palette[ids[p], c] - target[p, c]) * field[p, c]
        energies[iteration] = energy / n
        if iteration == passes or (iteration and changes[iteration - 1] == 0):
            return ids, energies[:iteration + 1], changes[:iteration]
        # Fixed xorshift32 + Fisher-Yates: no process-global random state.
        for i in range(n - 1, 0, -1):
            state = np.uint32(state ^ np.uint32(state << 13))
            state = np.uint32(state ^ (state >> 17))
            state = np.uint32(state ^ np.uint32(state << 5))
            j = int(state % (i + 1))
            order[i], order[j] = order[j], order[i]
        for i in range(n):
            p = order[i]
            old = int(ids[p])
            newink, swap = old, -1
            x, y = p % w, p // w
            best = -1e-12
            for k in range(2 if mono[p] else 6):
                if k == old:
                    continue
                cost = 0.
                for c in range(3):
                    d = palette[k, c] - palette[old, c]
                    cost += 2 * d * field[p, c] + k0 * d * d
                if cost < best:
                    best, newink, swap = cost, k, -1
            for dx, dy in _SWAPS:
                xx, yy = x + dx, y + dy
                if xx < 0 or xx >= w or yy < 0 or yy >= h:
                    continue
                q = yy * w + xx
                k = int(ids[q])
                if k == old or (mono[p] and k > 1) or (mono[q] and old > 1):
                    continue
                kpq = kernel[dy + radius, dx + radius]
                cost = 0.
                for c in range(3):
                    d = palette[k, c] - palette[old, c]
                    cost += 2 * d * (field[p, c] - field[q, c]) + 2 * (k0 - kpq) * d * d
                if cost < best:
                    best, newink, swap = cost, k, q
            if newink == old:
                continue
            for c in range(3):
                delta[c] = palette[newink, c] - palette[old, c]
            ids[p] = newink
            _update(field, w, h, p, delta, kernel)
            if swap >= 0:
                ids[swap] = old
                for c in range(3):
                    delta[c] = -delta[c]
                _update(field, w, h, swap, delta, kernel)
            changes[iteration] += 1
    return ids, energies, changes


def dither_fine(img):
    """The tested winning path: photo -> coarse search -> quality search.

    Source gamut mapping, grayscale protection and CHROMA_W for the initial
    photo pass are shared with hlformat. The subsequent model weights are fixed.
    """
    import hlformat as F
    if img.size != (F.WIDTH, F.HEIGHT):
        raise ValueError("image must be 480x720")
    inks = [ink for ink, _ in F.PALETTE]
    lookup = {ink: i for i, ink in enumerate(inks)}
    initial = np.array([lookup[ink] for ink in F.dither(img)], dtype=np.uint8)
    palette = np.array([rgb for _, rgb in F._PAL_LIN], dtype=np.float64)
    target, mono = _prepare(img, palette)
    opponent = np.array([_opponent(rgb) for rgb in palette], dtype=np.float64)
    coarse, _, _ = _search(target, opponent, mono, img.width, img.height, initial,
                          _kernel(_COARSE), 32, 20261104)
    result, _, _ = _search(target, opponent, mono, img.width, img.height, coarse,
                          _kernel(_QUALITY), 56, 20261105)
    return [inks[i] for i in result]
