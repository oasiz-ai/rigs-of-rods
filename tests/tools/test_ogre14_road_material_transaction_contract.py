#!/usr/bin/env python3
"""Static contract tests for exact translated procedural-road materials."""

from __future__ import annotations

from pathlib import Path
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
RENDER = REPOSITORY_ROOT / "source/main/gfx/render"
SCENE_HEADER = RENDER / "Ogre14GraphicsSceneSource.h"
SCENE_SOURCE = SCENE_HEADER.with_suffix(".cpp")
ROAD_HEADER = RENDER / "Ogre14ProceduralRoadSource.h"
ROAD_SOURCE = ROAD_HEADER.with_suffix(".cpp")
CLOSURE_HEADER = RENDER / "Ogre14LegacyMaterialClosure.h"
CLOSURE_SOURCE = CLOSURE_HEADER.with_suffix(".cpp")
TRANSLATOR_HEADER = RENDER / "Ogre14LegacyAssetTranslator.h"
TRANSLATOR_SOURCE = TRANSLATOR_HEADER.with_suffix(".cpp")
CPP_TEST = (
    REPOSITORY_ROOT
    / "tests/gfx/render/Ogre14RoadMaterialTransactionTests.cpp"
)
ROAD_CPP_TEST = (
    REPOSITORY_ROOT
    / "tests/gfx/render/Ogre14ProceduralRoadSourceTests.cpp"
)
README = RENDER / "README.md"
PRODUCER_DOC = (
    REPOSITORY_ROOT / "doc/nextgen/GRAPHICS_SCENE_SNAPSHOT_PRODUCER.md"
)


class Ogre14RoadMaterialTransactionContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.scene_header = SCENE_HEADER.read_text(encoding="utf-8")
        cls.scene_source = SCENE_SOURCE.read_text(encoding="utf-8")
        cls.road_header = ROAD_HEADER.read_text(encoding="utf-8")
        cls.road_source = ROAD_SOURCE.read_text(encoding="utf-8")
        cls.closure_header = CLOSURE_HEADER.read_text(encoding="utf-8")
        cls.closure_source = CLOSURE_SOURCE.read_text(encoding="utf-8")
        cls.translator_header = TRANSLATOR_HEADER.read_text(encoding="utf-8")
        cls.translator_source = TRANSLATOR_SOURCE.read_text(encoding="utf-8")
        cls.cpp_test = CPP_TEST.read_text(encoding="utf-8")
        cls.road_cpp_test = ROAD_CPP_TEST.read_text(encoding="utf-8")

    def test_detached_closure_carries_rederivable_exact_identity(self) -> None:
        for token in (
            "material_audit",
            "std::vector<Ogre14LegacyAssetKey> asset_keys",
            "ValidateOgre14LegacyMaterialClosure",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.closure_header)
        for token in (
            "closure.asset_keys.size() != closure.assets.size()",
            "BuildOgre14LegacyStableAssetKey",
            "ValidateAsset(translated_asset",
            "parsed_key != closure.asset_keys[index]",
            "closure.asset_keys.back() != material_key",
            "closure.asset_keys[1U] != sampler_key",
            "candidate.asset_keys.push_back",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.closure_source)

    def test_static_seam_is_bounded_atomic_and_binding_aware(self) -> None:
        for token in (
            "kMaximumOgre14GraphicsSceneStaticSections = 65536U",
            "kMaximumOgre14GraphicsSceneStaticAssets = 65536U",
            "resolved_material",
            "IOgre14GraphicsSceneStaticInventoryFaultInjector",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.scene_header)
        for token in (
            "ValidateOgre14LegacyMaterialClosure",
            "resolved_source_sequence",
            "resolved_catalog_sequence",
            "EquivalentGraphicsSceneAssetInput",
            "lhs.material_bindings == rhs.material_bindings",
            "ValidateProducerBoundMaterialMeshCompatibility",
            "canonical_assets_by_asset_key_",
            "AFTER_FIRST_RESOLVED_DEPENDENCY",
            "catch (const std::bad_alloc &)",
            "static_inventory.assets",
            "static_inventory.registry_lifetime",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.scene_source)
        static_method = self.scene_source[
            self.scene_source.index(
                "ValidationResult BuildOgre14GraphicsSceneStaticInventory"
            ) : self.scene_source.index(
                "ValidationResult BuildOgre14GraphicsSceneEnvironment"
            )
        ]
        self.assertNotIn("inputs.size() * 2U", static_method)

    def test_exact_road_overload_has_no_audit_or_fallback_coercion(self) -> None:
        for token in (
            "exact_native_material_audit",
            "const Ogre14LegacyTranslatedFrame &authoritative_material_frame",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.road_header)
        for token in (
            "EquivalentOgre14LegacyMaterialPipelineAudit",
            "ValidateExactRoadMaterialCapture",
            "authoritative_material_frame->source_sequence",
            "authoritative_material_frame->catalog_sequence",
            "resolved_materials",
            "section.resolved_material",
            "capture.material.texture_unit_count != (textured ? 1U : 0U)",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.road_source)
        exact_method = self.road_source[
            self.road_source.index("ValidateExactRoadMaterialCapture") :
            self.road_source.index("ValidationResult ValidateConfiguration")
        ]
        self.assertNotIn("BuildOgre14GraphicsSceneMaterialFallback", exact_method)
        self.assertIn(
            "EquivalentOgre14LegacyMaterialPipelineAudit",
            self.translator_header,
        )
        self.assertIn("return EquivalentAudit(lhs, rhs)", self.translator_source)

    def test_road_payload_cache_keys_exact_admitted_winding(self) -> None:
        for token in (
            "kOgre14ProceduralRoadWindingProofVersion = 1U",
            "struct Ogre14ProceduralRoadWindingProof",
            "bool reverse_winding = false",
            "bool complete = false",
            "Ogre14ProceduralRoadWindingProof exact_winding_proof",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.road_header)
        for token in (
            "ValidateStandaloneWindingProof",
            "ValidateWindingProofForCapture",
            "ValidateCachedPayloadWinding",
            "previous->exact_winding_proof.reverse_winding ==",
            "winding_proof.reverse_winding",
            "section.mesh_identity.reverse_winding = winding_proof.reverse_winding",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.road_source)

        transaction = self.road_source[
            self.road_source.index(
                "ValidationResult Ogre14ProceduralRoadInventoryTransaction::Build"
            ) : self.road_source.index(
                "ValidationResult BuildOgre14ProceduralRoadInventory("
            )
        ]
        exact_admission = transaction.index("ValidateExactRoadMaterialCapture")
        proof_completion = transaction.index("winding_proof.complete = true")
        cache_resolution = transaction.index(
            "ResolveOgre14ProceduralRoadCacheEntry("
        )
        self.assertLess(exact_admission, proof_completion)
        self.assertLess(proof_completion, cache_resolution)

        for token in (
            "same-geometry cull flip reused oppositely wound payload bytes",
            "NONE-to-CLOCKWISE proof update replaced identical payload bytes",
            "forged cache proof/payload mismatch was reused or published",
            "winding-only replacement double-counted the payload or lifetime cap",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.road_cpp_test)
        for token in (
            "TestExactWindingCacheReplacementAndRollback",
            "same-geometry exact cull flip reused the old owner or exceeded caps",
            "forged admitted cull mismatch mutated cache or published output",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.cpp_test)

    def test_hostile_cpp_acceptance_matrix_and_provenance_are_registered(self) -> None:
        for token in (
            "TestExactRoadClosureAndSharedOwners",
            "TestActivationGateAndNativeAuditEquality",
            "TestDetachedClosureHostileMutations",
            "TestStaticHostileTransactionsAndLineage",
            "TestStaticExceptionRollback",
            "forged texture key/payload/ID association",
            "shared dependency ID accepted conflicting immutable payloads",
            "shared material ID accepted conflicting producer-owned bindings",
            "mesh without its authored UVs",
            "kMaximumOgre14GraphicsSceneStaticSections + 1U",
            "static lifetime cap+1 committed a partial identity transaction",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.cpp_test)

        test_cmake = (REPOSITORY_ROOT / "tests/CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            "Ogre14RoadMaterialTransactionTests.cpp",
            test_cmake,
        )
        self.assertIn(
            "ror_ogre14_road_material_transaction_tests", test_cmake
        )
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
                    "tests/gfx/render/Ogre14RoadMaterialTransactionTests.cpp",
                    text,
                )
                self.assertIn(
                    "tests/tools/test_ogre14_road_material_transaction_contract.py",
                    text,
                )
        for document in (README, PRODUCER_DOC):
            text = document.read_text(encoding="utf-8")
            with self.subTest(document=document.name):
                self.assertIn("exact road2 material closure", text)
                self.assertIn("bit-exact native pipeline audit", text)


if __name__ == "__main__":
    unittest.main()
