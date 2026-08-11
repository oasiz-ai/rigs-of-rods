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
NATIVE_TEST = (
    REPOSITORY_ROOT
    / "tests/gfx/ogre14/Ogre14LegacyNativeAssetExtractorCompileTests.cpp"
)
README = REPOSITORY_ROOT / "source/main/gfx/render/README.md"
SEMANTIC_CATALOG_DOC = (
    REPOSITORY_ROOT / "doc/nextgen/OGRE14_MATERIAL_SEMANTIC_CATALOG_V2.md"
)
PROVENANCE_TOOLS = (
    REPOSITORY_ROOT / "tools/run_ogre_next_probe.py",
    REPOSITORY_ROOT / "tools/verify_ogre_next_artifact_set.py",
    REPOSITORY_ROOT
    / "tools/ogre_next_probe/cmake/VerifyN2SourceProvenance.cmake",
    REPOSITORY_ROOT / "tools/ogre_next_probe/CMakeLists.txt",
)
OGRE_NEXT_WORKFLOW = (
    REPOSITORY_ROOT / ".github/workflows/ogre-next-probe.yml"
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
        cls.native_test = NATIVE_TEST.read_text(encoding="utf-8")
        cls.readme = README.read_text(encoding="utf-8")
        cls.semantic_catalog_doc = SEMANTIC_CATALOG_DOC.read_text(
            encoding="utf-8"
        )

    def test_pure_contract_is_versioned_and_native_type_free(self) -> None:
        for token in (
            "kOgre14LegacyAssetTranslatorVersion = 2U",
            "kOgre14LegacyTextureInputVersion = 1U",
            "kOgre14LegacyMaterialInputVersion = 2U",
            "kOgre14LegacyAssetIdentityFrameViewVersion = 1U",
            "kOgre14LegacyPipelineAuditVersion = 2U",
            "kOgre14LegacyTranslatedFrameVersion = 2U",
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
            "Ogre14LegacyCatalogIdentityReceipt",
            "SameOgre14LegacyCatalogIdentity",
            "Ogre14LegacyAssetTranslatorCommittableTransaction",
            "BeginCommittableTransaction",
            "CommitAfterAcceptedExposure",
            "Ogre14LegacyAssetIdentityFrameView",
            "PreflightLifetimeAdmission",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.header)
        self.assertNotIn("#include <Ogre", self.header)
        self.assertNotIn("Ogre::", self.header)
        self.assertNotIn("GfxScene", self.source)
        self.assertNotIn("GraphicsSceneSnapshotProducer", self.source)

    def test_exclusive_receipt_and_publication_lease_are_fail_closed(self) -> None:
        for token in (
            "std::shared_ptr<const void> owner_",
            "friend class Ogre14LegacyAssetTranslator",
            "catalog_identity",
            "TransactionLineage::kExclusiveLease",
            "candidate_state.compare_exchange_strong",
            "translator.exclusive_transaction",
            "EXCLUSIVE_LEASE_REQUIRED",
            "CommitExclusiveTransaction",
            "ReleaseCandidateRegistration",
            "committed_source.store(this",
            "CommitAfterAcceptedExposure() noexcept",
            "Discard() noexcept",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.header + self.source)
        for token in (
            "TestCatalogIdentityIsOpaqueFreshAndCloneExact",
            "TestExclusiveCommittableTransactionIsAtomicAndInfallible",
            "TestExclusiveLeaseFaultExhaustionAndDestructionRelease",
            "std::is_nothrow_move_assignable_v",
            "ALREADY_CONSUMED",
            "INVALID_SOURCE",
            "translator.exclusive_test_fault",
        ):
            with self.subTest(test_token=token):
                self.assertIn(token, self.translator_test)

        exclusive_start = self.source.index(
            "Ogre14LegacyAssetTranslator::CommitExclusiveTransaction("
        )
        exclusive_end = self.source.index(
            "Ogre14LegacyAssetTranslator::PreflightLifetimeAdmission(",
            exclusive_start,
        )
        exclusive_body = self.source[exclusive_start:exclusive_end]
        for forbidden in (
            "new ",
            "make_unique",
            "make_shared",
            "ValidationResult::Failure",
            "throw ",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, exclusive_body)

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
            "Ogre14LegacyAssetTranslator::CommitExclusiveTransaction(",
            commit_start,
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

    def test_read_only_lifetime_admission_is_borrowed_and_fail_closed(
        self,
    ) -> None:
        for token in (
            "const Ogre14LegacyTextureInput *const *texture_inputs",
            "const Ogre14LegacyMaterialInput *const *material_inputs",
            "std::size_t texture_input_count",
            "std::size_t material_input_count",
            "PreflightLifetimeAdmission(\n"
            "      const Ogre14LegacyAssetIdentityFrameView &input) const",
            "AtLifetimeAdmissionIdentityForTesting",
        ):
            with self.subTest(header_token=token):
                self.assertIn(token, self.header)

        preflight_start = self.source.index(
            "Ogre14LegacyAssetTranslator::PreflightLifetimeAdmission("
        )
        preflight_end = self.source.index(
            "Ogre14LegacyAssetTranslator::Translate(", preflight_start
        )
        preflight = self.source[preflight_start:preflight_end]
        for token in (
            "input.texture_input_count >",
            "input.material_input_count >",
            "identity_frame.input_ranges",
            "identity_frame.texture_inputs",
            "identity_frame.material_inputs",
            "identity_frame.live_assets",
            "prospective_ids_by_key",
            "prospective_keys_by_id",
            "state_->records.find(stable_key)",
            "state_->stable_keys_by_id.find(source_asset_id)",
            "frame.lifetime_asset_records",
            "catch (const std::bad_alloc &)",
            "catch (...)",
        ):
            with self.subTest(preflight_token=token):
                self.assertIn(token, preflight)
        for forbidden in (
            "DecodeOgre14LegacyTexture",
            "mip.bytes",
            "source_mip.bytes",
            "*state_ =",
            "state_.swap",
        ):
            with self.subTest(preflight_forbidden=forbidden):
                self.assertNotIn(forbidden, preflight)

        for token in (
            "TestLifetimeAdmissionPreflightBoundsReuseAndRollback",
            "TestLifetimeAdmissionPreflightRejectsInvalidBorrowedIdentities",
            "TestLifetimeAdmissionCollisionAndExceptionRollback",
            "OVERRIDE_SOURCE_ID",
            "translator.lifetime_preflight.allocation",
            "translator.lifetime_preflight.exception",
            "EquivalentFrameValue(before, after)",
            "SameFrameOwners(before, after)",
            "texture.mip_layout",
        ):
            with self.subTest(runtime_token=token):
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

    def test_native_audit_mint_authority_is_not_renderer_neutral(self) -> None:
        for token in (
            "DeriveOgre14LegacyMaterialPipelineAudit(",
            "Failure leaves `output` untouched",
        ):
            self.assertIn(token, self.header + self.source)
        self.assertNotIn("Ogre14LegacyNativeMaterialAuditReceipt", self.header)
        self.assertNotIn("MintOgre14LegacyMaterialPipelineAuditCapture", self.header)
        self.assertNotIn("MintOgre14LegacyMaterialPipelineAuditCapture", self.source)

        for token in (
            "kOgre14LegacyNativeMaterialAuditReceiptVersion = 2U",
            "class Ogre14LegacyNativeMaterialAuditReceipt final",
            "friend ValidationResult CaptureOgre14LegacyNativeMaterial(",
            "friend class Testing::Ogre14LegacyNativeMaterialAuditTestAccess",
            "ROR_OGRE14_NATIVE_MATERIAL_AUDIT_INTERNAL_TESTING",
            "Authenticates(",
            "exact_native_material_audit",
            "native_material_audit_receipt",
        ):
            self.assertIn(token, self.native_header)
        receipt_start = self.native_header.index(
            "class Ogre14LegacyNativeMaterialAuditReceipt final"
        )
        receipt_end = self.native_header.index(
            "struct Ogre14LegacyNativeMaterialCapture", receipt_start
        )
        receipt_body = self.native_header[receipt_start:receipt_end]
        self.assertIn("private:", receipt_body)
        self.assertIn(
            "explicit Ogre14LegacyNativeMaterialAuditReceipt(", receipt_body
        )
        self.assertIn(
            "std::make_shared<const Ogre14LegacyMaterialPipelineAudit>",
            self.native_source,
        )
        for token in (
            "native extractor did not mint the exact independently derived audit",
            "separate native captures reused one audit control block",
        ):
            self.assertIn(token, self.native_test)

    def test_native_declaration_digest_is_canonical_and_owner_bound(self) -> None:
        for token in (
            "kOgre14LegacyNativeMaterialDeclarationSerializationVersion = 1U",
            "kOgre14LegacyNativeMaterialDeclarationDigestBytes = 32U",
            "kOgre14LegacyNativeMaterialDeclarationMaximumCanonicalBytes",
            "Ogre14LegacyNativeMaterialDeclarationSha256",
            "native_material_declaration_serialization_version",
            "native_material_declaration_sha256",
            "SharesNativeDeclarationAuthorityWith",
            "declaration_serialization_version_",
            "declaration_sha256_",
            "capture_sha256_",
            "ComputeOgre14LegacyNativeMaterialCaptureSha256",
        ):
            with self.subTest(header_token=token):
                self.assertIn(token, self.native_header)
        for token in (
            "kNativeMaterialDeclarationMagic",
            "'R', 'O', 'R', 'N', 'M', 'D', '1'",
            "BuildNativeMaterialDeclarationDigest(",
            "Sha256(writer.bytes().data(), writer.bytes().size())",
            "material.getReceiveShadows()",
            "material.getTransparencyCastsShadows()",
            "technique.getSchemeName()",
            "technique.getLodIndex()",
            "technique.getShadowCasterMaterial()",
            "technique.getShadowCasterMaterialName()",
            "technique.getShadowReceiverMaterialName()",
            "pass.getVertexColourTracking()",
            "pass.getTransparentSortingEnabled()",
            "native_unit->getName()",
            "native_unit->getEffects().size()",
            "native_unit->getColourBlendMode()",
            "native_unit->getAlphaBlendMode()",
            "AppendSampler(writer, sampler)",
            "BEFORE_DIGEST_COMMIT",
            "BEFORE_FRESHNESS_REVALIDATION",
            "verified_declaration_sha256",
            "native.getSampler()",
        ):
            with self.subTest(source_token=token):
                self.assertIn(token, self.native_source)
        for token in (
            "canonical declaration serialization or SHA-256 known answer drifted",
            "declaration digest value semantics or alteration checks drifted",
            "fresh native capture reused declaration control-block authority",
            "exact material group/name did not affect the native digest",
            "exact material resource group did not affect the native digest",
            "altered accepted pass state did not affect the native digest",
            "exact texture-unit name/order did not affect the native digest",
            "exact texture resource group did not affect the native digest",
            "exact texture resource name did not affect the native digest",
            "altered sampler state did not affect the native digest",
            "altered unsupported combine semantics were partially hashed",
            "unsupported environment augmentation was partially hashed",
            "oversized native declaration bypassed the canonical byte cap",
            "digest bad_alloc changed deep native capture ownership",
            "unexpected digest exception changed deep native capture ownership",
            "mid-capture native mutation published a stale or hybrid digest",
            "null native sampler crashed or escaped fail-closed validation",
            "unresolved shadow-caster declaration was confused with absent state",
            "unresolved shadow-receiver declaration was confused with absent state",
        ):
            with self.subTest(native_test_token=token):
                self.assertIn(token, self.native_test)
        for token in (
            "Canonical native structure digest v1",
            "RORNMD1",
            "64 KiB",
            "inactive manual combine union members indeterminate",
            "PSSM versus stencil is scene-level state",
            "version-2 opaque",
            "texture pixels and archive/source authority",
            "Counts precede their records",
            "canonical zero",
        ):
            with self.subTest(doc_token=token):
                self.assertIn(token, self.semantic_catalog_doc)

    def test_native_digest_shipping_tus_override_release_fast_math(self) -> None:
        strict_start = self.main_cmake.index(
            "set(ROR_RENDER_CONTRACT_STRICT_FP_SOURCES"
        )
        strict_end = self.main_cmake.index(
            "# Authenticated input traces", strict_start
        )
        strict_block = self.main_cmake[strict_start:strict_end]
        for token in (
            "if (ROR_OGRE14)",
            "gfx/ogre14/Ogre14LegacyNativeAssetExtractor.cpp",
            "gfx/ogre14/Ogre14LegacyNativeMaterialCaptureAuthority.cpp",
            "SKIP_PRECOMPILE_HEADERS ON",
            '${ROR_RENDER_CONTRACT_STRICT_FP_SOURCES}',
            'COMPILE_OPTIONS "/fp:strict"',
            'COMPILE_OPTIONS "-fno-fast-math;-ffp-contract=off"',
        ):
            with self.subTest(strict_fp_token=token):
                self.assertIn(token, strict_block)

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
            "Exact OGRE 14 legacy asset translator v2",
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
            "opaque catalog-identity receipt",
            "BeginCommittableTransaction",
            "CommitAfterAcceptedExposure",
            "cannot `Translate`, clone, or begin another",
            "Ogre14LegacyAssetIdentityFrameView",
            "PreflightLifetimeAdmission",
            "copies no mip bytes",
            "genuinely new stable keys",
            "early resource-admission proof",
            "DeriveOgre14LegacyMaterialPipelineAudit",
            "Ogre14LegacyNativeMaterialAuditReceipt",
            "authenticates both the exact object pointer and its control block",
            "observation's texture bytes are charged before any deduplication",
            "can never authenticate two stable material keys",
            "Ogre14ProceduralRoadCapture::exact_native_material_audit",
            "never copies or",
            "reboxes `closure.material_audit`",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.readme)
        for tool in PROVENANCE_TOOLS:
            text = tool.read_text(encoding="utf-8")
            with self.subTest(tool=tool.name):
                for path in (
                    "source/main/gfx/ogre14/Ogre14LegacyNativeAssetExtractor.cpp",
                    "source/main/gfx/ogre14/Ogre14LegacyNativeAssetExtractor.h",
                    "source/main/gfx/ogre14/Ogre14LegacyNativeMaterialCaptureAuthority.cpp",
                    "cmake/conan/recipes/ogre3d/patches/14.5.2/"
                    "expose-shadow-material-declaration-names.patch",
                ):
                    self.assertIn(path, text)
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
        workflow = OGRE_NEXT_WORKFLOW.read_text(encoding="utf-8")
        self.assertEqual(
            workflow.count(
                "source/main/gfx/ogre14/"
                "Ogre14LegacyNativeMaterialCaptureAuthority.cpp"
            ),
            2,
        )


if __name__ == "__main__":
    unittest.main()
