#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Deterministic automotive paint set for the Alexis Saber body.

``AlexisSaber.zip`` ships real art for the chassis, wheels, band and window
glass, but every body-paint member is a 5x5 flat-colour placeholder:
``bodytemp.png`` (#B10000, the red the packaged thumbnail shows) plus the five
``body_<colour>.png`` skin swatches and the ``bodytempspec.png`` specular
placeholder.  This module authors the paint those placeholders stand in for.

What the presenter can actually consume decides the authoring budget, so it is
worth stating plainly.  For an AlexisSaber managed material the lowering in
``source/main/gfx/ogre14/detail/OgreNextDemoMaterialSource.cpp`` sets

* ``base_color_texture``  <- the diffuse member, decoded as SRGB_COLOR,
* ``specular_texture``    <- the ``*spec`` member, decoded as LINEAR_DATA,
* ``metallic_factor``     = 0.0 (hardcoded),
* ``specular_factor``     = {1, 1, 1} (constant off the curated path),
* ``index_of_refraction`` = 1.5, giving a dielectric F0 of 0.04,
* ``roughness_factor``    = sqrt(2 / (shininess + 2)) for every other legacy
  projection, which lands on 1.0 because no managed template authors a
  shininess.  The body paint is the one exception: it carries the reviewed
  ``kOgreNextDemoAlexisBodyPaintRoughness`` instead.

There is still no normal map and no separate clearcoat lobe, so the two colour
maps plus that one reviewed scalar are the entire authoring surface.  The
reviewed roughness is not optional decoration: under the shininess derivation
the lobe is fully rough, and a fully rough lobe spreads the specular product so
wide that any clearcoat map is indistinguishable from a flat one.  Paint and
roughness are one deliverable; shipping either alone wastes the other.

The second constraint is the UV layout.  Every ``SaberBody`` panel mesh
(``AlexisSaberDoors/FF/RF/Hood/FBump/RBump``) independently unwraps across the
full 0..1 square, because the panels were authored against a 5x5 solid.  Any
large-scale structure in the paint would therefore repeat once per panel and
break at every panel seam.  Every field below is consequently
lattice-periodic (seamless under both wrap and clamp) and statistically
uniform: nothing in these maps carries position-dependent meaning.

Everything is stdlib-only and bit-deterministic; the PNG encoder and the
coordinate hash are the reviewed ones from ``tools/cityworld_road_texture.py``.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import functools
import importlib.util as _importlib_util
from pathlib import Path
import sys

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


class PaintError(RuntimeError):
    """Raised when the paint set cannot be authored deterministically."""


PAINT_FORMAT = "ror-alexis-saber-paint-v1"

#: Both maps are square and power-of-two so the generated mip tail is exact.
#: 512 is deliberate rather than maximal: each panel unwraps across the whole
#: square, a panel is roughly a metre across, and the flake footprint below is
#: 2x2 texels so it survives exactly one mip level.  Going higher would only
#: buy detail that the mip chain averages away at any normal viewing distance.
PAINT_SIZE = 512

#: Clearcoat specular level, in the same 0..255 linear encoding the shipped
#: ``*Spec`` members use.  Clearcoat reflectance does not depend on the pigment
#: underneath it, so every skin shares this mean.
#:
#: Deliberately *not* the 73 the shipped ``bodytempspec.png`` placeholder
#: carried.  73 was authored for the legacy ``SpecularMapping1`` pass, where the
#: member is an amplitude on an additive cube-map reflection.  The presenter
#: consumes the very same member as a PBS specular multiplier: the pinned Hlms
#: does ``pixelData.specular.xyz *= specularMap`` under SpecularWorkflow, with
#: F0 held at the IOR 1.5 dielectric 0.04.  Carried over literally, 73 would cut
#: a dielectric's specular to 29 per cent of neutral for no physical reason.
#: 214 leaves the clearcoat close to the neutral multiplier while keeping
#: headroom above it for flake and below it for the sheen and peel fields.
#: Because F0 stays 0.04 no matter how bright this map is, a high level cannot
#: turn the paint into chrome; it only stops it being needlessly dim.
CLEARCOAT_SPECULAR_LEVEL = 214

#: Coarse "wet clearcoat" undulation: 16 lattice cells over the square.
#: A panel is roughly a metre across, so a cell lands near 6 cm - large
#: enough to read as reflection roll across a fender, small enough that it
#: never reads as blotching or dirt.
_SHEEN_CELLS = 16
#: Orange-peel cell structure: 32 lattice cells over the square.
_PEEL_CELLS = 32
#: Metallic flake is evaluated on a half-resolution grid, so one flake covers
#: a 2x2 texel block and survives the first mip reduction intact.
_FLAKE_BLOCK = 2
#: Flakes per thousand half-resolution cells.
_FLAKE_PERMILLE = 62

_SHEEN_SALT_STEP = 0x11
_PEEL_SALT_STEP = 0x25
_FLAKE_SALT_STEP = 0x3B


@dataclass(frozen=True)
class PaintSkin:
    """One authored body colour and the two members that carry it."""

    key: str
    base_color_member: str
    specular_member: str
    #: Authored mean base colour.  These are the exact RGB triples the shipped
    #: 5x5 placeholders carried, so the skin identities are preserved.
    base_rgb: tuple[int, int, int]
    #: Peak flake brightening added to the base colour, in 0..255 steps.
    flake_albedo_gain: int
    #: Peak flake brightening added to the specular map.
    flake_specular_gain: int
    #: Peak signed base-colour swing from the sheen and peel fields.
    albedo_sheen: int
    albedo_peel: int
    #: Peak signed specular swing from the sheen and peel fields.
    specular_sheen: int
    specular_peel: int
    #: Per-skin hash salt; distinct salts make each colour a distinct paint
    #: batch rather than six recolours of one noise field.
    salt: int


#: The default member (``bodytemp.png``) keeps the #B10000 red the packaged
#: ``AlexisSaber-mini.png`` thumbnail shows, so the out-of-the-box car is the
#: red the author intended.  The five swatch colours are likewise the exact
#: placeholder tones, so ``AlexisSaber.skin`` keeps meaning what it said.
#:
#: Flake amplitude is scaled per colour the way real basecoats are: dark
#: paints carry the most visible flake because the pigment cannot hide it,
#: white the least because the flake is barely brighter than the base.
PAINT_SKINS: tuple[PaintSkin, ...] = (
    PaintSkin(
        key="red",
        base_color_member="bodytemp.png",
        specular_member="bodytempspec.png",
        base_rgb=(177, 0, 0),
        flake_albedo_gain=34,
        flake_specular_gain=46,
        albedo_sheen=5,
        albedo_peel=4,
        specular_sheen=24,
        specular_peel=17,
        salt=0x41,
    ),
    PaintSkin(
        key="black",
        base_color_member="body_black.png",
        specular_member="body_blackspec.png",
        base_rgb=(3, 3, 3),
        flake_albedo_gain=44,
        flake_specular_gain=50,
        albedo_sheen=4,
        albedo_peel=3,
        specular_sheen=27,
        specular_peel=19,
        salt=0x47,
    ),
    PaintSkin(
        key="blue",
        base_color_member="body_blue.png",
        specular_member="body_bluespec.png",
        base_rgb=(0, 19, 127),
        flake_albedo_gain=38,
        flake_specular_gain=48,
        albedo_sheen=5,
        albedo_peel=4,
        specular_sheen=25,
        specular_peel=18,
        salt=0x4D,
    ),
    PaintSkin(
        key="green",
        base_color_member="body_green.png",
        specular_member="body_greenspec.png",
        base_rgb=(58, 193, 0),
        flake_albedo_gain=30,
        flake_specular_gain=44,
        albedo_sheen=6,
        albedo_peel=5,
        specular_sheen=22,
        specular_peel=16,
        salt=0x53,
    ),
    PaintSkin(
        key="purple",
        base_color_member="body_purple.png",
        specular_member="body_purplespec.png",
        base_rgb=(127, 0, 110),
        flake_albedo_gain=36,
        flake_specular_gain=47,
        albedo_sheen=5,
        albedo_peel=4,
        specular_sheen=25,
        specular_peel=18,
        salt=0x59,
    ),
    PaintSkin(
        key="white",
        base_color_member="body_white.png",
        specular_member="body_whitespec.png",
        base_rgb=(253, 253, 253),
        flake_albedo_gain=14,
        flake_specular_gain=40,
        albedo_sheen=4,
        albedo_peel=4,
        specular_sheen=20,
        specular_peel=15,
        salt=0x5F,
    ),
)


def _clamp_byte(value: int) -> int:
    if value < 0:
        return 0
    if value > 255:
        return 255
    return value


def _validate_size(size: int) -> None:
    if size <= 0 or size & (size - 1) != 0:
        raise PaintError(
            f"paint size must be a positive power of two, got {size}")
    for cells in (_SHEEN_CELLS, _PEEL_CELLS):
        if size % cells != 0:
            raise PaintError(
                f"paint size {size} is not a whole multiple of the "
                f"{cells}-cell lattice")
    if size % _FLAKE_BLOCK != 0:
        raise PaintError(
            f"paint size {size} is not a whole multiple of the flake block")


def _lattice(cells: int, salt: int) -> list[list[int]]:
    """A ``cells``x``cells`` field of 0..255 values, sampled with wraparound."""

    return [[_hash32(cx, cy, salt) & 0xFF for cx in range(cells)]
            for cy in range(cells)]


def _smooth_weights(step: int) -> list[int]:
    """Smoothstep weights in 0..``step`` for one lattice span.

    Pure integer, so the field is bit-identical on every host.  Plain bilinear
    would leave visible diamond creases in a field this coarse.
    """

    weights = []
    for offset in range(step):
        # step * offset^2 * (3*step - 2*offset) / step^3, rounded down.
        weights.append(
            (offset * offset * ((3 * step) - (2 * offset))) // (step * step))
    return weights


def _sample_lattice(field: list[list[int]], cells: int, step: int,
                    weights: list[int], x: int, y: int) -> int:
    """Smoothstep-interpolated lattice sample in 0..255, seamless at the edge."""

    cx0, fx = divmod(x, step)
    cy0, fy = divmod(y, step)
    cx1 = (cx0 + 1) % cells
    cy1 = (cy0 + 1) % cells
    cx0 %= cells
    cy0 %= cells
    wx = weights[fx]
    wy = weights[fy]
    top = (field[cy0][cx0] * (step - wx)) + (field[cy0][cx1] * wx)
    bottom = (field[cy1][cx0] * (step - wx)) + (field[cy1][cx1] * wx)
    return ((top * (step - wy)) + (bottom * wy)) // (step * step)


def _signed(sample: int, amplitude: int) -> int:
    """Maps a 0..255 lattice sample onto -amplitude..+amplitude."""

    return ((sample - 128) * amplitude) // 128


def _flake_field(size: int, salt: int) -> list[list[int]]:
    """Sparse 0..255 flake intensities on the half-resolution flake grid."""

    cells = size // _FLAKE_BLOCK
    field = []
    for cy in range(cells):
        row = []
        for cx in range(cells):
            value = _hash32(cx, cy, salt)
            if (value >> 7) % 1000 >= _FLAKE_PERMILLE:
                row.append(0)
            else:
                # 96..255: a flake is never a barely-there speck, otherwise it
                # vanishes into the base colour the moment it is mipped.
                row.append(96 + ((value >> 19) % 160))
        field.append(row)
    return field


def build_paint_pair(skin: PaintSkin,
                     size: int = PAINT_SIZE) -> tuple[bytes, bytes]:
    """Builds ``(base_color_png, specular_png)`` for one skin.

    Both maps share the same sheen, peel and flake fields, so a bright flake
    in the base colour is a bright flake in the specular response - which is
    what a metal flake suspended in a basecoat actually does.
    """

    _validate_size(size)
    sheen = _lattice(_SHEEN_CELLS, skin.salt + _SHEEN_SALT_STEP)
    peel = _lattice(_PEEL_CELLS, skin.salt + _PEEL_SALT_STEP)
    flake = _flake_field(size, skin.salt + _FLAKE_SALT_STEP)

    sheen_step = size // _SHEEN_CELLS
    peel_step = size // _PEEL_CELLS
    sheen_weights = _smooth_weights(sheen_step)
    peel_weights = _smooth_weights(peel_step)

    red, green, blue = skin.base_rgb
    base_rgba = bytearray(size * size * 4)
    spec_rgba = bytearray(size * size * 4)
    offset = 0
    for y in range(size):
        flake_row = flake[y // _FLAKE_BLOCK]
        for x in range(size):
            sheen_sample = _sample_lattice(
                sheen, _SHEEN_CELLS, sheen_step, sheen_weights, x, y)
            peel_sample = _sample_lattice(
                peel, _PEEL_CELLS, peel_step, peel_weights, x, y)
            flake_sample = flake_row[x // _FLAKE_BLOCK]

            albedo_delta = (
                _signed(sheen_sample, skin.albedo_sheen) +
                _signed(peel_sample, skin.albedo_peel) +
                ((flake_sample * skin.flake_albedo_gain) // 255))
            base_rgba[offset] = _clamp_byte(red + albedo_delta)
            base_rgba[offset + 1] = _clamp_byte(green + albedo_delta)
            base_rgba[offset + 2] = _clamp_byte(blue + albedo_delta)
            base_rgba[offset + 3] = 255

            specular = _clamp_byte(
                CLEARCOAT_SPECULAR_LEVEL +
                _signed(sheen_sample, skin.specular_sheen) +
                _signed(peel_sample, skin.specular_peel) +
                ((flake_sample * skin.flake_specular_gain) // 255))
            spec_rgba[offset] = specular
            spec_rgba[offset + 1] = specular
            spec_rgba[offset + 2] = specular
            spec_rgba[offset + 3] = 255
            offset += 4

    return (encode_png_rgba(size, size, bytes(base_rgba)),
            encode_png_rgba(size, size, bytes(spec_rgba)))


@functools.lru_cache(maxsize=None)
def _cached_paint_pair(skin: PaintSkin, size: int) -> tuple[bytes, bytes]:
    """Memoised ``build_paint_pair``; the generator is a pure function."""

    return build_paint_pair(skin, size)


def build_paint_members(size: int = PAINT_SIZE) -> dict[str, bytes]:
    """Every authored member name mapped to its PNG bytes."""

    members: dict[str, bytes] = {}
    for skin in PAINT_SKINS:
        base_png, spec_png = _cached_paint_pair(skin, size)
        for name, payload in ((skin.base_color_member, base_png),
                              (skin.specular_member, spec_png)):
            if name in members:
                raise PaintError(f"duplicate authored member '{name}'")
            members[name] = payload
    return members


def paint_member_names() -> tuple[str, ...]:
    """Authored member names, in a stable order."""

    names: list[str] = []
    for skin in PAINT_SKINS:
        names.append(skin.base_color_member)
        names.append(skin.specular_member)
    return tuple(names)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Author the Alexis Saber body paint set.")
    parser.add_argument(
        "--output-dir", type=Path, required=True,
        help="directory the PNG members are written into")
    parser.add_argument(
        "--size", type=int, default=PAINT_SIZE,
        help=f"square edge length (default {PAINT_SIZE})")
    args = parser.parse_args(argv)

    args.output_dir.mkdir(parents=True, exist_ok=True)
    for name, payload in build_paint_members(args.size).items():
        (args.output_dir / name).write_bytes(payload)
        print(f"{name}: {len(payload)} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
