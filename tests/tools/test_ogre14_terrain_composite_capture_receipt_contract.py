#!/usr/bin/env python3
"""Static closure for lossless OGRE 14 terrain-composite capture."""

from __future__ import annotations

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "source/main/gfx/ogre14/Ogre14TerrainCompositeCaptureReceipt.h"
SOURCE = ROOT / "source/main/gfx/ogre14/Ogre14TerrainCompositeCaptureReceipt.cpp"
NATIVE = ROOT / "source/main/gfx/ogre14/Ogre14TerrainCompositeNativeAdapter.cpp"
TEST = ROOT / "tests/gfx/ogre14/Ogre14TerrainCompositeCaptureReceiptTests.cpp"
NATIVE_TEST = ROOT / "tests/gfx/ogre14/Ogre14TerrainCompositeNativeReadbackTests.cpp"
DOC = ROOT / "doc/nextgen/OGRE14_TERRAIN_COMPOSITE_CAPTURE_RECEIPTS.md"
PATCH = (
    ROOT
    / "cmake/conan/recipes/ogre3d/patches/14.5.2/"
    "terrain-composite-revision-metal-readback.patch"
)
THIS_PATH = (
    "tests/tools/test_ogre14_terrain_composite_capture_receipt_contract.py"
)


def executable_block(cmake: str, target: str) -> str:
    marker = f"add_executable(\n        {target}"
    start = cmake.index(marker)
    end = cmake.index("\n    )", start) + len("\n    )")
    return cmake[start:end]


class Ogre14TerrainCompositeCaptureReceiptContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = HEADER.read_text(encoding="utf-8")
        cls.source = SOURCE.read_text(encoding="utf-8")
        cls.native = NATIVE.read_text(encoding="utf-8")
        cls.test = TEST.read_text(encoding="utf-8")
        cls.native_test = NATIVE_TEST.read_text(encoding="utf-8")
        cls.doc = DOC.read_text(encoding="utf-8")
        cls.patch = PATCH.read_text(encoding="utf-8")

    def test_receipt_is_versioned_immutable_and_native_minted(self) -> None:
        contract = self.header + self.source
        for token in (
            "kOgre14TerrainCompositeCaptureReceiptVersion = 1U",
            "kOgre14TerrainCompositeNativeObservationVersion = 1U",
            "class Ogre14TerrainCompositeCaptureReceipt final",
            "class Ogre14TerrainCompositeNativeAdapter final",
            "std::shared_ptr<const State> state_",
            "friend class Ogre14TerrainCompositeNativeAdapter",
            "ROR_OGRE14_TERRAIN_COMPOSITE_CAPTURE_INTERNAL_TESTING",
            "Ogre14TerrainCompositeCaptureTestAccess() = delete",
            "SharesImmutableStateWith",
            "std::is_nothrow_move_assignable<",
            "receipt publication must be transactional",
        ):
            self.assertIn(token, contract)
        public_receipt = self.header.split(
            "class Ogre14TerrainCompositeCaptureReceipt final", 1
        )[1].split("private:", 1)[0]
        self.assertNotIn("Build", public_receipt)
        self.assertNotIn("Mint", public_receipt)
        self.assertNotIn("State>", public_receipt)
        receipt_private = self.header.split(
            "class Ogre14TerrainCompositeCaptureReceipt final", 1
        )[1].split("class Ogre14TerrainCompositeNativeAdapter final", 1)[0]
        self.assertNotIn(
            "friend class Testing::Ogre14TerrainCompositeCaptureTestAccess",
            receipt_private,
        )
        self.assertIn(
            "neither that entry point nor a caller-definable friend seam",
            self.doc,
        )

    def test_exact_native_update_readback_and_revalidation_order(self) -> None:
        for token in (
            "OGRE_VERSION_MAJOR == 14",
            "OGRE_VERSION_MINOR == 5",
            "OGRE_VERSION_PATCH == 2",
            "FindExactSlot(",
            "terrain->updateCompositeMap();",
            "texture->getBuffer(0U, 0U)",
            "Ogre::PF_BYTE_RGBA",
            "pixel_buffer->blitToMemory(destination);",
            "current_terrain->getCompositeMap()",
            "CaptureObservation(terrain_group, slot_x, slot_y,",
            "PublishOwnedReadback(configuration, before_readback",
        ):
            self.assertIn(token, self.native)
        self.assertLess(
            self.native.index("terrain->updateCompositeMap();"),
            self.native.index("CaptureObservation(terrain_group"),
        )
        self.assertLess(
            self.native.index("pixel_buffer->blitToMemory(destination);"),
            self.native.index("current_terrain->getCompositeMap()"),
        )
        self.assertLess(
            self.native.index("current_terrain->getCompositeMap()"),
            self.native.index(
                "PublishOwnedReadback(configuration, before_readback"
            ),
        )

    def test_authority_is_not_name_only(self) -> None:
        contract = self.header + self.source + self.native
        for token in (
            "terrain_group_pointer_token",
            "terrain_slot_pointer_token",
            "terrain_pointer_token",
            "packed_slot_key",
            "slot_x",
            "slot_y",
            "page_definition_kind",
            "exact_definition_filename",
            "definition_import_data_pointer_token",
            "generated_save_filename",
            "terrain_is_loaded",
            "terrain_derived_data_update_in_progress",
            "texture_pointer_token",
            "pixel_buffer_pointer_token",
            "texture_handle",
            "exact_texture_resource_group",
            "exact_texture_name",
            "texture_resource_revision_before_readback",
            "texture_resource_revision_after_readback",
            "texture_loading_state",
            "SameObservation(before_readback, after_readback)",
        ):
            self.assertIn(token, contract)
        for token in (
            "substituted texture pointer was accepted",
            "substituted pixel-buffer pointer was accepted",
            "substituted Terrain slot pointer was accepted",
            "substituted Terrain pointer was accepted",
            "name-only texture substitution was accepted",
            "TerrainGroup page substitution was accepted",
            "Terrain page definition substitution was accepted",
            "Terrain generated save identity substitution was accepted",
            "stale texture revision was accepted",
        ):
            self.assertIn(token, self.test)
        self.assertIn(
            "consumed import was mislabeled as a file-backed page source",
            self.test,
        )
        self.assertNotIn("exact page filename identity", self.doc)
        self.assertIn(
            "It does not\nclaim that `TerrainGroup::generateFilename()` was the historical load source",
            self.doc,
        )

    def test_lossless_layout_digest_and_caps_are_closed(self) -> None:
        contract = self.header + self.source
        for token in (
            "OGRE_PIXELBOX_ROW_ZERO_FIRST_NO_FLIP",
            "RED_GREEN_BLUE_ALPHA",
            "tight_row_pitch_bytes",
            "tight_slice_pitch_bytes",
            "rgba_sha256",
            "CheckedMultiplyU64",
            "kOgre14TerrainCompositeHardMaximumDimension",
            "kOgre14TerrainCompositeHardMaximumRgbaBytes",
            "maximum_identifier_bytes",
        ):
            self.assertIn(token, contract)
        for token in (
            "readback rows were flipped or repacked",
            "specular-mask alpha bytes were discarded",
            "retained RGBA SHA-256 changed",
            "RGBA layout overflow returned the wrong failure",
            "padded-row claim returned the wrong failure",
        ):
            self.assertIn(token, self.test)
        self.assertIn("without changing row orientation", self.patch)
        self.assertIn("sourceLevel:static_cast<NSUInteger>(mLevel)", self.patch)

    def test_alpha_semantic_blocks_dishonest_unlit_or_pbr_lowering(self) -> None:
        contract = self.header + self.source + self.doc
        for token in (
            "LINEAR_SPECULAR_MASK",
            "BLOCKED_ALPHA_IS_SPECULAR_MASK",
            "terrain_composite.material_descriptor.alpha_specular_mask",
            "MaterialDescriptor base alpha is coverage",
            "UNLIT and PBR lowering are not lossless",
            "three diffuse channels plus one specular-mask channel",
        ):
            self.assertIn(token, contract)
        self.assertIn("alpha carries the per-layer linear\nspecular mask", self.doc)

    def test_transaction_faults_preserve_deep_owner(self) -> None:
        contract = self.header + self.source + self.test
        for token in (
            "AFTER_NATIVE_IDENTITY_CAPTURE",
            "AFTER_RGBA_ALLOCATION",
            "AFTER_NATIVE_READBACK",
            "BEFORE_RECEIPT_PUBLICATION",
            "catch (const std::bad_alloc &)",
            "catch (...) ",
            "exception path changed deep receipt owner",
            "output.metadata() == metadata_before",
            "output.rgba_bytes() == bytes_before",
        ):
            self.assertIn(token, contract)

    def test_source_is_backend_portable_and_native_probe_is_scoped(self) -> None:
        for forbidden in (
            "Metal/",
            "OgreMetal",
            "d3d11.h",
            "OgreD3D",
            "OgreGL",
            "windows.h",
        ):
            self.assertNotIn(forbidden, self.native)
        for token in (
            "OgreHardwarePixelBuffer.h",
            "Terrain/OgreTerrain.h",
            "Terrain/OgreTerrainGroup.h",
            "pinned OGRE public PF_BYTE_RGBA conversion changed channels or row order",
            "native alpha/specular bytes were not preserved",
        ):
            self.assertIn(token, self.native + self.native_test)

        cmake = (ROOT / "tests/CMakeLists.txt").read_text(encoding="utf-8")
        native_block = executable_block(
            cmake, "ror_ogre14_terrain_composite_native_readback_tests"
        )
        self.assertIn("Ogre14TerrainCompositeNativeAdapter.cpp", native_block)
        self.assertIn("Ogre14TerrainCompositeNativeReadbackTests.cpp", native_block)
        self.assertIn("PRIVATE OGRE::Terrain", cmake)
        self.assertGreaterEqual(
            cmake.count(
                "ROR_OGRE14_TERRAIN_COMPOSITE_CAPTURE_INTERNAL_TESTING=1"
            ),
            2,
        )
        probe_cmake = (ROOT / "tools/ogre_next_probe/CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            "ROR_OGRE14_TERRAIN_COMPOSITE_CAPTURE_INTERNAL_TESTING=1",
            probe_cmake,
        )

    def test_provenance_and_ci_include_this_gate(self) -> None:
        manifests = (
            ROOT / "tools/run_ogre_next_probe.py",
            ROOT / "tools/verify_ogre_next_artifact_set.py",
            ROOT / "tools/ogre_next_probe/CMakeLists.txt",
            ROOT / "tools/ogre_next_probe/cmake/VerifyN2SourceProvenance.cmake",
        )
        for manifest in manifests:
            with self.subTest(manifest=manifest.name):
                self.assertIn(THIS_PATH, manifest.read_text(encoding="utf-8"))
        workflow = (ROOT / ".github/workflows/ogre-next-probe.yml").read_text(
            encoding="utf-8"
        )
        self.assertEqual(workflow.count(THIS_PATH), 2)


if __name__ == "__main__":
    unittest.main()
