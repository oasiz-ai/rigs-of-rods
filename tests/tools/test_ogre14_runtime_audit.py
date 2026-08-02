#!/usr/bin/env python3
"""Hostile-input tests for the cross-platform OGRE 14 runtime auditor."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL = REPOSITORY_ROOT / "tools" / "ogre14_runtime_audit.py"
SPEC = importlib.util.spec_from_file_location(
    "ogre14_runtime_audit",
    TOOL,
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot import runtime auditor from {TOOL}")
AUDIT = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = AUDIT
SPEC.loader.exec_module(AUDIT)

EXPECTED_PLUGINS = {
    "linux-x86_64": (
        "Codec_FreeImage",
        "RenderSystem_GL3Plus",
        "Plugin_ParticleFX",
        "Plugin_OctreeSceneManager",
    ),
    "windows-x86_64": (
        "Codec_FreeImage",
        "RenderSystem_Direct3D11",
        "Plugin_ParticleFX",
        "Plugin_OctreeSceneManager",
    ),
}
EXPECTED_PLUGIN_FOLDERS = {
    "linux-x86_64": "lib/OGRE",
    "windows-x86_64": ".",
}


class Ogre14RuntimeAuditTests(unittest.TestCase):
    def test_auditor_contract_matches_literal_expected_plugins(self) -> None:
        self.assertEqual(AUDIT.EXPECTED_PLUGINS, EXPECTED_PLUGINS)
        self.assertEqual(
            AUDIT.EXPECTED_PLUGIN_FOLDERS,
            EXPECTED_PLUGIN_FOLDERS,
        )

    def test_exact_linux_and_windows_configs_are_accepted(self) -> None:
        for platform in sorted(EXPECTED_PLUGINS):
            plugins = EXPECTED_PLUGINS[platform]
            folder = EXPECTED_PLUGIN_FOLDERS[platform]
            text = (
                "# disabled entries do not participate\n"
                "# Plugin=RenderSystem_Unsafe\n"
                f"PluginFolder={folder}\n"
                + "".join(f"Plugin={plugin}\n" for plugin in plugins)
            )
            with self.subTest(platform=platform):
                parsed = AUDIT.parse_plugins_config(
                    text,
                    expected_folder=folder,
                    expected_plugins=plugins,
                    context="fixture",
                )
                self.assertEqual(parsed.folder, folder)
                self.assertEqual(parsed.plugins, plugins)

    def test_hostile_plugin_configs_fail_closed(self) -> None:
        plugins = EXPECTED_PLUGINS["linux-x86_64"]
        valid_lines = "".join(f"Plugin={plugin}\n" for plugin in plugins)
        cases = {
            "absolute folder": "PluginFolder=/tmp/plugins\n" + valid_lines,
            "traversal folder": "PluginFolder=../lib/OGRE\n" + valid_lines,
            "duplicate folder": (
                "PluginFolder=lib/OGRE\n"
                "PluginFolder=lib/OGRE\n"
                + valid_lines
            ),
            "missing plugin": (
                "PluginFolder=lib/OGRE\n"
                + "".join(
                    f"Plugin={plugin}\n" for plugin in plugins[:-1]
                )
            ),
            "duplicate plugin": (
                "PluginFolder=lib/OGRE\n"
                + valid_lines
                + f"Plugin={plugins[-1]}\n"
            ),
            "extension token": (
                "PluginFolder=lib/OGRE\n"
                + "".join(
                    f"Plugin={plugin}\n" for plugin in plugins[:-1]
                )
                + "Plugin=Plugin_OctreeSceneManager.so\n"
            ),
            "traversal token": (
                "PluginFolder=lib/OGRE\n"
                + "".join(
                    f"Plugin={plugin}\n" for plugin in plugins[:-1]
                )
                + "Plugin=../Plugin_OctreeSceneManager\n"
            ),
            "cache prefix": (
                "# /tmp/.conan2/p/b/hash/p/lib/OGRE\n"
                "PluginFolder=lib/OGRE\n"
                + valid_lines
            ),
            "malformed active line": (
                "PluginFolder=lib/OGRE\n"
                + valid_lines
                + "this is not a directive\n"
            ),
            "unknown active directive": (
                "PluginFolder=lib/OGRE\n"
                + valid_lines
                + "PluginSearchPath=lib/Injected\n"
            ),
            "unsupported plus token": (
                "PluginFolder=lib/OGRE\n"
                + "".join(
                    f"Plugin={plugin}\n" for plugin in plugins[:-1]
                )
                + "Plugin=Plugin+OctreeSceneManager\n"
            ),
        }
        for label, text in cases.items():
            with self.subTest(case=label):
                with self.assertRaises(AUDIT.AuditError):
                    AUDIT.parse_plugins_config(
                        text,
                        expected_folder="lib/OGRE",
                        expected_plugins=plugins,
                        context="hostile fixture",
                    )

    def test_caller_forbidden_prefix_is_checked_cross_platform(self) -> None:
        for payload in (
            "/work/ror/build/plugins",
            r"C:\work\ror\build\plugins",
        ):
            with self.subTest(payload=payload):
                with self.assertRaises(AUDIT.AuditError):
                    AUDIT.assert_clean_text(
                        payload,
                        context="metadata",
                        forbidden_prefixes=("/work/ror", r"C:\work\ror"),
                    )

    def test_symlinks_must_be_relative_contained_and_resolvable(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="ror-ogre14-audit-symlinks-"
        ) as temporary:
            temporary_root = Path(temporary)
            root = temporary_root / "runtime"
            root.mkdir()
            target = root / "libPlugin.so.14.5"
            target.write_bytes(b"fixture")
            relative = root / "libPlugin.so"
            try:
                relative.symlink_to(target.name)
            except OSError as error:
                self.skipTest(f"symlinks unavailable: {error}")
            self.assertEqual(AUDIT.validate_symlinks(root), 1)

            outside = temporary_root / "outside.so"
            outside.write_bytes(b"outside")
            relative.unlink()
            relative.symlink_to(outside)
            with self.assertRaises(AUDIT.AuditError):
                AUDIT.validate_symlinks(root)

            relative.unlink()
            relative.symlink_to(outside.resolve())
            with self.assertRaises(AUDIT.AuditError):
                AUDIT.validate_symlinks(root)

    def test_elf_origin_paths_cannot_escape_or_be_absolute(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="ror-ogre14-audit-origin-"
        ) as temporary:
            root = Path(temporary)
            plugin_dir = root / "lib" / "OGRE"
            plugin_dir.mkdir(parents=True)
            binary = plugin_dir / "Plugin.so"
            binary.write_bytes(b"fixture")
            self.assertEqual(
                AUDIT.resolve_origin_search_path(
                    binary,
                    "$ORIGIN/..",
                    root,
                ),
                (root / "lib").resolve(),
            )
            self.assertEqual(
                AUDIT.resolve_origin_search_path(
                    binary,
                    "${ORIGIN}/../OGRE",
                    root,
                ),
                plugin_dir.resolve(),
            )
            for entry in (
                "/tmp/.conan2/lib",
                "$ORIGIN/../../../outside",
                "${ORIGIN}/../../../../outside",
                "$ORIGIN\\..\\outside",
                "$LIBRARY_PATH",
            ):
                with self.subTest(entry=entry):
                    with self.assertRaises(AUDIT.AuditError):
                        AUDIT.resolve_origin_search_path(
                            binary,
                            entry,
                            root,
                        )

    def test_elf_metadata_and_ldd_parsers_reject_unresolved_paths(self) -> None:
        needed, search_paths = AUDIT.parse_elf_dynamic_paths(
            " 0x1 (NEEDED) Shared library: [libOgreMain.so.14.5]\n"
            " 0x1d (RUNPATH) Library runpath: "
            "[$ORIGIN/..:$ORIGIN/../lib]\n"
        )
        self.assertEqual(needed, ["libOgreMain.so.14.5"])
        self.assertEqual(
            search_paths,
            ["$ORIGIN/..", "$ORIGIN/../lib"],
        )
        for dependency in (
            "/tmp/libInjected.so",
            "../libInjected.so",
            r"..\libInjected.so",
        ):
            with self.subTest(dependency=dependency):
                with self.assertRaises(AUDIT.AuditError):
                    AUDIT.parse_elf_dynamic_paths(
                        " 0x1 (NEEDED) Shared library: "
                        f"[{dependency}]\n"
                    )
        paths, unresolved = AUDIT.parse_ldd_paths(
            "linux-vdso.so.1 (0x1)\n"
            "libOgreMain.so.14.5 => /relocated/lib/libOgreMain.so.14.5 "
            "(0x2)\n"
            "libMissing.so => not found\n"
            "/lib64/ld-linux-x86-64.so.2 (0x3)\n"
        )
        self.assertEqual(
            paths,
            [
                Path("/relocated/lib/libOgreMain.so.14.5"),
                Path("/lib64/ld-linux-x86-64.so.2"),
            ],
        )
        self.assertEqual(unresolved, ["libMissing.so"])

    def test_linux_loader_environment_cannot_inherit_host_injection(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory(
            prefix="ror-ogre14-audit-loader-env-"
        ) as temporary:
            root = Path(temporary)
            (root / "lib").mkdir()
            environment = AUDIT.linux_loader_environment(
                root,
                {
                    "LANG": "host-locale",
                    "LC_ALL": "host-locale",
                    "LD_AUDIT": "/tmp/injected-audit.so",
                    "LD_DEBUG": "all",
                    "LD_DEBUG_OUTPUT": "/tmp/injected-debug",
                    "LD_LIBRARY_PATH": "/tmp/injected-lib",
                    "LD_PRELOAD": "/tmp/injected-preload.so",
                    "SAFE_SENTINEL": "preserved",
                },
            )
        self.assertEqual(environment["LANG"], "C")
        self.assertEqual(environment["LC_ALL"], "C")
        self.assertEqual(
            environment["LD_LIBRARY_PATH"],
            str(root.resolve() / "lib"),
        )
        self.assertEqual(environment["SAFE_SENTINEL"], "preserved")
        for variable in (
            "LD_AUDIT",
            "LD_DEBUG",
            "LD_DEBUG_OUTPUT",
            "LD_PRELOAD",
        ):
            with self.subTest(variable=variable):
                self.assertNotIn(variable, environment)

    def test_dumpbin_parsers_ignore_only_the_input_header(self) -> None:
        output = (
            "Dump of file C:\\work\\runtime\\RoR.exe\n"
            "\n"
            "  Image has the following dependencies:\n"
            "\n"
            "    KERNEL32.dll\n"
            "    OgreMain.dll\n"
            "\n"
            "  Summary\n"
        )
        self.assertEqual(
            AUDIT.parse_dumpbin_dependents(output),
            ("KERNEL32.dll", "OgreMain.dll"),
        )
        payload = AUDIT.dumpbin_payload(output)
        self.assertNotIn(r"C:\work\runtime", payload)
        self.assertIn("OgreMain.dll", payload)
        AUDIT.assert_amd64_pe(
            "            8664 machine (x64)\n",
            Path("RoR.exe"),
        )
        with self.assertRaises(AUDIT.AuditError):
            AUDIT.assert_amd64_pe(
                "             14C machine (x86)\n",
                Path("RoR.exe"),
            )

    def test_dumpbin_imports_include_delay_load_dll_closure(self) -> None:
        imports = (
            "Dump of file C:\\work\\runtime\\RoR.exe\n"
            "\n"
            "  Section contains the following imports:\n"
            "\n"
            "    KERNEL32.dll\n"
            "              00000000 Import Address Table\n"
            "\n"
            "  Section contains the following delay load imports:\n"
            "\n"
            "    InjectedDelay.dll\n"
            "              00000000 Import Address Table\n"
            "\n"
            "  Summary\n"
        )
        dependencies = AUDIT.parse_dumpbin_dependents(imports)
        self.assertEqual(
            dependencies,
            ("KERNEL32.dll", "InjectedDelay.dll"),
        )
        with self.assertRaisesRegex(
            AUDIT.AuditError,
            "InjectedDelay.dll",
        ):
            AUDIT.assert_windows_dependency_closure(
                Path("RoR.exe"),
                dependencies,
                {},
                system_root=None,
            )
        AUDIT.assert_windows_dependency_closure(
            Path("RoR.exe"),
            dependencies,
            {"injecteddelay.dll": Path("InjectedDelay.dll")},
            system_root=None,
        )

    def test_windows_pe_audit_checks_imports_for_every_binary(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="ror-ogre14-audit-pe-imports-"
        ) as temporary:
            root = Path(temporary)
            executable = root / "RoR.exe"
            dependency = root / "OgreMain.dll"
            executable.write_bytes(b"fixture")
            dependency.write_bytes(b"fixture")
            commands: list[tuple[str, ...]] = []

            def run_fixture(
                command: tuple[str, ...] | list[str],
                **_kwargs: object,
            ) -> object:
                rendered = tuple(command)
                commands.append(rendered)
                option = rendered[2]
                binary_name = Path(rendered[-1]).name
                if option == "/headers":
                    output = "            8664 machine (x64)\n"
                elif option == "/dependents":
                    output = "    KERNEL32.dll\n"
                elif option == "/imports":
                    output = "    KERNEL32.dll\n"
                    if binary_name == "RoR.exe":
                        output += "    InjectedDelay.dll\n"
                else:
                    raise AssertionError(f"unexpected dumpbin option: {option}")
                return type("Result", (), {"stdout": output})()

            with (
                mock.patch.object(
                    AUDIT.shutil,
                    "which",
                    return_value="dumpbin",
                ),
                mock.patch.object(
                    AUDIT,
                    "run",
                    side_effect=run_fixture,
                ),
                self.assertRaisesRegex(
                    AUDIT.AuditError,
                    "InjectedDelay.dll",
                ),
            ):
                AUDIT.audit_windows_pe(root, forbidden_prefixes=())

            inspected_imports = {
                Path(command[-1]).name
                for command in commands
                if command[2] == "/imports"
            }
            self.assertEqual(inspected_imports, {"OgreMain.dll", "RoR.exe"})

            (root / "InjectedDelay.dll").write_bytes(b"packaged")
            commands.clear()
            with (
                mock.patch.object(
                    AUDIT.shutil,
                    "which",
                    return_value="dumpbin",
                ),
                mock.patch.object(
                    AUDIT,
                    "run",
                    side_effect=run_fixture,
                ),
            ):
                metrics = AUDIT.audit_windows_pe(
                    root,
                    forbidden_prefixes=(),
                )
            self.assertEqual(metrics["pe_files"], 3)
            self.assertEqual(
                {
                    Path(command[-1]).name
                    for command in commands
                    if command[2] == "/imports"
                },
                {"InjectedDelay.dll", "OgreMain.dll", "RoR.exe"},
            )

    def test_windows_system_boundary_does_not_accept_arbitrary_dlls(
        self,
    ) -> None:
        for name in (
            "KERNEL32.dll",
            "api-ms-win-core-file-l1-1-0.dll",
            "ext-ms-win-ntuser-window-l1-1-0.dll",
        ):
            with self.subTest(name=name):
                self.assertTrue(
                    AUDIT.is_windows_system_dependency(
                        name,
                        system_root=None,
                    )
                )
        for name in (
            "OgreMain.dll",
            "../Injected.dll",
            r"..\Injected.dll",
            "Injected.so",
        ):
            with self.subTest(name=name):
                self.assertFalse(
                    AUDIT.is_windows_system_dependency(
                        name,
                        system_root=None,
                    )
                )

    def test_forbidden_renderer_binaries_are_platform_specific(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="ror-ogre14-audit-forbidden-"
        ) as temporary:
            root = Path(temporary)
            safe = root / "RenderSystem_GL3Plus.so"
            safe.write_bytes(b"fixture")
            AUDIT.assert_no_forbidden_plugin_binaries(
                root,
                "linux-x86_64",
            )
            legacy_gl = root / "RenderSystem_GL.so"
            legacy_gl.write_bytes(b"fixture")
            with self.assertRaises(AUDIT.AuditError):
                AUDIT.assert_no_forbidden_plugin_binaries(
                    root,
                    "linux-x86_64",
                )
            legacy_gl.unlink()
            unsafe = root / "RenderSystem_Direct3D11.so"
            unsafe.write_bytes(b"fixture")
            with self.assertRaises(AUDIT.AuditError):
                AUDIT.assert_no_forbidden_plugin_binaries(
                    root,
                    "linux-x86_64",
                )
            unsafe.unlink()
            safe.unlink()
            (root / "RenderSystem_GL3Plus.dll").write_bytes(b"fixture")
            with self.assertRaises(AUDIT.AuditError):
                AUDIT.assert_no_forbidden_plugin_binaries(
                    root,
                    "windows-x86_64",
                )

    def test_linux_plugin_directory_is_an_exact_soname_family_set(
        self,
    ) -> None:
        plugins = EXPECTED_PLUGINS["linux-x86_64"]
        with tempfile.TemporaryDirectory(
            prefix="ror-ogre14-audit-linux-plugin-set-"
        ) as temporary:
            root = Path(temporary)
            plugin_dir = root / "lib" / "OGRE"
            plugin_dir.mkdir(parents=True)
            for plugin in plugins:
                real = plugin_dir / f"{plugin}.so.14.5.2"
                real.write_bytes(b"fixture")
                soname = plugin_dir / f"{plugin}.so.14.5"
                soname.symlink_to(real.name)
                unversioned = plugin_dir / f"{plugin}.so"
                unversioned.symlink_to(soname.name)
            self.assertEqual(
                AUDIT.assert_exact_plugin_binary_families(
                    root,
                    "linux-x86_64",
                    plugins,
                ),
                len(plugins) * 3,
            )
            missing_family_paths = tuple(
                plugin_dir.glob(
                    f"{plugins[-1]}.so*"
                )
            )
            for path in missing_family_paths:
                path.unlink()
            with self.assertRaises(AUDIT.AuditError):
                AUDIT.assert_exact_plugin_binary_families(
                    root,
                    "linux-x86_64",
                    plugins,
                )
            for path in missing_family_paths:
                if path.name.endswith(".14.5.2"):
                    path.write_bytes(b"fixture")
            (plugin_dir / f"{plugins[-1]}.so.14.5").symlink_to(
                f"{plugins[-1]}.so.14.5.2"
            )
            (plugin_dir / f"{plugins[-1]}.so").symlink_to(
                f"{plugins[-1]}.so.14.5"
            )

            hostile_names = (
                "Plugin_BSPSceneManager.so",
                "Plugin_ParticleFX_d.so",
                "Codec_FreeImage.so.debug",
                "RenderSystem_GL3Plus.so.14.5.cache",
                "RenderSystem_GL3Plus.so.14.5.3",
            )
            for hostile_name in hostile_names:
                with self.subTest(hostile_name=hostile_name):
                    hostile = plugin_dir / hostile_name
                    hostile.write_bytes(b"hostile")
                    with self.assertRaises(AUDIT.AuditError):
                        AUDIT.assert_exact_plugin_binary_families(
                            root,
                            "linux-x86_64",
                            plugins,
                        )
                    hostile.unlink()

            unversioned = plugin_dir / f"{plugins[0]}.so"
            unversioned.unlink()
            unversioned.symlink_to(f"{plugins[0]}.so.14.5.2")
            with self.assertRaisesRegex(
                AUDIT.AuditError,
                "non-canonical relative SONAME link",
            ):
                AUDIT.assert_exact_plugin_binary_families(
                    root,
                    "linux-x86_64",
                    plugins,
                )
            unversioned.unlink()
            unversioned.symlink_to(f"{plugins[0]}.so.14.5")

            real = plugin_dir / f"{plugins[0]}.so.14.5.2"
            real.unlink()
            real.symlink_to(f"{plugins[0]}.so.14.5")
            with self.assertRaises(AUDIT.AuditError):
                AUDIT.assert_exact_plugin_binary_families(
                    root,
                    "linux-x86_64",
                    plugins,
                )
            real.unlink()
            real.write_bytes(b"fixture")

            unexpected_directory = plugin_dir / "Plugin_Injected.so"
            unexpected_directory.mkdir()
            with self.assertRaises(AUDIT.AuditError):
                AUDIT.assert_exact_plugin_binary_families(
                    root,
                    "linux-x86_64",
                    plugins,
                )

    def test_windows_ogre_style_dlls_are_an_exact_release_set(self) -> None:
        plugins = EXPECTED_PLUGINS["windows-x86_64"]
        with tempfile.TemporaryDirectory(
            prefix="ror-ogre14-audit-windows-plugin-set-"
        ) as temporary:
            root = Path(temporary)
            for plugin in plugins:
                (root / f"{plugin}.dll").write_bytes(b"fixture")
            for dependency in ("OgreMain.dll", "SDL2.dll", "OpenAL32.dll"):
                (root / dependency).write_bytes(b"dependency")
            self.assertEqual(
                AUDIT.assert_exact_plugin_binary_families(
                    root,
                    "windows-x86_64",
                    plugins,
                ),
                len(plugins),
            )
            missing = root / f"{plugins[-1]}.dll"
            missing.unlink()
            with self.assertRaises(AUDIT.AuditError):
                AUDIT.assert_exact_plugin_binary_families(
                    root,
                    "windows-x86_64",
                    plugins,
                )
            missing.write_bytes(b"fixture")

            hostile_names = (
                "Plugin_BSPSceneManager.dll",
                "Plugin_ParticleFX_d.dll",
                "Codec_FreeImage_d.dll",
                "RenderSystem_GL3Plus.dll",
                "RenderSystem_Direct3D11_d.dll",
            )
            for hostile_name in hostile_names:
                with self.subTest(hostile_name=hostile_name):
                    hostile = root / hostile_name
                    hostile.write_bytes(b"hostile")
                    with self.assertRaises(AUDIT.AuditError):
                        AUDIT.assert_exact_plugin_binary_families(
                            root,
                            "windows-x86_64",
                            plugins,
                        )
                    hostile.unlink()

    def test_windows_pe_runtime_must_be_flat_beside_executable(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="ror-ogre14-audit-pe-layout-"
        ) as temporary:
            root = Path(temporary)
            (root / "RoR.exe").write_bytes(b"fixture")
            (root / "OgreMain.dll").write_bytes(b"fixture")
            self.assertEqual(
                [path.name for path in AUDIT.windows_pe_files(root)],
                ["OgreMain.dll", "RoR.exe"],
            )
            nested = root / "plugins"
            nested.mkdir()
            (nested / "Injected.dll").write_bytes(b"fixture")
            with self.assertRaises(AUDIT.AuditError):
                AUDIT.windows_pe_files(root)

    def test_linux_renderer_suite_audits_public_and_compatibility_elves(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory(
            prefix="ror-ogre14-audit-renderer-suite-"
        ) as temporary:
            root = Path(temporary)
            public = root / "RoR"
            compatibility = root / "RoR-Ogre14"
            public.write_bytes(b"public")
            compatibility.write_bytes(b"compatibility")
            files = AUDIT.linux_elf_files(root, compatibility)
            self.assertEqual(
                {path.name for path in files},
                {"RoR", "RoR-Ogre14"},
            )

    def test_script_has_no_platform_specific_import_dependency(self) -> None:
        source = TOOL.read_text(encoding="utf-8")
        for dependency in ("pefile", "elftools", "yaml"):
            with self.subTest(dependency=dependency):
                self.assertNotIn(f"import {dependency}", source)
        self.assertNotIn("source/main/worldmodel", source)
        self.assertNotIn("tests/worldmodel", source)


if __name__ == "__main__":
    unittest.main()
