#!/usr/bin/env python3
"""Static closure tests for exact translated OGRE 14 material resolution."""

from __future__ import annotations

from pathlib import Path
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
HEADER = (
    REPOSITORY_ROOT
    / "source/main/gfx/render/Ogre14LegacyMaterialClosure.h"
)
SOURCE = HEADER.with_suffix(".cpp")
TEST_SOURCE = (
    REPOSITORY_ROOT
    / "tests/gfx/render/Ogre14LegacyMaterialClosureTests.cpp"
)
TRANSLATOR_HEADER = (
    REPOSITORY_ROOT
    / "source/main/gfx/render/Ogre14LegacyAssetTranslator.h"
)
TRANSLATOR_SOURCE = TRANSLATOR_HEADER.with_suffix(".cpp")
MAIN_CMAKE = REPOSITORY_ROOT / "source/main/CMakeLists.txt"
TEST_CMAKE = REPOSITORY_ROOT / "tests/CMakeLists.txt"
RENDER_CONTRACTS = (
    REPOSITORY_ROOT / "source/main/gfx/render/RenderContracts.h"
)
README = REPOSITORY_ROOT / "source/main/gfx/render/README.md"
PROVENANCE_TOOLS = (
    REPOSITORY_ROOT / "tools/run_ogre_next_probe.py",
    REPOSITORY_ROOT / "tools/verify_ogre_next_artifact_set.py",
)


class Ogre14LegacyMaterialClosureContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = HEADER.read_text(encoding="utf-8")
        cls.source = SOURCE.read_text(encoding="utf-8")
        cls.test_source = TEST_SOURCE.read_text(encoding="utf-8")
        cls.translator_header = TRANSLATOR_HEADER.read_text(encoding="utf-8")
        cls.translator_source = TRANSLATOR_SOURCE.read_text(encoding="utf-8")
        cls.main_cmake = MAIN_CMAKE.read_text(encoding="utf-8")
        cls.test_cmake = TEST_CMAKE.read_text(encoding="utf-8")
        cls.render_contracts = RENDER_CONTRACTS.read_text(encoding="utf-8")
        cls.readme = README.read_text(encoding="utf-8")

    def test_contract_is_versioned_bounded_and_renderer_neutral(self) -> None:
        for token in (
            "kOgre14LegacyMaterialClosureVersion = 2U",
            "kMaximumOgre14LegacyMaterialClosureLiveAssets",
            "kMaximumOgre14LegacyMaterialClosureMutations",
            "kMaximumOgre14LegacyMaterialClosurePayloadBytes",
            "std::uint64_t source_sequence",
            "std::uint64_t catalog_sequence",
            "std::uint64_t material_source_asset_id",
            "bool requires_reverse_winding",
            "std::vector<GraphicsSceneAssetInput> assets",
            "IOgre14LegacyMaterialClosureFaultInjector",
            "Ogre14LegacyMaterialClosureFaultPoint",
            "ResolveOgre14LegacyMaterialClosure",
            "kOgre14LegacyMaterialClosureRequestVersion = 1U",
            "kOgre14LegacyMaterialClosureBatchVersion = 1U",
            "kMaximumOgre14LegacyMaterialClosureRequests",
            "Ogre14LegacyCatalogIdentityReceipt catalog_identity",
            "Ogre14LegacyMaterialClosureRequest",
            "Ogre14LegacyMaterialClosureBatch",
            "ResolveOgre14LegacyMaterialClosureBatch",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.header)
        self.assertNotIn("#include <Ogre", self.header)
        self.assertNotIn("Ogre::", self.header)
        self.assertNotIn("#include <Ogre", self.source)
        self.assertNotIn("Ogre::", self.source)

    def test_batch_index_is_single_pass_lineage_exact_and_atomic(self) -> None:
        for token in (
            "ValidateOgre14LegacyMaterialClosureForFrame",
            "MakeOgre14LegacyMaterialClosureRequest",
            "SameOgre14LegacyCatalogIdentity",
            "material_requests.catalog_identity",
            "material_requests.sequence",
            "std::vector<IndexedMaterialRequest>",
            "std::sort(indexed_requests.begin()",
            "material request set duplicates an exact source identity",
            "BuildIndexedMaterialClosure",
            "candidate.catalog_identity = frame.catalog_identity",
            "output = std::move(candidate)",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.source)
        self.assertEqual(
            self.source.count("ValidateCompleteFrame(frame, assets)"), 1
        )
        for token in (
            "TestBatchResolutionValidatesOnceAndSharesCanonicalOwners",
            "TestBatchRejectsDuplicateForeignStaleMissingAndForgedKeys",
            "TestBatchExceptionAndRequestCapsAreAtomic",
            "counts.index_count == 1U",
            "SameOwner(batch.closures[0U].assets.front().payload",
            "std::is_nothrow_move_assignable_v",
        ):
            with self.subTest(test_token=token):
                self.assertIn(token, self.test_source)

    def test_hostile_snapshot_revalidation_and_atomic_failure_are_explicit(self) -> None:
        for token in (
            "frame.full_snapshot",
            "frame.catalog_sequence > frame.source_sequence",
            "StrictAssetOrder",
            "StrictMutationOrder",
            "ParseStableKey",
            "DeriveOgre14LegacySourceAssetId",
            "RenderAssetPayloadKind",
            "ValidateTextureResourceDescriptor",
            "ValidateSamplerResourceDescriptor",
            "ValidateMaterialDescriptor",
            "ValidateOgre14LegacyMaterialPipelineAudit",
            "ValidateMaterialTextureCompatibility",
            "orphan material-owned sampler",
            "SharedOwnerAndPointer",
            "BEFORE_INDEX_CONSTRUCTION",
            "DURING_DEPENDENCY_ASSEMBLY",
            "catch (const std::bad_alloc &)",
            "catch (...)"
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.source)
        self.assertLess(
            self.source.index("output = std::move(candidate)"),
            self.source.index("return ValidationResult::Success()", self.source.index("output = std::move(candidate)")),
        )

    def test_exact_material_binding_has_no_guessed_shader_fallback(self) -> None:
        for token in (
            "MaterialTextureSlot::BASE_COLOR",
            "binding.texture_source_asset_id = audit.texture_source_asset_id",
            "binding.sampler_source_asset_id = audit.sampler_source_asset_id",
            "FloatBitsEqual(material.metallic_factor, 0.0F)",
            "FloatBitsEqual(material.roughness_factor, 1.0F)",
            "ExactAbsentReference(material.base_color_texture.texture)",
            "audit.base_color_semantic",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.source)
        for forbidden in ("fallback", "default shader", "guess shader"):
            self.assertNotIn(forbidden, self.source.lower())
        self.assertIn(
            "audit.pipeline.cull == Ogre14LegacyCullMode::ANTICLOCKWISE",
            self.translator_source,
        )

    def test_stable_key_and_audit_validators_are_public_and_fail_closed(self) -> None:
        for token in (
            "kMaximumOgre14LegacyStableAssetKeyBytes = 512U",
            "BuildOgre14LegacyStableAssetKey",
            "ValidateOgre14LegacyMaterialPipelineAudit",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.translator_header)
                self.assertIn(token.split(" = ")[0], self.translator_source)
        self.assertIn("stable_key = std::move(candidate)", self.translator_source)
        self.assertIn("audit.requires_reverse_winding != expected_reverse", self.translator_source)

    def test_build_tests_docs_and_provenance_are_closed(self) -> None:
        self.assertIn(
            "gfx/render/Ogre14LegacyMaterialClosure.{h,cpp}",
            self.main_cmake,
        )
        self.assertEqual(
            self.main_cmake.count("Ogre14LegacyMaterialClosure.cpp"), 2
        )
        for token in (
            "ror_ogre14_legacy_material_closure_tests",
            "Ogre14LegacyMaterialClosureTests.cpp",
            "NAME ogre14_legacy_material_closure",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.test_cmake)
        self.assertIn(
            '#include "Ogre14LegacyMaterialClosure.h"',
            self.render_contracts,
        )
        for token in (
            "Ogre14LegacyMaterialClosure",
            "complete full snapshot",
            "sRGB RGBA8 2D texture",
            "orphan samplers",
            "material_bindings",
            "no partially allocated dependency list",
            "opaque catalog-identity receipt",
            "BeginCommittableTransaction",
            "ResolveOgre14LegacyMaterialClosureBatch",
            "validates the full frame once",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.readme)
        for tool in PROVENANCE_TOOLS:
            text = tool.read_text(encoding="utf-8")
            with self.subTest(tool=tool.name):
                self.assertIn(
                    '"tests/gfx/render/Ogre14LegacyMaterialClosureTests.cpp"',
                    text,
                )
                self.assertIn(
                    '"tests/tools/test_ogre14_legacy_material_closure_contract.py"',
                    text,
                )


if __name__ == "__main__":
    unittest.main()
