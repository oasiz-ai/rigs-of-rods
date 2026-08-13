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
        cls.lock = json.loads(
            (PROBE / "metal-sun-visibility-v2.lock.json").read_text()
        )

    def test_v1_contract_and_backend_shader_are_untouched(self) -> None:
        # V2 must live beside, not silently reinterpret, the reviewed V1 API.
        self.assertIn("kNativeSunVisibilityV2ContractVersion = 2U", self.header)
        self.assertNotIn("NativeSunVisibilityV2", (
            OGRE_NEXT / "NativeDirectionalShadowContract.h"
        ).read_text())
        self.assertNotIn("sun_visibility_v2", (
            OGRE_NEXT / "OgreNextMetalRayTracingBackend.mm"
        ).read_text())

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


if __name__ == "__main__":
    unittest.main()
