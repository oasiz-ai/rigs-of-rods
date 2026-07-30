#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
FAMILY_PATH = (
    REPOSITORY_ROOT
    / "content-source/cityworld_next/regional_infill/"
    "rorng_city_regional_infill_family.v1.json"
)
ASSET_VALIDATOR = REPOSITORY_ROOT / "tools/validate_cityworld_asset.py"
ASSET_COMPILER = REPOSITORY_ROOT / "tools/compile_cityworld_asset.py"

EXPECTED_VARIANTS = {
    "rorng_city_infill_farmstead_98x86": ("farmland", [4884, 324, 84]),
    "rorng_city_infill_suburb_block_96x88": ("suburb", [8976, 560, 292]),
    "rorng_city_infill_service_station_90x65": (
        "service-station",
        [4120, 232, 140],
    ),
    "rorng_city_infill_red_mesa_19m": (
        "natural-landmark",
        [4552, 348, 36],
    ),
    "rorng_city_infill_arroyo_oasis_19m": (
        "natural-landmark",
        [7892, 560, 112],
    ),
}
EXPECTED_RUNTIME_LIGHTS = {
    "rorng_city_infill_farmstead_98x86": 0,
    "rorng_city_infill_suburb_block_96x88": 0,
    "rorng_city_infill_service_station_90x65": 6,
    "rorng_city_infill_red_mesa_19m": 0,
    "rorng_city_infill_arroyo_oasis_19m": 0,
}
EXPECTED_RUNTIME_ROLES = {
    "collision-fixture",
    "material-fallback",
    "render-lod0",
    "render-lod1",
    "render-lod2",
    "terrain-object",
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


class CityWorldInfillAssetTests(unittest.TestCase):
    @staticmethod
    def family() -> dict[str, object]:
        return json.loads(FAMILY_PATH.read_text(encoding="utf-8"))

    def variants(self) -> list[tuple[dict[str, object], Path]]:
        return [
            (variant, REPOSITORY_ROOT / variant["manifest"])
            for variant in self.family()["variants"]
        ]

    def test_family_is_project_authored_and_pins_the_generator(self) -> None:
        family = self.family()
        self.assertEqual(
            family["format"],
            "ror-cityworld-regional-infill-family-v1",
        )
        self.assertEqual(
            family["asset"],
            {
                "author": "Oasiz AI and Rigs of Rods contributors",
                "id": "rorng_city_regional_infill_family",
                "license": "GPL-3.0-or-later",
                "source_uri": "https://github.com/oasiz-ai/rigs-of-rods",
                "version": 1,
            },
        )
        provenance = family["authoring"]["procedural_provenance"]
        self.assertEqual(
            provenance,
            {
                "external_geometry": False,
                "external_materials": False,
                "external_textures": False,
                "method": "deterministic-project-authored-blender-python",
                "rights_basis": "GPL-3.0-or-later project-authored source",
            },
        )
        generator = family["authoring"]["generator"]
        generator_path = REPOSITORY_ROOT / generator["path"]
        self.assertEqual(
            generator["format"],
            "ror-cityworld-regional-infill-generator-v1",
        )
        self.assertEqual(generator["sha256"], sha256(generator_path))
        self.assertEqual(
            family["placement_target"],
            {
                "integration_status": "asset-ready-overlay-v6",
                "source_archive_sha256":
                    "ebeac2f0204f25ca1955f29ca1583b2afa4517a3a848feb1db203814acac2ef3",
            },
        )

    def test_family_has_the_exact_five_portable_variants(self) -> None:
        family = self.family()
        observed = {
            variant["asset_id"]: (
                variant["category"],
                variant["lod_triangles"],
            )
            for variant in family["variants"]
        }
        self.assertEqual(observed, EXPECTED_VARIANTS)
        for variant, manifest_path in self.variants():
            with self.subTest(asset=variant["asset_id"]):
                self.assertTrue(manifest_path.is_file())
                self.assertFalse(manifest_path.is_symlink())
                self.assertEqual(
                    manifest_path.relative_to(REPOSITORY_ROOT).as_posix(),
                    variant["manifest"],
                )

    def test_assets_and_checked_ogre_packages_pass_normal_and_optimized(
        self,
    ) -> None:
        for variant, manifest_path in self.variants():
            asset_id = variant["asset_id"]
            for optimized in (False, True):
                with self.subTest(asset=asset_id, optimized=optimized):
                    command = [sys.executable]
                    if optimized:
                        command.append("-O")
                    command.extend(
                        [
                            str(ASSET_VALIDATOR),
                            str(manifest_path),
                            "--repo-root",
                            str(REPOSITORY_ROOT),
                        ]
                    )
                    validation = subprocess.run(
                        command,
                        check=False,
                        capture_output=True,
                        text=True,
                    )
                    self.assertEqual(
                        validation.returncode,
                        0,
                        validation.stderr or validation.stdout,
                    )
                    validation_report = json.loads(validation.stdout)
                    self.assertTrue(validation_report["summary"]["valid"])
                    self.assertEqual(
                        validation_report["summary"]["runtime_lights"],
                        EXPECTED_RUNTIME_LIGHTS[asset_id],
                    )

                    command = [sys.executable]
                    if optimized:
                        command.append("-O")
                    command.extend(
                        [
                            str(ASSET_COMPILER),
                            str(manifest_path),
                            "--repo-root",
                            str(REPOSITORY_ROOT),
                            "--validate-checked",
                        ]
                    )
                    compilation = subprocess.run(
                        command,
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
                        {
                            output["role"]
                            for output in compile_report["outputs"]
                        },
                        EXPECTED_RUNTIME_ROLES,
                    )
                    self.assertEqual(
                        len(compile_report["runtime_lights"]),
                        EXPECTED_RUNTIME_LIGHTS[asset_id],
                    )

    def test_geometry_is_grounded_lod_budgeted_and_texture_free(self) -> None:
        for variant, manifest_path in self.variants():
            with self.subTest(asset=variant["asset_id"]):
                manifest = json.loads(
                    manifest_path.read_text(encoding="utf-8")
                )
                self.assertEqual(
                    manifest["geometry"]["asset_family"],
                    "rorng_city_regional_infill_family",
                )
                lods = manifest["geometry"]["lods"]
                triangles = [lod["triangles"] for lod in lods]
                self.assertEqual(triangles, variant["lod_triangles"])
                self.assertGreater(triangles[0], triangles[1])
                self.assertGreater(triangles[1], triangles[2])
                self.assertLessEqual(
                    triangles[0],
                    manifest["geometry"]["lod0_triangle_ceiling"],
                )
                self.assertEqual(
                    manifest["geometry"]["texcoord_policy"],
                    "canonical-zero-textureless-v1",
                )
                self.assertFalse(
                    any(
                        key.startswith("base_color_texture")
                        or key.startswith("normal_texture")
                        for material in manifest["materials"]
                        for key in material
                    )
                )
                self.assertTrue(
                    all(
                        lod["bounds_blender_z_up"]["min"][2] >= -0.1
                        for lod in lods
                    )
                )
                collision = manifest["collision"]["objects"][0]
                self.assertTrue(collision["topology"]["watertight"])
                self.assertTrue(collision["topology"]["outward_winding"])
                self.assertEqual(
                    collision["topology"]["intersecting_faces"],
                    0,
                )


if __name__ == "__main__":
    unittest.main()
