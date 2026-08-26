"""Resolution-independent procedural material generators.

WHY PROCEDURAL, NOT UPSCALING
-----------------------------
A 256x256 legacy brick texture cannot be honestly turned into a 2K one by
upscaling: every added texel is invented. Regenerating the same wall from a
*parametric model* fitted to the source art produces real high-frequency
detail derived from geometry (row pitch, mortar width, bevel) plus colour
statistics measured from the original. The art direction survives; the
resolution is genuinely new information rather than hallucinated pixels.

PROVENANCE
----------
The brick/pavement/scratch pattern functions below are CPU ports of node
definitions from Material Maker (https://github.com/RodZill4/material-maker),
MIT licensed, Copyright (c) Rodolphe Suescun and contributors. The GLSL
originals are `addons/material_maker/nodes/{bricks,arc_pavement,scratches}.mmg`
and the `rand/rand2/rand3` hashes in `addons/material_maker/shader_functions.tres`.
See THIRD_PARTY_NOTICES.md. Node names and parameter names are kept identical
so a Material Maker graph and a rorsmith call describe the same surface.

WHY A NUMPY PORT RATHER THAN A HEADLESS GLSL EVALUATOR
------------------------------------------------------
Evaluating the .mmg graphs as GLSL would need the whole Material Maker node
engine: `$param` / `$(name_uv)` substitution, graph topological expansion, a
shader compiler and a GPU or software rasteriser context. The generators
CityWorld actually needs are a handful of closed-form functions of `vec2 uv`,
and every one of them vectorises to numpy in a few dozen lines that run a 2K
map in well under a second on the CPU with no context, no driver, and no
build step. The port is checked against the GLSL line by line; the only
intentional divergence is float32 arithmetic order, which changes the chaotic
hash noise but not the pattern geometry.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Callable

import numpy as np

from .paths import RorsmithError

F32 = np.float32


# --------------------------------------------------------------------------
# Material Maker shader_functions.tres - MIT, ported verbatim.
# --------------------------------------------------------------------------

def _fract(x: np.ndarray) -> np.ndarray:
    return x - np.floor(x)


def _mm_rand(x: np.ndarray) -> np.ndarray:
    """float rand(vec2 x) - Material Maker, MIT."""
    d = x[..., 0] * F32(13.9898) + x[..., 1] * F32(8.141)
    return _fract(np.cos(np.mod(d, F32(3.14))) * F32(43758.5))


def _mm_rand2(x: np.ndarray) -> np.ndarray:
    """vec2 rand2(vec2 x) - Material Maker, MIT."""
    a = x[..., 0] * F32(13.9898) + x[..., 1] * F32(8.141)
    b = x[..., 0] * F32(3.4562) + x[..., 1] * F32(17.398)
    return _fract(
        np.cos(np.mod(np.stack([a, b], axis=-1), F32(3.14))) * F32(43758.5)
    )


def _mm_rand3(x: np.ndarray) -> np.ndarray:
    """vec3 rand3(vec2 x) - Material Maker, MIT."""
    a = x[..., 0] * F32(13.9898) + x[..., 1] * F32(8.141)
    b = x[..., 0] * F32(3.4562) + x[..., 1] * F32(17.398)
    c = x[..., 0] * F32(13.254) + x[..., 1] * F32(5.867)
    return _fract(
        np.cos(np.mod(np.stack([a, b, c], axis=-1), F32(3.14))) * F32(43758.5)
    )


def _uv_grid(size: int) -> np.ndarray:
    """Texel-centre UVs in [0,1)^2, y down, matching Ogre texture space."""
    axis = (np.arange(size, dtype=F32) + F32(0.5)) / F32(size)
    v, u = np.meshgrid(axis, axis, indexing="ij")
    return np.stack([u, v], axis=-1)


# --------------------------------------------------------------------------
# bricks.mmg - MIT. `oldbricks_rb` / `oldbricks_rb2` / `oldbricks_hb` and
# `oldbrick`, ported to numpy.
# --------------------------------------------------------------------------

def _bricks_rb(uv, count, repeat, offset):
    count = count * repeat
    x_offset = offset * (_fract(uv[..., 1] * count[1] * F32(0.5)) >= F32(0.5)).astype(F32)
    bx = np.floor(uv[..., 0] * count[0] - x_offset) + x_offset
    by = np.floor(uv[..., 1] * count[1])
    bmin = np.stack([bx / count[0], by / count[1]], axis=-1)
    return bmin, bmin + np.array([1.0 / count[0], 1.0 / count[1]], dtype=F32)


def _bricks_rb2(uv, count, repeat, offset):
    count = count * repeat
    step = (_fract(uv[..., 1] * count[1] * F32(0.5)) >= F32(0.5)).astype(F32)
    x_offset = offset * step
    cx = count[0] * (F32(1.0) + step)
    bx = np.floor(uv[..., 0] * cx - x_offset) + x_offset
    by = np.floor(uv[..., 1] * count[1])
    bmin = np.stack([bx / cx, by / count[1]], axis=-1)
    bmax = bmin + np.stack([F32(1.0) / cx, np.full_like(bx, F32(1.0) / count[1])], axis=-1)
    return bmin, bmax


def _bricks_hb(uv, count, repeat, offset):
    pc = count[0] + count[1]
    c = pc * repeat
    corner = np.floor(uv * c)
    cdiff = np.mod(corner[..., 0] - corner[..., 1], pc)
    horizontal = cdiff < count[0]
    min_h = corner - np.stack([cdiff, np.zeros_like(cdiff)], axis=-1)
    max_h = min_h + np.array([count[0], 1.0], dtype=F32)
    min_v = corner - np.stack([np.zeros_like(cdiff), pc - cdiff - F32(1.0)], axis=-1)
    max_v = min_v + np.array([1.0, count[1]], dtype=F32)
    sel = horizontal[..., None]
    return np.where(sel, min_h, min_v) / c, np.where(sel, max_h, max_v) / c


_BRICK_PATTERNS: dict[str, Callable] = {
    "running_bond": _bricks_rb,
    "running_bond_2": _bricks_rb2,
    "herringbone": _bricks_hb,
}


def _brick_field(uv, bmin, bmax, mortar, round_radius, bevel):
    """vec4 oldbrick(...) - Material Maker, MIT. Returns (mask, centre)."""
    size = bmax - bmin
    min_size = np.minimum(size[..., 0], size[..., 1])
    mortar = mortar * min_size
    bevel = np.maximum(F32(1e-6), bevel * min_size)
    round_radius = round_radius * min_size
    centre = F32(0.5) * (bmin + bmax)
    d = np.abs(uv - centre) - F32(0.5) * size + (round_radius + mortar)[..., None]
    outside = np.linalg.norm(np.maximum(d, F32(0.0)), axis=-1)
    inside = np.minimum(np.maximum(d[..., 0], d[..., 1]), F32(0.0))
    signed = outside + inside - round_radius
    return np.clip(-signed / bevel, F32(0.0), F32(1.0)), centre


# --------------------------------------------------------------------------
# arc_pavement.mmg - MIT.
# --------------------------------------------------------------------------

def _arc_pavement(uv, acount, lcount):
    pi = F32(math.pi)
    radius = F32(0.5 / math.sqrt(2.0))
    x = uv[..., 0].copy()
    y = uv[..., 1]
    ux = F32(0.5) * _fract(x + F32(0.5)) + F32(0.25)
    centre = (ux - F32(0.5)) / radius
    centre = centre * centre
    centre = np.floor(acount * (y - radius * np.sqrt(np.maximum(F32(0.0), F32(1.0) - centre))) + F32(0.5)) / acount
    vx = ux - F32(0.5)
    vy = y - centre
    corner_angle = F32(0.85) / acount + F32(0.25) * pi
    count_angle = (pi - F32(2.0) * corner_angle) / (lcount + np.floor(np.mod(centre * acount, F32(2.0))))
    angle = np.mod(np.arctan2(vy, vx), F32(2.0) * pi)
    length = np.sqrt(vx * vx + vy * vy)
    local_uvy = F32(0.5) + acount * (length - radius) * (F32(1.66) - F32(0.71) * np.cos(F32(1.44) * (angle - pi * F32(0.5))))

    low = angle < corner_angle
    high = angle > pi - corner_angle
    base_angle = corner_angle + (np.floor((angle - corner_angle) / count_angle) + F32(0.5)) * count_angle
    lu = (angle - base_angle) / count_angle + F32(0.5)
    lv = F32(1.0) - local_uvy
    base_angle = np.where(low, F32(0.25) * pi, base_angle)
    lu = np.where(low, (angle - F32(0.25) * pi) / corner_angle * F32(0.4) * acount + F32(0.55), lu)
    base_angle = np.where(high, F32(0.75) * pi, base_angle)
    lu = np.where(high, local_uvy, lu)
    lv = np.where(high, F32(0.45) - (F32(0.75) * pi - angle) / corner_angle * F32(0.4) * acount, lv)
    seed = np.stack([_fract(centre), np.where(low | high, F32(0.0), base_angle)], axis=-1)
    return np.stack([lu, lv], axis=-1), seed


def _pavement_mask(local_uv, bevel, mortar):
    """float pavement(vec2, float, float) - Material Maker, MIT."""
    a = np.abs(local_uv - F32(0.5))
    return np.clip(
        (F32(0.5) * (F32(1.0) - mortar) - np.maximum(a[..., 0], a[..., 1]))
        / max(F32(1e-4), bevel),
        F32(0.0),
        F32(1.0),
    )


# --------------------------------------------------------------------------
# scratches.mmg - MIT.
# --------------------------------------------------------------------------

def _scratch(uv, size, waviness, angle, randomness, seed):
    tau = F32(6.28318530718)
    subdivide = F32(math.floor(1.0 / size[0]))
    cut = F32(size[0]) * subdivide
    p = uv * subdivide
    r1 = _mm_rand2(np.floor(p) + seed)
    r2 = _mm_rand2(r1)
    p = _fract(p)
    border = F32(10.0) * np.minimum(p, F32(1.0) - p)
    p = F32(2.0) * p - F32(1.0)
    a = tau * (F32(angle) + (r1[..., 0] - F32(0.5)) * F32(randomness))
    c, s = np.cos(a), np.sin(a)
    px = c * p[..., 0] + s * p[..., 1]
    py = s * p[..., 0] - c * p[..., 1]
    py = py + F32(2.0) * r1[..., 1] - F32(1.0)
    py = py + F32(0.5) * F32(waviness) * np.cos(F32(2.0) * px + tau * r2[..., 1])
    px = px / cut
    py = py / (subdivide * F32(size[1]))
    return (
        np.minimum(border[..., 0], border[..., 1])
        * (F32(1.0) - px * px)
        * np.maximum(F32(0.0), F32(1.0) - F32(1000.0) * py * py)
    )


def _scratches(uv, layers, size, waviness, angle, randomness, seed):
    value = np.zeros(uv.shape[:-1], dtype=F32)
    s = np.array(seed, dtype=F32)
    for _ in range(int(layers)):
        s = _mm_rand2(np.broadcast_to(s, uv.shape).copy())[0, 0]
        value = np.maximum(value, _scratch(_fract(uv + s), size, waviness, angle, randomness, s))
    return np.clip(value, F32(0.0), F32(1.0))


# --------------------------------------------------------------------------
# Value-noise fbm. NOT a Material Maker port - dirt.mmg is a multi-node graph
# whose evaluation needs the graph engine, so this is an ordinary tileable
# value-noise fbm written here and named as such.
# --------------------------------------------------------------------------

def _value_noise(size: int, cells: int, seed: float) -> np.ndarray:
    rng = np.random.default_rng(int(seed * 1000003) & 0x7FFFFFFF)
    lattice = rng.random((cells, cells), dtype=np.float64).astype(F32)
    lattice = np.pad(lattice, ((0, 1), (0, 1)), mode="wrap")
    uv = _uv_grid(size) * F32(cells)
    x0 = np.floor(uv[..., 0]).astype(np.int32)
    y0 = np.floor(uv[..., 1]).astype(np.int32)
    fx = uv[..., 0] - x0
    fy = uv[..., 1] - y0
    sx = fx * fx * (F32(3.0) - F32(2.0) * fx)
    sy = fy * fy * (F32(3.0) - F32(2.0) * fy)
    x0 %= cells
    y0 %= cells
    x1, y1 = (x0 + 1) % cells, (y0 + 1) % cells
    a = lattice[y0, x0]
    b = lattice[y0, x1]
    c = lattice[y1, x0]
    d = lattice[y1, x1]
    return (a * (1 - sx) + b * sx) * (1 - sy) + (c * (1 - sx) + d * sx) * sy


def fbm(size: int, cells: int = 8, octaves: int = 5, gain: float = 0.5, seed: float = 1.0) -> np.ndarray:
    total = np.zeros((size, size), dtype=F32)
    amplitude = F32(1.0)
    norm = F32(0.0)
    for octave in range(max(1, int(octaves))):
        c = max(1, int(cells * (2 ** octave)))
        if c > size:
            break
        total += amplitude * _value_noise(size, c, seed + octave * 7.13)
        norm += amplitude
        amplitude *= F32(gain)
    return total / max(F32(1e-6), norm)


# --------------------------------------------------------------------------
# Maps derived from a height field.
# --------------------------------------------------------------------------

def normal_from_height(height: np.ndarray, strength: float = 1.0) -> np.ndarray:
    """Tangent-space normal from a height field by central differences.

    Method: wrapped 3x3 Sobel on the height field, slope scaled by
    `strength * size` so the result is resolution-independent, then
    normalised and encoded to the OpenGL +Y-up convention Ogre's PBSM_NORMAL
    expects (x right, y up, z out; 0.5 is flat).
    """
    h = height.astype(F32)
    size = h.shape[0]
    kx = np.array([[-1, 0, 1], [-2, 0, 2], [-1, 0, 1]], dtype=F32)
    ky = kx.T
    padded = np.pad(h, 1, mode="wrap")
    gx = sum(
        kx[j, i] * padded[j : j + size, i : i + size]
        for j in range(3)
        for i in range(3)
    )
    gy = sum(
        ky[j, i] * padded[j : j + size, i : i + size]
        for j in range(3)
        for i in range(3)
    )
    scale = F32(strength) * F32(size) / F32(8.0) / F32(64.0)
    nx = -gx * scale
    ny = gy * scale
    nz = np.ones_like(nx)
    length = np.sqrt(nx * nx + ny * ny + nz * nz)
    return np.stack([nx / length, ny / length, nz / length], axis=-1)


def ao_from_height(height: np.ndarray, radius_fraction: float = 0.02) -> np.ndarray:
    """Cavity-style AO: how far a texel sits below its neighbourhood mean.

    A box-blurred height is the local horizon estimate; texels below it are
    occluded in proportion to the drop. This is a cavity approximation, not a
    ray-traced bent-normal AO, and is reported as such.
    """
    h = height.astype(F32)
    size = h.shape[0]
    radius = max(1, int(size * radius_fraction))
    kernel = np.ones(2 * radius + 1, dtype=F32) / F32(2 * radius + 1)
    padded = np.pad(h, radius, mode="wrap")
    blurred = np.apply_along_axis(lambda m: np.convolve(m, kernel, mode="valid"), 1, padded)
    blurred = np.apply_along_axis(lambda m: np.convolve(m, kernel, mode="valid"), 0, blurred)
    drop = np.clip(blurred - h, F32(0.0), F32(1.0))
    return np.clip(F32(1.0) - drop * F32(1.6), F32(0.0), F32(1.0))


# --------------------------------------------------------------------------
# Generators.
# --------------------------------------------------------------------------

@dataclass
class GeneratorResult:
    height: np.ndarray
    albedo: np.ndarray  # (H, W, 3) linear-ish sRGB values in 0..1
    roughness: np.ndarray
    mask: np.ndarray  # pattern mask, 1 = brick face, 0 = mortar/gap
    notes: list[str] = field(default_factory=list)


@dataclass
class GeneratorSpec:
    name: str
    summary: str
    provenance: str
    parameters: dict[str, dict[str, object]]
    build: Callable[..., GeneratorResult]


def _mix_rgb(a, b, t):
    return a * (1.0 - t)[..., None] + b * t[..., None]


def _brick_common(size, pattern, rows, columns, repeat, row_offset, mortar,
                  bevel, round_, brick_color, mortar_color, color_variation,
                  brick_roughness, mortar_roughness, grit, seed):
    if pattern not in _BRICK_PATTERNS:
        raise RorsmithError(
            "unknown_brick_pattern",
            f"'{pattern}' is not one of {sorted(_BRICK_PATTERNS)}",
        )
    uv = _uv_grid(size)
    count = np.array([float(columns), float(rows)], dtype=F32)
    bmin, bmax = _BRICK_PATTERNS[pattern](uv, count, F32(repeat), F32(row_offset))
    mask, centre = _brick_field(uv, bmin, bmax, F32(mortar), F32(round_), F32(bevel))
    tint = _mm_rand3(_fract(centre + F32(seed)))
    grit_noise = fbm(size, cells=max(4, size // 64), octaves=4, seed=seed + 3.0)
    fine = fbm(size, cells=max(8, size // 16), octaves=3, seed=seed + 11.0)

    brick = np.array(brick_color, dtype=F32)
    mortarc = np.array(mortar_color, dtype=F32)
    varied = brick[None, None, :] * (
        F32(1.0) - F32(color_variation) * F32(0.5) + F32(color_variation) * tint
    )
    varied = np.clip(varied, F32(0.0), F32(1.0))
    albedo = _mix_rgb(np.broadcast_to(mortarc, varied.shape).copy(), varied, mask)
    albedo *= (F32(1.0) - F32(grit) * F32(0.35) * (F32(1.0) - grit_noise))[..., None]
    albedo = np.clip(albedo, F32(0.0), F32(1.0))

    # Height: brick faces proud of the mortar bed, with per-brick jitter and
    # surface grit so the recess survives a normal-map bake.
    jitter = (_mm_rand(_fract(centre + F32(seed) + F32(0.37))) - F32(0.5)) * F32(0.12)
    height = mask * (F32(0.78) + jitter) + F32(0.10) * fine * mask
    height += F32(0.06) * grit_noise * (F32(1.0) - mask)
    height = np.clip(height, F32(0.0), F32(1.0))

    roughness = np.clip(
        mask * F32(brick_roughness) + (F32(1.0) - mask) * F32(mortar_roughness)
        + F32(0.06) * (fine - F32(0.5)),
        F32(0.03),
        F32(1.0),
    )
    return GeneratorResult(
        height=height,
        albedo=albedo,
        roughness=roughness,
        mask=mask,
        notes=[
            "pattern from Material Maker bricks.mmg (MIT), CPU port",
            "height/roughness/grit are rorsmith additions, not from the node",
        ],
    )


def _gen_bricks(size=1024, pattern="running_bond", rows=6.0, columns=3.0,
                repeat=1.0, row_offset=0.5, mortar=0.1, bevel=0.1, round=0.0,
                brick_color=(0.52, 0.24, 0.17), mortar_color=(0.62, 0.60, 0.56),
                color_variation=0.35, brick_roughness=0.79,
                mortar_roughness=0.90, grit=0.35, seed=1.0) -> GeneratorResult:
    return _brick_common(size, pattern, rows, columns, repeat, row_offset,
                         mortar, bevel, round, brick_color, mortar_color,
                         color_variation, brick_roughness, mortar_roughness,
                         grit, seed)


def _gen_arc_pavement(size=1024, rows=8.0, bricks=8.0, repeat=2.0, mortar=0.05,
                      bevel=0.2, stone_color=(0.42, 0.40, 0.38),
                      mortar_color=(0.24, 0.23, 0.22), color_variation=0.4,
                      stone_roughness=0.845, seed=1.0) -> GeneratorResult:
    uv = _uv_grid(size) * np.array([float(repeat), -1.0], dtype=F32)
    local_uv, seedfield = _arc_pavement(_fract(uv), F32(rows), F32(bricks))
    mask = _pavement_mask(local_uv, F32(bevel), F32(2.0) * F32(mortar))
    tint = _mm_rand3(seedfield + F32(seed))
    fine = fbm(size, cells=max(8, size // 16), octaves=4, seed=seed + 5.0)
    stone = np.array(stone_color, dtype=F32)
    varied = np.clip(
        stone[None, None, :] * (F32(1.0) - F32(color_variation) * F32(0.5)
                                + F32(color_variation) * tint),
        F32(0.0), F32(1.0),
    )
    albedo = _mix_rgb(
        np.broadcast_to(np.array(mortar_color, dtype=F32), varied.shape).copy(),
        varied, mask,
    )
    height = np.clip(mask * F32(0.82) + F32(0.14) * fine * mask, F32(0.0), F32(1.0))
    roughness = np.clip(
        mask * F32(stone_roughness) + (F32(1.0) - mask) * F32(0.93)
        + F32(0.05) * (fine - F32(0.5)),
        F32(0.05), F32(1.0),
    )
    return GeneratorResult(height, albedo, roughness, mask,
                           ["pattern from Material Maker arc_pavement.mmg (MIT), CPU port"])


def _gen_scratches(size=1024, layers=5, length=0.25, width=0.4, waviness=0.3,
                   angle=0.0, randomness=0.3, depth=0.35,
                   base_color=(0.5, 0.5, 0.5), seed=1.0) -> GeneratorResult:
    uv = _uv_grid(size)
    value = _scratches(uv, layers, (float(length), float(width)),
                       float(waviness), float(angle), float(randomness),
                       (float(seed), 0.0))
    height = np.clip(F32(1.0) - F32(depth) * value, F32(0.0), F32(1.0))
    base = np.array(base_color, dtype=F32)
    albedo = np.broadcast_to(base, (size, size, 3)).copy()
    albedo = np.clip(albedo * (F32(1.0) + F32(0.25) * value)[..., None], F32(0.0), F32(1.0))
    roughness = np.clip(F32(0.5) - F32(0.3) * value, F32(0.05), F32(1.0))
    return GeneratorResult(height, albedo, roughness, value,
                           ["pattern from Material Maker scratches.mmg (MIT), CPU port"])


def _gen_grime(size=1024, cells=6, octaves=5, coverage=0.55, contrast=1.6,
               color=(0.14, 0.12, 0.10), roughness=0.94, seed=1.0) -> GeneratorResult:
    noise = fbm(size, cells=int(cells), octaves=int(octaves), seed=seed)
    shaped = np.clip((noise - (F32(1.0) - F32(coverage))) * F32(contrast) + F32(0.5),
                     F32(0.0), F32(1.0))
    albedo = np.broadcast_to(np.array(color, dtype=F32), (size, size, 3)).copy()
    height = shaped * F32(0.18)
    rough = np.full((size, size), F32(roughness), dtype=F32)
    return GeneratorResult(height, albedo, rough, shaped,
                           ["value-noise fbm written for rorsmith; NOT a Material Maker port"])


def _gen_moss(size=1024, cells=10, octaves=5, coverage=0.4, contrast=2.2,
              color=(0.14, 0.22, 0.09), roughness=0.9, seed=2.0) -> GeneratorResult:
    result = _gen_grime(size, cells, octaves, coverage, contrast, color, roughness, seed)
    result.notes = ["value-noise fbm written for rorsmith; NOT a Material Maker port"]
    result.height = result.mask * F32(0.26)
    return result


def _gen_surface_noise(size=1024, cells=64, octaves=3, amplitude=0.12,
                       roughness=0.85, color=(0.5, 0.5, 0.5), seed=3.0) -> GeneratorResult:
    noise = fbm(size, cells=int(cells), octaves=int(octaves), seed=seed)
    albedo = np.clip(
        np.array(color, dtype=F32)[None, None, :] * (F32(0.85) + F32(0.3) * noise)[..., None],
        F32(0.0), F32(1.0),
    )
    return GeneratorResult(noise * F32(amplitude), albedo,
                           np.full((size, size), F32(roughness), dtype=F32), noise,
                           ["value-noise fbm written for rorsmith; NOT a Material Maker port"])


GENERATORS: dict[str, GeneratorSpec] = {
    "bricks": GeneratorSpec(
        "bricks",
        "Running-bond / herringbone brick or block wall with mortar recess.",
        "Material Maker bricks.mmg (MIT) - CPU port",
        {
            "pattern": {"type": "enum", "values": sorted(_BRICK_PATTERNS), "default": "running_bond"},
            "rows": {"type": "number", "default": 6.0, "min": 1, "max": 64},
            "columns": {"type": "number", "default": 3.0, "min": 1, "max": 64},
            "repeat": {"type": "number", "default": 1.0, "min": 1, "max": 8},
            "row_offset": {"type": "number", "default": 0.5, "min": 0.0, "max": 1.0},
            "mortar": {"type": "number", "default": 0.1, "min": 0.0, "max": 0.5},
            "bevel": {"type": "number", "default": 0.1, "min": 0.001, "max": 0.5},
            "round": {"type": "number", "default": 0.0, "min": 0.0, "max": 0.5},
            "brick_color": {"type": "rgb", "default": [0.52, 0.24, 0.17]},
            "mortar_color": {"type": "rgb", "default": [0.62, 0.60, 0.56]},
            "color_variation": {"type": "number", "default": 0.35, "min": 0.0, "max": 1.0},
            "brick_roughness": {"type": "number", "default": 0.79, "min": 0.03, "max": 1.0},
            "mortar_roughness": {"type": "number", "default": 0.90, "min": 0.03, "max": 1.0},
            "grit": {"type": "number", "default": 0.35, "min": 0.0, "max": 1.0},
            "seed": {"type": "number", "default": 1.0},
        },
        _gen_bricks,
    ),
    "arc_pavement": GeneratorSpec(
        "arc_pavement",
        "Fan/arc cobblestone pavement, for plazas and legacy sidewalk pages.",
        "Material Maker arc_pavement.mmg (MIT) - CPU port",
        {
            "rows": {"type": "number", "default": 8.0, "min": 4, "max": 16},
            "bricks": {"type": "number", "default": 8.0, "min": 4, "max": 16},
            "repeat": {"type": "number", "default": 2.0, "min": 1, "max": 4},
            "mortar": {"type": "number", "default": 0.05, "min": 0.0, "max": 0.5},
            "bevel": {"type": "number", "default": 0.2, "min": 0.001, "max": 0.5},
            "stone_color": {"type": "rgb", "default": [0.42, 0.40, 0.38]},
            "mortar_color": {"type": "rgb", "default": [0.24, 0.23, 0.22]},
            "color_variation": {"type": "number", "default": 0.4, "min": 0.0, "max": 1.0},
            "stone_roughness": {"type": "number", "default": 0.845, "min": 0.03, "max": 1.0},
            "seed": {"type": "number", "default": 1.0},
        },
        _gen_arc_pavement,
    ),
    "scratches": GeneratorSpec(
        "scratches",
        "Directional scratch/wear layer for painted and polished surfaces.",
        "Material Maker scratches.mmg (MIT) - CPU port",
        {
            "layers": {"type": "integer", "default": 5, "min": 1, "max": 10},
            "length": {"type": "number", "default": 0.25, "min": 0.1, "max": 1.0},
            "width": {"type": "number", "default": 0.4, "min": 0.1, "max": 1.0},
            "waviness": {"type": "number", "default": 0.3, "min": 0.0, "max": 1.0},
            "angle": {"type": "number", "default": 0.0, "min": -180, "max": 180},
            "randomness": {"type": "number", "default": 0.3, "min": 0.0, "max": 1.0},
            "depth": {"type": "number", "default": 0.35, "min": 0.0, "max": 1.0},
            "base_color": {"type": "rgb", "default": [0.5, 0.5, 0.5]},
            "seed": {"type": "number", "default": 1.0},
        },
        _gen_scratches,
    ),
    "grime": GeneratorSpec(
        "grime",
        "Weathering/soot accumulation layer. Meant to be height-blended so it "
        "settles into recesses rather than lying flat across them.",
        "rorsmith value-noise fbm (not a Material Maker port)",
        {
            "cells": {"type": "integer", "default": 6, "min": 1, "max": 128},
            "octaves": {"type": "integer", "default": 5, "min": 1, "max": 8},
            "coverage": {"type": "number", "default": 0.55, "min": 0.0, "max": 1.0},
            "contrast": {"type": "number", "default": 1.6, "min": 0.1, "max": 8.0},
            "color": {"type": "rgb", "default": [0.14, 0.12, 0.10]},
            "roughness": {"type": "number", "default": 0.94, "min": 0.03, "max": 1.0},
            "seed": {"type": "number", "default": 1.0},
        },
        _gen_grime,
    ),
    "moss": GeneratorSpec(
        "moss",
        "Organic staining/moss layer, higher relief than grime so it wins the "
        "height blend inside deep recesses.",
        "rorsmith value-noise fbm (not a Material Maker port)",
        {
            "cells": {"type": "integer", "default": 10, "min": 1, "max": 128},
            "octaves": {"type": "integer", "default": 5, "min": 1, "max": 8},
            "coverage": {"type": "number", "default": 0.4, "min": 0.0, "max": 1.0},
            "contrast": {"type": "number", "default": 2.2, "min": 0.1, "max": 8.0},
            "color": {"type": "rgb", "default": [0.14, 0.22, 0.09]},
            "roughness": {"type": "number", "default": 0.9, "min": 0.03, "max": 1.0},
            "seed": {"type": "number", "default": 2.0},
        },
        _gen_moss,
    ),
    "surface_noise": GeneratorSpec(
        "surface_noise",
        "Fine high-frequency break-up. The top layer of a stack; it exists to "
        "kill the flat plastic read at grazing incidence.",
        "rorsmith value-noise fbm (not a Material Maker port)",
        {
            "cells": {"type": "integer", "default": 64, "min": 1, "max": 512},
            "octaves": {"type": "integer", "default": 3, "min": 1, "max": 8},
            "amplitude": {"type": "number", "default": 0.12, "min": 0.0, "max": 1.0},
            "roughness": {"type": "number", "default": 0.85, "min": 0.03, "max": 1.0},
            "color": {"type": "rgb", "default": [0.5, 0.5, 0.5]},
            "seed": {"type": "number", "default": 3.0},
        },
        _gen_surface_noise,
    ),
}


def evaluate(generator: str, params: dict, size: int) -> GeneratorResult:
    spec = GENERATORS.get(generator)
    if spec is None:
        raise RorsmithError(
            "unknown_generator",
            f"'{generator}' is not one of {sorted(GENERATORS)}",
        )
    if size <= 0 or size & (size - 1):
        raise RorsmithError("resolution_not_power_of_two", str(size))
    if size > 4096:
        raise RorsmithError(
            "resolution_too_large",
            f"{size} exceeds the 4096 cap; a larger map is a compiler job, "
            "not an MCP response",
        )
    unknown = sorted(set(params) - set(spec.parameters))
    if unknown:
        raise RorsmithError(
            "unknown_generator_parameter",
            f"{generator} has no parameter(s) {unknown}; known: "
            + ", ".join(sorted(spec.parameters)),
        )
    kwargs = {k: v for k, v in params.items()}
    for key, value in list(kwargs.items()):
        meta = spec.parameters[key]
        if meta["type"] == "rgb":
            if not (isinstance(value, (list, tuple)) and len(value) == 3):
                raise RorsmithError("parameter_type", f"{key} must be [r,g,b]")
            kwargs[key] = tuple(float(c) for c in value)
        elif meta["type"] == "enum":
            if value not in meta["values"]:
                raise RorsmithError(
                    "parameter_value", f"{key}={value!r} not in {meta['values']}"
                )
        elif meta["type"] == "integer":
            kwargs[key] = int(value)
        else:
            kwargs[key] = float(value)
            low, high = meta.get("min"), meta.get("max")
            if low is not None and kwargs[key] < low:
                raise RorsmithError("parameter_range", f"{key} < {low}")
            if high is not None and kwargs[key] > high:
                raise RorsmithError("parameter_range", f"{key} > {high}")
    return spec.build(size=size, **kwargs)
