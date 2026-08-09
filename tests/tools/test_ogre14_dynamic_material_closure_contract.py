#!/usr/bin/env python3
"""Static contract tests for exact translated deformable materials."""

from __future__ import annotations

from pathlib import Path
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
RENDER = REPOSITORY_ROOT / "source/main/gfx/render"
SCENE_HEADER = RENDER / "Ogre14GraphicsSceneSource.h"
SCENE_SOURCE = SCENE_HEADER.with_suffix(".cpp")
CPP_TEST = (
    REPOSITORY_ROOT
    / "tests/gfx/render/Ogre14DynamicMaterialClosureTests.cpp"
)
README = RENDER / "README.md"
PRODUCER_DOC = (
    REPOSITORY_ROOT / "doc/nextgen/GRAPHICS_SCENE_SNAPSHOT_PRODUCER.md"
)


class Ogre14DynamicMaterialClosureContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.scene_header = SCENE_HEADER.read_text(encoding="utf-8")
        cls.scene_source = SCENE_SOURCE.read_text(encoding="utf-8")
        cls.cpp_test = CPP_TEST.read_text(encoding="utf-8")

    def test_dynamic_input_reuses_the_static_exact_closure_contract(self) -> None:
        for token in (
            "kMaximumOgre14GraphicsSceneDynamicSections = 65536U",
            "kMaximumOgre14GraphicsSceneDynamicAssets = 65536U",
            "std::shared_ptr<const Ogre14LegacyMaterialClosure> resolved_material",
            "bool mesh_reverse_winding = false",
            "Ogre14GraphicsSceneResolvedMaterialFrameLineage",
            "ValidateOgre14GraphicsSceneResolvedMaterialFrameLineage",
            "IOgre14GraphicsSceneDynamicInventoryFaultInjector",
            "canonical_assets_by_asset_key_",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.scene_header)

    def test_dynamic_transaction_is_binding_aware_bounded_and_atomic(self) -> None:
        method = self.scene_source[
            self.scene_source.index(
                "ValidationResult BuildOgre14GraphicsSceneDynamicInventory"
            ) : self.scene_source.index(
                "ValidationResult BuildOgre14GraphicsSceneMaterialFallback"
            )
        ]
        for token in (
            "ValidateOgre14LegacyMaterialClosure",
            "resolved_source_sequence",
            "resolved_catalog_sequence",
            "resolved_material->material_source_asset_id",
            "resolved_material->requires_reverse_winding",
            "resolved_material->assets",
            "BuildOgre14LegacyStableAssetKey",
            "ValidateProducerBoundMaterialMeshCompatibility",
            "EquivalentGraphicsSceneAssetInput",
            "lhs.material_bindings == rhs.material_bindings",
            "canonical_assets_by_asset_key_",
            "AFTER_FIRST_RESOLVED_DEPENDENCY",
            "CheckedAddSize",
            "dynamic_inventory.assets",
            "dynamic_inventory.registry_lifetime",
            "catch (const std::bad_alloc &)",
            "dynamic_inventory.exception",
        ):
            with self.subTest(token=token):
                self.assertIn(token, method if token != "lhs.material_bindings == rhs.material_bindings" else self.scene_source)
        self.assertNotIn("inputs.size() *", method)
        self.assertIn(
            "if (resolved_material != nullptr) {\n"
            "      proposed_material_assets = resolved_material->assets;\n"
            "    } else {",
            method,
        )

    def test_joined_lineage_preflight_covers_static_and_dynamic(self) -> None:
        lineage = self.scene_source[
            self.scene_source.index(
                "ValidationResult ValidateOgre14GraphicsSceneResolvedMaterialFrameLineage"
            ) : self.scene_source.index(
                "ValidationResult BuildOgre14GraphicsSceneDynamicInventory"
            )
        ]
        for token in (
            "static_inputs",
            "dynamic_inputs",
            "ValidateOgre14LegacyMaterialClosure",
            "candidate.source_sequence != closure->source_sequence",
            "candidate.catalog_sequence != closure->catalog_sequence",
            "lineage = candidate",
            "catch (const std::bad_alloc &)",
        ):
            with self.subTest(token=token):
                self.assertIn(token, lineage)

    def test_hostile_native_acceptance_matrix_is_present(self) -> None:
        for token in (
            "TestExactDynamicClosureSharedOwnersAndStaticEquivalence",
            "TestFallbackRemainsExactAndTexturedFailClosed",
            "TestWindingAndJoinedLineageAreExact",
            "TestHostileClosuresCollisionsAndUvGate",
            "TestCapsAndExceptionRollback",
            "TestInjectedExceptionRollback",
            "static and dynamic payload/binding equivalence rules diverged",
            "different source epochs were merged",
            "cross-domain catalog mismatch",
            "forged dynamic dependency order",
            "conflicting payloads",
            "conflicting exact bindings",
            "cross-kind collision",
            "missing authored UV",
            "kMaximumOgre14GraphicsSceneDynamicSections + 1U",
            "dynamic lifetime cap+1 published partial state",
            "allocation exception changed deep registry/output owners or values",
            "unexpected exception changed deep registry/output owners or values",
            "post-fault dynamic deformation owner was not reusable",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.cpp_test)

    def test_build_ci_provenance_and_docs_are_registered(self) -> None:
        test_cmake = (REPOSITORY_ROOT / "tests/CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        self.assertIn("Ogre14DynamicMaterialClosureTests.cpp", test_cmake)
        self.assertIn("ror_ogre14_dynamic_material_closure_tests", test_cmake)
        manifests = (
            REPOSITORY_ROOT / "tools/ogre_next_probe/CMakeLists.txt",
            REPOSITORY_ROOT
            / "tools/ogre_next_probe/cmake/VerifyN2SourceProvenance.cmake",
            REPOSITORY_ROOT / "tools/run_ogre_next_probe.py",
            REPOSITORY_ROOT / "tools/verify_ogre_next_artifact_set.py",
        )
        for manifest in manifests:
            text = manifest.read_text(encoding="utf-8")
            with self.subTest(manifest=manifest.name):
                self.assertIn(
                    "tests/gfx/render/Ogre14DynamicMaterialClosureTests.cpp",
                    text,
                )
                self.assertIn(
                    "tests/tools/test_ogre14_dynamic_material_closure_contract.py",
                    text,
                )
        for document in (README, PRODUCER_DOC):
            text = document.read_text(encoding="utf-8")
            with self.subTest(document=document.name):
                self.assertIn(
                    "ValidateOgre14GraphicsSceneResolvedMaterialFrameLineage",
                    text,
                )
                self.assertIn("mesh_reverse_winding", text)


if __name__ == "__main__":
    unittest.main()
