#!/usr/bin/env python3
"""Static hostile gates for the isolated Metal sun-visibility V2 slice."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
OGRE_NEXT = ROOT / "source/main/gfx/render/ogrenext"
PROBE = ROOT / "tools/ogre_next_probe"


class NativeSunVisibilityV2ContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (OGRE_NEXT / "NativeSunVisibilityV2Contract.h").read_text()
        cls.implementation = (
            OGRE_NEXT / "NativeSunVisibilityV2Contract.cpp"
        ).read_text()
        cls.shader_path = (
            PROBE / "media/Hlms/RoR/SunVisibilityV2/SunVisibilityV2.metal"
        )
        cls.shader = cls.shader_path.read_text()
        cls.interop = (
            OGRE_NEXT / "OgreNextSunVisibilityV2Interop.h"
        ).read_text()
        cls.interop_implementation = (
            OGRE_NEXT / "OgreNextSunVisibilityV2Interop.cpp"
        ).read_text()
        cls.metal_backend = (
            OGRE_NEXT / "OgreNextMetalRayTracingBackend.mm"
        ).read_text()
        cls.frontend_header = (
            OGRE_NEXT / "OgreNextN1Frontend.h"
        ).read_text()
        cls.frontend = (
            OGRE_NEXT / "OgreNextN1Frontend.cpp"
        ).read_text()
        cls.lock = json.loads(
            (PROBE / "metal-sun-visibility-v2.lock.json").read_text()
        )

    def test_v1_contract_and_backend_shader_are_untouched(self) -> None:
        # V2 must live beside, not silently reinterpret, the reviewed V1 API.
        self.assertIn("kNativeSunVisibilityV2ContractVersion = 2U", self.header)
        frozen_files = {
            OGRE_NEXT / "NativeDirectionalShadowContract.h":
                "554e5d00a2a4cf894f80eba5f9fa65683869f61180b5e1560d11fbb637c7435e",
            OGRE_NEXT / "NativeDirectionalShadowContract.cpp":
                "50abad378461ceee1c2e2c9c5de69348b463079d1050f0de5a9ef67cf6e5cc81",
            PROBE / "src/metal_n4_directional_shadow_smoke.cpp":
                "c25a76c8eb833a971ac0ee9ead0c0c59a9663a8d6da7bff0921f0d207216bb4e",
        }
        for path, expected in frozen_files.items():
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), expected)
        shader_start = self.metal_backend.index(
            "    static NSString *const directional_shadow_shader_source ="
        )
        shader_end = self.metal_backend.index(
            "      NSError *directional_library_error = nil;", shader_start
        )
        frozen_shader = self.metal_backend[shader_start:shader_end].encode()
        self.assertEqual(
            hashlib.sha256(frozen_shader).hexdigest(),
            "1250501c0e21d2b409dabb8d05439aa2c36211b0b5f0407c0c895fd75cde6a93",
        )

    def test_shader_composes_only_sun_direct_and_forces_opaque_alpha(self) -> None:
        for token in (
            "texture2d<half, access::read> base_hdr",
            "texture2d<half, access::read> sun_direct_hdr",
            "texture2d<half, access::write> visibility",
            "texture2d<half, access::write> lit_hdr",
            "float3(base.rgb) + float(v) * float3(sun_direct.rgb)",
            "half4(half3(composed), half(1.0h))",
            "lit_hdr.write(half4(base.rgb, half(1.0h)), pixel)",
            "2.0f * primary.min_distance",
            "device atomic_uint* counters",
            "counters + 0",
            "counters + 1",
            "counters + 4",
        ):
            self.assertIn(token, self.shader)
        self.assertNotIn("base.rgb *", self.shader)

    def test_shader_is_hash_locked(self) -> None:
        observed = hashlib.sha256(self.shader_path.read_bytes()).hexdigest()
        self.assertEqual(observed, self.lock["sha256"])

    def test_admission_excludes_nonopaque_layers(self) -> None:
        for token in (
            "NATIVE_SUN_VISIBILITY_V2_ALPHA_LAYER",
            "NATIVE_SUN_VISIBILITY_V2_DECAL",
            "NATIVE_SUN_VISIBILITY_V2_RT_INERT",
            "NATIVE_SUN_VISIBILITY_V2_RASTER_VISIBLE",
            "kNativeSunVisibilityV2MaximumAdmittedInstances = 64U",
            "strictly increasing",
            "raster-visible opaque caster must also be a receiver",
        ):
            self.assertIn(token, self.header + self.implementation)

    def test_production_readbacks_are_exact_zero(self) -> None:
        self.assertIn("production_cpu_content_readbacks != 0U", self.implementation)
        self.assertIn("production_gpu_content_readbacks != 0U", self.implementation)
        self.assertIn("may not read image content back", self.implementation)
        v2_start = self.metal_backend.index(
            "  NativeSunVisibilityV2Result RenderSunVisibilityV2("
        )
        v2_end = self.metal_backend.index(
            "  RenderOperationResult ValidateInteropEvidence(", v2_start
        )
        v2_execution = self.metal_backend[v2_start:v2_end]
        for forbidden in ("copyFromTexture", "getBytes:",
                          "blitCommandEncoder", "synchronizeResource"):
            self.assertNotIn(forbidden, v2_execution)
        self.assertIn("v2_counter_buffer_.contents", v2_execution)

    def test_backend_executes_locked_persistent_same_queue_v2(self) -> None:
        for token in (
            "device.supportsRaytracing",
            "MTLGPUFamilyApple9",
            "ReadLockedSunVisibilityV2Shader",
            "kSunVisibilityV2ShaderSha256",
            "SunVisibilityV2BlasCacheEntry",
            "BlasWork::Operation::BUILD",
            "BlasWork::Operation::REFIT",
            "BlasWork::Operation::HIT",
            "TlasOperation::BUILD",
            "TlasOperation::REFIT",
            "TlasOperation::HIT",
            "encodeWaitForEvent:timeline",
            "encodeSignalEvent:timeline",
            "ContinuePresentationFromSunVisibilityV2LitHdr",
        ):
            self.assertIn(token, self.metal_backend)
        native_interop = (
            OGRE_NEXT / "OgreNextN1NativeInterop.h"
        ).read_text()
        metal_interop = (OGRE_NEXT / "OgreNextMetalInterop.mm").read_text()
        self.assertIn("native_storage_generation", native_interop)
        self.assertIn("binding.native_storage_generation", metal_interop)

    def test_persistent_tlas_and_moved_scene_lineage_are_explicit(self) -> None:
        for token in (
            "scene_plan_digest",
            "tlas_cache_hit_count",
            "acceptance_caster_transform_revision",
            "ValidateNativeSunVisibilityV2MovedCasterSmokeContract(\n"
            "    const NativeSunVisibilityV2FrameContract &first_frame,",
        ):
            self.assertIn(token, self.header + self.implementation)

    def test_four_image_alias_check_ignores_spoofed_generation(self) -> None:
        self.assertIn(
            "bindings[lhs]->image.value == bindings[rhs]->image.value",
            self.interop_implementation,
        )
        self.assertNotIn(
            "bindings[lhs]->image.value == bindings[rhs]->image.value &&",
            self.interop_implementation,
        )

    def test_failures_retain_exact_stage_and_detail(self) -> None:
        for token in (
            "NativeSunVisibilityV2Stage",
            "CAPABILITY_GATE",
            "ACCELERATION_STRUCTURE_BUILD",
            "VISIBILITY_AND_COMPOSITE",
            "EXTERNAL_COMPLETION",
            "PRESENT_CONTINUATION",
            "std::string detail",
            "ValidateNativeSunVisibilityV2Result",
        ):
            self.assertIn(token, self.header + self.implementation)

    def test_multi_image_gpu_lease_and_present_continuation_are_additive(self) -> None:
        for token in (
            "BASE_HDR_RGBA16",
            "SUN_DIRECT_HDR_RGBA16",
            "VISIBILITY_R16",
            "LIT_HDR_RGBA16",
            "R16_FLOAT = 2",
            "OgreNextSunVisibilityV2ImageBinding base_hdr",
            "OgreNextSunVisibilityV2ImageBinding sun_direct_hdr",
            "OgreNextSunVisibilityV2ImageBinding visibility",
            "OgreNextSunVisibilityV2ImageBinding lit_hdr",
            "ValidateOgreNextSunVisibilityV2ImageSetExport",
            "AcquireSunVisibilityV2ImageSet",
            "ValidateSunVisibilityV2ImageSetLease",
            "ContinuePresentationFromSunVisibilityV2LitHdr",
            "AbortSunVisibilityV2ImageSetBeforeSubmission",
            "ReleaseSunVisibilityV2ImageSet",
            "there is intentionally no byte vector",
        ):
            self.assertIn(token, self.interop)

    def test_test_only_evidence_retains_the_exact_four_image_equation(self) -> None:
        for token in (
            "kOgreNextSunVisibilityV2ContentEvidenceVersion = 2U",
            "std::vector<std::uint16_t> base_hdr_rgba16",
            "std::vector<std::uint16_t> sun_direct_hdr_rgba16",
            "std::vector<std::uint16_t> visibility_r16",
            "std::vector<std::uint16_t> lit_hdr_rgba16",
        ):
            self.assertIn(token, self.frontend_header)
        capture_start = self.frontend.index(
            "  CaptureSunVisibilityV2ContentEvidence() {"
        )
        capture_end = self.frontend.index(
            "  [[nodiscard]] std::uint64_t &LightingContentReadbackCounter()",
            capture_start,
        )
        capture = self.frontend[capture_start:capture_end]
        for token in (
            "download_rgba16(hdr_base_hdr_target, 1.0F, \"BaseHdr\"",
            "download_rgba16(hdr_sun_direct_hdr_target, 0.0F, \"SunDirectHdr\"",
            "visibility_image.convertFromTexture(hdr_visibility_target",
            "download_rgba16(hdr_lit_target, 1.0F, \"LitHdr\"",
        ):
            self.assertIn(token, capture)
        self.assertEqual(
            capture.count("++lighting_audit.test_artifact_content_readbacks;"),
            2,
        )
        self.assertNotIn("production_content_readbacks", capture)


if __name__ == "__main__":
    unittest.main()
