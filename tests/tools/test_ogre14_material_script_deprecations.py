#!/usr/bin/env python3

import re
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
MATERIAL_ROOTS = (
    REPOSITORY_ROOT / "resources" / "managed_materials",
    REPOSITORY_ROOT / "resources" / "materials",
)
NICEMETAL_MATERIALS = (
    MATERIAL_ROOTS[0] / "managed_mats_vehicles_nicemetal.material",
    MATERIAL_ROOTS[0] / "managed_mats_vehicles_transparent_nicemetal.material",
)


def material_scripts() -> dict[Path, str]:
    return {
        path: path.read_text(encoding="utf-8")
        for root in MATERIAL_ROOTS
        for path in root.rglob("*.material")
    }


def directive_lines(script: str, directive: str) -> list[str]:
    pattern = re.compile(rf"^\s*{re.escape(directive)}\b")
    return [
        line.strip()
        for line in script.splitlines()
        if not line.lstrip().startswith("//") and pattern.match(line)
    ]


class Ogre14MaterialScriptDeprecationTests(unittest.TestCase):
    def test_bsp_only_software_culling_is_not_declared(self) -> None:
        declarations = {
            path.relative_to(REPOSITORY_ROOT): directive_lines(script, "cull_software")
            for path, script in material_scripts().items()
            if directive_lines(script, "cull_software")
        }

        self.assertEqual(declarations, {})

    def test_combined_cubemaps_use_dual_version_texture_syntax(self) -> None:
        scripts = material_scripts()
        combined_cubemaps = [
            line
            for script in scripts.values()
            for line in directive_lines(script, "cubic_texture")
            if line.endswith("combinedUVW")
        ]
        environment_cubemaps = sum(
            script.count("texture EnvironmentTexture cubic 0 PF_R8G8B8")
            for script in scripts.values()
        )
        reflection_cubemaps = sum(
            script.count("texture ReflectionCube cubic")
            for script in scripts.values()
        )

        self.assertEqual(combined_cubemaps, [])
        self.assertEqual(environment_cubemaps, 15)
        self.assertEqual(reflection_cubemaps, 2)

    def test_legacy_separate_uv_skyboxes_remain_explicit(self) -> None:
        script = (MATERIAL_ROOTS[1] / "ror.material").read_text(encoding="utf-8")
        separate_uv = [
            line
            for line in directive_lines(script, "cubic_texture")
            if line.endswith("separateUV")
        ]

        self.assertEqual(
            separate_uv,
            [
                "cubic_texture cloudy_noon.dds separateUV",
                "cubic_texture early_morning.dds separateUV",
                "cubic_texture ct.dds separateUV",
                "cubic_texture clouds.dds separateUV",
            ],
        )

    def test_dynamic_nicemetal_units_use_assignable_placeholders(self) -> None:
        for path in NICEMETAL_MATERIALS:
            with self.subTest(material=path.name):
                script = path.read_text(encoding="utf-8")

                self.assertEqual(directive_lines(script, "texture_alias"), [])
                self.assertEqual(script.count("texture unknown.dds"), 10)
                self.assertEqual(script.count("texture_unit Specular_Map"), 6)

    def test_legacy_texture_aliases_stay_inside_the_compatibility_allowlist(self) -> None:
        scripts = material_scripts()
        alias_counts = {
            path.relative_to(REPOSITORY_ROOT).as_posix(): len(
                directive_lines(script, "texture_alias")
            )
            for path, script in scripts.items()
            if directive_lines(script, "texture_alias")
        }
        set_alias_counts = {
            path.relative_to(REPOSITORY_ROOT).as_posix(): len(
                directive_lines(script, "set_texture_alias")
            )
            for path, script in scripts.items()
            if directive_lines(script, "set_texture_alias")
        }

        self.assertEqual(
            alias_counts,
            {
                "resources/managed_materials/managed_mats.material": 6,
                "resources/managed_materials/managed_submesh.material": 14,
                "resources/managed_materials/texture/texture_manager.material": 2,
            },
        )
        self.assertEqual(
            set_alias_counts,
            {
                "resources/materials/hangar.material": 3,
                "resources/materials/largedocks.material": 1,
                "resources/materials/marina.material": 1,
                "resources/materials/ror.material": 10,
                "resources/materials/train_rails.material": 6,
                "resources/materials/truckshop.material": 1,
            },
        )


if __name__ == "__main__":
    unittest.main()
