#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the deterministic DXT1 road base-color decoder."""

import importlib.util
from pathlib import Path
import struct
import sys
import unittest
import zlib


ROOT = Path(__file__).resolve().parents[2]

_SPEC = importlib.util.spec_from_file_location(
    "cityworld_road_texture", ROOT / "tools" / "cityworld_road_texture.py"
)
road_texture = importlib.util.module_from_spec(_SPEC)
assert _SPEC.loader is not None
sys.modules[_SPEC.name] = road_texture
_SPEC.loader.exec_module(road_texture)


def dxt1_block(color0: int, color1: int, lookup: int) -> bytes:
    return struct.pack("<HHI", color0, color1, lookup)


def dds_payload(width: int, height: int, blocks: bytes) -> bytes:
    header = bytearray(128)
    header[0:4] = b"DDS "
    struct.pack_into("<I", header, 4, 124)
    struct.pack_into("<I", header, 12, height)
    struct.pack_into("<I", header, 16, width)
    struct.pack_into("<I", header, 76, 32)
    struct.pack_into("<I", header, 80, road_texture.DDPF_FOURCC)
    header[84:88] = b"DXT1"
    return bytes(header) + blocks


class Dxt1DecodeTests(unittest.TestCase):
    def test_four_color_block_decodes_exact_endpoints(self) -> None:
        # color0 = pure red (0xF800), color1 = pure blue (0x001F);
        # color0 > color1 selects 4-color mode. Lookup all zeros: every
        # texel is color0.
        rgba = road_texture.decode_dxt1_rgba(
            dxt1_block(0xF800, 0x001F, 0), 4, 4)
        self.assertEqual(tuple(rgba[0:4]), (255, 0, 0, 255))
        self.assertEqual(tuple(rgba[-4:]), (255, 0, 0, 255))

    def test_interpolated_colors_use_fixed_rounding(self) -> None:
        # All texels index 2: (2*c0 + c1 + 1) // 3 per channel.
        lookup = 0
        for texel in range(16):
            lookup |= 0x2 << (texel * 2)
        rgba = road_texture.decode_dxt1_rgba(
            dxt1_block(0xF800, 0x001F, lookup), 4, 4)
        self.assertEqual(
            tuple(rgba[0:4]),
            (((2 * 255) + 0 + 1) // 3, 0, ((2 * 0) + 255 + 1) // 3, 255))

    def test_punch_through_mode_stays_opaque(self) -> None:
        # color0 <= color1 selects 3-color mode; index 3 is transparent
        # black in DXT1 but roads must stay opaque.
        lookup = 0
        for texel in range(16):
            lookup |= 0x3 << (texel * 2)
        rgba = road_texture.decode_dxt1_rgba(
            dxt1_block(0x001F, 0xF800, lookup), 4, 4)
        self.assertEqual(tuple(rgba[0:4]), (0, 0, 0, 255))
        self.assertEqual(set(rgba[3::4]), {255})

    def test_geometry_and_size_are_validated(self) -> None:
        with self.assertRaises(road_texture.RoadTextureError):
            road_texture.decode_dxt1_rgba(b"\x00" * 8, 3, 4)
        with self.assertRaises(road_texture.RoadTextureError):
            road_texture.decode_dxt1_rgba(b"\x00" * 7, 4, 4)

    def test_dds_container_is_validated(self) -> None:
        with self.assertRaises(road_texture.RoadTextureError):
            road_texture.decode_dxt1_dds(b"not a dds")
        bad_fourcc = bytearray(dds_payload(4, 4, dxt1_block(0, 0, 0)))
        bad_fourcc[84:88] = b"DXT5"
        with self.assertRaises(road_texture.RoadTextureError):
            road_texture.decode_dxt1_dds(bytes(bad_fourcc))

    def test_shipped_road2_dds_decodes_deterministically(self) -> None:
        payload = (ROOT / "resources" / "textures" / "road2.dds").read_bytes()
        first = road_texture.build_road_basecolor_png(payload)
        second = road_texture.build_road_basecolor_png(payload)
        self.assertEqual(first, second)
        width, height, rgba = road_texture.decode_dxt1_dds(payload)
        self.assertEqual((width, height), (512, 1024))
        self.assertEqual(set(rgba[3::4]), {255})


class PngEncodeTests(unittest.TestCase):
    def test_round_trip_preserves_samples(self) -> None:
        width, height = 8, 4
        rgba = bytearray(width * height * 4)
        rgba[0:4] = bytes((1, 2, 3, 255))
        rgba[-4:] = bytes((9, 8, 7, 255))
        png = road_texture.encode_png_rgba(width, height, bytes(rgba))
        self.assertTrue(png.startswith(b"\x89PNG\r\n\x1a\n"))
        offset = 8
        chunks = {}
        while offset < len(png):
            (length,) = struct.unpack(">I", png[offset:offset + 4])
            tag = png[offset + 4:offset + 8]
            body = png[offset + 8:offset + 8 + length]
            (declared_crc,) = struct.unpack(
                ">I", png[offset + 8 + length:offset + 12 + length])
            self.assertEqual(
                declared_crc, zlib.crc32(tag + body) & 0xFFFFFFFF)
            chunks[tag] = body
            offset += 12 + length
        decoded_width, decoded_height = struct.unpack(
            ">II", chunks[b"IHDR"][:8])
        self.assertEqual((decoded_width, decoded_height), (width, height))
        raw = zlib.decompress(chunks[b"IDAT"])
        stride = 1 + (width * 4)
        self.assertEqual(len(raw), stride * height)
        self.assertEqual(tuple(raw[1:5]), (1, 2, 3, 255))
        self.assertEqual(tuple(raw[-4:]), (9, 8, 7, 255))

    def test_mismatched_payload_is_refused(self) -> None:
        with self.assertRaises(road_texture.RoadTextureError):
            road_texture.encode_png_rgba(4, 4, b"\x00" * 15)


if __name__ == "__main__":
    unittest.main()
