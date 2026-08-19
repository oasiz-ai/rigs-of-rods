#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the independently authored CityWorld replacement textures."""

import importlib.util
from pathlib import Path
import struct
import sys
import unittest
import zlib


ROOT = Path(__file__).resolve().parents[2]

_SPEC = importlib.util.spec_from_file_location(
    "cityworld_replacement_textures",
    ROOT / "tools" / "cityworld_replacement_textures.py",
)
replacement_textures = importlib.util.module_from_spec(_SPEC)
assert _SPEC.loader is not None
sys.modules[_SPEC.name] = replacement_textures
_SPEC.loader.exec_module(replacement_textures)


def decode_png_rgba(payload: bytes) -> tuple[int, int, bytearray]:
    """Minimal decoder for the generator's filter-0 RGBA PNG stream."""

    assert payload[:8] == b"\x89PNG\r\n\x1a\n"
    position = 8
    width = height = None
    compressed = b""
    while position < len(payload):
        (length,) = struct.unpack_from(">I", payload, position)
        tag = payload[position + 4:position + 8]
        body = payload[position + 8:position + 8 + length]
        if tag == b"IHDR":
            width, height, bit_depth, color_type = struct.unpack_from(
                ">IIBB", body, 0)
            assert bit_depth == 8 and color_type == 6
        elif tag == b"IDAT":
            compressed += body
        position += 12 + length
    raw = zlib.decompress(compressed)
    stride = (width * 4) + 1
    rgba = bytearray()
    for row in range(height):
        assert raw[row * stride] == 0, "generator writes filter-0 rows only"
        rgba += raw[(row * stride) + 1:(row + 1) * stride]
    return width, height, rgba


# Small extents keep the suite fast; the generators are extent-agnostic.
TEST_BRICK_PARAMS = dict(
    width=128,
    height=128,
    columns=7,
    rows=9,
    mortar_px=2,
    mortar_rgb=(196, 188, 180),
    brick_rgb=(148, 108, 92),
    tone_span_percent=20,
    dark_rate_percent=12,
    dark_rgb=(104, 58, 44),
    fine_amplitude=7,
    salt=104,
)
TEST_STUCCO_PARAMS = dict(
    width=128,
    height=128,
    base=(201, 197, 184),
    coarse_cells=12,
    coarse_amplitude=7,
    fine_amplitude=6,
    salt=101,
    bands=((0, 14, 256, (68, 61, 51)),),
    speck_rate_permille=140,
    speck_colors=((168, 164, 150), (150, 146, 133)),
)
TEST_PANEL_PARAMS = dict(
    width=128,
    height=128,
    base=(183, 170, 153),
    coarse_cells=8,
    coarse_amplitude=11,
    fine_amplitude=6,
    salt=107,
    chip_rate_permille=130,
    chip_colors=((167, 112, 78), (104, 92, 80)),
)


class ReplacementManifestTests(unittest.TestCase):
    def test_manifest_is_reserved_namespaced_and_collision_free(self) -> None:
        entries = replacement_textures.REPLACEMENT_TEXTURES
        self.assertEqual(len(entries), 8)
        replacement_members = set()
        original_members = set()
        for entry in entries:
            self.assertTrue(
                entry.replacement_member.startswith(
                    replacement_textures.REPLACEMENT_NAMESPACE))
            self.assertTrue(
                entry.replacement_member.endswith(f"_{entry.width}.png"))
            self.assertNotEqual(
                entry.replacement_member.casefold(),
                entry.original_member.casefold())
            # The replacement basename never equals the original member
            # name, so a zip basename fallback can never shadow it.
            basename = entry.replacement_member.rsplit("/", 1)[-1]
            self.assertNotEqual(
                basename.casefold(), entry.original_member.casefold())
            replacement_members.add(entry.replacement_member.casefold())
            original_members.add(entry.original_member.casefold())
            self.assertEqual(len(entry.original_sha256), 64)
            self.assertTrue(
                set(entry.original_sha256) <= set("0123456789abcdef"))
            self.assertIn(entry.generator, ("brick", "stucco",
                                            "concrete-panel"))
        self.assertEqual(len(replacement_members), len(entries))
        self.assertEqual(len(original_members), len(entries))
        self.assertFalse(replacement_members & original_members)

    def test_manifest_records_are_json_ready_and_sorted(self) -> None:
        records = replacement_textures.replacement_manifest()
        self.assertEqual(len(records), 8)
        self.assertEqual(
            [record["replacement_member"] for record in records],
            sorted(record["replacement_member"] for record in records))
        for record in records:
            self.assertEqual(record["size"], [1024, 1024])
            self.assertIsInstance(record["params"], dict)

    def test_unreviewed_member_is_rejected(self) -> None:
        with self.assertRaises(
                replacement_textures.ReplacementTextureError):
            replacement_textures.build_replacement("not-reviewed.dds")


class GeneratorDeterminismTests(unittest.TestCase):
    def test_generators_are_bit_deterministic(self) -> None:
        for build, params in (
            (replacement_textures.build_brick_png, TEST_BRICK_PARAMS),
            (replacement_textures.build_stucco_png, TEST_STUCCO_PARAMS),
            (replacement_textures.build_concrete_panel_png,
             TEST_PANEL_PARAMS),
        ):
            self.assertEqual(build(**params), build(**params))

    def test_generators_are_opaque_rgba8(self) -> None:
        for build, params in (
            (replacement_textures.build_brick_png, TEST_BRICK_PARAMS),
            (replacement_textures.build_stucco_png, TEST_STUCCO_PARAMS),
            (replacement_textures.build_concrete_panel_png,
             TEST_PANEL_PARAMS),
        ):
            width, height, rgba = decode_png_rgba(build(**params))
            self.assertEqual((width, height), (128, 128))
            self.assertTrue(
                all(rgba[index] == 255
                    for index in range(3, len(rgba), 4)))

    def test_tiles_are_seamless_at_the_wrap_edges(self) -> None:
        # A seam would show as a step across the wrap edge much larger than
        # the step between interior neighbours. Compare the mean absolute
        # channel difference across the wrap columns/rows against interior
        # column/row pairs; the noise keeps interior differences well above
        # zero, so a hard seam (~tens of levels) would dominate. Accent
        # bands are excluded here: a band edge on the wrap row is authored
        # content (the original banded texture tiles the same way), so the
        # stucco case runs band-free.
        seam_stucco_params = dict(TEST_STUCCO_PARAMS, bands=())
        for build, params in (
            (replacement_textures.build_brick_png, TEST_BRICK_PARAMS),
            (replacement_textures.build_stucco_png, seam_stucco_params),
            (replacement_textures.build_concrete_panel_png,
             TEST_PANEL_PARAMS),
        ):
            width, height, rgba = decode_png_rgba(build(**params))

            def column_diff(left: int, right: int) -> float:
                total = 0
                for y in range(height):
                    row = y * width * 4
                    for channel in range(3):
                        total += abs(
                            rgba[row + (left * 4) + channel]
                            - rgba[row + (right * 4) + channel])
                return total / (height * 3)

            def row_diff(upper: int, lower: int) -> float:
                total = 0
                for x in range(width):
                    for channel in range(3):
                        total += abs(
                            rgba[((upper * width) + x) * 4 + channel]
                            - rgba[((lower * width) + x) * 4 + channel])
                return total / (width * 3)

            interior_columns = max(
                column_diff(x, x + 1) for x in range(0, width - 1, 7))
            interior_rows = max(
                row_diff(y, y + 1) for y in range(0, height - 1, 7))
            self.assertLessEqual(
                column_diff(width - 1, 0), interior_columns + 2.0)
            self.assertLessEqual(
                row_diff(height - 1, 0), interior_rows + 2.0)

    def test_brick_grid_carries_mortar_and_tonal_variation(self) -> None:
        width, height, rgba = decode_png_rgba(
            replacement_textures.build_brick_png(**TEST_BRICK_PARAMS))
        # The first rows of the tile are horizontal mortar.
        mortar_mean = sum(rgba[0:width * 4:4]) / width
        self.assertGreater(mortar_mean, 150)
        # Brick interiors vary per brick: sample one texel per brick row
        # center and expect more than one distinct tone.
        tones = set()
        rows = TEST_BRICK_PARAMS["rows"]
        for brick_row in range(rows):
            y = ((brick_row * height) + (height // 2)) // rows
            index = ((y * width) + (width // 2)) * 4
            tones.add(tuple(rgba[index:index + 3]))
        self.assertGreater(len(tones), rows // 2)

    def test_stucco_bands_are_placed_by_exact_fractions(self) -> None:
        width, height, rgba = decode_png_rgba(
            replacement_textures.build_stucco_png(**TEST_STUCCO_PARAMS))
        # Band rows [0, 14) of 256 scale to [0, 7) of 128.
        banded = sum(rgba[(3 * width + x) * 4] for x in range(width)) / width
        field = sum(rgba[(64 * width + x) * 4] for x in range(width)) / width
        self.assertLess(banded, 110)
        self.assertGreater(field, 160)

    def test_full_resolution_replacement_builds_once_per_member(self) -> None:
        member, payload = replacement_textures.build_replacement(
            "asiaconcrete.dds")
        self.assertEqual(
            member, "cityworld_next_replacements/asiaconcrete_1024.png")
        width, height, rgba = decode_png_rgba(payload)
        self.assertEqual((width, height), (1024, 1024))
        self.assertTrue(
            all(rgba[index] == 255 for index in range(3, len(rgba), 4)))
        again_member, again_payload = replacement_textures.build_replacement(
            "asiaconcrete.dds")
        self.assertEqual(member, again_member)
        self.assertEqual(payload, again_payload)


if __name__ == "__main__":
    unittest.main()
