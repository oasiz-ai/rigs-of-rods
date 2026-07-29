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
FAMILY_PATH = (
    REPOSITORY_ROOT
    / "content-source/cityworld_next/buildings/storefront_family/"
    "rorng_city_storefront_family.v1.json"
)
FAMILY_VALIDATOR = (
    REPOSITORY_ROOT / "tools/validate_cityworld_storefront_family.py"
)
ASSET_VALIDATOR = REPOSITORY_ROOT / "tools/validate_cityworld_asset.py"
ASSET_COMPILER = REPOSITORY_ROOT / "tools/compile_cityworld_asset.py"
GENERATOR_PATH = (
    REPOSITORY_ROOT
    / "tools/blender/cityworld_next/generate_cityworld_storefront_family.py"
)


class CityWorldStorefrontFamilyTests(unittest.TestCase):
    @staticmethod
    def family() -> dict[str, object]:
        return json.loads(FAMILY_PATH.read_text(encoding="utf-8"))

    def manifests(self) -> list[Path]:
        return [
            REPOSITORY_ROOT / variant["manifest"]
            for variant in self.family()["variants"]
        ]

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
                str(FAMILY_VALIDATOR),
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

    def test_checked_family_passes_normal_and_optimized_gate(self) -> None:
        expected = {
            "assets": 5,
            "compiled_outputs": 30,
            "errors": 0,
            "placements": 40,
            "styles": 5,
            "valid": True,
        }
        for optimized in (False, True):
            with self.subTest(optimized=optimized):
                result, report = self.run_family(optimized=optimized)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(report["diagnostics"], [])
                self.assertEqual(report["summary"], expected)

    def test_assets_and_checked_ogre_packages_pass(self) -> None:
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
                self.assertEqual(report["summary"]["glb_nodes"], 4)
                self.assertEqual(report["summary"]["glb_materials"], 10)
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

    def test_audit_covers_all_high_reuse_storefronts(self) -> None:
        family = self.family()
        audit = family["legacy_audit"]
        counts = {
            item["legacy_object"]: item["placement_count"]
            for item in audit["objects"]
        }
        self.assertEqual(
            counts,
            {
                "store02": 6,
                "store03": 9,
                "store05": 9,
                "store06": 9,
                "store08": 7,
            },
        )
        self.assertEqual(sum(counts.values()), 40)
        self.assertFalse(audit["source_geometry_imported"])
        self.assertFalse(audit["source_materials_imported"])
        self.assertFalse(audit["source_textures_imported"])

    def test_selector_preserves_exact_fit_yaw_and_scale(self) -> None:
        family = self.family()
        variants = {
            item["legacy_object"]: item["asset_id"]
            for item in family["variants"]
        }
        assignments = family["selector"]["assignments"]
        self.assertEqual(
            [item["global_ordinal"] for item in assignments],
            list(range(40)),
        )
        self.assertTrue(
            all(item["variant"] == variants[item["legacy_object"]] for item in assignments)
        )
        self.assertTrue(all(item["uniform_scale"] == 1.0 for item in assignments))
        self.assertTrue(all(item["yaw_offset_degrees"] == 0.0 for item in assignments))
        self.assertTrue(
            all(item["yaw_degrees"] in {0.0, 90.0, 180.0, 270.0} for item in assignments)
        )
        self.assertEqual(
            Counter(item["legacy_object"] for item in assignments),
            Counter(
                {
                    "store02": 6,
                    "store03": 9,
                    "store05": 9,
                    "store06": 9,
                    "store08": 7,
                }
            ),
        )
        self.assertTrue(
            all(
                not ({"position", "location", "x", "y", "z"} & set(item))
                for item in assignments
            )
        )

    def test_buildings_are_grounded_and_lod_budgeted(self) -> None:
        for manifest_path in self.manifests():
            with self.subTest(manifest=manifest_path.name):
                manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
                geometry = manifest["geometry"]
                grounding = manifest["storefront"]["grounding"]
                self.assertEqual(manifest["asset"]["profile"], "static-building-v1")
                self.assertEqual(geometry["ground_plane_z_m"], 0.0)
                self.assertEqual(grounding["foundation_below_ground_m"], 0.0)
                self.assertEqual(grounding["placement_vertical_offset_m"], 0.0)
                self.assertTrue(
                    all(
                        lod["bounds_blender_z_up"]["min"][2] == 0.0
                        for lod in geometry["lods"]
                    )
                )
                self.assertEqual(
                    manifest["collision"]["objects"][0]["bounds_blender_z_up"]["min"][2],
                    0.0,
                )
                lods = {
                    item["lod"]: item["triangles"]
                    for item in geometry["lods"]
                }
                self.assertGreaterEqual(lods[0], 15_000)
                self.assertLessEqual(lods[0], 70_000)
                self.assertLessEqual(lods[1] / lods[0], 0.35)
                self.assertLessEqual(lods[2] / lods[0], 0.12)

    def test_portable_material_policy_has_one_justified_emissive(self) -> None:
        styles = set()
        for manifest_path in self.manifests():
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            styles.add(manifest["geometry"]["style_variant"])
            emissive = [
                material
                for material in manifest["materials"]
                if material.get("emissive_factor_linear")
            ]
            self.assertEqual(len(emissive), 1)
            self.assertEqual(
                emissive[0]["emissive_factor_linear"],
                [0.684, 0.3312, 0.0864],
            )
            self.assertEqual(
                manifest["storefront"]["emissive_policy"]["purpose"],
                "selected-occupied-interior-windows-only",
            )
            self.assertEqual(
                manifest["storefront"]["emissive_policy"]["runtime_point_lights"],
                0,
            )
            self.assertEqual(manifest["export"]["textures"], [])
            self.assertEqual(
                manifest["geometry"]["texcoord_policy"],
                "canonical-zero-textureless-v1",
            )
        self.assertEqual(len(styles), 5)

    def test_mutated_audit_selector_and_grounding_fail_closed(self) -> None:
        cases = (
            (
                lambda document: document["legacy_audit"]["archive"].update(
                    {"sha256": "0" * 64}
                ),
                "LEGACY_ARCHIVE",
            ),
            (
                lambda document: document["selector"]["assignments"][0].update(
                    {"uniform_scale": 1.1}
                ),
                "SELECTOR_STALE",
            ),
            (
                lambda document: document["placement_target"].update(
                    {"integration_status": "placed"}
                ),
                "PLACEMENT_TARGET",
            ),
        )
        for mutate, expected_code in cases:
            with self.subTest(code=expected_code):
                document = self.family()
                mutate(document)
                with tempfile.NamedTemporaryFile(
                    mode="w",
                    suffix=".json",
                    prefix=".storefront-family-test-",
                    dir=FAMILY_PATH.parent,
                    encoding="utf-8",
                    delete=False,
                ) as temporary:
                    json.dump(document, temporary, ensure_ascii=True, sort_keys=True)
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

    def test_static_building_profile_rejects_corridor_and_subgrade_data(self) -> None:
        manifest_path = self.manifests()[0]
        cases = (
            (
                lambda document: document["geometry"].update(
                    {"road_width_m": 8.9}
                ),
                "BUILDING_GEOMETRY",
            ),
            (
                lambda document: document["collision"]["objects"][0][
                    "bounds_blender_z_up"
                ].update({"min": [-9.83, -9.83, -0.1]}),
                "BUILDING_COLLISION_GROUND",
            ),
        )
        for mutate, expected_code in cases:
            with self.subTest(code=expected_code):
                document = json.loads(manifest_path.read_text(encoding="utf-8"))
                mutate(document)
                with tempfile.NamedTemporaryFile(
                    mode="w",
                    suffix=".asset.json",
                    prefix=".storefront-building-test-",
                    dir=manifest_path.parent,
                    encoding="utf-8",
                    delete=False,
                ) as temporary:
                    json.dump(document, temporary, ensure_ascii=True, sort_keys=True)
                    temporary.write("\n")
                    temporary_path = Path(temporary.name)
                try:
                    result = subprocess.run(
                        [
                            sys.executable,
                            str(ASSET_VALIDATOR),
                            str(temporary_path),
                            "--repo-root",
                            str(REPOSITORY_ROOT),
                        ],
                        check=False,
                        capture_output=True,
                        text=True,
                    )
                finally:
                    temporary_path.unlink()
                self.assertEqual(result.returncode, 1)
                report = json.loads(result.stdout)
                self.assertIn(
                    expected_code,
                    {item["code"] for item in report["diagnostics"]},
                )

    def test_generator_is_project_authored_and_has_no_archive_dependency(self) -> None:
        source = GENERATOR_PATH.read_text(encoding="utf-8")
        self.assertIn('"external_geometry": False', source)
        self.assertIn('"external_materials": False', source)
        self.assertIn('"external_textures": False', source)
        self.assertNotIn("zipfile", source)
        self.assertNotIn("urllib", source)
        self.assertNotIn("requests", source)
        self.assertNotIn("import random", source)
        self.assertIn("canonicalize_textureless_texcoords", source)


if __name__ == "__main__":
    unittest.main()
