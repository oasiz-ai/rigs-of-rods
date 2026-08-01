#!/usr/bin/env python3
"""Offline fail-closed checks for the isolated Ogre-Next N1 frontend."""

from __future__ import annotations

import copy
import importlib.util
from pathlib import Path
import struct
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


def reflection_fixture(
    policy_name: str = "macos-arm64-metal",
    payload: bytes | None = None,
) -> tuple[dict, bytes, dict[str, str]]:
    renderers = {
        "macos-arm64-metal": "Metal Rendering Subsystem",
        "windows-x64-d3d11": "Direct3D11 Rendering Subsystem",
        "linux-x86_64-vulkan": "Vulkan Rendering Subsystem",
    }
    if payload is None:
        raw_pixels = RUNNER.RT4_REFLECTION_RAW_BYTES // 8
        filtered_pixels = RUNNER.RT4_REFLECTION_FILTERED_BYTES // 8
        raw = (
            struct.pack("<4e", 1.0, 0.5, 0.25, 0.75) * (raw_pixels - 1)
            + struct.pack("<4e", 0.25, 1.0, 0.5, 0.25)
        )
        filtered = (
            struct.pack("<4e", 0.75, 0.375, 0.125, 1.0)
            * (filtered_pixels - 1)
            + struct.pack("<4e", 0.125, 0.75, 0.375, 1.0)
        )
        payload = raw + filtered
    raw = payload[: RUNNER.RT4_REFLECTION_RAW_BYTES]
    filtered = payload[RUNNER.RT4_REFLECTION_RAW_BYTES :]

    def section(
        block: bytes, offset: int, dimensions: list[int]
    ) -> dict:
        metrics = RUNNER._reflection_half_metrics(block, "fixture")
        return {
            "offset": offset,
            "bytes": len(block),
            "face_count": RUNNER.RT4_REFLECTION_FACE_COUNT,
            "mip_dimensions": dimensions,
            "exact_fnv1a64": RUNNER._fnv1a64(block),
            **metrics,
        }

    policy = {
        "name": policy_name,
        "renderer_name": renderers[policy_name],
    }
    report = {
        "renderer": policy["renderer_name"],
        "reflection_probes": {
            "schema": RUNNER.RT4_REFLECTION_SCHEMA,
            "evidence_file": RUNNER.RT4_PBR_REFLECTION_EVIDENCE_NAME,
            "evidence_bytes": RUNNER.RT4_REFLECTION_EVIDENCE_BYTES,
            "backend": RUNNER.RT4_REFLECTION_BACKENDS[policy_name],
            "render_system": policy["renderer_name"],
            "device_name": "Synthetic GPU",
            "driver_version": "1.2.3",
            "pixel_format": "RGBA16_FLOAT",
            "byte_order": "little_endian",
            "row_padding_included": False,
            "subresource_order": (
                "raw_face_major_then_filtered_mip_major_face_major"
            ),
            "ui_included": False,
            "same_device_exact_replay": True,
            "capture": {
                "render_frame_id": 1,
                "simulation_tick": 1,
                "probe_id": 1,
                "content_revision": 1,
                "candidate_generation": 1,
                "deterministic_seed": "0123456789abcdef",
                "resolution": RUNNER.RT4_REFLECTION_RESOLUTION,
            },
            "runtime_audit": {
                "version": 2,
                "successful_capture_count": 1,
                "failed_capture_count": 0,
                "live_probe_count": 1,
                "blend_resolution": 2048,
                "blend_texture_ready": True,
                "committed_state_digest": "1111111111111111",
                "native_execution_evidence": "2222222222222222",
                "capture_digest": "3333333333333333",
                "canonical_filtered_payload_bytes": (
                    RUNNER.RT4_REFLECTION_FILTERED_BYTES
                ),
                "completed_face_count": RUNNER.RT4_REFLECTION_FACE_COUNT,
                "completed_mip_count": len(
                    RUNNER.RT4_REFLECTION_FILTERED_DIMENSIONS
                ),
                "ui_free_capture": True,
                "reserved_render_queue_excluded": True,
            },
            "raw": section(
                raw, 0, [RUNNER.RT4_REFLECTION_RESOLUTION]
            ),
            "filtered": section(
                filtered,
                RUNNER.RT4_REFLECTION_RAW_BYTES,
                list(RUNNER.RT4_REFLECTION_FILTERED_DIMENSIONS),
            ),
        },
    }
    return report, payload, policy


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
        cls.media_integrity = (
            RENDER_ROOT / "ogrenext" / "OgreNextN1MediaIntegrity.cpp"
        ).read_text(encoding="utf-8")
        cls.policy = (
            RENDER_ROOT / "ogrenext" / "OgreNextN1Policy.cpp"
        ).read_text(encoding="utf-8")
        cls.policy_header = (
            RENDER_ROOT / "ogrenext" / "OgreNextN1Policy.h"
        ).read_text(encoding="utf-8")
        cls.reflection_header = (
            RENDER_ROOT / "ogrenext" / "OgreNextReflectionProbeRuntime.h"
        ).read_text(encoding="utf-8")
        cls.reflection_runtime = (
            RENDER_ROOT / "ogrenext" / "OgreNextReflectionProbeRuntime.cpp"
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
        self.assertIn("ROR_SOURCE_MANIFEST_SHA256", self.entry_cmake)
        self.assertIn("ror_source_identity", RUNNER_PATH.read_text(encoding="utf-8"))
        self.assertIn("ror_relevant_source_manifest_sha256", self.smoke)
        contract_sources = self.entry_cmake[
            self.entry_cmake.index("ror_ogre_next_n1_contract STATIC") :
            self.entry_cmake.index(
                "target_include_directories(\n        ror_ogre_next_n1_contract"
            )
        ]
        self.assertIn("ReflectionProbeRuntime.cpp", contract_sources)
        self.assertIn("ReflectionProbeCaptureReceipt.cpp", contract_sources)

    def test_public_boundary_contains_no_ogre_types(self) -> None:
        self.assertNotIn('#include "Ogre', self.header)
        self.assertNotIn("Ogre::", self.header)
        self.assertIn("std::unique_ptr<Impl>", self.header)
        self.assertRegex(
            self.entry_cmake,
            r"set_target_properties\(\s*ror_ogre_next_frontend_n1\s+"
            r"PROPERTIES\s+CXX_VISIBILITY_PRESET hidden\s+"
            r"VISIBILITY_INLINES_HIDDEN YES\s*\)",
        )

    def test_shader_media_is_runtime_owned_relocatable_and_fail_closed(self) -> None:
        self.assertIn("OgreNextN1Configuration", self.header)
        self.assertIn("std::string shader_media_root", self.header)
        self.assertIn("ResolveShaderMediaRoot", self.frontend)
        self.assertIn("std::filesystem::weakly_canonical", self.frontend)
        self.assertIn("requested.is_absolute()", self.frontend)
        self.assertNotIn("ROR_OGRE_NEXT_N1_MEDIA_ROOT", self.frontend)
        self.assertNotIn("ROR_OGRE_NEXT_N1_MEDIA_ROOT", self.entry_cmake)
        self.assertIn("ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_RELATIVE", self.entry_cmake)
        self.assertIn("copy_directory", self.entry_cmake)
        self.assertIn("ROR_OGRE_NEXT_N1_MEDIA_MANIFEST_ENTRIES", self.entry_cmake)
        self.assertIn("file(SHA256", self.entry_cmake)
        self.assertIn("VerifyOgreNextN1ShaderMedia", self.frontend)
        self.assertLess(
            self.frontend.index("VerifyOgreNextN1ShaderMedia"),
            self.frontend.index("if (!TryClaimOgreNextN1Root())"),
        )
        for token in (
            "kOgreNextN1ShaderMediaManifestCount",
            "recursive_directory_iterator",
            "is_symlink(status)",
            "digest != expected.sha256",
        ):
            self.assertIn(token, self.media_integrity)
        for license_name in (
            "Rigs-of-Rods-GPL-3.0.txt",
            "Ogre-Next-MIT.txt",
            "RapidJSON-license.txt",
            "LicenseRef-Heitz-LTC-Paper-Notice.txt",
            "IBLBaker.txt",
        ):
            self.assertIn(license_name, self.entry_cmake)
        self.assertIn("validate_n1_package", RUNNER_PATH.read_text(encoding="utf-8"))
        self.assertIn("ror_ogre_next_frontend_n1_media_tamper", self.entry_cmake)
        self.assertIn("VerifyN1MediaTamper.cmake", self.entry_cmake)
        self.assertIn("--media-root", self.entry_cmake)
        self.assertIn("relative shader media root did not fail closed", self.smoke)
        self.assertIn("missing shader media root did not fail closed", self.smoke)

    def test_reflection_media_closure_is_exact_cross_platform_and_pre_device(self) -> None:
        expected = (
            "Common/Any/PccDepthCompressor_ps.any",
            "Common/GLSL/PccDepthCompressor_ps.glsl",
            "Common/GLSL/QuadCameraDirNoUV_vs.glsl",
            "Common/GLSL/QuadCameraDir_vs.glsl",
            "Common/GLSL/Quad_vs.glsl",
            "Common/HLSL/PccDepthCompressor_ps.hlsl",
            "Common/HLSL/QuadCameraDirNoUV_vs.hlsl",
            "Common/HLSL/QuadCameraDir_vs.hlsl",
            "Common/HLSL/Quad_vs.hlsl",
            "Common/Metal/PccDepthCompressor_ps.metal",
            "Common/Metal/QuadCameraDirNoUV_vs.metal",
            "Common/Metal/QuadCameraDir_vs.metal",
            "Common/Metal/Quad_vs.metal",
            "Common/PccDepthCompressor.material",
            "Common/Quad.program",
            "LocalCubemaps/BlendProjectCubemap.material",
            "LocalCubemaps/CopyCubemap.material",
            "LocalCubemaps/GLSL/BlendProjectCubemap_ps.glsl",
            "LocalCubemaps/HLSL/BlendProjectCubemap_ps.hlsl",
            "LocalCubemaps/Metal/BlendProjectCubemap_ps.metal",
            "Compute/Algorithms/IBL/IBL.material.json",
            "Compute/Algorithms/IBL/SpecularIblIntegrator_cs.glsl",
            "Compute/Algorithms/IBL/SpecularIblIntegrator_cs.hlsl",
            "Compute/Algorithms/IBL/SpecularIblIntegrator_cs.metal",
            "Compute/Algorithms/IBL/SpecularIblIntegrator_piece_cs.any",
            "Compute/Tools/Any/sRGB.any",
        )
        for relative in expected:
            self.assertIn(relative, self.entry_cmake)
        for token in (
            "ROR_OGRE_NEXT_REFLECTION_MEDIA_MANIFEST_ENTRIES",
            "ROR_OGRE_NEXT_REFLECTION_MEDIA_MANIFEST_SHA256",
            "frontend_reflection_media_manifest.h.in",
            "kOgreNextReflectionMediaManifestCount",
            "reflection media contains a symbolic link",
            "digest != expected.sha256",
        ):
            self.assertIn(token, self.entry_cmake + self.media_integrity)
        self.assertIn(".stage-v8", self.entry_cmake)
        self.assertIn(
            '"Compute/Algorithms/IBL/SpecularIblIntegrator_piece_cs.any"',
            self.entry_cmake,
        )
        self.assertIn(
            "ror_ogre_next_frontend_reflection_media_tamper",
            self.entry_cmake,
        )
        self.assertIn("VerifyReflectionMediaTamper.cmake", self.entry_cmake)
        self.assertIn("--reflection-evidence", self.entry_cmake)
        self.assertIn(
            "ror-ogre-next-frontend-rt4-pbr-v1-reflection.bin",
            self.entry_cmake,
        )
        reflection_tamper = (
            PROBE_ROOT / "cmake" / "VerifyReflectionMediaTamper.cmake"
        ).read_text(encoding="utf-8")
        self.assertIn("--reflection-evidence", reflection_tamper)
        self.assertLess(
            self.frontend.index("VerifyOgreNextReflectionProbeMedia"),
            self.frontend.index("if (!TryClaimOgreNextN1Root())"),
        )
        self.assertIn(
            "OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1",
            self.frontend[
                self.frontend.index("VerifyOgreNextN1ShaderMedia") :
                self.frontend.index("if (!TryClaimOgreNextN1Root())")
            ],
        )
        self.assertIn("N1 has no native reflection-probe capture adapter", self.policy)
        self.assertIn("OgreNextReflectionProbeRuntime.cpp", self.entry_cmake)

    def test_rt4_reflection_probe_adapter_is_native_atomic_and_cross_platform(self) -> None:
        for token in (
            "ROR_OGRE_NEXT_N1_METAL",
            "ROR_OGRE_NEXT_N1_D3D11",
            "OGRE_NEXT_VULKAN",
            "Ogre::ParallaxCorrectedCubemap",
            "PCC/DepthCompressor",
            "IblSpecular/Integrate",
            "Ogre::PFG_RGBA16_FLOAT",
            "Ogre::PFG_D32_FLOAT",
            "Ogre::PASS_IBL_SPECULAR",
            "ComputeReflectionProbeCaptureMeasurement",
            "IssueFromConcreteAdapter",
            "scheduler.Commit(pending->plan_id, pending->receipts)",
            "kOgreNextPccReservedRenderQueue",
            "kOgreNextPccCaptureVisibilityBit",
            "MESH_INSTANCE_VISIBLE_IN_REFLECTIONS",
            "include_dynamic_geometry",
            "PrepareFrame",
            "FinalizeFrame",
            "AbortFrame",
            "filtered_nonzero_rgb_component_count",
            "view.inverseAffine().getTrans()",
            "owner_thread",
        ):
            self.assertIn(
                token, self.reflection_header + self.reflection_runtime
            )
        self.assertNotIn("setPriority(", self.reflection_runtime)
        self.assertLess(
            self.reflection_runtime.index(
                "scheduler.Commit(pending->plan_id, pending->receipts)"
            ),
            self.reflection_runtime.index("states.swap(published->candidate_states)"),
        )
        self.assertLess(
            self.reflection_runtime.index("states.swap(published->candidate_states)"),
            self.reflection_runtime.index(
                "audit.native_execution_evidence ="
            ),
        )
        self.assertLess(
            self.frontend.index("reflection_probe_runtime->PrepareFrame("),
            self.frontend.index("createTexture(\n        target_name"),
        )
        self.assertLess(
            self.frontend.index("ValidateRenderFrameOutput(request, candidate)"),
            self.frontend.index("reflection_probe_runtime->FinalizeFrame("),
        )
        self.assertLess(
            self.frontend.index("native_interop->PreparePublishFrame("),
            self.frontend.index("reflection_probe_runtime->FinalizeFrame("),
        )
        self.assertLess(
            self.frontend.index("reflection_probe_runtime->FinalizeFrame("),
            self.frontend.index("native_interop->CommitPreparedFrame()"),
        )
        self.assertLess(
            self.frontend.index("native_interop->CommitPreparedFrame()"),
            self.frontend.index("submission_state.CommitPrepared(request)"),
        )
        self.assertIn("native_interop->AbortPreparedFrame()", self.frontend)
        self.assertLess(
            self.frontend.index("reflection_probe_runtime->Shutdown()"),
            self.frontend.index("root->destroySceneManager(scene_manager)"),
        )
        for token in (
            "native_execution_evidence",
            "successful_capture_count == 1U",
            "compositor_defined_in_code",
            "pbs_bound",
        ):
            self.assertIn(token, self.smoke)
        self.assertIn(
            "scene->mLastRQ = kOgreNextPccReservedRenderQueue;",
            self.frontend,
        )
        self.assertNotIn("createBasicWorkspaceDef(", self.frontend)

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
            "OgreNextRasterFeatureTier::STATIC_PBR_N1",
            "no calibrated physical-light adapter",
            "N1 does not support deformable geometry",
            "N1 does not support particles",
            "N1 materials must be completely texture free",
            "N1 renders exactly one colour view",
        ):
            self.assertIn(token, self.policy)
        self.assertIn("kOgreNextN1MaximumDirectionalLights = 0U", self.policy_header)

    def test_rt4_directional_light_mapping_is_bounded_and_exact(self) -> None:
        for token in (
            "kOgreNextRt4MaximumDirectionalLights = 1U",
            "kOgreNextRt4LuxToNativePowerScale = 1.0F / 1024.0F",
        ):
            self.assertIn(token, self.policy_header)
        for token in (
            "RT4/V1 admits at most one calibrated directional light",
            "light.type != LightType::DIRECTIONAL",
            "light.shadow_flags != 0U",
            "light.intensity * kOgreNextRt4LuxToNativePowerScale",
        ):
            self.assertIn(token, self.policy)
        for token in (
            "createLight()",
            "Ogre::Light::LT_DIRECTIONAL",
            "setPowerScale(",
            "kOgreNextRt4LuxToNativePowerScale",
            "getPowerScale()",
            "failed native readback",
            "destroyLight(iterator->first)",
        ):
            self.assertIn(token, self.frontend)
        self.assertLess(
            self.frontend.index("node->attachObject(light)"),
            self.frontend.index("light->setDirection(Ogre::Vector3"),
        )
        self.assertIn(
            '"directional_lux_to_native_power_scale"',
            RUNNER_PATH.read_text(encoding="utf-8"),
        )

    def test_pbr_mapping_uses_reviewed_brdf_and_live_getter_gate(self) -> None:
        self.assertNotIn("importUnity", self.frontend)
        for token in (
            "setBrdf(Ogre::PbsBrdf::Default)",
            "Ogre::HlmsPbsDatablock::MetallicWorkflow",
            "setDiffuse(",
            "setMetalness(",
            "setRoughness(",
            "setEmissive(",
            "setTwoSidedLighting(descriptor.double_sided, false)",
            "VerifyPbsMapping(*native.datablock, descriptor)",
            "datablock.getBrdf() != Ogre::PbsBrdf::Default",
            "datablock.getWorkflow() != Ogre::HlmsPbsDatablock::MetallicWorkflow",
            "datablock.getDiffuse()",
            "datablock.getMetalness()",
            "datablock.getRoughness()",
            "datablock.getEmissive()",
            "datablock.getTwoSidedLighting()",
        ):
            self.assertIn(token, self.frontend)

    def test_submission_and_cleanup_state_are_lifetime_exact_and_fault_latched(self) -> None:
        self.assertIn("std::map<std::uint64_t", self.policy_header)
        self.assertIn("std::weak_ptr<const SceneSnapshot>", self.policy_header)
        self.assertIn("snapshots_.find(snapshot_id)", self.policy)
        self.assertIn("owner_before", self.policy)
        self.assertIn("TrackedSnapshotIdentityCount", self.policy_header)
        self.assertIn("request.frame_id != last_frame_id_ + 1U", self.policy)
        self.assertIn("iterator->second.expired()", self.policy)
        self.assertNotIn(
            "std::shared_ptr<const SceneSnapshot>> snapshots_", self.policy_header
        )
        self.assertNotIn("completed_frame_ranges_", self.policy_header)
        self.assertIn("impl_->faulted = true", self.frontend)
        self.assertIn("return FrameCleanupFailure()", self.frontend)
        self.assertIn("fail_after_cleanup", self.frontend)
        self.assertIn("[[nodiscard]] bool DestroyCatalog()", self.frontend)
        self.assertIn("[[nodiscard]] bool CleanupBackend()", self.frontend)
        self.assertIn("if (!impl_->CleanupBackend())", self.frontend)
        self.assertIn("TryClaimOgreNextN1Root", self.frontend)
        self.assertIn("ReleaseOgreNextN1Root", self.frontend)
        self.assertIn("owns_root_claim", self.frontend)
        self.assertIn("FromOgreMatrix(reconstructed)", self.frontend)
        self.assertIn(
            "N1 reconstructed Ogre TRS can overflow native world bounds",
            self.frontend,
        )

    def test_rt4_v1_is_explicit_texture_backed_and_fail_closed(self) -> None:
        for token in (
            "MODERN_PBR_RT4_V1",
            "PFG_RGBA8_UNORM_SRGB",
            "PFG_R8_UNORM",
            "PFG_RG8_UNORM",
            "UploadedTextureChannel::GREEN",
            "UploadedTextureChannel::BLUE",
            "UploadedTextureChannel::NORMAL_RG",
            "waitForStreamingCompletion",
            "setTextureUvSource",
            "VerifySamplerMapping",
            "PendingTextureAllocation",
            "impl_->textures.swap(candidate_textures)",
            "NativeTextureUsage",
            "ReferencedTextureUsage",
            "existing->second.usage == referenced->second.usage",
            "allocated an unused sampled RGBA texture",
            "rejects aliases between sampled sRGB and packed linear",
            "QueryTextureAllocationAudit",
            "native_allocation_creates",
            "native_allocation_destroys",
            "live_native_allocations",
            "findTextureNoThrow",
            "texture_retired_name_rejections",
            "normal_rg8_allocations",
            "Ogre::PBSM_NORMAL",
            "setNormalMapWeight(1.0F)",
            "getNormalMapWeight() != 1.0F",
        ):
            self.assertIn(token, self.frontend)
        for token in (
            "kOgreNextRt4NormalDecodedQuantizationTolerance",
            "within exactly 1/255 decoded units",
            "rejects negative-Z normal texels",
            "normal_scale exactly one",
            "pinned HLMS PBS has no ambient-occlusion texture slot",
        ):
            self.assertIn(token, self.policy)
        self.assertIn("only the", self.policy)
        self.assertIn("texture/sampler pairs actually referenced", self.policy)
        self.assertIn("--modern-pbr", self.smoke)
        self.assertIn(
            "ror.ogre_next_frontend_rt4_pbr_v1_smoke.v2", self.smoke
        )
        self.assertIn(
            "ror_ogre_next_frontend_rt4_pbr_v1_runtime", self.entry_cmake
        )
        self.assertIn("--evidence", self.entry_cmake)
        self.assertIn("RequireControlledCatalog", self.smoke)
        self.assertIn("RequireControlledSceneAndView", self.smoke)
        self.assertIn("packed_green_roughness", self.smoke)
        self.assertIn("packed_blue_metallic", self.smoke)
        self.assertIn("sampler_address_over_uv0", self.smoke)
        self.assertIn("canonical_positive_z_normal_rg", self.smoke)
        self.assertIn("normal_RG8_UNORM", self.smoke)
        self.assertIn("unused_packed_rgba_allocations", self.smoke)
        self.assertIn("MakeRetirementTexture", self.smoke)
        self.assertIn("RunTextureRetirementProof", self.smoke)
        self.assertIn("4x2 two-mip padded-row", self.smoke)
        self.assertIn(
            "find_texture_no_throw_rejected_old_names", self.smoke
        )
        self.assertIn("isolated_from_visual_variants", self.smoke)
        retirement_catalog = self.smoke[
            self.smoke.index("RenderAssetDelta MakeRetirementCatalog") :
            self.smoke.index("RenderAssetDelta MakeCatalog")
        ]
        self.assertLess(
            retirement_catalog.index("RenderAssetKind::TEXTURE, 30U"),
            retirement_catalog.index("RenderAssetKind::SAMPLER, 31U"),
        )
        self.assertLess(
            retirement_catalog.index("RenderAssetKind::SAMPLER, 31U"),
            retirement_catalog.index("RenderAssetKind::MATERIAL, 32U"),
        )

        create_texture = self.frontend[
            self.frontend.index("NativeTexture CreateTexture(") :
            self.frontend.index("void VerifyTexture(")
        ]
        self.assertIn("if (usage.sampled_rgba)", create_texture)
        self.assertLess(
            create_texture.index("if (usage.sampled_rgba)"),
            create_texture.index("UploadedTextureChannel::RGBA"),
        )

        destroy_catalog = self.frontend[
            self.frontend.index("bool DestroyCatalog()") :
            self.frontend.index("bool DestroyFrameMeshes()")
        ]
        self.assertLess(
            destroy_catalog.index("DestroyMaterial"),
            destroy_catalog.index("DestroyTexture"),
        )

    def test_normal_map_audit_remediation_is_native_and_fail_closed(self) -> None:
        for token in (
            "HasEffectivelyUniformScale",
            "rejects non-uniform scale",
            "accurate_non_uniform_scaled_normals",
        ):
            self.assertIn(token, self.policy)
        for token in (
            "OgreNextN1NormalUploadAudit",
            "QueryNormalUploadAudit",
            "exact_source_rg_to_native_image",
            "verified_padded_source_rows",
        ):
            self.assertIn(token, self.header + self.frontend)
        for token in (
            "RunTangentHandednessProof",
            "positive tangent-w HDR Render",
            "negative tangent-w HDR Render",
            "non_uniform_scale_rejected_before_submission",
            "ror.ogre_next_rt4_tangent_handedness.v1",
        ):
            self.assertIn(token, self.smoke)

    def test_projection_and_device_extent_paths_fail_closed(self) -> None:
        self.assertIn("TryConvertPortableProjectionToOgreClip", self.policy_header)
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
            "ToOgreMatrix(converted_projection)",
            self.frontend,
        )
        self.assertIn("TryComputeReadbackLayout", self.frontend)
        self.assertLess(
            self.frontend.index("TryComputeReadbackLayout(validated_view.width"),
            self.frontend.index("createTexture(\n        target_name"),
        )
        self.assertEqual(self.frontend.count("createRenderWindow("), 1)

    def test_rt4_isolation_validator_is_exact_and_tamper_closed(self) -> None:
        names = (
            ("baseline", "none"),
            ("base_color", "base_color_rgb"),
            ("roughness_g", "packed_green_roughness"),
            ("metallic_b", "packed_blue_metallic"),
            ("emissive", "emissive_rgb"),
            ("normal_rg", "canonical_positive_z_normal_rg"),
            ("sampler_uv", "sampler_address_over_uv0"),
        )
        report: dict = {
            "texture_isolation": {
                "schema": "ror.ogre_next_rt4_texture_isolation.v1",
                "evidence_file": RUNNER.RT4_PBR_EVIDENCE_NAME,
                "width": 192,
                "height": 128,
                "geometry_identical": True,
                "material_factors_constants_identical": True,
                "camera_identical": True,
                "lights_identical": True,
                "ui_included": False,
                "variants": [],
            },
            "hdr": {},
            "sdr": {},
        }
        evidence = bytearray()
        baseline_blocks: dict[str, bytes] = {}
        for index, (name, changed_input) in enumerate(names):
            entry = {
                "name": name,
                "changed_input": changed_input,
                "asset_sequence": index + 1,
            }
            for label, bytes_per_pixel in (("hdr", 8), ("sdr", 4)):
                block = bytearray(192 * 128 * bytes_per_pixel)
                if index:
                    for pixel in range(64 + index):
                        block[pixel * bytes_per_pixel] = index
                block_bytes = bytes(block)
                if index == 0:
                    baseline_blocks[label] = block_bytes
                entry[label] = {
                    "offset": len(evidence),
                    "bytes": len(block_bytes),
                    "exact_fnv1a64": RUNNER._fnv1a64(block_bytes),
                    "changed_pixels_from_baseline": RUNNER._changed_pixels(
                        baseline_blocks[label], block_bytes, bytes_per_pixel
                    ),
                }
                evidence.extend(block_bytes)
            report["texture_isolation"]["variants"].append(entry)
        report["texture_isolation"]["evidence_bytes"] = len(evidence)
        handedness_offset = len(evidence)
        positive_hdr = bytes(192 * 128 * 8)
        positive_sdr = bytes(192 * 128 * 4)
        negative_hdr_bytes = bytearray(positive_hdr)
        negative_sdr_bytes = bytearray(positive_sdr)
        for pixel in range(64):
            negative_hdr_bytes[pixel * 8] = 1
            negative_sdr_bytes[pixel * 4] = 1
        negative_hdr = bytes(negative_hdr_bytes)
        negative_sdr = bytes(negative_sdr_bytes)
        handedness: dict = {}
        for sign, hdr_block, sdr_block in (
            ("positive", positive_hdr, positive_sdr),
            ("negative", negative_hdr, negative_sdr),
        ):
            handedness[sign] = {}
            for label, block in (("hdr", hdr_block), ("sdr", sdr_block)):
                handedness[sign][label] = {
                    "offset": len(evidence),
                    "bytes": len(block),
                    "exact_fnv1a64": RUNNER._fnv1a64(block),
                }
                evidence.extend(block)
        report["tangent_handedness"] = {
            "schema": "ror.ogre_next_rt4_tangent_handedness.v1",
            "evidence_file": RUNNER.RT4_PBR_EVIDENCE_NAME,
            "evidence_offset": handedness_offset,
            "evidence_bytes": len(evidence) - handedness_offset,
            "authored_tangent_format": "FLOAT4",
            "positive_tangent_w": 1,
            "negative_tangent_w": -1,
            "position_normal_tangent_xyz_uv0_identical": True,
            "material_camera_lights_identical": True,
            "ui_included": False,
            "positive": handedness["positive"],
            "negative": handedness["negative"],
            "hdr_changed_pixels": 64,
            "sdr_changed_pixels": 64,
        }
        report["hdr"]["exact_attachment_fnv1a64"] = RUNNER._fnv1a64(
            baseline_blocks["hdr"]
        )
        report["sdr"]["exact_attachment_fnv1a64"] = RUNNER._fnv1a64(
            baseline_blocks["sdr"]
        )
        with tempfile.TemporaryDirectory(prefix="ror-rt4-isolation-") as temp:
            path = Path(temp) / RUNNER.RT4_PBR_EVIDENCE_NAME
            path.write_bytes(evidence)
            RUNNER.validate_rt4_isolation_evidence(report, path)
            tampered = bytearray(evidence)
            tampered[-1] ^= 1
            path.write_bytes(tampered)
            with self.assertRaises(RUNNER.ProbeError):
                RUNNER.validate_rt4_isolation_evidence(report, path)

    def test_rt4_reflection_validator_is_cross_platform_and_tamper_closed(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-rt4-reflection-") as temp:
            path = Path(temp) / RUNNER.RT4_PBR_REFLECTION_EVIDENCE_NAME
            for policy_name in RUNNER.RT4_REFLECTION_BACKENDS:
                with self.subTest(policy=policy_name):
                    report, payload, policy = reflection_fixture(policy_name)
                    path.write_bytes(payload)
                    slices = RUNNER.validate_rt4_reflection_evidence(
                        report, path, policy
                    )
                    self.assertEqual(len(slices), 18)
                    self.assertEqual(slices[0]["texture"], "raw")
                    self.assertEqual(slices[6]["texture"], "filtered")
                    self.assertEqual(slices[-1]["mip"], 1)
                    self.assertEqual(
                        slices[-1]["offset"] + slices[-1]["bytes"],
                        RUNNER.RT4_REFLECTION_EVIDENCE_BYTES,
                    )

            report, payload, policy = reflection_fixture()
            cases: list[tuple[str, bytes, dict]] = []
            raw_tamper = bytearray(payload)
            raw_tamper[0] ^= 1
            cases.append(("raw hash", bytes(raw_tamper), report))
            filtered_tamper = bytearray(payload)
            filtered_tamper[RUNNER.RT4_REFLECTION_RAW_BYTES] ^= 1
            cases.append(("filtered hash", bytes(filtered_tamper), report))
            nonfinite = bytearray(payload)
            nonfinite[:8] = struct.pack("<4e", float("nan"), 1.0, 1.0, 1.0)
            cases.append(("non-finite", bytes(nonfinite), report))
            cases.append(("truncated", payload[:-1], report))
            cases.append(("trailing", payload + b"\x00", report))
            wrong_backend = copy.deepcopy(report)
            wrong_backend["reflection_probes"]["backend"] = "OGRE_NEXT_VULKAN"
            cases.append(("backend", payload, wrong_backend))
            for label, changed_payload, changed_report in cases:
                with self.subTest(tamper=label):
                    path.write_bytes(changed_payload)
                    with self.assertRaises(RUNNER.ProbeError):
                        RUNNER.validate_rt4_reflection_evidence(
                            changed_report, path, policy
                        )

            mip_one_start = (
                RUNNER.RT4_REFLECTION_RAW_BYTES
                + RUNNER.RT4_REFLECTION_RESOLUTION
                * RUNNER.RT4_REFLECTION_RESOLUTION
                * RUNNER.RT4_REFLECTION_FACE_COUNT
                * 8
            )
            missing_mip_one = payload[:mip_one_start] + bytes(
                len(payload) - mip_one_start
            )
            missing_report, _, _ = reflection_fixture(
                payload=missing_mip_one
            )
            path.write_bytes(missing_mip_one)
            with self.assertRaisesRegex(
                RUNNER.ProbeError, "subresource coverage"
            ):
                RUNNER.validate_rt4_reflection_evidence(
                    missing_report, path, policy
                )

    def test_real_smoke_covers_hdr_sdr_readback_and_recovery(self) -> None:
        for token in (
            "PixelFormat::RGBA16_FLOAT",
            "PixelFormat::RGBA8_SRGB",
            "HalfToFloat",
            "maximum_luminance <= 1.05F",
            "unsupported depth request",
            "post-reinitialize Render",
            "a second simultaneous frontend escaped Ogre Root ownership",
            "process_global_root_exclusion",
            "Ogre v2 Mesh plus immutable VertexArrayObject",
            "PbsBrdf::Default height-correlated GGX",
            "pbr_datablock_readback_verified",
        ):
            self.assertIn(token, self.smoke)
        self.assertIn("ror_ogre_next_frontend_n1_runtime", self.entry_cmake)
        self.assertIn("ror_ogre_next_frontend_n1_report", self.entry_cmake)

    def test_texture_upload_rollback_is_centralized_and_fault_injected(self) -> None:
        self.assertEqual(self.frontend.count("manager->destroyTexture(texture)"), 1)
        self.assertIn("RetireNativeTextureAllocation", self.frontend)
        self.assertIn("++texture_retired_name_lookups", self.frontend)
        self.assertIn("++texture_retired_name_rejections", self.frontend)
        self.assertIn("++texture_allocation_destroys", self.frontend)
        self.assertIn("if (!clean) {\n      faulted = true;", self.frontend)
        for stage in (
            "AFTER_CREATE",
            "AFTER_SET_RESOLUTION",
            "AFTER_SET_MIPMAPS",
            "AFTER_SET_PIXEL_FORMAT",
            "AFTER_SCHEDULE_TRANSITION",
        ):
            self.assertIn(stage, self.header)
            self.assertIn(stage, self.frontend)
        for proof in (
            "RunTextureUploadRollbackProof",
            "after_failure",
            "after_retry",
            "after_replacement",
            "after_shutdown",
            "clean_retry_replacement_shutdown",
        ):
            self.assertIn(proof, self.smoke)

    def test_rt4_attestation_flushes_file_and_parent_directory(self) -> None:
        runner = RUNNER_PATH.read_text(encoding="utf-8")
        self.assertIn("def _fsync_parent_directory", runner)
        self.assertIn("os.replace(temporary, path)", runner)
        self.assertIn("_fsync_parent_directory(path)", runner)
        self.assertIn("def _durable_unlink", runner)
        self.assertIn("_durable_unlink(attestation_path)", runner)
        self.assertIn("not-a-cryptographic-signature", runner)

        with tempfile.TemporaryDirectory(prefix="ror-rt4-durable-") as temp:
            root = Path(temp)
            target = root / "attestation.json"
            fsync_targets = []
            with mock.patch.object(
                RUNNER.os,
                "fsync",
                side_effect=lambda descriptor: fsync_targets.append(
                    RUNNER.os.fstat(descriptor).st_mode
                ),
            ):
                RUNNER._atomic_write_json(target, {"status": "pass"})
            self.assertEqual(
                len(fsync_targets), 1 if RUNNER.os.name == "nt" else 2
            )
            self.assertTrue(target.is_file())

            fsync_targets.clear()
            with mock.patch.object(
                RUNNER.os,
                "fsync",
                side_effect=lambda descriptor: fsync_targets.append(
                    RUNNER.os.fstat(descriptor).st_mode
                ),
            ):
                RUNNER._durable_unlink(target)
            self.assertEqual(
                len(fsync_targets), 0 if RUNNER.os.name == "nt" else 1
            )
            self.assertFalse(target.exists())

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
                "ror_repository": RUNNER.ROR_SOURCE_REPOSITORY,
                "ror_ref": "codex/test",
                "ror_commit": "1" * 40,
                "ror_relevant_source_manifest_sha256": "5" * 64,
                "ror_relevant_source_manifest_file_count": 123,
                "ogre_next_commit": lock["commit"],
                "ogre_next_archive_sha256": lock["archive_sha256"],
                "normal_map_source_lock_sha256": (
                    RUNNER.NORMAL_MAP_SOURCE_LOCK_SHA256
                ),
                "shader_media_root": lock["shader_media"]["root"],
                "shader_media_license_expression": lock["shader_media"][
                    "license_expression"
                ],
                "shader_media_notice_sha256": lock["shader_media"][
                    "third_party_notice"
                ]["notice_sha256"],
                "shader_media_manifest_sha256": "2" * 64,
                "shader_media_manifest_file_count": 107,
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
                "runtime_media_root": "explicit_absolute",
                "package_media_relative_path": (
                    "share/rigsofrods/ogre-next/Samples/Media"
                ),
                "relocated_executable": True,
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
                "double_sided_pbs_readback": True,
                "lifetime_snapshot_identity_replay": True,
                "lifetime_completed_frame_queries": True,
                "process_global_root_exclusion": True,
                "shutdown_reinitialize_render_shutdown": True,
            },
        }
        with tempfile.TemporaryDirectory(prefix="ror-n1-validator-") as temp:
            image = Path(temp) / "n1.ppm"
            image.write_bytes(b"P6\n192 128\n255\n" + pixels)
            manifest = {"sha256": "2" * 64, "file_count": 107}
            source_identity = {
                "repository": RUNNER.ROR_SOURCE_REPOSITORY,
                "ref": "codex/test",
                "commit": "1" * 40,
                "relevant_manifest_sha256": "5" * 64,
                "relevant_manifest_file_count": 123,
            }
            RUNNER.validate_n1_checkpoint(
                report, image, lock, policy, manifest, source_identity
            )
            invalid = copy.deepcopy(report)
            invalid["hdr"]["maximum_luminance"] = 1.0
            with self.assertRaises(RUNNER.ProbeError):
                RUNNER.validate_n1_checkpoint(
                    invalid, image, lock, policy, manifest, source_identity
                )

    def test_wrapper_makes_n1_artifacts_mandatory(self) -> None:
        lock = RUNNER.load_lock()
        policy = RUNNER.detect_policy("Darwin", "arm64")
        source_identity = {
            "repository": RUNNER.ROR_SOURCE_REPOSITORY,
            "ref": "codex/test",
            "commit": "1" * 40,
            "relevant_manifest_sha256": "5" * 64,
            "relevant_manifest_file_count": 123,
        }
        with tempfile.TemporaryDirectory(prefix="ror-n1-orchestration-") as temp:
            with mock.patch.object(RUNNER, "run") as run:
                with mock.patch.object(
                    RUNNER, "require_source_identity_unchanged"
                ) as unchanged:
                    with self.assertRaisesRegex(
                        RUNNER.ProbeError, "did not produce required artifacts"
                    ):
                        RUNNER.run_n1_checkpoint(
                            Path(temp), "Release", 2, lock, policy,
                            source_identity,
                        )
                self.assertEqual(run.call_count, 1)
                unchanged.assert_called_once_with(source_identity)

    def test_package_license_bundle_is_hash_validated(self) -> None:
        lock = RUNNER.load_lock()
        ror_hash = "1" * 64
        with tempfile.TemporaryDirectory(prefix="ror-n1-package-") as temp:
            package = Path(temp) / RUNNER.N1_PACKAGE_NAME / "licenses"
            package.mkdir(parents=True)
            paths = {
                "Rigs-of-Rods-GPL-3.0.txt": ror_hash,
                "Ogre-Next-MIT.txt": lock["license"]["sha256"],
                "RapidJSON-license.txt": lock["dependencies"]["rapidjson"][
                    "license_sha256"
                ],
                "LicenseRef-Heitz-LTC-Paper-Notice.txt": lock[
                    "shader_media"
                ]["third_party_notice"]["notice_sha256"],
                "IBLBaker.txt": lock["reflection_shader_media"][
                    "third_party_notice"
                ]["source_sha256"],
            }
            for name in paths:
                (package / name).write_text(name, encoding="utf-8")

            def fake_sha256(path: Path) -> str:
                if path == REPOSITORY_ROOT / "COPYING":
                    return ror_hash
                return paths[path.name]

            manifest = {
                "sha256": "3" * 64,
                "file_count": 107,
                "entries": [("file", 1, "4" * 64)],
            }
            with mock.patch.object(
                RUNNER, "sha256_file", side_effect=fake_sha256
            ), mock.patch.object(
                RUNNER, "shader_media_manifest", return_value=manifest
            ):
                self.assertEqual(
                    RUNNER.validate_n1_package(Path(temp), lock), manifest
                )
                (package / "RapidJSON-license.txt").unlink()
                with self.assertRaisesRegex(
                    RUNNER.ProbeError, "missing licenses/RapidJSON-license.txt"
                ):
                    RUNNER.validate_n1_package(Path(temp), lock)

    def test_shader_media_manifest_is_path_size_and_sha_exact(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-n1-media-") as temp:
            root = Path(temp)
            (root / "A").mkdir()
            (root / "A" / "one.any").write_bytes(b"one")
            (root / "two.any").write_bytes(b"two")
            manifest = RUNNER.shader_media_manifest(root)
            self.assertEqual(manifest["file_count"], 2)
            self.assertEqual(
                [entry[0] for entry in manifest["entries"]],
                ["A/one.any", "two.any"],
            )
            original_digest = manifest["sha256"]
            (root / "two.any").write_bytes(b"changed")
            self.assertNotEqual(
                RUNNER.shader_media_manifest(root)["sha256"], original_digest
            )


if __name__ == "__main__":
    unittest.main()
