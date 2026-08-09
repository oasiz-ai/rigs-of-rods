#!/usr/bin/env python3
"""Compiler and static contracts for the RORMAT2 semantic catalog."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
COMPILER = ROOT / "tools/compile_ogre14_material_semantic_catalog_v2.py"
SCHEMA = ROOT / "tools/schemas/ogre14-material-semantic-catalog-v2.schema.json"
FIXTURE = ROOT / "tests/fixtures/gfx/ogre14/material-semantic-catalog-v2.synthetic.json"
HEADER = ROOT / "source/main/gfx/ogre14/Ogre14LegacyMaterialSemanticCatalogV2.h"
SOURCE = ROOT / "source/main/gfx/ogre14/Ogre14LegacyMaterialSemanticCatalogV2.cpp"
CPP_TEST = ROOT / "tests/gfx/ogre14/Ogre14LegacyMaterialSemanticCatalogV2Tests.cpp"
PATHS = (
    "doc/nextgen/OGRE14_MATERIAL_SEMANTIC_CATALOG_V2.md",
    "source/main/gfx/ogre14/Ogre14LegacyMaterialSemanticCatalogV2.cpp",
    "source/main/gfx/ogre14/Ogre14LegacyMaterialSemanticCatalogV2.h",
    "tests/gfx/ogre14/Ogre14LegacyMaterialSemanticCatalogV2Tests.cpp",
    "tests/fixtures/gfx/ogre14/material-semantic-catalog-v2.synthetic.json",
    "tests/tools/test_ogre14_material_semantic_catalog_v2.py",
    "tools/compile_ogre14_material_semantic_catalog_v2.py",
    "tools/schemas/ogre14-material-semantic-catalog-v2.schema.json",
)

SPEC = importlib.util.spec_from_file_location("ror_catalog_v2", COMPILER)
assert SPEC is not None and SPEC.loader is not None
CATALOG = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CATALOG)


class Ogre14MaterialSemanticCatalogV2Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.document = json.loads(FIXTURE.read_text(encoding="utf-8"))
        cls.compiler_text = COMPILER.read_text(encoding="utf-8")
        cls.header = HEADER.read_text(encoding="utf-8")
        cls.source = SOURCE.read_text(encoding="utf-8")
        cls.cpp_test = CPP_TEST.read_text(encoding="utf-8")

    def test_compiler_is_deterministic_and_canonical(self) -> None:
        compiled = CATALOG.compile_document(copy.deepcopy(self.document))
        self.assertEqual(len(compiled), 1067)
        fields = struct.unpack("<8sHHIII32sQ", compiled[:64])
        self.assertEqual(fields[:6], (b"RORMAT2\0", 2, 64, 0, 2, 1003))
        self.assertEqual(fields[6], hashlib.sha256(compiled[64:]).digest())
        self.assertEqual(
            fields[6].hex(),
            "4b723f73c874b4dfe06aa4ddd7026a62764c6fd5da3045b5501f990ecd28c2ce",
        )
        self.assertEqual(fields[7], 0)
        reversed_document = copy.deepcopy(self.document)
        reversed_document["records"].reverse()
        self.assertEqual(
            CATALOG.compile_document(reversed_document), compiled
        )

    def test_every_json_object_is_closed(self) -> None:
        mutations = []
        for path in (
            (),
            ("records", 0),
            ("records", 0, "pass"),
            ("records", 1, "texture_units", 0),
            ("records", 1, "texture_units", 0, "sampler"),
            ("records", 1, "texture_units", 0, "combine"),
        ):
            candidate = copy.deepcopy(self.document)
            target = candidate
            for component in path:
                target = target[component]
            target["unknown_field"] = 1
            mutations.append(candidate)
        for candidate in mutations:
            with self.subTest(candidate=candidate):
                with self.assertRaises(CATALOG.CatalogError):
                    CATALOG.compile_document(candidate)

    def test_duplicate_and_trailing_json_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            duplicate = Path(directory) / "duplicate.json"
            duplicate.write_text(
                '{"schema":"ror.ogre14.material-semantic-catalog.v2",'
                '"schema":"ror.ogre14.material-semantic-catalog.v2",'
                '"records":[]}',
                encoding="utf-8",
            )
            with self.assertRaises(CATALOG.CatalogError):
                CATALOG.load_source(duplicate)
            trailing = Path(directory) / "trailing.json"
            trailing.write_text(
                FIXTURE.read_text(encoding="utf-8") + " trailing",
                encoding="utf-8",
            )
            with self.assertRaises(CATALOG.CatalogError):
                CATALOG.load_source(trailing)

    def test_exact_binding_and_cross_field_failures_are_rejected(self) -> None:
        mutations = []
        uppercase_digest = copy.deepcopy(self.document)
        uppercase_digest["records"][0]["package_archive_sha256"] = "A" * 64
        mutations.append(uppercase_digest)
        nonfinite = copy.deepcopy(self.document)
        nonfinite["records"][1]["texture_units"][0]["sampler"][
            "maximum_lod_f32_bits"
        ] = "7f800000"
        mutations.append(nonfinite)
        wrong_ordinal = copy.deepcopy(self.document)
        wrong_ordinal["records"][1]["texture_units"][1]["ordinal"] = 7
        mutations.append(wrong_ordinal)
        wrong_environment = copy.deepcopy(self.document)
        wrong_environment["records"][1]["environment_texture_unit"] = 0
        mutations.append(wrong_environment)
        role_mismatch = copy.deepcopy(self.document)
        role_mismatch["records"][1]["texture_units"][0]["color_role"] = (
            "LINEAR_DATA"
        )
        mutations.append(role_mismatch)
        duplicate = copy.deepcopy(self.document)
        duplicate["records"][0]["material_name"] = duplicate["records"][1][
            "material_name"
        ]
        mutations.append(duplicate)
        for candidate in mutations:
            with self.subTest(candidate=candidate):
                with self.assertRaises(CATALOG.CatalogError):
                    CATALOG.compile_document(candidate)

    def test_failed_compile_preserves_existing_output(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "catalog.rormat2"
            output.write_bytes(b"sentinel")
            bad_source = Path(directory) / "bad.json"
            bad_source.write_text("{}", encoding="utf-8")
            with self.assertRaises(CATALOG.CatalogError):
                CATALOG.compile_file(bad_source, output)
            self.assertEqual(output.read_bytes(), b"sentinel")

    def test_schema_and_compiler_cover_all_required_exact_facts(self) -> None:
        schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
        self.assertFalse(schema["additionalProperties"])
        record = schema["$defs"]["record"]
        self.assertFalse(record["additionalProperties"])
        for field in (
            "package_archive_sha256",
            "resource_group",
            "resource_generation",
            "source_script_member",
            "source_script_sha256",
            "effective_script_sha256",
            "repair_plan_version",
            "material_name",
            "native_structure_sha256",
            "selected_scheme",
            "selected_lod",
            "runtime_generation",
            "base_color_semantic",
            "registry_texture_color_role",
            "lowering_algorithm",
            "lowering_version",
            "declaration_revision",
            "pass",
            "environment_augmentation",
            "shadow_augmentation",
            "texture_units",
        ):
            self.assertIn(field, record["required"])
            self.assertIn(field, self.compiler_text)
        for definition in ("pass", "sampler", "combine", "textureUnit"):
            self.assertFalse(schema["$defs"][definition]["additionalProperties"])

    def test_cpp_parser_is_bounded_transactional_and_rapidjson_free(self) -> None:
        joined = self.header + self.source + self.cpp_test
        for token in (
            "maximum_catalog_bytes",
            "maximum_records",
            "maximum_texture_units_per_record",
            "maximum_total_string_bytes",
            "AFTER_HEADER",
            "AFTER_FIRST_RECORD",
            "BEFORE_COMMIT",
            "std::bad_alloc",
            "throw 17",
            "SharesImmutableStateWith",
            "unknown trailing compiled field",
            "empty input mutated committed output",
            "BuildOgre14LegacyMaterialSemanticRegistryFromCatalogV2",
        ):
            self.assertIn(token, joined)
        self.assertNotIn("RapidJSON", self.source)
        self.assertNotIn("rapidjson", self.source)
        for forbidden in ("tolower(", "regex", "find(\"specular"):
            self.assertNotIn(forbidden, self.source)

    def test_build_provenance_and_cross_platform_gate_cover_catalog(self) -> None:
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
        native_cmake = (ROOT / "tests/CMakeLists.txt").read_text(encoding="utf-8")
        probe_cmake = (ROOT / "tools/ogre_next_probe/CMakeLists.txt").read_text(encoding="utf-8")
        workflow = (ROOT / ".github/workflows/ogre-next-probe.yml").read_text(encoding="utf-8")
        for cmake in (native_cmake, probe_cmake):
            self.assertIn("ror_ogre14_material_semantic_catalog_v2_tests", cmake)
            self.assertIn("compile_ogre14_material_semantic_catalog_v2.py", cmake)
        self.assertIn("ror_ogre14_material_semantic_catalog_v2_tests", workflow)
        self.assertIn("ror_ogre14_material_semantic_catalog_v2", workflow)


if __name__ == "__main__":
    unittest.main()
