#!/usr/bin/env python3
"""Static contracts for the packaged renderer-suite process topology."""

from __future__ import annotations

from pathlib import Path
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]


class RendererSuitePackagingContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.root_cmake = (REPOSITORY_ROOT / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        cls.source_cmake = (
            REPOSITORY_ROOT / "source" / "main" / "CMakeLists.txt"
        ).read_text(encoding="utf-8")
        cls.package_facts = (
            REPOSITORY_ROOT
            / "cmake"
            / "RendererLauncherPackageConfig.cmake"
        ).read_text(encoding="utf-8")
        cls.icon_resource = (
            REPOSITORY_ROOT / "source" / "main" / "icon.rc"
        ).read_text(encoding="utf-8")
        cls.mac_stager = (
            REPOSITORY_ROOT / "cmake" / "macos" / "StageMacOSBundle.cmake"
        ).read_text(encoding="utf-8")
        cls.linux_stager = (
            REPOSITORY_ROOT / "cmake" / "linux" / "StageLinuxRuntime.cmake"
        ).read_text(encoding="utf-8")
        cls.runtime_audit = (
            REPOSITORY_ROOT / "tools" / "ogre14_runtime_audit.py"
        ).read_text(encoding="utf-8")
        cls.mac_workflow = (
            REPOSITORY_ROOT / ".github" / "workflows" / "macos-native.yml"
        ).read_text(encoding="utf-8")
        cls.native_workflow = (
            REPOSITORY_ROOT / ".github" / "workflows" / "ogre14-native.yml"
        ).read_text(encoding="utf-8")

    def test_ogre14_builds_default_to_the_public_renderer_suite(self) -> None:
        option_contract = (
            'set(_ror_renderer_public_launcher_default "${ROR_OGRE14}")\n'
            "option(\n"
            "    ROR_RENDERER_PUBLIC_LAUNCHER\n"
            "    \"Build and package the Ogre-Next-first public renderer "
            "chooser with the admitted renderer siblings\"\n"
            '    "${_ror_renderer_public_launcher_default}")\n'
            "unset(_ror_renderer_public_launcher_default)"
        )
        self.assertIn(option_contract, self.root_cmake)
        self.assertIn(
            "if (ROR_RENDERER_PUBLIC_LAUNCHER AND NOT ROR_OGRE14)",
            self.root_cmake,
        )
        for contract in (
            "if (APPLE AND ROR_RENDERER_PUBLIC_LAUNCHER)",
            "file(GLOB_RECURSE\n"
            "        _ror_stale_macos_install_scripts",
            '"${CMAKE_BINARY_DIR}/*/cmake_install.cmake"',
            '"${CMAKE_BINARY_DIR}/CPackConfig.cmake"',
            '"${CMAKE_BINARY_DIR}/CPackSourceConfig.cmake"',
            '"${CMAKE_BINARY_DIR}/CMakeFiles/package.util"',
            '"${CMAKE_BINARY_DIR}/CMakeFiles/package_source.util"',
            "unset(_ror_stale_macos_install_scripts)",
            "set(CMAKE_SKIP_INSTALL_RULES ON)",
        ):
            with self.subTest(mac_install_contract=contract):
                self.assertIn(contract, self.root_cmake)
        self.assertIn(
            "if (NOT (APPLE AND ROR_RENDERER_PUBLIC_LAUNCHER))\n"
            "    include(CPack)\n"
            "endif ()",
            self.source_cmake,
        )
        self.assertNotIn(
            "AND NOT ROR_RENDERER_PUBLIC_LAUNCHER",
            self.source_cmake,
        )

    def test_launcher_and_compatibility_child_are_staged_together(self) -> None:
        source = self.source_cmake
        for contract in (
            'OUTPUT_NAME "RoR-Ogre14"',
            'PDB_NAME "RoR-Ogre14"',
            'OUTPUT_NAME "RoR"',
            "set(_ror_application_install_targets ${BINNAME})",
            "list(PREPEND _ror_application_install_targets "
            "ror_renderer_launcher)",
            "TARGETS ${_ror_application_install_targets}",
            "set(_ror_macos_primary_target ror_renderer_launcher)",
            'set(_ror_macos_sibling_executables "$<TARGET_FILE:${BINNAME}>")',
            '"-DROR_SIBLING_EXECUTABLES=${_ror_macos_sibling_executables}"',
            'set(_ror_linux_installed_game_executable "RoR-Ogre14")',
        ):
            with self.subTest(contract=contract):
                self.assertIn(contract, source)

    def test_content_install_respects_the_content_build_option(self) -> None:
        guarded_install = (
            "if (ROR_CREATE_CONTENT_FOLDER)\n"
            "    install(DIRECTORY ${RUNTIME_OUTPUT_DIRECTORY}/content/ "
            "DESTINATION content COMPONENT \"Base_Content\")\n"
            "endif ()"
        )
        self.assertIn(guarded_install, self.source_cmake)

    def test_windows_public_launcher_owns_ror_icon_and_version_metadata(self) -> None:
        source = self.source_cmake
        launcher_start = source.index("add_executable(\n        ror_renderer_launcher")
        launcher_end = source.index(
            'message(STATUS\n        "Renderer suite packages',
            launcher_start,
        )
        launcher = source[launcher_start:launcher_end]
        for contract in (
            'target_sources(ror_renderer_launcher PRIVATE "icon.rc")',
            'PRIVATE "${CMAKE_BINARY_DIR}/source/version_info"',
            "add_dependencies(ror_renderer_launcher generate_version)",
        ):
            with self.subTest(contract=contract):
                self.assertIn(contract, launcher)
        for contract in (
            '#define ROR_RESOURCE_INTERNAL_NAME_STRING "RoR"',
            '#define ROR_RESOURCE_ORIGINAL_FILENAME_STRING "RoR.exe"',
            'VALUE "InternalName", ROR_RESOURCE_INTERNAL_NAME_STRING',
            'VALUE "OriginalFilename", '
            "ROR_RESOURCE_ORIGINAL_FILENAME_STRING",
        ):
            with self.subTest(resource_contract=contract):
                self.assertIn(contract, self.icon_resource)

    def test_windows_compatibility_child_has_exact_version_metadata(self) -> None:
        for contract in (
            'ROR_RESOURCE_INTERNAL_NAME_STRING=\\"RoR-Ogre14\\"',
            'ROR_RESOURCE_ORIGINAL_FILENAME_STRING=\\"RoR-Ogre14.exe\\"',
        ):
            with self.subTest(contract=contract):
                self.assertIn(contract, self.source_cmake)

    def test_package_facts_keep_ogrenext_fail_closed(self) -> None:
        facts = self.package_facts
        for contract in (
            'set(ROR_RENDERER_LAUNCHER_OGRE_NEXT_CHILD_PRESENT "false")',
            'set(ROR_RENDERER_LAUNCHER_OGRE_NEXT_PRODUCTION_READY "false")',
            'set(ROR_RENDERER_LAUNCHER_OGRE_NEXT_PSSM_ADMITTED "false")',
            'set(ROR_RENDERER_LAUNCHER_NATIVE_SHADOW_BACKEND "NONE")',
        ):
            with self.subTest(contract=contract):
                self.assertIn(contract, facts)
        self.assertNotIn("CACHE", facts)

    def test_platform_stagers_own_the_exact_compatibility_child(self) -> None:
        mac = self.mac_stager
        for contract in (
            "ROR_SIBLING_EXECUTABLES",
            "Duplicate sibling executable basename",
            "Sibling executable collides with the public executable",
            'EXECUTABLES "${_ror_executable}" ${_ror_sibling_executables}',
            "set(_ror_bundle_executables",
            "_ror_destination_sibling_executables",
            "_ror_bundle_executables)",
        ):
            with self.subTest(platform="macos", contract=contract):
                self.assertIn(contract, mac)

        linux = self.linux_stager
        for contract in (
            "ROR_LINUX_INSTALLED_EXECUTABLE_NAME",
            'set(ROR_LINUX_INSTALLED_EXECUTABLE_NAME "RoR")',
            '"${ROR_LINUX_INSTALL_ROOT}/'
            '${ROR_LINUX_INSTALLED_EXECUTABLE_NAME}"',
            "Unsafe installed Linux game executable name",
        ):
            with self.subTest(platform="linux", contract=contract):
                self.assertIn(contract, linux)

    def test_ci_and_runtime_audit_follow_public_to_child_handoff(self) -> None:
        self.assertIn(
            'compatibility_executable="$app/Contents/MacOS/RoR-Ogre14"',
            self.mac_workflow,
        )
        self.assertIn(
            "RoR-Ogre14.pdb",
            self.native_workflow,
        )
        self.assertIn(
            "LocalDumps\\RoR-Ogre14.exe",
            self.native_workflow,
        )
        self.assertIn(
            '"--executable",\n'
            '            "$env:GITHUB_WORKSPACE/artifacts/'
            'runtime-${{ matrix.platform }}/RoR.exe"',
            self.native_workflow,
        )
        self.assertIn(
            'compatibility_executable = root / "RoR-Ogre14"',
            self.runtime_audit,
        )
        self.assertIn(
            'public_executable = root / "RoR"',
            self.runtime_audit,
        )


if __name__ == "__main__":
    unittest.main()
