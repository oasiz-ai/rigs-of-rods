#!/usr/bin/env python3
"""Static closure for authenticated OGRE 14 source-texture receipts."""

from __future__ import annotations

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "source/main/gfx/ogre14/Ogre14AuthenticatedTextureReceipt.h"
SOURCE = ROOT / "source/main/gfx/ogre14/Ogre14AuthenticatedTextureReceipt.cpp"
CONTENT_HEADER = ROOT / "source/main/resources/ContentManager.h"
CONTENT_SOURCE = ROOT / "source/main/resources/ContentManager.cpp"
CACHE_SOURCE = ROOT / "source/main/resources/CacheSystem.cpp"
COMPATIBILITY_HEADER = (
    ROOT / "source/main/resources/LegacyMaterialCompatibilityPlan.h"
)
COMPATIBILITY_SOURCE = (
    ROOT / "source/main/resources/LegacyMaterialCompatibilityPlan.cpp"
)
SANITIZER_SOURCE = (
    ROOT / "source/main/resources/LegacyMaterialScriptSanitizer.cpp"
)
CITYWORLD_COMPATIBILITY_SOURCE = (
    ROOT
    / "source/main/resources/tobj_fileformat/CityWorldNeoQ20Compatibility.cpp"
)
ARCHIVE_HEADER = (
    ROOT
    / "source/main/resources/terrn2_fileformat/TerrainBundleArchiveVerifier.h"
)
ARCHIVE_SOURCE = (
    ROOT
    / "source/main/resources/terrn2_fileformat/TerrainBundleArchiveVerifier.cpp"
)
CPP_TEST = ROOT / "tests/gfx/ogre14/Ogre14AuthenticatedTextureReceiptTests.cpp"
ARCHIVE_CPP_TEST = ROOT / "tests/resources/TerrainBundleArchiveVerifierTests.cpp"
RECIPE_DATA = ROOT / "cmake/conan/recipes/ogre3d/conandata.yml"
RECIPE_PATCH = (
    ROOT
    / "cmake/conan/recipes/ogre3d/patches/14.5.2/"
    "archive-manager-load-rollback.patch"
)
RECIPE_PROBE = (
    ROOT
    / "cmake/conan/recipes/ogre3d/test_package/src/ogre_recipe_probe.cpp"
)
DOC = ROOT / "doc/nextgen/OGRE14_AUTHENTICATED_TEXTURE_RECEIPTS.md"
PATHS = (
    "cmake/conan/locks/ogre3d-14.5.2-linux-x86_64-release.lock",
    "cmake/conan/locks/ogre3d-14.5.2-macos-arm64-release.lock",
    "cmake/conan/locks/ogre3d-14.5.2-windows-x86_64-release.lock",
    "cmake/conan/locks/ror-ogre14-linux-x86_64-release.lock",
    "cmake/conan/locks/ror-ogre14-macos-arm64-release.lock",
    "cmake/conan/locks/ror-ogre14-windows-x86_64-release.lock",
    "cmake/conan/recipes/mygui/conanfile.py",
    "cmake/conan/recipes/ogre3d/conandata.yml",
    "cmake/conan/recipes/ogre3d/patches/14.5.2/archive-manager-load-rollback.patch",
    "cmake/conan/recipes/ogre3d/test_package/src/ogre_recipe_probe.cpp",
    "conanfile.py",
    "doc/nextgen/OGRE14_AUTHENTICATED_TEXTURE_RECEIPTS.md",
    "source/main/resources/CacheSystem.cpp",
    "source/main/resources/ContentManager.cpp",
    "source/main/resources/ContentManager.h",
    "source/main/resources/LegacyMaterialCompatibilityPlan.cpp",
    "source/main/resources/LegacyMaterialCompatibilityPlan.h",
    "source/main/resources/LegacyMaterialScriptSanitizer.cpp",
    "source/main/resources/tobj_fileformat/CityWorldNeoQ20Compatibility.cpp",
    "source/main/resources/terrn2_fileformat/TerrainBundleArchiveVerifier.cpp",
    "source/main/resources/terrn2_fileformat/TerrainBundleArchiveVerifier.h",
    "source/main/gfx/ogre14/Ogre14AuthenticatedTextureReceipt.cpp",
    "source/main/gfx/ogre14/Ogre14AuthenticatedTextureReceipt.h",
    "tests/gfx/ogre14/Ogre14AuthenticatedTextureReceiptTests.cpp",
    "tests/resources/TerrainBundleArchiveVerifierTests.cpp",
    "tests/tools/assert_ogre_recipe_graph.py",
    "tests/tools/test_ogre14_authenticated_texture_receipt_contract.py",
    "tests/tools/test_ogre14_authenticated_texture_capture_bridge_contract.py",
)


class Ogre14AuthenticatedTextureReceiptContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = HEADER.read_text(encoding="utf-8")
        cls.source = SOURCE.read_text(encoding="utf-8")
        cls.content_header = CONTENT_HEADER.read_text(encoding="utf-8")
        cls.content_source = CONTENT_SOURCE.read_text(encoding="utf-8")
        cls.cache_source = CACHE_SOURCE.read_text(encoding="utf-8")
        cls.compatibility_header = COMPATIBILITY_HEADER.read_text(
            encoding="utf-8"
        )
        cls.compatibility_source = COMPATIBILITY_SOURCE.read_text(
            encoding="utf-8"
        )
        cls.sanitizer_source = SANITIZER_SOURCE.read_text(encoding="utf-8")
        cls.cityworld_compatibility_source = (
            CITYWORLD_COMPATIBILITY_SOURCE.read_text(encoding="utf-8")
        )
        cls.archive_header = ARCHIVE_HEADER.read_text(encoding="utf-8")
        cls.archive_source = ARCHIVE_SOURCE.read_text(encoding="utf-8")
        cls.cpp_test = CPP_TEST.read_text(encoding="utf-8")
        cls.archive_cpp_test = ARCHIVE_CPP_TEST.read_text(encoding="utf-8")
        cls.recipe_data = RECIPE_DATA.read_text(encoding="utf-8")
        cls.recipe_patch = RECIPE_PATCH.read_text(encoding="utf-8")
        cls.recipe_probe = RECIPE_PROBE.read_text(encoding="utf-8")
        cls.doc = DOC.read_text(encoding="utf-8")

    def test_receipt_binds_exact_authenticated_source_and_resource(self) -> None:
        for token in (
            "kOgre14AuthenticatedTextureReceiptVersion = 1U",
            "effective_resource_group",
            "group_generation",
            "archive_identity",
            "archive_name",
            "archive_type",
            "archive_sha256",
            "archive_pointer_token",
            "exact_member_name",
            "resource_pointer_token",
            "resource_handle",
            "resource_state_count",
            "pre_resource_token",
            "byte_count",
            "bytes_sha256",
            "Ogre14SourceDdsHeaderFacts",
            "generated_fallback_rule_version",
        ):
            self.assertIn(token, self.header)
        self.assertIn("Sha256(candidate->bytes)", self.source)
        self.assertIn("ParseDdsFacts(candidate->bytes", self.source)
        self.assertNotIn("tolower", self.source.lower())
        self.assertNotIn("filename", self.source.lower())

    def test_contract_is_bounded_immutable_and_transactional(self) -> None:
        for token in (
            "65536U",
            "512ULL * 1024ULL * 1024ULL",
            "1024ULL * 1024ULL * 1024ULL",
            "16ULL * 1024ULL * 1024ULL",
            "SharesImmutableStateWith",
            "AFTER_SOURCE_BYTES_COPIED",
            "BEFORE_RECEIPT_COMMIT",
            "BEFORE_REGISTRY_COMMIT",
            "BEFORE_GROUP_TRANSITION_COMMIT",
            "catch (const std::bad_alloc &)",
            "std::make_shared",
        ):
            self.assertIn(token, self.header + self.source)
        for token in (
            "exact same-state downstream-failure retry",
            "same-generation reload silently changed authenticated bytes",
            "live pointer reuse",
            "stale receipt resurrected",
            "group reset",
            "group teardown",
            "pre-resource token",
            "bad_alloc changed",
            "unexpected",
        ):
            self.assertIn(token, self.cpp_test)

    def test_content_manager_returns_only_identical_replacement_bytes(self) -> None:
        for token in (
            '#include "gfx/ogre14/Ogre14AuthenticatedTextureReceipt.h"',
            "m_authenticated_texture_receipts",
            "AdvanceOgre14AuthenticatedTextureGroupGeneration",
            "TeardownOgre14AuthenticatedTextureGroup",
            "RemoveOgre14AuthenticatedTextureResource",
            "BuildOgre14AuthenticatedTextureReceipt",
            "CommitOgre14AuthenticatedTextureReceipt",
            "SelectOgre14AuthenticatedTextureArchiveMember",
            "member_selection.code !=",
            "Render::ValidationCode::MISSING_REFERENCE",
            "location.archive->isCaseSensitive()",
            "indexed_file.path + indexed_file.basename",
            "authenticated_stream = selected_archive->open(selected_member_name)",
            "receipt.source_bytes(), receipt.source_size()",
            "return replacement",
            "IsLowercaseOgre14Sha256(",
            "kOgre14GeneratedTextureFallbackRule",
            "kOgre14GeneratedTextureFallbackRuleVersion",
        ):
            self.assertIn(token, self.content_header + self.content_source)
        texture_path = self.content_source.split(
            "if (is_texture_resource)", 2
        )[-1].split("static const std::size_t MAX_AUTHENTICATED_MESH_BYTES", 1)[0]
        self.assertNotIn("findGroupContainingResource", texture_path)
        self.assertNotIn("open(name)", texture_path)
        self.assertIn("Ogre::MemoryDataStream", texture_path)
        self.assertIn("receipt.source_bytes()", texture_path)
        self.assertIn(
            "case-folded full-path collision was accepted",
            self.cpp_test,
        )
        self.assertIn(
            "unique non-strict Zip basename fallback",
            self.cpp_test,
        )
        self.assertIn("Existing authenticated mesh loading keeps", self.content_source)

    def test_exact_verified_archive_bytes_are_the_only_mounted_source(self) -> None:
        archive_contract = self.archive_header + self.archive_source
        for token in (
            "TERRAIN_BUNDLE_AUTHENTICATED_ARCHIVE_SNAPSHOT_VERSION = 1U",
            "TERRAIN_BUNDLE_AUTHENTICATED_ARCHIVE_MAXIMUM_BYTES",
            "TerrainBundleAuthenticatedArchiveSnapshot",
            "SharesImmutableStateWith",
            "LoadAndVerifyTerrainBundleArchiveSnapshot",
            "EVP_DigestUpdate",
            "out_snapshot = TerrainBundleAuthenticatedArchiveSnapshot",
            "catch (const std::bad_alloc&)",
            "unexpected failure before authenticated snapshot commit",
        ):
            self.assertIn(token, archive_contract)
        for token in (
            "snapshot.SharesImmutableStateWith(owner)",
            "snapshot.bytes()[2U]",
            "archive SHA-256 mismatch",
            "archive exceeds authenticated snapshot byte cap",
        ):
            self.assertIn(token, self.archive_cpp_test)

        dependency_mount = self.cache_source.split(
            "bool CacheSystem::LoadTerrainResourceBundleDependencies", 1
        )[1].split("static bool CheckAndReplacePathIgnoreCase", 1)[0]
        self.assertIn(
            "LoadAndVerifyTerrainBundleArchiveSnapshot", dependency_mount
        )
        self.assertIn(
            "MountAuthenticatedPackageResourceLocation", dependency_mount
        )
        self.assertNotIn("addResourceLocation(", dependency_mount)
        self.assertNotIn(
            "VerifyTerrainBundleArchiveSha256(", dependency_mount
        )

        for token in (
            "Ogre::EmbeddedZipArchiveFactory::addEmbbeddedFile",
            "AFTER_EMBEDDED_ZIP_REGISTRATION",
            "AFTER_RESOURCE_LOCATION_INSERTION",
            "BEFORE_POINTER_BOUND_STATE_SWAP",
            "MaybeInjectOgre14AuthenticatedArchiveMountFault",
            'selected_archive->getType() != "EmbeddedZip"',
            "reinterpret_cast<std::uintptr_t>(selected_archive)",
            "authenticated_bindings_candidate",
            "pointer_node.key() = selected_archive",
            "m_package_archives_by_group.swap",
            "m_authenticated_package_archives_by_group.swap",
            "m_authenticated_package_archive_bindings_by_group.swap",
            "m_legacy_material_group_generations.swap",
            "m_authenticated_texture_receipts = std::move(texture_candidate)",
            "pending_owner_node",
            "static_assert(",
            "noexcept(m_package_archives_by_group.swap(",
            "std::is_nothrow_destructible_v<",
            "m_authenticated_package_archive_pending_snapshots",
            "MAX_AUTHENTICATED_EMBEDDED_ZIP_REGISTRATION_ATTEMPTS = 65536U",
            "rollback_authority.AuthenticatesExclusive(",
            "rollback_authority.AuthenticatesDetached(",
            "Ogre::EmbeddedZipArchiveFactory::removeEmbbeddedFile",
            "std::terminate();",
        ):
            self.assertIn(token, self.content_source)
        for token in (
            "mount fault changed prior generation/maps/retained-byte",
            "successful teardown retained manager/factory state",
            "AFTER_EMBEDDED_ZIP_REGISTRATION",
            "AFTER_RESOURCE_LOCATION_INSERTION",
            "BEFORE_POINTER_BOUND_STATE_SWAP",
            "process-terminal boundary",
            "process-terminal mount state admitted a recoverable retry",
        ):
            self.assertIn(token, self.header + self.cpp_test)
        self.assertEqual(
            self.content_source.count(
                "authenticated_stream = selected_archive->open("
            ),
            1,
        )
        self.assertNotIn(
            "validation_archive_name", self.content_source
        )

    def test_exact_primary_demo_archive_uses_the_same_authenticated_mount(self) -> None:
        digest = (
            "ebeac2f0204f25ca1955f29ca1583b2a"
            "fa4517a3a848feb1db203814acac2ef3"
        )
        authority = (
            "kCityWorldLegacyMaterialCompatibilityArchiveSha256"
        )
        self.assertEqual(self.compatibility_header.count(digest), 1)
        self.assertNotIn(digest, self.compatibility_source)
        self.assertNotIn(digest, self.sanitizer_source)
        self.assertNotIn(digest, self.cityworld_compatibility_source)
        self.assertIn(authority, self.compatibility_header)
        self.assertIn(authority, self.compatibility_source)
        self.assertIn(authority, self.sanitizer_source)
        self.assertIn(authority, self.cityworld_compatibility_source)

        primary_load = self.cache_source.split(
            "void CacheSystem::LoadResource", 1
        )[1].split("void CacheSystem::ReLoadResource", 1)[0]
        for token in (
            "IsOgreNextDemoCaptureEnabled()",
            "ShouldProbeLegacyMaterialPrimaryArchive(",
            "LoadAndVerifyTerrainBundleArchiveSnapshot(",
            authority,
            "kCityWorldLegacyMaterialCompatibilityArchiveBytes",
            "DispatchLegacyMaterialPrimaryArchiveMount(",
            "MountAuthenticatedPackageResourceLocation(",
            "authenticated_primary_mount_published = true",
            "if (!primary_package_location_dispatched)",
            "RegisterPackageResourceLocation(",
            "destroyResourceGroup(group)",
            "UnregisterPackageResourceGroup(group)",
            "std::terminate();",
        ):
            self.assertIn(token, primary_load)
        self.assertNotIn("CityWorld.zip", primary_load)
        self.assertNotIn("CityWorld.terrn2", primary_load)
        self.assertLess(
            primary_load.index("LoadAndVerifyTerrainBundleArchiveSnapshot("),
            primary_load.index("createResourceGroup("),
        )
        cleanup = primary_load.split(
            "const auto abandon_resource_group", 1
        )[1].split("// Load now.", 1)[0]
        self.assertLess(
            cleanup.index("destroyResourceGroup(group)"),
            cleanup.index("UnregisterPackageResourceGroup(group)"),
        )
        self.assertIn("loaded_entry->resource_group.clear()", cleanup)
        unload = self.cache_source.split(
            "bool CacheSystem::UnLoadResource", 1
        )[1].split("CacheEntryPtr CacheSystem::FetchSkinByName", 1)[0]
        self.assertIn("Failed to unregister resource group", unload)
        self.assertLess(
            unload.index("destroyResourceGroup(resource_group)"),
            unload.index("UnregisterPackageResourceGroup("),
        )
        self.assertLess(
            unload.index("UnregisterPackageResourceGroup("),
            unload.index("i_entry->resource_group = \"\""),
        )

    def test_pinned_ogre_rollback_patch_is_cross_platform_closed(self) -> None:
        self.assertIn(
            "archive-manager-load-rollback.patch", self.recipe_data
        )
        for token in (
            "ArchiveFactory* creatingFactory",
            "const auto inserted = mArchives.emplace(filename, pArch)",
            "if (!inserted.second)",
            "creatingFactory->destroyInstance(pArch)",
            "catch (...)",
        ):
            self.assertIn(token, self.recipe_patch)
        for token in (
            "VerifyArchiveManagerLoadRollback",
            "factory.destroy_count != initial_destroys + 2U",
            "failed archive remained published after throw",
        ):
            self.assertIn(token, self.recipe_probe)

        root_recipe = (ROOT / "conanfile.py").read_text(encoding="utf-8")
        mygui_recipe = (
            ROOT / "cmake/conan/recipes/mygui/conanfile.py"
        ).read_text(encoding="utf-8")
        revision_line = next(
            line
            for line in root_recipe.splitlines()
            if line.startswith("OGRE14_RECIPE_REVISION = ")
        )
        revision = revision_line.split('"')[1]
        self.assertEqual(len(revision), 32)
        self.assertIn(
            f'OGRE14_RECIPE_REVISION = "{revision}"', mygui_recipe
        )
        for target in (
            '("Linux", "x86_64")',
            '("Macos", "armv8")',
            '("Windows", "x86_64")',
        ):
            self.assertIn(target, root_recipe)
            self.assertIn(target, mygui_recipe)
        for lock_name in (
            "ogre3d-14.5.2-linux-x86_64-release.lock",
            "ogre3d-14.5.2-macos-arm64-release.lock",
            "ogre3d-14.5.2-windows-x86_64-release.lock",
            "ror-ogre14-linux-x86_64-release.lock",
            "ror-ogre14-macos-arm64-release.lock",
            "ror-ogre14-windows-x86_64-release.lock",
        ):
            lock_text = (
                ROOT / "cmake/conan/locks" / lock_name
            ).read_text(encoding="utf-8")
            self.assertIn(f"ogre3d/14.5.2#{revision}%", lock_text)

    def test_dds_and_generated_rules_are_documented_without_quality_claims(self) -> None:
        for token in (
            "exact source",
            "DDS",
            "DX10",
            "ror-legacy-material-procedural-dds-v1",
            "never reopen",
            "behavior remains separate and unchanged",
            "not live Ogre-Next material consumption",
            "process terminates",
            "not a recoverable group state",
        ):
            self.assertIn(token, self.doc)

    def test_build_ci_and_provenance_execute_the_gate(self) -> None:
        native_cmake = (ROOT / "tests/CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        probe_cmake = (ROOT / "tools/ogre_next_probe/CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        workflow = (ROOT / ".github/workflows/ogre-next-probe.yml").read_text(
            encoding="utf-8"
        )
        for cmake in (native_cmake, probe_cmake):
            self.assertIn("ror_ogre14_authenticated_texture_receipt_tests", cmake)
            self.assertIn("Ogre14AuthenticatedTextureReceiptTests.cpp", cmake)
            self.assertIn("Ogre14AuthenticatedTextureReceipt.cpp", cmake)
        self.assertIn("ror_ogre14_authenticated_texture_receipt_tests", workflow)
        self.assertIn("-R '^ror_ogre14_authenticated_texture_receipt$'", workflow)
        self.assertEqual(
            workflow.count(
                "source/main/gfx/ogre14/Ogre14AuthenticatedTextureReceipt.*"
            ),
            2,
        )
        self.assertEqual(
            workflow.count(
                "tests/gfx/ogre14/Ogre14AuthenticatedTextureReceiptTests.cpp"
            ),
            2,
        )
        self.assertEqual(
            workflow.count("source/main/resources/ContentManager.*"), 2
        )
        for trigger in (
            "conanfile.py",
            "cmake/conan/**",
            "source/main/resources/CacheSystem.cpp",
            "source/main/resources/terrn2_fileformat/TerrainBundleArchiveVerifier.*",
            "tests/resources/TerrainBundleArchiveVerifierTests.cpp",
            "tests/tools/assert_ogre_recipe_graph.py",
        ):
            with self.subTest(trigger=trigger):
                self.assertEqual(workflow.count(trigger), 2)

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
