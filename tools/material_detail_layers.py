#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Deterministic authoring for weighted detail material layers.

The engine reads a base material's detail layers from a companion material
named ``RoR/DetailLayers/<base material name>`` whose texture units carry the
request (see source/main/gfx/ogre14/detail/Ogre14MaterialDetailLayerDeclaration.h).
This module emits those companion blocks and the procedural layer artwork they
name, byte-reproducibly, so a rerun over unchanged inputs is a no-op.

Rights policy, identical to tools/cityworld_replacement_textures.py: no byte of
any shipped archive member is decoded, upscaled, or copied into these payloads.
Every texture below is a purely procedural, stdlib-deterministic composition
built from recorded literals and the FNV-1a coordinate hash only.

THE ALPHA CHANNEL OF A DETAIL ALBEDO IS NOT TRANSPARENCY.

The pinned Ogre-Next PBS pixel shader composites a detail layer as

    detailWeights.<channel_i> *= detailCol_i.w
    detailCol_i.w              = detailWeights.<channel_i>

(Samples/Media/Hlms/Pbs/Any/Main/800.PixelShader_piece_ps.any, piece
SampleDetailMaps). So a layer's effective per-texel strength is

    mask_channel_i * cDetailWeights_i * detailAlbedo_i.ALPHA

and the albedo is sampled at that layer's OWN UV repeat while the mask is
sampled unscaled. The alpha channel is therefore a per-texel height/coverage
signal at the layer's authored density, and it is what makes grime settle into
mortar lines instead of lying flat across them.

The engine cannot add contrast to that signal later, so this module bakes it:

    alpha = clamp((height - HEIGHT_PIVOT) * HEIGHT_CONTRAST + 0.5, 0, 1)

Both literals are recorded per layer below and travel with the artwork.
"""

from __future__ import annotations

from dataclasses import dataclass
import importlib.util as _importlib_util
from pathlib import Path
import sys
from typing import Callable, Sequence

_ROAD_TEXTURE_SPEC = _importlib_util.spec_from_file_location(
    "cityworld_road_texture",
    Path(__file__).resolve().parent / "cityworld_road_texture.py",
)
if _ROAD_TEXTURE_SPEC is None or _ROAD_TEXTURE_SPEC.loader is None:
    raise RuntimeError("could not load the road texture generator")
road_texture = _importlib_util.module_from_spec(_ROAD_TEXTURE_SPEC)
sys.modules.setdefault(_ROAD_TEXTURE_SPEC.name, road_texture)
_ROAD_TEXTURE_SPEC.loader.exec_module(road_texture)

_hash32 = road_texture._hash32
encode_png_rgba = road_texture.encode_png_rgba


class DetailLayerAuthoringError(RuntimeError):
    """Raised when a layer set cannot be authored deterministically."""


DETAIL_LAYERS_FORMAT = "ror-material-detail-layers-v1"

#: Companion material name prefix. Must match
#: kMaterialDetailLayerCompanionPrefix in the engine header.
COMPANION_PREFIX = "RoR/DetailLayers/"

#: Reserved texture-unit names. Must match the engine header.
WEIGHT_UNIT_NAME = "ror_detail_weight"
UNIT_PREFIX = "ror_detail"

#: Blend operator tokens the engine parses, from
#: MaterialDetailBlendModeToken in MaterialDescriptor.cpp. Kept as a frozen
#: tuple so a typo here is a build-time failure rather than a silent refusal
#: at load.
BLEND_TOKENS = (
    "normal",
    "premul",
    "add",
    "subtract",
    "multiply",
    "multiply2x",
    "screen",
    "overlay",
    "lighten",
    "darken",
    "grain_extract",
    "grain_merge",
    "difference",
)

#: The pinned HlmsPbs detail budget. Base + 4 detail = 5 layers, and there is
#: no fifth detail channel to grow into.
MAX_DETAIL_LAYERS = 4

_UINT8_MAX = 255


def _clamp_byte(value: float) -> int:
    if value < 0:
        return 0
    if value > _UINT8_MAX:
        return _UINT8_MAX
    return int(value)


def _clamp_unit(value: float) -> float:
    if value < 0.0:
        return 0.0
    if value > 1.0:
        return 1.0
    return value


def bake_height_to_alpha(height: float, pivot: float, contrast: float) -> int:
    """The one contrast curve the engine cannot apply for us.

    ``height`` and ``pivot`` are in [0, 1]; ``contrast`` is the slope. A
    contrast of 1 reproduces the raw height shifted about the pivot, which
    gives the soft linear falloff; larger values sharpen the transition until
    the layer reads as sitting in the recesses rather than washing over them.
    """

    return _clamp_byte(
        round(_clamp_unit((height - pivot) * contrast + 0.5) * _UINT8_MAX)
    )


@dataclass(frozen=True)
class DetailLayerSpec:
    """One authored detail layer."""

    #: Layer ordinal, 0..3. Selects the weight mask channel (R/G/B/A).
    index: int
    #: Archive member name of the albedo (RGB) + baked height (A) map.
    albedo_member: str
    #: Archive member name of the tangent-space detail normal, or None.
    normal_member: str | None
    #: One of BLEND_TOKENS.
    blend: str
    #: UV repeats across the surface. Written to the companion as an OGRE
    #: `scale` directive; the engine reads the composed matrix back.
    uv_repeats: float
    #: Constant layer strength, multiplied on top of mask and baked alpha.
    weight: float = 1.0
    #: Detail normal strength.
    normal_weight: float = 1.0
    #: Recorded literals of the alpha bake, for provenance.
    height_pivot: float = 0.5
    height_contrast: float = 1.0

    def __post_init__(self) -> None:
        if not 0 <= self.index < MAX_DETAIL_LAYERS:
            raise DetailLayerAuthoringError(
                f"detail layer index {self.index} is outside the pinned budget"
            )
        if self.blend not in BLEND_TOKENS:
            raise DetailLayerAuthoringError(
                f"unknown detail blend operator {self.blend!r}"
            )
        if not self.uv_repeats > 0.0:
            raise DetailLayerAuthoringError(
                "a detail layer's UV repeats must be strictly positive"
            )
        for name, value in (("weight", self.weight),
                            ("normal_weight", self.normal_weight)):
            if not 0.0 <= value <= 1.0:
                raise DetailLayerAuthoringError(
                    f"detail layer {name} must be within the unit range"
                )


@dataclass(frozen=True)
class DetailLayerSet:
    """Every layer declared for one base material."""

    base_material: str
    weight_mask_member: str
    layers: Sequence[DetailLayerSpec]

    def __post_init__(self) -> None:
        if not self.base_material:
            raise DetailLayerAuthoringError("a layer set needs a base material")
        if not self.layers:
            raise DetailLayerAuthoringError(
                "a layer set must declare at least one layer"
            )
        seen: set[int] = set()
        for layer in self.layers:
            if layer.index in seen:
                raise DetailLayerAuthoringError(
                    f"detail layer {layer.index} declared twice"
                )
            seen.add(layer.index)

    @property
    def companion_material(self) -> str:
        return COMPANION_PREFIX + self.base_material


def _format_float(value: float) -> str:
    """Shortest exact-enough decimal, stable across runs and platforms."""

    text = f"{value:.6f}".rstrip("0").rstrip(".")
    return text if text else "0"


def emit_companion_material(layer_set: DetailLayerSet, *, indent: str = "\t",
                            eol: str = "\n") -> str:
    """Emits the companion material block.

    Only stock OGRE directives are written. ``scale`` sets the layer's repeat;
    ``alpha_op_ex source1 src_manual src_current <w>`` carries its constant
    weight, which the engine reads back from getAlphaBlendMode().alphaArg1.
    """

    lines: list[str] = []
    add = lines.append
    add(f"material {layer_set.companion_material}")
    add("{")
    add(f"{indent}technique")
    add(f"{indent}{{")
    add(f"{indent * 2}pass")
    add(f"{indent * 2}{{")

    add(f"{indent * 3}texture_unit {WEIGHT_UNIT_NAME}")
    add(f"{indent * 3}{{")
    add(f"{indent * 4}texture {layer_set.weight_mask_member} 2d")
    add(f"{indent * 3}}}")

    for layer in sorted(layer_set.layers, key=lambda item: item.index):
        suffix = "" if layer.blend == "normal" else f"_{layer.blend}"
        add(f"{indent * 3}texture_unit {UNIT_PREFIX}{layer.index}{suffix}")
        add(f"{indent * 3}{{")
        add(f"{indent * 4}texture {layer.albedo_member} 2d")
        add(f"{indent * 4}scale {_format_float(1.0 / layer.uv_repeats)} "
            f"{_format_float(1.0 / layer.uv_repeats)}")
        if layer.weight != 1.0:
            add(f"{indent * 4}alpha_op_ex source1 src_manual src_current "
                f"{_format_float(layer.weight)}")
        add(f"{indent * 3}}}")
        if layer.normal_member is None:
            continue
        add(f"{indent * 3}texture_unit {UNIT_PREFIX}{layer.index}_nm")
        add(f"{indent * 3}{{")
        add(f"{indent * 4}texture {layer.normal_member} 2d")
        add(f"{indent * 4}scale {_format_float(1.0 / layer.uv_repeats)} "
            f"{_format_float(1.0 / layer.uv_repeats)}")
        if layer.normal_weight != 1.0:
            add(f"{indent * 4}alpha_op_ex source1 src_manual src_current "
                f"{_format_float(layer.normal_weight)}")
        add(f"{indent * 3}}}")

    add(f"{indent * 2}}}")
    add(f"{indent}}}")
    add("}")
    return eol.join(lines) + eol


# --------------------------------------------------------------------------
# Procedural layer artwork
# --------------------------------------------------------------------------


def build_weight_mask_png(size: int, channels: Sequence[Callable[[float, float], float]]) -> bytes:
    """One linear RGBA mask whose channels select layers 0..3.

    Sampled unscaled across the surface, so this does large-scale placement
    only: which parts of the facade are grimy at all, not the per-texel detail
    that the layer's own baked alpha supplies.
    """

    if size <= 0:
        raise DetailLayerAuthoringError("mask size must be positive")
    if len(channels) > MAX_DETAIL_LAYERS:
        raise DetailLayerAuthoringError("a mask carries at most four channels")
    resolved = list(channels) + [lambda u, v: 0.0] * (
        MAX_DETAIL_LAYERS - len(channels)
    )
    rows = bytearray()
    for y in range(size):
        v = (y + 0.5) / size
        for x in range(size):
            u = (x + 0.5) / size
            for channel in resolved:
                rows.append(_clamp_byte(round(_clamp_unit(channel(u, v)) * _UINT8_MAX)))
    return encode_png_rgba(size, size, bytes(rows))


def build_grime_albedo_png(size: int, *, salt: int, tint: Sequence[int],
                           pivot: float, contrast: float) -> bytes:
    """Soot/weathering albedo with its coverage height baked into alpha.

    The height field is a two-octave value-noise streak field biased downward
    in V, which is how airborne soot actually deposits on a vertical facade:
    heaviest under ledges and in the recesses, thinning as it runs down.
    """

    if size <= 0:
        raise DetailLayerAuthoringError("texture size must be positive")
    if len(tint) != 3:
        raise DetailLayerAuthoringError("tint must be RGB")

    def value_noise(x: int, y: int, cell: int, salt_offset: int) -> float:
        cx, cy = x // cell, y // cell
        fx = (x % cell) / cell
        fy = (y % cell) / cell
        # Smoothstep the cell-local coordinates so the lattice does not show.
        sx = fx * fx * (3.0 - 2.0 * fx)
        sy = fy * fy * (3.0 - 2.0 * fy)
        corners = []
        for dy in (0, 1):
            for dx in (0, 1):
                corners.append(
                    (_hash32(cx + dx, cy + dy, salt + salt_offset) & 0xFFFF) / 65535.0
                )
        top = corners[0] * (1.0 - sx) + corners[1] * sx
        bottom = corners[2] * (1.0 - sx) + corners[3] * sx
        return top * (1.0 - sy) + bottom * sy

    coarse_cell = max(size // 8, 1)
    fine_cell = max(size // 32, 1)
    rows = bytearray()
    for y in range(size):
        # Soot runs downward: V near 0 (top) stays cleaner than V near 1.
        vertical_bias = (y + 0.5) / size
        for x in range(size):
            noise = (
                0.65 * value_noise(x, y, coarse_cell, 0)
                + 0.35 * value_noise(x, y, fine_cell, 977)
            )
            height = _clamp_unit(0.35 * vertical_bias + 0.65 * noise)
            # Denser deposit reads darker and slightly warmer.
            shade = 0.55 + 0.45 * (1.0 - height)
            rows.append(_clamp_byte(round(tint[0] * shade)))
            rows.append(_clamp_byte(round(tint[1] * shade)))
            rows.append(_clamp_byte(round(tint[2] * shade)))
            rows.append(bake_height_to_alpha(height, pivot, contrast))
    return encode_png_rgba(size, size, bytes(rows))


def build_relief_normal_png(size: int, *, salt: int, amplitude: float,
                            cell_divisor: int = 16) -> bytes:
    """Tangent-space detail normal from the gradient of a procedural height.

    Encoded as the usual `0.5 * v + 0.5` with +Y along the mesh bitangent, and
    stored LINEAR because the shader decodes it as `2 * texel - 1`.
    """

    if size <= 0:
        raise DetailLayerAuthoringError("texture size must be positive")
    cell = max(size // max(cell_divisor, 1), 1)

    def height_at(x: int, y: int) -> float:
        cx, cy = x // cell, y // cell
        fx = (x % cell) / cell
        fy = (y % cell) / cell
        sx = fx * fx * (3.0 - 2.0 * fx)
        sy = fy * fy * (3.0 - 2.0 * fy)
        corners = []
        for dy in (0, 1):
            for dx in (0, 1):
                corners.append(
                    (_hash32(cx + dx, cy + dy, salt) & 0xFFFF) / 65535.0
                )
        top = corners[0] * (1.0 - sx) + corners[1] * sx
        bottom = corners[2] * (1.0 - sx) + corners[3] * sx
        return top * (1.0 - sy) + bottom * sy

    rows = bytearray()
    for y in range(size):
        for x in range(size):
            # Central differences, wrapping so the map tiles seamlessly.
            dx = height_at((x + 1) % size, y) - height_at((x - 1) % size, y)
            dy = height_at(x, (y + 1) % size) - height_at(x, (y - 1) % size)
            nx = -dx * amplitude
            ny = -dy * amplitude
            nz = 1.0
            length = (nx * nx + ny * ny + nz * nz) ** 0.5
            nx, ny, nz = nx / length, ny / length, nz / length
            rows.append(_clamp_byte(round((nx * 0.5 + 0.5) * _UINT8_MAX)))
            rows.append(_clamp_byte(round((ny * 0.5 + 0.5) * _UINT8_MAX)))
            rows.append(_clamp_byte(round((nz * 0.5 + 0.5) * _UINT8_MAX)))
            rows.append(_UINT8_MAX)
    return encode_png_rgba(size, size, bytes(rows))


def build_flake_normal_png(size: int, *, salt: int, amplitude: float,
                           orange_peel_cell: int = 24) -> bytes:
    """Automotive clearcoat relief: fine metallic flake over orange peel.

    Two scales, because the two are physically different: orange peel is a
    slow thickness undulation of the clearcoat, flake is discrete aluminium
    platelets suspended in the basecoat. Rendering only one reads as noise.
    """

    if size <= 0:
        raise DetailLayerAuthoringError("texture size must be positive")
    peel_cell = max(orange_peel_cell, 2)

    def peel_at(x: int, y: int) -> float:
        cx, cy = x // peel_cell, y // peel_cell
        fx = (x % peel_cell) / peel_cell
        fy = (y % peel_cell) / peel_cell
        sx = fx * fx * (3.0 - 2.0 * fx)
        sy = fy * fy * (3.0 - 2.0 * fy)
        corners = []
        for dy in (0, 1):
            for dx in (0, 1):
                corners.append(
                    (_hash32(cx + dx, cy + dy, salt) & 0xFFFF) / 65535.0
                )
        top = corners[0] * (1.0 - sx) + corners[1] * sx
        bottom = corners[2] * (1.0 - sx) + corners[3] * sx
        return top * (1.0 - sy) + bottom * sy

    def flake_at(x: int, y: int) -> float:
        # One platelet per texel, sparsely populated: most texels are flat
        # clearcoat and a minority catch the light.
        sample = _hash32(x, y, salt + 4241) & 0xFFFF
        if sample < 56000:
            return 0.0
        return (sample - 56000) / (65535 - 56000)

    rows = bytearray()
    for y in range(size):
        for x in range(size):
            def height(px: int, py: int) -> float:
                return 0.8 * peel_at(px, py) + 0.2 * flake_at(px, py)

            dx = height((x + 1) % size, y) - height((x - 1) % size, y)
            dy = height(x, (y + 1) % size) - height(x, (y - 1) % size)
            nx = -dx * amplitude
            ny = -dy * amplitude
            nz = 1.0
            length = (nx * nx + ny * ny + nz * nz) ** 0.5
            nx, ny, nz = nx / length, ny / length, nz / length
            rows.append(_clamp_byte(round((nx * 0.5 + 0.5) * _UINT8_MAX)))
            rows.append(_clamp_byte(round((ny * 0.5 + 0.5) * _UINT8_MAX)))
            rows.append(_clamp_byte(round((nz * 0.5 + 0.5) * _UINT8_MAX)))
            rows.append(_UINT8_MAX)
    return encode_png_rgba(size, size, bytes(rows))


def build_opaque_mask_png(size: int, channels: int) -> bytes:
    """A mask that selects the first ``channels`` layers everywhere.

    The honest choice when a material wants its layers applied uniformly and
    all the per-texel variation lives in the layers' own baked alpha.
    """

    if not 0 < channels <= MAX_DETAIL_LAYERS:
        raise DetailLayerAuthoringError("channel count is outside the budget")
    return build_weight_mask_png(
        size,
        [(lambda u, v: 1.0) for _ in range(channels)],
    )
