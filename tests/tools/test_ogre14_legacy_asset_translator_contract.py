#!/usr/bin/env python3
"""Static closure tests for the OGRE 14 legacy asset translator v1."""

from __future__ import annotations

from pathlib import Path
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
MAIN_CMAKE = REPOSITORY_ROOT / "source" / "main" / "CMakeLists.txt"
TEST_CMAKE = REPOSITORY_ROOT / "tests" / "CMakeLists.txt"
PHYSICS_WORKFLOW = (
    REPOSITORY_ROOT / ".github" / "workflows" / "physics-core.yml"
)
NATIVE_WORKFLOW = (
    REPOSITORY_ROOT / ".github" / "workflows" / "ogre14-native.yml"
)
TRANSLATOR_HEADER = (
    REPOSITORY_ROOT
    / "source/main/gfx/render/Ogre14LegacyAssetTranslator.h"
)
TRANSLATOR_SOURCE = TRANSLATOR_HEADER.with_suffix(".cpp")
TRANSLATOR_TEST = (
    REPOSITORY_ROOT
    / "tests/gfx/render/Ogre14LegacyAssetTranslatorTests.cpp"
)
NATIVE_HEADER = (
    REPOSITORY_ROOT
    / "source/main/gfx/ogre14/Ogre14LegacyNativeAssetExtractor.h"
)
NATIVE_SOURCE = NATIVE_HEADER.with_suffix(".cpp")
README = REPOSITORY_ROOT / "source/main/gfx/render/README.md"
PROVENANCE_TOOLS = (
    REPOSITORY_ROOT / "tools/run_ogre_next_probe.py",
    REPOSITORY_ROOT / "tools/verify_ogre_next_artifact_set.py",
)


class Ogre14LegacyAssetTranslatorContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.main_cmake = MAIN_CMAKE.read_text(encoding="utf-8")
        cls.test_cmake = TEST_CMAKE.read_text(encoding="utf-8")
        cls.physics_workflow = PHYSICS_WORKFLOW.read_text(encoding="utf-8")
        cls.native_workflow = NATIVE_WORKFLOW.read_text(encoding="utf-8")
        cls.header = TRANSLATOR_HEADER.read_text(encoding="utf-8")
        cls.source = TRANSLATOR_SOURCE.read_text(encoding="utf-8")
        cls.translator_test = TRANSLATOR_TEST.read_text(encoding="utf-8")
        cls.native_header = NATIVE_HEADER.read_text(encoding="utf-8")
        cls.native_source = NATIVE_SOURCE.read_text(encoding="utf-8")
        cls.readme = README.read_text(encoding="utf-8")

    def test_pure_contract_is_versioned_and_native_type_free(self) -> None:
        for token in (
            "kOgre14LegacyAssetTranslatorVersion = 1U",
            "kOgre14LegacyTextureInputVersion = 1U",
            "kOgre14LegacyMaterialInputVersion = 1U",
            "kOgre14LegacyPipelineAuditVersion = 1U",
            "kOgre14LegacyTranslatedFrameVersion = 1U",
            "kOgre14LegacyAssetTranslatorConfigurationVersion = 1U",
            "kOgre14LegacyAssetTranslatorTransactionConfigurationVersion = 1U",
            "maximum_lifetime_asset_records",
            "maximum_decoded_bytes_per_frame",
            "maximum_clone_metadata_bytes",
            "maximum_epoch",
            "std::shared_ptr<const RenderAssetPayload>",
            "std::shared_ptr<const Ogre14LegacyMaterialPipelineAudit>",
            "BuildFullSnapshot",
            "BeforeCommit",
            "CloneForTransaction",
            "CommitTransaction",
            "Ogre14LegacyAssetTranslatorCommitResult",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.header)
        self.assertNotIn("#include <Ogre", self.header)
        self.assertNotIn("Ogre::", self.header)
        self.assertNotIn("GfxScene", self.source)
        self.assertNotIn("GraphicsSceneSnapshotProducer", self.source)

    def test_transaction_fork_is_lineage_bound_and_noexcept_at_commit(self) -> None:
        for token in (
            "std::shared_ptr<const TransactionLineage>",
            "transaction_base_epoch_",
            "TransactionRole::CANDIDATE",
            "transaction_lineage_.get() != candidate.transaction_lineage_.get()",
            "candidate.transaction_base_epoch_ != state_->transaction_epoch",
            "INCOMPATIBLE_CONFIGURATION",
            "fault_injector_ != candidate.fault_injector_",
            "candidate.state_->transaction_epoch = state_->transaction_epoch + 1U",
            "if (transaction_role_ == TransactionRole::COMMITTED_SOURCE)",
            "state_.swap(candidate.state_)",
            "Ogre14LegacyAssetTranslator &&) = delete",
            "Ogre14LegacyAssetTranslator &candidate) noexcept",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.header + self.source)

        commit_start = self.source.index(
            "Ogre14LegacyAssetTranslator::CommitTransaction("
        )
        commit_end = self.source.index(
            "Ogre14LegacyAssetTranslator::Translate(", commit_start
        )
        commit_body = self.source[commit_start:commit_end]
        for forbidden in (
            "new ",
            "make_unique",
            "make_shared",
            "ValidationResult::Failure",
            "throw ",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, commit_body)

        for token in (
            "TestTransactionClonePreservesSourceAndImmutableOwners",
            "TestSourceAdvanceStalesCandidateWithoutChangingIt",
            "TestTransactionEpochExhaustionRejectsCommitExactly",
            "TestCandidateWorkConsumesOneEpochOnlyAtPublication",
            "TestMoveConstructionPreservesTransactionRolesAndLineage",
            "BEFORE_CANDIDATE_PUBLISH",
            "sentinel.get() == sentinel_identity",
            "SameFrameOwners",
            "FOREIGN_LINEAGE",
            "STALE_SOURCE",
        ):
            with self.subTest(test_token=token):
                self.assertIn(token, self.translator_test)

    def test_fail_closed_feature_set_and_exact_bytes_are_present(self) -> None:
        for token in (
            "A8R8G8B8_WORD_LITTLE_ENDIAN",
            "A8R8G8B8_WORD_BIG_ENDIAN",
            "texture.color_transform",
            "texture.mip_layout",
            "material.programs",
            "material.pass_structure",
            "material.texture_unit",
            "material.fixed_function_lobes",
            "material.pipeline.blend",
            "material.pipeline.depth_raster",
            "material.texture_color_role",
            "EquivalentRenderAssetPayload",
            "a permanently tombstoned legacy identity may never return",
            "frame.lifetime_asset_records",
            "frame.decoded_texture_bytes",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.source)

    def test_native_edge_is_pinned_and_uses_canonical_ogre_readback(self) -> None:
        for token in (
            "OGRE_VERSION_MAJOR == 14",
            "OGRE_VERSION_MINOR == 5",
            "OGRE_VERSION_PATCH == 2",
            "Ogre::PF_BYTE_RGBA",
            "blitToMemory",
            "PixelUtil::isCompressed",
            "PixelUtil::getBitDepths",
            "getTextureAddressingMode",
            "getTextureFiltering",
            "getTextureMipmapBias",
            "getTextureAnisotropy",
            "getTextureCompareEnabled",
            "getTextureBorderColour",
            "getSourceBlendFactorAlpha",
            "getDestBlendFactorAlpha",
            "getDepthCheckEnabled",
            "getDepthWriteEnabled",
            "getDepthFunction",
            "getAlphaRejectFunction",
            "getCullingMode",
            "getTransparentSortingEnabled",
            "getIteratePerLight",
            "getVertexColourTracking",
            "getGPUVendorRules",
            "getUnorderedAccessMipLevel",
            "maximum_decoded_bytes_per_asset",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.native_source)
        self.assertIn("class Material;", self.native_header)

    def test_cmake_closes_pure_native_and_strict_fp_builds(self) -> None:
        self.assertGreaterEqual(
            self.main_cmake.count("Ogre14LegacyAssetTranslator.cpp"), 2
        )
        self.assertIn(
            "gfx/render/Ogre14LegacyAssetTranslator.{h,cpp}",
            self.main_cmake,
        )
        self.assertIn(
            "gfx/ogre14/Ogre14LegacyNativeAssetExtractor.{h,cpp}",
            self.main_cmake,
        )
        self.assertIn("if (ROR_OGRE14)", self.main_cmake)
        for token in (
            "ror_ogre14_legacy_asset_translator_tests",
            "Ogre14LegacyAssetTranslatorTests.cpp",
            "ror_ogre14_legacy_native_asset_extractor_compile_tests",
            "Ogre14LegacyNativeAssetExtractorCompileTests.cpp",
            "source/main/gfx/ogre14/Ogre14LegacyNativeAssetExtractor.cpp",
            "SYSTEM PRIVATE ${OGRE_INCLUDE_DIRS}",
            "PRIVATE OgreMain",
            "NAME ogre14_legacy_asset_translator",
            "NAME ogre14_legacy_native_asset_extractor_compile",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.test_cmake)

    def test_ci_runs_sanitized_pure_and_real_native_closures(self) -> None:
        self.assertIn(
            "-fsanitize=address,undefined -fno-omit-frame-pointer",
            self.physics_workflow,
        )
        self.assertIn(
            "cmake --build build-physics-tests-sanitized --config Debug",
            self.physics_workflow,
        )
        self.assertIn("-DROR_BUILD_TESTS=ON", self.native_workflow)
        self.assertIn(
            "Configure OgreNext-first native Release",
            self.native_workflow,
        )
        self.assertNotIn("-DROR_OGRE14=", self.native_workflow)
        self.assertIn("--target all", self.native_workflow)

    def test_docs_and_provenance_cover_the_new_contract(self) -> None:
        for token in (
            "Exact OGRE 14 legacy asset translator v1",
            "not wired into",
            "dependency-ordered",
            "permanent tombstones",
            "attached exactly once",
            "OGRE 14.5.2",
            "aggregate canonical decoded bytes",
            "checked arithmetic",
            "CloneForTransaction",
            "private immutable lineage identity",
            "allocation-free `noexcept` publication",
            "owner-equivalent",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.readme)
        for tool in PROVENANCE_TOOLS:
            text = tool.read_text(encoding="utf-8")
            with self.subTest(tool=tool.name):
                self.assertIn(
                    '"tests/gfx/render/Ogre14LegacyAssetTranslatorTests.cpp"',
                    text,
                )
                self.assertIn(
                    '"tests/gfx/ogre14/'
                    'Ogre14LegacyNativeAssetExtractorCompileTests.cpp"',
                    text,
                )
                self.assertIn(
                    '"tests/tools/'
                    'test_ogre14_legacy_asset_translator_contract.py"',
                    text,
                )


if __name__ == "__main__":
    unittest.main()
