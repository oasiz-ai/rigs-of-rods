#!/usr/bin/env python3
"""Static closure for registry-authorized OGRE 14 texture capture."""

from __future__ import annotations

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
RECEIPT_HEADER = ROOT / "source/main/gfx/ogre14/Ogre14AuthenticatedTextureReceipt.h"
RECEIPT_SOURCE = ROOT / "source/main/gfx/ogre14/Ogre14AuthenticatedTextureReceipt.cpp"
NATIVE_HEADER = ROOT / "source/main/gfx/ogre14/Ogre14LegacyNativeAssetExtractor.h"
NATIVE_SOURCE = ROOT / "source/main/gfx/ogre14/Ogre14LegacyNativeAssetExtractor.cpp"
CONTENT_HEADER = ROOT / "source/main/resources/ContentManager.h"
CONTENT_SOURCE = ROOT / "source/main/resources/ContentManager.cpp"
RECEIPT_TEST = ROOT / "tests/gfx/ogre14/Ogre14AuthenticatedTextureReceiptTests.cpp"
NATIVE_TEST = ROOT / "tests/gfx/ogre14/Ogre14LegacyNativeAssetExtractorCompileTests.cpp"
DOC = ROOT / "doc/nextgen/OGRE14_AUTHENTICATED_TEXTURE_RECEIPTS.md"
THIS_PATH = "tests/tools/test_ogre14_authenticated_texture_capture_bridge_contract.py"


class Ogre14AuthenticatedTextureCaptureBridgeContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.receipt_header = RECEIPT_HEADER.read_text(encoding="utf-8")
        cls.receipt_source = RECEIPT_SOURCE.read_text(encoding="utf-8")
        cls.native_header = NATIVE_HEADER.read_text(encoding="utf-8")
        cls.native_source = NATIVE_SOURCE.read_text(encoding="utf-8")
        cls.content_header = CONTENT_HEADER.read_text(encoding="utf-8")
        cls.content_source = CONTENT_SOURCE.read_text(encoding="utf-8")
        cls.receipt_test = RECEIPT_TEST.read_text(encoding="utf-8")
        cls.native_test = NATIVE_TEST.read_text(encoding="utf-8")
        cls.doc = DOC.read_text(encoding="utf-8")

    def test_resolution_is_registry_minted_and_resolver_bound(self) -> None:
        contract = self.receipt_header + self.receipt_source
        for token in (
            "kOgre14AuthenticatedTextureResolutionVersion = 1U",
            "class Ogre14AuthenticatedTextureResolution final",
            "class IOgre14AuthenticatedTextureResolver",
            "MintLoadedResourceResolution",
            "RevalidateLoadedResourceResolution",
            "friend class ::RoR::ContentManager",
            "registry_snapshot",
            "exact_source_receipt",
            "resolver_pointer_token",
            "SharesImmutableStateWith",
            "BEFORE_RESOLUTION_COMMIT",
        ):
            self.assertIn(token, contract)
        self.assertLess(
            self.receipt_header.index("MintLoadedResourceResolution"),
            self.receipt_header.index("friend class ::RoR::ContentManager"),
        )
        public_resolution = self.receipt_header.split(
            "class Ogre14AuthenticatedTextureResolution final", 1
        )[1].split("private:", 1)[0]
        self.assertNotIn("Build", public_resolution)
        self.assertNotIn("Mint", public_resolution)

    def test_exact_preload_plus_one_and_snapshot_invalidation(self) -> None:
        for token in (
            "resource_state_count + 1U",
            "loaded texture is not exactly one successful load",
            "resolution.state_->registry_snapshot.state_ != state_",
            "found->second.SharesImmutableStateWith",
        ):
            self.assertIn(token, self.receipt_source)
        for token in (
            "independent Build receipts unexpectedly shared authority",
            "equivalent synthetic registry substituted",
            "old proof survived an unrelated immutable-registry publication",
            "fresh resolve did not recover",
            "reload did not invalidate",
            "group teardown did not invalidate",
            "bad_alloc during resolution mint changed output authority",
            "unexpected resolution-mint exception changed output authority",
        ):
            self.assertIn(token, self.receipt_test)

    def test_content_manager_is_the_exact_live_authority(self) -> None:
        for token in (
            "public Render::IOgre14AuthenticatedTextureResolver",
            "ResolveAuthenticatedTexture(",
            "RevalidateAuthenticatedTexture(",
            'manager->getResourceType() != "Texture"',
            "texture.getCreator() != manager",
            "manager->getByHandle(texture.getHandle())",
            "manager->getResourceByName(",
            "by_handle.get() != &texture",
            "by_name.get() != &texture",
            "texture.isLoaded()",
            "m_legacy_material_state_mutex",
            "MintLoadedResourceResolution",
            "RevalidateLoadedResourceResolution",
        ):
            self.assertIn(token, self.content_header + self.content_source)
        bridge = self.content_source.split(
            "ContentManager::ResolveAuthenticatedTexture", 1
        )[1].split("void ContentManager::EnsureResourceGroupListener", 1)[0]
        self.assertNotIn("openResource", bridge)
        self.assertNotIn("selected_archive->open", bridge)
        self.assertNotIn("findGroupContainingResource", bridge)

    def test_native_capture_reacquires_and_revalidates_before_publish(self) -> None:
        contract = self.native_header + self.native_source
        for token in (
            "kOgre14LegacyNativeAssetExtractorVersion = 2U",
            "authenticated_texture_resolutions",
            "ResolveAuthenticatedTexture(",
            "resolution.MatchesResolver",
            "MatchesLoadedResourceIdentity",
            "current_unit->_getTexturePtr()",
            "RevalidateAuthenticatedTexture(*current_texture",
            "RevalidateAuthenticatedTextureForPublication(",
            "std::is_nothrow_move_assignable<",
            "native capture publication must preserve transactional rollback",
        ):
            self.assertIn(token, contract)
        authenticated_overload = self.native_source.rsplit(
            "ValidationResult CaptureOgre14LegacyNativeMaterial(", 1
        )[1]
        self.assertLess(
            authenticated_overload.index(
                "RevalidateAuthenticatedTextureForPublication("
            ),
            authenticated_overload.index("capture = std::move(candidate)"),
        )
        for token in (
            "authenticated native capture lost exact source authority",
            "resolver substitution published",
            "legacy compatibility capture was confused",
            "untextured authenticated capture invoked",
            "resolver bad_alloc changed native capture output ownership",
            "unexpected resolver exception changed native capture output ownership",
            "stale reload state published",
            "group teardown did not invalidate final native publication",
        ):
            self.assertIn(token, self.native_test)

    def test_serialized_ogre_lifecycle_limit_is_explicit(self) -> None:
        for token in (
            "plain `size_t`, not an atomic",
            "serialized OGRE resource/render thread",
            "successful decode/upload",
            "checked state `n + 1`",
            "unrelated registry publication invalidates",
            "immediately before output publication",
        ):
            self.assertIn(token, self.doc)

    def test_build_workflow_and_provenance_include_this_gate(self) -> None:
        native_cmake = (ROOT / "tests/CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn(
            '"${ROR_REPOSITORY_ROOT}/source/main/gfx/ogre14/'
            'Ogre14AuthenticatedTextureReceipt.cpp"',
            native_cmake,
        )
        manifests = (
            ROOT / "tools/run_ogre_next_probe.py",
            ROOT / "tools/verify_ogre_next_artifact_set.py",
            ROOT / "tools/ogre_next_probe/CMakeLists.txt",
            ROOT / "tools/ogre_next_probe/cmake/VerifyN2SourceProvenance.cmake",
        )
        for manifest_path in manifests:
            with self.subTest(manifest=manifest_path.name):
                self.assertIn(
                    THIS_PATH, manifest_path.read_text(encoding="utf-8")
                )
        workflow = (ROOT / ".github/workflows/ogre-next-probe.yml").read_text(
            encoding="utf-8"
        )
        self.assertEqual(workflow.count("tests/tools/test_ogre14_*.py"), 2)
        self.assertEqual(workflow.count(THIS_PATH), 2)
        self.assertIn("python tools/run_ogre_next_probe.py --validate-contract-only", workflow)


if __name__ == "__main__":
    unittest.main()
