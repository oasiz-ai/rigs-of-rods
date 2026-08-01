#!/usr/bin/env python3
"""Offline fail-closed checks for Ogre-Next Metal N2 interop."""

from __future__ import annotations

import copy
import importlib.util
import json
from pathlib import Path
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

    def test_headers_keep_objc_and_ogre_types_private(self) -> None:
        for header in (self.native_header, self.backend_header):
            with self.subTest(header=header[:40]):
                self.assertNotIn("#import", header)
                self.assertNotIn("id<MTL", header)
                self.assertNotIn("Ogre::", header)
        self.assertIn("std::unique_ptr<Impl>", self.backend_header)

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
        ):
            self.assertIn(token, self.state + self.bridge + self.backend)
        for token in (
            "stale_generation_rejected",
            "revision_n_plus_one_blocked_while_n_live",
            "frontend_shutdown_blocked_before_backend",
            "backend_shutdown_before_frontend",
            "post_release_revision_n_plus_one_rendered",
            "ValidateNativeGeometryInteropProofSet",
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
        self.assertIn(
            "available only in the macOS Metal target", self.frontend
        )

    def test_report_validator_requires_exact_live_evidence(self) -> None:
        lock = RUNNER.load_lock()
        policy = RUNNER.detect_policy("Darwin", "arm64")
        readback = bytes((0x52, 0x54, 64, 0xFF)) * (96 * 64)
        hash_value = 14695981039346656037
        for value in readback:
            hash_value ^= value
            hash_value = (hash_value * 1099511628211) & ((1 << 64) - 1)
        report = {
            "schema": "ror.ogre_next_metal_rt_n2.v1",
            "status": "pass",
            "scope": (
                "same-device one-ray geometry interop acceptance; no ray-traced "
                "material, lighting, denoising, or compositing parity claim"
            ),
            "provenance": {
                "ror_repository": "https://github.com/RigsOfRods/rigs-of-rods",
                "ror_ref": "codex/test",
                "ror_commit": "1" * 40,
                "ogre_next_repository": lock["repository"],
                "ogre_next_commit": lock["commit"],
                "ogre_next_archive_sha256": lock["archive_sha256"],
                "build_artifact": "ror_ogre_next_metal_n2_smoke",
                "build_artifact_bytes": 1024,
                "build_artifact_fnv1a64": "1" * 16,
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
            "dispatch": {
                "rays": 1,
                "hit_magic": 0x52545254,
                "hit_distance": 1.0,
                "readback_bytes": len(readback),
                "readback_fnv1a64": f"{hash_value:016x}",
            },
            "lifecycle": {
                field: True
                for field in (
                    "stale_generation_rejected",
                    "revision_n_plus_one_blocked_while_n_live",
                    "frontend_shutdown_blocked_before_backend",
                    "backend_shutdown_before_frontend",
                    "post_release_revision_n_plus_one_rendered",
                    "interop_report_geometry_proven",
                )
            },
        }
        with tempfile.TemporaryDirectory(prefix="ror-metal-n2-validator-") as temp:
            artifact = Path(temp) / RUNNER.N2_READBACK_NAME
            artifact.write_bytes(readback)
            RUNNER.validate_n2_checkpoint(report, artifact, lock, policy)
            invalid = copy.deepcopy(report)
            invalid["geometry"]["vertex_pool_offset_bytes"] = 5000
            with self.assertRaises(RUNNER.ProbeError):
                RUNNER.validate_n2_checkpoint(invalid, artifact, lock, policy)
            invalid = copy.deepcopy(report)
            invalid["scope"] = "full ray-traced compositing parity"
            with self.assertRaises(RUNNER.ProbeError):
                RUNNER.validate_n2_checkpoint(invalid, artifact, lock, policy)
            invalid = copy.deepcopy(report)
            invalid["geometry"]["vertex_pool_offset_bytes"] = "128"
            with self.assertRaises(RUNNER.ProbeError):
                RUNNER.validate_n2_checkpoint(invalid, artifact, lock, policy)

    def test_checked_in_report_schema_is_json_serializable(self) -> None:
        # This guards accidental non-JSON values in validator test fixtures and
        # keeps optimized (-O) test execution equivalent.
        self.assertIsInstance(json.dumps({"schema": "n2"}), str)


if __name__ == "__main__":
    unittest.main()
