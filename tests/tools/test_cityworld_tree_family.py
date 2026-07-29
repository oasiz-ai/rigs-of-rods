#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

from collections import Counter
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = REPOSITORY_ROOT / "tools/validate_cityworld_tree_family.py"
ASSET_VALIDATOR = REPOSITORY_ROOT / "tools/validate_cityworld_asset.py"
ASSET_COMPILER = REPOSITORY_ROOT / "tools/compile_cityworld_asset.py"
GENERATOR_PATH = (
    REPOSITORY_ROOT
    / "tools/blender/cityworld_next/generate_neoq_tree_family.py"
)
FAMILY_PATH = (
    REPOSITORY_ROOT
    / "content-source/cityworld_next/vegetation/"
    "rorng_city_neoq_tree_family.v1.json"
)


class CityWorldTreeFamilyTests(unittest.TestCase):
    def run_family(
        self,
        family: Path = FAMILY_PATH,
        *,
        optimized: bool = False,
    ) -> tuple[subprocess.CompletedProcess[str], dict[str, object]]:
        command = [sys.executable]
        if optimized:
            command.append("-O")
        command.extend(
            [
                str(TOOL_PATH),
                str(family),
                "--repo-root",
                str(REPOSITORY_ROOT),
            ]
        )
        result = subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
        )
        return result, json.loads(result.stdout)

    @staticmethod
    def family() -> dict[str, object]:
        return json.loads(FAMILY_PATH.read_text(encoding="utf-8"))

    def manifests(self) -> list[Path]:
        family = self.family()
        variants = family["variants"]
        self.assertIsInstance(variants, list)
        return [
            REPOSITORY_ROOT / variant["manifest"]
            for variant in variants
        ]

    def test_checked_family_passes_normal_and_optimized_gate(self) -> None:
        expected = {
            "assets": 3,
            "compiled_outputs": 18,
            "errors": 0,
            "placements": 18,
            "silhouettes": 3,
            "valid": True,
        }
        for optimized in (False, True):
            with self.subTest(optimized=optimized):
                result, report = self.run_family(optimized=optimized)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(report["diagnostics"], [])
                self.assertEqual(report["summary"], expected)

    def test_all_variants_pass_generic_asset_and_compiler_gates(self) -> None:
        for manifest in self.manifests():
            with self.subTest(manifest=manifest.name):
                validation = subprocess.run(
                    [
                        sys.executable,
                        str(ASSET_VALIDATOR),
                        str(manifest),
                        "--repo-root",
                        str(REPOSITORY_ROOT),
                    ],
                    check=False,
                    capture_output=True,
                    text=True,
                )
                self.assertEqual(
                    validation.returncode,
                    0,
                    validation.stderr or validation.stdout,
                )
                report = json.loads(validation.stdout)
                self.assertTrue(report["summary"]["valid"])
                self.assertEqual(report["summary"]["glb_nodes"], 4)
                self.assertEqual(report["summary"]["glb_materials"], 4)
                self.assertEqual(report["summary"]["runtime_lights"], 0)

                compilation = subprocess.run(
                    [
                        sys.executable,
                        str(ASSET_COMPILER),
                        str(manifest),
                        "--repo-root",
                        str(REPOSITORY_ROOT),
                        "--validate-checked",
                    ],
                    check=False,
                    capture_output=True,
                    text=True,
                )
                self.assertEqual(
                    compilation.returncode,
                    0,
                    compilation.stderr or compilation.stdout,
                )
                compile_report = json.loads(compilation.stdout)
                self.assertEqual(
                    {output["role"] for output in compile_report["outputs"]},
                    {
                        "material-fallback",
                        "terrain-object",
                        "collision-fixture",
                        "render-lod0",
                        "render-lod1",
                        "render-lod2",
                    },
                )

    def test_selector_is_bounded_and_uses_every_silhouette(self) -> None:
        family = self.family()
        assignments = family["selector"]["assignments"]
        variants = family["variants"]
        ids = {variant["asset_id"] for variant in variants}
        counts = Counter(assignment["variant"] for assignment in assignments)

        self.assertEqual(
            [assignment["placement_ordinal"] for assignment in assignments],
            list(range(18)),
        )
        self.assertEqual(set(counts), ids)
        self.assertGreaterEqual(min(counts.values()), 3)
        self.assertTrue(
            all(0.94 <= assignment["scale"] <= 1.06 for assignment in assignments)
        )
        self.assertTrue(
            all(
                0.0 <= assignment["yaw_degrees"] < 360.0
                for assignment in assignments
            )
        )
        self.assertEqual(
            len({assignment["digest_sha256"] for assignment in assignments}),
            18,
        )

    def test_asset_budgets_and_geometry_foliage_are_explicit(self) -> None:
        for manifest_path in self.manifests():
            with self.subTest(manifest=manifest_path.name):
                manifest = json.loads(
                    manifest_path.read_text(encoding="utf-8")
                )
                lods = {
                    item["lod"]: item["triangles"]
                    for item in manifest["geometry"]["lods"]
                }
                self.assertGreaterEqual(lods[0], 10_000)
                self.assertLessEqual(lods[0], 35_000)
                self.assertLessEqual(lods[1] / lods[0], 0.4)
                self.assertLessEqual(lods[2] / lods[0], 0.12)
                self.assertEqual(
                    manifest["vegetation"]["foliage"],
                    {
                        "alpha_mode": "opaque-geometry",
                        "mip_safe": True,
                        "texture_dependencies": [],
                    },
                )
                self.assertEqual(manifest["export"]["textures"], [])
                self.assertEqual(
                    manifest["collision"]["purpose"],
                    "trunk-only-vehicle-contact",
                )

    def test_mutated_selector_and_impostor_contract_fail_closed(self) -> None:
        cases = (
            (
                lambda document: document["selector"]["assignments"][0].update(
                    {"variant": document["variants"][1]["asset_id"]}
                ),
                "SELECTOR_STALE",
            ),
            (
                lambda document: document["impostor"].update(
                    {"compiler_emits": True}
                ),
                "IMPOSTOR_PROFILE",
            ),
            (
                lambda document: document["wind"].update(
                    {"runtime_consumes": True}
                ),
                "WIND_PROFILE",
            ),
        )
        for mutate, expected_code in cases:
            with self.subTest(code=expected_code):
                document = self.family()
                mutate(document)
                with tempfile.NamedTemporaryFile(
                    mode="w",
                    suffix=".json",
                    prefix=".tree-family-test-",
                    dir=FAMILY_PATH.parent,
                    encoding="utf-8",
                    delete=False,
                ) as temporary:
                    json.dump(document, temporary, sort_keys=True)
                    temporary.write("\n")
                    temporary_path = Path(temporary.name)
                try:
                    result, report = self.run_family(temporary_path)
                finally:
                    temporary_path.unlink()
                self.assertEqual(result.returncode, 1)
                self.assertIn(
                    expected_code,
                    {item["code"] for item in report["diagnostics"]},
                )

    def test_generator_has_no_external_asset_or_random_dependency(self) -> None:
        source = GENERATOR_PATH.read_text(encoding="utf-8")
        self.assertIn("class StableRng:", source)
        self.assertIn('"external_geometry": False', source)
        self.assertIn('"external_textures": False', source)
        self.assertNotIn("import random", source)
        self.assertNotIn("urllib", source)
        self.assertNotIn("requests", source)


if __name__ == "__main__":
    unittest.main()
