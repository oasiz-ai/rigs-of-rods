#!/usr/bin/env python3
"""Contract tests for the retired Cg runtime-plugin route."""

from __future__ import annotations

from pathlib import Path
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]


class CgRuntimeRouteRetiredContractTests(unittest.TestCase):
    def read_repository_file(self, relative_path: str) -> str:
        return (REPOSITORY_ROOT / relative_path).read_text(encoding="utf-8")

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


if __name__ == "__main__":
    unittest.main()
