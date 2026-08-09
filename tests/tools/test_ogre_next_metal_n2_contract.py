#!/usr/bin/env python3
"""Offline fail-closed checks for Ogre-Next Metal N2 interop."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
RENDER_ROOT = REPOSITORY_ROOT / "source" / "main" / "gfx" / "render"
OGRE_ROOT = RENDER_ROOT / "ogrenext"
PROBE_ROOT = REPOSITORY_ROOT / "tools" / "ogre_next_probe"
RUNNER_PATH = REPOSITORY_ROOT / "tools" / "run_ogre_next_probe.py"
RUNNER_SPEC = importlib.util.spec_from_file_location(
    "run_ogre_next_probe_for_n2_tests", RUNNER_PATH
)
if RUNNER_SPEC is None or RUNNER_SPEC.loader is None:
    raise RuntimeError("could not load Ogre-Next probe runner")
RUNNER = importlib.util.module_from_spec(RUNNER_SPEC)
RUNNER_SPEC.loader.exec_module(RUNNER)


class OgreNextMetalN2ContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.frontend = (OGRE_ROOT / "OgreNextN1Frontend.cpp").read_text(
            encoding="utf-8"
        )
        cls.bridge = (OGRE_ROOT / "OgreNextMetalInterop.mm").read_text(
            encoding="utf-8"
        )
        cls.backend = (
            OGRE_ROOT / "OgreNextMetalRayTracingBackend.mm"
        ).read_text(encoding="utf-8")
        cls.native_header = (
            OGRE_ROOT / "OgreNextN1NativeInterop.h"
        ).read_text(encoding="utf-8")
        cls.backend_header = (
            OGRE_ROOT / "OgreNextMetalRayTracingBackend.h"
        ).read_text(encoding="utf-8")
        cls.state = (OGRE_ROOT / "OgreNextN2InteropState.cpp").read_text(
            encoding="utf-8"
        )
        cls.smoke = (
            PROBE_ROOT / "src" / "metal_n2_smoke.cpp"
        ).read_text(encoding="utf-8")
        cls.cmake = (PROBE_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        cls.provenance_check = (
            PROBE_ROOT / "cmake" / "VerifyN2SourceProvenance.cmake"
        ).read_text(encoding="utf-8")

    def test_headers_keep_objc_and_ogre_types_private(self) -> None:
        for header in (self.native_header, self.backend_header):
            with self.subTest(header=header[:40]):
                self.assertNotIn("#import", header)
                self.assertNotIn("id<MTL", header)
                self.assertNotIn("Ogre::", header)
        self.assertIn("std::unique_ptr<Impl>", self.backend_header)
        self.assertIn(
            "std::enable_shared_from_this<OgreNextN1NativeInteropBridge>",
            self.native_header,
        )

    def test_bridge_borrows_the_exact_live_ogre_device_and_queue(self) -> None:
        for token in (
            "getActiveDevice()",
            "metal_device->mDevice",
            "metal_device->mMainCommandQueue",
            "metal_device->mCurrentCommandBuffer",
            "endAllEncoders()",
            "commitAndNextCommandBuffer()",
            "newSharedEvent",
        ):
            self.assertIn(token, self.bridge)
        combined = self.bridge + self.backend
        self.assertNotIn("MTLCreateSystemDefaultDevice", combined)
        self.assertNotIn("newCommandQueue", combined)

    def test_export_uses_exact_pooled_v2_slices_with_bounds(self) -> None:
        for token in (
            "MetalBufferInterface",
            "getVboName()",
            "_getFinalBufferStart()",
            "getBytesPerElement()",
            "metal_vertex_buffer.length",
            "metal_index_buffer.length",
            "binding.frame_id",
        ):
            self.assertIn(token, self.bridge)
        self.assertIn("READ_ONLY_ACCELERATION_STRUCTURE_BUILD", self.state)
        self.assertIn("render_mesh->vertex_buffer", self.frontend)
        self.assertIn("render_mesh->index_buffer", self.frontend)
        self.assertIn("submitted_frame_meshes", self.frontend)

    def test_timeline_order_is_explicit_and_cpu_wait_follows_commit(self) -> None:
        self.assertLess(
            self.bridge.index("metal_device_->endAllEncoders();"),
            self.bridge.index("encodeSignalEvent:timeline_"),
        )
        self.assertLess(
            self.backend.index("[acceleration_encoder endEncoding]"),
            self.backend.index("encodeSignalEvent:timeline"),
        )
        self.assertLess(
            self.backend.index("[compute_encoder endEncoding]"),
            self.backend.index("encodeSignalEvent:timeline"),
        )
        self.assertLess(
            self.backend.index("[command_buffer commit]"),
            self.backend.index("dispatch_semaphore_wait(\n            completion_"),
        )
        self.assertIn("five seconds; live leases were retained", self.backend)
        self.assertIn("backend and leases remain live", self.backend)

    def test_backend_builds_blas_and_tlas_from_exported_buffers(self) -> None:
        for token in (
            "triangle.vertexBuffer = vertex_buffer",
            "triangle.vertexBufferOffset",
            "triangle.vertexStride",
            "triangle.indexBuffer = index_buffer",
            "triangle.indexBufferOffset",
            "triangle.indexType",
            "buildAccelerationStructure:blas",
            "buildAccelerationStructure:tlas",
            "setAccelerationStructure:tlas",
            "supportsRaytracing",
            "MTLGPUFamilyApple9",
        ):
            self.assertIn(token, self.backend)
        self.assertNotIn("newBufferWithBytes:vertices", self.backend)

    def test_lifecycle_fails_closed_and_shutdown_order_is_proven(self) -> None:
        for token in (
            "OUTSTANDING_LEASES",
            "AbortExternalFrameBeforeSubmission",
            "MarkExternalSubmitted",
            "MarkExternalCompleted",
            "CanShutdown",
            "native RT backend must shut down before the Ogre frontend",
            "AbandonRayTracingBackendAfterFault",
            "RevokeFrontend",
        ):
            self.assertIn(token, self.state + self.bridge + self.backend)
        for token in (
            "stale_generation_rejected",
            "revision_n_plus_one_blocked_while_n_live",
            "frontend_shutdown_blocked_before_backend",
            "backend_shutdown_before_frontend",
            "frontend_revoke_clears_backend_readiness",
            "frontend_destructor_before_backend_safe",
            "backend_destructor_before_frontend_safe",
            "native_submission_precedes_injected_observation",
            "injected_device_lost_abandon_allows_frontend_shutdown",
            "injected_timeout_abandon_allows_frontend_shutdown",
            "post_release_revision_n_plus_one_rendered",
            "ValidateNativeGeometryInteropProofSet",
        ):
            self.assertIn(token, self.smoke)
        self.assertIn(
            "std::shared_ptr<OgreNextN1NativeInteropBridge> bridge_",
            self.backend,
        )
        self.assertNotIn("OgreNextN1NativeInteropBridge *bridge_", self.backend)
        self.assertIn("bridge_->QueryCapabilities()", self.backend)
        self.assertIn("InjectObservationForTesting", self.backend_header)
        self.assertIn("ObserveSubmittedCommand()", self.backend)
        self.assertIn("ProveInjectedObservationAbandon", self.smoke)

    def test_probe_does_not_claim_a_rendered_image_or_gpu_timing(self) -> None:
        self.assertIn("RunGeometryInteropProbe", self.backend_header + self.backend)
        self.assertIn("does not produce a view-dependent RenderFrameOutput", self.backend)
        self.assertNotIn("FillProofOutput", self.backend)
        self.assertNotIn("gpu_frame_milliseconds", self.backend)
        self.assertNotIn("cpu_submit_milliseconds", self.backend)
        for token in (
            '"rendered_image_produced\\\": false',
            '"view_dependent\\\": false',
            '"gpu_timestamp_measured\\\": false',
        ):
            self.assertIn(token, self.smoke)

    def test_objcxx_and_runtime_proof_are_apple_only(self) -> None:
        self.assertIn("if (APPLE)\n    enable_language(OBJCXX)", self.cmake)
        apple_block = self.cmake[
            self.cmake.index("if (APPLE)\n        set(_ror_n1_renderer_definition") :
            self.cmake.index("elseif (WIN32)")
        ]
        for source in (
            "OgreNextMetalInterop.mm",
            "OgreNextMetalRayTracingBackend.mm",
            "ror_ogre_next_metal_n2_smoke",
        ):
            self.assertIn(source, self.cmake)
        self.assertIn("OgreNextMetalRayTracingBackend.mm", apple_block)
        self.assertIn("-fobjc-arc", apple_block)
        self.assertIn("SKIP_RETURN_CODE 77", self.cmake)
        self.assertIn("RunN2Smoke.cmake", self.cmake)
        self.assertIn(
            "available only in the macOS Metal target", self.frontend
        )
        self.assertIn("--media-root", self.smoke)
        self.assertIn("N2_MEDIA_ROOT", self.cmake)
        self.assertIn("OgreNextN1Configuration{media_root}", self.smoke)
        self.assertIn(
            "OgreNextN1Configuration{arguments.media_root}", self.smoke
        )

    def test_each_isolated_frontend_uses_contiguous_frame_ids(self) -> None:
        for stale_start in (
            "MakeFrame(10U",
            "MakeFrame(20U",
            "MakeFrame(30U",
            "MakeFrame(40U",
        ):
            with self.subTest(stale_start=stale_start):
                self.assertNotIn(stale_start, self.smoke)
        self.assertIn("MakeFrame(1U, MakeScene(1U, 2U", self.smoke)
        self.assertIn("MakeFrame(2U, MakeScene(2U, 3U", self.smoke)

    def test_source_provenance_is_clean_and_rechecked_before_link(self) -> None:
        combined = self.cmake + self.provenance_check
        for token in (
            "git status --porcelain=v1 --untracked-files=all",
            "file(GLOB_RECURSE",
            "file(SHA256",
            "ROR_OGRE_NEXT_N2_RELEVANT_SOURCE_CLEAN=1",
            "ROR_OGRE_NEXT_N2_SOURCE_MANIFEST_SHA256",
            "PRE_LINK",
            "N2_EXPECTED_SOURCE_MANIFEST_SHA256",
        ):
            self.assertIn(token, combined)

    def test_relevant_source_manifest_rejects_dirty_content(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-metal-n2-source-") as temp:
            repository = Path(temp)
            relevant = repository / "source" / "main" / "gfx" / "render"
            relevant.mkdir(parents=True)
            tracked = relevant / "proof.cpp"
            tracked.write_text("int proof = 1;\n", encoding="utf-8")
            subprocess.run(["git", "init", "-q"], cwd=repository, check=True)
            subprocess.run(["git", "add", "."], cwd=repository, check=True)
            subprocess.run(
                [
                    "git",
                    "-c",
                    "user.name=RoR N2 Test",
                    "-c",
                    "user.email=ror-n2@example.invalid",
                    "commit",
                    "-q",
                    "-m",
                    "fixture",
                ],
                cwd=repository,
                check=True,
            )
            paths = ("source/main/gfx/render",)
            manifest = RUNNER.relevant_source_manifest(repository, paths)
            self.assertRegex(manifest["sha256"], r"^[0-9a-f]{64}$")
            self.assertEqual(manifest["file_count"], 1)
            RUNNER.require_relevant_source_clean(repository, paths)

            tracked.write_text("int proof = 2;\n", encoding="utf-8")
            with self.assertRaises(RUNNER.ProbeError):
                RUNNER.require_relevant_source_clean(repository, paths)
            self.assertNotEqual(
                RUNNER.relevant_source_manifest(repository, paths), manifest
            )
            tracked.write_text("int proof = 1;\n", encoding="utf-8")
            RUNNER.require_relevant_source_clean(repository, paths)
            self.assertEqual(
                RUNNER.relevant_source_manifest(repository, paths), manifest
            )

            (relevant / "untracked.cpp").write_text(
                "int untracked = 1;\n", encoding="utf-8"
            )
            with self.assertRaises(RUNNER.ProbeError):
                RUNNER.require_relevant_source_clean(repository, paths)
            expanded_manifest = RUNNER.relevant_source_manifest(repository, paths)
            self.assertEqual(expanded_manifest["file_count"], 2)
            self.assertNotEqual(expanded_manifest["sha256"], manifest["sha256"])

    def test_deformable_capture_manifest_rejects_deletion_and_hashes_mutation(
        self,
    ) -> None:
        relative = "source/main/physics/flex/FlexBody.cpp"
        self.assertIn(relative, RUNNER.RELEVANT_SOURCE_PATHS)
        with tempfile.TemporaryDirectory(
            prefix="ror-metal-n2-deformable-source-"
        ) as temp:
            repository = Path(temp)
            tracked = repository / relative
            tracked.parent.mkdir(parents=True)
            tracked.write_text("int deformable_capture = 1;\n", encoding="utf-8")
            paths = (relative,)
            baseline = RUNNER.relevant_source_manifest(repository, paths)

            tracked.write_text("int deformable_capture = 2;\n", encoding="utf-8")
            self.assertNotEqual(
                RUNNER.relevant_source_manifest(repository, paths), baseline
            )

            tracked.unlink()
            with self.assertRaises(RUNNER.ProbeError):
                RUNNER.relevant_source_manifest(repository, paths)

    def test_report_validator_requires_exact_live_evidence(self) -> None:
        lock = RUNNER.load_lock()
        policy = RUNNER.detect_policy("Darwin", "arm64")
        probe = struct.pack("<If", 0x52545254, 1.0)
        executable = b"reviewed-metal-n2-executable"
        source_commit = "1" * 40
        source_ref = "codex/test"
        source_manifest_sha256 = "a" * 64
        report = {
            "schema": "ror.ogre_next_metal_rt_n2.v3",
            "status": "pass",
            "scope": (
                "same-device single-ray geometry interop capability probe; no "
                "rendered image, view-dependent result, GPU timing, material, "
                "lighting, denoising, or compositing claim"
            ),
            "provenance": {
                "ror_repository": RUNNER.ROR_SOURCE_REPOSITORY,
                "ror_ref": source_ref,
                "ror_commit": source_commit,
                "relevant_source_clean": True,
                "relevant_source_manifest_sha256": source_manifest_sha256,
                "ogre_next_repository": lock["repository"],
                "ogre_next_commit": lock["commit"],
                "ogre_next_archive_sha256": lock["archive_sha256"],
                "build_artifact": "ror_ogre_next_metal_n2_smoke",
                "build_artifact_bytes": len(executable),
                "build_artifact_sha256": hashlib.sha256(executable).hexdigest(),
            },
            "device": {
                "name": "Apple M5",
                "context_id": 1,
                "same_ogre_device": True,
                "same_ogre_queue": True,
            },
            "admission": {
                field: True
                for field in (
                    "frontend_api_reported",
                    "backend_compiled",
                    "api_supported",
                    "supports_raytracing",
                    "supports_family_apple9",
                    "hardware_accelerated",
                    "dispatch_readback_probe_passed",
                    "geometry_interop_ready",
                )
            },
            "geometry": {
                "frame_id": 1,
                "snapshot_id": 1,
                "instance_id": 1,
                "topology_revision": 1,
                "deformation_revision": 2,
                "vertex_count": 3,
                "index_count": 3,
                "vertex_buffer_generation": 1,
                "vertex_pool_offset_bytes": 128,
                "vertex_slice_bytes": 60,
                "vertex_stride_bytes": 24,
                "vertex_buffer_length_bytes": 4096,
                "index_buffer_generation": 1,
                "index_pool_offset_bytes": 256,
                "index_slice_bytes": 6,
                "index_stride_bytes": 2,
                "index_buffer_length_bytes": 4096,
                "exact_exported_vertex_slice_used": True,
                "exact_exported_index_slice_used": True,
            },
            "synchronization": {
                "frontend_complete_value": 1,
                "external_complete_value": 2,
                "same_shared_event": True,
                "external_encoders_ended_before_signal": True,
                "cpu_wait_after_commit_only": True,
            },
            "acceleration_structures": {
                "blas_bytes": 512,
                "blas_scratch_bytes": 512,
                "tlas_bytes": 512,
                "tlas_scratch_bytes": 512,
            },
            "probe": {
                "kind": "single_ray_geometry_interop",
                "rays": 1,
                "hit_magic": 0x52545254,
                "hit_distance": 1.0,
                "probe_readback_bytes": len(probe),
                "probe_readback_sha256": hashlib.sha256(probe).hexdigest(),
                "rendered_image_produced": False,
                "view_dependent": False,
                "gpu_timestamp_measured": False,
            },
            "lifecycle": {
                field: True
                for field in (
                    "stale_generation_rejected",
                    "revision_n_plus_one_blocked_while_n_live",
                    "frontend_shutdown_blocked_before_backend",
                    "backend_shutdown_before_frontend",
                    "frontend_revoke_clears_backend_readiness",
                    "frontend_destructor_before_backend_safe",
                    "backend_destructor_before_frontend_safe",
                    "native_submission_precedes_injected_observation",
                    "injected_device_lost_abandon_allows_frontend_shutdown",
                    "injected_timeout_abandon_allows_frontend_shutdown",
                    "post_release_revision_n_plus_one_rendered",
                    "interop_report_geometry_proven",
                )
            },
        }
        with tempfile.TemporaryDirectory(prefix="ror-metal-n2-validator-") as temp:
            artifact = Path(temp) / RUNNER.N2_PROBE_NAME
            artifact.write_bytes(probe)
            executable_path = Path(temp) / "ror_ogre_next_metal_n2_smoke"
            executable_path.write_bytes(executable)
            RUNNER.validate_n2_checkpoint(
                report,
                artifact,
                executable_path,
                lock,
                policy,
                source_commit,
                source_ref,
                source_manifest_sha256,
            )
            invalid = copy.deepcopy(report)
            invalid["geometry"]["vertex_pool_offset_bytes"] = 5000
            with self.assertRaises(RUNNER.ProbeError):
                RUNNER.validate_n2_checkpoint(
                    invalid,
                    artifact,
                    executable_path,
                    lock,
                    policy,
                    source_commit,
                    source_ref,
                    source_manifest_sha256,
                )
            invalid = copy.deepcopy(report)
            invalid["scope"] = "full ray-traced compositing parity"
            with self.assertRaises(RUNNER.ProbeError):
                RUNNER.validate_n2_checkpoint(
                    invalid,
                    artifact,
                    executable_path,
                    lock,
                    policy,
                    source_commit,
                    source_ref,
                    source_manifest_sha256,
                )
            invalid = copy.deepcopy(report)
            invalid["geometry"]["vertex_pool_offset_bytes"] = "128"
            with self.assertRaises(RUNNER.ProbeError):
                RUNNER.validate_n2_checkpoint(
                    invalid,
                    artifact,
                    executable_path,
                    lock,
                    policy,
                    source_commit,
                    source_ref,
                    source_manifest_sha256,
                )
            invalid = copy.deepcopy(report)
            invalid["provenance"]["ror_commit"] = "2" * 40
            with self.assertRaises(RUNNER.ProbeError):
                RUNNER.validate_n2_checkpoint(
                    invalid,
                    artifact,
                    executable_path,
                    lock,
                    policy,
                    source_commit,
                    source_ref,
                    source_manifest_sha256,
                )
            invalid = copy.deepcopy(report)
            invalid["provenance"]["build_artifact_sha256"] = "0" * 64
            with self.assertRaises(RUNNER.ProbeError):
                RUNNER.validate_n2_checkpoint(
                    invalid,
                    artifact,
                    executable_path,
                    lock,
                    policy,
                    source_commit,
                    source_ref,
                    source_manifest_sha256,
                )
            invalid = copy.deepcopy(report)
            invalid["provenance"]["relevant_source_manifest_sha256"] = "b" * 64
            with self.assertRaises(RUNNER.ProbeError):
                RUNNER.validate_n2_checkpoint(
                    invalid,
                    artifact,
                    executable_path,
                    lock,
                    policy,
                    source_commit,
                    source_ref,
                    source_manifest_sha256,
                )
            invalid = copy.deepcopy(report)
            invalid["provenance"]["relevant_source_clean"] = False
            with self.assertRaises(RUNNER.ProbeError):
                RUNNER.validate_n2_checkpoint(
                    invalid,
                    artifact,
                    executable_path,
                    lock,
                    policy,
                    source_commit,
                    source_ref,
                    source_manifest_sha256,
                )
            invalid = copy.deepcopy(report)
            invalid["lifecycle"][
                "injected_timeout_abandon_allows_frontend_shutdown"
            ] = False
            with self.assertRaises(RUNNER.ProbeError):
                RUNNER.validate_n2_checkpoint(
                    invalid,
                    artifact,
                    executable_path,
                    lock,
                    policy,
                    source_commit,
                    source_ref,
                    source_manifest_sha256,
                )

    def test_checked_in_report_schema_is_json_serializable(self) -> None:
        # This guards accidental non-JSON values in validator test fixtures and
        # keeps optimized (-O) test execution equivalent.
        self.assertIsInstance(json.dumps({"schema": "n2"}), str)

    def test_report_validator_accepts_explicit_capability_skip(self) -> None:
        lock = RUNNER.load_lock()
        policy = RUNNER.detect_policy("Darwin", "arm64")
        source_commit = "3" * 40
        source_ref = "codex/capability-skip"
        source_manifest_sha256 = "c" * 64
        executable = b"compiled-on-apple-family-seven"
        report = {
            "schema": "ror.ogre_next_metal_rt_n2.v3",
            "status": "skip",
            "scope": (
                "same-device single-ray geometry interop capability probe; no "
                "rendered image, view-dependent result, GPU timing, material, "
                "lighting, denoising, or compositing claim"
            ),
            "provenance": {
                "ror_repository": RUNNER.ROR_SOURCE_REPOSITORY,
                "ror_ref": source_ref,
                "ror_commit": source_commit,
                "relevant_source_clean": True,
                "relevant_source_manifest_sha256": source_manifest_sha256,
                "ogre_next_repository": lock["repository"],
                "ogre_next_commit": lock["commit"],
                "ogre_next_archive_sha256": lock["archive_sha256"],
                "build_artifact": "ror_ogre_next_metal_n2_smoke",
                "build_artifact_bytes": len(executable),
                "build_artifact_sha256": hashlib.sha256(executable).hexdigest(),
            },
            "device": {
                "name": "Apple M1",
                "context_id": 7,
                "same_ogre_device": True,
                "same_ogre_queue": True,
            },
            "admission": {
                "frontend_api_reported": False,
                "interop_context_exported": True,
                "backend_compiled": True,
                "supports_raytracing": False,
                "supports_family_apple9": False,
                "hardware_floor_met": False,
            },
            "skip": {
                "initialization_code": "UNSUPPORTED",
                "reason": "the exact Ogre Metal device does not support ray tracing",
                "required_metal_ray_tracing": True,
                "required_apple_gpu_family": 9,
            },
            "probe": {
                "executed": False,
                "probe_readback_bytes": 0,
                "rendered_image_produced": False,
                "view_dependent": False,
                "gpu_timestamp_measured": False,
            },
        }
        with tempfile.TemporaryDirectory(prefix="ror-metal-n2-skip-") as temp:
            executable_path = Path(temp) / "ror_ogre_next_metal_n2_smoke"
            executable_path.write_bytes(executable)
            RUNNER.validate_n2_checkpoint(
                report,
                None,
                executable_path,
                lock,
                policy,
                source_commit,
                source_ref,
                source_manifest_sha256,
            )
            invalid = copy.deepcopy(report)
            invalid["admission"]["hardware_floor_met"] = True
            with self.assertRaises(RUNNER.ProbeError):
                RUNNER.validate_n2_checkpoint(
                    invalid,
                    None,
                    executable_path,
                    lock,
                    policy,
                    source_commit,
                    source_ref,
                    source_manifest_sha256,
                )


if __name__ == "__main__":
    unittest.main()
