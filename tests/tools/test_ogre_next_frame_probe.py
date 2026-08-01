#!/usr/bin/env python3
"""Offline tests for the first real OGRE-Next raster-frame checkpoint."""

from __future__ import annotations

import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = REPOSITORY_ROOT / "tools" / "validate_ogre_next_frame_probe.py"
SOURCE_PATH = (
    REPOSITORY_ROOT / "tools" / "ogre_next_probe" / "src" / "frame_main.cpp"
)
CMAKE_PATH = REPOSITORY_ROOT / "tools" / "ogre_next_probe" / "CMakeLists.txt"

SPEC = importlib.util.spec_from_file_location(
    "validate_ogre_next_frame_probe", TOOL_PATH
)
assert SPEC and SPEC.loader
FRAME = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(FRAME)


class OgreNextFrameProbeTests(unittest.TestCase):
    @staticmethod
    def make_pixels(background: tuple[int, int, int] = (0, 0, 0)) -> bytes:
        colours = (
            (255, 0, 0),
            (0, 255, 0),
            (0, 0, 255),
            (255, 255, 255),
            (128, 64, 32),
            (32, 128, 64),
            (64, 32, 128),
            (200, 200, 20),
        )
        foreground = [colours[index % len(colours)] for index in range(600)]
        pixels = [background] * (FRAME.WIDTH * FRAME.HEIGHT - len(foreground))
        pixels.extend(foreground)
        return bytes(component for colour in pixels for component in colour)

    @staticmethod
    def make_report(pixels: bytes) -> dict:
        observed = FRAME.inspect_pixels(pixels)
        return {
            "schema_version": 1,
            "status": "pass",
            "platform_policy": "macos-arm64-metal",
            "renderer": "Metal Rendering Subsystem",
            "frame": {
                "width": FRAME.WIDTH,
                "height": FRAME.HEIGHT,
                "warmup_frames": FRAME.WARMUP_FRAMES,
                "pixel_format": FRAME.PIXEL_FORMAT,
                "ui_included": False,
                "hlms_pbs_geometry": True,
                "compositor2": True,
                "gpu_readback": True,
                **observed,
            },
            "native_ray_tracing": "not_evaluated",
        }

    def test_valid_frame_and_report_pass(self) -> None:
        pixels = self.make_pixels()
        report = self.make_report(pixels)
        observed = FRAME.validate(report, pixels, "macos-arm64-metal")
        self.assertEqual(observed["non_background_pixels"], 600)

    def test_background_is_modal_colour_not_lexicographic_minimum(self) -> None:
        pixels = self.make_pixels((240, 240, 240))
        observed = FRAME.inspect_pixels(pixels)
        self.assertEqual(observed["non_background_pixels"], 600)

    def test_capability_claims_and_artifact_agreement_fail_closed(self) -> None:
        pixels = self.make_pixels()
        report = self.make_report(pixels)
        mutations = (
            lambda value: value.update(native_ray_tracing="supported"),
            lambda value: value["frame"].update(ui_included=True),
            lambda value: value["frame"].update(hlms_pbs_geometry=False),
            lambda value: value["frame"].update(compositor2=False),
            lambda value: value["frame"].update(gpu_readback=False),
            lambda value: value["frame"].update(distinct_rgb8_values=7),
            lambda value: value["frame"].update(non_background_pixels=511),
            lambda value: value["frame"].update(rgb8_fnv1a64="0" * 16),
            lambda value: value.update(renderer="OpenGL Rendering Subsystem"),
        )
        for mutate in mutations:
            with self.subTest(mutate=mutate):
                invalid = copy.deepcopy(report)
                mutate(invalid)
                with self.assertRaises(FRAME.FrameValidationError):
                    FRAME.validate(invalid, pixels, "macos-arm64-metal")

    def test_policy_matrix_is_explicit(self) -> None:
        pixels = self.make_pixels()
        for policy, renderer in FRAME.RENDERERS.items():
            with self.subTest(policy=policy):
                report = self.make_report(pixels)
                report["platform_policy"] = policy
                report["renderer"] = renderer
                FRAME.validate(report, pixels, policy)
        with self.assertRaises(FRAME.FrameValidationError):
            FRAME.validate(self.make_report(pixels), pixels, "freebsd-vulkan")

    def test_ppm_reader_requires_exact_rgb8_payload(self) -> None:
        pixels = self.make_pixels()
        header = f"P6\n{FRAME.WIDTH} {FRAME.HEIGHT}\n255\n".encode("ascii")
        with tempfile.TemporaryDirectory(prefix="ror-ogre-next-frame-") as temp:
            path = Path(temp) / "frame.ppm"
            path.write_bytes(header + pixels)
            self.assertEqual(FRAME.read_ppm(path), pixels)
            path.write_bytes(b"P3\n" + pixels)
            with self.assertRaises(FRAME.FrameValidationError):
                FRAME.read_ppm(path)
            path.write_bytes(header + pixels[:-1])
            with self.assertRaises(FRAME.FrameValidationError):
                FRAME.read_ppm(path)

    def test_checked_in_source_executes_real_raster_path(self) -> None:
        source = SOURCE_PATH.read_text(encoding="utf-8")
        for required in (
            "Ogre::HlmsPbs",
            "Ogre::HlmsPbsDatablock",
            "Ogre::ManualObject",
            "createBasicWorkspaceDef",
            "addWorkspace",
            "Ogre::PFG_RGBA8_UNORM",
            "root.renderOneFrame()",
            "image.convertFromTexture",
            '\\"ui_included\\": false',
            '\\"native_ray_tracing\\": \\"not_evaluated\\"',
        ):
            self.assertIn(required, source)
        self.assertNotIn(
            '\\"native_ray_tracing\\": \\"supported\\"', source
        )

    def test_cmake_keeps_frame_probe_isolated_and_runnable(self) -> None:
        cmake = CMAKE_PATH.read_text(encoding="utf-8")
        self.assertIn("add_executable(ror_ogre_next_frame_probe", cmake)
        self.assertIn("OgreNextHlmsPbs", cmake)
        self.assertIn("${ROR_OGRE_NEXT_RENDERER_TARGET}", cmake)
        self.assertIn("ror_ogre_next_frame_probe_runtime", cmake)
        self.assertNotIn(
            "ogre_next_frame_probe",
            (REPOSITORY_ROOT / "CMakeLists.txt").read_text(encoding="utf-8"),
        )

    def test_fixture_report_is_json_serializable(self) -> None:
        report = self.make_report(self.make_pixels())
        self.assertEqual(json.loads(json.dumps(report)), report)


if __name__ == "__main__":
    unittest.main()
