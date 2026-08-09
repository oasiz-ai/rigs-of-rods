#!/usr/bin/env python3
"""Static contract for explicit OGRE 14 material semantic declarations."""

from __future__ import annotations

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "source/main/gfx/ogre14/Ogre14LegacyMaterialSemanticRegistry.h"
SOURCE = ROOT / "source/main/gfx/ogre14/Ogre14LegacyMaterialSemanticRegistry.cpp"
CPP_TEST = ROOT / "tests/gfx/ogre14/Ogre14LegacyMaterialSemanticRegistryTests.cpp"
PATHS = (
    "source/main/gfx/ogre14/Ogre14LegacyMaterialSemanticRegistry.cpp",
    "source/main/gfx/ogre14/Ogre14LegacyMaterialSemanticRegistry.h",
    "tests/gfx/ogre14/Ogre14LegacyMaterialSemanticRegistryTests.cpp",
    "tests/tools/test_ogre14_material_semantic_registry_contract.py",
)


class Ogre14MaterialSemanticRegistryContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = HEADER.read_text(encoding="utf-8")
        cls.source = SOURCE.read_text(encoding="utf-8")
        cls.cpp_test = CPP_TEST.read_text(encoding="utf-8")

    def test_only_explicit_versioned_provenance_is_representable(self) -> None:
        for token in (
            "CONTENT_METADATA",
            "VERSIONED_COMPATIBILITY_TABLE",
            "source_revision",
            "Ogre14LegacyAssetKey material_key",
            "Ogre14LegacyMaterialSemanticResolutionMatchesKey",
            "Ogre14LegacyBaseColorSemantic base_color_semantic",
            "Ogre14LegacyTextureColorRole texture_color_role",
            "Ogre14LegacyMaterialSemanticDeclarationIdentityReceipt",
            "SameOgre14LegacyMaterialSemanticDeclarationIdentity",
            "Ogre14LegacyMaterialSemanticResolutionAuthenticates",
        ):
            self.assertIn(token, self.header)
        for forbidden in ("tolower(", "regex", "filename", "specular"):
            self.assertNotIn(forbidden, self.source)
        self.assertIn("BuildOgre14LegacyStableAssetKey", self.source)
        self.assertIn("state_->declarations.find(material_key)", self.source)

    def test_registry_is_bounded_and_transactional(self) -> None:
        for token in (
            "maximum_declarations",
            "maximum_total_key_bytes",
            "AFTER_FIRST_DECLARATION",
            "BEFORE_COMMIT",
            "std::make_shared",
            "catch (const std::bad_alloc &)",
            "output = Ogre14LegacyMaterialSemanticRegistry",
            "SharesImmutableStateWith",
        ):
            self.assertIn(token, self.header + self.source)
        for token in (
            "cap+1",
            "bad_alloc changed committed semantic registry or owner",
            "unexpected exception changed semantic registry or owner",
            "case-folded material key",
            "duplicate exact declaration key",
            "caller mutation changed immutable registry keys",
            "cross-declaration resolution forged semantic authority",
            "fresh registry build reused a numeric or content-derived identity",
            "stale result authenticated after registry replacement",
            "caller-created empty receipt forged an issued resolution",
            "rollback did not preserve reusable immutable declaration state",
        ):
            self.assertIn(token, self.cpp_test)

    def test_native_capture_receives_the_resolved_declaration(self) -> None:
        self.assertIn(
            '#include "Ogre14LegacyNativeAssetExtractor.h"', self.header
        )
        self.assertIn(
            "Ogre14LegacyNativeMaterialDeclaration native_declaration",
            self.header,
        )
        for token in (
            "candidate.native_declaration.base_color_semantic",
            "candidate.native_declaration.texture_color_role",
            "candidate.native_declaration.translator_configuration",
        ):
            self.assertIn(token, self.source)

    def test_build_and_cross_platform_probe_execute_the_cpp_gate(self) -> None:
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
            self.assertIn(
                "ror_ogre14_material_semantic_registry_tests", cmake
            )
            self.assertIn(
                "Ogre14LegacyMaterialSemanticRegistryTests.cpp", cmake
            )
        semantic_probe_target = probe_cmake.split(
            "add_executable(\n        ror_ogre14_material_semantic_registry_tests",
            1,
        )[1].split("set_target_properties(", 1)[0]
        self.assertIn(
            "source/main/gfx/render/Ogre14LegacyAssetTranslator.cpp",
            semantic_probe_target,
        )
        self.assertIn(
            "ror_ogre14_material_semantic_registry_tests", workflow
        )
        self.assertIn(
            "-R '^ror_ogre14_material_semantic_registry$'", workflow
        )
        self.assertEqual(
            workflow.count(
                "source/main/gfx/ogre14/"
                "Ogre14LegacyMaterialSemanticRegistry.*"
            ),
            2,
        )
        self.assertEqual(
            workflow.count(
                "tests/gfx/ogre14/"
                "Ogre14LegacyMaterialSemanticRegistryTests.cpp"
            ),
            2,
        )

    def test_provenance_manifests_cover_the_complete_registry(self) -> None:
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
