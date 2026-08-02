#!/usr/bin/env python3
"""Tests for exact Windows OGRE 14 runtime DLL staging."""

from __future__ import annotations

from pathlib import Path
import subprocess
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
STAGER = (
    REPOSITORY_ROOT
    / "cmake"
    / "windows"
    / "StageWindowsRuntime.cmake"
)
MAIN_CMAKE = REPOSITORY_ROOT / "source" / "main" / "CMakeLists.txt"
APP_CONTEXT = REPOSITORY_ROOT / "source" / "main" / "AppContext.cpp"
WINDOWS_RUNTIME_PATH = (
    REPOSITORY_ROOT
    / "source"
    / "main"
    / "utils"
    / "WindowsRuntimePath.h"
)
EXPECTED_PLUGINS = (
    "Codec_FreeImage",
    "RenderSystem_Direct3D11",
    "Plugin_ParticleFX",
    "Plugin_OctreeSceneManager",
)
UNUSED_PLUGIN_DLLS = (
    "Plugin_BSPSceneManager.dll",
    "Plugin_OctreeZone.dll",
    "Plugin_PCZSceneManager.dll",
)


def stage_runtime(
    filenames: tuple[str, ...],
    *,
    configuration: str = "Release",
) -> tuple[subprocess.CompletedProcess[str], set[str]]:
    with tempfile.TemporaryDirectory(
        prefix="ror-ogre14-windows-stage-"
    ) as temporary:
        root = Path(temporary)
        runtime = root / "runtime"
        install = root / "install"
        runtime.mkdir()
        for filename in filenames:
            (runtime / filename).write_bytes(b"fixture")
        script = root / "stage.cmake"
        script.write_text(
            f'set(CMAKE_INSTALL_PREFIX "{install.as_posix()}")\n'
            f'set(CMAKE_INSTALL_CONFIG_NAME "{configuration}")\n'
            "set(ROR_WINDOWS_RUNTIME_OUTPUT_DIRECTORY "
            f'"{runtime.as_posix()}")\n'
            "set(ROR_WINDOWS_EXPECTED_PLUGINS "
            f'"{";".join(EXPECTED_PLUGINS)}")\n'
            "set(ROR_WINDOWS_PLUGIN_BINARIES_USE_DEBUG_SUFFIX ON)\n"
            f'include("{STAGER.as_posix()}")\n',
            encoding="utf-8",
        )
        result = subprocess.run(
            ["cmake", "-P", str(script)],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        installed = (
            {path.name for path in install.iterdir()}
            if install.is_dir()
            else set()
        )
        return result, installed


class WindowsOgre14RuntimeContractTests(unittest.TestCase):
    def test_launcher_extended_path_is_normalized_before_runtime_roots(self) -> None:
        source = APP_CONTEXT.read_text(encoding="utf-8")
        self.assertIn('#include "WindowsRuntimePath.h"', source)
        normalize = "NormalizeWindowsExtendedPathForRuntime(exe_path);"
        normalize_offset = source.index(normalize)
        process_root_offset = source.index(
            "App::sys_process_dir->setStr",
            normalize_offset,
        )
        self.assertLess(normalize_offset, process_root_offset)

        helper = WINDOWS_RUNTIME_PATH.read_text(encoding="utf-8")
        self.assertIn('extended_unc_prefix = "\\\\\\\\?\\\\UNC\\\\"', helper)
        self.assertIn('extended_prefix = "\\\\\\\\?\\\\"', helper)

    def test_ogre14_install_routes_dlls_through_exact_stager(self) -> None:
        source = MAIN_CMAKE.read_text(encoding="utf-8")
        self.assertEqual(source.count("StageWindowsRuntime.cmake"), 1)
        self.assertIn("set(ROR_WINDOWS_EXPECTED_PLUGINS ", source)
        self.assertEqual(
            source.count("${_ror_ogre14_runtime_ACTIVE_PLUGINS}"),
            1,
        )
        self.assertIn(
            "set(ROR_WINDOWS_PLUGIN_BINARIES_USE_DEBUG_SUFFIX ",
            source,
        )
        self.assertEqual(
            source.count(
                "${_ror_ogre14_runtime_PLUGIN_BINARIES_USE_DEBUG_SUFFIX}"
            ),
            1,
        )

    def test_release_stages_only_active_plugin_families(self) -> None:
        expected_dlls = tuple(
            f"{plugin}.dll" for plugin in EXPECTED_PLUGINS
        )
        dependencies = ("OgreMain.dll", "SDL2.dll", "OpenAL32.dll")
        result, installed = stage_runtime(
            expected_dlls + UNUSED_PLUGIN_DLLS + dependencies
        )
        self.assertEqual(
            result.returncode,
            0,
            msg=result.stdout + result.stderr,
        )
        self.assertEqual(installed, set(expected_dlls + dependencies))

    def test_debug_selects_only_debug_plugin_variants(self) -> None:
        release_dlls = tuple(
            f"{plugin}.dll" for plugin in EXPECTED_PLUGINS
        )
        debug_dlls = tuple(
            f"{plugin}_d.dll" for plugin in EXPECTED_PLUGINS
        )
        result, installed = stage_runtime(
            release_dlls
            + debug_dlls
            + ("Plugin_BSPSceneManager_d.dll", "OgreMain_d.dll"),
            configuration="Debug",
        )
        self.assertEqual(
            result.returncode,
            0,
            msg=result.stdout + result.stderr,
        )
        self.assertEqual(installed, set(debug_dlls + ("OgreMain_d.dll",)))

    def test_missing_active_plugin_fails_before_install(self) -> None:
        expected_dlls = tuple(
            f"{plugin}.dll" for plugin in EXPECTED_PLUGINS[:-1]
        )
        result, installed = stage_runtime(
            expected_dlls + ("OgreMain.dll",)
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            "Windows OGRE plugin DLL set is incomplete",
            result.stdout + result.stderr,
        )
        self.assertEqual(installed, set())

    def test_unsupported_configuration_fails_closed(self) -> None:
        expected_dlls = tuple(
            f"{plugin}.dll" for plugin in EXPECTED_PLUGINS
        )
        result, installed = stage_runtime(
            expected_dlls,
            configuration="RelWithDebInfo",
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            "supports only Debug and Release",
            result.stdout + result.stderr,
        )
        self.assertEqual(installed, set())


if __name__ == "__main__":
    unittest.main()
