#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Independently authored replacement textures for legacy CityWorld facades.

The CityWorld Next local overlay ships high-resolution facade and wall
textures under the reserved ``cityworld_next_replacements/`` namespace. They
are reached exclusively through reviewed script-repair plans
(source/main/resources/LegacyMaterialScriptSanitizer.cpp); original CityWorld
member names are never intercepted and remain resolvable by their own names.

Rights policy (same contract as tools/build_cityworld_local_overlay.py): no
byte of any CityWorld.zip member is decoded, upscaled, or copied into these
payloads. Every texture below is a purely procedural, stdlib-deterministic
composition patterned on build_parcel_asphalt_png. The palette constants in
``REPLACEMENT_TEXTURES`` were authored by reviewing the original textures and
recording a handful of derived literals (dominant tones, band positions, and
brick grid counts); the pixel data itself is generated from those literals
and the FNV-1a coordinate hash only.
"""

from __future__ import annotations

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


class ReplacementTextureError(RuntimeError):
    """Raised when a replacement texture cannot be authored deterministically."""


REPLACEMENT_TEXTURES_FORMAT = "ror-cityworld-replacement-textures-v1"

# Reserved, collision-free namespace. tools/build_cityworld_local_overlay.py
# additionally asserts at build time that no generated member name or
# basename equals any member of the audited CityWorld.zip index.
REPLACEMENT_NAMESPACE = "cityworld_next_replacements/"

DEFAULT_TEXTURE_SIZE = 1024

_FNV_OFFSET_BASIS = 2166136261
_FNV_PRIME = 16777619
_UINT32_MASK = 0xFFFFFFFF
_SPECK_CELL_PX = 32


def _clamp_byte(value: float) -> int:
    if value < 0:
        return 0
    if value > 255:
        return 255
    return int(value)


def _fnv_x_prefixes(width: int) -> list[int]:
    """FNV-1a states after hashing the two x bytes, one per column.

    Continuing each prefix with (y & 0xFF, (y >> 8) & 0xFF, salt & 0xFF)
    reproduces road_texture._hash32(x, y, salt) exactly; hoisting the x half
    keeps the per-texel hash affordable at 1024 pixels and beyond.
    """

    prefixes = []
    for x in range(width):
        value = _FNV_OFFSET_BASIS
        for byte in (x & 0xFF, (x >> 8) & 0xFF):
            value ^= byte
            value = (value * _FNV_PRIME) & _UINT32_MASK
        prefixes.append(value)
    return prefixes


def _fnv_row(prefixes: list[int], y: int, salt: int) -> list[int]:
    y_low = y & 0xFF
    y_high = (y >> 8) & 0xFF
    salt_byte = salt & 0xFF
    prime = _FNV_PRIME
    mask = _UINT32_MASK
    return [
        ((((((value ^ y_low) * prime & mask) ^ y_high) * prime & mask)
          ^ salt_byte) * prime & mask)
        for value in prefixes
    ]


def _mottle_lattice(cells: int, amplitude: int, salt: int) -> list[list[int]]:
    span = (2 * amplitude) + 1
    return [
        [(_hash32(cx, cy, salt) % span) - amplitude for cx in range(cells)]
        for cy in range(cells)
    ]


def _column_lerp_tables(
    width: int,
    cells: int,
) -> tuple[list[int], list[float]]:
    columns = []
    fractions = []
    for x in range(width):
        q = x * cells
        columns.append(q // width)
        fractions.append((q % width) / width)
    return columns, fractions


def _speck_list(
    width: int,
    height: int,
    rate_permille: int,
    radius_min: int,
    radius_max: int,
    colors: tuple[tuple[int, int, int], ...],
    salt: int,
) -> list[tuple[int, int, int, tuple[int, int, int]]]:
    if rate_permille <= 0 or not colors:
        return []
    specks = []
    for cell_y in range(height // _SPECK_CELL_PX):
        for cell_x in range(width // _SPECK_CELL_PX):
            digest = _hash32(cell_x, cell_y, salt)
            if digest % 1000 >= rate_permille:
                continue
            center_x = (cell_x * _SPECK_CELL_PX) + ((digest >> 10) % _SPECK_CELL_PX)
            center_y = (cell_y * _SPECK_CELL_PX) + ((digest >> 15) % _SPECK_CELL_PX)
            radius = radius_min + ((digest >> 20) % (radius_max - radius_min + 1))
            color = colors[(digest >> 25) % len(colors)]
            specks.append((center_x, center_y, radius, color))
    return specks


def _paint_specks(
    rgba: bytearray,
    width: int,
    height: int,
    specks: list[tuple[int, int, int, tuple[int, int, int]]],
) -> None:
    for center_x, center_y, radius, (red, green, blue) in specks:
        radius_squared = radius * radius
        for delta_y in range(-radius, radius + 1):
            row = (center_y + delta_y) % height
            for delta_x in range(-radius, radius + 1):
                if (delta_x * delta_x) + (delta_y * delta_y) > radius_squared:
                    continue
                column = (center_x + delta_x) % width
                index = ((row * width) + column) * 4
                rgba[index] = red
                rgba[index + 1] = green
                rgba[index + 2] = blue


def _validate_dimensions(width: int, height: int) -> None:
    for extent in (width, height):
        if extent <= 0 or extent & (extent - 1):
            raise ReplacementTextureError(
                "replacement texture extents must be powers of two, "
                f"got {width}x{height}")


def _surface_rgba(
    *,
    width: int,
    height: int,
    base: tuple[int, int, int],
    coarse_cells: int,
    coarse_amplitude: int,
    fine_amplitude: int,
    salt: int,
    bands: tuple[tuple[int, int, int, tuple[int, int, int]], ...] = (),
) -> bytearray:
    """Seamless mottled surface: wrapped bilinear lattice + per-texel noise.

    ``bands`` entries are (start_numerator, end_numerator, denominator,
    color): horizontal accent bands expressed as exact fractions of the tile
    height so the authored layout is resolution independent.
    """

    if coarse_cells <= 1:
        raise ReplacementTextureError("mottle lattice needs at least 2 cells")
    lattice = _mottle_lattice(coarse_cells, coarse_amplitude, salt)
    column_cells, column_fractions = _column_lerp_tables(width, coarse_cells)
    prefixes = _fnv_x_prefixes(width)
    fine_span = (2 * fine_amplitude) + 1

    band_rows: dict[int, tuple[int, int, int]] = {}
    seam_rows: set[int] = set()
    for start_numerator, end_numerator, denominator, color in bands:
        start_row = (start_numerator * height) // denominator
        end_row = (end_numerator * height) // denominator
        for row in range(start_row, end_row):
            band_rows[row % height] = color
        for row in (start_row, start_row + 1, end_row - 2, end_row - 1):
            seam_rows.add(row % height)

    rgba = bytearray(width * height * 4)
    index = 0
    for y in range(height):
        q = y * coarse_cells
        cell_row = q // height
        ty = (q % height) / height
        upper = lattice[cell_row % coarse_cells]
        lower = lattice[(cell_row + 1) % coarse_cells]
        row_values = [
            (upper[cx] * (1.0 - ty)) + (lower[cx] * ty)
            for cx in range(coarse_cells)
        ]
        row_base = band_rows.get(y, base)
        seam_offset = -22 if y in seam_rows else 0
        base_red, base_green, base_blue = row_base
        noise = _fnv_row(prefixes, y, salt)
        for x in range(width):
            cell = column_cells[x]
            tx = column_fractions[x]
            mottle = (row_values[cell] * (1.0 - tx)) + (
                row_values[(cell + 1) % coarse_cells] * tx)
            offset = (
                mottle
                + ((noise[x] % fine_span) - fine_amplitude)
                + seam_offset
            )
            rgba[index] = _clamp_byte(base_red + offset)
            rgba[index + 1] = _clamp_byte(base_green + offset)
            rgba[index + 2] = _clamp_byte(base_blue + offset)
            rgba[index + 3] = 255
            index += 4
    return rgba


def build_stucco_png(
    *,
    width: int = DEFAULT_TEXTURE_SIZE,
    height: int = DEFAULT_TEXTURE_SIZE,
    base: tuple[int, int, int],
    coarse_cells: int = 12,
    coarse_amplitude: int = 6,
    fine_amplitude: int = 5,
    salt: int,
    bands: tuple[tuple[int, int, int, tuple[int, int, int]], ...] = (),
    speck_rate_permille: int = 0,
    speck_radius_min: int = 2,
    speck_radius_max: int = 3,
    speck_colors: tuple[tuple[int, int, int], ...] = (),
) -> bytes:
    """Fine-noise stucco/plaster, optionally with horizontal accent bands."""

    _validate_dimensions(width, height)
    rgba = _surface_rgba(
        width=width,
        height=height,
        base=base,
        coarse_cells=coarse_cells,
        coarse_amplitude=coarse_amplitude,
        fine_amplitude=fine_amplitude,
        salt=salt,
        bands=bands,
    )
    _paint_specks(
        rgba,
        width,
        height,
        _speck_list(
            width,
            height,
            speck_rate_permille,
            speck_radius_min,
            speck_radius_max,
            speck_colors,
            salt + 1,
        ),
    )
    return encode_png_rgba(width, height, bytes(rgba))


def build_concrete_panel_png(
    *,
    width: int = DEFAULT_TEXTURE_SIZE,
    height: int = DEFAULT_TEXTURE_SIZE,
    base: tuple[int, int, int],
    coarse_cells: int = 8,
    coarse_amplitude: int = 11,
    fine_amplitude: int = 6,
    salt: int,
    chip_rate_permille: int = 120,
    chip_radius_min: int = 2,
    chip_radius_max: int = 4,
    chip_colors: tuple[tuple[int, int, int], ...] = (),
) -> bytes:
    """Coarsely mottled concrete panel with sparse embedded aggregate chips."""

    _validate_dimensions(width, height)
    rgba = _surface_rgba(
        width=width,
        height=height,
        base=base,
        coarse_cells=coarse_cells,
        coarse_amplitude=coarse_amplitude,
        fine_amplitude=fine_amplitude,
        salt=salt,
    )
    _paint_specks(
        rgba,
        width,
        height,
        _speck_list(
            width,
            height,
            chip_rate_permille,
            chip_radius_min,
            chip_radius_max,
            chip_colors,
            salt + 1,
        ),
    )
    return encode_png_rgba(width, height, bytes(rgba))


def build_brick_png(
    *,
    width: int = DEFAULT_TEXTURE_SIZE,
    height: int = DEFAULT_TEXTURE_SIZE,
    columns: int,
    rows: int,
    mortar_px: int,
    mortar_rgb: tuple[int, int, int],
    brick_rgb: tuple[int, int, int],
    tone_span_percent: int,
    dark_rate_percent: int,
    dark_rgb: tuple[int, int, int],
    fine_amplitude: int,
    salt: int,
) -> bytes:
    """Seamless running-bond brick with per-brick FNV tonal variation.

    Brick boundaries are computed in exact ``x * columns`` / ``y * rows``
    integer space, so any row/column count tiles without seams and the grid
    keeps the same UV fractions as the original texture it replaces.
    """

    _validate_dimensions(width, height)
    if columns < 2 or rows < 2:
        raise ReplacementTextureError("brick grid needs at least 2x2 bricks")
    if mortar_px <= 0:
        raise ReplacementTextureError("mortar must be at least one pixel")

    half_brick_q = width // 2
    prefixes = _fnv_x_prefixes(width)
    fine_span = (2 * fine_amplitude) + 1
    mortar_fine_amplitude = max(1, fine_amplitude // 2)
    mortar_fine_span = (2 * mortar_fine_amplitude) + 1
    tone_span = (2 * tone_span_percent) + 1

    column_tables: dict[int, tuple[list[int], list[bool]]] = {}
    for parity in (0, 1):
        parity_columns = []
        parity_mortar = []
        offset = parity * half_brick_q
        for x in range(width):
            q = (x * columns) + offset
            parity_columns.append((q // width) % columns)
            parity_mortar.append((q % width) < (mortar_px * columns))
        column_tables[parity] = (parity_columns, parity_mortar)

    def brick_tone(brick_column: int, brick_row: int) -> tuple[int, int, int]:
        digest = _hash32(brick_column, brick_row, salt)
        if digest % 100 < dark_rate_percent:
            wobble = 1.0 + ((((digest >> 8) % 17) - 8) / 100.0)
            source = dark_rgb
        else:
            wobble = 1.0 + (
                (((digest >> 8) % tone_span) - tone_span_percent) / 100.0)
            source = brick_rgb
        return (
            _clamp_byte(source[0] * wobble),
            _clamp_byte(source[1] * wobble),
            _clamp_byte(source[2] * wobble),
        )

    rgba = bytearray(width * height * 4)
    index = 0
    for y in range(height):
        q = y * rows
        brick_row = q // height
        horizontal_mortar = (q % height) < (mortar_px * rows)
        parity_columns, parity_mortar = column_tables[brick_row & 1]
        row_tones = [
            brick_tone(brick_column, brick_row)
            for brick_column in range(columns)
        ]
        noise = _fnv_row(prefixes, y, salt)
        for x in range(width):
            if horizontal_mortar or parity_mortar[x]:
                offset = (noise[x] % mortar_fine_span) - mortar_fine_amplitude
                red, green, blue = mortar_rgb
            else:
                offset = (noise[x] % fine_span) - fine_amplitude
                red, green, blue = row_tones[parity_columns[x]]
            rgba[index] = _clamp_byte(red + offset)
            rgba[index + 1] = _clamp_byte(green + offset)
            rgba[index + 2] = _clamp_byte(blue + offset)
            rgba[index + 3] = 255
            index += 4
    return encode_png_rgba(width, height, bytes(rgba))


@dataclass(frozen=True)
class ReplacementTexture:
    """One reviewed original-to-replacement pairing.

    ``original_sha256`` pins the audited CityWorld.zip member this
    replacement was color matched against; the palette literals in ``params``
    are the derived constants recorded from that review.
    """

    original_member: str
    original_sha256: str
    original_width: int
    original_height: int
    replacement_member: str
    width: int
    height: int
    generator: str
    params: tuple[tuple[str, object], ...]


REPLACEMENT_TEXTURES: tuple[ReplacementTexture, ...] = (
    ReplacementTexture(
        original_member="asiaconcrete.dds",
        original_sha256=(
            "b757ebb47d21f9954ed196e4cdad72e34bfb85d4853ecd103f295ed4b9a3c4ae"),
        original_width=256,
        original_height=256,
        replacement_member=(
            "cityworld_next_replacements/asiaconcrete_1024.png"),
        width=1024,
        height=1024,
        generator="stucco",
        params=(
            ("base", (201, 197, 184)),
            ("coarse_cells", 12),
            ("coarse_amplitude", 7),
            ("fine_amplitude", 6),
            ("salt", 101),
            ("speck_rate_permille", 140),
            ("speck_radius_min", 2),
            ("speck_radius_max", 3),
            ("speck_colors", ((168, 164, 150), (150, 146, 133))),
        ),
    ),
    ReplacementTexture(
        original_member="darkcrete.dds",
        original_sha256=(
            "83d2e0c6ea7e363f61c45fb9e11ddf0f45d14b2e84173cdfa1804e940aecfc41"),
        original_width=256,
        original_height=256,
        replacement_member=(
            "cityworld_next_replacements/darkcrete_1024.png"),
        width=1024,
        height=1024,
        generator="stucco",
        params=(
            ("base", (45, 48, 42)),
            ("coarse_cells", 12),
            ("coarse_amplitude", 4),
            ("fine_amplitude", 4),
            ("salt", 102),
            ("speck_rate_permille", 110),
            ("speck_radius_min", 2),
            ("speck_radius_max", 2),
            ("speck_colors", ((56, 60, 52), (38, 41, 36))),
        ),
    ),
    ReplacementTexture(
        original_member="redcrete.dds",
        original_sha256=(
            "6917950fbf6df605b7108ce58eab054aedc272c44afc1678adccc3e11ee7a5e4"),
        original_width=256,
        original_height=256,
        replacement_member=(
            "cityworld_next_replacements/redcrete_1024.png"),
        width=1024,
        height=1024,
        generator="stucco",
        params=(
            ("base", (205, 201, 188)),
            ("coarse_cells", 12),
            ("coarse_amplitude", 7),
            ("fine_amplitude", 5),
            ("salt", 103),
            # Horizontal accent bands measured from the original layout:
            # rows [0, 14) and [90, 106) of 256.
            ("bands", (
                (0, 14, 256, (68, 61, 51)),
                (90, 106, 256, (68, 61, 51)),
            )),
            ("speck_rate_permille", 120),
            ("speck_radius_min", 2),
            ("speck_radius_max", 3),
            ("speck_colors", ((172, 168, 154), (152, 148, 135))),
        ),
    ),
    ReplacementTexture(
        original_member="betterbrickdiffuse.dds",
        original_sha256=(
            "39b6168251c434b29b3e7d1fb154c8de4038a86ed58c4d7a91e9aadad4d9dc68"),
        original_width=256,
        original_height=256,
        replacement_member=(
            "cityworld_next_replacements/betterbrickdiffuse_1024.png"),
        width=1024,
        height=1024,
        generator="brick",
        params=(
            ("columns", 7),
            ("rows", 18),
            ("mortar_px", 5),
            ("mortar_rgb", (196, 188, 180)),
            ("brick_rgb", (148, 108, 92)),
            ("tone_span_percent", 20),
            ("dark_rate_percent", 12),
            ("dark_rgb", (104, 58, 44)),
            ("fine_amplitude", 7),
            ("salt", 104),
        ),
    ),
    ReplacementTexture(
        original_member="lightgreybrick.dds",
        original_sha256=(
            "ea7fceea6bc72cd3f081783657bb1bbce275336d40577f3eb31014aa2339a166"),
        original_width=256,
        original_height=256,
        replacement_member=(
            "cityworld_next_replacements/lightgreybrick_1024.png"),
        width=1024,
        height=1024,
        generator="brick",
        params=(
            ("columns", 10),
            ("rows", 18),
            ("mortar_px", 4),
            ("mortar_rgb", (152, 154, 154)),
            ("brick_rgb", (194, 197, 197)),
            ("tone_span_percent", 5),
            ("dark_rate_percent", 8),
            ("dark_rgb", (168, 171, 171)),
            ("fine_amplitude", 5),
            ("salt", 105),
        ),
    ),
    ReplacementTexture(
        original_member="brickwall_darkred.dds",
        original_sha256=(
            "b6dd61d304b05a7c7f730ac78cd41d8b971acfb8d19386dbeb185b9d27adf80c"),
        original_width=128,
        original_height=256,
        replacement_member=(
            "cityworld_next_replacements/brickwall_darkred_1024.png"),
        width=1024,
        height=1024,
        generator="brick",
        params=(
            ("columns", 8),
            ("rows", 32),
            ("mortar_px", 4),
            ("mortar_rgb", (170, 144, 128)),
            ("brick_rgb", (149, 109, 93)),
            ("tone_span_percent", 16),
            ("dark_rate_percent", 10),
            ("dark_rgb", (118, 76, 62)),
            ("fine_amplitude", 6),
            ("salt", 106),
        ),
    ),
    ReplacementTexture(
        original_member="concretetan.dds",
        original_sha256=(
            "72fc1b64f6bf0271426046c749b542dd5ca9a08bdcfa7632cc1a00818a4faa44"),
        original_width=256,
        original_height=256,
        replacement_member=(
            "cityworld_next_replacements/concretetan_1024.png"),
        width=1024,
        height=1024,
        generator="concrete-panel",
        params=(
            ("base", (183, 170, 153)),
            ("coarse_cells", 8),
            ("coarse_amplitude", 11),
            ("fine_amplitude", 6),
            ("salt", 107),
            ("chip_rate_permille", 130),
            ("chip_radius_min", 2),
            ("chip_radius_max", 4),
            ("chip_colors", (
                (167, 112, 78),
                (104, 92, 80),
                (208, 198, 184),
            )),
        ),
    ),
    ReplacementTexture(
        original_member="concretelightgrey.dds",
        original_sha256=(
            "18f28616eba326d26435ede793a78f8225144c246a0fd4912068490e7c14abe8"),
        original_width=256,
        original_height=256,
        replacement_member=(
            "cityworld_next_replacements/concretelightgrey_1024.png"),
        width=1024,
        height=1024,
        generator="stucco",
        params=(
            ("base", (190, 193, 193)),
            ("coarse_cells", 10),
            ("coarse_amplitude", 7),
            ("fine_amplitude", 4),
            ("salt", 108),
            ("speck_rate_permille", 100),
            ("speck_radius_min", 2),
            ("speck_radius_max", 3),
            ("speck_colors", ((166, 169, 170), (176, 179, 179))),
        ),
    ),
)

_GENERATORS = {
    "brick": build_brick_png,
    "stucco": build_stucco_png,
    "concrete-panel": build_concrete_panel_png,
}


def _validate_manifest() -> dict[str, ReplacementTexture]:
    by_original: dict[str, ReplacementTexture] = {}
    replacement_members: set[str] = set()
    for entry in REPLACEMENT_TEXTURES:
        if entry.generator not in _GENERATORS:
            raise ReplacementTextureError(
                f"unknown replacement generator: {entry.generator!r}")
        if not entry.replacement_member.startswith(REPLACEMENT_NAMESPACE):
            raise ReplacementTextureError(
                "replacement member escapes the reserved namespace: "
                f"{entry.replacement_member!r}")
        expected_suffix = f"_{entry.width}.png"
        if not entry.replacement_member.endswith(expected_suffix):
            raise ReplacementTextureError(
                "replacement member must carry its resolution suffix: "
                f"{entry.replacement_member!r}")
        if entry.replacement_member.casefold() in replacement_members:
            raise ReplacementTextureError(
                f"duplicate replacement member: {entry.replacement_member!r}")
        replacement_members.add(entry.replacement_member.casefold())
        if entry.original_member.casefold() in by_original:
            raise ReplacementTextureError(
                f"duplicate original member: {entry.original_member!r}")
        if entry.original_member.casefold() == \
                entry.replacement_member.casefold():
            raise ReplacementTextureError(
                "replacement member must differ from the original member: "
                f"{entry.original_member!r}")
        if len(entry.original_sha256) != 64 or any(
                character not in "0123456789abcdef"
                for character in entry.original_sha256):
            raise ReplacementTextureError(
                "original member digest must be canonical sha256: "
                f"{entry.original_member!r}")
        by_original[entry.original_member.casefold()] = entry
    return by_original


_BY_ORIGINAL = _validate_manifest()


def replacement_manifest() -> list[dict[str, object]]:
    """JSON-ready manifest records, sorted by replacement member name."""

    records = []
    for entry in REPLACEMENT_TEXTURES:
        records.append(
            {
                "generator": entry.generator,
                "original_member": entry.original_member,
                "original_sha256": entry.original_sha256,
                "original_size": [
                    entry.original_width,
                    entry.original_height,
                ],
                "params": {key: value for key, value in entry.params},
                "replacement_member": entry.replacement_member,
                "size": [entry.width, entry.height],
            }
        )
    records.sort(key=lambda record: record["replacement_member"])
    return records


@functools.lru_cache(maxsize=None)
def build_replacement(original_member: str) -> tuple[str, bytes]:
    """Author the replacement for one original member name.

    Returns the namespaced replacement member name and its deterministic
    PNG payload. The payload is generated purely from the manifest literals;
    the original member is never read.
    """

    entry = _BY_ORIGINAL.get(original_member.casefold())
    if entry is None:
        raise ReplacementTextureError(
            f"no reviewed replacement for member: {original_member!r}")
    generator = _GENERATORS[entry.generator]
    payload = generator(
        width=entry.width,
        height=entry.height,
        **{key: value for key, value in entry.params},
    )
    return entry.replacement_member, payload


def build_all_replacements() -> tuple[tuple[str, bytes], ...]:
    """Author every replacement, ordered by replacement member name."""

    built = [
        build_replacement(entry.original_member)
        for entry in REPLACEMENT_TEXTURES
    ]
    built.sort(key=lambda pair: pair[0])
    return tuple(built)
