#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Deterministic road base-color texture for the CityWorld enhanced overlay.

The combined runtime admits textured procedural roads only through the exact
legacy material closure, whose authenticated native capture reads the texture
back as uncompressed RGBA8. The stock ``road2.dds`` is DXT1, which that
capture refuses, so the overlay ships the same pixels decoded to an
uncompressed PNG. Everything here is stdlib-only and bit-deterministic: the
same DDS bytes always produce the same PNG bytes.

The decoder is intentionally narrow: DXT1 only, top mip level only, and every
output texel is forced fully opaque. Roads are opaque surfaces, and the
Ogre-Next admission policy requires an opaque base color to carry alpha 255
in every texel; a punch-through DXT1 block must not leak alpha 0 into an
otherwise opaque road texture.
"""

from __future__ import annotations

import struct
import zlib


class RoadTextureError(RuntimeError):
    """Raised when the DDS payload cannot be decoded exactly."""


DDS_MAGIC = b"DDS "
DDS_HEADER_BYTES = 128
DDPF_FOURCC = 0x4


def _expand_565(value: int) -> tuple[int, int, int]:
    red5 = (value >> 11) & 0x1F
    green6 = (value >> 5) & 0x3F
    blue5 = value & 0x1F
    return (
        (red5 << 3) | (red5 >> 2),
        (green6 << 2) | (green6 >> 4),
        (blue5 << 3) | (blue5 >> 2),
    )


def _mix(left: int, right: int, left_weight: int, right_weight: int,
         divisor: int) -> int:
    # Fixed round-half-up integer blend: the exact decoder arithmetic is part
    # of the overlay's determinism contract.
    return ((left * left_weight) + (right * right_weight) +
            (divisor // 2)) // divisor


def decode_dxt1_rgba(payload: bytes, width: int, height: int) -> bytearray:
    """Decodes one tightly packed DXT1 level into opaque RGBA8 rows."""

    if width <= 0 or height <= 0 or width % 4 != 0 or height % 4 != 0:
        raise RoadTextureError(
            f"DXT1 level must have positive multiple-of-4 dimensions, "
            f"got {width}x{height}")
    block_columns = width // 4
    block_rows = height // 4
    expected_bytes = block_columns * block_rows * 8
    if len(payload) != expected_bytes:
        raise RoadTextureError(
            f"DXT1 payload is {len(payload)} bytes, expected "
            f"{expected_bytes} for {width}x{height}")

    rgba = bytearray(width * height * 4)
    offset = 0
    for block_row in range(block_rows):
        for block_column in range(block_columns):
            color0, color1, lookup = struct.unpack_from(
                "<HHI", payload, offset)
            offset += 8
            first = _expand_565(color0)
            second = _expand_565(color1)
            if color0 > color1:
                palette = (
                    first,
                    second,
                    tuple(_mix(a, b, 2, 1, 3) for a, b in zip(first, second)),
                    tuple(_mix(a, b, 1, 2, 3) for a, b in zip(first, second)),
                )
            else:
                # Punch-through mode: index 3 is transparent black in DXT1;
                # roads are opaque, so it decodes to opaque black here.
                palette = (
                    first,
                    second,
                    tuple(_mix(a, b, 1, 1, 2) for a, b in zip(first, second)),
                    (0, 0, 0),
                )
            for texel_row in range(4):
                y = (block_row * 4) + texel_row
                row_base = ((y * width) + (block_column * 4)) * 4
                for texel_column in range(4):
                    index = (lookup >> (((texel_row * 4) + texel_column) * 2)
                             ) & 0x3
                    red, green, blue = palette[index]
                    texel_base = row_base + (texel_column * 4)
                    rgba[texel_base] = red
                    rgba[texel_base + 1] = green
                    rgba[texel_base + 2] = blue
                    rgba[texel_base + 3] = 255
    return rgba


def decode_dxt1_dds(payload: bytes) -> tuple[int, int, bytearray]:
    """Decodes the top level of a DXT1 DDS file to opaque RGBA8."""

    if len(payload) < DDS_HEADER_BYTES or payload[:4] != DDS_MAGIC:
        raise RoadTextureError("payload is not a DDS file")
    (header_bytes,) = struct.unpack_from("<I", payload, 4)
    if header_bytes != 124:
        raise RoadTextureError(
            f"DDS header declares {header_bytes} bytes, expected 124")
    (height,) = struct.unpack_from("<I", payload, 12)
    (width,) = struct.unpack_from("<I", payload, 16)
    (pixel_format_bytes,) = struct.unpack_from("<I", payload, 76)
    (pixel_format_flags,) = struct.unpack_from("<I", payload, 80)
    four_cc = payload[84:88]
    if (pixel_format_bytes != 32 or
            not pixel_format_flags & DDPF_FOURCC or
            four_cc != b"DXT1"):
        raise RoadTextureError(
            f"DDS pixel format is not DXT1 (fourcc={four_cc!r})")
    top_level_bytes = (width // 4) * (height // 4) * 8
    top_level = payload[
        DDS_HEADER_BYTES:DDS_HEADER_BYTES + top_level_bytes]
    return width, height, decode_dxt1_rgba(top_level, width, height)


def encode_png_rgba(width: int, height: int, rgba: bytes) -> bytes:
    """Encodes tightly packed RGBA8 rows as a deterministic PNG."""

    if width <= 0 or height <= 0:
        raise RoadTextureError("PNG dimensions must be positive")
    if len(rgba) != width * height * 4:
        raise RoadTextureError(
            f"RGBA payload is {len(rgba)} bytes, expected "
            f"{width * height * 4}")

    def chunk(tag: bytes, body: bytes) -> bytes:
        return (struct.pack(">I", len(body)) + tag + body +
                struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF))

    raw = bytearray()
    stride = width * 4
    for y in range(height):
        raw.append(0)  # filter: none
        raw.extend(rgba[y * stride:(y + 1) * stride])
    return b"".join((
        b"\x89PNG\r\n\x1a\n",
        chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)),
        chunk(b"IDAT", zlib.compress(bytes(raw), level=9)),
        chunk(b"IEND", b""),
    ))


def build_road_basecolor_png(dds_payload: bytes) -> bytes:
    """DXT1 DDS in, deterministic opaque RGBA8 PNG out."""

    width, height, rgba = decode_dxt1_dds(dds_payload)
    return encode_png_rgba(width, height, rgba)


def _hash32(x: int, y: int, salt: int) -> int:
    """Deterministic per-texel hash (FNV-1a over the coordinates)."""

    value = 2166136261
    for byte in (x & 0xFF, (x >> 8) & 0xFF, y & 0xFF, (y >> 8) & 0xFF,
                 salt & 0xFF):
        value ^= byte
        value = (value * 16777619) & 0xFFFFFFFF
    return value


def build_parcel_asphalt_png(size: int = 512) -> bytes:
    """Deterministic tileable asphalt base color for the infill parcels.

    The compiled parcel assets authored near-black factor-only asphalt
    (linear 0.035-0.048), which reads as a void on the lit presenter. This
    tile carries the same tonal family at display brightness with speckle
    and coarse mottling so large pads stop rendering as flat paint. Pure
    stdlib and bit-deterministic; both noise octaves wrap, so the tile is
    seamless.
    """

    if size <= 0 or size & (size - 1):
        raise RoadTextureError("parcel tile size must be a power of two")
    coarse = 16
    cell = size // coarse
    coarse_values = [
        [(_hash32(cx, cy, 7) % 17) - 8 for cx in range(coarse)]
        for cy in range(coarse)
    ]
    rgba = bytearray(size * size * 4)
    for y in range(size):
        cy0, fy = divmod(y, cell)
        ty = fy / cell
        for x in range(size):
            cx0, fx = divmod(x, cell)
            tx = fx / cell
            v00 = coarse_values[cy0 % coarse][cx0 % coarse]
            v10 = coarse_values[cy0 % coarse][(cx0 + 1) % coarse]
            v01 = coarse_values[(cy0 + 1) % coarse][cx0 % coarse]
            v11 = coarse_values[(cy0 + 1) % coarse][(cx0 + 1) % coarse]
            mottle = ((v00 * (1 - tx) + v10 * tx) * (1 - ty) +
                      (v01 * (1 - tx) + v11 * tx) * ty)
            speckle = (_hash32(x, y, 3) % 21) - 10
            base = 62 + mottle + speckle
            luminance = max(34, min(96, int(base)))
            index = ((y * size) + x) * 4
            rgba[index] = luminance
            rgba[index + 1] = luminance + 1
            rgba[index + 2] = luminance + 3
            rgba[index + 3] = 255
    return encode_png_rgba(size, size, bytes(rgba))
