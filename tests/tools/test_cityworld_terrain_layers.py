#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the CityWorld Next enhanced terrain layer generator."""

import importlib.util
from pathlib import Path
import struct
import sys
import unittest
import zlib


ROOT = Path(__file__).resolve().parents[2]

_SPEC = importlib.util.spec_from_file_location(
    "cityworld_terrain_layers", ROOT / "tools" / "cityworld_terrain_layers.py"
)
layers = importlib.util.module_from_spec(_SPEC)
assert _SPEC.loader is not None
sys.modules[_SPEC.name] = layers
_SPEC.loader.exec_module(layers)

_INFILL_SPEC = importlib.util.spec_from_file_location(
    "cityworld_infill_for_terrain_tests", ROOT / "tools" / "cityworld_infill.py"
)
infill = importlib.util.module_from_spec(_INFILL_SPEC)
assert _INFILL_SPEC.loader is not None
sys.modules[_INFILL_SPEC.name] = infill
_INFILL_SPEC.loader.exec_module(infill)


def route_record(route) -> dict:
    """Adapt a built route: points carry x/z and per-point width."""

    return {
        "route_id": route.route_id,
        "xz_points": tuple((point.x, point.z) for point in route.points),
        "width_m": max(point.width_m for point in route.points),
    }


def site_record(site) -> dict:
    return {
        "site_id": site.site_id,
        "category": site.category,
        "polygon_xz_m": site.polygon_xz_m,
    }


class BlendChannelTests(unittest.TestCase):
    def test_authored_routes_paint_the_asphalt_channel(self) -> None:
        asphalt, hard, rock = layers.rasterize_blend_channels(
            [route_record(route) for route in infill.ROUTES],
            [],
            size=512,
        )
        self.assertGreater(sum(1 for v in asphalt if v), 0)
        self.assertEqual(sum(1 for v in hard if v), 0)
        self.assertEqual(sum(1 for v in rock if v), 0)

    def test_every_route_covers_its_own_polyline(self) -> None:
        size = 1024
        for route in infill.ROUTES:
            with self.subTest(route=route.route_id):
                asphalt, _, _ = layers.rasterize_blend_channels(
                    [route_record(route)], [], size=size)
                for x_m, z_m in ((pt.x, pt.z) for pt in route.points):
                    x = int((x_m / layers.WORLD_SIZE_M) * size)
                    z = int((z_m / layers.WORLD_SIZE_M) * size)
                    self.assertEqual(
                        asphalt[(z * size) + x], 255,
                        f"{route.route_id} vertex ({x_m}, {z_m}) uncovered")

    def test_site_categories_select_their_channels(self) -> None:
        size = 512
        _, hard, rock = layers.rasterize_blend_channels(
            [], [site_record(site) for site in infill.SITES], size=size)
        self.assertGreater(sum(1 for v in hard if v), 0)
        self.assertGreater(sum(1 for v in rock if v), 0)

        # Farmland keeps the grass base: a farmland-only rasterization paints
        # nothing at all.
        farmland_only = [
            site_record(site)
            for site in infill.SITES
            if site.category == "farmland"
        ]
        self.assertGreater(len(farmland_only), 0)
        _, hard_none, rock_none = layers.rasterize_blend_channels(
            [], farmland_only, size=size)
        self.assertEqual(sum(1 for v in hard_none if v), 0)
        self.assertEqual(sum(1 for v in rock_none if v), 0)

    def test_site_centers_are_covered(self) -> None:
        size = 1024
        for site in infill.SITES:
            channel_index = layers.SITE_CATEGORY_CHANNEL.get(site.category)
            if channel_index is None:
                continue
            with self.subTest(site=site.site_id):
                channels = layers.rasterize_blend_channels(
                    [], [site_record(site)], size=size)
                x = int((site.center_xz_m[0] / layers.WORLD_SIZE_M) * size)
                z = int((site.center_xz_m[1] / layers.WORLD_SIZE_M) * size)
                self.assertEqual(
                    channels[channel_index][(z * size) + x], 255,
                    f"{site.site_id} center uncovered")

    def test_roads_win_over_parcels(self) -> None:
        # A route crossing a parcel polygon must clear the parcel's channel
        # where they overlap: the drivable surface stays a road.
        size = 256
        route = {
            "route_id": "test-crossing",
            "xz_points": ((0.0, 6000.0), (12000.0, 6000.0)),
            "width_m": 200.0,
        }
        site = {
            "site_id": "test-parcel",
            "category": "suburb",
            "polygon_xz_m": (
                (5000.0, 5000.0),
                (7000.0, 5000.0),
                (7000.0, 7000.0),
                (5000.0, 7000.0),
            ),
        }
        asphalt, hard, _ = layers.rasterize_blend_channels(
            [route], [site], size=size)
        center = ((size // 2) * size) + (size // 2)
        self.assertEqual(asphalt[center], 255)
        self.assertEqual(hard[center], 0)

    def test_invalid_inputs_are_refused(self) -> None:
        with self.assertRaises(layers.TerrainLayerError):
            layers.rasterize_blend_channels([], [], size=1000)  # not 2^n
        with self.assertRaises(layers.TerrainLayerError):
            layers.rasterize_blend_channels(
                [{"route_id": "no-width",
                  "xz_points": ((0.0, 0.0), (1.0, 1.0)),
                  "width_m": 0.0}],
                [],
                size=256,
            )
        with self.assertRaises(layers.TerrainLayerError):
            layers.rasterize_blend_channels(
                [],
                [{"site_id": "degenerate", "category": "suburb",
                  "polygon_xz_m": ((0.0, 0.0), (1.0, 1.0))}],
                size=256,
            )


class PngEncodingTests(unittest.TestCase):
    def test_round_trip_is_valid_and_deterministic(self) -> None:
        size = 8
        red = bytearray(size * size)
        green = bytearray(size * size)
        blue = bytearray(size * size)
        red[0] = 255
        green[size + 1] = 128
        blue[-1] = 7

        first = layers.encode_png_rgba(size, red, green, blue)
        second = layers.encode_png_rgba(size, red, green, blue)
        self.assertEqual(first, second)
        self.assertTrue(first.startswith(b"\x89PNG\r\n\x1a\n"))

        # Decode and verify the exact samples survive.
        offset = 8
        chunks = {}
        while offset < len(first):
            (length,) = struct.unpack(">I", first[offset:offset + 4])
            tag = first[offset + 4:offset + 8]
            payload = first[offset + 8:offset + 8 + length]
            (declared_crc,) = struct.unpack(
                ">I", first[offset + 8 + length:offset + 12 + length])
            self.assertEqual(
                declared_crc, zlib.crc32(tag + payload) & 0xFFFFFFFF)
            chunks[tag] = payload
            offset += 12 + length

        width, height, depth, colour = struct.unpack(
            ">IIBB", chunks[b"IHDR"][:10])
        self.assertEqual((width, height, depth, colour), (size, size, 8, 6))
        raw = zlib.decompress(chunks[b"IDAT"])
        stride = 1 + (size * 4)
        self.assertEqual(len(raw), stride * size)
        self.assertEqual(raw[0], 0)  # filter byte
        self.assertEqual(raw[1], 255)  # red[0]
        self.assertEqual(raw[stride + 1 + 4 + 1], 128)  # green[1,1]
        self.assertEqual(raw[-2], 7)  # blue[-1]
        self.assertEqual(raw[-1], 255)  # opaque alpha

    def test_mismatched_channels_are_refused(self) -> None:
        with self.assertRaises(layers.TerrainLayerError):
            layers.encode_png_rgba(4, bytearray(16), bytearray(15),
                                   bytearray(16))


class OtcCompositionTests(unittest.TestCase):
    def test_global_config_enables_normal_mapping(self) -> None:
        text = layers.build_global_otc("CityWorldNext-page-0-0.otc")
        self.assertIn("NormalMappingEnabled=1", text)
        self.assertIn("SpecularMappingEnabled=1", text)
        self.assertIn("PageFileFormat=CityWorldNext-page-0-0.otc", text)
        self.assertIn("WorldSizeX=12000", text)
        self.assertIn(f"LayerBlendMapSize={layers.BLEND_MAP_SIZE}", text)

    def test_page_config_matches_the_parser_contract(self) -> None:
        text = layers.build_page_otc()
        lines = [line for line in text.splitlines() if line]
        self.assertEqual(lines[0], "CityWorld.raw")
        self.assertEqual(int(lines[1]), len(layers.TERRAIN_LAYERS))
        # Base layer has exactly three fields; blended layers have six.
        data_lines = [line for line in lines[2:] if not line.startswith(";")]
        base_fields = [field.strip() for field in data_lines[0].split(",")]
        self.assertEqual(len(base_fields), 3)
        for line in data_lines[1:]:
            fields = [field.strip() for field in line.split(",")]
            self.assertEqual(len(fields), 6, line)
            self.assertIn(fields[4], ("R", "G", "B", "A"), line)
            self.assertEqual(
                fields[3], "cityworld_next_terrain_blend.png", line)

    def test_blended_layers_use_distinct_channels(self) -> None:
        channels = [
            layer["channel"]
            for layer in layers.TERRAIN_LAYERS
            if layer["channel"] is not None
        ]
        self.assertEqual(len(channels), len(set(channels)))


if __name__ == "__main__":
    unittest.main()
