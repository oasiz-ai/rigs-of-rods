#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the deterministic Alexis Saber body paint generator."""

import hashlib
import importlib.util
from pathlib import Path
import struct
import sys
import unittest
import zlib


ROOT = Path(__file__).resolve().parents[2]

_SPEC = importlib.util.spec_from_file_location(
    "alexis_saber_paint", ROOT / "tools" / "alexis_saber_paint.py"
)
paint = importlib.util.module_from_spec(_SPEC)
assert _SPEC.loader is not None
sys.modules[_SPEC.name] = paint
_SPEC.loader.exec_module(paint)


# The archive replacement tool writes exactly these bytes into
# USER:/mods/AlexisSaber.zip, so the paint is pinned: retuning the generator
# has to move this table in the same commit that moves the art.
PINNED_MEMBER_SHA256 = {
    "bodytemp.png":
        "032555688d1fd236f4dd50897446c622ef3f003c6ed6ad02b6ca8f706030357b",
    "bodytempspec.png":
        "61646baa4fad5c05366c11f36000c50fd6075830c169c4e4ea123edb8a74635e",
    "body_black.png":
        "c50b3cde9475e08dc869f9ca1a907b4f483cdc56426c3364a23b49265ee82fa0",
    "body_blackspec.png":
        "74baf89871f96617497cea5a2067cd131e8414fe2b764ae0e73c4ee2875af38e",
    "body_blue.png":
        "d48b297f96977928dc852578b3fc7e3e0b95c6bead9e74901dc2cdd442e00441",
    "body_bluespec.png":
        "a86f7f5c0adee0b93aa796f8a471eba49f9eaad1ac889a3dcb00da9c0bac9bc5",
    "body_green.png":
        "9fbb6849e031e446a17e4051b41f4289272d708af64faa431368908fe2655ecc",
    "body_greenspec.png":
        "cd360baefbd03c3cc2a650f982c52b50af32e1d062ddf10eae52e33c22c5788d",
    "body_purple.png":
        "b6ef84d77ccda6bc20f6dc4f1acfc149a993662bf129c2ebc676f5fa3184b44d",
    "body_purplespec.png":
        "a96a52ec4aa3677b9bc4f029cbbc36803ecaa7991024a2e0876e6ef731dd5390",
    "body_white.png":
        "3c402211ef6e134379826d666f8b0ad6ecb8c7c31b6a12ab267914fe6f6c2ad1",
    "body_whitespec.png":
        "cdebefa5d1e83399de705f8f40b0ee76a80cc0060e54d308cd999d48240a3e0d",
}


def decode_png_rgba(payload: bytes) -> tuple[int, int, bytes]:
    """Minimal decoder for the filter-0 truecolour-alpha PNGs authored here."""

    assert payload[:8] == b"\x89PNG\r\n\x1a\n"
    offset = 8
    width = height = 0
    idat = bytearray()
    while offset < len(payload):
        (length,) = struct.unpack_from(">I", payload, offset)
        tag = payload[offset + 4:offset + 8]
        body = payload[offset + 8:offset + 8 + length]
        offset += 12 + length
        if tag == b"IHDR":
            width, height, depth, color, comp, filt, interlace = struct.unpack(
                ">IIBBBBB", body)
            assert (depth, color, comp, filt, interlace) == (8, 6, 0, 0, 0)
        elif tag == b"IDAT":
            idat.extend(body)
    raw = zlib.decompress(bytes(idat))
    stride = width * 4
    rgba = bytearray()
    for y in range(height):
        row_start = y * (stride + 1)
        assert raw[row_start] == 0, "authored PNGs use filter type 0 only"
        rgba.extend(raw[row_start + 1:row_start + 1 + stride])
    return width, height, bytes(rgba)


def channel_mean(rgba: bytes, channel: int) -> float:
    values = rgba[channel::4]
    return sum(values) / len(values)


class PaintDeterminismTests(unittest.TestCase):
    def test_repeated_builds_are_bit_identical(self) -> None:
        # Deliberately the uncached entry point: `build_paint_members` is
        # memoised, so going through it would compare a value with itself.
        skin = paint.PAINT_SKINS[0]
        self.assertEqual(paint.build_paint_pair(skin),
                         paint.build_paint_pair(skin))

    def test_authored_members_match_their_pinned_digests(self) -> None:
        members = paint.build_paint_members()
        self.assertEqual(set(members), set(PINNED_MEMBER_SHA256))
        for name, payload in members.items():
            with self.subTest(member=name):
                self.assertEqual(
                    hashlib.sha256(payload).hexdigest(),
                    PINNED_MEMBER_SHA256[name])

    def test_member_names_cover_every_skin_slot(self) -> None:
        names = paint.paint_member_names()
        self.assertEqual(len(names), len(set(names)))
        # The six placeholders the archive already carried must all be
        # authored, or the replacement leaves a 5x5 solid behind.
        for existing in ("bodytemp.png", "bodytempspec.png", "body_black.png",
                         "body_blue.png", "body_green.png", "body_purple.png",
                         "body_white.png"):
            self.assertIn(existing, names)
        # Every base colour has exactly one paired specular member.
        for skin in paint.PAINT_SKINS:
            self.assertIn(skin.specular_member, names)
            self.assertTrue(skin.specular_member.endswith("spec.png"))


class PaintFormatTests(unittest.TestCase):
    def setUp(self) -> None:
        self.members = paint.build_paint_members()

    def test_every_member_is_square_opaque_rgba8(self) -> None:
        for name, payload in self.members.items():
            with self.subTest(member=name):
                width, height, rgba = decode_png_rgba(payload)
                self.assertEqual(width, paint.PAINT_SIZE)
                self.assertEqual(height, paint.PAINT_SIZE)
                self.assertEqual(set(rgba[3::4]), {255})

    def test_specular_members_are_greyscale(self) -> None:
        # The lowering decodes the specular member as LINEAR_DATA and the
        # shipped *Spec art is greyscale; a coloured specular map would be a
        # different convention from the rest of the vehicle.
        for skin in paint.PAINT_SKINS:
            with self.subTest(member=skin.specular_member):
                _, _, rgba = decode_png_rgba(self.members[skin.specular_member])
                self.assertEqual(rgba[0::4], rgba[1::4])
                self.assertEqual(rgba[0::4], rgba[2::4])

    def test_base_colour_means_stay_on_the_authored_swatch(self) -> None:
        for skin in paint.PAINT_SKINS:
            _, _, rgba = decode_png_rgba(self.members[skin.base_color_member])
            for channel, authored in enumerate(skin.base_rgb):
                with self.subTest(member=skin.base_color_member,
                                  channel=channel):
                    # Flake and sheen clamp against 0 and 255, so a channel
                    # pinned at an extreme drifts; 6 levels is the budget.
                    self.assertLessEqual(
                        abs(channel_mean(rgba, channel) - authored), 6.0)

    def test_specular_means_hold_the_clearcoat_level(self) -> None:
        for skin in paint.PAINT_SKINS:
            with self.subTest(member=skin.specular_member):
                _, _, rgba = decode_png_rgba(self.members[skin.specular_member])
                self.assertLessEqual(
                    abs(channel_mean(rgba, 0) -
                        paint.CLEARCOAT_SPECULAR_LEVEL), 2.0)

    def test_specular_carries_real_variation(self) -> None:
        # The presenter pins roughness at 1.0 for this vehicle, so the
        # specular map is the only spatially varying specular input there is.
        # A flat map would make the paint indistinguishable from the 5x5
        # placeholder it replaces.
        for skin in paint.PAINT_SKINS:
            with self.subTest(member=skin.specular_member):
                _, _, rgba = decode_png_rgba(self.members[skin.specular_member])
                values = rgba[0::4]
                self.assertGreaterEqual(len(set(values)), 64)
                self.assertGreaterEqual(max(values) - min(values), 60)

    def test_every_skin_is_a_distinct_paint(self) -> None:
        payloads = [self.members[skin.specular_member]
                    for skin in paint.PAINT_SKINS]
        self.assertEqual(len(payloads), len(set(payloads)))


class PaintLayoutTests(unittest.TestCase):
    """The body panels each unwrap across the whole 0..1 square."""

    def test_lattice_sampling_wraps_without_a_seam(self) -> None:
        size = paint.PAINT_SIZE
        for cells in (paint._SHEEN_CELLS, paint._PEEL_CELLS):
            step = size // cells
            weights = paint._smooth_weights(step)
            field = paint._lattice(cells, 0x11)
            for coordinate in (0, 7, step, size // 2):
                self.assertEqual(
                    paint._sample_lattice(field, cells, step, weights,
                                          size, coordinate),
                    paint._sample_lattice(field, cells, step, weights,
                                          0, coordinate))
                self.assertEqual(
                    paint._sample_lattice(field, cells, step, weights,
                                          coordinate, size),
                    paint._sample_lattice(field, cells, step, weights,
                                          coordinate, 0))

    def test_no_large_scale_structure_across_the_square(self) -> None:
        # Any gradient or landmark would repeat once per body panel and break
        # at every panel seam, because the panels do not share a UV atlas.
        size = paint.PAINT_SIZE
        members = paint.build_paint_members()
        for skin in paint.PAINT_SKINS:
            _, _, rgba = decode_png_rgba(members[skin.specular_member])
            rows = [rgba[y * size * 4:(y + 1) * size * 4] for y in range(size)]
            top = sum(sum(row[0::4]) for row in rows[:size // 2])
            bottom = sum(sum(row[0::4]) for row in rows[size // 2:])
            left = sum(sum(row[0:size * 2:4]) for row in rows)
            right = sum(sum(row[size * 2::4]) for row in rows)
            half = size * size // 2
            with self.subTest(member=skin.specular_member):
                self.assertLessEqual(abs(top - bottom) / half, 3.0)
                self.assertLessEqual(abs(left - right) / half, 3.0)


class PaintValidationTests(unittest.TestCase):
    def test_non_power_of_two_sizes_are_refused(self) -> None:
        with self.assertRaises(paint.PaintError):
            paint.build_paint_pair(paint.PAINT_SKINS[0], 500)

    def test_sizes_below_the_lattice_are_refused(self) -> None:
        with self.assertRaises(paint.PaintError):
            paint.build_paint_pair(paint.PAINT_SKINS[0], 16)

    def test_zero_size_is_refused(self) -> None:
        with self.assertRaises(paint.PaintError):
            paint.build_paint_pair(paint.PAINT_SKINS[0], 0)


if __name__ == "__main__":
    unittest.main()
