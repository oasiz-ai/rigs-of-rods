#!/usr/bin/env python3
"""Hostile contracts for the pinned Cg-free Ogre 1.11 developer lane."""

from __future__ import annotations

import copy
import importlib.util
import json
from pathlib import Path, PureWindowsPath
import re
import subprocess
import tempfile
import textwrap
import unittest
from unittest import mock

import assert_ogre11_app_graph as graph_assertion


ROOT = Path(__file__).resolve().parents[2]
ROOT_RECIPE = ROOT / "conanfile.py"
MAIN_CMAKE = ROOT / "CMakeLists.txt"
MAIN_SOURCE_CMAKE = ROOT / "source/main/CMakeLists.txt"
PLATFORM_CMAKE = ROOT / "cmake/OgreLegacyPlatform.cmake"
LEGACY_RECIPE = ROOT / "cmake/conan/recipes/ogre3d-legacy/conanfile.py"
LEGACY_AUDIT = (
    ROOT / "cmake/conan/recipes/ogre3d-legacy/cg_package_audit.py"
)
LEGACY_CONANDATA = ROOT / "cmake/conan/recipes/ogre3d-legacy/conandata.yml"
LEGACY_CAELUM_RECIPE = (
    ROOT / "cmake/conan/recipes/ogre3d-caelum-legacy/conanfile.py"
)
LEGACY_CAELUM_AUDIT = (
    ROOT
    / "cmake/conan/recipes/ogre3d-caelum-legacy/caelum_package_audit.py"
)
LEGACY_CAELUM_CONANDATA = (
    ROOT / "cmake/conan/recipes/ogre3d-caelum-legacy/conandata.yml"
)
LEGACY_CAELUM_CPP17_PATCH = (
    ROOT
    / "cmake/conan/recipes/ogre3d-caelum-legacy/patches/0.6.3.1"
    / "ModernizeRemovedCpp17LibraryFeatures.patch"
)
LEGACY_PAGED_GEOMETRY_RECIPE = (
    ROOT
    / "cmake/conan/recipes/ogre3d-pagedgeometry-legacy/conanfile.py"
)
LEGACY_PAGED_GEOMETRY_AUDIT = (
    ROOT
    / "cmake/conan/recipes/ogre3d-pagedgeometry-legacy"
    / "pagedgeometry_package_audit.py"
)
LEGACY_PAGED_GEOMETRY_CONANDATA = (
    ROOT
    / "cmake/conan/recipes/ogre3d-pagedgeometry-legacy/conandata.yml"
)
LEGACY_PAGED_GEOMETRY_CPP17_PATCH = (
    ROOT
    / "cmake/conan/recipes/ogre3d-pagedgeometry-legacy/patches/1.2.0"
    / "ModernizeRemovedCpp17LibraryFeatures.patch"
)
OGRE14_NATIVE_WORKFLOW = ROOT / ".github/workflows/ogre14-native.yml"
MACOS_NATIVE_WORKFLOW = ROOT / ".github/workflows/macos-native.yml"
LEGACY_MACOS_SDK_PATCH = (
    ROOT
    / "cmake/conan/recipes/ogre3d-legacy/patches/1.11.6.1"
    / "FixMacOSSDKRoot.patch"
)
LEGACY_DIRECTX11_PATCH = (
    ROOT
    / "cmake/conan/recipes/ogre3d-legacy/patches/1.11.6.1"
    / "UseWindowsSDKDirectX11.patch"
)
LEGACY_ARM_DETECTION_PATCH = (
    ROOT
    / "cmake/conan/recipes/ogre3d-legacy/patches/1.11.6.1"
    / "DetectArmBeforeAppleX86.patch"
)
LEGACY_FUNCTIONAL_ADAPTOR_PATCH = (
    ROOT
    / "cmake/conan/recipes/ogre3d-legacy/patches/1.11.6.1"
    / "ModernizeRemovedFunctionalAdaptors.patch"
)
LEGACY_LOCKS = tuple(
    ROOT / "cmake/conan/locks" / filename
    for filename in (
        "ror-ogre11-linux-x86_64-release.lock",
        "ror-ogre11-macos-arm64-release.lock",
        "ror-ogre11-windows-x86_64-release.lock",
    )
)
EXPECTED_LEGACY_BUILD_REQUIREMENTS = {
    "ror-ogre11-linux-x86_64-release.lock": {
        "autoconf/2.71#51077f068e61700d65bb05541ea1e4b0",
        "automake/1.16.5#b91b7c384c3deaa9d535be02da14d04f",
        "cmake/3.31.12#173a926abc2b77f03c826b6fd6539426",
        "cmake/4.4.2#8a0d360635c870b1d5c675489ee25074",
        "gnu-config/cci.20210814#466e9d4d7779e1c142443f7ea44b4284",
        "libtool/2.4.7#14e7739cc128bc1623d2ed318008e47e",
        "m4/1.4.19#1727f439cf74e83826ec96d0b4904eee",
        "meson/1.10.2#9d2d10681fe7fe61c788c58626c89b25",
        "ninja/1.13.2#c8c5dc2a52ed6e4e42a66d75b4717ceb",
        "pkgconf/2.5.1#93c2051284cba1279494a43a4fcfeae2",
    },
    "ror-ogre11-macos-arm64-release.lock": {
        "autoconf/2.71#51077f068e61700d65bb05541ea1e4b0",
        "automake/1.16.5#b91b7c384c3deaa9d535be02da14d04f",
        "cmake/3.31.12#173a926abc2b77f03c826b6fd6539426",
        "cmake/4.4.2#8a0d360635c870b1d5c675489ee25074",
        "gnu-config/cci.20210814#466e9d4d7779e1c142443f7ea44b4284",
        "libtool/2.4.7#14e7739cc128bc1623d2ed318008e47e",
        "m4/1.4.19#1727f439cf74e83826ec96d0b4904eee",
        "meson/1.10.2#9d2d10681fe7fe61c788c58626c89b25",
        "ninja/1.13.2#c8c5dc2a52ed6e4e42a66d75b4717ceb",
        "pkgconf/2.5.1#93c2051284cba1279494a43a4fcfeae2",
    },
    "ror-ogre11-windows-x86_64-release.lock": {
        "cmake/3.31.12#173a926abc2b77f03c826b6fd6539426",
        "cmake/4.4.2#8a0d360635c870b1d5c675489ee25074",
        "meson/1.10.2#9d2d10681fe7fe61c788c58626c89b25",
        "nasm/2.16.01#31e26f2ee3c4346ecd347911bd126904",
        "ninja/1.13.2#c8c5dc2a52ed6e4e42a66d75b4717ceb",
        "pkgconf/2.5.1#93c2051284cba1279494a43a4fcfeae2",
        "strawberryperl/5.32.1.1#8d114504d172cfea8ea1662d09b6333e",
    },
}
DEFAULT_LOCKS = tuple(
    sorted((ROOT / "cmake/conan/locks").glob("ror-ogre14-*.lock"))
)


def load_legacy_audit_module():
    spec = importlib.util.spec_from_file_location(
        "ror_ogre3d_legacy_package_audit_contract", LEGACY_AUDIT
    )
    if spec is None or spec.loader is None:
        raise AssertionError("cannot load the repository legacy package audit")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_caelum_audit_module():
    spec = importlib.util.spec_from_file_location(
        "ror_caelum_legacy_package_audit_contract", LEGACY_CAELUM_AUDIT
    )
    if spec is None or spec.loader is None:
        raise AssertionError("cannot load the repository Caelum package audit")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_paged_geometry_audit_module():
    spec = importlib.util.spec_from_file_location(
        "ror_paged_geometry_legacy_package_audit_contract",
        LEGACY_PAGED_GEOMETRY_AUDIT,
    )
    if spec is None or spec.loader is None:
        raise AssertionError(
            "cannot load the repository PagedGeometry package audit"
        )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run_cmake_script(source: str) -> subprocess.CompletedProcess[str]:
    with tempfile.TemporaryDirectory(prefix="ror-ogre11-cmake-contract-") as tmp:
        script = Path(tmp) / "contract.cmake"
        script.write_text(textwrap.dedent(source), encoding="utf-8")
        return subprocess.run(
            ["cmake", "-P", str(script)],
            check=False,
            capture_output=True,
            text=True,
        )


def recipe_revision(source: str) -> str:
    match = re.search(
        r'^OGRE_LEGACY_RECIPE_REVISION\s*=\s*"([0-9a-f]{32})"$',
        source,
        re.MULTILINE,
    )
    if match is None:
        raise AssertionError("root recipe has no exact legacy revision")
    return match.group(1)


def assert_cg_free_lock(payload: dict) -> None:
    if payload.get("version") != "0.5":
        raise AssertionError("unsupported Conan lockfile version")
    references = []
    for key in ("requires", "build_requires"):
        entries = payload.get(key)
        if not isinstance(entries, list) or not all(
            isinstance(entry, str) for entry in entries
        ):
            raise AssertionError(f"lockfile {key} is not a string list")
        references.extend(entries)
    forbidden = (
        "cg-toolkit",
        "nvidia-cg",
        "cgprogram",
        "directx-sdk",
    )
    lowered = [reference.lower() for reference in references]
    if any(
        fragment in reference
        for fragment in forbidden
        for reference in lowered
    ):
        raise AssertionError(
            "lockfile contains a forbidden Cg or DirectX 9 dependency"
        )


def graph_fixture(platform: str = "linux-x86_64") -> dict:
    settings = dict(graph_assertion.EXPECTED_PLATFORM_SETTINGS[platform])
    fixture = {
        "graph": {
            "nodes": {
                "0": {
                    "ref": "Rigs of Rods/None",
                    "options": {"ogre14": "False"},
                    "settings": dict(settings),
                    "dependencies": {
                        "1": {
                            "ref": graph_assertion.OGRE_BASE_REFERENCE,
                            "require": graph_assertion.OGRE_BASE_REFERENCE,
                            "force": True,
                        }
                    },
                },
                "1": {
                    "ref": graph_assertion.OGRE_REFERENCE,
                    "context": "host",
                    "recipe": "Cache",
                    "options": dict(graph_assertion.EXPECTED_OGRE_OPTIONS),
                    "settings": dict(settings),
                    "dependencies": {},
                },
                "2": {
                    "ref": graph_assertion.CAELUM_REFERENCE,
                    "context": "host",
                    "recipe": "Cache",
                    "dependencies": {
                        "1": {
                            "ref": graph_assertion.OGRE_BASE_REFERENCE,
                            "require": graph_assertion.OGRE_BASE_REFERENCE,
                            "force": False,
                        }
                    },
                },
                "3": {
                    "ref": graph_assertion.MYGUI_REFERENCE,
                    "context": "host",
                    "recipe": "Cache",
                },
                "4": {
                    "ref": graph_assertion.PAGED_GEOMETRY_REFERENCE,
                    "context": "host",
                    "recipe": "Cache",
                    "dependencies": {
                        "1": {
                            "ref": graph_assertion.OGRE_BASE_REFERENCE,
                            "require": graph_assertion.OGRE_BASE_REFERENCE,
                            "force": False,
                        }
                    },
                },
                "5": {
                    "ref": graph_assertion.OIS_REFERENCE,
                    "context": "host",
                    "recipe": "Cache",
                },
            },
            "overrides": {
                graph_assertion.OGRE_REFERENCE: [
                    None,
                    graph_assertion.OGRE_REFERENCE,
                ]
            },
            "replaced_requires": {},
            "resolved_ranges": {},
        }
    }
    nodes = fixture["graph"]["nodes"]
    for index, reference in enumerate(
        sorted(graph_assertion.EXPECTED_BUILD_REFERENCES[platform]),
        start=10,
    ):
        nodes[str(index)] = {
            "ref": reference,
            "context": "build",
            "recipe": "Cache",
            "settings": {
                "os": settings["os"],
                "arch": settings["arch"],
            },
            "dependencies": {},
        }
    existing_host_references = {
        node["ref"]
        for node in nodes.values()
        if node.get("context") == "host"
    }
    for index, reference in enumerate(
        sorted(
            graph_assertion.EXPECTED_HOST_REFERENCES[platform]
            - existing_host_references
        ),
        start=100,
    ):
        nodes[str(index)] = {
            "ref": reference,
            "context": "host",
            "recipe": "Cache",
            "dependencies": {},
        }
    return fixture


class LegacyOgreCgFreeRecipeContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.root_recipe = ROOT_RECIPE.read_text(encoding="utf-8")
        cls.main_cmake = MAIN_CMAKE.read_text(encoding="utf-8")
        cls.main_source_cmake = MAIN_SOURCE_CMAKE.read_text(encoding="utf-8")
        cls.platform_cmake = PLATFORM_CMAKE.read_text(encoding="utf-8")
        cls.legacy_recipe = LEGACY_RECIPE.read_text(encoding="utf-8")
        cls.legacy_audit = LEGACY_AUDIT.read_text(encoding="utf-8")
        cls.legacy_conandata = LEGACY_CONANDATA.read_text(encoding="utf-8")
        cls.legacy_caelum_recipe = LEGACY_CAELUM_RECIPE.read_text(
            encoding="utf-8"
        )
        cls.legacy_caelum_audit = LEGACY_CAELUM_AUDIT.read_text(
            encoding="utf-8"
        )
        cls.legacy_caelum_conandata = LEGACY_CAELUM_CONANDATA.read_text(
            encoding="utf-8"
        )
        cls.legacy_caelum_cpp17_patch = (
            LEGACY_CAELUM_CPP17_PATCH.read_text(encoding="utf-8")
        )
        cls.legacy_paged_geometry_recipe = (
            LEGACY_PAGED_GEOMETRY_RECIPE.read_text(encoding="utf-8")
        )
        cls.legacy_paged_geometry_audit = (
            LEGACY_PAGED_GEOMETRY_AUDIT.read_text(encoding="utf-8")
        )
        cls.legacy_paged_geometry_conandata = (
            LEGACY_PAGED_GEOMETRY_CONANDATA.read_text(encoding="utf-8")
        )
        cls.legacy_paged_geometry_cpp17_patch = (
            LEGACY_PAGED_GEOMETRY_CPP17_PATCH.read_text(encoding="utf-8")
        )
        cls.ogre14_native_workflow = OGRE14_NATIVE_WORKFLOW.read_text(
            encoding="utf-8"
        )
        cls.macos_native_workflow = MACOS_NATIVE_WORKFLOW.read_text(
            encoding="utf-8"
        )
        cls.legacy_macos_sdk_patch = LEGACY_MACOS_SDK_PATCH.read_text(
            encoding="utf-8"
        )
        cls.legacy_directx11_patch = LEGACY_DIRECTX11_PATCH.read_text(
            encoding="utf-8"
        )
        cls.legacy_arm_detection_patch = (
            LEGACY_ARM_DETECTION_PATCH.read_text(encoding="utf-8")
        )
        cls.legacy_functional_adaptor_patch = (
            LEGACY_FUNCTIONAL_ADAPTOR_PATCH.read_text(encoding="utf-8")
        )
        cls.legacy_audit_module = load_legacy_audit_module()
        cls.legacy_caelum_audit_module = load_caelum_audit_module()
        cls.legacy_paged_geometry_audit_module = (
            load_paged_geometry_audit_module()
        )
        cls.revision = recipe_revision(cls.root_recipe)

    def test_root_uses_only_the_exact_repository_legacy_revision(self) -> None:
        exact_reference = (
            '"ogre3d/1.11.6.1@anotherfoxguy/stable"\n'
            "                f\"#{OGRE_LEGACY_RECIPE_REVISION}\""
        )
        self.assertIn(exact_reference, self.root_recipe)
        self.assertNotIn(
            '"ogre3d/1.11.6.1@anotherfoxguy/stable",',
            self.root_recipe,
        )
        self.assertIn(
            'ANGELSCRIPT_RECIPE_REVISION = '
            '"8c9b8d736d0176a6e69c64a4501eeeb1"',
            self.root_recipe,
        )
        self.assertIn(
            'f"angelscript/2.38.0#{ANGELSCRIPT_RECIPE_REVISION}"',
            self.root_recipe,
        )
        self.assertNotIn("angelscript/2.35.1", self.root_recipe)

    def test_legacy_recipe_pins_source_and_disables_cg_at_configure_and_package(
        self,
    ) -> None:
        self.assertIn(
            "31c84051ffe9a3710c553cfa27ebda7b176c9dfe3b4d2a113d4f02caf48ecd5b",
            self.legacy_conandata,
        )
        self.assertIn(
            'tc.variables["OGRE_BUILD_PLUGIN_CG"] = "OFF"',
            self.legacy_recipe,
        )
        self.assertIn(
            'tc.variables["OGRE_BUILD_LIBS_AS_FRAMEWORKS"] = "OFF"',
            self.legacy_recipe,
        )
        self.assertEqual(
            self.legacy_recipe.count("OGRE_BUILD_LIBS_AS_FRAMEWORKS"), 1
        )
        self.assertLess(
            self.legacy_recipe.index("OGRE_BUILD_LIBS_AS_FRAMEWORKS"),
            self.legacy_recipe.index(
                'if str(self.settings.os) == "Windows":',
                self.legacy_recipe.index("def generate(self):"),
            ),
        )
        self.assertNotIn("cg-toolkit", self.legacy_recipe.lower())
        self.assertNotIn("directx-sdk", self.legacy_recipe.lower())
        self.assertIn(
            'tc.variables["OGRE_BUILD_RENDERSYSTEM_D3D11"]',
            self.legacy_recipe,
        )
        self.assertIn(
            'tc.variables["OGRE_BUILD_RENDERSYSTEM_D3D9"] = "OFF"',
            self.legacy_recipe,
        )
        self.assertIn(
            'tc.cache_variables["CMAKE_DISABLE_FIND_PACKAGE_DirectX"] = True',
            self.legacy_recipe,
        )
        self.assertIn(
            'tc.cache_variables["CMAKE_REQUIRE_FIND_PACKAGE_DirectX11"] = True',
            self.legacy_recipe,
        )
        self.assertIn(
            "find_windows_d3d11_cache_contract_errors", self.legacy_recipe
        )
        self.assertNotIn(
            '"find_package(DirectX9)"',
            self.legacy_recipe,
        )
        self.assertIn(
            "UseWindowsSDKDirectX11.patch",
            self.legacy_conandata,
        )
        self.assertIn(
            "DetectArmBeforeAppleX86.patch",
            self.legacy_conandata,
        )
        for retired_fragment in (
            "DXSDK_DIR",
            "DIRECTX_HOME",
            "DirectX11_D3DX11_LIBRARY",
            "DirectX11_DXERR_LIBRARY",
        ):
            with self.subTest(retired_fragment=retired_fragment):
                matching_lines = [
                    line
                    for line in self.legacy_directx11_patch.splitlines()
                    if retired_fragment in line
                ]
                self.assertTrue(matching_lines)
                self.assertTrue(
                    all(line.startswith("-") for line in matching_lines)
                )
        self.assertIn("+            NO_DEFAULT_PATH)", self.legacy_directx11_patch)
        self.assertIn(
            "+                \"${_DirectX11_kit_root}/Lib/"
            "${_DirectX11_include_parent_name}/um/x64\")",
            self.legacy_directx11_patch,
        )
        added_directx_lines = [
            line[1:]
            for line in self.legacy_directx11_patch.splitlines()
            if line.startswith("+") and not line.startswith("+++")
        ]
        self.assertNotIn(
            '                "${_DirectX11_kit_root}/Lib/"',
            added_directx_lines,
        )
        self.assertNotIn(
            '                "${_DirectX11_include_parent_name}/um/x64")',
            added_directx_lines,
        )
        self.assertIn(
            '        if(NOT IS_DIRECTORY "${_DirectX11_library_dir}")',
            added_directx_lines,
        )
        for missing_library in ("d3d11.lib", "dxgi.lib", "dxguid.lib"):
            with self.subTest(missing_library=missing_library):
                self.assertIn(
                    "                \"DirectX11 Windows Kit is missing "
                    f"{missing_library} in: \"",
                    added_directx_lines,
                )
        for import_library in (
            "DirectX11_D3D11_LIBRARY",
            "DirectX11_DXGI_LIBRARY",
            "DirectX11_DXGUID_LIBRARY",
        ):
            self.assertIn(
                "+            " + import_library,
                self.legacy_directx11_patch,
            )
        self.assertIn(
            '"OGRE_BUILD_RENDERSYSTEM_D3D11:BOOL=ON"',
            self.legacy_audit,
        )
        self.assertIn(
            '"OGRE_BUILD_RENDERSYSTEM_D3D9:INTERNAL=OFF"',
            self.legacy_audit,
        )
        self.assertIn("/windows kits/", self.legacy_audit)
        self.assertIn(
            "is_trusted_windows_kits_include_path", self.legacy_recipe
        )
        self.assertIn(
            "is_trusted_windows_kits_library_path", self.legacy_recipe
        )
        self.assertIn('"d3d11.h"', self.legacy_recipe)
        self.assertIn("find_forbidden_cg_package_entries", self.legacy_recipe)
        self.assertIn('exports = "cg_package_audit.py"', self.legacy_recipe)
        self.assertIn("os.walk(", self.legacy_audit)
        self.assertIn("FORBIDDEN_CG_SUFFIXES", self.legacy_audit)
        self.assertIn("active_cg_route_lines", self.legacy_audit)
        self.assertIn('str(self.settings.build_type) != "Release"', self.legacy_recipe)
        self.assertIn("FixMacOSSDKRoot.patch", self.legacy_conandata)
        self.assertIn(
            "ModernizeRemovedFunctionalAdaptors.patch",
            self.legacy_conandata,
        )
        for removed_adaptor in (
            "std::binary_function",
            "std::bind2nd",
        ):
            with self.subTest(removed_adaptor=removed_adaptor):
                matching_lines = [
                    line
                    for line in self.legacy_functional_adaptor_patch.splitlines()
                    if removed_adaptor in line
                ]
                self.assertTrue(matching_lines)
                self.assertTrue(
                    all(line.startswith("-") for line in matching_lines)
                )
        self.assertIn(
            "+            [&cleanName](const FileInfo& fileInfo) {",
            self.legacy_functional_adaptor_patch,
        )
        self.assertIn(
            "+        bool operator() (const MeshLodUsage& mesh1, "
            "const MeshLodUsage& mesh2) const",
            self.legacy_functional_adaptor_patch,
        )
        for patch_contract in (
            "COMMAND xcrun --sdk",
            "--show-sdk-path",
            'if(NOT IS_DIRECTORY "${_OGRE_MACOS_SDK}")',
            '+  set(CMAKE_OSX_SYSROOT "${_OGRE_MACOS_SDK}" CACHE PATH',
            '+  set(XCODE_ATTRIBUTE_SDKROOT "${_OGRE_MACOS_SDK}")',
        ):
            with self.subTest(patch_contract=patch_contract):
                self.assertIn(
                    patch_contract, self.legacy_macos_sdk_patch
                )

    def test_legacy_arm_detection_precedes_the_apple_x86_fallback(self) -> None:
        added_lines = [
            line[1:]
            for line in self.legacy_arm_detection_patch.splitlines()
            if line.startswith("+") and not line.startswith("+++")
        ]
        removed_lines = [
            line[1:]
            for line in self.legacy_arm_detection_patch.splitlines()
            if line.startswith("-") and not line.startswith("---")
        ]
        arm_branch = (
            "#elif defined(__arm__) || defined(_M_ARM) || "
            "defined(__arm64__) || defined(__aarch64__)"
        )
        apple_x86_branch = (
            "#elif OGRE_PLATFORM == OGRE_PLATFORM_APPLE && "
            "(defined(__i386__) || defined(__x86_64__))"
        )

        self.assertIn(arm_branch, added_lines)
        self.assertIn(apple_x86_branch, added_lines)
        self.assertLess(
            added_lines.index(arm_branch),
            added_lines.index(apple_x86_branch),
        )
        self.assertIn("#elif OGRE_PLATFORM == OGRE_PLATFORM_APPLE", removed_lines)
        self.assertIn(arm_branch, removed_lines)
        self.assertNotIn(
            "#elif OGRE_PLATFORM == OGRE_PLATFORM_APPLE",
            added_lines,
        )
        self.assertNotIn("OgreOptimisedUtilSSE", self.legacy_arm_detection_patch)

    def test_package_audit_covers_plugins_debug_configs_and_frameworks(
        self,
    ) -> None:
        audit = self.legacy_audit_module.find_forbidden_cg_package_entries
        hostile_entries = (
            ("bin/Plugin_CgProgramManager.so", None),
            ("bin/PluginCgProgramManager.so", None),
            ("lib/OGRE/plugins/Plugin_CgProgramManager_d.dll", None),
            ("Library/Frameworks/Cg.framework/Cg", None),
            ("share/OGRE/shaders/common.CgInC", None),
            (
                "share/OGRE/plugins_d.cfg",
                "PluginFolder=.\nPlugin=Plugin_CgProgramManager\n",
            ),
            (
                "share/OGRE/legacy.material",
                "fragment_program Legacy cg\n",
            ),
            (
                "share/OGRE/legacy.program",
                "source Legacy.cginc\n",
            ),
            (
                "share/OGRE/legacy.compositor",
                "vertex_program Legacy asm\n",
            ),
        )
        for relative_path, contents in hostile_entries:
            with self.subTest(relative_path=relative_path):
                with tempfile.TemporaryDirectory(
                    prefix="ror-ogre11-cg-audit-"
                ) as tmp:
                    artifact = Path(tmp) / relative_path
                    artifact.parent.mkdir(parents=True, exist_ok=True)
                    artifact.write_text(contents or "hostile", encoding="utf-8")
                    findings = audit(tmp)
                    self.assertTrue(findings)
                    self.assertTrue(
                        any(
                            token in finding.lower()
                            for finding in findings
                            for token in ("cg", "asm")
                        )
                    )

        with tempfile.TemporaryDirectory(
            prefix="ror-ogre11-cg-symlink-audit-"
        ) as tmp:
            link = Path(tmp) / "lib" / "renderer-plugin.so"
            link.parent.mkdir(parents=True)
            link.symlink_to("../Frameworks/Cg.framework/Cg")
            findings = audit(tmp)
            self.assertTrue(any("cg.framework" in item.lower() for item in findings))

        with tempfile.TemporaryDirectory(
            prefix="ror-ogre11-clean-audit-"
        ) as tmp:
            config = Path(tmp) / "bin" / "plugins_d.cfg"
            config.parent.mkdir(parents=True)
            config.write_text(
                "# Plugin=Plugin_CgProgramManager\n"
                "/* source Historical.cginc */\n"
                "// vertex_program Historical asm\n"
                "Plugin=RenderSystem_GL\n",
                encoding="utf-8",
            )
            self.assertEqual(audit(tmp), [])

    def test_package_audit_canonicalizes_windows_diagnostic_paths(
        self,
    ) -> None:
        relative_path = r"lib\OGRE\cmake\OGREConfig.cmake"
        with mock.patch.object(
            self.legacy_audit_module.os.path,
            "relpath",
            return_value=relative_path,
        ), mock.patch.object(
            self.legacy_audit_module,
            "Path",
            PureWindowsPath,
        ):
            observed = (
                self.legacy_audit_module.package_relative_posix_path(
                    r"C:\package\lib\OGRE\cmake\OGREConfig.cmake",
                    r"C:\package",
                )
            )
        self.assertEqual(observed, "lib/OGRE/cmake/OGREConfig.cmake")

    def test_package_audit_rejects_d3d9_and_requires_d3d11_policy(
        self,
    ) -> None:
        audit = (
            self.legacy_audit_module
            .find_forbidden_legacy_directx_package_entries
        )
        hostile_entries = (
            ("bin/RenderSystem_Direct3D9.dll", None),
            ("lib/d3dx9.lib", None),
            ("bin/D3DX11_43.dll", None),
            ("lib/DxErr.lib", None),
            (
                "share/cmake/OGREConfig.cmake",
                "set(DIRECTX_ROOT C:/private/Microsoft DirectX SDK)\n",
            ),
            (
                "share/cmake/OGRETargets.cmake",
                "set_property(TARGET OGRE APPEND PROPERTY "
                "INTERFACE_LINK_LIBRARIES C:/poison/d3d11.lib)\n",
            ),
            (
                "share/OGRE/plugins.cfg",
                "Plugin=RenderSystem_Direct3D9\n",
            ),
        )
        for relative_path, contents in hostile_entries:
            with self.subTest(relative_path=relative_path):
                with tempfile.TemporaryDirectory(
                    prefix="ror-ogre11-d3d9-audit-"
                ) as tmp:
                    artifact = Path(tmp) / relative_path
                    artifact.parent.mkdir(parents=True, exist_ok=True)
                    artifact.write_text(contents or "hostile", encoding="utf-8")
                    findings = audit(tmp)
                    self.assertTrue(findings)

        with tempfile.TemporaryDirectory(
            prefix="ror-ogre11-d3d11-audit-"
        ) as tmp:
            config = Path(tmp) / "bin" / "plugins.cfg"
            config.parent.mkdir(parents=True)
            config.write_text(
                "# Plugin=RenderSystem_Direct3D9\n"
                "Plugin=RenderSystem_Direct3D11\n",
                encoding="utf-8",
            )
            self.assertEqual(audit(tmp), [])

        for literal_false in ("OFF", "FALSE"):
            with self.subTest(literal_false=literal_false):
                with tempfile.TemporaryDirectory(
                    prefix="ror-ogre11-inactive-d3d9-audit-"
                ) as tmp:
                    config = Path(tmp) / "lib/OGRE/cmake/OGREConfig.cmake"
                    config.parent.mkdir(parents=True)
                    config.write_text(
                        f"if({literal_false})\n"
                        "    ogre_declare_plugin(RenderSystem Direct3D9)\n"
                        "endif()\n",
                        encoding="utf-8",
                    )
                    self.assertEqual(audit(tmp), [])

        hostile_cmake_stanzas = (
            "ogre_declare_plugin(RenderSystem Direct3D9)\n",
            "if(ON)\n"
            "    ogre_declare_plugin(RenderSystem Direct3D9)\n"
            "endif()\n",
            "if(TRUE)\n"
            "    ogre_declare_plugin(RenderSystem Direct3D9)\n"
            "endif()\n",
            "if(NOT OFF)\n"
            "    ogre_declare_plugin(RenderSystem Direct3D9)\n"
            "endif()\n",
            "if(OFF OR ON)\n"
            "    ogre_declare_plugin(RenderSystem Direct3D9)\n"
            "endif()\n",
            "if(OGRE_BUILD_RENDERSYSTEM_D3D9)\n"
            "    ogre_declare_plugin(RenderSystem Direct3D9)\n"
            "endif()\n",
            "if(false)\n"
            "    ogre_declare_plugin(RenderSystem Direct3D9)\n"
            "endif()\n",
        )
        for hostile_stanza in hostile_cmake_stanzas:
            with self.subTest(hostile_stanza=hostile_stanza):
                with tempfile.TemporaryDirectory(
                    prefix="ror-ogre11-active-d3d9-audit-"
                ) as tmp:
                    config = Path(tmp) / "lib/OGRE/cmake/OGREConfig.cmake"
                    config.parent.mkdir(parents=True)
                    config.write_text(hostile_stanza, encoding="utf-8")
                    self.assertTrue(audit(tmp))

        with tempfile.TemporaryDirectory(
            prefix="ror-ogre11-mixed-d3d9-audit-"
        ) as tmp:
            config = Path(tmp) / "lib/OGRE/cmake/OGREConfig.cmake"
            config.parent.mkdir(parents=True)
            config.write_text(
                "if(OFF)\n"
                "    ogre_declare_plugin(RenderSystem Direct3D9)\n"
                "endif()\n"
                "ogre_declare_plugin(RenderSystem Direct3D9)\n",
                encoding="utf-8",
            )
            self.assertEqual(
                audit(tmp),
                [
                    "lib/OGRE/cmake/OGREConfig.cmake:4:"
                    "ogre_declare_plugin(RenderSystem Direct3D9)"
                ],
            )

        trusted_path = (
            self.legacy_audit_module.is_trusted_windows_kits_include_path
        )
        self.assertTrue(
            trusted_path(
                "C:/Program Files (x86)/Windows Kits/10/"
                "Include/10.0.26100.0/um"
            )
        )
        self.assertTrue(
            trusted_path(
                "D:/Program Files/Windows Kits/8.1/Include/um"
            )
        )
        for hostile_path in (
            "C:/private/Windows Kits/10/Include/10.0.26100.0/um",
            "C:/Program Files (x86)/Fake/Windows Kits/10/Include/x/um",
            "C:/Program Files (x86)/Microsoft DirectX SDK/Include",
            "C:/Program Files (x86)/Windows Kits/10/Include/x/shared",
        ):
            with self.subTest(hostile_path=hostile_path):
                self.assertFalse(trusted_path(hostile_path))

        trusted_library_path = (
            self.legacy_audit_module.is_trusted_windows_kits_library_path
        )
        self.assertTrue(
            trusted_library_path(
                "C:/Program Files (x86)/Windows Kits/10/Lib/"
                "10.0.26100.0/um/x64/d3d11.lib",
                "d3d11.lib",
            )
        )
        self.assertTrue(
            trusted_library_path(
                "D:/Program Files/Windows Kits/8.1/Lib/"
                "winv6.3/um/x64/dxgi.lib",
                "dxgi.lib",
            )
        )
        for hostile_library_path in (
            "C:/private/Windows Kits/10/Lib/10.0.26100.0/um/x64/d3d11.lib",
            "C:/Program Files (x86)/Windows Kits/10/Lib/"
            "10.0.26100.0/um/x64/d3dx11.lib",
            "C:/poison/d3d11.lib",
            "C:/Program Files (x86)/Microsoft DirectX SDK/Lib/x64/d3d11.lib",
        ):
            with self.subTest(hostile_library_path=hostile_library_path):
                self.assertFalse(
                    trusted_library_path(hostile_library_path, "d3d11.lib")
                )

        with tempfile.TemporaryDirectory(
            prefix="ror-ogre11-clean-d3d11-audit-"
        ) as tmp:
            config = Path(tmp) / "share" / "cmake" / "OGRETargets.cmake"
            config.parent.mkdir(parents=True)
            config.write_text(
                "set_property(TARGET OGRE APPEND PROPERTY "
                "INTERFACE_LINK_LIBRARIES "
                "C:/Program Files (x86)/Windows Kits/10/Lib/"
                "10.0.26100.0/um/x64/d3d11.lib)\n",
                encoding="utf-8",
            )
            self.assertEqual(audit(tmp), [])

        self.assertNotIn("${DirectX_INCLUDE_DIR}", self.main_cmake)
        self.assertIn(
            'set(CFG_COMMENT_RENDERSYSTEM_D3D9 "# ")',
            self.main_source_cmake,
        )
        self.assertIn(
            'if (NOT ROR_OGRE14)\n'
            '        set(CFG_COMMENT_RENDERSYSTEM_D3D11 "")',
            self.main_source_cmake,
        )

    def test_windows_d3d11_cache_contract_is_exact_and_hostile(self) -> None:
        validate = (
            self.legacy_audit_module.find_windows_d3d11_cache_contract_errors
        )
        valid_cache = (
            "OGRE_BUILD_RENDERSYSTEM_D3D11:BOOL=ON\n"
            "OGRE_BUILD_RENDERSYSTEM_D3D9:INTERNAL=OFF\n"
        )
        self.assertEqual(validate(valid_cache), [])

        hostile_caches = {
            "missing D3D9": "OGRE_BUILD_RENDERSYSTEM_D3D11:BOOL=ON\n",
            "duplicate D3D9": valid_cache
            + "OGRE_BUILD_RENDERSYSTEM_D3D9:INTERNAL=OFF\n",
            "D3D11 disabled": valid_cache.replace(
                "D3D11:BOOL=ON", "D3D11:BOOL=OFF"
            ),
            "D3D9 enabled": valid_cache.replace(
                "D3D9:INTERNAL=OFF", "D3D9:INTERNAL=ON"
            ),
            "D3D9 type substitution": valid_cache.replace(
                "D3D9:INTERNAL=OFF", "D3D9:STRING=OFF"
            ),
            "D3D9 untyped substitution": valid_cache.replace(
                "D3D9:INTERNAL=OFF", "D3D9=OFF"
            ),
            "valid plus untyped duplicate": valid_cache
            + "OGRE_BUILD_RENDERSYSTEM_D3D9=OFF\n",
            "similar key substitution": (
                "OGRE_BUILD_RENDERSYSTEM_D3D11:BOOL=ON\n"
                "OGRE_BUILD_RENDERSYSTEM_D3D9_SHADOW:INTERNAL=OFF\n"
            ),
        }
        for name, hostile_cache in hostile_caches.items():
            with self.subTest(name=name):
                self.assertTrue(validate(hostile_cache))

    def test_caelum_recipe_is_exactly_bound_to_the_cg_free_ogre_host(
        self,
    ) -> None:
        self.assertIn(
            '"#44875cdee59d651783849e1924b04ea6"',
            self.legacy_caelum_recipe,
        )
        self.assertNotIn("[~14]", self.legacy_caelum_recipe)
        self.assertIn(
            'str(self.settings.build_type) != "Release"',
            self.legacy_caelum_recipe,
        )
        self.assertIn(
            "b4b4b6fe2997c3da4537ada2ab69032647e65773886e57c9803fc90aac2dd158",
            self.legacy_caelum_conandata,
        )
        self.assertIn('"LICENSE*"', self.legacy_caelum_recipe)
        self.assertIn(
            'exports = "caelum_package_audit.py"',
            self.legacy_caelum_recipe,
        )
        self.assertIn(
            "export_conandata_patches(self)",
            self.legacy_caelum_recipe,
        )
        self.assertIn(
            "apply_conandata_patches(self)",
            self.legacy_caelum_recipe,
        )
        self.assertIn(
            'patch_file: "patches/0.6.3.1/'
            'ModernizeRemovedCpp17LibraryFeatures.patch"',
            self.legacy_caelum_conandata,
        )
        self.assertIn(
            "find_forbidden_caelum_package_entries",
            self.legacy_caelum_recipe,
        )
        self.assertIn("RESOURCE_SCRIPT_SUFFIXES", self.legacy_caelum_audit)
        self.assertIn(
            'self.info.requires["ogre3d"].full_package_mode()',
            self.legacy_caelum_recipe,
        )
        self.assertNotIn("full_recipe_mode()", self.legacy_caelum_recipe)

    def test_caelum_cpp17_patch_preserves_ownership_and_callback_semantics(
        self,
    ) -> None:
        removed_lines = "\n".join(
            line[1:]
            for line in self.legacy_caelum_cpp17_patch.splitlines()
            if line.startswith("-") and not line.startswith("---")
        )
        added_lines = "\n".join(
            line[1:]
            for line in self.legacy_caelum_cpp17_patch.splitlines()
            if line.startswith("+") and not line.startswith("+++")
        )
        active_added_lines = "\n".join(
            line
            for line in added_lines.splitlines()
            if not line.lstrip().startswith("*")
        )

        self.assertEqual(removed_lines.count("std::auto_ptr"), 28)
        self.assertEqual(active_added_lines.count("std::unique_ptr"), 25)
        self.assertNotIn("std::auto_ptr", added_lines)
        self.assertEqual(removed_lines.count("std::bind1st"), 3)
        self.assertEqual(removed_lines.count("std::mem_fun"), 3)
        self.assertNotIn("std::bind1st", added_lines)
        self.assertNotIn("std::mem_fun", added_lines)

        for owner in (
            "mSkyDome",
            "mSun",
            "mMoon",
            "mImageStarfield",
            "mPointStarfield",
            "mGroundFog",
            "mCloudSystem",
            "mDepthComposer",
        ):
            self.assertIn(f"{owner}.get () !=", added_lines)

        self.assertIn("[oldptr] (Ogre::Viewport* viewport)", added_lines)
        self.assertIn("[newptr] (Ogre::Viewport* viewport)", added_lines)
        self.assertIn(
            "[depthComposer] (Ogre::Viewport* viewport)",
            added_lines,
        )
        self.assertEqual(
            self.legacy_caelum_cpp17_patch.count("return inst.release();"),
            2,
        )
        self.assertEqual(
            self.legacy_caelum_cpp17_patch.count("return layer.release();"),
            1,
        )
        self.assertNotIn("mCloudCoverLookup.reset(0)", added_lines)
        self.assertNotIn("mCloudCoverLookup.reset (0)", added_lines)

    def test_caelum_package_audit_rejects_cg_assets_and_active_routes(
        self,
    ) -> None:
        audit = (
            self.legacy_caelum_audit_module
            .find_forbidden_caelum_package_entries
        )
        hostile_entries = (
            ("share/caelum/Legacy.cg", "hostile\n"),
            ("share/caelum/Common.CgInC", "hostile\n"),
            (
                "share/caelum/Legacy.material",
                "fragment_program Caelum/Legacy cg\n",
            ),
            (
                "share/caelum/Legacy.program",
                "source CaelumLegacy.cg\n",
            ),
            (
                "share/caelum/plugins.cfg",
                "Plugin=Plugin_CgProgramManager\n",
            ),
            (
                "share/caelum/Legacy.compositor",
                "vertex_program Caelum/Legacy asm\n",
            ),
            ("lib/libCg.dylib", "hostile\n"),
        )
        for relative_path, contents in hostile_entries:
            with self.subTest(relative_path=relative_path):
                with tempfile.TemporaryDirectory(
                    prefix="ror-caelum-cg-audit-"
                ) as tmp:
                    artifact = Path(tmp) / relative_path
                    artifact.parent.mkdir(parents=True, exist_ok=True)
                    artifact.write_text(contents, encoding="utf-8")
                    self.assertTrue(audit(tmp))

        with tempfile.TemporaryDirectory(
            prefix="ror-caelum-clean-cg-audit-"
        ) as tmp:
            header = Path(tmp) / "include/Caelum/LayeredCloud.h"
            header.parent.mkdir(parents=True)
            header.write_text(
                "// Historical source name: CaelumLayeredCloud.cg\n",
                encoding="utf-8",
            )
            material = Path(tmp) / "share/caelum/Modern.material"
            material.parent.mkdir(parents=True)
            material.write_text(
                "/* source CaelumLegacy.cginc */\n"
                "// fragment_program Caelum/Legacy cg\n"
                "fragment_program Caelum/Modern glsl\n",
                encoding="utf-8",
            )
            self.assertEqual(audit(tmp), [])

    def test_paged_geometry_recipe_owns_exact_cpp17_and_cg_free_bytes(
        self,
    ) -> None:
        self.assertIn(
            '"#44875cdee59d651783849e1924b04ea6"',
            self.legacy_paged_geometry_recipe,
        )
        self.assertNotIn("[~1.11]", self.legacy_paged_geometry_recipe)
        self.assertIn(
            "513043eecf1aaf6f8b2335c953b0664128aa492007fe3d93068e5f4be436093a",
            self.legacy_paged_geometry_conandata,
        )
        self.assertIn(
            'patch_file: "patches/1.2.0/'
            'ModernizeRemovedCpp17LibraryFeatures.patch"',
            self.legacy_paged_geometry_conandata,
        )
        for contract in (
            'exports = "pagedgeometry_package_audit.py"',
            "export_conandata_patches(self)",
            "apply_conandata_patches(self)",
            'toolchain.variables["CMAKE_CXX_STANDARD"] = "17"',
            'toolchain.variables["CMAKE_CXX_STANDARD_REQUIRED"] = True',
            'toolchain.variables["CMAKE_CXX_EXTENSIONS"] = False',
            "find_removed_cpp17_source_entries",
            "find_forbidden_cg_package_entries",
            'str(self.settings.build_type) != "Release"',
            'self.info.requires["ogre3d"].full_package_mode()',
        ):
            with self.subTest(contract=contract):
                self.assertIn(contract, self.legacy_paged_geometry_recipe)

    def test_paged_geometry_cpp17_patch_preserves_loader_ownership(
        self,
    ) -> None:
        removed_lines = "\n".join(
            line[1:]
            for line in self.legacy_paged_geometry_cpp17_patch.splitlines()
            if line.startswith("-") and not line.startswith("---")
        )
        added_lines = "\n".join(
            line[1:]
            for line in self.legacy_paged_geometry_cpp17_patch.splitlines()
            if line.startswith("+") and not line.startswith("+++")
        )

        self.assertEqual(removed_lines.count("std::auto_ptr"), 2)
        self.assertEqual(added_lines.count("std::unique_ptr"), 1)
        self.assertNotIn("std::auto_ptr", added_lines)
        self.assertEqual(
            added_lines.count(
                "loader.reset(new ImpostorTextureResourceLoader(*this));"
            ),
            1,
        )
        self.assertIn("loader(nullptr)", added_lines)
        self.assertNotIn("loader.release", added_lines)
        self.assertIn(
            "#if OGRE_PLATFORM == OGRE_PLATFORM_LINUX", removed_lines
        )

    def test_paged_geometry_audits_reject_removed_cpp17_and_cg_routes(
        self,
    ) -> None:
        removed_audit = (
            self.legacy_paged_geometry_audit_module
            .find_removed_cpp17_source_entries
        )
        cg_audit = (
            self.legacy_paged_geometry_audit_module
            .find_forbidden_cg_package_entries
        )

        with tempfile.TemporaryDirectory(
            prefix="ror-paged-removed-cpp17-audit-"
        ) as tmp:
            root = Path(tmp)
            (root / "include").mkdir()
            (root / "source").mkdir()
            (root / "include/ImpostorPage.h").write_text(
                "std::auto_ptr<int> owner;\n"
                "std :: auto_ptr<int> spaced_owner;\n",
                encoding="utf-8",
            )
            (root / "source/Forest.cpp").write_text(
                "struct Sort : std::binary_function<int, int, bool> {};\n",
                encoding="utf-8",
            )
            findings = removed_audit(tmp)
            self.assertEqual(len(findings), 3)
            self.assertTrue(any("std::auto_ptr" in item for item in findings))
            self.assertTrue(any("std :: auto_ptr" in item for item in findings))
            self.assertTrue(
                any("std::binary_function" in item for item in findings)
            )

        with tempfile.TemporaryDirectory(
            prefix="ror-paged-clean-cpp17-audit-"
        ) as tmp:
            root = Path(tmp)
            (root / "include").mkdir()
            (root / "source").mkdir()
            (root / "include/ImpostorPage.h").write_text(
                "std::unique_ptr<int> owner;\n", encoding="utf-8"
            )
            (root / "source/Forest.cpp").write_text(
                "auto predicate = [](int value) { return value > 0; };\n",
                encoding="utf-8",
            )
            self.assertEqual(removed_audit(tmp), [])
            self.assertEqual(
                removed_audit(tmp, relative_trees=("include",)), []
            )

        hostile_cg_entries = (
            ("share/PagedGeometry/grass.cg", "hostile\n"),
            ("share/PagedGeometry/common.cginc", "hostile\n"),
            (
                "share/PagedGeometry/grass.material",
                "vertex_program grassVP cg\n",
            ),
            (
                "share/PagedGeometry/include.program",
                "source Common.cginc\n",
            ),
            (
                "share/PagedGeometry/legacy.compositor",
                "vertex_program grassVP asm\n",
            ),
            ("lib/libCg.dylib", "hostile\n"),
        )
        for relative_path, contents in hostile_cg_entries:
            with self.subTest(relative_path=relative_path):
                with tempfile.TemporaryDirectory(
                    prefix="ror-paged-cg-audit-"
                ) as tmp:
                    artifact = Path(tmp) / relative_path
                    artifact.parent.mkdir(parents=True, exist_ok=True)
                    artifact.write_text(contents, encoding="utf-8")
                    self.assertTrue(cg_audit(tmp))

        with tempfile.TemporaryDirectory(
            prefix="ror-paged-clean-cg-audit-"
        ) as tmp:
            material = Path(tmp) / "share/PagedGeometry/grass.material"
            material.parent.mkdir(parents=True)
            material.write_text(
                "/* source Historical.cginc */\n"
                "// vertex_program grassVP cg\n"
                "vertex_program grassVP glsl\n",
                encoding="utf-8",
            )
            self.assertEqual(cg_audit(tmp), [])

    def test_native_workflows_validate_real_legacy_graphs(self) -> None:
        for workflow in (
            self.ogre14_native_workflow,
            self.macos_native_workflow,
        ):
            with self.subTest(workflow=workflow[:40]):
                self.assertIn("conan graph info .", workflow)
                self.assertIn("assert_ogre11_app_graph.py", workflow)
                self.assertIn("--build='*'", workflow)
                self.assertIn("-ogre11-conan-graph.json", workflow)
                self.assertIn("-ogre11-conan-graph-receipt.json", workflow)

    def test_cmake_exports_the_repository_recipe_and_forces_the_opt_out_graph(
        self,
    ) -> None:
        self.assertIn("else ()\n    # The Ogre 1.11 developer opt-out", self.main_cmake)
        self.assertIn("cmake/conan/recipes/ogre3d-legacy", self.main_cmake)
        self.assertIn(
            "cmake/conan/recipes/ogre3d-caelum-legacy",
            self.main_cmake,
        )
        self.assertIn('"--version=1.11.6.1"', self.main_cmake)

        legacy_lane_start = self.main_cmake.index(
            "else ()\n    # The Ogre 1.11 developer opt-out"
        )
        legacy_lane_end = self.main_cmake.index(
            "\nendif ()\n\nset(ROR_BUILD_INSTALLER", legacy_lane_start
        )
        legacy_lane = self.main_cmake[legacy_lane_start:legacy_lane_end]
        self.assertIn(
            "find_program(Python3_EXECUTABLE NAMES python3 python REQUIRED)",
            legacy_lane,
        )
        self.assertIn(
            '"import sys; raise SystemExit(0 if sys.version_info >= (3, 8) else 1)"',
            legacy_lane,
        )
        self.assertIn(
            '"Ogre 1.11 recipe export requires Python 3.8 or newer"',
            legacy_lane,
        )
        self.assertIsNone(
            re.search(r"(?m)^\s*find_package\(", legacy_lane)
        )
        self.assertLess(
            legacy_lane.index("_ror_legacy_python_version_result"),
            legacy_lane.index("cmake/conan/export_pinned_dependency_recipes.py"),
        )
        self.assertLess(
            legacy_lane.index("cmake/conan/export_pinned_dependency_recipes.py"),
            legacy_lane.index("set(_ror_legacy_conan_args"),
        )
        self.assertIn('"--user=anotherfoxguy"', self.main_cmake)
        self.assertIn('"--channel=stable"', self.main_cmake)
        self.assertIn('"-o=&:ogre14=False"', self.main_cmake)
        self.assertIn(
            '"--lockfile=${CMAKE_SOURCE_DIR}/${_ror_legacy_lockfile}"',
            self.main_cmake,
        )
        self.assertIn(
            "cmake/conan/export_pinned_dependency_recipes.py",
            self.main_cmake,
        )
        self.assertIn(
            '"--conan=${CONAN_COMMAND}"',
            self.main_cmake,
        )
        self.assertIn(
            "ror_sanitize_ogre_graph_conan_install_args(", self.main_cmake
        )
        self.assertIn(
            "ror_disable_ogre14_dependent_options()", self.main_cmake
        )
        self.assertIn(
            "ror_validate_ogre_legacy_configuration(", self.main_cmake
        )

    def test_cmake_helpers_sanitize_stale_graph_state_and_disable_products(
        self,
    ) -> None:
        result = run_cmake_script(
            f"""
            include({json.dumps(str(PLATFORM_CMAKE))})
            set(CONAN_INSTALL_ARGS
                "--build=missing"
                "-o=&:ogre14=True"
                "--lockfile=/tmp/ror-ogre14-linux-x86_64-release.lock"
                "-o" "&:ogre14=False"
                "--options=openal-soft/*:thread_sanitizer=True"
                "--lockfile" "/tmp/ror-ogre11-linux-x86_64-release.lock"
                "-o=zlib/*:shared=True")
            ror_sanitize_ogre_graph_conan_install_args(
                SANITIZED ${{CONAN_INSTALL_ARGS}})
            set(EXPECTED "--build=missing;-o=zlib/*:shared=True")
            if (NOT SANITIZED STREQUAL EXPECTED)
                message(FATAL_ERROR "unexpected sanitized args: ${{SANITIZED}}")
            endif ()
            set(ROR_RENDERER_PUBLIC_LAUNCHER ON CACHE BOOL "" FORCE)
            set(ROR_OGRE_NEXT_PRODUCTION_PACKAGE ON CACHE BOOL "" FORCE)
            set(ROR_OGRE_NEXT_DEMO_ADMISSION ON CACHE BOOL "" FORCE)
            ror_disable_ogre14_dependent_options()
            if (ROR_RENDERER_PUBLIC_LAUNCHER
                    OR ROR_OGRE_NEXT_PRODUCTION_PACKAGE
                    OR ROR_OGRE_NEXT_DEMO_ADMISSION)
                message(FATAL_ERROR "legacy dependent product remains enabled")
            endif ()
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_cmake_helper_rejects_multiconfig_and_non_release_legacy_lanes(
        self,
    ) -> None:
        accepted = run_cmake_script(
            f"""
            include({json.dumps(str(PLATFORM_CMAKE))})
            ror_validate_ogre_legacy_configuration(FALSE Release)
            """
        )
        self.assertEqual(
            accepted.returncode, 0, accepted.stdout + accepted.stderr
        )

        hostile_cases = (
            ("TRUE", "Release", "single-config generator"),
            ("FALSE", "Debug", "CMAKE_BUILD_TYPE=Release"),
        )
        for is_multi_config, build_type, expected_error in hostile_cases:
            with self.subTest(
                is_multi_config=is_multi_config, build_type=build_type
            ):
                rejected = run_cmake_script(
                    f"""
                    include({json.dumps(str(PLATFORM_CMAKE))})
                    ror_validate_ogre_legacy_configuration(
                        {is_multi_config} {build_type})
                    """
                )
                self.assertNotEqual(rejected.returncode, 0)
                self.assertIn(
                    expected_error, rejected.stdout + rejected.stderr
                )

    def test_every_supported_platform_selects_an_exact_legacy_lock(self) -> None:
        for lockfile in LEGACY_LOCKS:
            with self.subTest(lockfile=lockfile.name):
                self.assertTrue(lockfile.is_file())
                self.assertIn(lockfile.name, self.platform_cmake)

    def test_default_and_opt_out_locks_have_no_cg_dependency(self) -> None:
        self.assertGreaterEqual(len(DEFAULT_LOCKS), 3)
        exact_reference = (
            "ogre3d/1.11.6.1@anotherfoxguy/stable#" + self.revision
        )
        exact_legacy_references = (
            exact_reference,
            graph_assertion.ANGELSCRIPT_REFERENCE,
            graph_assertion.CAELUM_REFERENCE,
            graph_assertion.MYGUI_REFERENCE,
            graph_assertion.PAGED_GEOMETRY_REFERENCE,
            graph_assertion.OIS_REFERENCE,
        )
        for lockfile in (*DEFAULT_LOCKS, *LEGACY_LOCKS):
            with self.subTest(lockfile=lockfile.name):
                payload = json.loads(lockfile.read_text(encoding="utf-8"))
                assert_cg_free_lock(payload)
                if lockfile in LEGACY_LOCKS:
                    locked = [entry.partition("%")[0] for entry in payload["requires"]]
                    for required_reference in exact_legacy_references:
                        self.assertEqual(
                            locked.count(required_reference),
                            1,
                            required_reference,
                        )
                    locked_build_requirements = {
                        entry.partition("%")[0]
                        for entry in payload["build_requires"]
                    }
                    self.assertEqual(
                        locked_build_requirements,
                        EXPECTED_LEGACY_BUILD_REQUIREMENTS[lockfile.name],
                    )

    def test_hostile_lock_injection_fails_closed(self) -> None:
        payload = json.loads(LEGACY_LOCKS[0].read_text(encoding="utf-8"))
        payload["requires"].append(
            "cg-toolkit/3.1@anotherfoxguy/stable#hostile%1"
        )
        with self.assertRaises(AssertionError):
            assert_cg_free_lock(payload)

    def test_actual_graph_shape_and_hostile_variants(self) -> None:
        graph = graph_fixture()
        graph_assertion.assert_cg_free_legacy_graph(graph, "linux-x86_64")

        for platform in graph_assertion.EXPECTED_PLATFORM_SETTINGS:
            with self.subTest(platform=platform):
                graph_assertion.assert_cg_free_legacy_graph(
                    graph_fixture(platform), platform
                )

        hostile_cg = copy.deepcopy(graph)
        hostile_cg["graph"]["nodes"]["6"] = {
            "ref": "cg-toolkit/3.1#hostile"
        }
        with self.assertRaises(AssertionError):
            graph_assertion.assert_cg_free_legacy_graph(
                hostile_cg, "linux-x86_64"
            )

        hostile_directx = graph_fixture("windows-x86_64")
        hostile_directx["graph"]["nodes"]["6"] = {
            "ref": "directx-sdk/9.0#hostile",
            "dependencies": {},
        }
        with self.assertRaises(AssertionError):
            graph_assertion.assert_cg_free_legacy_graph(
                hostile_directx, "windows-x86_64"
            )

        for hostile_reference in (
            "directx/9.0#hostile",
            "directx_sdk/9.0#hostile",
            "evil-sdk/1.0#hostile",
        ):
            with self.subTest(hostile_reference=hostile_reference):
                hostile_host = graph_fixture("windows-x86_64")
                hostile_host["graph"]["nodes"]["hostile"] = {
                    "ref": hostile_reference,
                    "context": "host",
                    "recipe": "Cache",
                    "dependencies": {},
                }
                with self.assertRaises(AssertionError):
                    graph_assertion.assert_cg_free_legacy_graph(
                        hostile_host, "windows-x86_64"
                    )

        hostile_override = copy.deepcopy(graph)
        hostile_override["graph"]["overrides"][
            "ogre3d/[~14]@anotherfoxguy/stable"
        ] = [None, graph_assertion.OGRE_REFERENCE]
        with self.assertRaises(AssertionError):
            graph_assertion.assert_cg_free_legacy_graph(
                hostile_override, "linux-x86_64"
            )

        hostile_directx_override = copy.deepcopy(graph)
        hostile_directx_override["graph"]["overrides"][
            "directx-sdk/9.0"
        ] = ["directx-sdk/9.0#hostile"]
        with self.assertRaises(AssertionError):
            graph_assertion.assert_cg_free_legacy_graph(
                hostile_directx_override, "linux-x86_64"
            )

        hostile_caelum_requirement = copy.deepcopy(graph)
        hostile_caelum_requirement["graph"]["nodes"]["2"][
            "dependencies"
        ]["1"]["require"] = "ogre3d/[~14]@anotherfoxguy/stable"
        with self.assertRaises(AssertionError):
            graph_assertion.assert_cg_free_legacy_graph(
                hostile_caelum_requirement, "linux-x86_64"
            )

        hostile_paged_requirement = copy.deepcopy(graph)
        hostile_paged_requirement["graph"]["nodes"]["4"][
            "dependencies"
        ]["1"]["require"] = "ogre3d/[~1.11]@anotherfoxguy/stable"
        with self.assertRaises(AssertionError):
            graph_assertion.assert_cg_free_legacy_graph(
                hostile_paged_requirement, "linux-x86_64"
            )

        wrong_revision = copy.deepcopy(graph)
        wrong_revision["graph"]["nodes"]["1"]["ref"] = (
            "ogre3d/1.11.6.1@anotherfoxguy/stable#remote-revision"
        )
        with self.assertRaises(AssertionError):
            graph_assertion.assert_cg_free_legacy_graph(
                wrong_revision, "linux-x86_64"
            )

        wrong_lane = copy.deepcopy(graph)
        wrong_lane["graph"]["nodes"]["0"]["options"]["ogre14"] = "True"
        with self.assertRaises(AssertionError):
            graph_assertion.assert_cg_free_legacy_graph(
                wrong_lane, "linux-x86_64"
            )

        wrong_build = copy.deepcopy(graph)
        wrong_build["graph"]["nodes"]["0"]["settings"][
            "build_type"
        ] = "Debug"
        with self.assertRaises(AssertionError):
            graph_assertion.assert_cg_free_legacy_graph(
                wrong_build, "linux-x86_64"
            )

        wrong_arch = copy.deepcopy(graph)
        wrong_arch["graph"]["nodes"]["1"]["settings"][
            "arch"
        ] = "armv8"
        with self.assertRaises(AssertionError):
            graph_assertion.assert_cg_free_legacy_graph(
                wrong_arch, "linux-x86_64"
            )


if __name__ == "__main__":
    unittest.main()
