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
        cls.media_integrity = (
            RENDER_ROOT / "ogrenext" / "OgreNextN1MediaIntegrity.cpp"
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
        self.assertIn("ROR_SOURCE_MANIFEST_SHA256", self.entry_cmake)
        self.assertIn("ror_source_identity", RUNNER_PATH.read_text(encoding="utf-8"))
        self.assertIn("ror_relevant_source_manifest_sha256", self.smoke)

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
        ):
            self.assertIn(license_name, self.entry_cmake)
        self.assertIn("validate_n1_package", RUNNER_PATH.read_text(encoding="utf-8"))
        self.assertIn("ror_ogre_next_frontend_n1_media_tamper", self.entry_cmake)
        self.assertIn("VerifyN1MediaTamper.cmake", self.entry_cmake)
        self.assertIn("--media-root", self.entry_cmake)
        self.assertIn("relative shader media root did not fail closed", self.smoke)
        self.assertIn("missing shader media root did not fail closed", self.smoke)

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
            "light.casts_shadows",
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
            "UploadedTextureChannel::GREEN",
            "UploadedTextureChannel::BLUE",
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
        ):
            self.assertIn(token, self.frontend)
        self.assertIn("pinned PBS reconstructs positive Z", self.policy)
        self.assertIn("only the", self.policy)
        self.assertIn("texture/sampler pairs actually referenced", self.policy)
        self.assertIn("--modern-pbr", self.smoke)
        self.assertIn(
            "ror.ogre_next_frontend_rt4_pbr_v1_smoke.v1", self.smoke
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
        self.assertIn("unused_packed_rgba_allocations", self.smoke)

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
