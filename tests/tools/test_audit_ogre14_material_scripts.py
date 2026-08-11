#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = REPOSITORY_ROOT / "tools/audit_ogre14_material_scripts.py"

SPEC = importlib.util.spec_from_file_location(
    "audit_ogre14_material_scripts", TOOL_PATH
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load OGRE material-script auditor")
AUDITOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDITOR)


SIMPLE_SCRIPT = """\
material Road/Base
{
    technique
    {
        pass
        {
            diffuse 1 1 1 1
            texture_unit
            {
                texture_alias diffusemap
                texture road.dds
            }
        }
    }
}
"""

COMPLEX_SCRIPT = """\
material Glass/Facade : Shared/Base
{
    technique
    {
        pass
        {
            vertex_program_ref FacadeVP {}
            texture_unit { texture facade.dds }
            texture_unit
            {
                texture_alias specularmap
                texture facade_spec.dds
                env_map spherical
                scroll 0.1 0
            }
        }
    }
}
material Road/Base
{
    technique
    {
        pass {}
    }
}
"""


class Ogre14MaterialScriptAuditTests(unittest.TestCase):
    def make_archive(
        self,
        root: Path,
        *,
        entries: list[tuple[str, bytes]] | None = None,
    ) -> Path:
        archive_path = root / "CityWorld.zip"
        members = entries or [
            ("roads.material", SIMPLE_SCRIPT.encode()),
            ("facades.material", COMPLEX_SCRIPT.encode()),
            ("road.dds", b"texture"),
        ]
        with zipfile.ZipFile(
            archive_path, "w", compression=zipfile.ZIP_DEFLATED
        ) as archive:
            for name, payload in members:
                archive.writestr(name, payload)
        return archive_path

    def test_report_is_deterministic_and_does_not_guess_roles(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = self.make_archive(root)
            first = AUDITOR.audit_archive(archive)
            second = AUDITOR.audit_archive(archive)
            rendered = AUDITOR.canonical_json(first)

        self.assertEqual(first, second)
        self.assertEqual(first["format"], "ror-ogre14-material-script-audit-v1")
        self.assertNotIn(str(root), rendered)
        self.assertEqual(first["summary"]["material_definitions"], 3)
        self.assertEqual(first["summary"]["script_files"], 2)
        self.assertEqual(first["summary"]["v1_structural_candidates"], 2)
        self.assertEqual(first["summary"]["v1_structural_blocked"], 1)
        self.assertEqual(
            first["summary"]["authored_texture_alias_counts"],
            {"diffusemap": 1, "specularmap": 1},
        )
        self.assertEqual(
            first["summary"]["duplicate_exact_names"],
            [
                {
                    "exact_name": "Road/Base",
                    "locations": [
                        "facades.material:19",
                        "roads.material:1",
                    ],
                }
            ],
        )
        complex_material = next(
            material
            for material in first["materials"]
            if material["exact_name"] == "Glass/Facade"
        )
        self.assertTrue(complex_material["requires_explicit_semantic_declaration"])
        self.assertEqual(complex_material["texture_units"], 2)
        self.assertEqual(complex_material["texture_directives"], 2)
        self.assertEqual(
            complex_material["v1_structural_blockers"],
            [
                "SCRIPT_INHERITANCE_REQUIRES_NATIVE_RESOLUTION",
                "AUTHORED_GPU_PROGRAM",
                "MULTIPLE_TEXTURE_UNITS",
                "ENVIRONMENT_MAPPING",
                "TEXTURE_TRANSFORM_OR_ANIMATION",
            ],
        )
        self.assertNotIn("inferred_texture_roles", complex_material)

    def test_expected_hash_and_unsafe_members_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            archive = self.make_archive(Path(temporary))
            with self.assertRaisesRegex(
                AUDITOR.AuditFailure, "SHA-256 mismatch"
            ):
                AUDITOR.audit_archive(archive, expected_sha256="0" * 64)

        with tempfile.TemporaryDirectory() as temporary:
            archive = self.make_archive(
                Path(temporary),
                entries=[
                    ("safe.material", SIMPLE_SCRIPT.encode()),
                    ("../unsafe.material", SIMPLE_SCRIPT.encode()),
                ],
            )
            with self.assertRaisesRegex(AUDITOR.AuditFailure, "unsafe ZIP"):
                AUDITOR.audit_archive(archive)

    def test_nul_script_and_definition_cap_fail_closed(self) -> None:
        with self.assertRaisesRegex(AUDITOR.AuditFailure, "contains NUL"):
            AUDITOR.parse_material_script(
                "material Good {}\x00material Bad {}", "bad.material"
            )

        original = AUDITOR.MAX_MATERIAL_DEFINITIONS
        try:
            AUDITOR.MAX_MATERIAL_DEFINITIONS = 1
            with self.assertRaisesRegex(
                AUDITOR.AuditFailure, "definition count exceeds"
            ):
                AUDITOR.parse_material_script(
                    "material One {}\nmaterial Two {}\n", "many.material"
                )
        finally:
            AUDITOR.MAX_MATERIAL_DEFINITIONS = original

    def test_cli_output_round_trips(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = self.make_archive(root)
            output = root / "report.json"
            self.assertEqual(
                AUDITOR.main([str(archive), "--output", str(output)]), 0
            )
            report = json.loads(output.read_text(encoding="utf-8"))
        self.assertTrue(report["ok"])


if __name__ == "__main__":
    unittest.main()
