#!/usr/bin/env python3
"""Offline fail-closed tests for the Metal N3 hybrid HDR checkpoint."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
RUNNER_PATH = REPOSITORY_ROOT / "tools" / "run_ogre_next_probe.py"
SPEC = importlib.util.spec_from_file_location("run_ogre_next_probe_n3", RUNNER_PATH)
assert SPEC is not None and SPEC.loader is not None
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)


class MetalN3ContractTests(unittest.TestCase):
    source_commit = "a" * 40
    source_ref = "codex/test-metal-n3"
    source_manifest = "b" * 64

    @classmethod
    def setUpClass(cls) -> None:
        render_root = REPOSITORY_ROOT / "source" / "main" / "gfx" / "render"
        ogre_root = render_root / "ogrenext"
        cls.contract = (render_root / "RendererFrontend.h").read_text(
            encoding="utf-8"
        )
        cls.frontend = (ogre_root / "OgreNextN1Frontend.cpp").read_text(
            encoding="utf-8"
        )
        cls.backend = (ogre_root / "OgreNextMetalRayTracingBackend.mm").read_text(
            encoding="utf-8"
        )
        cls.smoke = (
            REPOSITORY_ROOT / "tools" / "ogre_next_probe" / "src" / "metal_n3_smoke.cpp"
        ).read_text(encoding="utf-8")
        cls.cmake = (
            REPOSITORY_ROOT / "tools" / "ogre_next_probe" / "CMakeLists.txt"
        ).read_text(encoding="utf-8")
        cls.run_n3 = (
            REPOSITORY_ROOT
            / "tools"
            / "ogre_next_probe"
            / "cmake"
            / "RunN3Smoke.cmake"
        ).read_text(encoding="utf-8")

    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.executable = self.root / "ror_ogre_next_metal_n3_smoke"
        self.executable.write_bytes(b"reviewed-metal-n3-executable")
        self.raster_path = self.root / RUNNER.N3_RASTER_NAME
        self.contribution_path = self.root / RUNNER.N3_CONTRIBUTION_NAME
        self.hybrid_path = self.root / RUNNER.N3_HYBRID_NAME
        self.width = 96
        self.height = 64
        pixel_count = self.width * self.height
        raster_pixel = struct.pack("<4e", 0.125, 0.0625, 0.03125, 1.0)
        contribution_zero = struct.pack("<4e", 0.0, 0.0, 0.0, 0.0)
        contribution_hit = struct.pack("<4e", 0.25, 0.125, 0.0625, 0.0)
        hybrid_hit = struct.pack("<4e", 0.375, 0.1875, 0.09375, 1.0)
        self.raster = raster_pixel * pixel_count
        self.contribution = contribution_hit + contribution_zero * (pixel_count - 1)
        self.hybrid = hybrid_hit + raster_pixel * (pixel_count - 1)
        self.raster_path.write_bytes(self.raster)
        self.contribution_path.write_bytes(self.contribution)
        self.hybrid_path.write_bytes(self.hybrid)
        self.lock = RUNNER.load_lock()
        raster_metrics = RUNNER._n3_image_metrics(
            self.raster, self.width, self.height
        )
        contribution_metrics = RUNNER._n3_image_metrics(
            self.contribution, self.width, self.height
        )
        hybrid_metrics = RUNNER._n3_image_metrics(
            self.hybrid, self.width, self.height
        )
        for metrics in (raster_metrics, contribution_metrics, hybrid_metrics):
            metrics["format"] = "RGBA16_FLOAT"
        self.report = {
            "schema": "ror.ogre_next_metal_rt_n3.v2",
            "status": "pass",
            "scope": (
                "same-device Metal primary-ray hit contribution composited into "
                "exact UI-free Ogre-Next HDR target; no GI, reflection, denoising, "
                "multi-bounce, or material parity claim"
            ),
            "provenance": {
                "ror_repository": RUNNER.ROR_SOURCE_REPOSITORY,
                "ror_ref": self.source_ref,
                "ror_commit": self.source_commit,
                "relevant_source_clean": True,
                "relevant_source_manifest_sha256": self.source_manifest,
                "ogre_next_commit": self.lock["commit"],
                "build_artifact": self.executable.name,
                "build_artifact_bytes": self.executable.stat().st_size,
                "build_artifact_sha256": RUNNER.sha256_file(self.executable),
            },
            "device": {
                "name": "Apple test GPU",
                "same_ogre_device": True,
                "same_ogre_queue": True,
                "apple_family_9": True,
            },
            "contract": {
                "image_version": 2,
                "image_generation": 1,
                "usage": "COLOR_ATTACHMENT_SHADER_READ_WRITE_COPY_SOURCE",
                "release_state": "GENERAL_READ_WRITE",
                "return_state": "GENERAL_READ_WRITE",
            },
            "raster_only_hdr": raster_metrics,
            "rt_contribution": contribution_metrics,
            "hybrid_hdr": hybrid_metrics,
            "second_view_contribution": {
                **contribution_metrics,
                "sha256": "c" * 64,
            },
            "resized_hybrid": {
                "width": 80,
                "height": 48,
                "format": "RGBA16_FLOAT",
                "bytes": 80 * 48 * 8,
                "sha256": "d" * 64,
                "mean_luminance": 0.1,
                "nontrivial_pixels": 1,
            },
            "proof": {
                "exact_exported_vertex_slice_used": True,
                "exact_exported_index_slice_used": True,
                "exact_exported_color_image_used": True,
                "gpu_composite_not_cpu_postprocess": True,
                "contribution_pixels": 1,
                "hybrid_changes_only_on_contribution": True,
                "all_channels_finite": True,
                "second_camera_changes_contribution_hash": True,
                "camera_mismatch_rejected": True,
                "snapshot_transform_mismatch_rejected": True,
                "off_axis_far_plane_contribution_pixels": 1,
                "off_axis_far_plane_hit_passed": True,
                "released_frame_allows_extent_change": True,
                "submitted_device_loss_and_timeout_paths_tested": True,
                "view_dependent_output_ready": True,
                "hybrid_composite_ready": True,
            },
        }

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def validate(self, report: dict[str, object]) -> None:
        RUNNER.validate_n3_checkpoint(
            report,
            self.raster_path,
            self.contribution_path,
            self.hybrid_path,
            self.executable,
            self.lock,
            {"name": "macos-arm64-metal"},
            self.source_commit,
            self.source_ref,
            self.source_manifest,
        )

    def test_valid_independent_artifacts_pass(self) -> None:
        self.validate(self.report)

    def test_hybrid_change_without_contribution_fails(self) -> None:
        tampered = bytearray(self.hybrid)
        tampered[8:10] = struct.pack("<e", 0.5)
        self.hybrid_path.write_bytes(tampered)
        report = copy.deepcopy(self.report)
        metrics = RUNNER._n3_image_metrics(bytes(tampered), self.width, self.height)
        metrics["format"] = "RGBA16_FLOAT"
        report["hybrid_hdr"] = metrics
        with self.assertRaises(RUNNER.ProbeError):
            self.validate(report)

    def test_hybrid_change_must_equal_reported_contribution(self) -> None:
        tampered = bytearray(self.hybrid)
        tampered[0:2] = struct.pack("<e", 0.5)
        self.hybrid_path.write_bytes(tampered)
        report = copy.deepcopy(self.report)
        metrics = RUNNER._n3_image_metrics(bytes(tampered), self.width, self.height)
        metrics["format"] = "RGBA16_FLOAT"
        report["hybrid_hdr"] = metrics
        with self.assertRaises(RUNNER.ProbeError):
            self.validate(report)

    def test_nonfinite_contribution_fails(self) -> None:
        tampered = bytearray(self.contribution)
        tampered[0:2] = struct.pack("<H", 0x7E00)
        self.contribution_path.write_bytes(tampered)
        with self.assertRaises(RUNNER.ProbeError):
            self.validate(self.report)

    def test_reported_hash_cannot_be_fabricated(self) -> None:
        report = copy.deepcopy(self.report)
        report["hybrid_hdr"]["sha256"] = hashlib.sha256(b"fabricated").hexdigest()
        with self.assertRaises(RUNNER.ProbeError):
            self.validate(report)

    def test_source_identity_mismatch_fails(self) -> None:
        report = copy.deepcopy(self.report)
        report["provenance"]["ror_commit"] = "e" * 40
        with self.assertRaises(RUNNER.ProbeError):
            self.validate(report)

    def test_capability_skip_is_attested_without_image_artifacts(self) -> None:
        report = {
            "schema": "ror.ogre_next_metal_rt_n3.v2",
            "status": "skip",
            "scope": "same-device Metal primary-ray hybrid HDR contribution",
            "reason": "test device is below Apple family 9",
            "provenance": copy.deepcopy(self.report["provenance"]),
        }
        RUNNER.validate_n3_checkpoint(
            report,
            None,
            None,
            None,
            self.executable,
            self.lock,
            {"name": "macos-arm64-metal"},
            self.source_commit,
            self.source_ref,
            self.source_manifest,
        )

    def test_renderer_neutral_image_contract_has_no_metal_types(self) -> None:
        for token in (
            "kNativeImageInteropContractVersion",
            "NativeImageExportRequest",
            "NativeImageExport",
            "NativeImageUsage",
            "NativeImageState",
            "AcquireImage",
            "ReleaseImage",
        ):
            self.assertIn(token, self.contract)
        self.assertNotIn("#import <Metal/", self.contract)
        self.assertNotIn("id<MTL", self.contract)

    def test_n3_uses_native_camera_rays_and_gpu_target_composite(self) -> None:
        for token in (
            "instance_acceleration_structure scene",
            "parameters.render_from_clip * float4(ndc, 0.0f, 1.0f)",
            "texture2d<half, access::read_write> hybrid",
            "texture2d<half, access::write> contribution",
            "contribution.write(traced_half, pixel)",
            "hybrid.write(half4(half3(composed), raster.a), pixel)",
            "[compute_encoder dispatchThreads:image_size",
            "[command_buffer commit]",
            "ValidateHybridReadbacks",
            "primary.max_distance = length(segment)",
        ):
            self.assertIn(token, self.backend)
        self.assertIn("Ogre::TextureFlags::RenderToTexture", self.frontend)
        self.assertIn("target_flags |= Ogre::TextureFlags::Uav", self.frontend)
        self.assertNotIn(
            "min(length(segment), parameters.maximum_distance)", self.backend
        )

    def test_n3_smoke_proves_view_resize_and_submitted_faults(self) -> None:
        for token in (
            "second_camera_changes_contribution_hash",
            "released_frame_allows_extent_change",
            "submitted_device_loss_and_timeout_paths_tested",
            "gpu_composite_not_cpu_postprocess",
            "ProveInjectedObservation",
            "VerifyContributionMapping",
            "camera_mismatch_rejected",
            "snapshot_transform_mismatch_rejected",
            "off_axis_far_plane_hit_passed",
            "OffAxisFarPlaneTransform",
        ):
            self.assertIn(token, self.smoke)

    def test_cmake_accepts_only_explicit_capability_skip(self) -> None:
        self.assertIn("RunN3Smoke.cmake", self.cmake)
        self.assertIn("_ror_n3_result EQUAL 77", self.run_n3)
        self.assertIn("Metal N3 smoke failed with exit code", self.run_n3)
        self.assertIn("ror_ogre_next_metal_n3_runtime", self.cmake)
        self.assertIn("SKIP_RETURN_CODE 77", self.cmake)

    def test_parser_exposes_independent_n3_checkpoint(self) -> None:
        self.assertEqual(
            RUNNER.build_parser().parse_args(["--checkpoint", "n3"]).checkpoint,
            "n3",
        )


if __name__ == "__main__":
    unittest.main()
