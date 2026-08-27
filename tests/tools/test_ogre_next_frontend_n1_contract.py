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
UV_AFFINE_SHADER_VERIFIER_PATH = (
    PROBE_ROOT / "verify_uv0_affine_pbs_shader.py"
)
UV_AFFINE_SHADER_VERIFIER_SPEC = importlib.util.spec_from_file_location(
    "verify_uv0_affine_pbs_shader_for_n1_tests",
    UV_AFFINE_SHADER_VERIFIER_PATH,
)
assert (
    UV_AFFINE_SHADER_VERIFIER_SPEC
    and UV_AFFINE_SHADER_VERIFIER_SPEC.loader
)
UV_AFFINE_SHADER_VERIFIER = importlib.util.module_from_spec(
    UV_AFFINE_SHADER_VERIFIER_SPEC
)
UV_AFFINE_SHADER_VERIFIER_SPEC.loader.exec_module(
    UV_AFFINE_SHADER_VERIFIER
)
AERIAL_HAZE_SHADER_VERIFIER_PATH = (
    PROBE_ROOT / "verify_aerial_haze_shader.py"
)
AERIAL_HAZE_SHADER_VERIFIER_SPEC = importlib.util.spec_from_file_location(
    "verify_aerial_haze_shader_for_n1_tests",
    AERIAL_HAZE_SHADER_VERIFIER_PATH,
)
assert (
    AERIAL_HAZE_SHADER_VERIFIER_SPEC
    and AERIAL_HAZE_SHADER_VERIFIER_SPEC.loader
)
AERIAL_HAZE_SHADER_VERIFIER = importlib.util.module_from_spec(
    AERIAL_HAZE_SHADER_VERIFIER_SPEC
)
AERIAL_HAZE_SHADER_VERIFIER_SPEC.loader.exec_module(
    AERIAL_HAZE_SHADER_VERIFIER
)


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
                "version": 4,
                "successful_capture_count": 1,
                "failed_capture_count": 0,
                "live_probe_count": 1,
                "probe_resolution": RUNNER.RT4_REFLECTION_RESOLUTION,
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
        cls.tamper_cmake = (
            PROBE_ROOT / "cmake" / "VerifyN1MediaTamper.cmake"
        ).read_text(encoding="utf-8")

        cls.header = (
            RENDER_ROOT / "ogrenext" / "OgreNextN1Frontend.h"
        ).read_text(encoding="utf-8")
        cls.hdr_topology_header = (
            RENDER_ROOT / "ogrenext" / "OgreNextHdrSceneTopology.h"
        ).read_text(encoding="utf-8")
        cls.frontend = (
            RENDER_ROOT / "ogrenext" / "OgreNextN1Frontend.cpp"
        ).read_text(encoding="utf-8")
        cls.frontend_api = (
            RENDER_ROOT / "RendererFrontend.h"
        ).read_text(encoding="utf-8")
        cls.frontend_base = (
            RENDER_ROOT / "RendererFrontend.cpp"
        ).read_text(encoding="utf-8")
        cls.display_domain_unlit_header = (
            RENDER_ROOT
            / "ogrenext"
            / "OgreNextDisplayDomainUnlit.h"
        ).read_text(encoding="utf-8")
        cls.display_domain_unlit_source = (
            RENDER_ROOT
            / "ogrenext"
            / "OgreNextDisplayDomainUnlit.cpp"
        ).read_text(encoding="utf-8")
        cls.display_domain_piece = (
            PROBE_ROOT
            / "media"
            / "Hlms"
            / "RoR"
            / "DisplayDomain"
            / "DisplayDomain_piece_ps.any"
        ).read_text(encoding="utf-8")
        cls.uv_affine_header = (
            RENDER_ROOT / "ogrenext" / "OgreNextUvAffinePbs.h"
        ).read_text(encoding="utf-8")
        cls.uv_affine_source = (
            RENDER_ROOT / "ogrenext" / "OgreNextUvAffinePbs.cpp"
        ).read_text(encoding="utf-8")
        cls.uv_affine_piece = (
            PROBE_ROOT
            / "media"
            / "Hlms"
            / "RoR"
            / "UvAffinePbs"
            / "UvAffinePbs_piece_ps.any"
        ).read_text(encoding="utf-8")
        cls.media_integrity = (
            RENDER_ROOT / "ogrenext" / "OgreNextN1MediaIntegrity.cpp"
        ).read_text(encoding="utf-8")
        cls.screen_shade_hlsl = (
            PROBE_ROOT
            / "media/2.0/scripts/materials/RoRHaze/HLSL"
            / "RoRScreenShadeAO_ps.hlsl"
        ).read_text(encoding="utf-8")
        cls.policy = (
            RENDER_ROOT / "ogrenext" / "OgreNextN1Policy.cpp"
        ).read_text(encoding="utf-8")
        cls.policy_header = (
            RENDER_ROOT / "ogrenext" / "OgreNextN1Policy.h"
        ).read_text(encoding="utf-8")
        cls.render_math = (
            RENDER_ROOT / "RenderMath.h"
        ).read_text(encoding="utf-8")
        cls.ogre14_scene_source = (
            RENDER_ROOT / "Ogre14GraphicsSceneSource.cpp"
        ).read_text(encoding="utf-8")
        cls.ogre14_scene_source_header = (
            RENDER_ROOT / "Ogre14GraphicsSceneSource.h"
        ).read_text(encoding="utf-8")
        cls.gfx_scene = (
            REPOSITORY_ROOT / "source" / "main" / "gfx" / "GfxScene.cpp"
        ).read_text(encoding="utf-8")
        cls.reflection_header = (
            RENDER_ROOT / "ogrenext" / "OgreNextReflectionProbeRuntime.h"
        ).read_text(encoding="utf-8")
        cls.reflection_runtime = (
            RENDER_ROOT / "ogrenext" / "OgreNextReflectionProbeRuntime.cpp"
        ).read_text(encoding="utf-8")
        cls.pssm_policy = (
            RENDER_ROOT / "ogrenext" / "OgreNextPssmShadowPolicy.cpp"
        ).read_text(encoding="utf-8")
        cls.smoke = (
            PROBE_ROOT / "src" / "frontend_n1_smoke.cpp"
        ).read_text(encoding="utf-8")

    def test_uv0_affine_pbs_shader_lock_rejects_mutation(self) -> None:
        shader_relative = Path(
            "media/Hlms/RoR/UvAffinePbs/UvAffinePbs_piece_ps.any"
        )
        source_shader = PROBE_ROOT / shader_relative
        source_lock = (
            PROBE_ROOT / "ogre-next-uv0-affine-pbs-v1.lock.json"
        )
        with tempfile.TemporaryDirectory(prefix="ror-uv-affine-lock-") as temp:
            root = Path(temp)
            shader = root / shader_relative
            shader.parent.mkdir(parents=True)
            shader.write_bytes(source_shader.read_bytes())
            lock = root / source_lock.name
            lock.write_bytes(source_lock.read_bytes())
            self.assertEqual(
                UV_AFFINE_SHADER_VERIFIER.verify_shader(root, lock),
                "3d60e9a0e30f2c4490de0b9f88406c9ce9a32acfaff01f308ec5f64922de3159",
            )
            shader.write_bytes(shader.read_bytes() + b"\n")
            with self.assertRaisesRegex(ValueError, "digest mismatch"):
                UV_AFFINE_SHADER_VERIFIER.verify_shader(root, lock)

        self.assertIn(
            "ogre-next-uv0-affine-pbs-v1.lock.json", self.entry_cmake
        )
        self.assertIn(
            "verify_uv0_affine_pbs_shader.py", self.entry_cmake
        )

    def test_vulkan_shader_root_layout_covers_declared_resource_slots(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-vulkan-root-layout-") as temp:
            root = Path(temp)
            material = root / "Resolve.material"
            shader = root / "GLSL" / "Resolve_ps.glsl"
            shader.parent.mkdir(parents=True)
            shader.write_text(
                "vulkan_layout( ogre_t0 ) uniform texture2D currentTexture;\n"
                "vulkan_layout( ogre_t4 ) uniform texture2D historyTexture;\n"
                "vulkan( layout( ogre_s4 ) uniform sampler historySampler );\n",
                encoding="utf-8",
            )
            standard_program = (
                "fragment_program Test/Resolve_ps_VK glslvk\n"
                "{\n"
                "  source Resolve_ps.glsl\n"
                "}\n"
            )
            material.write_text(standard_program, encoding="utf-8")
            with self.assertRaisesRegex(SystemExit, "standard exposes 4 slots"):
                AERIAL_HAZE_SHADER_VERIFIER.validate_vulkan_root_layout_capacity(
                    root, material
                )

            material.write_text(
                standard_program.replace(
                    "  source Resolve_ps.glsl\n",
                    "  source Resolve_ps.glsl\n  root_layout high\n",
                ),
                encoding="utf-8",
            )
            AERIAL_HAZE_SHADER_VERIFIER.validate_vulkan_root_layout_capacity(
                root, material
            )

        temporal_material = (
            PROBE_ROOT
            / "media/2.0/scripts/materials/RoRHaze/RoRTemporalAa.material"
        ).read_text(encoding="utf-8")
        for token in (
            "vertex_program RoR/HDR/TaaResolveQuad_vs_VK glslvk",
            "vertex_program RoR/HDR/TaaResolveQuad_vs unified",
            "delegate RoR/HDR/TaaResolveQuad_vs_VK",
            "fragment_program RoR/HDR/TaaResolve_ps_VK glslvk",
            "vertex_program_ref RoR/HDR/TaaResolveQuad_vs",
        ):
            self.assertIn(token, temporal_material)
        self.assertEqual(temporal_material.count("root_layout high"), 2)

    def test_d3d11_screen_shade_keeps_runtime_tier_loops_dynamic(self) -> None:
        self.assertEqual(self.screen_shade_hlsl.count("[loop]"), 2)
        self.assertIn(
            "[loop]\n\t\t\tfor( int i = 0; i < sampleCount; ++i )",
            self.screen_shade_hlsl,
        )
        self.assertIn(
            "[loop]\n\t\tfor( int i = 0; i < contactSteps; ++i )",
            self.screen_shade_hlsl,
        )

    def test_dependency_policy_is_shared_pinned_and_isolated(self) -> None:
        self.assertIn("cmake/PinnedOgreNext.cmake", self.entry_cmake)
        self.assertIn("37149a802de747f6806996fa3067b0748ecc1084", self.pinned_cmake)
        self.assertIn("URL_HASH \"SHA256=${ROR_OGRE_NEXT_ARCHIVE_SHA256}\"", self.pinned_cmake)
        self.assertIn(
            "if (TARGET OgreMain AND NOT ROR_OGRE_NEXT_EMBEDDED_ROOT_PROVIDER)",
            self.pinned_cmake,
        )
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

    def test_scene_free_bootstrap_policy_defaults_fail_closed(self) -> None:
        self.assertIn(
            "virtual RenderOperationResult PresentBootstrapFrame();",
            self.frontend_api,
        )
        default = self.frontend_base[
            self.frontend_base.index(
                "IRendererFrontend::PresentBootstrapFrame()"
            ) : self.frontend_base.index(
                "bool IsKnownRendererFrontendKind"
            )
        ]
        self.assertIn("RenderOperationCode::UNSUPPORTED", default)
        self.assertIn("scene-free startup presentation", default)
        self.assertIn(
            "RenderOperationResult PresentBootstrapFrame() override;",
            self.header,
        )
        implementation = self.frontend[
            self.frontend.index(
                "OgreNextN1Frontend::PresentBootstrapFrame()"
            ) : self.frontend.index(
                "OgreNextN1Frontend::UpdateSurface("
            )
        ]
        self.assertIn("ProductionPresentationEnabled()", implementation)
        self.assertIn("EnsureBootstrapPresentationGraph()", implementation)

    def test_public_boundary_contains_no_ogre_types(self) -> None:
        self.assertNotRegex(self.header, r'#include\s+[<"]Ogre(?!Next)')
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
            "FreeType-GPLv2.txt",
            "FreeType-LICENSE.txt",
            "LicenseRef-Heitz-LTC-Paper-Notice.txt",
            "IBLBaker.txt",
        ):
            self.assertIn(
                license_name, self.entry_cmake + self.pinned_cmake
            )
        self.assertIn("validate_n1_package", RUNNER_PATH.read_text(encoding="utf-8"))
        self.assertIn("ror_ogre_next_frontend_n1_media_tamper", self.entry_cmake)
        self.assertIn("ror_ogre_next_frontend_hdr_media_tamper", self.entry_cmake)
        self.assertIn("VerifyN1MediaTamper.cmake", self.entry_cmake)
        self.assertIn(
            '--compositor-evidence "${N1_WORK_ROOT}/tamper-hdr.bin"',
            self.tamper_cmake,
        )
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
        self.assertIn(".stage-v12", self.entry_cmake)
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
            "pcc_created",
            "DestroyUncommittedPcc",
            "owns_automatic_ibl_mipmap_policy",
            "resetIblSpecMipmap(0U)",
            "OgreNextReflectionProbeNativeOwnershipEvidence",
            "QueryNativeOwnershipEvidence",
            "is_nothrow_destructible<Ogre::ParallaxCorrectedCubemap>",
            "filtered_nonzero_rgb_component_count",
            "last_probe_resolution",
            "numeric_limits<std::uint8_t>::max",
            "static_cast<std::uint8_t>(mip)",
            "view.inverseAffine().getTrans()",
            "owner_thread",
        ):
            self.assertIn(
                token, self.reflection_header + self.reflection_runtime
            )
        self.assertNotIn("setPriority(", self.reflection_runtime)
        self.assertNotIn("audit.pcc_enabled = true", self.reflection_runtime)
        self.assertEqual(self.reflection_runtime.count("resetIblSpecMipmap(0U)"), 1)
        self.assertIn(
            "reflection_configuration.owns_automatic_ibl_mipmap_policy = true",
            self.frontend,
        )
        destroy_uncommitted = self.reflection_runtime.split(
            "bool DestroyUncommittedPcc() noexcept", 1
        )[1].split("bool AbortLocalPlan", 1)[0]
        self.assertLess(
            destroy_uncommitted.index("delete retired"),
            destroy_uncommitted.index("ResetAutomaticIblMipmapPolicy()"),
        )
        self.assertLess(
            self.reflection_runtime.index(
                "scheduler.Commit(pending->plan_id, pending->receipts)"
            ),
            self.reflection_runtime.index("states.swap(published->candidate_states)"),
        )
        self.assertLess(
            self.reflection_runtime.index(
                "scheduler.Commit(pending->plan_id, pending->receipts)"
            ),
            self.reflection_runtime.index("audit.pcc_enabled = pcc != nullptr"),
        )
        self.assertLess(
            self.reflection_runtime.index("states.swap(published->candidate_states)"),
            self.reflection_runtime.index(
                "audit.native_execution_evidence ="
            ),
        )
        self.assertLess(
            self.frontend.index("reflection_probe_runtime->PrepareFrame("),
            self.frontend.index("createTexture(\n        target_text"),
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
        # Lifecycle completion must precede the runtime's own reset.
        self.assertLess(
            self.frontend.index("RetireProbesForSceneGeneration()"),
            self.frontend.index("reflection_probe_runtime->ResetSceneGeneration()"),
        )
        # The fail-closed post-condition must not be deleted.
        self.assertIn(
            "final empty scene did not retire all reflection probes",
            self.reflection_runtime,
        )
        # Native destruction stays at the single deferred-drain site.
        self.assertLess(
            self.reflection_runtime.index("RetireProbesForSceneGeneration()"),
            self.reflection_runtime.index(
                "retired probes could not be destroyed at scene reset"
            ),
        )
        # Black-capture guard: the retirement entry point must open no scheduler
        # frame, take no capture, and require no camera.
        start = self.reflection_runtime.index(
            "RenderOperationResult RetireProbesForSceneGeneration()"
        )
        end = self.reflection_runtime.index(
            "RenderOperationResult ResetSceneGeneration()", start
        )
        body = self.reflection_runtime[start:end]
        prepared_guard = body.index("if (pending != nullptr)")
        deferred_guard = body.index("if (in_flight != nullptr")
        cancellation = body.index("CancelInFlightCapture()")
        residual_guard = body.index("if (scheduler.has_pending_plan())")
        unbind = body.index("SetPbsBinding(false)")
        self.assertLess(prepared_guard, deferred_guard)
        self.assertLess(deferred_guard, cancellation)
        self.assertLess(cancellation, residual_guard)
        self.assertLess(residual_guard, unbind)
        self.assertIn(
            "deferred reflection capture could not be canceled at probe retirement",
            body,
        )
        for forbidden in (
            "scheduler.BeginFrame",
            "pcc->updateAllDirtyProbes",
            "tracking_camera",
        ):
            self.assertNotIn(forbidden, body)
        # HDR frame identity must be both checked and applied on the retire path.
        self.assertIn("CanAccountRetiredFrame", self.frontend)
        self.assertIn("AccountRetiredFrame", self.frontend)

    def test_native_mesh_path_uses_v2_vao_not_manual_object(self) -> None:
        create_mesh = self.frontend[
            self.frontend.index("NativeMesh CreateMesh(") :
            self.frontend.index("void MaybeInjectTextureUploadFailure(")
        ]
        for token in (
            "createVertexBuffer(",
            "createIndexBuffer(",
            "createVertexArrayObject(",
            "Ogre::MeshManager::getSingleton().createManual(",
            "Ogre::SubMesh *submesh",
        ):
            self.assertIn(token, create_mesh)
        self.assertNotIn("Ogre::ManualObject", create_mesh)
        self.assertLess(
            create_mesh.index("destroyVertexArrayObject(vao)"),
            create_mesh.index("destroyVertexBuffer(vertex_buffer)"),
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
            "report.supports_dynamic_mesh_updates = true",
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

    def test_dynamic_mesh_uses_stable_ogre_next_storage_and_replay_exact(self) -> None:
        for token in (
            "RunDynamicMeshProof",
            "MakeDynamicCatalog",
            "MakeDynamicScene",
            "DynamicMeshUpdateDescriptor update",
            "synchronous_full_frame_owned",
            "persistent Ogre-Next deformation was not visible, stable, and deterministic",
            "ror.ogre_next_dynamic_mesh.v2",
            "base_exact_replay",
            "deformed_exact_replay",
            "persistent_vertex_storage_exact",
        ):
            self.assertIn(token, self.smoke)
        for token in (
            "Ogre::BT_DYNAMIC_PERSISTENT",
            "UpdateDynamicMeshVertexBuffer",
            "apply_deformed_instance_update",
            "dynamic_vertex_storage",
            "Ogre::UO_KEEP_PERSISTENT",
            "record.item->setLocalAabb(updated_mesh_bounds)",
            "persistent deformation Item bounds failed native readback",
            "!impl_->native_interop",
            "frame_meshes.push_back(std::move(record.deformed_mesh))",
        ):
            self.assertIn(token, self.frontend)

    def test_indirect_alpha_shader_is_staged_into_the_exact_n1_package(self) -> None:
        for token in (
            "ROR_OGRE_NEXT_INDIRECT_ALPHA_MEDIA_SOURCE",
            "${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT}/Hlms/RoR/IndirectAlpha",
            "${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT}/Hlms/"
            "${ROR_OGRE_NEXT_INDIRECT_ALPHA_MEDIA_RELATIVE}",
        ):
            self.assertIn(token, self.entry_cmake)
        self.assertIn(
            "(INDIRECT_ALPHA_MEDIA_RELATIVE, INDIRECT_ALPHA_MEDIA_PATH)",
            RUNNER_PATH.read_text(encoding="utf-8"),
        )
        self.assertIn('"dynamic_meshes"', RUNNER_PATH.read_text(encoding="utf-8"))

    def test_rt4_directional_light_mapping_is_bounded_and_exact(self) -> None:
        for token in (
            "kOgreNextRt4MaximumDirectionalLights = 1U",
            "kOgreNextRt4LuxToNativePowerScale = 1.0F / 1024.0F",
        ):
            self.assertIn(token, self.policy_header)
        combined_policy = self.policy + self.pssm_policy
        for token in (
            "RT4/V1 admits at most one calibrated directional light",
            # Stage 2: point/spot lights are admitted through Forward+; the
            # per-type bounds and the shadowless local contract replace the
            # old directional-only refusal.
            "light.type != LightType::POINT && light.type != LightType::SPOT",
            "kOgreNextRt4MaximumLocalLights",
            "RT4/V1 local lights do not substitute shadow maps",
            "PSSM_3_CASCADE_V1 does not substitute local-light shadows",
            "shadow_flags != 0U",
            "light.intensity * kOgreNextRt4LuxToNativePowerScale",
        ):
            self.assertIn(token, combined_policy)
        for token in (
            "createLight()",
            "Ogre::Light::LT_DIRECTIONAL",
            "setPowerScale(",
            "kOgreNextRt4LuxToNativePowerScale",
            "getPowerScale()",
            "failed native readback",
            "destroyLight(iterator->light)",
        ):
            self.assertIn(token, self.frontend)
        self.assertLess(
            self.frontend.index("record.node->attachObject(record.light)"),
            # Stage 2: light direction is applied as an absolute node
            # orientation (the incremental Light::setDirection accumulates
            # quaternion drift on per-frame direction changes).
            self.frontend.index("Ogre::Vector3::NEGATIVE_UNIT_Z.getRotationTo("),
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
            "Ogre::HlmsPbsDatablock::SpecularWorkflow",
            "MaterialPbrWorkflow::SPECULAR",
            "setDiffuse(",
            "setSpecular(",
            "setMetalness(",
            "setRoughness(",
            "setEmissive(",
            "setIndexOfRefraction(",
            "getFresnel().x",
            "hasSeparateFresnel()",
            "setTwoSidedLighting(descriptor.double_sided, false)",
            "BuildPbsMacroblock(descriptor)",
            "BuildPbsBlendblock(descriptor)",
            "MaterialBlendMode::STRAIGHT_SOURCE_OVER",
            "MaterialBlendMode::LEGACY_STRAIGHT_ALPHA",
            "MaterialAlphaTestMode::GREATER",
            "Ogre::CMPF_GREATER_EQUAL",
            "VerifyPbsMapping(*native.pbs_datablock, descriptor)",
            "datablock.getBrdf() == Ogre::PbsBrdf::Default",
            "datablock.getWorkflow() ==",
            "datablock.getDiffuse()",
            "datablock.getSpecular()",
            "datablock.getMetalness()",
            "datablock.getRoughness()",
            "datablock.getEmissive()",
            "datablock.getTwoSidedLighting()",
            "datablock.getAlphaTest()",
            "datablock.getMacroblock()",
            "datablock.getBlendblock()",
            "Ogre::PBSM_SPECULAR",
        ):
            self.assertIn(token, self.frontend)

    def test_specular_ior_152_reaches_native_f0_and_exact_readback(self) -> None:
        expected_f0 = ((1.0 - 1.52) / (1.0 + 1.52)) ** 2
        self.assertAlmostEqual(expected_f0, 0.04257999496094735)
        self.assertIn(
            "Ogre::Vector3(descriptor.index_of_refraction), false",
            self.frontend,
        )
        self.assertIn(
            "(1.0F - descriptor.index_of_refraction) /",
            self.frontend,
        )
        self.assertIn(
            "(1.0F + descriptor.index_of_refraction)",
            self.frontend,
        )
        self.assertIn(
            "datablock.getFresnel().x, expected_fresnel",
            self.frontend,
        )
        self.assertIn("datablock.hasSeparateFresnel()", self.frontend)

    def test_thin_slab_keeps_texture_alpha_disabled_after_alpha_test_setup(
        self,
    ) -> None:
        self.assertIn(
            "false, !thin_slab_transmission);",
            self.frontend,
        )
        self.assertIn(
            "datablock.getUseAlphaFromTextures() == !thin_slab_transmission",
            self.frontend,
        )

    def test_specular_sampler_participates_in_exact_device_limit_gate(self) -> None:
        policy = (RENDER_ROOT / "ogrenext" / "OgreNextN1Policy.cpp").read_text(
            encoding="utf-8"
        )
        for token in (
            "ValidateOgreNextN1SamplerDeviceLimits(",
            "&material->specular_texture",
            "sampler->maximum_anisotropy > maximum_anisotropy",
            "instead of permitting backend clamping",
        ):
            self.assertIn(token, policy)
        self.assertIn(
            "ValidateOgreNextN1SamplerDeviceLimits(", self.frontend
        )
        self.assertIn("ToOgreFilter(descriptor.mip_filter, false)", self.frontend)

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
        self.assertIn(
            "destroy_definitions_and_resources && compositor_manager != nullptr",
            self.frontend,
        )
        shadow_cleanup = self.frontend[
            self.frontend.index(
                "Ogre::CompositorManager2 *const compositor_manager ="
            ) : self.frontend.index(
                "hdr_shadow_node_definition_created = false;",
                self.frontend.index(
                    "Ogre::CompositorManager2 *const compositor_manager ="
                ),
            )
        ]
        self.assertNotIn("root->getCompositorManager2()->", shadow_cleanup)
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
            "rejects aliases among decode-before-filter sRGB",
            "QueryTextureAllocationAudit",
            "native_allocation_creates",
            "native_allocation_destroys",
            "live_native_allocations",
            "findTextureNoThrow",
            "texture_retired_name_rejections",
            "normal_rg8_allocations",
            "Ogre::PBSM_NORMAL",
            "setNormalMapWeight(1.0F)",
            "getNormalMapWeight() == 1.0F",
        ):
            self.assertIn(token, self.frontend)
        self.assertIn("allocations.version == 2U", self.smoke)
        self.assertIn("result.texture_allocations.version == 2U", self.smoke)
        self.assertNotIn("allocations.version == 1U", self.smoke)
        self.assertNotIn("result.texture_allocations.version == 1U", self.smoke)
        self.assertNotIn("audit.version == 1U", self.smoke)
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
            "ror.ogre_next_frontend_rt4_pbr_v1_smoke.v4", self.smoke
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
        color_channel_start = create_texture.index(
            "const UploadedTextureChannel color_channel ="
        )
        color_channel = create_texture[
            color_channel_start : create_texture.index(
                ";", color_channel_start
            )
        ]
        self.assertIn(
            "block_compressed ? UploadedTextureChannel::BLOCK_COMPRESSED",
            color_channel,
        )
        self.assertIn(": UploadedTextureChannel::RGBA", color_channel)

        sampled_rgba_start = create_texture.index("if (usage.sampled_rgba)")
        sampled_rgba = create_texture[
            sampled_rgba_start : create_texture.index(
                "if (usage.display_domain_rgba)", sampled_rgba_start
            )
        ]
        self.assertIn("native.sampled = CreateUploadedTexture(", sampled_rgba)
        self.assertIn(
            "descriptor, native.sampled_name, color_channel", sampled_rgba
        )

        destroy_catalog = self.frontend[
            self.frontend.index("bool DestroyCatalog()") :
            self.frontend.index("bool DestroyFrameMeshes()")
        ]
        self.assertLess(
            destroy_catalog.index("DestroyMaterial"),
            destroy_catalog.index("DestroyTexture"),
        )

    def test_rt4_v1_analytic_sky_is_live_native_and_transactional(self) -> None:
        for token in (
            "kOgre14ModernAnalyticSkyPolicyVersion = 4U",
            "SkyX's native shader is azimuth-dependent",
            "BuildOgre14GraphicsSceneAnalyticSkyEnvironment",
            "joined live",
            "kOgre14ModernAnalyticSunAngularRadiusRadians",
            "kOgre14ModernAnalyticSkyCloudCoverageDaylightFraction",
            "kOgre14ModernAnalyticSkyCloudPhaseRadiansPerSecond",
            "kOgre14ModernAnalyticSkyHazeVisibilityMeters",
            "kOgre14ModernAnalyticSkyHazeNightFraction",
            "kOgre14ModernAnalyticSkyHazeScaleHeightMeters",
        ):
            self.assertIn(token, self.ogre14_scene_source_header)
        for token in (
            "native_sun_scale",
            "sun_height",
            "kNightZenith",
            "kDayHorizon",
            "kSunDiskScale",
            "candidate.analytic_sky = sky",
            "environment = candidate",
            "kKoschmiederContrastThresholdLog",
            "sky.haze_extinction_per_meter",
            "sky.haze_inverse_scale_height_per_meter",
            "sky.haze_base_height_meters = 0.0F",
        ):
            self.assertIn(token, self.ogre14_scene_source)
        capture = self.gfx_scene[
            self.gfx_scene.index("CaptureOgreNextDemoSceneLights(") :
            self.gfx_scene.index("BuildOgre14AutomaticReflectionProbe(")
        ]
        self.assertLess(
            capture.index("CaptureOgreNextDemoSceneLights("),
            capture.index("BuildOgre14GraphicsSceneAnalyticSkyEnvironment("),
        )
        self.assertLess(
            capture.index("BuildOgre14GraphicsSceneAnalyticSkyEnvironment("),
            capture.index("Ogre14GraphicsSceneCaptureField::ENVIRONMENT"),
        )
        for token in (
            "kOgre14AutomaticReflectionProbePolicyVersion = 1U",
            "kOgre14AutomaticReflectionProbeId",
            "BuildOgre14AutomaticReflectionProbe",
            "PERIODIC_SIMULATION_TICKS",
        ):
            self.assertIn(
                token,
                self.ogre14_scene_source_header + self.ogre14_scene_source,
            )
        for token in (
            "BuildOgreNextAnalyticSkyNativeMesh",
            "kOgreNextAnalyticSkyHemisphereRings",
            "kOgreNextAnalyticSkyCloudRings",
            "kOgreNextAnalyticSkyCloudSegments",
            "cloud_coverage",
            "cloud_phase_radians",
            "candidate.background_vertices",
            "candidate.sun_vertices",
            "mesh = std::move(candidate)",
        ):
            self.assertIn(token, self.policy + self.policy_header)
        for token in (
            "OgreNextN1AnalyticSkyFailureStage",
            "AFTER_ATTACHED_STATE_VERIFICATION",
            "analytic_sky_failure_pending",
            "analytic_sky_audit = {};",
            "mDepthCheck = false",
            "mDepthWrite = false",
            "Ogre::SBT_ADD",
            "Ogre::SBT_REPLACE",
            "setRenderQueueGroup(0U)",
            "native_view.inverseAffine().getTrans()",
            "CreateAnalyticSkySection",
            "DestroyAnalyticSkySection",
            "ExactAnalyticSkyBufferContents",
            "retain_analytic_sky_geometry_content_evidence",
            "createVertexBuffer",
            "createIndexBuffer",
            "createVertexArrayObject",
            "native_gpu_content_readbacks",
            "last_cpu_geometry_fnv1a64",
            "AnalyticSkyGeometryCacheKey",
            "SameAnalyticSkyGeometryCacheKey",
            "kOgreNextAnalyticSkyGeometryRefreshIntervalFrames = 30U",
            "analytic_sky_geometry_cache_frame_id",
            "impl_->retain_analytic_sky_geometry_content_evidence",
            "destroy_sky_datablock",
            "QueryAnalyticSkyAudit",
            "portable_scene_identity_absent = true",
        ):
            self.assertIn(token, self.header + self.frontend)
        self.assertIn(
            "analytic_sky_committed_descriptor =\n"
            "          impl_->analytic_sky_geometry_cache_key.descriptor",
            self.frontend,
        )
        for token in (
            "RunAnalyticSkyRollbackProof",
            "RunAnalyticSkyVisualProof",
            "RunAnalyticSkyProductionDefaultReadbackProof",
            "ror.ogre_next_analytic_sky.v2",
            "frontend_owned_v2_mesh_item",
            "sun_changed_pixels_alpha_exact_one",
            "native_lifetimes_balanced_on_failure",
            "production_default_gpu_content_readbacks_zero",
            "joined_live_ambient_and_exact_converted_main_light",
            "SkyX_shader_is_azimuth_dependent_and_may_apply_LDR_exposure",
            "analytic_sky_capture_policy_version",
            "analytic_sky_native_render_policy_version",
        ):
            self.assertIn(token, self.smoke)
        runner = RUNNER_PATH.read_text(encoding="utf-8")
        for token in (
            "validate_analytic_sky_report",
            "RT4_PBR_ANALYTIC_SKY_IMAGE_NAME",
            "RT4_PBR_ANALYTIC_SKY_EVIDENCE_NAME",
            "sun_changed_pixels_alpha_exact_one",
            "analytic_sky_slices",
            "ror.ogre_next_frontend_rt4_pbr_v1_smoke.v4",
        ):
            self.assertIn(token, runner)
        for token in (
            "--analytic-sky-output",
            "--analytic-sky-evidence",
            "PRIMARY_ANALYTIC_SKY_IMAGE",
            "PRIMARY_ANALYTIC_SKY_EVIDENCE",
            "REPEAT_ANALYTIC_SKY_IMAGE",
            "REPEAT_ANALYTIC_SKY_EVIDENCE",
        ):
            self.assertIn(token, self.entry_cmake)
        verifier = (
            REPOSITORY_ROOT / "tools" / "verify_ogre_next_artifact_set.py"
        ).read_text(encoding="utf-8")
        for token in (
            "_verify_analytic_sky_visual",
            "RT4_ANALYTIC_SKY_PPM_ARTIFACT",
            "RT4_ANALYTIC_SKY_EVIDENCE_ARTIFACT",
            "exact_gpu_buffer_content_readback",
        ):
            self.assertIn(token, verifier)
        for token in (
            "[RoR|OgreNextDemo|AnalyticSky|Source]",
            "radiance_authority=",
            "joined_live_ambient_and_exact_converted_main_light",
            "m_ogre_next_demo_analytic_sky_log_snapshot",
            "m_ogre_next_demo_analytic_sky_log_captures",
            "pending->analytic_sky_log_snapshot",
            "m_ogre_next_demo_analytic_sky_log_captures % 300U",
            "captures={} {}",
            "haze_extinction_per_meter={:.9g}",
            "haze_inverse_scale_height_per_meter={:.9g}",
            "haze_base_height_meters={:.9g}",
        ):
            self.assertIn(
                token,
                self.gfx_scene
                + (
                    REPOSITORY_ROOT / "source/main/gfx/GfxScene.h"
                ).read_text(encoding="utf-8"),
            )
        presenter = (
            REPOSITORY_ROOT
            / "source/main/system/RendererOgreNextInProcessPresenter.cpp"
        ).read_text(encoding="utf-8")
        presenter_header = (
            REPOSITORY_ROOT
            / "source/main/system/RendererOgreNextInProcessPresenter.h"
        ).read_text(encoding="utf-8")
        main_source = (
            REPOSITORY_ROOT / "source/main/main.cpp"
        ).read_text(encoding="utf-8")
        for token in (
            "RendererAnalyticSkyAudit",
            "AnalyticSkyAudit() const noexcept",
            "frontend->QueryAnalyticSkyAudit()",
            "frontend->QueryPresentationAudit()",
            "native_ownership_balanced",
            "expected_per_frame_ownership",
            "cpu_geometry_digest_verified",
            "native_geometry_metadata_verified",
            "production_gpu_readbacks_zero",
        ):
            self.assertIn(token, presenter + presenter_header)
        for token in (
            ".AnalyticSkyAudit()",
            "[RoR|RendererCombined|AnalyticSky|",
            "completed_frames={}",
            "cpu_geometry_fnv1a64={}",
            "native_gpu_content_readbacks={}",
            "gpu_readback_scope=",
            "production_disabled_test_artifact_only",
            # Aerial-perspective runtime evidence. constants_bound_verified is
            # the per-frame _readRawConstants readback, so the live-runner
            # validator can treat it as a hard gate.
            "[RoR|RendererCombined|AerialHaze|",
            "node=RoRAerialHazeNodeV1",
            "depth=RoROpaqueDepth",
            "constants_bound_verified={}",
            "extinction_per_meter={:.9g}",
        ):
            self.assertIn(token, main_source)
        for token in (
            "aerial_haze_applied",
            "aerial_haze_workspace_verified",
            "aerial_haze_constants_bound",
            "aerial_haze_depth_export_verified",
            "aerial_haze_extinction_per_meter",
            "aerial_haze_inscatter_r",
        ):
            self.assertIn(token, presenter + presenter_header)

    def test_display_domain_unlit_runs_after_filter_in_full32_unorm(self) -> None:
        for token in (
            "Ogre::HlmsUnlit(data_folder, library_folders)",
            "calculateHashForPreCreate",
            "kOgreNextDisplayDomainDatablockPrefix",
            "kOgreNextDisplayDomainProperty",
        ):
            self.assertIn(
                token,
                self.display_domain_unlit_header
                + self.display_domain_unlit_source,
            )
        self.assertNotIn("HLMS_USER0", self.display_domain_unlit_source)
        for token in (
            "setPrecisionMode(Ogre::Hlms::PrecisionFull32)",
            "getSupportedPrecisionMode() != Ogre::Hlms::PrecisionFull32",
            "UploadedTextureChannel::DISPLAY_DOMAIN_RGBA",
            "PFG_RGBA8_UNORM",
            "display_domain_unlit_datablock",
            "pbs_material && shadow_plan.enabled",
            "pbs_datablock->clone",
            "OgreNextN1DisplayDomainUploadAudit",
            "DisplayDomainUploadAudit() const",
            "readback.convertFromTexture(",
            "std::memcmp(downloaded_row, source_row, row_bytes)",
        ):
            self.assertIn(token, self.frontend)

        for token in (
            "@property( ror_display_domain_unlit )",
            "@piece( custom_ps_preLights )",
            "0.04045f",
            "12.92f",
            "2.4f",
        ):
            self.assertIn(token, self.display_domain_piece)
        for token in (
            "RunDisplayDomainUnlitProof",
            "MakeDisplayDomainUnlitCatalog",
            "RGBA16_FLOAT",
            "QuantizeHdrR16Float",
            "matching_foreground_pixels >= 512U",
            "decode_before_filter_pixels == 0U",
            "complete_unorm_mips_uploaded",
            "QueryDisplayDomainUploadAudit",
            "native_upload.expected_mip_levels == 2U",
            "native_upload.verified_mip_levels == 2U",
            "native_upload.verified_rgba_bytes == 20U",
            "full32_after_filter_shader_executed",
            "no_cast_or_receive_shadow_flags",
            "RunDisplayDomainUsageTransitionProof",
            "AFTER_ROLE_TRANSITION_CANDIDATE_TEXTURES",
            "usage_transition_rollback_exact",
            "usage_transition_commit_exact",
        ):
            self.assertIn(token, self.smoke)
        for token in (
            "TextureAssetName",
            "existing->second.usage != entry.second.usage",
            "replacement->second.usage != entry.second.usage",
        ):
            self.assertIn(token, self.frontend)

    def test_rt4_uv0_affine_is_exact_native_pbs_state(self) -> None:
        combined = self.uv_affine_header + self.uv_affine_source
        for token in (
            "Ogre::HlmsPbs(data_folder, library_folders)",
            "kOgreNextUvAffinePbsDatablockPrefix",
            "kOgreNextUvAffinePbsProperty",
            "calculateHashForPreCreate",
            "calculateHashForPreCaster",
            "SelectsUv0AffineShader",
        ):
            self.assertIn(token, combined)
        for token in (
            "@property( ror_uv0_affine_pbs )",
            "@piece( custom_ps_uv_modifier_macros )",
            "material.userValue[0].xy",
            "material.userValue[0].zw",
            "#define UV_DIFFUSE",
            "#define UV_NORMAL",
            "#define UV_SPECULAR",
            "#define UV_ROUGHNESS",
            "#define UV_EMISSIVE",
        ):
            self.assertIn(token, self.uv_affine_piece)
        for token in (
            "BuildOgreNextN1PbsUv0AffineTransform",
            "strictly positive scale components",
            "supports UV0 only",
            "shared UV0 affine transform",
            "overflow native binary32 multiplication",
            "overflow native binary32 addition",
        ):
            self.assertIn(token, self.policy + self.policy_header)
        for token in (
            "setUserValue(\n          0U",
            "getUserValue(0U)",
            "getTextureUvSource(pbs_slot) == 0U",
            "QueryPbsUv0AffineState",
            "native_texture_slot_readbacks",
            "native_user_value_readbacks",
            "exact_native_state",
        ):
            self.assertIn(token, self.frontend + self.header)
        for token in (
            "TextureVariant::UV0_AFFINE",
            "shared_uv0_scale_offset",
            "Float2{2.0F, 4.0F}",
            "Float2{0.125F, -0.25F}",
            "native_texture_slot_readbacks == 5U",
            "native_user_value_readbacks == 3U",
        ):
            self.assertIn(token, self.smoke)
        self.assertIn(
            "ror.ogre_next_rt4_texture_isolation.v2", self.smoke
        )

    def test_normal_map_audit_remediation_is_native_and_fail_closed(self) -> None:
        # Uniform-scale handling is an instance-retention decision in the live
        # presenter, not a scene-policy rejection. The retired policy helper
        # made one unsupported object fail the whole frame.
        self.assertNotIn("HasEffectivelyUniformScale", self.policy)
        # The uniformity predicate and its reason live in exactly one place so
        # the producer's filter and the presenter's skip cannot disagree about
        # which instances are drawable.
        for token in (
            "HasEffectivelyUniformLinearScale",
            "accurate_non_uniform_scaled_normals",
        ):
            self.assertIn(token, self.render_math)
        # A non-uniform scale rejects the INSTANCE, never the frame. The
        # producer drops the section before it can enter a snapshot and the
        # presenter skips it as a backstop; both count the drop, and neither
        # may return a frame verdict. Terrain .odef scale is applied verbatim
        # upstream, so this is the difference between one missing object and a
        # dead session.
        self.assertIn(
            "non_uniform_scale_sections_filtered",
            self.ogre14_scene_source_header,
        )
        self.assertIn(
            "HasEffectivelyUniformLinearScale", self.ogre14_scene_source
        )
        self.assertIn(
            "degrade_audit.non_uniform_scale_instance_rejections",
            self.frontend,
        )
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
            "non_uniform_scale_skips_instance_not_frame",
            "ror.ogre_next_rt4_tangent_handedness.v1",
        ):
            self.assertIn(token, self.smoke)

    def test_hdr_compositor_is_persistent_deterministic_and_packaged(self) -> None:
        for token in (
            "enable_hdr_compositor",
            "OgreNextHdrTemporalConfiguration",
            "QueryHdrCompositorAudit",
        ):
            self.assertIn(token, self.header)
        for token in (
            "RoRHdrWorkspaceHudV1",
            "RoRHdrWorkspaceUiOverlayControlV3",
            "CreateHudOverlayRuntime",
            "CommitHudOverlay",
            "DestroyHudOverlayRuntime",
            "UnbindHudOverlayTextureBeforeAssetReplacement",
            "RoRDisplayDomainUnlit_HudOverlayPanelV1",
            "hud_workspace_verified",
            # The HUD extent mismatch is the ordinary transient of a 30 Hz
            # rate-capped HUD readback against a per-frame camera extent -- any
            # window resize produces it for ~33 ms. It degrades to the same
            # hidden overlay a disabled HUD already uses, and is counted; it
            # must never return a frame verdict again.
            "degrade_audit.hud_extent_mismatch_frames",
            "PFG_RGBA16_FLOAT",
            "PFG_R16_FLOAT",
            "PFG_RGBA8_UNORM_SRGB",
            "2.0/scripts/materials/Common/Metal",
            "2.0/scripts/materials/HDR/Metal",
            "native_r16_history_validated",
            "exact_current_to_old_copy_verified",
            "history_allowed_error",
            "InitializeExactHdrHistory",
            "initial_image.uploadTo(history_texture, 0U, 0U)",
            "exact initial R16 history upload did not round-trip",
            "hdr_temporal_state.PrepareCommit",
            "hdr_temporal_state.CanCommitPrepared",
            "hdr_temporal_state.CommitPrepared",
            "hdr_temporal_state.AbortPrepared",
        ):
            self.assertIn(token, self.frontend)
        warmup_loop = self.frontend.index(
            "for (std::uint64_t warmup = 0U; warmup < 2U; ++warmup)"
        )
        exact_history_seed = self.frontend.index(
            "InitializeExactHdrHistory(", warmup_loop
        )
        exact_history_seed_end = self.frontend.index(";", exact_history_seed)
        exact_history_seed_call = self.frontend[
            exact_history_seed:exact_history_seed_end
        ]
        self.assertIn("history_seed", exact_history_seed_call)
        self.assertIn("observed_history", exact_history_seed_call)
        history_validation = self.frontend.index(
            "hdr_native_history_validated = true", exact_history_seed
        )
        self.assertLess(warmup_loop, exact_history_seed)
        self.assertLess(exact_history_seed, history_validation)
        scene_reset = self.frontend.index("ResetSceneGeneration()")
        reset_history_seed = self.frontend.index(
            "impl_->InitializeExactHdrHistory(", scene_reset
        )
        reset_history_seed_end = self.frontend.index(";", reset_history_seed)
        reset_history_seed_call = self.frontend[
            reset_history_seed:reset_history_seed_end
        ]
        self.assertIn("initial_history", reset_history_seed_call)
        self.assertIn("observed_history", reset_history_seed_call)
        self.assertIn("Ogre::v1::OverlaySystem", self.frontend)
        self.assertIn("Ogre::v1::OverlayManager", self.frontend)
        self.assertIn("kOgreNextHdrUiNode", self.frontend)
        self.assertIn("hdr_ui_overlay_control", self.frontend)
        self.assertIn("definition->getNodeAliasMap()", self.frontend)
        for stage in (
            "AFTER_RESOURCE_GROUP_CREATE",
            "AFTER_RESOURCE_LOCATIONS",
            "AFTER_RESOURCE_GROUP_INITIALIZE",
            "AFTER_WORKSPACE_DEFINITION",
            "AFTER_OUTPUT_CREATE",
            "AFTER_OUTPUT_CONFIGURE",
            "AFTER_WORKSPACE_CREATE",
            "AFTER_PARAMETER_BINDING",
            "AFTER_WARMUP_FRAME_ONE",
            "AFTER_WARMUP_FRAME_TWO",
            "AFTER_FRAME_COMMIT_PREPARE",
        ):
            self.assertIn(stage, self.header)
            self.assertIn(stage, self.frontend)
        self.assertIn("AFTER_FRAME_COMMIT_PREPARE", self.smoke)
        frame_transaction = self.frontend.index(
            "const ValidationResult output_validation"
        )
        transaction_order = [
            self.frontend.index(
                "ValidateRenderFrameOutput(request, candidate)", frame_transaction
            ),
            self.frontend.index(
                "VerifyAndPrepareHdrFrame(hdr_plan)", frame_transaction
            ),
            self.frontend.index(
                "OgreNextN1HdrFailureStage::AFTER_FRAME_COMMIT_PREPARE",
                frame_transaction,
            ),
            self.frontend.index(
                "submission_state.PrepareCommit(request)", frame_transaction
            ),
            self.frontend.index(
                "native_interop->PreparePublishFrame(", frame_transaction
            ),
            self.frontend.index("if (!cleanup_scene())", frame_transaction),
            self.frontend.index(
                "hdr_temporal_state.CanCommitPrepared()", frame_transaction
            ),
            self.frontend.index(
                "submission_state.CanCommitPrepared(request)", frame_transaction
            ),
            self.frontend.index(
                "native_interop->CanCommitPreparedFrame(", frame_transaction
            ),
            self.frontend.index(
                "reflection_probe_runtime->FinalizeFrame(", frame_transaction
            ),
            self.frontend.index(
                "native_interop->CommitPreparedFrame()", frame_transaction
            ),
            self.frontend.index(
                "hdr_temporal_state.CommitPrepared()", frame_transaction
            ),
            self.frontend.index(
                "submission_state.CommitPrepared(request)", frame_transaction
            ),
            self.frontend.index("output = std::move(candidate)", frame_transaction),
        ]
        self.assertEqual(transaction_order, sorted(transaction_order))
        self.assertNotIn(
            "static_cast<void>(destroy_retained_target())", self.frontend
        )
        for token in (
            "_ror_n1_hdr_media_roots",
            "ROR_OGRE_NEXT_N1_HDR_MEDIA_MANIFEST_ENTRIES",
            "Samples/Media/2.0/scripts/Compositors",
            "Samples/Media/2.0/scripts/materials/Common",
            "Samples/Media/2.0/scripts/materials/HDR",
            # The RoR aerial-haze media joins the SAME manifest, staging, and
            # provenance chain. Every link below must move together or the
            # runtime media gate silently stops covering the haze shader.
            "ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_ROOT_RELATIVE",
            "ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_RELATIVE",
            "ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_SOURCE_ROOT",
            "ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_SOURCES",
            "ROR_OGRE_NEXT_AERIAL_HAZE_SHADER_LOCK",
            "verify_aerial_haze_shader.py",
            "ror-aerial-haze-v1.lock.json",
            '"2.0/scripts/materials/RoRHaze")',
            "/RoRAerialHaze.material",
            "/GLSL/RoRAerialHaze_ps.glsl",
            "/HLSL/RoRAerialHaze_ps.hlsl",
            "/Metal/RoRAerialHaze_ps.metal",
            "_ror_n1_aerial_haze_package_commands",
        ):
            self.assertIn(token, self.entry_cmake)
        media_integrity = (
            REPOSITORY_ROOT
            / "source/main/gfx/render/ogrenext/OgreNextN1MediaIntegrity.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn('"2.0/scripts/materials/RoRHaze"', media_integrity)
        for token in (
            '"2.0/scripts/materials/RoRHaze"',
            '"2.0/scripts/materials/RoRHaze/GLSL"',
            '"2.0/scripts/materials/RoRHaze/HLSL"',
            '"2.0/scripts/materials/RoRHaze/Metal"',
            "std::array<const char *, 15U> relative_locations",
        ):
            self.assertIn(token, self.frontend)
        self.assertIn("RunHdrCompositorProof", self.smoke)
        self.assertIn("ror.ogre_next_hdr_compositor.v6", self.smoke)
        self.assertIn("ror.ogre_next_hdr_compositor_visual.v2", self.smoke)
        for token in (
            '\\"split_lighting\\"',
            '\\"linear_split_attachments\\"',
            '\\"lighting_production_content_readbacks\\"',
            '\\"lighting_production_framebuffer_readbacks\\"',
            '\\"ogre14_lighting_passes\\"',
        ):
            self.assertIn(token, self.smoke)
        self.assertIn("Ogre::v1::Overlay", self.smoke)
        self.assertIn("--compositor-evidence", self.smoke)
        history_error = self.smoke.index('"    \\"history_absolute_error\\"')
        self.assertGreater(
            self.smoke.rfind(
                "std::setprecision(std::numeric_limits<double>::max_digits10)",
                0,
                history_error,
            ),
            self.smoke.rfind("std::setprecision(9)", 0, history_error),
        )
        self.assertIn("VerifyRt4DeterministicRepeat.cmake", self.entry_cmake)
        self.assertIn("same_object_reinitialize_verified", self.smoke)
        for token in (
            "frame_commit_prepare_failure_verified",
            "aborted_hdr_audit_unchanged",
            "aborted_reflection_audit_unchanged",
            "aborted_submission_uncommitted",
            "aborted_output_unchanged",
            "post_render_failure_fault_latched",
        ):
            self.assertIn(token, self.smoke)

    def test_one_scene_hdr_pssm_topology_is_exact_and_audited(self) -> None:
        for token in (
            "enum class OgreNextHdrSceneTopology",
            "DIRECTIONAL_SPLIT_V2 = 0U",
            "SINGLE_EVALUATION_PSSM_V1",
        ):
            self.assertIn(token, self.hdr_topology_header)
        for token in (
            # Compositor audit v5 adds the depth-export and aerial-haze
            # evidence. Lighting audit v6 adds post-render native distance-LOD
            # selection and triangle-reduction evidence.
            "std::uint32_t version = 5U;",
            "kOgreNextNativeLightingPassAuditVersion = 6U",
            "OgreNextHdrSceneTopology scene_topology",
            "OgreNextHdrSceneTopology hdr_scene_topology",
            "pssm_finalized_with_populated_scene",
            "bool opaque_depth_export_verified = false;",
            "bool aerial_haze_workspace_verified = false;",
            "bool aerial_haze_constants_bound = false;",
            "bool aerial_haze_applied = false;",
        ):
            self.assertIn(token, self.header)

        topology_start = self.frontend.index(
            "void CreateAndVerifyHdrSingleSceneNode("
        )
        topology_end = self.frontend.index(
            "void ConfigureAndVerifyHdrPostExecutionMask(", topology_start
        )
        topology = self.frontend[topology_start:topology_end]
        for token in (
            "node->setNumLocalTextureDefinitions(3U);",
            "scene_texture->format = Ogre::PFG_RGBA16_FLOAT;",
            "scene_texture->width = 0U;",
            "scene_texture->height = 0U;",
            # The scene owns an explicit D32 depth texture instead of a pooled
            # depth buffer so the aerial-haze pass can sample it. Every token
            # below is part of that export and must move together.
            "scene_texture->depthBufferId = Ogre::DepthBuffer::POOL_NO_DEPTH;",
            "scene_view->depthBufferId = Ogre::DepthBuffer::POOL_NO_DEPTH;",
            "opaque_depth->format = Ogre::PFG_D32_FLOAT;",
            "opaque_depth->width = 0U;",
            "opaque_depth->height = 0U;",
            "opaque_depth->textureFlags = Ogre::TextureFlags::RenderToTexture;",
            "opaque_depth->depthBufferId = Ogre::DepthBuffer::POOL_NO_DEPTH;",
            "scene_view->depthAttachment.textureName = "
            "kOgreNextHdrOpaqueDepthTexture;",
            "scene->mClearDepth = 1.0F;",
            "scene->mStoreActionDepth = Ogre::StoreAction::Store;",
            "history->format = Ogre::PFG_R16_FLOAT;",
            "history->width = 1U;",
            "history->height = 1U;",
            "history->depthBufferId = Ogre::DepthBuffer::POOL_NO_DEPTH;",
            "node->setNumTargetPass(2U);",
            "scene_target->setNumPasses(1U);",
            "scene_target->addPass(Ogre::PASS_SCENE)",
            "scene->mIdentifier = kOgreNextHdrSingleScenePassIdentifier;",
            "scene->mIncludeOverlays = false;",
            "scene->mEnableForwardPlus = true;",
            "scene->setVisibilityMask(kOgreNextRt4AuthoredVisibilityMask);",
            "history_target->setNumPasses(1U);",
            "history_target->addPass(Ogre::PASS_CLEAR)",
            "clear_history->mNumInitialPasses = 1U;",
            "node->setNumOutputChannels(3U);",
            "node->mapOutputChannel(0U, kOgreNextHdrRasterLitTexture);",
            "node->mapOutputChannel(1U, kOgreNextHdrHistoryTexture);",
            "node->mapOutputChannel(2U, kOgreNextHdrOpaqueDepthTexture);",
            "verified_scene->mShadowNode != Ogre::IdString()",
            # Depth adds a texture and a channel but no pass.
            "textures.size() != 3U",
            "node->getNumOutputChannels() != 3U",
            "node->calculateNumPasses() != 2U",
            "textures[2U].format != Ogre::PFG_D32_FLOAT",
            "verified_scene->mStoreActionDepth != Ogre::StoreAction::Store",
        ):
            with self.subTest(token=token):
                self.assertIn(token, topology)
        self.assertEqual(topology.count("node->addTextureDefinition("), 3)
        self.assertEqual(topology.count("node->addTargetPass("), 2)
        self.assertEqual(topology.count("addPass(Ogre::PASS_SCENE)"), 1)
        self.assertEqual(topology.count("addPass(Ogre::PASS_CLEAR)"), 1)
        for split_attachment in (
            "kOgreNextHdrBaseTexture",
            "kOgreNextHdrSunFullTexture",
            "kOgreNextHdrSunDirectTexture",
            "kOgreNextHdrVisibilityTexture",
            "kOgreNextHdrLitTexture",
        ):
            self.assertNotIn(split_attachment, topology)

        runtime_start = self.frontend.index(
            "RenderOperationResult RefreshSingleSceneHdrRuntimeTargets("
        )
        runtime_end = self.frontend.index(
            "bool RollbackSingleSceneHdrPssm()", runtime_start
        )
        runtime = self.frontend[runtime_start:runtime_end]
        for token in (
            "linear_scene->getPixelFormat() == Ogre::PFG_RGBA16_FLOAT",
            "old_luminance->getPixelFormat() == Ogre::PFG_R16_FLOAT",
            "iterative_luminance->getPixelFormat() == Ogre::PFG_R16_FLOAT",
            "current_luminance->getPixelFormat() == Ogre::PFG_R16_FLOAT",
            "definition->getNumTargetPasses() == 2U",
            "definition->calculateNumPasses() == 2U",
            "definition->getNumOutputChannels() == 3U",
            # recreateAllNodes() during PSSM finalize/rollback replaces every
            # TextureGpu, so the refresh must re-resolve and re-verify the
            # exported depth instead of keeping a stale pointer.
            "opaque_depth->getPixelFormat() == Ogre::PFG_D32_FLOAT",
            "hdr_opaque_depth_target = opaque_depth;",
            "hdr_auto_exposure_graph_verified",
            "hdr_bloom_graph_verified",
            "hdr_tone_map_graph_verified",
            "hdr_srgb_output_verified",
        ):
            with self.subTest(token=token):
                self.assertIn(token, runtime)

        for token in (
            "audit.scene_topology = hdr_scene_topology;",
            "lighting_candidate.hdr_scene_topology = impl_->hdr_scene_topology;",
            "impl_->hdr_enabled && !impl_->SingleSceneHdrPssmEnabled() ? 3U : 1U;",
            "impl_->hdr_scene_topology, &shadow_plan, this,",
            "retained_instance_block_already_validated);",
        ):
            self.assertIn(token, self.frontend)
        policy = self.policy_header + self.policy
        for token in (
            "OgreNextHdrSceneTopology hdr_scene_topology",
            "reviewed_single_scene_hdr_pssm",
            "reviewed_native_sun_visibility_v2",
            "OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1",
            "OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1",
            "!native_directional_shadow_enabled",
            "OgreNextHdrSceneTopology::SINGLE_EVALUATION_PSSM_V1",
            '"hdr_scene_topology"',
            "persistent HDR directional shadows require the exact reviewed single-scene PSSM or directional-split native sun-visibility topology",
            "sun-visibility V2 requires exactly RT4/V1, persistent HDR, disabled PSSM and native N4, and DIRECTIONAL_SPLIT_V2",
        ):
            self.assertIn(token, policy)
        self.assertNotIn("reviewed_hdr_directional_shadow_node", policy)

        for token in (
            "RunSingleSceneHdrPssmTopologyProof",
            # v2 adds the aerial-perspective evidence. The DIRECTIONAL_SPLIT_V2
            # schemas (hdr_compositor.v6 / hdr_compositor_visual.v2) stay
            # frozen: haze is single-scene only.
            "ror.ogre_next_hdr_pssm_single_scene.v2",
            '\\"depth_export_verified\\"',
            '\\"haze_node_verified\\"',
            '\\"haze_constants_bound\\"',
            '\\"haze_identity_when_sky_disabled\\"',
            '\\"haze_applied\\"',
            '\\"topology\\": \\"SINGLE_EVALUATION_PSSM_V1\\"',
            '\\"pssm_deferred_until_populated_scene\\"',
            '\\"zero_light_pssm_warmup_avoided\\"',
            '\\"warmup_native_absence_checks\\"',
            '\\"scene_evaluations\\"',
            '\\"single_history_step\\"',
            '\\"rgba16_hdr\\"',
            '\\"auto_exposure\\"',
            '\\"bloom\\"',
            '\\"filmic\\"',
            '\\"srgb\\"',
            '\\"production_content_readbacks\\"',
            '\\"production_framebuffer_readbacks\\"',
            '\\"ogre14_lighting_passes\\"',
            '\\"rollback_stages_verified\\"',
            '\\"clean_shutdown\\"',
        ):
            self.assertIn(token, self.smoke)

    def test_one_scene_pssm_finalization_is_populated_and_transactional(self) -> None:
        for field in (
            "pssm_finalization_attempts",
            "pssm_finalization_commits",
            "pssm_finalization_rollbacks",
            "pssm_deferred_until_scene_population",
            "pssm_finalized_with_populated_scene",
            "zero_light_pssm_warmup_avoided",
            "pssm_warmup_native_absence_checks",
        ):
            self.assertIn(field, self.header)
            self.assertIn(field, self.frontend)
        for stage in (
            "AFTER_SINGLE_SCENE_PSSM_DEFINITION",
            "AFTER_SINGLE_SCENE_PSSM_WORKSPACE_RECREATE",
            "BEFORE_SINGLE_SCENE_WARMUP_ABSENCE_CHECK_COUNTER_DRIFT",
            "AFTER_FRAME_COMMIT_PREPARE",
        ):
            self.assertIn(stage, self.header)
            self.assertIn(stage, self.frontend)

        finalization_start = self.frontend.index(
            "RenderOperationResult FinalizeSingleSceneHdrPssm("
        )
        finalization_end = self.frontend.index(
            "RenderOperationResult CreateHdrCompositor(", finalization_start
        )
        finalization = self.frontend[finalization_start:finalization_end]
        for token in (
            "directional_lights != 1U",
            "shadow_casters == 0U",
            "shadow_receivers == 0U",
            "authored_view_visibility != kOgreNextRt4AuthoredVisibilityMask",
            "++hdr_pssm_finalization_attempts;",
            "CreateAndVerifyPssmShadowNode(",
            "BindAndVerifyPssmWorkspace(",
            "kOgreNextHdrSingleScenePassIdentifier",
            "hdr_workspace->recreateAllNodes();",
            "RefreshSingleSceneHdrRuntimeTargets(true)",
            "InitializeExactHdrHistory(",
            "hdr_pssm_finalization_prepared = true;",
            "CanCommitPreparedSingleSceneHdrPssm()",
            "CommitPreparedSingleSceneHdrPssm()",
            "hdr_pssm_finalized_with_populated_scene = true;",
            "++hdr_pssm_finalization_commits;",
            "RollbackSingleSceneHdrPssm()",
        ):
            with self.subTest(token=token):
                self.assertIn(token, finalization)
        self.assertLess(
            finalization.index("directional_lights != 1U"),
            finalization.index("CreateAndVerifyPssmShadowNode("),
        )
        self.assertLess(
            finalization.index("BindAndVerifyPssmWorkspace("),
            finalization.index("hdr_workspace->recreateAllNodes();"),
        )
        self.assertLess(
            finalization.index("RefreshSingleSceneHdrRuntimeTargets(true)"),
            finalization.index("hdr_pssm_finalization_prepared = true;"),
        )
        self.assertLess(
            finalization.index("hdr_pssm_finalization_prepared = true;"),
            finalization.index(
                "hdr_pssm_finalized_with_populated_scene = true;"
            ),
        )

        for token in (
            "VerifySingleSceneHdrWarmupShadowAbsence(",
            "bound_shadow_scene_passes == 0U",
            "hdr_workspace->findShadowNode(shadow_name) == nullptr",
            "shadow_audit.shadow_node_creates == expected_shadow_node_creates",
            "shadow_audit.shadow_node_destroys ==",
            "hdr_pssm_warmup_native_absence_checks == 3U",
            "hdr_pssm_deferred_until_scene_population_verified =",
            "hdr_zero_light_pssm_warmup_avoided_verified =",
            "abort_hdr_pssm_finalization",
            "impl_->RollbackSingleSceneHdrPssm()",
            "impl_->CanCommitPreparedSingleSceneHdrPssm()",
            "impl_->CommitPreparedSingleSceneHdrPssm()",
        ):
            self.assertIn(token, self.frontend)
        render_start = self.frontend.index(
            "RenderOperationResult OgreNextN1Frontend::Render("
        )
        render = self.frontend[render_start:]
        self.assertLess(
            render.index("impl_->FinalizeSingleSceneHdrPssm("),
            render.index(
                "OgreNextN1HdrFailureStage::AFTER_FRAME_COMMIT_PREPARE"
            ),
        )
        self.assertLess(
            render.index(
                "OgreNextN1HdrFailureStage::AFTER_FRAME_COMMIT_PREPARE"
            ),
            render.index("impl_->CommitPreparedSingleSceneHdrPssm()"),
        )

        populate_call = self.frontend.index(
            "impl_->FinalizeSingleSceneHdrPssm("
        )
        production_graph = self.frontend.index(
            "impl_->EnsureProductionPresentationGraph(", populate_call
        )
        self.assertLess(populate_call, production_graph)
        populate_block = self.frontend[populate_call:production_graph]
        for argument in (
            "lighting_candidate.last_directional_lights",
            "lighting_candidate.last_shadow_casters",
            "lighting_candidate.last_shadow_receivers",
            "authored_view_visibility",
        ):
            self.assertIn(argument, populate_block)

        resize_start = self.frontend.index("const bool rebuild_hdr =")
        resize_end = self.frontend.index(
            "if (production_presentation)", resize_start
        )
        resize = self.frontend[resize_start:resize_end]
        self.assertIn("impl_->DestroyHdrCompositor(false)", resize)
        self.assertIn("impl_->CreateHdrCompositor(", resize)
        teardown_start = self.frontend.index(
            "DestroyHdrCompositor(bool destroy_definitions_and_resources"
        )
        teardown_end = self.frontend.index(
            "RenderOperationResult ConfigureHdrParameters(", teardown_start
        )
        teardown = self.frontend[teardown_start:teardown_end]
        for token in (
            "UnbindAndVerifyPssmWorkspace(",
            "removeShadowNodeDefinition(shadow_name)",
            "hdr_pssm_finalized_with_populated_scene = false;",
            "shadow_audit.shadow_node_destroys",
        ):
            self.assertIn(token, teardown)

    def test_production_hdr_lighting_is_gpu_only_and_has_no_ogre14_pass(self) -> None:
        for token in (
            "production native presentation forbids all optional content readback evidence",
            "impl_->hdr_temporal_state.PrepareGpuOnlyCommit(hdr_plan)",
            "lighting_candidate.production_content_readbacks =",
            "lighting_candidate.production_framebuffer_readbacks =",
            "lighting_candidate.production_gpu_only = gpu_only_output;",
            "lighting_candidate.no_ogre14_lighting = true;",
            "lighting_candidate.production_content_readbacks != 0U",
            "lighting_candidate.production_framebuffer_readbacks != 0U",
            "!lighting_candidate.production_gpu_only",
        ):
            self.assertIn(token, self.frontend)
        self.assertIn(
            "std::uint64_t ogre14_lighting_passes = 0U;", self.header
        )
        self.assertIn("bool no_ogre14_lighting = true;", self.header)

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
            self.frontend.index("createTexture(\n        target_text"),
        )
        self.assertEqual(self.frontend.count("createRenderWindow("), 3)
        self.assertIn(
            "if (!impl_->presentation_configuration.enabled)", self.frontend
        )
        self.assertIn("RoR Ogre-Next N1 bootstrap", self.frontend)
        self.assertIn("RoR Ogre-Next N1 null bootstrap", self.frontend)
        self.assertIn("RoR Ogre-Next N1 native presentation", self.frontend)

    def test_rt4_isolation_validator_is_exact_and_tamper_closed(self) -> None:
        names = (
            ("baseline", "none"),
            ("base_color", "base_color_rgb"),
            ("roughness_g", "packed_green_roughness"),
            ("metallic_b", "packed_blue_metallic"),
            ("emissive", "emissive_rgb"),
            ("normal_rg", "canonical_positive_z_normal_rg"),
            ("uv0_affine", "shared_uv0_scale_offset"),
            ("sampler_uv", "sampler_address_over_uv0"),
        )

        sampler_start = self.smoke.index(
            "variant->variant == TextureVariant::SAMPLER_UV"
        )
        sampler_end = self.smoke.index(
            "sampler_descriptor.address_v", sampler_start
        )
        sampler_fixture = self.smoke[sampler_start:sampler_end]
        self.assertIn("? SamplerAddressMode::REPEAT", sampler_fixture)
        self.assertNotIn("MIRRORED_REPEAT", sampler_fixture)
        report: dict = {
            "texture_isolation": {
                "schema": "ror.ogre_next_rt4_texture_isolation.v2",
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
            transformed = name == "uv0_affine"
            entry = {
                "name": name,
                "changed_input": changed_input,
                "asset_sequence": index + 1,
                "uv0_affine": {
                    "version": 1,
                    "scale": [2, 4] if transformed else [1, 1],
                    "offset": [0.125, -0.25] if transformed else [0, 0],
                    "portable_binding_count": 4,
                    "native_slot_count": 5,
                    "native_slot_readbacks": 5,
                    "native_user_value_readbacks": 3,
                    "transformed": transformed,
                    "uv0_only": True,
                    "positive_scale": True,
                    "rotation_zero": True,
                    "shared_across_bound_slots": True,
                    "shader_piece_selected": True,
                    "exact_native_state": True,
                },
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
            uv_receipt = report["texture_isolation"]["variants"][6][
                "uv0_affine"
            ]
            uv_receipt["native_slot_readbacks"] = 4
            with self.assertRaisesRegex(
                RUNNER.ProbeError, "native UV0 affine receipt"
            ):
                RUNNER.validate_rt4_isolation_evidence(report, path)
            uv_receipt["native_slot_readbacks"] = 5
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
                "dynamic_mesh_updates": "synchronous_full_frame_owned",
                "analytic_lights_calibrated": False,
                "constant_environment_only": True,
                "native_interop": False,
                "ray_tracing": False,
            },
            "catalog": {
                "sequence": 1,
                "transactional_replay_after_restart": True,
            },
            "dynamic_meshes": {
                "schema": "ror.ogre_next_dynamic_mesh.v2",
                "base_deformation_revision": 1,
                "deformed_deformation_revision": 2,
                "full_update_owned": True,
                "solver_memory_aliased": False,
                "changed_pixels": 512,
                "base_attachment_fnv1a64": "1" * 16,
                "deformed_attachment_fnv1a64": "2" * 16,
                "base_exact_replay": True,
                "deformed_exact_replay": True,
                "persistent_vertex_storage_exact": True,
                "persistent_buffer_updates": 1,
                "native_mesh_rebuilds_through_persistent_update": 1,
                "uploaded_vertex_bytes_through_persistent_update": 192,
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
            invalid_dynamic = copy.deepcopy(report)
            invalid_dynamic["dynamic_meshes"][
                "deformed_attachment_fnv1a64"
            ] = invalid_dynamic["dynamic_meshes"][
                "base_attachment_fnv1a64"
            ]
            with self.assertRaises(RUNNER.ProbeError):
                RUNNER.validate_n1_checkpoint(
                    invalid_dynamic,
                    image,
                    lock,
                    policy,
                    manifest,
                    source_identity,
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
            uv_lock_hash = "a" * 64
            paths = {
                "Rigs-of-Rods-GPL-3.0.txt": ror_hash,
                "Ogre-Next-MIT.txt": lock["license"]["sha256"],
                "RapidJSON-license.txt": lock["dependencies"]["rapidjson"][
                    "license_sha256"
                ],
                "FreeType-GPLv2.txt": lock["dependencies"]["freetype"][
                    "license_sha256"
                ],
                "FreeType-LICENSE.txt": lock["dependencies"]["freetype"][
                    "overview_sha256"
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
            staged_uv_lock = (
                Path(temp)
                / RUNNER.N1_PACKAGE_NAME
                / RUNNER.UV_AFFINE_PBS_SHADER_LOCK_PACKAGE_RELATIVE
            )
            staged_uv_lock.parent.mkdir(parents=True)
            staged_uv_lock.write_text(staged_uv_lock.name, encoding="utf-8")

            def fake_sha256(path: Path) -> str:
                if path == REPOSITORY_ROOT / "COPYING":
                    return ror_hash
                if path == RUNNER.DISPLAY_DOMAIN_MEDIA_PATH:
                    return "7" * 64
                if path == RUNNER.INDIRECT_ALPHA_MEDIA_PATH:
                    return "b" * 64
                if path == RUNNER.SUN_VISIBILITY_V2_MEDIA_PATH:
                    return "8" * 64
                if path == RUNNER.UV_AFFINE_PBS_MEDIA_PATH:
                    return "9" * 64
                if path == RUNNER.UV_AFFINE_PBS_SHADER_LOCK_PATH:
                    return uv_lock_hash
                if path == staged_uv_lock:
                    return uv_lock_hash
                return paths[path.name]

            manifest = {
                "sha256": "3" * 64,
                "file_count": 107,
                "entries": [("file", 1, "4" * 64)],
            }
            package_manifest = RUNNER._shader_media_manifest_from_entries(
                [
                    *manifest["entries"],
                    (
                        "RoR/DisplayDomain/DisplayDomain_piece_ps.any",
                        RUNNER.DISPLAY_DOMAIN_MEDIA_PATH.stat().st_size,
                        "7" * 64,
                    ),
                    (
                        "RoR/IndirectAlpha/IndirectAlpha_piece_ps.any",
                        RUNNER.INDIRECT_ALPHA_MEDIA_PATH.stat().st_size,
                        "b" * 64,
                    ),
                    (
                        "RoR/SunVisibilityV2/SunVisibilityV2.metal",
                        RUNNER.SUN_VISIBILITY_V2_MEDIA_PATH.stat().st_size,
                        "8" * 64,
                    ),
                    (
                        "RoR/UvAffinePbs/UvAffinePbs_piece_ps.any",
                        RUNNER.UV_AFFINE_PBS_MEDIA_PATH.stat().st_size,
                        "9" * 64,
                    ),
                ]
            )
            hdr_manifest = {
                "sha256": "5" * 64,
                "file_count": 137,
                "entries": [("2.0/scripts/file", 1, "6" * 64)],
            }
            expected = {
                **package_manifest,
                "hdr_sha256": hdr_manifest["sha256"],
                "hdr_file_count": hdr_manifest["file_count"],
            }
            with mock.patch.object(
                RUNNER, "sha256_file", side_effect=fake_sha256
            ), mock.patch.object(
                RUNNER,
                "shader_media_manifest",
                side_effect=[manifest, package_manifest],
            ) as media_manifest, mock.patch.object(
                RUNNER,
                "expected_hdr_media_manifest",
                return_value=hdr_manifest,
            ), mock.patch.object(
                RUNNER, "hdr_media_manifest", return_value=hdr_manifest
            ):
                self.assertEqual(
                    RUNNER.validate_n1_package(Path(temp), lock), expected
                )
                indirect_media = Path(temp) / "indirect-display-domain.any"
                indirect_media.symlink_to(RUNNER.DISPLAY_DOMAIN_MEDIA_PATH)
                media_manifest.side_effect = [manifest, package_manifest]
                with mock.patch.object(
                    RUNNER, "DISPLAY_DOMAIN_MEDIA_PATH", indirect_media
                ), self.assertRaisesRegex(
                    RUNNER.ProbeError, "missing or symbolic"
                ):
                    RUNNER.validate_n1_package(Path(temp), lock)
                indirect_alpha = Path(temp) / "indirect-alpha.any"
                indirect_alpha.symlink_to(RUNNER.INDIRECT_ALPHA_MEDIA_PATH)
                media_manifest.side_effect = [manifest, package_manifest]
                with mock.patch.object(
                    RUNNER, "INDIRECT_ALPHA_MEDIA_PATH", indirect_alpha
                ), self.assertRaisesRegex(
                    RUNNER.ProbeError, "missing or symbolic"
                ):
                    RUNNER.validate_n1_package(Path(temp), lock)
                indirect_sun = Path(temp) / "indirect-sun-visibility.metal"
                indirect_sun.symlink_to(RUNNER.SUN_VISIBILITY_V2_MEDIA_PATH)
                media_manifest.side_effect = [manifest, package_manifest]
                with mock.patch.object(
                    RUNNER, "SUN_VISIBILITY_V2_MEDIA_PATH", indirect_sun
                ), self.assertRaisesRegex(
                    RUNNER.ProbeError, "missing or symbolic"
                ):
                    RUNNER.validate_n1_package(Path(temp), lock)
                indirect_uv = Path(temp) / "indirect-uv-affine.any"
                indirect_uv.symlink_to(RUNNER.UV_AFFINE_PBS_MEDIA_PATH)
                media_manifest.side_effect = [manifest, package_manifest]
                with mock.patch.object(
                    RUNNER, "UV_AFFINE_PBS_MEDIA_PATH", indirect_uv
                ), self.assertRaisesRegex(
                    RUNNER.ProbeError, "missing or symbolic"
                ):
                    RUNNER.validate_n1_package(Path(temp), lock)
                media_manifest.side_effect = [manifest, manifest]
                with self.assertRaisesRegex(
                    RUNNER.ProbeError, "reviewed RoR shader manifest"
                ):
                    RUNNER.validate_n1_package(Path(temp), lock)
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

    def test_hdr_media_manifest_is_cross_backend_and_byte_exact(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-n1-hdr-media-") as temp:
            root = Path(temp)
            files = {
                "2.0/scripts/Compositors/HDR.compositor": b"compositor",
                "2.0/scripts/materials/Common/Metal/Quad_vs.metal": b"metal",
                "2.0/scripts/materials/HDR/HLSL/ToneMap.hlsl": b"hlsl",
                # RoRHaze is a required manifest root: the RoR-owned aerial
                # haze material and shaders are inside the same byte-exact
                # closure the runtime verifier enforces.
                (
                    "2.0/scripts/materials/RoRHaze/RoRAerialHaze.material"
                ): b"haze-material",
                (
                    "2.0/scripts/materials/RoRHaze/Metal/"
                    "RoRAerialHaze_ps.metal"
                ): b"haze-metal",
            }
            for relative, payload in files.items():
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(payload)
            manifest = RUNNER.hdr_media_manifest(root)
            self.assertEqual(manifest["file_count"], len(files))
            self.assertEqual(
                [entry[0] for entry in manifest["entries"]], sorted(files)
            )
            original_digest = manifest["sha256"]
            target = root / "2.0/scripts/materials/Common/Metal/Quad_vs.metal"
            target.write_bytes(b"tampered")
            self.assertNotEqual(
                RUNNER.hdr_media_manifest(root)["sha256"], original_digest
            )



class RuntimeAuditPerformanceContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.frontend = (
            RENDER_ROOT / "ogrenext" / "OgreNextN1Frontend.cpp"
        ).read_text(encoding="utf-8")
        self.frontend_header = (
            RENDER_ROOT / "ogrenext" / "OgreNextN1Frontend.h"
        ).read_text(encoding="utf-8")
        self.main_source = (
            REPOSITORY_ROOT / "source" / "main" / "main.cpp"
        ).read_text(encoding="utf-8")
        self.gfx_scene = (
            REPOSITORY_ROOT / "source" / "main" / "gfx" / "GfxScene.cpp"
        ).read_text(encoding="utf-8")

    def test_monotonic_audits_are_heartbeat_throttled_and_render_is_timed(
        self,
    ) -> None:
        self.assertIn(
            "kOgreNextRetainedSceneAuditVersion = 7U",
            self.frontend_header,
        )
        render_timer = self.frontend.index(
            "const auto native_render_phase_start"
        )
        render_call = self.frontend.index(
            "if (!impl_->root->renderOneFrame())", render_timer
        )
        render_receipt = self.frontend.index(
            "last_native_render_phase_microseconds =", render_call
        )
        self.assertLess(render_timer, render_call)
        self.assertLess(render_call, render_receipt)

        # The frame validator owns the scene/PSSM policy and returns the
        # validated plan. An exact immutable predecessor receipt narrows the
        # hot path to patched instances; first/stale frames still take the
        # complete validator.
        self.assertIn(
            "impl_->hdr_scene_topology, &shadow_plan, this,",
            self.frontend,
        )
        for token in (
            "request.scene_snapshot->has_retained_instance_block_proof()",
            "retained_instance_predecessor_snapshot_id() ==",
            "retained_instance_block_already_validated",
            "snapshot.retained_instance_patched_indices()",
            "last_diff_used_retained_block_proof =",
        ):
            self.assertIn(token, self.frontend)
        self.assertNotIn(
            "const ValidationResult shadow_validation =",
            self.frontend,
        )

        for token in (
            "frame_prepare_phase_microseconds =",
            "light_phase_microseconds =",
            "instance_phase_microseconds =",
            "native_prepare_phase_microseconds =",
            "native_render_phase_microseconds =",
            "post_render_phase_microseconds =",
            "cleanup_phase_microseconds =",
            "publication_phase_microseconds =",
            "renderer_combined_particle_audit_state_signature",
            "renderer_combined_particle_audit_logged_sequence",
            "particle_audit_heartbeat",
            "renderer_combined_native_lighting_audit_state_signature",
            "renderer_combined_native_lighting_audit_logged_frame",
            "lighting_audit_heartbeat",
            "frame_prepare_phase_us={}",
            "native_prepare_phase_us={}",
            "native_render_phase_us={}",
            "post_render_phase_us={}",
            "publication_phase_us={}",
            "retained_scene_audit.last_created +",
            "retained_scene_audit.last_destroyed !=",
        ):
            self.assertIn(
                token,
                self.frontend
                if token.endswith("microseconds =")
                else self.main_source,
            )
        self.assertNotIn(
            "retained_scene_audit.last_dynamic_updates !=",
            self.main_source,
        )
        self.assertNotIn(
            "renderer_combined_particle_audit_signature",
            self.main_source,
        )
        self.assertNotIn(
            "renderer_combined_native_lighting_audit_signature",
            self.main_source,
        )

        runtime_particle_key = self.main_source[
            self.main_source.index(
                "const std::string audit_state_signature ="
            ) : self.main_source.index(
                "const bool particle_audit_heartbeat ="
            )
        ]
        self.assertIn("live_systems={}", runtime_particle_key)
        self.assertNotIn("audit.live_particles,", runtime_particle_key)

        capture_particle_key = self.gfx_scene[
            self.gfx_scene.index(
                "const std::string particle_state_signature ="
            ) : self.gfx_scene.index(
                "const std::string particle_snapshot ="
            )
        ]
        self.assertIn("lifetime_max_admitted_particles={}", capture_particle_key)
        self.assertNotIn("particles.observed_particles", capture_particle_key)
        self.assertNotIn("particles.captured_particles", capture_particle_key)
        self.assertIn("particle_audit_heartbeat", self.gfx_scene)


class SingleEvaluationPssmDeferralContractTests(unittest.TestCase):
    """Lock the paged-terrain deferral that the live suite cannot reach.

    The Ogre-Next combined runtime refused every terrain whose geometry pages
    in - which is all of the pinned simple2 validation scenes - because
    single-evaluation PSSM finalization treated a first frame with no shadow
    casters as a fatal backend failure, and that failure is terminal. Four
    separate sites conflated "shadows were requested" with "the shadow node
    exists". None of it was caught by the existing suite, because nothing
    exercises an unpopulated frame followed by a populated one, and this path
    needs a live Metal device to run. These are therefore source-closure
    checks, in the same spirit as the other wiring contracts here.
    """

    def setUp(self) -> None:
        self.frontend = (
            RENDER_ROOT / "ogrenext" / "OgreNextN1Frontend.cpp"
        ).read_text()

    def test_empty_shadow_sets_defer_rather_than_fail(self) -> None:
        anchor = "if (shadow_casters == 0U || shadow_receivers == 0U) {"
        self.assertEqual(self.frontend.count(anchor), 1)
        start = self.frontend.index(anchor)
        block = self.frontend[start : self.frontend.index("\n    }", start)]
        self.assertIn("hdr_pssm_finalization_deferrals", block)
        self.assertIn("hdr_pssm_finalization_deferred = true", block)
        self.assertIn("RenderOperationResult::Success()", block)
        # The deferral must not be reachable as a backend failure.
        self.assertNotIn("HdrBackendFailure", block)

    def test_genuine_contract_violations_stay_fatal(self) -> None:
        anchor = (
            "if (!SingleSceneHdrPssmEnabled() || hdr_workspace == nullptr ||\n"
            "        directional_lights != 1U ||"
        )
        self.assertEqual(self.frontend.count(anchor), 1)
        start = self.frontend.index(anchor)
        block = self.frontend[start : self.frontend.index("\n    }", start)]
        self.assertIn("directional_lights != 1U", block)
        self.assertIn("kOgreNextRt4AuthoredVisibilityMask", block)
        self.assertIn("HdrBackendFailure", block)
        # Empty caster/receiver sets must no longer be part of the fatal guard.
        self.assertNotIn("shadow_casters == 0U", block)
        self.assertNotIn("shadow_receivers == 0U", block)

    def test_shadow_node_use_is_gated_on_readiness(self) -> None:
        anchor = "const bool pssm_ready_this_frame ="
        self.assertEqual(self.frontend.count(anchor), 1)
        declaration = self.frontend[
            self.frontend.index(anchor) : self.frontend.index(anchor) + 220
        ]
        self.assertIn("hdr_shadow_node_definition_created", declaration)
        self.assertIn("shadow_plan.enabled", declaration)
        # Presentation, shadow-node selection, cascade readback and the
        # post-instantiation check must all use the readiness predicate.
        self.assertGreaterEqual(
            self.frontend.count("pssm_ready_this_frame"), 5)
        readback = self.frontend.index("ReadAndVerifyNativePssmState(*workspace")
        guard = self.frontend.rindex("if (pssm_ready_this_frame) {", 0, readback)
        self.assertLess(guard, readback)

    def test_publication_accepts_a_deferred_topology(self) -> None:
        anchor = "bool CanCommitPreparedSingleSceneHdrPssm() const noexcept {"
        block = self.frontend[
            self.frontend.index(anchor) : self.frontend.index(anchor) + 900
        ]
        self.assertIn("hdr_pssm_finalization_deferred", block)
        self.assertIn("return !hdr_pssm_finalization_prepared;", block)

    def test_deferral_cannot_outlive_its_frame(self) -> None:
        # The flag must clear on the non-deferring finalization exits and
        # alongside every reset of the prepared flag, or a stale deferral
        # would let a genuinely changed topology publish.
        self.assertGreaterEqual(
            self.frontend.count("hdr_pssm_finalization_deferred = false"), 4)

if __name__ == "__main__":
    unittest.main()
