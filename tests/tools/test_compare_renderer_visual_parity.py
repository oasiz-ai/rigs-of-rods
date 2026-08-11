#!/usr/bin/env python3
"""Focused tests for the dependency-free paired renderer parity oracle."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL = REPOSITORY_ROOT / "tools/compare_renderer_visual_parity.py"
METADATA_SCHEMA = "ror.renderer_visual_parity_frame_metadata.v1"
ZERO_SHA256 = "0" * 64
ONE_SHA256 = "1" * 64


def png_chunk(chunk_type: bytes, payload: bytes) -> bytes:
    crc = zlib.crc32(chunk_type)
    crc = zlib.crc32(payload, crc) & 0xFFFFFFFF
    return struct.pack(">I", len(payload)) + chunk_type + payload + struct.pack(">I", crc)


def paeth(left: int, above: int, upper_left: int) -> int:
    estimate = left + above - upper_left
    distances = (
        abs(estimate - left),
        abs(estimate - above),
        abs(estimate - upper_left),
    )
    if distances[0] <= distances[1] and distances[0] <= distances[2]:
        return left
    if distances[1] <= distances[2]:
        return above
    return upper_left


def filtered_row(current: bytes, previous: bytes, bpp: int, filter_type: int) -> bytes:
    if filter_type == 0:
        return current
    encoded = bytearray(len(current))
    for index, value in enumerate(current):
        left = current[index - bpp] if index >= bpp else 0
        above = previous[index]
        upper_left = previous[index - bpp] if index >= bpp else 0
        if filter_type == 1:
            predictor = left
        elif filter_type == 2:
            predictor = above
        elif filter_type == 3:
            predictor = (left + above) // 2
        elif filter_type == 4:
            predictor = paeth(left, above, upper_left)
        else:
            raise ValueError("invalid test filter")
        encoded[index] = (value - predictor) & 0xFF
    return bytes(encoded)


def make_png(
    width: int,
    height: int,
    pixels: bytes,
    *,
    color_type: int = 2,
    interlace: int = 0,
    filters: tuple[int, ...] = (0,),
    ancillary: tuple[tuple[bytes, bytes], ...] = (),
) -> bytes:
    bpp = 3 if color_type == 2 else 4 if color_type == 6 else 1
    expected = width * height * bpp
    if len(pixels) != expected:
        raise ValueError(f"expected {expected} bytes, received {len(pixels)}")
    previous = bytes(width * bpp)
    scanlines = bytearray()
    for row in range(height):
        current = pixels[row * width * bpp : (row + 1) * width * bpp]
        filter_type = filters[row % len(filters)]
        scanlines.append(filter_type)
        scanlines.extend(filtered_row(current, previous, bpp, filter_type))
        previous = current
    header = struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, interlace)
    payload = bytearray(b"\x89PNG\r\n\x1a\n")
    payload.extend(png_chunk(b"IHDR", header))
    for chunk_type, chunk_payload in ancillary:
        payload.extend(png_chunk(chunk_type, chunk_payload))
    payload.extend(png_chunk(b"IDAT", zlib.compress(bytes(scanlines), level=9)))
    payload.extend(png_chunk(b"IEND", b""))
    return bytes(payload)


def scene_pixels(width: int, height: int) -> bytes:
    pixels = bytearray()
    for y in range(height):
        for x in range(width):
            pixels.extend(
                (
                    (x * 7 + y * 3) & 0xFF,
                    (x * 2 + y * 9 + 17) & 0xFF,
                    (x * 5 + y * 5 + 31) & 0xFF,
                )
            )
    return bytes(pixels)


def metadata(width: int, height: int, renderer_name: str, build_sha: str) -> dict:
    return {
        "schema": METADATA_SCHEMA,
        "renderer": {
            "name": renderer_name,
            "backend": "metal",
            "build_sha256": build_sha,
        },
        "content": {
            "scene": "CityWorld",
            "content_sha256": "2" * 64,
        },
        "camera": {
            "projection": "perspective",
            "position": [10.0, 2.0, -4.0],
            "orientation_xyzw": [0.0, 0.0, 0.0, 1.0],
            "vertical_fov_degrees": 60.0,
            "near_clip": 0.1,
            "far_clip": 2000.0,
        },
        "exposure": {"mode": "manual", "ev100": 11.5},
        "weather": {"preset": "clear", "time_of_day_seconds": 43200},
        "resolution": {"width": width, "height": height},
        "color_space": "srgb",
        "ui_free": True,
    }


class VisualParityOracleTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.width = 32
        self.height = 24
        self.reference_pixels = scene_pixels(self.width, self.height)
        self.candidate_pixels = bytes(self.reference_pixels)
        self.reference_png = self.root / "reference.png"
        self.candidate_png = self.root / "candidate.png"
        self.reference_metadata = self.root / "reference.json"
        self.candidate_metadata = self.root / "candidate.json"
        self.output = self.root / "receipt.json"
        self.write_inputs()

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def write_inputs(
        self,
        *,
        reference_png: bytes | None = None,
        candidate_png: bytes | None = None,
        reference_metadata: dict | None = None,
        candidate_metadata: dict | None = None,
    ) -> None:
        self.reference_png.write_bytes(
            reference_png
            if reference_png is not None
            else make_png(self.width, self.height, self.reference_pixels)
        )
        self.candidate_png.write_bytes(
            candidate_png
            if candidate_png is not None
            else make_png(self.width, self.height, self.candidate_pixels)
        )
        reference = (
            reference_metadata
            if reference_metadata is not None
            else metadata(self.width, self.height, "ogre14", ZERO_SHA256)
        )
        candidate = (
            candidate_metadata
            if candidate_metadata is not None
            else metadata(self.width, self.height, "ogre-next", ONE_SHA256)
        )
        self.reference_metadata.write_text(
            json.dumps(reference, sort_keys=True) + "\n", encoding="utf-8"
        )
        self.candidate_metadata.write_text(
            json.dumps(candidate, sort_keys=True) + "\n", encoding="utf-8"
        )

    def run_tool(
        self,
        *,
        optimized: bool = False,
        output: Path | None = None,
        extra: tuple[str, ...] = (),
    ) -> subprocess.CompletedProcess[str]:
        command = [sys.executable]
        if optimized:
            command.append("-O")
        command.extend(
            [
                str(TOOL),
                "--reference",
                str(self.reference_png),
                "--candidate",
                str(self.candidate_png),
                "--reference-metadata",
                str(self.reference_metadata),
                "--candidate-metadata",
                str(self.candidate_metadata),
                "--output",
                str(output if output is not None else self.output),
                *extra,
            ]
        )
        return subprocess.run(command, text=True, capture_output=True, check=False)

    def load_receipt(self, path: Path | None = None) -> dict:
        return json.loads((path if path is not None else self.output).read_text())

    def test_identical_frame_passes_and_binds_every_exact_input(self) -> None:
        result = self.run_tool()
        self.assertEqual(result.returncode, 0, result.stderr)
        receipt_bytes = self.output.read_bytes()
        receipt = json.loads(receipt_bytes)
        self.assertTrue(receipt["passed"])
        self.assertEqual(receipt["schema"], "ror.renderer_visual_parity_receipt.v1")
        self.assertEqual(
            receipt["comparison_semantics"],
            {
                "candidate_goal": "meet_or_exceed_reference_quality",
                "improvement_claimed_by_symmetric_metrics": False,
                "pixel_identity_required": False,
                "reference_role": "regression_floor",
                "symmetric_difference_budget": True,
            },
        )
        self.assertEqual(receipt["metrics"]["changed_pixel_count"], 0)
        self.assertEqual(receipt["metrics"]["linear_rgb"]["mean_absolute_error"], 0.0)
        self.assertEqual(receipt["metrics"]["linear_rgb"]["root_mean_square_error"], 0.0)
        self.assertEqual(receipt["metrics"]["luminance_global_ssim"], 1.0)
        self.assertEqual(receipt["metrics"]["sobel_edge_disagreement_mean"], 0.0)
        self.assertEqual(
            receipt["inputs"]["reference"]["png_sha256"],
            hashlib.sha256(self.reference_png.read_bytes()).hexdigest(),
        )
        self.assertEqual(
            receipt["inputs"]["candidate"]["metadata_sha256"],
            hashlib.sha256(self.candidate_metadata.read_bytes()).hexdigest(),
        )
        self.assertEqual(
            receipt["tool"]["source_sha256"],
            hashlib.sha256(TOOL.read_bytes()).hexdigest(),
        )
        self.assertEqual(receipt["dimensions"]["pixel_count"], self.width * self.height)
        self.assertTrue(receipt_bytes.endswith(b"\n"))
        self.assertEqual(
            receipt_bytes,
            (json.dumps(receipt, sort_keys=True, separators=(",", ":")) + "\n").encode(
                "ascii"
            ),
        )

    def test_small_bounded_change_passes_and_is_counted_exactly(self) -> None:
        candidate = bytearray(self.reference_pixels)
        candidate[3 * (5 * self.width + 7) + 1] ^= 1
        self.candidate_pixels = bytes(candidate)
        self.write_inputs()
        result = self.run_tool()
        self.assertEqual(result.returncode, 0, result.stderr)
        receipt = self.load_receipt()
        self.assertTrue(receipt["passed"])
        self.assertEqual(receipt["metrics"]["changed_pixel_count"], 1)
        self.assertGreater(receipt["metrics"]["linear_rgb"]["mean_absolute_error"], 0)

    def test_thresholds_compare_unrounded_metric_values(self) -> None:
        reference = bytearray(b"\x00\x00\x00" * (self.width * self.height))
        reference[-3:] = b"\x10\x10\x10"
        candidate = bytearray(reference)
        candidate[0] = 1
        self.write_inputs(
            reference_png=make_png(self.width, self.height, bytes(reference)),
            candidate_png=make_png(self.width, self.height, bytes(candidate)),
        )
        rounded_down_threshold = 1.31739e-7
        result = self.run_tool(
            extra=("--max-linear-mae", str(rounded_down_threshold),)
        )
        self.assertEqual(result.returncode, 1, result.stderr)
        receipt = self.load_receipt()
        self.assertGreater(
            receipt["metrics"]["linear_rgb"]["mean_absolute_error"],
            rounded_down_threshold,
        )
        self.assertFalse(receipt["threshold_checks"]["linear_rgb_mae"])

    def test_geometry_deletion_writes_a_fail_receipt_and_exits_nonzero(self) -> None:
        candidate = bytearray(self.reference_pixels)
        for y in range(4, 20):
            for x in range(8, 25):
                offset = 3 * (y * self.width + x)
                candidate[offset : offset + 3] = b"\x00\x00\x00"
        self.candidate_pixels = bytes(candidate)
        self.write_inputs()
        result = self.run_tool()
        self.assertEqual(result.returncode, 1, result.stderr)
        receipt = self.load_receipt()
        self.assertFalse(receipt["passed"])
        self.assertGreater(receipt["metrics"]["changed_pixel_count"], 200)
        self.assertFalse(all(receipt["threshold_checks"].values()))

    def test_sobel_extremum_is_normalized_to_one(self) -> None:
        # Difference signs [-1,-1,-1; -1,-1,+1; +1,+1,+1] attain
        # Gx=4, Gy=8 and therefore the exact joint magnitude sqrt(80).
        difference_signs = (-1, -1, -1, -1, -1, 1, 1, 1, 1)
        reference = bytearray()
        candidate = bytearray()
        for sign in difference_signs:
            reference.extend(b"\xff\xff\xff" if sign > 0 else b"\x00\x00\x00")
            candidate.extend(b"\x00\x00\x00" if sign > 0 else b"\xff\xff\xff")
        self.write_inputs(
            reference_png=make_png(3, 3, bytes(reference)),
            candidate_png=make_png(3, 3, bytes(candidate)),
            reference_metadata=metadata(3, 3, "ogre14", ZERO_SHA256),
            candidate_metadata=metadata(3, 3, "ogre-next", ONE_SHA256),
        )
        result = self.run_tool()
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(
            self.load_receipt()["metrics"]["sobel_edge_disagreement_mean"],
            1.0,
        )

    def test_all_png_filters_decode_to_the_same_pixels(self) -> None:
        self.write_inputs(
            candidate_png=make_png(
                self.width,
                self.height,
                self.candidate_pixels,
                filters=(0, 1, 2, 3, 4),
            )
        )
        result = self.run_tool()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.load_receipt()["metrics"]["changed_pixel_count"], 0)

    def test_dimension_mismatch_is_rejected_without_a_receipt(self) -> None:
        mismatched_width = self.width - 1
        mismatched_pixels = scene_pixels(mismatched_width, self.height)
        self.write_inputs(
            candidate_png=make_png(mismatched_width, self.height, mismatched_pixels)
        )
        result = self.run_tool()
        self.assertEqual(result.returncode, 2)
        self.assertIn("dimensions differ", result.stderr)
        self.assertFalse(self.output.exists())

    def test_malformed_crc_interlace_palette_ancillary_and_trailing_are_rejected(self) -> None:
        valid = make_png(self.width, self.height, self.candidate_pixels)
        bad_crc = bytearray(valid)
        idat = bad_crc.index(b"IDAT")
        bad_crc[idat + 5] ^= 1
        cases = {
            "signature": b"not-png" + valid[7:],
            "crc": bytes(bad_crc),
            "interlace": make_png(
                self.width, self.height, self.candidate_pixels, interlace=1
            ),
            "palette": make_png(
                self.width,
                self.height,
                bytes((x + y) & 0xFF for y in range(self.height) for x in range(self.width)),
                color_type=3,
            ),
            "ancillary": make_png(
                self.width,
                self.height,
                self.candidate_pixels,
                ancillary=((b"tEXt", b"ambiguous\x00metadata"),),
            ),
            "trailing": valid + b"trailing",
        }
        for name, candidate_png in cases.items():
            with self.subTest(name=name):
                self.write_inputs(candidate_png=candidate_png)
                result = self.run_tool()
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertFalse(self.output.exists())

    def test_mismatched_capture_fields_fail_closed_and_preserve_output(self) -> None:
        mutations = {
            "content": lambda value: value["content"].update(
                {"content_sha256": "3" * 64}
            ),
            "camera": lambda value: value["camera"].update(
                {"position": [11.0, 2.0, -4.0]}
            ),
            "exposure": lambda value: value["exposure"].update({"ev100": 12.0}),
            "weather": lambda value: value["weather"].update({"preset": "rain"}),
            "resolution": lambda value: value["resolution"].update(
                {"width": self.width - 1}
            ),
        }
        for field, mutate in mutations.items():
            with self.subTest(field=field):
                candidate = metadata(
                    self.width, self.height, "ogre-next", ONE_SHA256
                )
                mutate(candidate)
                self.write_inputs(candidate_metadata=candidate)
                sentinel = b"previous-valid-receipt\n"
                self.output.write_bytes(sentinel)
                result = self.run_tool()
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertIn(f"mismatch at {field}", result.stderr)
                self.assertEqual(self.output.read_bytes(), sentinel)
                self.assertEqual(list(self.root.glob(f".{self.output.name}.*.tmp")), [])

    def test_metadata_matching_is_json_type_sensitive(self) -> None:
        reference = metadata(self.width, self.height, "ogre14", ZERO_SHA256)
        candidate = metadata(self.width, self.height, "ogre-next", ONE_SHA256)
        reference["exposure"]["manual"] = True
        candidate["exposure"]["manual"] = 1
        self.write_inputs(
            reference_metadata=reference, candidate_metadata=candidate
        )
        result = self.run_tool()
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn("mismatch at exposure", result.stderr)
        self.assertFalse(self.output.exists())

    def test_invalid_input_rolls_back_existing_output(self) -> None:
        self.candidate_png.write_bytes(b"invalid")
        sentinel = b"previous-valid-receipt\n"
        self.output.write_bytes(sentinel)
        result = self.run_tool()
        self.assertEqual(result.returncode, 2)
        self.assertEqual(self.output.read_bytes(), sentinel)
        self.assertEqual(list(self.root.glob(f".{self.output.name}.*.tmp")), [])

    def test_output_hardlink_alias_cannot_replace_an_input(self) -> None:
        original = self.reference_png.read_bytes()
        alias = self.root / "hardlink-output.json"
        try:
            os.link(self.reference_png, alias)
        except OSError as exc:
            self.skipTest(f"hard links are unavailable: {exc}")
        result = self.run_tool(output=alias)
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn("must not replace an input", result.stderr)
        self.assertEqual(self.reference_png.read_bytes(), original)
        self.assertEqual(alias.read_bytes(), original)

    def test_output_through_symlinked_parent_cannot_replace_an_input(self) -> None:
        original = self.reference_png.read_bytes()
        alias_parent = self.root / "aliased-parent"
        try:
            alias_parent.symlink_to(self.root, target_is_directory=True)
        except OSError as exc:
            self.skipTest(f"directory symlinks are unavailable: {exc}")
        result = self.run_tool(output=alias_parent / self.reference_png.name)
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn("must not replace an input", result.stderr)
        self.assertEqual(self.reference_png.read_bytes(), original)

    def test_reference_candidate_hardlink_alias_is_rejected(self) -> None:
        self.candidate_png.unlink()
        try:
            os.link(self.reference_png, self.candidate_png)
        except OSError as exc:
            self.skipTest(f"hard links are unavailable: {exc}")
        result = self.run_tool()
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn("PNGs must be distinct filesystem objects", result.stderr)
        self.assertFalse(self.output.exists())

    def test_reference_candidate_metadata_symlink_alias_is_rejected(self) -> None:
        self.candidate_metadata.unlink()
        try:
            self.candidate_metadata.symlink_to(self.reference_metadata)
        except OSError as exc:
            self.skipTest(f"file symlinks are unavailable: {exc}")
        result = self.run_tool()
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn("metadata must be distinct filesystem objects", result.stderr)
        self.assertFalse(self.output.exists())

    def test_transparency_and_blank_frames_are_rejected(self) -> None:
        rgba = bytearray()
        for index in range(self.width * self.height):
            offset = index * 3
            rgba.extend(self.reference_pixels[offset : offset + 3])
            rgba.append(254 if index == 5 else 255)
        blank = bytes([16, 32, 48] * (self.width * self.height))
        cases = {
            "alpha": make_png(
                self.width, self.height, bytes(rgba), color_type=6
            ),
            "blank": make_png(self.width, self.height, blank),
        }
        for name, reference_png in cases.items():
            with self.subTest(name=name):
                self.write_inputs(reference_png=reference_png)
                result = self.run_tool()
                self.assertEqual(result.returncode, 2)
                self.assertIn(name, result.stderr.lower())
                self.assertFalse(self.output.exists())

    def test_nonfinite_duplicate_and_non_ui_free_metadata_are_rejected(self) -> None:
        cases = {
            "nonfinite": self.reference_metadata.read_text().replace("11.5", "NaN"),
            "surrogate": self.reference_metadata.read_text().replace(
                '"preset": "clear"', '"preset": "\\ud800"'
            ),
            "duplicate": self.reference_metadata.read_text().replace(
                '"ui_free": true', '"ui_free": true, "ui_free": true'
            ),
            "ui": self.reference_metadata.read_text().replace(
                '"ui_free": true', '"ui_free": false'
            ),
        }
        for name, raw_metadata in cases.items():
            with self.subTest(name=name):
                self.write_inputs()
                self.reference_metadata.write_text(raw_metadata, encoding="utf-8")
                result = self.run_tool()
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertFalse(self.output.exists())

    def test_normal_and_optimized_python_emit_identical_receipts(self) -> None:
        normal_output = self.root / "normal.json"
        optimized_output = self.root / "optimized.json"
        normal = self.run_tool(output=normal_output)
        optimized = self.run_tool(optimized=True, output=optimized_output)
        self.assertEqual(normal.returncode, 0, normal.stderr)
        self.assertEqual(optimized.returncode, 0, optimized.stderr)
        self.assertEqual(normal_output.read_bytes(), optimized_output.read_bytes())


if __name__ == "__main__":
    unittest.main()
