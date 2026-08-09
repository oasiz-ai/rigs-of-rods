#!/usr/bin/env python3
"""Static contract for atomic live OGRE 14 material preparation."""

from __future__ import annotations

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "source/main/gfx/ogre14/Ogre14LegacyLiveMaterialCoordinator.h"
SOURCE = ROOT / "source/main/gfx/ogre14/Ogre14LegacyLiveMaterialCoordinator.cpp"
CPP_TEST = ROOT / "tests/gfx/ogre14/Ogre14LegacyLiveMaterialCoordinatorTests.cpp"
PATHS = (
    "source/main/gfx/ogre14/Ogre14LegacyLiveMaterialCoordinator.cpp",
    "source/main/gfx/ogre14/Ogre14LegacyLiveMaterialCoordinator.h",
    "tests/gfx/ogre14/Ogre14LegacyLiveMaterialCoordinatorTests.cpp",
    "tests/tools/test_ogre14_live_material_coordinator_contract.py",
)


class Ogre14LiveMaterialCoordinatorContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = HEADER.read_text(encoding="utf-8")
        cls.source = SOURCE.read_text(encoding="utf-8")
        cls.cpp_test = CPP_TEST.read_text(encoding="utf-8")

    def test_one_owned_registry_and_exclusive_translator_drive_the_frame(self) -> None:
        for token in (
            "Ogre14LegacyMaterialSemanticRegistry semantic_registry_",
            "std::unique_ptr<Ogre14LegacyAssetTranslator> translator_",
            "std::unique_ptr<PendingFrame> pending_",
            "semantic_registry.Resolve(",
            "BeginCommittableTransaction(",
            "ResolveOgre14LegacyMaterialClosureBatch(",
            "CommitAfterAcceptedExposure()",
        ):
            self.assertIn(token, self.header + self.source)
        self.assertNotIn("CommitTransaction(", self.source)

    def test_observations_are_exact_bounded_and_canonical(self) -> None:
        for token in (
            "maximum_material_observations",
            "observations.size() > configuration_.maximum_material_observations",
            "source_sequence != translator_->source_sequence() + 1U",
            "BuildOgre14LegacyStableAssetKey(",
            "DUPLICATE_IDENTIFIER",
            "Ogre14LegacyMaterialSemanticResolutionAuthenticates(",
            "native_material_audit_receipt.Authenticates(",
            "DeriveOgre14LegacyMaterialPipelineAudit(",
            "EquivalentOgre14LegacyMaterialPipelineAudit(",
            "std::owner_less<NativeAuditOwner>",
            "SameNativeCapture(",
            "std::map<std::string, const Ogre14LegacyTextureInput *",
            "!SameTexture(*existing->second, texture)",
            "live native texture mip bytes are not canonical and",
            '"tightly packed"',
            "observed native texture bytes exceed the configured frame cap",
            "derived native asset count exceeds the configured cap",
            "Ogre14LegacyAssetIdentityFrameView identity_view",
            "translator_->PreflightLifetimeAdmission(identity_view)",
            "native capture must carry exactly its referenced v1 textures",
            "native material capture disagrees with its exact semantic",
            '"declaration"',
        ):
            self.assertIn(token, self.header + self.source)
        preflight_index = self.source.index(
            "translator_->PreflightLifetimeAdmission(identity_view)"
        )
        material_copy_index = self.source.index(
            "frame_input.materials.push_back(*material)"
        )
        texture_copy_index = self.source.index(
            "frame_input.textures.push_back(*texture)"
        )
        lease_index = self.source.index("translator_->BeginCommittableTransaction(")
        self.assertLess(preflight_index, material_copy_index)
        self.assertLess(preflight_index, texture_copy_index)
        self.assertLess(preflight_index, lease_index)
        byte_charge_index = self.source.index(
            "observed_texture_bytes = next_observed_texture_bytes"
        )
        canonicalization_index = self.source.index(
            "std::vector<IndexedObservation> canonical_observations"
        )
        self.assertLess(byte_charge_index, canonicalization_index)

        closure_resolution_index = self.source.index(
            "ResolveOgre14LegacyMaterialClosureBatch("
        )
        native_match_index = self.source.index(
            "EquivalentOgre14LegacyMaterialPipelineAudit(*native_audit"
        )
        prepared_native_owner_index = self.source.index(
            "material.native_material_audit = native_audit"
        )
        self.assertLess(closure_resolution_index, native_match_index)
        self.assertLess(native_match_index, prepared_native_owner_index)

    def test_publication_and_lookup_fail_closed(self) -> None:
        copy_index = self.source.index("candidate_pending->prepared = prepared")
        output_index = self.source.index("output = std::move(prepared)")
        pending_index = self.source.index("pending_ = std::move(candidate_pending)")
        self.assertLess(copy_index, output_index)
        self.assertLess(output_index, pending_index)
        for token in (
            "class Ogre14LegacyPreparedMaterialFrame final",
            "struct State;",
            "std::shared_ptr<const State> state_",
            "SharesImmutableStateWith(accepted_frame)",
            "PREPARED_FRAME_MISMATCH",
            "std::is_nothrow_move_assignable<Ogre14LegacyPreparedMaterialFrame>",
            "std::is_nothrow_copy_assignable<Ogre14LegacyPreparedMaterialFrame>",
            "std::is_nothrow_move_assignable<decltype(pending_)>",
            "FindOgre14LegacyPreparedMaterialClosure(",
            "FindOgre14LegacyPreparedMaterial(",
            "SharesControlBlock(native_audit, closure.material_audit)",
            "catch (const std::bad_alloc &)",
            "catch (...) ",
        ):
            self.assertIn(token, self.header + self.source)

    def test_runtime_gate_covers_lineage_rollback_and_hostile_inputs(self) -> None:
        for token in (
            "shared texture capture was accepted",
            "shared material observations did not reuse one canonical native",
            "same material value under different native audit owners was accepted",
            "missing native material audit owner was accepted",
            "same-value reboxed native audit bypassed the opaque capture receipt",
            "translated closure audit owner was laundered as native capture",
            "authenticated translated closure owner escaped the post-translation",
            "native audit with mismatched cull was accepted",
            "native audit with mismatched pipeline was accepted",
            "native audit with mismatched texture identity was accepted",
            "one untextured native audit owner identified two exact materials",
            "material without semantic declaration was accepted",
            "native capture with forged semantics was accepted",
            "material observation with an empty semantic receipt was accepted",
            "material observation from a different registry build was accepted",
            "padded native texture payload was copied or accepted",
            "repeated observed texture bytes escaped the aggregate source cap",
            "shared native audit owner waived duplicated texture-byte admission",
            "material observation count cap+1 was accepted",
            "unique native texture count cap+1 was accepted",
            "derived live-asset cap+1 was accepted",
            "decoded-byte cap+1 was accepted",
            "lifetime asset cap+1 was accepted",
            "exhausted exclusive publication epoch was accepted",
            "bad_alloc mutated deep output owners",
            "unexpected exception mutated deep output owners",
            "fresh scene generation reused an opaque catalog identity",
            "authoritative empty material inventory did not commit",
            "material frame committed twice",
            "stale discarded output committed a different retry candidate",
            "discarded identical frame committed a fresh immutable retry state",
            "copied handle sharing the accepted immutable state did not commit",
            "SharesExactOwner(",
            "!lhs.owner_before(rhs)",
            "!rhs.owner_before(lhs)",
            "prepared material lookup did not retain exact closures",
            "Ogre14LegacyNativeMaterialAuditTestAccess",
            "AuthenticateExistingOwnerForHostileTesting",
        ):
            self.assertIn(token, self.cpp_test)

    def test_native_and_cross_platform_builds_execute_the_cpp_gate(self) -> None:
        native_cmake = (ROOT / "tests/CMakeLists.txt").read_text(encoding="utf-8")
        probe_cmake = (ROOT / "tools/ogre_next_probe/CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        main_cmake = (ROOT / "source/main/CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        workflow = (ROOT / ".github/workflows/ogre-next-probe.yml").read_text(
            encoding="utf-8"
        )
        for cmake in (native_cmake, probe_cmake):
            self.assertIn("ror_ogre14_live_material_coordinator_tests", cmake)
            self.assertIn("Ogre14LegacyLiveMaterialCoordinatorTests.cpp", cmake)
            self.assertIn("Ogre14LegacyLiveMaterialCoordinator.cpp", cmake)
            self.assertIn("Ogre14LegacyMaterialSemanticRegistry.cpp", cmake)
        self.assertIn(
            "gfx/ogre14/Ogre14LegacyLiveMaterialCoordinator.{h,cpp}", main_cmake
        )
        self.assertIn("ror_ogre14_live_material_coordinator_tests", workflow)
        self.assertIn(
            "-R '^ror_ogre14_live_material_coordinator$'", workflow
        )
        self.assertEqual(
            workflow.count(
                "source/main/gfx/ogre14/Ogre14LegacyLiveMaterialCoordinator.*"
            ),
            2,
        )
        self.assertEqual(
            workflow.count(
                "tests/gfx/ogre14/Ogre14LegacyLiveMaterialCoordinatorTests.cpp"
            ),
            2,
        )

    def test_provenance_manifests_cover_the_complete_coordinator(self) -> None:
        manifests = (
            ROOT / "tools/run_ogre_next_probe.py",
            ROOT / "tools/verify_ogre_next_artifact_set.py",
            ROOT / "tools/ogre_next_probe/CMakeLists.txt",
            ROOT / "tools/ogre_next_probe/cmake/VerifyN2SourceProvenance.cmake",
        )
        for manifest_path in manifests:
            manifest = manifest_path.read_text(encoding="utf-8")
            for path in PATHS:
                with self.subTest(manifest=manifest_path.name, path=path):
                    self.assertIn(path, manifest)


if __name__ == "__main__":
    unittest.main()
