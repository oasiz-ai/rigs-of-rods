#!/usr/bin/env python3
"""Offline tests for the first real OGRE-Next raster-frame checkpoint."""

from __future__ import annotations

import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = REPOSITORY_ROOT / "tools" / "validate_ogre_next_frame_probe.py"
SOURCE_PATH = (
    REPOSITORY_ROOT / "tools" / "ogre_next_probe" / "src" / "frame_main.cpp"
)
CMAKE_PATH = REPOSITORY_ROOT / "tools" / "ogre_next_probe" / "CMakeLists.txt"
RUNNER_PATH = REPOSITORY_ROOT / "tools" / "run_ogre_next_probe.py"

SPEC = importlib.util.spec_from_file_location(
    "validate_ogre_next_frame_probe", TOOL_PATH
)
assert SPEC and SPEC.loader
FRAME = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(FRAME)

RUNNER_SPEC = importlib.util.spec_from_file_location(
    "run_ogre_next_probe_for_frame_tests", RUNNER_PATH
)
assert RUNNER_SPEC and RUNNER_SPEC.loader
RUNNER = importlib.util.module_from_spec(RUNNER_SPEC)
RUNNER_SPEC.loader.exec_module(RUNNER)


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
        pixels = [background] * (FRAME.WIDTH * FRAME.HEIGHT)
        positions = [
            (x, y)
            for y in range(40, 88)
            for x in range(71, 121)
            if (x + y) % 4 == 0
        ]
        self_count = len(positions)
        if self_count != 600:
            raise AssertionError(f"fixture must contain 600 foreground pixels, got {self_count}")
        for index, (x, y) in enumerate(positions):
            pixels[y * FRAME.WIDTH + x] = colours[index % len(colours)]
        return bytes(component for colour in pixels for component in colour)

    @staticmethod
    def make_report(pixels: bytes) -> dict:
        observed = FRAME.inspect_pixels(pixels)
        return {
            "schema_version": 2,
            "status": "pass",
            "provenance": {
                "ogre_next_commit": "1" * 40,
                "ogre_next_archive_sha256": "2" * 64,
            },
            "build": {
                "ogre_version": "3.0.0",
                "abi_cookie": "3" * 32,
            },
            "platform_policy": "macos-arm64-metal",
            "renderer": "Metal Rendering Subsystem",
            "device_name": "Reviewed GPU",
            "surface_mode": "macos-hidden-native",
            "frame": {
                "width": FRAME.WIDTH,
                "height": FRAME.HEIGHT,
                "warmup_frames": FRAME.WARMUP_FRAMES,
                "pixel_format": FRAME.PIXEL_FORMAT,
                "ui_included": False,
                "hlms_pbs_geometry": True,
                "compositor2": True,
                "gpu_readback": True,
                "distinct_rgb8_values": observed["distinct_rgb8_values"],
                "non_background_pixels": observed["non_background_pixels"],
                "minimum_luminance": observed["minimum_luminance"],
                "maximum_luminance": observed["maximum_luminance"],
                "rgb8_fnv1a64": observed["rgb8_fnv1a64"],
            },
            "lifecycle": {"renderer_shutdown_completed": True},
            "native_ray_tracing": "not_evaluated",
        }

    @staticmethod
    def make_capability_report(frame_report: dict) -> dict:
        return {
            "status": "pass",
            "provenance": {
                "commit": frame_report["provenance"]["ogre_next_commit"],
                "archive_sha256": frame_report["provenance"]
                ["ogre_next_archive_sha256"],
            },
            "build": {
                "ogre_version": frame_report["build"]["ogre_version"],
                "abi_cookie": frame_report["build"]["abi_cookie"],
                "platform_policy": frame_report["platform_policy"],
            },
            "capabilities": {
                "renderer": {"name": frame_report["renderer"]}
            },
        }

    def test_valid_frame_and_report_pass(self) -> None:
        pixels = self.make_pixels()
        report = self.make_report(pixels)
        observed = FRAME.validate(
            report,
            pixels,
            "macos-arm64-metal",
            self.make_capability_report(report),
        )
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
            lambda value: value["frame"].update(minimum_luminance=0.1),
            lambda value: value.update(renderer="OpenGL Rendering Subsystem"),
            lambda value: value.update(device_name="(default)"),
            lambda value: value["lifecycle"].update(
                renderer_shutdown_completed=False
            ),
            lambda value: value["provenance"].update(
                ogre_next_archive_sha256="4" * 64
            ),
        )
        for mutate in mutations:
            with self.subTest(mutate=mutate):
                invalid = copy.deepcopy(report)
                mutate(invalid)
                with self.assertRaises(FRAME.FrameValidationError):
                    FRAME.validate(
                        invalid,
                        pixels,
                        "macos-arm64-metal",
                        self.make_capability_report(report),
                    )

    def test_policy_matrix_is_explicit(self) -> None:
        pixels = self.make_pixels()
        for policy, renderer in FRAME.RENDERERS.items():
            with self.subTest(policy=policy):
                report = self.make_report(pixels)
                report["platform_policy"] = policy
                report["renderer"] = renderer
                report["surface_mode"] = FRAME.SURFACE_MODES[policy]
                capability_report = self.make_capability_report(report)
                FRAME.validate(report, pixels, policy, capability_report)
        with self.assertRaises(FRAME.FrameValidationError):
            report = self.make_report(pixels)
            FRAME.validate(
                report,
                pixels,
                "freebsd-vulkan",
                self.make_capability_report(report),
            )

    def test_spatial_geometry_contract_rejects_noise_and_missing_center(self) -> None:
        pixels = bytearray(self.make_pixels())
        report = self.make_report(bytes(pixels))
        capability_report = self.make_capability_report(report)
        center_offset = ((FRAME.HEIGHT // 2) * FRAME.WIDTH + FRAME.WIDTH // 2) * 3
        pixels[center_offset : center_offset + 3] = bytes((0, 0, 0))
        mutated = bytes(pixels)
        mutated_report = self.make_report(mutated)
        with self.assertRaises(FRAME.FrameValidationError):
            FRAME.validate(
                mutated_report,
                mutated,
                "macos-arm64-metal",
                self.make_capability_report(mutated_report),
            )

        corner_pixels = bytearray(self.make_pixels())
        corner_pixels[:3] = bytes((255, 0, 255))
        corner_report = self.make_report(bytes(corner_pixels))
        with self.assertRaises(FRAME.FrameValidationError):
            FRAME.validate(
                corner_report,
                bytes(corner_pixels),
                "macos-arm64-metal",
                self.make_capability_report(corner_report),
            )

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
            "getDeviceName",
            "formatAbiCookie",
            "renderer_shutdown_completed",
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
        self.assertIn("ror_ogre_next_frame_probe_report", cmake)
        self.assertIn("ror_ogre_next_frame_probe_runtime", cmake)
        self.assertNotIn(
            "ogre_next_frame_probe",
            (REPOSITORY_ROOT / "CMakeLists.txt").read_text(encoding="utf-8"),
        )

    def test_hardened_runner_makes_real_frame_validation_mandatory(self) -> None:
        runner = RUNNER_PATH.read_text(encoding="utf-8")
        capability_validation = runner.index("validate_report(report, lock, policy)")
        frame_checkpoint = runner.index(
            "run_frame_checkpoint(", capability_validation
        )
        final_report = runner.index(
            "print(json.dumps(report", frame_checkpoint
        )
        self.assertLess(capability_validation, frame_checkpoint)
        self.assertLess(frame_checkpoint, final_report)
        self.assertIn('"ror_ogre_next_frame_probe_report"', runner)
        self.assertIn('str(FRAME_VALIDATOR)', runner)
        self.assertIn('"--capability-report"', runner)

    def test_frame_orchestration_fails_closed_before_final_success(self) -> None:
        policy = {"name": "macos-arm64-metal"}
        with tempfile.TemporaryDirectory(prefix="ror-ogre-next-orchestration-") as temp:
            build_dir = Path(temp)
            capability_report = build_dir / RUNNER.REPORT_NAME

            with mock.patch.object(
                RUNNER, "run", side_effect=RUNNER.ProbeError("build failed")
            ) as mocked_run:
                with self.assertRaises(RUNNER.ProbeError):
                    RUNNER.run_frame_checkpoint(
                        build_dir, "Release", 2, policy, capability_report
                    )
                self.assertEqual(mocked_run.call_count, 1)

            with mock.patch.object(RUNNER, "run") as mocked_run:
                with self.assertRaisesRegex(
                    RUNNER.ProbeError, "did not produce required artifacts"
                ):
                    RUNNER.run_frame_checkpoint(
                        build_dir, "Release", 2, policy, capability_report
                    )
                self.assertEqual(mocked_run.call_count, 1)

            capability_report.write_text("{}", encoding="utf-8")
            (build_dir / RUNNER.FRAME_REPORT_NAME).write_text(
                "{}", encoding="utf-8"
            )
            (build_dir / RUNNER.FRAME_IMAGE_NAME).write_bytes(b"invalid")
            with mock.patch.object(
                RUNNER,
                "run",
                side_effect=[None, RUNNER.ProbeError("validator failed")],
            ) as mocked_run:
                with self.assertRaisesRegex(RUNNER.ProbeError, "validator failed"):
                    RUNNER.run_frame_checkpoint(
                        build_dir, "Release", 2, policy, capability_report
                    )
                self.assertEqual(mocked_run.call_count, 2)

    def test_fixture_report_is_json_serializable(self) -> None:
        report = self.make_report(self.make_pixels())
        self.assertEqual(json.loads(json.dumps(report)), report)


if __name__ == "__main__":
    unittest.main()
