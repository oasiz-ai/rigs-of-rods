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
            "std::map<std::string, const Ogre14LegacyTextureInput *",
            "!SameTexture(*existing->second, texture)",
            "live native texture mip bytes are not canonical and",
            '"tightly packed"',
            "observed native texture bytes exceed the configured frame cap",
            "derived native asset count exceeds the configured cap",
            "native capture must carry exactly its referenced v1 textures",
            "native material capture disagrees with its exact semantic",
            '"declaration"',
        ):
            self.assertIn(token, self.header + self.source)
        copy_index = self.source.index("frame_input.textures.push_back(*entry.second)")
        lease_index = self.source.index("translator_->BeginCommittableTransaction(")
        self.assertLess(copy_index, lease_index)

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
            "catch (const std::bad_alloc &)",
            "catch (...) ",
        ):
            self.assertIn(token, self.header + self.source)

    def test_runtime_gate_covers_lineage_rollback_and_hostile_inputs(self) -> None:
        for token in (
            "shared texture capture was accepted",
            "duplicate material observation was accepted",
            "material without semantic declaration was accepted",
            "native capture with forged semantics was accepted",
            "material observation with an empty semantic receipt was accepted",
            "material observation from a different registry build was accepted",
            "padded native texture payload was copied or accepted",
            "repeated observed texture bytes escaped the aggregate source cap",
            "material observation count cap+1 was accepted",
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
            "prepared material lookup did not retain exact closures",
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
