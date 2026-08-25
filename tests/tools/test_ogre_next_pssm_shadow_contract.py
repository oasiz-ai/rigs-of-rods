#!/usr/bin/env python3
"""Offline fail-closed contract for directional PSSM_3_CASCADE_V1."""

from __future__ import annotations

import contextlib
import copy
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
RENDER_ROOT = REPOSITORY_ROOT / "source/main/gfx/render/ogrenext"
PROBE_ROOT = REPOSITORY_ROOT / "tools/ogre_next_probe"
LOCK_PATH = PROBE_ROOT / "ogre-next-pssm-shadow-v1.lock.json"
CANONICAL_LOCK_PATH = PROBE_ROOT / "ogre-next.lock.json"
VERIFIER_PATH = PROBE_ROOT / "verify_pssm_shadow_source_closure.py"
SPEC = importlib.util.spec_from_file_location("verify_pssm_shadow_closure", VERIFIER_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load the PSSM source-closure verifier")
VERIFIER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERIFIER)


class OgreNextPssmShadowContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.lock = json.loads(LOCK_PATH.read_text(encoding="utf-8"))
        cls.policy_header = (RENDER_ROOT / "OgreNextPssmShadowPolicy.h").read_text(
            encoding="utf-8"
        )
        cls.policy = (RENDER_ROOT / "OgreNextPssmShadowPolicy.cpp").read_text(
            encoding="utf-8"
        )
        cls.frontend_header = (RENDER_ROOT / "OgreNextN1Frontend.h").read_text(
            encoding="utf-8"
        )
        cls.hdr_topology_header = (
            RENDER_ROOT / "OgreNextHdrSceneTopology.h"
        ).read_text(encoding="utf-8")
        cls.frontend = (RENDER_ROOT / "OgreNextN1Frontend.cpp").read_text(
            encoding="utf-8"
        )
        cls.smoke = (PROBE_ROOT / "src/pssm_shadow_smoke.cpp").read_text(
            encoding="utf-8"
        )
        cls.cmake = (PROBE_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        cls.runner = (PROBE_ROOT / "run_pssm_shadow.py").read_text(
            encoding="utf-8"
        )
        cls.artifact_verifier = (
            REPOSITORY_ROOT / "tools/verify_ogre_next_artifact_set.py"
        ).read_text(encoding="utf-8")
        cls.workflow = (
            REPOSITORY_ROOT / ".github/workflows/ogre-next-probe.yml"
        ).read_text(encoding="utf-8")

    def write_lock(self, value: object, root: Path) -> Path:
        path = root / "mutated.lock.json"
        path.write_text(json.dumps(value), encoding="utf-8")
        return path

    def test_source_closure_is_exact_and_matches_canonical_pin(self) -> None:
        validated = VERIFIER.validate_lock(LOCK_PATH, CANONICAL_LOCK_PATH)
        self.assertEqual(validated["ogre_next_commit"], VERIFIER.OGRE_NEXT_COMMIT)
        self.assertEqual(len(validated["sources"]), 86)
        self.assertEqual(
            validated["platform_policies"], VERIFIER.PLATFORM_POLICIES
        )
        texture_sources = {
            record["role"]: record["sha256"]
            for record in validated["sources"]
            if record["role"].startswith("texture_gpu_manager_")
        }
        self.assertEqual(
            texture_sources,
            {
                "texture_gpu_manager_api": (
                    "de05f16c0ec931e42d46fdcd55557269f6b9ccf9b2be0b2c4c99baa0c098a100"
                ),
                "texture_gpu_manager_allocation_runtime": (
                    "e05b007104f5eb7877ffb2842fe0b0bca631585d948dfee501396afec994ce38"
                ),
            },
        )
        movable_source = next(
            record
            for record in validated["sources"]
            if record["role"] == "movable_shadow_flag_masks"
        )
        canonical = json.loads(CANONICAL_LOCK_PATH.read_text(encoding="utf-8"))
        light_list_patch = next(
            patch
            for patch in canonical["patches"]
            if patch["path"]
            == "patches/0014-light-list-tail-lane-isolation.patch"
        )
        self.assertEqual(movable_source["path"], light_list_patch["source_path"])
        self.assertEqual(
            movable_source["sha256"], light_list_patch["patched_sha256"]
        )
        scene_source = next(
            record
            for record in validated["sources"]
            if record["role"] == "scene_caster_bounds_runtime"
        )
        capacity_patch = next(
            patch
            for patch in canonical["patches"]
            if patch["path"]
            == "patches/0015-global-light-list-packed-capacity.patch"
        )
        self.assertEqual(scene_source["path"], capacity_patch["source_path"])
        self.assertEqual(
            scene_source["sha256"], capacity_patch["patched_sha256"]
        )
        reference = os.environ.get("ROR_OGRE_NEXT_REFERENCE_SOURCE_ROOT")
        if reference:
            reference_policy = os.environ.get(
                "ROR_OGRE_NEXT_REFERENCE_PLATFORM_POLICY"
            )
            self.assertIn(reference_policy, VERIFIER.PLATFORM_POLICIES)
            VERIFIER.verify_source_root(
                validated, Path(reference), reference_policy
            )

    def test_windows_patch_digest_is_an_exact_platform_override(self) -> None:
        validated = VERIFIER.validate_lock(LOCK_PATH, CANONICAL_LOCK_PATH)
        self.assertEqual(
            [
                (
                    record["platform_policy"],
                    record["role"],
                    record["path"],
                    record["sha256"],
                )
                for record in validated["platform_source_overrides"]
            ],
            VERIFIER.PLATFORM_SOURCE_OVERRIDES,
        )
        source = next(
            record
            for record in validated["sources"]
            if record["role"] == "d3d11_render_system_capabilities"
        )
        self.assertEqual(
            source["sha256"],
            "036cb9ed4666a36839d03c8fe66eb0de05e516dc94c1f6642e3192a9e6acea41",
        )
        self.assertEqual(
            VERIFIER._expected_source_digest(
                validated, source, "windows-x64-d3d11"
            ),
            "d27e8af72005cadda20834ce67bb5cb476a83f03dc9280d5fc3aff0759330b7a",
        )
        for policy in ("macos-arm64-metal", "linux-x86_64-vulkan"):
            self.assertEqual(
                VERIFIER._expected_source_digest(validated, source, policy),
                source["sha256"],
            )

    def test_source_digest_selection_and_diagnostics_are_platform_exact(self) -> None:
        pristine = b"pristine upstream bytes\n"
        windows_patched = b"reviewed Windows patch bytes\n"
        relative = "RenderSystems/Direct3D11/src/OgreD3D11RenderSystem.cpp"
        record = {
            "role": "d3d11_render_system_capabilities",
            "path": relative,
            "sha256": hashlib.sha256(pristine).hexdigest(),
        }
        lock = {
            "platform_source_overrides": [
                {
                    "platform_policy": "windows-x64-d3d11",
                    "role": record["role"],
                    "path": relative,
                    "sha256": hashlib.sha256(windows_patched).hexdigest(),
                }
            ]
        }
        with tempfile.TemporaryDirectory(prefix="ror-pssm-platform-digest-") as temporary:
            root = Path(temporary).resolve()
            source = root / relative
            source.parent.mkdir(parents=True)
            source.write_bytes(pristine)
            VERIFIER._verify_source_record(
                lock, root, 69, record, "macos-arm64-metal"
            )
            with self.assertRaisesRegex(
                VERIFIER.VerificationError,
                rf"index 69 for windows-x64-d3d11: .*expected=.*got=",
            ):
                VERIFIER._verify_source_record(
                    lock, root, 69, record, "windows-x64-d3d11"
                )
            source.write_bytes(windows_patched)
            VERIFIER._verify_source_record(
                lock, root, 69, record, "windows-x64-d3d11"
            )
            with self.assertRaisesRegex(
                VERIFIER.VerificationError,
                rf"index 69 for linux-x86_64-vulkan: .*expected=.*got=",
            ):
                VERIFIER._verify_source_record(
                    lock, root, 69, record, "linux-x86_64-vulkan"
                )

    def test_lock_rejects_unknown_keys_reordering_and_hash_drift(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-pssm-lock-") as temporary:
            root = Path(temporary)
            for label, mutate in (
                (
                    "unknown key",
                    lambda value: value.update(unreviewed=True),
                ),
                (
                    "source reordering",
                    lambda value: value["sources"].reverse(),
                ),
                (
                    "uppercase hash",
                    lambda value: value["sources"][0].update(
                        sha256=value["sources"][0]["sha256"].upper()
                    ),
                ),
                (
                    "path traversal",
                    lambda value: value["sources"][0].update(path="../escape"),
                ),
                (
                    "platform override drift",
                    lambda value: value["platform_source_overrides"][0].update(
                        sha256="0" * 64
                    ),
                ),
                (
                    "platform override removal",
                    lambda value: value.update(platform_source_overrides=[]),
                ),
            ):
                with self.subTest(label=label):
                    value = copy.deepcopy(self.lock)
                    mutate(value)
                    with self.assertRaises(VERIFIER.VerificationError):
                        VERIFIER.validate_lock(
                            self.write_lock(value, root), CANONICAL_LOCK_PATH
                        )

    def test_lock_rejects_duplicate_root_and_nested_json_keys(self) -> None:
        canonical = LOCK_PATH.read_text(encoding="utf-8")
        root_duplicate = canonical.replace(
            '{\n  "schema_version": 2,',
            '{\n  "schema_version": 2,\n  "schema_version": 2,',
            1,
        )
        nested_duplicate = canonical.replace(
            '"role": "shadow_node_api",',
            '"role": "shadow_node_api",\n      "role": "shadow_node_api",',
            1,
        )
        with tempfile.TemporaryDirectory(prefix="ror-pssm-duplicate-") as temporary:
            root = Path(temporary)
            for label, payload in (
                ("root", root_duplicate),
                ("nested", nested_duplicate),
            ):
                with self.subTest(label=label):
                    path = root / f"{label}.json"
                    path.write_text(payload, encoding="utf-8")
                    with self.assertRaisesRegex(
                        VERIFIER.VerificationError, "duplicate JSON object key"
                    ):
                        VERIFIER.validate_lock(path, CANONICAL_LOCK_PATH)

    def test_platform_policy_cli_is_bound_to_source_verification(self) -> None:
        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                VERIFIER.parse_arguments(
                    [
                        "--contract-only",
                        "--platform-policy",
                        "windows-x64-d3d11",
                    ]
                )
            with self.assertRaises(SystemExit):
                VERIFIER.parse_arguments(["--source-root", "."])
        parsed = VERIFIER.parse_arguments(
            [
                "--source-root",
                ".",
                "--platform-policy",
                "windows-x64-d3d11",
            ]
        )
        self.assertEqual(parsed.platform_policy, "windows-x64-d3d11")

    def test_shadow_mode_is_opt_in_and_rt4_only(self) -> None:
        self.assertRegex(
            self.frontend_header,
            r"directional_shadow_mode\s*=\s*\n?\s*OgreNextDirectionalShadowMode::DISABLED",
        )
        for token in (
            "PSSM_3_CASCADE_V1",
            "MODERN_PBR_RT4_V1",
            "requires exactly one shadow-casting directional light",
            "does not substitute local-light shadows",
            "requires a nonzero static/dynamic geometry mask",
            "requires the exact pinned view near and far clip distances",
            "view.far_plane != kOgreNextExpectedViewFarMeters",
        ):
            self.assertIn(token, self.policy)
        self.assertIn("kOgreNextPssmFarMeters", self.policy_header)
        self.assertIn("kOgreNextExpectedViewFarMeters", self.policy_header)
        self.assertIn("shadow_flags != 0U", self.policy)

    def test_shadow_visibility_excludes_reflection_and_ogre_layers(self) -> None:
        self.assertIn(
            "kOgreNextPssmNativeVisibilityMask =\n"
            "    kOgreNextRt4AuthoredVisibilityMask",
            self.policy_header,
        )
        self.assertIn(
            "shadow_plan.native_visibility_mask != authored_view_visibility",
            self.frontend,
        )

    def test_hdr_showcase_binds_real_pssm_to_exactly_one_populated_scene(self) -> None:
        for token in (
            "enum class OgreNextHdrSceneTopology",
            "SINGLE_EVALUATION_PSSM_V1",
        ):
            self.assertIn(token, self.hdr_topology_header)
        for token in (
            "AFTER_SINGLE_SCENE_PSSM_DEFINITION",
            "AFTER_SINGLE_SCENE_PSSM_WORKSPACE_RECREATE",
        ):
            self.assertIn(token, self.frontend_header)

        bind_start = self.frontend.index("void BindAndVerifyPssmWorkspace(")
        bind_end = self.frontend.index(
            "void UnbindAndVerifyPssmWorkspace(", bind_start
        )
        bind = self.frontend[bind_start:bind_end]
        for token in (
            "scene_pass->mShadowNode = Ogre::IdString(shadow_node_name);",
            "scene_pass->mShadowNodeRecalculation = Ogre::SHADOW_NODE_FIRST_ONLY;",
            "bound_shadow_scene_passes != 1U",
            "candidate->mIdentifier != scene_pass_identifier",
        ):
            self.assertIn(token, bind)

        unbind_start = bind_end
        unbind_end = self.frontend.index(
            "struct NativePssmReadback final", unbind_start
        )
        unbind = self.frontend[unbind_start:unbind_end]
        for token in (
            "matching_scene_passes != 1U",
            "selected->mShadowNode = Ogre::IdString();",
            "bound_shadow_scene_passes != 0U",
        ):
            self.assertIn(token, unbind)

        finalize_start = self.frontend.index(
            "RenderOperationResult FinalizeSingleSceneHdrPssm("
        )
        finalize_end = self.frontend.index(
            "RenderOperationResult CreateHdrCompositor(", finalize_start
        )
        finalize = self.frontend[finalize_start:finalize_end]
        for token in (
            "directional_lights != 1U",
            "shadow_casters == 0U",
            "shadow_receivers == 0U",
            "CreateAndVerifyPssmShadowNode(",
            "BindAndVerifyPssmWorkspace(",
            "hdr_workspace->recreateAllNodes();",
            "RefreshSingleSceneHdrRuntimeTargets(true)",
            "hdr_pssm_finalized_with_populated_scene = true;",
        ):
            self.assertIn(token, finalize)

        render_start = self.frontend.index(
            "RenderOperationResult OgreNextN1Frontend::Render("
        )
        render = self.frontend[render_start:]
        finalize_call = render.index("impl_->FinalizeSingleSceneHdrPssm(")
        pssm_readback = render.index("ReadAndVerifyNativePssmState(")
        self.assertLess(finalize_call, pssm_readback)
        self.assertNotIn('"active_lights=0"', self.frontend)

    def test_reviewed_cascade_and_filter_values_are_literal_and_read_back(self) -> None:
        for declaration in (
            "kOgreNextPssmCascadeCount = 3U",
            "kOgreNextPssmNearMeters = 0.5F",
            "kOgreNextPssmFarMeters = 350.0F",
            "kOgreNextPssmLambda = 0.97F",
            "kOgreNextPssmSplitBlend = 0.125F",
            "kOgreNextPssmSplitPaddingMeters = 1.0F",
            "kOgreNextPssmStableCascadeCount = 1U",
            "kOgreNextPssmPcfKernelWidth = 4U",
            "kOgreNextPssmAtlasWidth = 2048U",
            "kOgreNextPssmAtlasHeight = 3072U",
            "{2048U, 2048U, 0U, 0U}",
            "{1024U, 1024U, 0U, 2048U}",
            "{1024U, 1024U, 1024U, 2048U}",
        ):
            self.assertIn(declaration, self.policy_header)
        for token in (
            "Ogre::PFG_D32_FLOAT",
            "Ogre::HlmsPbs::PCF_4x4",
            "createShadowNodeWithSettings",
            "validateAllNodes()",
            "SHADOW_NODE_FIRST_ONLY",
            "getPssmSplits(0U)",
            "getPssmBlends(0U)",
            "getPssmFade(0U)",
            "getNormalOffsetBias(index)",
            "getNumActiveShadowCastingLights() != 1U",
            "FET_TAN_HALF_ANGLES",
            "getProjectionMatrix()",
        ):
            self.assertIn(token, self.frontend)

    def test_d32_capability_is_a_transactional_native_allocation_and_readback(self) -> None:
        for token in (
            "ProbePssmD32Atlas",
            "RoRPssmD32AtlasCapabilityProbe",
            "Ogre::PFG_D32_FLOAT",
            "Ogre::TextureFlags::RenderToTexture",
            "Ogre::GpuResidency::Resident",
            "readback.convertFromTexture",
            "texture_manager.destroyTexture",
            "findTextureNoThrow",
            "d32_atlas_allocation_verified",
            "d32_atlas_readback_verified",
            "d32_atlas_cleanup_verified",
            "AFTER_D32_ATLAS_CREATE",
            "DURING_D32_ATLAS_CLEANUP_LOOKUP",
            "std::rethrow_exception(operation_failure)",
        ):
            self.assertIn(token, self.frontend)
        self.assertNotIn("checkSupport(", self.frontend)

    def test_off_center_projection_and_tight_bounds_have_native_pixels(self) -> None:
        for token in (
            "TightReceiverMesh",
            "tight_receiver_bounds",
            "last_native_bounds_observations",
            "native_aabb_observations",
            "0.25F",
            "-0.125F",
            "off_center_tight_bounds",
            "off_center_projection_verified",
            "tight_caster_bounds_verified",
            '\\"projection_and_bounds_fixture\\"',
        ):
            self.assertIn(token, self.smoke)
        for token in (
            "render_mesh.mesh->getAabb()",
            "item->getLocalAabb()",
            "item->getWorldAabbUpdated()",
            "node->_getFullTransformUpdated()",
            "NearlyEqualNativeTransform",
            "NearlyEqualNativeTransformedAabb",
        ):
            self.assertIn(token, self.frontend)

    def test_challenged_execution_is_atomically_bound_to_all_artifacts(self) -> None:
        for token in (
            "secrets.token_hex(32)",
            "--execution-challenge",
            "os.replace",
            "os.fsync",
            "ror.ogre_next_pssm_shadow_execution_receipt.v1",
            "ror.ogre_next_pssm_shadow_attestation.v1",
            "ror.ogre_next_pssm_shadow_artifact_manifest.v1",
            "external_dsse_required",
        ):
            self.assertIn(token, self.runner)
        for token in (
            "_pssm_entrypoint_bytes",
            "PSSM_EXECUTION_RECEIPT_SCHEMA",
            "PSSM_ATTESTATION_SCHEMA",
            "PSSM_ARTIFACT_MANIFEST_SCHEMA",
            "challenge_nonce",
            "PSSM_OFFLINE_EXECUTION_LIMITATION",
        ):
            self.assertIn(token, self.artifact_verifier)

    def test_mesh_light_masks_receivers_and_ui_free_output_are_explicit(self) -> None:
        for token in (
            "MeshInstanceCastsShadowForLight",
            "ClassifyShadowGeometry",
            "MESH_INSTANCE_RECEIVES_SHADOW",
        ):
            self.assertIn(token, self.policy)
        for token in (
            "setCastShadows(casts_shadow)",
            "setReceiveShadows(false)",
            '"PssmNonReceiver_i"',
            "mIncludeOverlays = false",
            "PixelFormat::RGBA16_FLOAT",
            "PixelFormat::RGBA8_SRGB",
        ):
            self.assertTrue(token in self.frontend or token in self.smoke)
        receiver_clone = self.frontend[
            self.frontend.index("const auto create_receiver_clone") :
            self.frontend.index("const auto create_retained_instance")
        ]
        for token in (
            "OgreNextUvAffinePbs::SelectsThinSlabTransmissionShader",
            "std::string(thin_slab_transmission",
            "kOgreNextThinSlabPbsDatablockPrefix",
            ": kOgreNextUvAffinePbsDatablockPrefix",
        ):
            self.assertIn(token, receiver_clone)

    def test_native_isolation_proof_toggles_only_casting_and_fails_closed(self) -> None:
        for token in (
            "controlled_visual_change",
            "occluder_instance_casts_shadow",
            "changed_pixels_outside_reviewed_receiver_region",
            "changed_pixels_inside_reviewed_occluder_region",
            "shadow_disabled_default_equals_explicit",
            "disabled_default == disabled_explicit",
            "backend_substitution",
            "split_stable_tangent_projection",
            "cascade_index",
            "AFTER_RECEIVER_DATABLOCK_CLONE",
            "AFTER_WORKSPACE_NODE_DEFINITION",
            "DURING_RECEIVER_DATABLOCK_CLEANUP_LOOKUP",
            "DURING_WORKSPACE_DEFINITION_CLEANUP_LOOKUP",
            "DURING_WORKSPACE_NODE_CLEANUP_LOOKUP",
            "DURING_SHADOW_NODE_CLEANUP_LOOKUP",
            "DURING_TARGET_TEXTURE_CLEANUP_LOOKUP",
            "cleanup_absence_checks",
            "return kUnsupportedExitCode",
        ):
            self.assertIn(token, self.smoke)
        self.assertNotIn("fallback", self.smoke.lower())

    def test_standalone_build_and_three_platform_ci_are_required(self) -> None:
        for token in (
            "ror_ogre_next_pssm_shadow_smoke",
            "ror_ogre_next_pssm_shadow_report",
            "ror_ogre_next_pssm_source_closure_verify",
            "SKIP_RETURN_CODE 77",
            "verify_pssm_shadow_source_closure.py",
            "ogre-next-pssm-shadow-v1.lock.json",
            '--platform-policy "${ROR_OGRE_NEXT_PLATFORM_POLICY}"',
        ):
            self.assertIn(token, self.cmake)
        for policy in VERIFIER.PLATFORM_POLICIES:
            self.assertIn(f"platform: {policy}", self.workflow)
        self.assertEqual(
            self.workflow.count("test_ogre_next_pssm_shadow_contract.py"), 2
        )
        for artifact in (
            "ror-ogre-next-pssm-shadow-report.json",
            "ror-ogre-next-pssm-shadow-isolation.bin",
            "ror-ogre-next-pssm-shadow-execution-receipt.json",
            "ror-ogre-next-pssm-shadow-attestation.json",
            "ror-ogre-next-pssm-shadow-artifact-manifest.json",
            "ror-ogre-next-pssm-shadow-execution-receipt.sigstore.jsonl",
            "bin/ror_ogre_next_pssm_shadow_smoke",
        ):
            self.assertIn(artifact, self.workflow)
        self.assertIn("_verify_pssm", self.workflow)
        self.assertIn("require_pass=True", self.workflow)
        self.assertIn("Require directional PSSM native pass", self.workflow)
        self.assertNotIn(
            "Require directional PSSM pass or explicit unsupported evidence",
            self.workflow,
        )
        self.assertIn("attest_pssm_receipt", self.workflow)
        self.assertIn("gh attestation verify", self.workflow)


if __name__ == "__main__":
    unittest.main()
