#!/usr/bin/env python3
"""Contract tests for the retired Cg runtime-plugin route."""

from __future__ import annotations

from pathlib import Path
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]


class CgRuntimeRouteRetiredContractTests(unittest.TestCase):
    def read_repository_file(self, relative_path: str) -> str:
        return (REPOSITORY_ROOT / relative_path).read_text(encoding="utf-8")

    def read_repository_bytes(self, relative_path: str) -> bytes:
        return (REPOSITORY_ROOT / relative_path).read_bytes()

    def test_generated_plugin_configs_have_no_cg_slot(self) -> None:
        for relative_path in (
            "source/main/plugins.cfg.in",
            "source/main/plugins_d.cfg.in",
        ):
            with self.subTest(path=relative_path):
                template = self.read_repository_file(relative_path)
                self.assertNotIn("Plugin_CgProgramManager", template)
                self.assertNotIn("CFG_COMMENT_PLUGIN_CG", template)

    def test_cmake_has_no_cg_template_plumbing(self) -> None:
        main_cmake = self.read_repository_file("source/main/CMakeLists.txt")
        platform_cmake = self.read_repository_file("cmake/Ogre14Platform.cmake")
        self.assertNotIn("CFG_COMMENT_PLUGIN_CG", main_cmake)
        self.assertNotIn("_plugin_comments_CG", main_cmake)
        self.assertNotIn("${output_prefix}_CG", platform_cmake)

    def test_linux_dependency_copy_does_not_inspect_cg_plugin(self) -> None:
        copy_tool = self.read_repository_file("tools/CI/copy_libs.cmake")
        self.assertNotIn("Plugin_CgProgramManager", copy_tool)

    def test_package_policy_disables_and_audits_cg(self) -> None:
        recipe = self.read_repository_file(
            "cmake/conan/recipes/ogre3d/conanfile.py"
        )
        native_package_audit = self.read_repository_file(
            "tests/tools/assert_ogre_native_package.py"
        )
        runtime_audit = self.read_repository_file(
            "tools/ogre14_runtime_audit.py"
        )
        macos_stager = self.read_repository_file(
            "cmake/macos/StageMacOSBundle.cmake"
        )

        self.assertIn('tc.variables["OGRE_BUILD_PLUGIN_CG"] = "OFF"', recipe)
        self.assertIn(
            '"cgprogrammanager" in path.name.lower()', native_package_audit
        )
        self.assertIn('"cgprogrammanager" in lowered', runtime_audit)
        self.assertIn("Cg plugins are forbidden", macos_stager)

    def test_legacy_terrain_shader_generator_keeps_modern_routes(self) -> None:
        terrain_header = self.read_repository_file(
            "source/main/terrain/OgreTerrainPSSMMaterialGenerator.h"
        )
        terrain_source = self.read_repository_file(
            "source/main/terrain/OgreTerrainPSSMMaterialGenerator.cpp"
        )
        terrain_bytes = (terrain_header + terrain_source).lower()

        self.assertNotIn(
            '#error "TerrainPSSMMaterialGenerator requires OGRE 14 or newer"',
            terrain_header,
        )
        self.assertIn("#if OGRE_VERSION_MAJOR >= 14", terrain_header)
        self.assertIn("#if OGRE_VERSION_MAJOR < 14", terrain_source)
        self.assertIn("#else", terrain_header)

        ogre14_facade, legacy_header = terrain_header.split("#else", 1)
        self.assertIn(
            "class TerrainPSSMMaterialGenerator : public "
            "TerrainMaterialGeneratorA",
            ogre14_facade,
        )
        self.assertIn(
            "TerrainMaterialGeneratorA::SM2Profile* profile",
            ogre14_facade,
        )
        self.assertIn(
            "class ShaderHelperHLSLSource : public ShaderHelper",
            legacy_header,
        )
        self.assertIn(
            "class ShaderHelperHLSL : public ShaderHelperHLSLSource",
            legacy_header,
        )
        self.assertIn("class ShaderHelperGLSL : public ShaderHelper", legacy_header)
        self.assertNotIn("class ShaderHelperGLSLES", legacy_header)
        self.assertNotIn("outStream) {}", legacy_header)

        self.assertIn(
            "OGRECave/ogre v1.11.6",
            terrain_source,
        )
        self.assertIn(
            "4cb5f7caf8e56ebc502327f8faa4e9c966ac9e41305599cb7e57bf9a9cbb9b17",
            terrain_source,
        )
        self.assertIn(
            'String lang = mgr.isLanguageSupported("glsles") ? "glsles" : "glsl";',
            terrain_source,
        )
        for generated_glsl_feature in (
            'outStream << "#version "',
            'outStream << "precision highp float;',
            '"    gl_Position = viewProjMatrix * worldPos;',
            '"    gl_FragColor.rgb += ambient.rgb * diffuse',
            "texture2D(globalNormal, uv)",
            "generateFpDynamicShadowsHelpers",
            'params->setNamedConstant("globalNormal", numSamplers++)',
            'params->setNamedConstant("compositeMap", numSamplers++)',
        ):
            with self.subTest(glsl_feature=generated_glsl_feature):
                self.assertIn(generated_glsl_feature, terrain_source)

        for retired_route in (
            "shaderhelpercg",
            'islanguagesupported("cg")',
            '"cg"',
            'setparameter("profiles"',
            "arbvp1",
            "arbfp1",
            "fp30",
            "fp40",
        ):
            with self.subTest(route=retired_route):
                self.assertNotIn(retired_route, terrain_bytes)

        for retained_route in (
            "OGRE_NEW ShaderHelperHLSL()",
            "OGRE_NEW ShaderHelperGLSL()",
            '"hlsl", GPT_VERTEX_PROGRAM',
            '"hlsl", GPT_FRAGMENT_PROGRAM',
            "lang, GPT_VERTEX_PROGRAM",
            "lang, GPT_FRAGMENT_PROGRAM",
        ):
            with self.subTest(route=retained_route):
                self.assertIn(retained_route, terrain_source)

    def test_workflows_and_snap_do_not_package_cg(self) -> None:
        for relative_path in (
            ".github/workflows/build-game.yml",
            ".github/workflows/linux-native.yml",
            "snapcraft.yaml",
        ):
            package_definition = self.read_repository_file(relative_path).lower()
            with self.subTest(path=relative_path):
                self.assertNotIn("nvidia-cg", package_definition)
                self.assertNotIn("libcggl", package_definition)

    def test_hydrax_has_no_dynamic_cg_shader_mode(self) -> None:
        hydrax_sources = [
            REPOSITORY_ROOT / "source/main/gfx/HydraxWater.cpp",
            *(REPOSITORY_ROOT / "source/main/gfx/hydrax").rglob("*.cpp"),
            *(REPOSITORY_ROOT / "source/main/gfx/hydrax").rglob("*.h"),
        ]
        source_bytes = b"\n".join(
            path.read_bytes() for path in sorted(hydrax_sources)
        )

        self.assertNotIn(b"SM_CG", source_bytes)
        self.assertNotIn(b'"cg"', source_bytes)

    def test_hydrax_shader_language_is_renderer_owned(self) -> None:
        hydrax_water = self.read_repository_bytes(
            "source/main/gfx/HydraxWater.cpp"
        )
        config_manager = self.read_repository_bytes(
            "source/main/gfx/hydrax/CfgFileManager.cpp"
        )

        self.assertIn(
            b"setShaderMode(Hydrax::MaterialManager::SM_HLSL)",
            hydrax_water,
        )
        self.assertIn(
            b"setShaderMode(Hydrax::MaterialManager::SM_GLSL)",
            hydrax_water,
        )
        self.assertNotIn(
            b"static_cast<Hydrax::MaterialManager::ShaderMode>",
            hydrax_water,
        )
        self.assertNotIn(b'"ShaderMode"', config_manager)
        self.assertNotIn(b"1=CG", config_manager)

    def test_hydrax_language_selection_is_explicit_and_fail_closed(self) -> None:
        material_header = self.read_repository_bytes(
            "source/main/gfx/hydrax/MaterialManager.h"
        )
        material_source = self.read_repository_bytes(
            "source/main/gfx/hydrax/MaterialManager.cpp"
        )

        self.assertIn(b"SM_HLSL = 0", material_header)
        self.assertIn(b"SM_GLSL = 2", material_header)
        self.assertIn(b"Ogre::String ShaderModeStr;", material_source)
        self.assertIn(b'ShaderModeStr = "hlsl";', material_source)
        self.assertIn(b'ShaderModeStr = "glsl";', material_source)
        self.assertIn(b"default:", material_source)
        self.assertIn(b"Unsupported shader mode.", material_source)
        self.assertNotIn(b"ShaderModesStr[", material_source)


if __name__ == "__main__":
    unittest.main()
