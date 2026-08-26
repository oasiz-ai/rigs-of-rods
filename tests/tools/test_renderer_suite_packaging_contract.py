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
        cls.conanfile = (REPOSITORY_ROOT / "conanfile.py").read_text(
            encoding="utf-8"
        )
        cls.building_guide = (REPOSITORY_ROOT / "BUILDING.md").read_text(
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
        cls.package_header = (
            REPOSITORY_ROOT
            / "source"
            / "main"
            / "system"
            / "RendererLauncherPackageConfig.h.in"
        ).read_text(encoding="utf-8")
        cls.public_launcher = (
            REPOSITORY_ROOT
            / "source"
            / "main"
            / "system"
            / "RendererPublicLauncher.cpp"
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
        cls.linux_entrypoint = (
            REPOSITORY_ROOT / "tools" / "linux" / "RunRoR-ogre14"
        ).read_text(encoding="utf-8")
        cls.linux_storefront = (
            REPOSITORY_ROOT / "tools" / "linux" / ".itch.toml"
        ).read_text(encoding="utf-8")
        cls.windows_storefront = (
            REPOSITORY_ROOT / "tools" / "windows" / ".itch.toml"
        ).read_text(encoding="utf-8")

    def test_supported_builds_default_to_the_ogre_next_first_suite(self) -> None:
        self.assertIn(
            'option(\n    ROR_OGRE14\n'
            '    "Build the cross-platform OGRE 14 simulation and compatibility host"\n'
            '    ON)',
            self.root_cmake,
        )
        self.assertIn('"ogre14": True,', self.conanfile)
        for conan_contract in (
            'tc.variables["ROR_OGRE14"] = build_renderer_suite',
            'tc.variables["ROR_RENDERER_PUBLIC_LAUNCHER"] = build_renderer_suite',
            'tc.variables["ROR_OGRE_NEXT_PRODUCTION_PACKAGE"] = (',
        ):
            with self.subTest(conan_contract=conan_contract):
                self.assertIn(conan_contract, self.conanfile)
        self.assertIn(
            '"The OgreNext-first product suite supports only "',
            self.conanfile,
        )
        for option_contract in (
            "option(\n"
            "    ROR_RENDERER_PUBLIC_LAUNCHER\n"
            "    \"Build and package the Ogre-Next-first public renderer "
            "chooser with the admitted renderer siblings\"\n"
            "    ON)",
            "option(\n"
            "    ROR_OGRE_NEXT_PRODUCTION_PACKAGE\n"
            "    \"Build and package the verified real OgreNext renderer "
            "child\"\n"
            "    ON)",
        ):
            with self.subTest(option_contract=option_contract):
                self.assertIn(option_contract, self.root_cmake)
        self.assertNotIn("_ror_renderer_public_launcher_default", self.root_cmake)
        self.assertNotIn("_ror_ogre_next_product_package_default", self.root_cmake)
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

    def test_building_guide_describes_the_default_product_truthfully(self) -> None:
        guide = self.building_guide
        for contract in (
            "The supported product build is OgreNext-first",
            "`RoR`: the public launcher, which requests OgreNext by default",
            "`RoR-OgreNext`: the isolated presentation renderer",
            "`RoR-Ogre14`: the simulation/game host and bounded compatibility "
            "fallback",
            "immutable package facts keep the",
            "normal launch fail-closed on `RoR-Ogre14`",
            "`ROR_OGRE14` | `ON`",
            "`ROR_RENDERER_PUBLIC_LAUNCHER` | `ON`",
            "`ROR_OGRE_NEXT_PRODUCTION_PACKAGE` | `ON`",
            "`ROR_OGRE_NEXT_DEMO_ADMISSION` | `OFF`",
            "`-DROR_OGRE_NEXT_DEMO_ADMISSION=ON`",
            "does not admit native ray",
        ):
            with self.subTest(contract=contract):
                self.assertIn(contract, guide)

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
            'string(REPLACE ";" "\\\\;"',
            '"-DROR_SIBLING_EXECUTABLES='
            '${_ror_macos_sibling_executables_argument}"',
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

    def test_demo_admission_is_explicit_and_build_time_only(self) -> None:
        for contract in (
            "option(\n"
            "    ROR_OGRE_NEXT_DEMO_ADMISSION\n"
            "    \"Admit the verified OgreNext child and PSSM for an "
            "explicit demo build\"\n"
            "    OFF)",
            "if (ROR_OGRE_NEXT_DEMO_ADMISSION AND\n"
            "        NOT ROR_OGRE_NEXT_PRODUCTION_PACKAGE)",
            '"ROR_OGRE_NEXT_DEMO_ADMISSION requires "',
            '"ROR_OGRE_NEXT_PRODUCTION_PACKAGE=ON"',
        ):
            with self.subTest(root_contract=contract):
                self.assertIn(contract, self.root_cmake)

        self.assertIn(
            '"${ROR_OGRE_NEXT_DEMO_ADMISSION}")', self.source_cmake
        )
        facts = self.package_facts
        for contract in (
            "admit_ogre_next_demo",
            'NOT "${admit_ogre_next_demo}" STREQUAL "ON"',
            'NOT "${admit_ogre_next_demo}" STREQUAL "OFF"',
            'if ("${admit_ogre_next_demo}" STREQUAL "ON")',
            'set(ROR_RENDERER_LAUNCHER_OGRE_NEXT_CHILD_PRESENT "true")',
            'set(ROR_RENDERER_LAUNCHER_OGRE_NEXT_PRODUCTION_READY "true")',
            'set(ROR_RENDERER_LAUNCHER_OGRE_NEXT_PSSM_ADMITTED "true")',
            'set(ROR_RENDERER_LAUNCHER_NATIVE_SHADOW_BACKEND "NONE")',
        ):
            with self.subTest(fact_contract=contract):
                self.assertIn(contract, facts)
        self.assertNotIn("getenv", facts.lower())

        for contract in (
            "if (APPLE AND ROR_OGRE_NEXT_DEMO_ADMISSION)",
            "PRIVATE ROR_OGRE_NEXT_MACOS_DEMO_AUTOSTART=1",
            "if (ROR_OGRE_NEXT_DEMO_ADMISSION)",
            '"-DROR_BUNDLE_DISPLAY_NAME=Rigs of Rods OgreNext Demo"',
            '"-DROR_BUNDLE_IDENTIFIER=org.rigsofrods.RoR.OgreNextDemo"',
        ):
            with self.subTest(autostart_build_contract=contract):
                self.assertIn(contract, self.source_cmake)
        for contract in (
            "#if defined(ROR_OGRE_NEXT_MACOS_DEMO_AUTOSTART)",
            "if (argc == 1)",
            'kCheckCache[] = "-checkcache"',
            'kMap[] = "CityWorld.terrn2"',
            'kTruck[] = "AlexisSaber.truck"',
            'kEnter[] = "-enter"',
            "RendererFrontendPreference::OGRE_NEXT_REQUIRE",
            "DirectionalShadowPreference::PSSM",
        ):
            with self.subTest(autostart_source_contract=contract):
                self.assertIn(contract, self.public_launcher)

    def test_generated_package_metadata_requests_ogrenext_by_default(self) -> None:
        for contract in (
            'set(ROR_RENDERER_LAUNCHER_DEFAULT_FRONTEND "OGRE_NEXT_PREFER")',
            'set(ROR_RENDERER_LAUNCHER_DEFAULT_DIRECTIONAL_SHADOWS "PSSM")',
        ):
            with self.subTest(config_contract=contract):
                self.assertIn(contract, self.package_facts)
        for contract in (
            "kDefaultFrontendPreference",
            "RendererFrontendPreference::@ROR_RENDERER_LAUNCHER_DEFAULT_FRONTEND@",
            "kDefaultDirectionalShadowPreference",
            "@ROR_RENDERER_LAUNCHER_DEFAULT_DIRECTIONAL_SHADOWS@",
            "the public package must request OgreNext by default",
        ):
            with self.subTest(header_contract=contract):
                self.assertIn(contract, self.package_header)
        self.assertIn(
            "result.intent = RendererPublicLauncherPackageDefaultIntent();",
            self.public_launcher,
        )

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

    def test_split_product_storefront_and_bundle_entrypoints_use_public_launcher(
        self,
    ) -> None:
        self.assertIn(
            'set(_ror_macos_bundle_executable_name "RoR")',
            self.source_cmake,
        )
        self.assertIn(
            '"-DROR_BUNDLE_EXECUTABLE_NAME='
            '${_ror_macos_bundle_executable_name}"',
            self.source_cmake,
        )
        self.assertIn(
            'if(NOT ROR_BUNDLE_EXECUTABLE_NAME STREQUAL "RoR")',
            self.mac_stager,
        )
        self.assertIn(
            '"RoR-Ogre14" _ror_ogre14_sibling_index', self.mac_stager
        )
        self.assertIn('exec "${ror_launcher_dir}/RoR" "$@"', self.linux_entrypoint)
        self.assertIn('path = "RunRoR"', self.linux_storefront)
        self.assertIn('path = "RoR.exe"', self.windows_storefront)
        self.assertNotIn('path = "RoR-Ogre14.exe"', self.windows_storefront)

    def test_ci_and_runtime_audit_follow_public_to_child_handoff(self) -> None:
        for workflow in (self.mac_workflow, self.native_workflow):
            with self.subTest(workflow_default="OgreNext-first"):
                self.assertNotIn("-DROR_OGRE14=", workflow)
                self.assertNotIn("-DROR_RENDERER_PUBLIC_LAUNCHER=", workflow)
                self.assertNotIn(
                    "-DROR_OGRE_NEXT_PRODUCTION_PACKAGE=", workflow
                )
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
