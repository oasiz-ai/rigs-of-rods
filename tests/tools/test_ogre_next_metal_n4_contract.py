#!/usr/bin/env python3
"""Offline fail-closed tests for the Metal N4 directional-shadow smoke."""

from __future__ import annotations

from pathlib import Path
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
PROBE_ROOT = REPOSITORY_ROOT / "tools" / "ogre_next_probe"


class MetalN4ContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.smoke = (
            PROBE_ROOT / "src" / "metal_n4_directional_shadow_smoke.cpp"
        ).read_text(encoding="utf-8")
        cls.cmake = (PROBE_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        cls.run_n4 = (PROBE_ROOT / "cmake" / "RunN4Smoke.cmake").read_text(
            encoding="utf-8"
        )
        ogre_root = (
            REPOSITORY_ROOT / "source" / "main" / "gfx" / "render" / "ogrenext"
        )
        cls.backend_header = (
            ogre_root / "OgreNextMetalRayTracingBackend.h"
        ).read_text(encoding="utf-8")
        cls.backend = (ogre_root / "OgreNextMetalRayTracingBackend.mm").read_text(
            encoding="utf-8"
        )

    def test_smoke_selects_rt4_n4_and_explicitly_disables_pssm(self) -> None:
        for token in (
            "OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1",
            "METAL_RAY_TRACING_N4_DIRECTIONAL_HARD_SHADOW",
            "OgreNextDirectionalShadowMode::DISABLED",
            "OgreNextMetalRayTracingMode::N4_DIRECTIONAL_HARD_SHADOW",
            "pssm_audit.shadow_frames_completed == 0U",
        ):
            self.assertIn(token, self.smoke)
        self.assertNotIn("PSSM_3_CASCADE_V1", self.smoke)

    def test_controlled_scene_has_full_receiver_and_partial_distinct_occluder(
        self,
    ) -> None:
        for token in (
            'Quad(3.25F, 2.25F, "N4 full-view receiver quad")',
            'Quad(0.72F, 0.58F, "N4 partial distinct occluder quad")',
            "receiver.instance_id = 1U",
            "receiver.flags = MESH_INSTANCE_RECEIVES_SHADOW",
            "receiver.visibility_mask = 0x01U",
            "occluder.instance_id = 2U",
            "occluder.flags = MESH_INSTANCE_CASTS_SHADOW",
            "occluder.visibility_mask = 0x02U",
            "occluder.render_from_object.elements[14U] = 1.0F",
            "view.visibility_mask = 0x01U",
            "light.direction = {0.0F, 0.0F, -1.0F}",
            "light.shadow_flags = LIGHT_SHADOW_STATIC_GEOMETRY",
        ):
            self.assertIn(token, self.smoke)
        self.assertNotIn(
            "receiver.flags = MESH_INSTANCE_CASTS_SHADOW", self.smoke
        )

    def test_every_visibility_texel_is_exact_r16_and_has_ray_lineage(self) -> None:
        for token in (
            "kNativeDirectionalShadowVisibleR16",
            "kNativeDirectionalShadowOccludedR16",
            "Read16(evidence.visibility_readback_bytes, pixel * 2U)",
            "Read32(evidence.directional_lineage_readback_bytes, pixel * 4U)",
            "lineage == 1U",
            "lineage == 3U",
            "visibility contains a primary miss or noncanonical R16 value",
            "metrics.visible_pixels + metrics.occluded_pixels == pixel_count",
        ):
            self.assertIn(token, self.smoke)
        self.assertIn("0x3c00", self.smoke)
        self.assertIn("0x0000", self.smoke)

    def test_visible_and_occluded_hybrid_bytes_are_checked_exactly(self) -> None:
        visible_branch = self.smoke[
            self.smoke.index(
                "if (visibility == kNativeDirectionalShadowVisibleR16)"
            ) : self.smoke.index(
                "else if (visibility == kNativeDirectionalShadowOccludedR16)"
            )
        ]
        self.assertIn("std::memcmp", visible_branch)
        self.assertIn("8U) == 0", visible_branch)
        occluded_branch = self.smoke[
            self.smoke.index(
                "else if (visibility == kNativeDirectionalShadowOccludedR16)"
            ) : self.smoke.index(
                "N4 visibility contains a primary miss or noncanonical R16 value"
            )
        ]
        for offset in ("raster_offset", "raster_offset + 2U", "raster_offset + 4U"):
            self.assertIn(offset, occluded_branch)
        self.assertIn("raster_offset + 6U", occluded_branch)
        self.assertNotIn("hybrid_readback_bytes[", self.smoke)

    def test_both_reported_samples_pass_the_portable_contract(self) -> None:
        for token in (
            "RequireSampleMatchesReadback(evidence, 0U, kWidth, kHeight)",
            "RequireSampleMatchesReadback(evidence, 1U, kWidth, kHeight)",
            "ValidateNativeDirectionalShadowPassContract(sample)",
            "NativeDirectionalShadowVisibility::VISIBLE",
            "NativeDirectionalShadowVisibility::OCCLUDED",
            "sample.native_visibility_r16_bits",
            "sample.raster_rgba16.channels.data()",
            "sample.native_hybrid_rgba16.channels.data()",
            '\\"portable_contract_validated\\": true',
        ):
            self.assertIn(token, self.smoke)

    def test_exact_dual_geometry_and_image_handoffs_are_required(self) -> None:
        for token in (
            "evidence.geometry_request.instance_id == 1U",
            "evidence.secondary_geometry_request.instance_id == 2U",
            "kOgreNextPositionNormalTangentUv0VertexStrideBytes",
            "evidence.image_export.format == PixelFormat::RGBA16_FLOAT",
            "NativeImageState::GENERAL_READ_WRITE",
            "evidence.raster_readback_bytes.size() == pixel_count * 8U",
            "evidence.visibility_readback_bytes.size() == pixel_count * 2U",
            "pixel_count * 4U",
            "evidence.hybrid_readback_bytes.size() == pixel_count * 8U",
        ):
            self.assertIn(token, self.smoke)

    def test_report_and_all_binary_artifacts_are_emitted(self) -> None:
        for option in (
            "--raster",
            "--visibility",
            "--lineage",
            "--hybrid",
            "--report",
        ):
            self.assertIn(option, self.smoke)
            self.assertIn(option, self.run_n4)
        for token in (
            '\\"format\\": \\"RGBA16_FLOAT\\"',
            '\\"format\\": \\"R16_FLOAT\\"',
            '\\"format\\": \\"R32_UINT\\"',
            "Sha256(evidence.raster_readback_bytes)",
            "Sha256(evidence.visibility_readback_bytes)",
            "Sha256(evidence.directional_lineage_readback_bytes)",
            "Sha256(evidence.hybrid_readback_bytes)",
        ):
            self.assertIn(token, self.smoke)
        self.assertIn(
            "ror.ogre_next_metal_rt_n4_directional_shadow.v1", self.smoke
        )

    def test_provenance_binds_source_ogre_and_executable(self) -> None:
        for token in (
            "ROR_OGRE_NEXT_N4_SOURCE_REPOSITORY",
            "ROR_OGRE_NEXT_N4_SOURCE_REF",
            "ROR_OGRE_NEXT_N4_SOURCE_COMMIT",
            "ROR_OGRE_NEXT_N4_RELEVANT_SOURCE_CLEAN",
            "ROR_OGRE_NEXT_N4_SOURCE_MANIFEST_SHA256",
            "ROR_OGRE_NEXT_N1_OGRE_COMMIT",
            "build_artifact_bytes",
            "build_artifact_sha256",
            "HashFile(arguments.executable_path)",
        ):
            self.assertIn(token, self.smoke)
            if token.startswith("ROR_OGRE_NEXT_N4_SOURCE") or token == (
                "ROR_OGRE_NEXT_N4_RELEVANT_SOURCE_CLEAN"
            ):
                self.assertIn(token, self.cmake)
        self.assertIn("VerifyN2SourceProvenance.cmake", self.cmake)
        self.assertIn("Verifying clean Metal N4 source provenance", self.cmake)

    def test_cmake_target_is_apple_only_and_skips_only_capability_exit(self) -> None:
        target = "ror_ogre_next_metal_n4_directional_shadow_smoke"
        definition = self.cmake.index(f"add_executable(\n            {target}")
        apple_block = self.cmake.rfind("    if (APPLE)", 0, definition)
        apple_end = self.cmake.index("    endif ()", definition)
        self.assertGreaterEqual(apple_block, 0)
        self.assertLess(definition, apple_end)
        self.assertEqual(self.cmake.count(f"add_executable(\n            {target}"), 1)
        self.assertIn("RunN4Smoke.cmake", self.cmake)
        self.assertIn("_ror_n4_result EQUAL 77", self.run_n4)
        self.assertIn(
            "Metal N4 directional-shadow smoke failed with exit code",
            self.run_n4,
        )
        self.assertIn("SKIP_RETURN_CODE 77", self.cmake)
        self.assertIn("ror_ogre_next_metal_n4_directional_shadow_runtime", self.cmake)

    def test_backend_api_and_smoke_agree_on_n4_evidence(self) -> None:
        for token in (
            "secondary_geometry_request",
            "secondary_geometry_export",
            "visibility_readback_bytes",
            "directional_lineage_readback_bytes",
            "receiver_visible_pixel_count",
            "occluded_pixel_count",
            "directional_shadow_samples",
            "directional_shadow_sample_x",
            "directional_shadow_sample_y",
            "directional_shadow_capabilities",
            "exact_secondary_vertex_slice_used",
            "exact_secondary_index_slice_used",
            "directional_shadow_passed",
        ):
            self.assertIn(token, self.backend_header)
            self.assertIn(token, self.smoke)
        self.assertIn("N4_DIRECTIONAL_HARD_SHADOW", self.backend_header)
        self.assertIn("MTLPixelFormatR16Float", self.backend)
        self.assertIn("ValidateNativeDirectionalShadowPassContract", self.backend)

    def test_success_and_skip_paths_release_runtime_ownership(self) -> None:
        for token in (
            "frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds)",
            "backend.Shutdown(kInfiniteRenderTimeoutNanoseconds)",
            "evidence.image_request.scene_snapshot.reset()",
            "evidence.image_export.scene_snapshot.reset()",
            "scene_weak.expired()",
        ):
            self.assertIn(token, self.smoke)
        initialized = self.smoke.index("const RenderOperationResult initialized")
        skip = self.smoke.index("kCapabilitySkipExitCode", initialized)
        self.assertLess(
            self.smoke.index("frontend.Shutdown", initialized),
            skip,
        )


if __name__ == "__main__":
    unittest.main()
