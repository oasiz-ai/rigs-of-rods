#!/usr/bin/env python3
"""Offline fail-closed checks for the isolated Ogre-Next N1 frontend."""

from __future__ import annotations

import copy
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest import mock


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
RENDER_ROOT = REPOSITORY_ROOT / "source" / "main" / "gfx" / "render"
PROBE_ROOT = REPOSITORY_ROOT / "tools" / "ogre_next_probe"
RUNNER_PATH = REPOSITORY_ROOT / "tools" / "run_ogre_next_probe.py"
RUNNER_SPEC = importlib.util.spec_from_file_location(
    "run_ogre_next_probe_for_n1_tests", RUNNER_PATH
)
assert RUNNER_SPEC and RUNNER_SPEC.loader
RUNNER = importlib.util.module_from_spec(RUNNER_SPEC)
RUNNER_SPEC.loader.exec_module(RUNNER)


class OgreNextN1FrontendContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.entry_cmake = (PROBE_ROOT / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        cls.pinned_cmake = (
            PROBE_ROOT / "cmake" / "PinnedOgreNext.cmake"
        ).read_text(encoding="utf-8")
        cls.header = (
            RENDER_ROOT / "ogrenext" / "OgreNextN1Frontend.h"
        ).read_text(encoding="utf-8")
        cls.frontend = (
            RENDER_ROOT / "ogrenext" / "OgreNextN1Frontend.cpp"
        ).read_text(encoding="utf-8")
        cls.policy = (
            RENDER_ROOT / "ogrenext" / "OgreNextN1Policy.cpp"
        ).read_text(encoding="utf-8")
        cls.policy_header = (
            RENDER_ROOT / "ogrenext" / "OgreNextN1Policy.h"
        ).read_text(encoding="utf-8")
        cls.smoke = (
            PROBE_ROOT / "src" / "frontend_n1_smoke.cpp"
        ).read_text(encoding="utf-8")

    def test_dependency_policy_is_shared_pinned_and_isolated(self) -> None:
        self.assertIn("cmake/PinnedOgreNext.cmake", self.entry_cmake)
        self.assertIn("37149a802de747f6806996fa3067b0748ecc1084", self.pinned_cmake)
        self.assertIn("URL_HASH \"SHA256=${ROR_OGRE_NEXT_ARCHIVE_SHA256}\"", self.pinned_cmake)
        self.assertIn("if (TARGET OgreMain)", self.pinned_cmake)
        self.assertIn("OgreNextMain", self.entry_cmake)
        self.assertNotIn("add_subdirectory(tools/ogre_next_probe", (
            REPOSITORY_ROOT / "CMakeLists.txt"
        ).read_text(encoding="utf-8"))

    def test_public_boundary_contains_no_ogre_types(self) -> None:
        self.assertNotIn('#include "Ogre', self.header)
        self.assertNotIn("Ogre::", self.header)
        self.assertIn("std::unique_ptr<Impl>", self.header)

    def test_native_mesh_path_uses_v2_vao_not_manual_object(self) -> None:
        for token in (
            "createVertexBuffer(",
            "createIndexBuffer(",
            "createVertexArrayObject(",
            "Ogre::MeshManager::getSingleton().createManual(",
            "Ogre::SubMesh *submesh",
        ):
            self.assertIn(token, self.frontend)
        self.assertNotIn("Ogre::ManualObject", self.frontend)
        self.assertLess(
            self.frontend.index("destroyVertexArrayObject(vao)"),
            self.frontend.index("destroyVertexBuffer(vertex_buffer)"),
        )

    def test_catalog_sync_is_zero_copy_transactional_and_raii_owned(self) -> None:
        self.assertIn("VisitRecords", self.policy)
        self.assertIn("VisitRecords", self.frontend)
        self.assertNotIn("BuildFullSnapshot", self.policy)
        self.assertNotIn("BuildFullSnapshot", self.frontend)
        for token in (
            "PendingMeshAllocation",
            "PendingMaterialAllocation",
            "RollbackCandidateAllocations",
            "impl_->meshes.swap(candidate_meshes)",
            "impl_->materials.swap(candidate_materials)",
            "impl_->registry.swap(candidate)",
        ):
            self.assertIn(token, self.frontend)

    def test_n1_capability_and_scene_policy_fail_closed(self) -> None:
        for token in (
            "report.supported_outputs = FrameOutputMask::COLOR",
            "report.native_api = NativeGraphicsApi::NONE",
            "snapshot.lights().empty()",
            "no calibrated physical-light adapter",
            "N1 does not support deformable geometry",
            "N1 does not support particles",
            "N1 materials must be completely texture free",
            "N1 renders exactly one colour view",
        ):
            self.assertIn(token, self.policy)
        self.assertIn("kOgreNextN1MaximumDirectionalLights = 0U", self.policy_header)

    def test_pbr_mapping_uses_reviewed_brdf_and_live_getter_gate(self) -> None:
        self.assertNotIn("importUnity", self.frontend)
        for token in (
            "setBrdf(Ogre::PbsBrdf::Default)",
            "Ogre::HlmsPbsDatablock::MetallicWorkflow",
            "setDiffuse(",
            "setMetalness(",
            "setRoughness(",
            "setEmissive(",
            "VerifyPbsMapping(*native.datablock, descriptor)",
            "datablock.getBrdf() != Ogre::PbsBrdf::Default",
            "datablock.getWorkflow() != Ogre::HlmsPbsDatablock::MetallicWorkflow",
            "datablock.getDiffuse()",
            "datablock.getMetalness()",
            "datablock.getRoughness()",
            "datablock.getEmissive()",
        ):
            self.assertIn(token, self.frontend)

    def test_submission_and_cleanup_state_are_bounded_and_fault_latched(self) -> None:
        self.assertIn("kOgreNextN1CompletedFrameHistoryLimit = 64U", self.policy_header)
        self.assertIn("completed_frames_.pop_front()", self.policy)
        self.assertIn("replay of only the latest snapshot", self.policy)
        self.assertIn("impl_->faulted = true", self.frontend)
        self.assertIn("return FrameCleanupFailure()", self.frontend)
        self.assertIn("fail_after_cleanup", self.frontend)
        self.assertIn("[[nodiscard]] bool DestroyCatalog()", self.frontend)
        self.assertIn("[[nodiscard]] bool CleanupBackend()", self.frontend)
        self.assertIn("if (!impl_->CleanupBackend())", self.frontend)
        self.assertNotIn("seen_snapshots", self.frontend)

    def test_projection_and_device_extent_paths_fail_closed(self) -> None:
        self.assertIn("ConvertPortableProjectionToOgreClip", self.policy_header)
        self.assertIn("2.0F * portable.elements[row_two]", self.policy)
        self.assertIn(
            "kOgreNextN1ConservativeMaximumTextureDimension = 2048U",
            self.policy_header,
        )
        self.assertIn("getMaximumResolution2D()", self.frontend)
        self.assertIn(
            "initial extent exceeds the initialized Ogre-Next device limit",
            self.frontend,
        )
        self.assertIn(
            "ToOgreMatrix(ConvertPortableProjectionToOgreClip(view.clip_from_view))",
            self.frontend,
        )
        self.assertEqual(self.frontend.count("createRenderWindow("), 1)

    def test_real_smoke_covers_hdr_sdr_readback_and_recovery(self) -> None:
        for token in (
            "PixelFormat::RGBA16_FLOAT",
            "PixelFormat::RGBA8_SRGB",
            "HalfToFloat",
            "maximum_luminance <= 1.05F",
            "unsupported depth request",
            "post-reinitialize Render",
            "Ogre v2 Mesh plus immutable VertexArrayObject",
            "PbsBrdf::Default height-correlated GGX",
            "pbr_datablock_readback_verified",
        ):
            self.assertIn(token, self.smoke)
        self.assertIn("ror_ogre_next_frontend_n1_runtime", self.entry_cmake)
        self.assertIn("ror_ogre_next_frontend_n1_report", self.entry_cmake)

    def test_wrapper_validator_checks_exact_pixels_hdr_and_lifecycle(self) -> None:
        lock = RUNNER.load_lock()
        policy = RUNNER.detect_policy("Darwin", "arm64")
        pixels = bytearray(192 * 128 * 3)
        for pixel in range(600):
            offset = (4000 + pixel) * 3
            pixels[offset : offset + 3] = bytes((220, 90, 30))
        hash_value = 14695981039346656037
        for value in pixels:
            hash_value ^= value
            hash_value = (hash_value * 1099511628211) & ((1 << 64) - 1)
        report = {
            "schema": "ror.ogre_next_frontend_n1_smoke.v1",
            "status": "pass",
            "provenance": {
                "ogre_next_commit": lock["commit"],
                "ogre_next_archive_sha256": lock["archive_sha256"],
                "shader_media_root": lock["shader_media"]["root"],
                "shader_media_license_expression": lock["shader_media"][
                    "license_expression"
                ],
                "shader_media_notice_sha256": lock["shader_media"][
                    "third_party_notice"
                ]["notice_sha256"],
            },
            "platform_policy": policy["name"],
            "renderer": policy["renderer_name"],
            "adapter": {
                "native_mesh_path": (
                    "Ogre v2 Mesh plus immutable VertexArrayObject"
                ),
                "material_path": "HLMS PBS metallic-roughness",
                "brdf": "PbsBrdf::Default height-correlated GGX",
                "pbr_datablock_readback_verified": True,
                "compositor2": True,
                "ui_included": False,
                "cpu_readback_completed": True,
                "analytic_lights_calibrated": False,
                "constant_environment_only": True,
                "native_interop": False,
                "ray_tracing": False,
            },
            "catalog": {
                "sequence": 1,
                "transactional_replay_after_restart": True,
            },
            "hdr": {
                "format": "RGBA16_FLOAT",
                "maximum_luminance": 1.2,
                "non_background_pixels": 600,
            },
            "sdr": {
                "format": "RGBA8_SRGB",
                "rgb8_fnv1a64": f"{hash_value:016x}",
                "distinct_rgb8_values": 2,
                "non_background_pixels": 600,
            },
            "lifecycle": {
                "unsupported_depth_failed_before_submission": True,
                "latest_snapshot_only_identity_window": True,
                "shutdown_reinitialize_render_shutdown": True,
            },
        }
        with tempfile.TemporaryDirectory(prefix="ror-n1-validator-") as temp:
            image = Path(temp) / "n1.ppm"
            image.write_bytes(b"P6\n192 128\n255\n" + pixels)
            RUNNER.validate_n1_checkpoint(report, image, lock, policy)
            invalid = copy.deepcopy(report)
            invalid["hdr"]["maximum_luminance"] = 1.0
            with self.assertRaises(RUNNER.ProbeError):
                RUNNER.validate_n1_checkpoint(invalid, image, lock, policy)

    def test_wrapper_makes_n1_artifacts_mandatory(self) -> None:
        lock = RUNNER.load_lock()
        policy = RUNNER.detect_policy("Darwin", "arm64")
        with tempfile.TemporaryDirectory(prefix="ror-n1-orchestration-") as temp:
            with mock.patch.object(RUNNER, "run") as run:
                with self.assertRaisesRegex(
                    RUNNER.ProbeError, "did not produce required artifacts"
                ):
                    RUNNER.run_n1_checkpoint(
                        Path(temp), "Release", 2, lock, policy
                    )
                self.assertEqual(run.call_count, 1)


if __name__ == "__main__":
    unittest.main()
