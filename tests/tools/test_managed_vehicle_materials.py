#!/usr/bin/env python3

import re
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
VEHICLE_MATERIALS = (
    REPOSITORY_ROOT
    / "resources"
    / "managed_materials"
    / "managed_mats_vehicles.material"
)
TEXTURE_MANAGER = (
    REPOSITORY_ROOT
    / "resources"
    / "managed_materials"
    / "texture"
    / "texture_manager.material"
)
MANAGED_SUBMESH = (
    REPOSITORY_ROOT
    / "resources"
    / "managed_materials"
    / "managed_submesh.material"
)
NICEMETAL = (
    REPOSITORY_ROOT
    / "resources"
    / "managed_materials"
    / "managed_mats_vehicles_nicemetal.material"
)
TRANSPARENT_NICEMETAL = (
    REPOSITORY_ROOT
    / "resources"
    / "managed_materials"
    / "managed_mats_vehicles_transparent_nicemetal.material"
)


def texture_unit_body(script: str, unit_name: str) -> str:
    match = re.search(rf"\btexture_unit\s+{re.escape(unit_name)}\s*\{{", script)
    if match is None:
        raise AssertionError(f"missing texture unit {unit_name}")

    block_start = match.end() - 1
    depth = 0
    for index in range(block_start, len(script)):
        if script[index] == "{":
            depth += 1
        elif script[index] == "}":
            depth -= 1
            if depth == 0:
                return script[block_start + 1 : index]

    raise AssertionError(f"unterminated texture unit {unit_name}")


class ManagedVehicleMaterialTests(unittest.TestCase):
    def test_managed_submesh_uses_no_removed_normalise_directive(self) -> None:
        script = MANAGED_SUBMESH.read_text(encoding="utf-8")

        self.assertNotIn("normalise_normals", script)
        self.assertEqual(script.count("material RoR/Submesh/"), 4)

    def test_nicemetal_dynamic_units_use_assignable_placeholders(self) -> None:
        for material_path in (NICEMETAL, TRANSPARENT_NICEMETAL):
            with self.subTest(material=material_path.name):
                script = material_path.read_text(encoding="utf-8")

                self.assertNotIn("texture_alias", script)
                self.assertEqual(script.count("texture unknown.dds"), 10)
                self.assertEqual(script.count("texture_unit Specular_Map"), 6)

    def test_dynamic_diffuse_units_keep_their_script_names(self) -> None:
        script = VEHICLE_MATERIALS.read_text(encoding="utf-8")

        self.assertNotIn("texture_alias", script)
        self.assertEqual(script.count("texture unknown.dds"), 8)
        for unit_name in ("Diffuse_Map", "Dmg_Diffuse_Map"):
            self.assertIn("texture unknown.dds", texture_unit_body(script, unit_name))

    def test_dynamic_specular_unit_keeps_its_script_name(self) -> None:
        script = TEXTURE_MANAGER.read_text(encoding="utf-8")
        specular_body = texture_unit_body(script, "SpecularMapping1_Tex")

        self.assertIn("texture unknown.dds", specular_body)
        self.assertNotIn("texture_alias", specular_body)


if __name__ == "__main__":
    unittest.main()
