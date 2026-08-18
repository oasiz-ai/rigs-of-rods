#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the CityWorld Next enhanced terrain layer set.

The original CityWorld terrain is a single grass layer over a flat 12 km plane
with normal mapping disabled, so every district reads as the same ground no
matter what stands on it. This module derives a multi-layer terrain from the
plan the overlay already computes - its access routes and its authored site
polygons - and emits the OTC configuration plus one RGBA blend map that drives
the extra layers from separate channels.

No geometry, texture, or byte is copied from the source archive here. The
layers name textures that the mounted read-only archive already provides, and
the blend coverage is computed from project-authored route and site geometry.
"""

from __future__ import annotations

import struct
import zlib
from typing import Iterable, Mapping, Sequence


#: The terrain plane the original CityWorld declares, in metres.
WORLD_SIZE_M = 12000.0

#: Blend-map resolution. The runtime resizes to the terrain's own blend-map
#: size, so this only has to be fine enough to keep an 8 m road from
#: disappearing: at 1024 across 12 km one texel is 11.7 m, which loses them.
#: 2048 gives 5.86 m per texel, so the narrowest authored route still covers a
#: texel run rather than aliasing away.
BLEND_MAP_SIZE = 2048

#: Layer 0 is the base and takes no blend map. Every later layer reads one
#: channel of the shared map, which keeps a single image serving all of them.
#: Texture names resolve against the mounted original archive.
TERRAIN_LAYERS = (
    {
        "world_size": 24.0,
        "diffusespecular": "CityWorld_grass.dds",
        "normalheight": "blank_NRM.dds",
        "blendmap": None,
        "channel": None,
        "alpha": 1.0,
        "reason": "base ground retained from the original terrain",
    },
    {
        "world_size": 12.0,
        "diffusespecular": "NQ2-0-asphalt.png",
        "normalheight": "blank_NRM.dds",
        "blendmap": "cityworld_next_terrain_blend.png",
        "channel": "R",
        "alpha": 1.0,
        "reason": "NeoQueretaro 2.0 asphalt under the authored access routes",
    },
    {
        "world_size": 16.0,
        "diffusespecular": "asiaconcrete.dds",
        "normalheight": "blank_NRM.dds",
        "blendmap": "cityworld_next_terrain_blend.png",
        "channel": "G",
        "alpha": 1.0,
        "reason": "hard standing under service and suburb parcels",
    },
    {
        "world_size": 32.0,
        "diffusespecular": "NQ-rock-A.jpg",
        "normalheight": "blank_NRM.dds",
        "blendmap": "cityworld_next_terrain_blend.png",
        "channel": "B",
        "alpha": 1.0,
        "reason": "NeoQueretaro rock around natural landmarks",
    },
)

#: Which blend channel each authored site category contributes to. Farmland is
#: deliberately absent: it keeps the grass base, which is what farmland is.
SITE_CATEGORY_CHANNEL = {
    "service-station": 1,
    "suburb": 1,
    "natural-landmark": 2,
}

#: Routes widen slightly in the blend map relative to their driven width, so
#: the surface reads as a road with shoulders rather than a hard-edged ribbon.
ROUTE_BLEND_MARGIN_M = 3.0


class TerrainLayerError(RuntimeError):
    """Fail-closed diagnostic for an invalid terrain layer request."""


def _world_to_texel(value_m: float, size: int) -> float:
    return (value_m / WORLD_SIZE_M) * float(size)


def _stamp_disc(
    channel: bytearray,
    size: int,
    center_x: float,
    center_z: float,
    radius: float,
) -> None:
    """Paint a filled disc at full strength, clipped to the map."""

    if radius <= 0.0:
        return
    # A narrow feature must never round to zero coverage: an 8 m road on a
    # 2048-texel map has sub-texel radius, and losing it would silently drop
    # the road from the surface. One texel is the floor.
    effective_radius = max(radius, 1.0)
    min_x = max(0, int(center_x - effective_radius) - 1)
    max_x = min(size - 1, int(center_x + effective_radius) + 1)
    min_z = max(0, int(center_z - effective_radius) - 1)
    max_z = min(size - 1, int(center_z + effective_radius) + 1)
    radius_squared = effective_radius * effective_radius
    for z in range(min_z, max_z + 1):
        dz = (float(z) + 0.5) - center_z
        row = z * size
        for x in range(min_x, max_x + 1):
            dx = (float(x) + 0.5) - center_x
            if (dx * dx) + (dz * dz) <= radius_squared:
                channel[row + x] = 255
    # Guarantee the center texel regardless of rounding.
    center_texel_x = min(size - 1, max(0, int(center_x)))
    center_texel_z = min(size - 1, max(0, int(center_z)))
    channel[(center_texel_z * size) + center_texel_x] = 255


def _stamp_segment(
    channel: bytearray,
    size: int,
    start: tuple[float, float],
    end: tuple[float, float],
    radius: float,
) -> None:
    """Paint a capsule along one route segment.

    Discs are stamped along the segment rather than solving the capsule
    analytically: the routes are short and this keeps the coverage identical
    for a segment and its reverse, which a scanline fill would not guarantee.
    """

    start_x, start_z = start
    end_x, end_z = end
    span = max(abs(end_x - start_x), abs(end_z - start_z))
    steps = max(1, int(span * 2.0))
    for step in range(steps + 1):
        ratio = float(step) / float(steps)
        _stamp_disc(
            channel,
            size,
            start_x + ((end_x - start_x) * ratio),
            start_z + ((end_z - start_z) * ratio),
            radius,
        )


def _stamp_polygon(
    channel: bytearray,
    size: int,
    polygon: Sequence[tuple[float, float]],
) -> None:
    """Fill a convex or concave polygon by even-odd scanline crossing."""

    if len(polygon) < 3:
        return
    min_z = max(0, int(min(point[1] for point in polygon)))
    max_z = min(size - 1, int(max(point[1] for point in polygon)) + 1)
    for z in range(min_z, max_z + 1):
        sample_z = float(z) + 0.5
        crossings: list[float] = []
        for index in range(len(polygon)):
            x0, z0 = polygon[index]
            x1, z1 = polygon[(index + 1) % len(polygon)]
            if (z0 <= sample_z < z1) or (z1 <= sample_z < z0):
                ratio = (sample_z - z0) / (z1 - z0)
                crossings.append(x0 + ((x1 - x0) * ratio))
        crossings.sort()
        row = z * size
        for pair in range(0, len(crossings) - 1, 2):
            start_x = max(0, int(crossings[pair]))
            end_x = min(size - 1, int(crossings[pair + 1]) + 1)
            for x in range(start_x, end_x + 1):
                channel[row + x] = 255


def rasterize_blend_channels(
    routes: Iterable[Mapping[str, object]],
    sites: Iterable[Mapping[str, object]],
    size: int = BLEND_MAP_SIZE,
) -> tuple[bytearray, bytearray, bytearray]:
    """Rasterize authored routes and sites into three blend channels.

    Returns (asphalt, hard standing, rock). The base grass layer needs no
    channel: it is whatever the others do not cover.
    """

    if size <= 0 or (size & (size - 1)) != 0:
        raise TerrainLayerError(
            f"the blend map size must be a positive power of two: {size}")

    asphalt = bytearray(size * size)
    hard_standing = bytearray(size * size)
    rock = bytearray(size * size)
    channels = (asphalt, hard_standing, rock)

    for route in routes:
        points = route.get("xz_points") or ()
        if len(points) < 2:
            continue
        width_m = float(route.get("width_m") or 0.0)
        if width_m <= 0.0:
            raise TerrainLayerError(
                f"route {route.get('route_id')!r} has no positive width")
        radius = _world_to_texel(
            (width_m * 0.5) + ROUTE_BLEND_MARGIN_M, size)
        for index in range(len(points) - 1):
            start = (
                _world_to_texel(float(points[index][0]), size),
                _world_to_texel(float(points[index][1]), size),
            )
            end = (
                _world_to_texel(float(points[index + 1][0]), size),
                _world_to_texel(float(points[index + 1][1]), size),
            )
            _stamp_segment(asphalt, size, start, end, radius)

    for site in sites:
        category = str(site.get("category") or "")
        channel_index = SITE_CATEGORY_CHANNEL.get(category)
        if channel_index is None:
            # Farmland keeps the grass base by design.
            continue
        polygon = site.get("polygon_xz_m") or ()
        if len(polygon) < 3:
            raise TerrainLayerError(
                f"site {site.get('site_id')!r} has no usable polygon")
        _stamp_polygon(
            channels[channel_index],
            size,
            [
                (
                    _world_to_texel(float(point[0]), size),
                    _world_to_texel(float(point[1]), size),
                )
                for point in polygon
            ],
        )

    # Routes win over parcels: a road crossing a forecourt is still a road.
    for index in range(size * size):
        if asphalt[index]:
            hard_standing[index] = 0
            rock[index] = 0

    return asphalt, hard_standing, rock


def encode_png_rgba(
    size: int,
    red: Sequence[int],
    green: Sequence[int],
    blue: Sequence[int],
) -> bytes:
    """Encode one RGBA PNG with a fully opaque alpha channel.

    Written here rather than taken from a dependency so the overlay build
    keeps its existing standard-library-only closure.
    """

    expected = size * size
    for name, channel in (("red", red), ("green", green), ("blue", blue)):
        if len(channel) != expected:
            raise TerrainLayerError(
                f"the {name} channel has {len(channel)} samples, "
                f"expected {expected}")

    raw = bytearray()
    for z in range(size):
        raw.append(0)  # filter type 0, no prediction
        row = z * size
        for x in range(size):
            index = row + x
            raw.append(red[index])
            raw.append(green[index])
            raw.append(blue[index])
            raw.append(255)

    def chunk(tag: bytes, payload: bytes) -> bytes:
        return (
            struct.pack(">I", len(payload))
            + tag
            + payload
            + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
        )

    header = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return b"".join(
        (
            b"\x89PNG\r\n\x1a\n",
            chunk(b"IHDR", header),
            # Fixed compression level keeps the overlay byte-deterministic.
            chunk(b"IDAT", zlib.compress(bytes(raw), 9)),
            chunk(b"IEND", b""),
        )
    )


def build_global_otc(
    page_config_name: str,
    heightmap_size: int = 5,
) -> str:
    """Compose the overlay's terrain configuration.

    Normal mapping is enabled here, unlike the original, so the layers have
    surface response instead of reading flat under every light.
    """

    return "\n".join(
        (
            "; Generated by tools/cityworld_terrain_layers.py",
            "; Enhanced multi-layer terrain for the CityWorld Next overlay.",
            "; The heightmap and every named texture are read from the",
            "; separately installed original archive; none are copied here.",
            f"Heightmap.0.0.raw.size={heightmap_size}",
            "Heightmap.0.0.raw.bpp=2",
            "Heightmap.0.0.flipX=0",
            "",
            "Flat=1",
            "",
            f"WorldSizeX={int(WORLD_SIZE_M)}",
            f"WorldSizeZ={int(WORLD_SIZE_M)}",
            "WorldSizeY=0",
            "",
            "disableCaching=1",
            "",
            f"PageFileFormat={page_config_name}",
            "",
            "MaxPixelError=0",
            "LightmapEnabled=0",
            "SpecularMappingEnabled=1",
            "NormalMappingEnabled=1",
            f"LayerBlendMapSize={BLEND_MAP_SIZE}",
            "",
        )
    )


def build_page_otc(heightmap_name: str = "CityWorld.raw") -> str:
    """Compose the page configuration naming every layer."""

    lines = [
        heightmap_name,
        str(len(TERRAIN_LAYERS)),
        "; worldSize, diffusespecular, normalheight, blendmap, blendmapmode, alpha",
    ]
    for layer in TERRAIN_LAYERS:
        fields = [
            f"{layer['world_size']:g}",
            str(layer["diffusespecular"]),
            str(layer["normalheight"]),
        ]
        if layer["blendmap"] is not None:
            fields.append(str(layer["blendmap"]))
            fields.append(str(layer["channel"]))
            fields.append(f"{layer['alpha']:g}")
        lines.append(", ".join(fields))
    lines.append("")
    return "\n".join(lines)
