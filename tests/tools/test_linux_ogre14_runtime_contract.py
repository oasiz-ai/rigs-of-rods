#!/usr/bin/env python3
"""Dependency-free tests for the relocatable Linux OGRE 14 runtime."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
CONTRACT_MODULE = (
    REPOSITORY_ROOT / "cmake" / "linux" / "LinuxRuntimeContract.cmake"
)
STAGER = REPOSITORY_ROOT / "cmake" / "linux" / "StageLinuxRuntime.cmake"
SOURCE_CMAKE = REPOSITORY_ROOT / "source" / "main" / "CMakeLists.txt"
PLATFORM_MODULE = REPOSITORY_ROOT / "cmake" / "Ogre14Platform.cmake"
LEGACY_LAUNCHER = REPOSITORY_ROOT / "tools" / "linux" / "RunRoR"
OGRE14_LAUNCHER = (
    REPOSITORY_ROOT / "tools" / "linux" / "RunRoR-ogre14"
)

EXPECTED_PLUGINS = (
    "Codec_FreeImage",
    "RenderSystem_GL3Plus",
    "Plugin_ParticleFX",
    "Plugin_OctreeSceneManager",
)


def native_path_text(value: str | Path) -> str:
    """Normalize CMake's slash style to the current host path syntax."""

    return os.path.normcase(os.path.normpath(os.fspath(value)))


def active_config(
    *,
    folder: str = "lib/OGRE",
    plugins: tuple[str, ...] = EXPECTED_PLUGINS,
) -> str:
    plugin_lines = "\n".join(f"Plugin={plugin}" for plugin in plugins)
    return f"PluginFolder={folder}\n\n{plugin_lines}\n"


def run_cmake(
    directory: Path,
    body: str,
) -> subprocess.CompletedProcess[str]:
    script = directory / "contract-test.cmake"
    script.write_text(
        f'include("{CONTRACT_MODULE.as_posix()}")\n{body}',
        encoding="utf-8",
    )
    return subprocess.run(
        ["cmake", "-P", str(script)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )


def validate_config(config_text: str) -> subprocess.CompletedProcess[str]:
    with tempfile.TemporaryDirectory(
        prefix="ror-linux-config-contract-"
    ) as temporary:
        root = Path(temporary)
        config = root / "plugins.cfg"
        config.write_text(config_text, encoding="utf-8")
        output = root / "plugins.txt"
        result = run_cmake(
            root,
            "set(expected_plugins\n"
            + "\n".join(f'    "{plugin}"' for plugin in EXPECTED_PLUGINS)
            + ")\n"
            "ror_linux_ogre14_validate_plugins_config(\n"
            f'    plugins "{config.as_posix()}" "lib/OGRE" '
            '"${expected_plugins}")\n'
            f'file(WRITE "{output.as_posix()}" "${{plugins}}")\n',
        )
        if result.returncode == 0:
            result.plugins = tuple(
                output.read_text(encoding="utf-8").split(";")
            )
        return result


class LinuxOgre14RuntimeContractTests(unittest.TestCase):
    def test_valid_config_selects_only_the_exact_runtime_plugins(self) -> None:
        result = validate_config(
            "# comments and disabled entries are ignored\n"
            "# PluginFolder=/tmp/.conan2/p/ogre/lib/OGRE\n"
            "# Plugin=RenderSystem_GL\n"
            + active_config()
        )
        self.assertEqual(
            result.returncode,
            0,
            msg=result.stdout + result.stderr,
        )
        self.assertEqual(result.plugins, EXPECTED_PLUGINS)

    def test_hostile_or_drifted_plugin_configs_fail_closed(self) -> None:
        cases = {
            "absolute cache folder": active_config(
                folder="/tmp/.conan2/p/ogre/lib/OGRE"
            ),
            "traversal folder": active_config(folder="../lib/OGRE"),
            "duplicate folder": (
                "PluginFolder=lib/OGRE\n" + active_config()
            ),
            "missing folder": "\n".join(
                f"Plugin={plugin}" for plugin in EXPECTED_PLUGINS
            ),
            "traversal plugin": active_config(
                plugins=EXPECTED_PLUGINS[:-1] + ("../escape",)
            ),
            "suffixed plugin": active_config(
                plugins=EXPECTED_PLUGINS[:-1]
                + ("Plugin_OctreeSceneManager.so",)
            ),
            "duplicate plugin": active_config(
                plugins=EXPECTED_PLUGINS
                + ("Plugin_OctreeSceneManager",)
            ),
            "extra plugin": active_config(
                plugins=EXPECTED_PLUGINS + ("Plugin_BSPSceneManager",)
            ),
            "unsupported directive": (
                active_config() + "PluginOptional=Injected\n"
            ),
        }
        for label, config in cases.items():
            with self.subTest(case=label):
                result = validate_config(config)
                self.assertNotEqual(result.returncode, 0)

    @unittest.skipIf(
        os.name == "nt",
        "Linux plugin SONAME chain execution runs in the Linux lane",
    )
    def test_plugin_resolution_follows_only_a_contained_symlink_chain(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory(
            prefix="ror-linux-plugin-contract-"
        ) as temporary:
            root = Path(temporary)
            plugin_dir = root / "package" / "lib" / "OGRE"
            plugin_dir.mkdir(parents=True)
            versioned = plugin_dir / "Codec_FreeImage.so.14.5"
            versioned.write_bytes(b"fixture")
            unversioned = plugin_dir / "Codec_FreeImage.so"
            unversioned.symlink_to(versioned.name)
            output = root / "resolved.txt"
            result = run_cmake(
                root,
                "ror_linux_ogre14_resolve_plugin(\n"
                f'    plugin real_plugin "{plugin_dir.as_posix()}" '
                '"Codec_FreeImage")\n'
                f'file(WRITE "{output.as_posix()}" '
                '"${plugin}\\n${real_plugin}\\n")\n',
            )
            self.assertEqual(
                result.returncode,
                0,
                msg=result.stdout + result.stderr,
            )
            self.assertEqual(
                [
                    native_path_text(value)
                    for value in output.read_text(
                        encoding="utf-8"
                    ).splitlines()[0].split(";")
                ],
                [
                    native_path_text(unversioned),
                    native_path_text(versioned),
                ],
            )
            self.assertEqual(
                native_path_text(
                    output.read_text(encoding="utf-8").splitlines()[1]
                ),
                native_path_text(versioned.resolve()),
            )

            outside = root / "outside.so.14.5"
            outside.write_bytes(b"outside")
            unversioned.unlink()
            unversioned.symlink_to(outside)
            escaped = run_cmake(
                root,
                "ror_linux_ogre14_resolve_plugin(\n"
                f'    plugin real_plugin "{plugin_dir.as_posix()}" '
                '"Codec_FreeImage")\n',
            )
            self.assertNotEqual(escaped.returncode, 0)
            self.assertIn(
                "symlink is absolute",
                escaped.stdout + escaped.stderr,
            )

    def test_ambiguous_or_traversal_plugin_sources_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="ror-linux-plugin-invalid-"
        ) as temporary:
            root = Path(temporary)
            plugin_dir = root / "plugins"
            plugin_dir.mkdir()
            (plugin_dir / "Plugin_ParticleFX.so.14.5").write_bytes(b"a")
            (plugin_dir / "Plugin_ParticleFX.so.15.0").write_bytes(b"b")
            ambiguous = run_cmake(
                root,
                "ror_linux_ogre14_resolve_plugin(\n"
                f'    plugin real_plugin "{plugin_dir.as_posix()}" '
                '"Plugin_ParticleFX")\n',
            )
            self.assertNotEqual(ambiguous.returncode, 0)

            (plugin_dir / "Plugin_ParticleFX.so.15.0").unlink()
            missing_unversioned = run_cmake(
                root,
                "ror_linux_ogre14_resolve_plugin(\n"
                f'    plugin real_plugin "{plugin_dir.as_posix()}" '
                '"Plugin_ParticleFX")\n',
            )
            self.assertNotEqual(missing_unversioned.returncode, 0)
            self.assertIn(
                ".so or .so.14.5",
                missing_unversioned.stdout + missing_unversioned.stderr,
            )

            traversal = run_cmake(
                root,
                "ror_linux_ogre14_resolve_plugin(\n"
                f'    plugin real_plugin "{plugin_dir.as_posix()}" '
                '"../Plugin_ParticleFX")\n',
            )
            self.assertNotEqual(traversal.returncode, 0)

    def test_runtime_dependencies_must_stay_in_approved_package_roots(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory(
            prefix="ror-linux-dependency-contract-"
        ) as temporary:
            root = Path(temporary)
            approved = root / "conan" / "package" / "lib"
            approved.mkdir(parents=True)
            dependency = approved / "libOgreMain.so.14.5"
            dependency.write_bytes(b"fixture")
            output = root / "validated.txt"
            valid = run_cmake(
                root,
                "ror_linux_ogre14_validate_runtime_paths(\n"
                f'    paths "{approved.as_posix()}" '
                f'"{dependency.as_posix()}")\n'
                f'file(WRITE "{output.as_posix()}" "${{paths}}")\n',
            )
            self.assertEqual(
                valid.returncode,
                0,
                msg=valid.stdout + valid.stderr,
            )
            self.assertEqual(
                native_path_text(output.read_text(encoding="utf-8")),
                native_path_text(dependency),
            )

            outside = root / "host" / "libInjected.so"
            outside.parent.mkdir()
            outside.write_bytes(b"fixture")
            invalid_paths = (
                str(outside),
                "../relative/libInjected.so",
                str(approved),
                str(root / "missing.so"),
            )
            for runtime_path in invalid_paths:
                with self.subTest(runtime_path=runtime_path):
                    invalid = run_cmake(
                        root,
                        "ror_linux_ogre14_validate_runtime_paths(\n"
                        f'    paths "{approved.as_posix()}" '
                        f'"{runtime_path}")\n',
                    )
                    self.assertNotEqual(invalid.returncode, 0)

            system_root = run_cmake(
                root,
                "ror_linux_ogre14_validate_runtime_paths(\n"
                '    paths "/usr/lib" "")\n',
            )
            self.assertNotEqual(system_root.returncode, 0)
            self.assertRegex(
                system_root.stdout + system_root.stderr,
                r"(host system library|not an absolute directory)",
                msg=(
                    "the host system root must be rejected either by the "
                    "platform path parser or the Linux system-library guard"
                ),
            )

    @unittest.skipIf(
        os.name == "nt",
        "Linux SONAME chain execution runs in the Linux lane",
    )
    def test_dependency_copy_roots_preserve_the_complete_soname_chain(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory(
            prefix="ror-linux-soname-contract-"
        ) as temporary:
            root = Path(temporary)
            library_dir = root / "package" / "lib"
            library_dir.mkdir(parents=True)
            real = library_dir / "libOgreMain.so.14.5.2"
            real.write_bytes(b"fixture")
            abi = library_dir / "libOgreMain.so.14.5"
            abi.symlink_to(real.name)
            unversioned = library_dir / "libOgreMain.so"
            unversioned.symlink_to(abi.name)
            output = root / "copy-roots.txt"
            result = run_cmake(
                root,
                "ror_linux_ogre14_dependency_copy_roots(\n"
                f'    roots "{real.as_posix()}")\n'
                f'file(WRITE "{output.as_posix()}" "${{roots}}")\n',
            )
            self.assertEqual(
                result.returncode,
                0,
                msg=result.stdout + result.stderr,
            )
            self.assertEqual(
                {
                    native_path_text(value)
                    for value in output.read_text(
                        encoding="utf-8"
                    ).split(";")
                },
                {
                    native_path_text(real),
                    native_path_text(abi),
                    native_path_text(unversioned),
                },
            )

    def test_installed_tree_rejects_absolute_and_escaping_symlinks(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory(
            prefix="ror-linux-installed-tree-"
        ) as temporary:
            root = Path(temporary)
            install_root = root / "package"
            library_root = install_root / "lib"
            library_root.mkdir(parents=True)
            target = library_root / "libValid.so.1"
            target.write_bytes(b"fixture")
            link = library_root / "libValid.so"
            link.symlink_to(target.name)
            valid = run_cmake(
                root,
                "ror_linux_ogre14_validate_installed_symlinks(\n"
                f'    "{install_root.as_posix()}" '
                f'"{library_root.as_posix()}")\n',
            )
            self.assertEqual(
                valid.returncode,
                0,
                msg=valid.stdout + valid.stderr,
            )

            outside = root / "outside.so"
            outside.write_bytes(b"outside")
            link.unlink()
            link.symlink_to(outside)
            absolute = run_cmake(
                root,
                "ror_linux_ogre14_validate_installed_symlinks(\n"
                f'    "{install_root.as_posix()}" '
                f'"{library_root.as_posix()}")\n',
            )
            self.assertNotEqual(absolute.returncode, 0)

            link.unlink()
            link.symlink_to("../../outside.so")
            escaped = run_cmake(
                root,
                "ror_linux_ogre14_validate_installed_symlinks(\n"
                f'    "{install_root.as_posix()}" '
                f'"{library_root.as_posix()}")\n',
            )
            self.assertNotEqual(escaped.returncode, 0)

    def test_unresolved_and_conflicting_dependency_sets_fail_closed(
        self,
    ) -> None:
        valid = None
        with tempfile.TemporaryDirectory(
            prefix="ror-linux-resolution-contract-"
        ) as temporary:
            root = Path(temporary)
            valid = run_cmake(
                root,
                'ror_linux_ogre14_assert_dependency_resolution("" "")\n',
            )
            unresolved = run_cmake(
                root,
                "ror_linux_ogre14_assert_dependency_resolution("
                '"libMissing.so" "")\n',
            )
            conflicting = run_cmake(
                root,
                "ror_linux_ogre14_assert_dependency_resolution("
                '"" "libConflict.so")\n',
            )
        self.assertEqual(
            valid.returncode,
            0,
            msg=valid.stdout + valid.stderr,
        )
        self.assertNotEqual(unresolved.returncode, 0)
        self.assertIn(
            "unresolved dependencies",
            unresolved.stdout + unresolved.stderr,
        )
        self.assertNotEqual(conflicting.returncode, 0)
        self.assertIn(
            "conflicting dependency basenames",
            conflicting.stdout + conflicting.stderr,
        )

    def test_loader_metadata_accepts_only_package_local_search_paths(
        self,
    ) -> None:
        valid_metadata = (
            " 0x000000000000001d (RUNPATH) Library runpath: "
            "[$ORIGIN:$ORIGIN/..:$ORIGIN/../lib]\n"
            " 0x0000000000000001 (NEEDED) Shared library: "
            "[libOgreMain.so.14.5]\n"
        )
        invalid_metadata = {
            "absolute cache": (
                " 0x1 (RUNPATH) Library runpath: "
                "[/tmp/.conan2/p/ogre/lib]\n"
            ),
            "origin traversal": (
                " 0x1 (RUNPATH) Library runpath: "
                "[$ORIGIN/../../outside]\n"
            ),
            "absolute needed": (
                " 0x1 (NEEDED) Shared library: "
                "[/tmp/.conan2/p/libInjected.so]\n"
            ),
            "needed traversal": (
                " 0x1 (NEEDED) Shared library: [../libInjected.so]\n"
            ),
        }
        with tempfile.TemporaryDirectory(
            prefix="ror-linux-loader-contract-"
        ) as temporary:
            root = Path(temporary)
            valid = run_cmake(
                root,
                "ror_linux_ogre14_validate_loader_metadata("
                f'[=[{valid_metadata}]=] "fixture")\n',
            )
            self.assertEqual(
                valid.returncode,
                0,
                msg=valid.stdout + valid.stderr,
            )
            for label, metadata in invalid_metadata.items():
                with self.subTest(case=label):
                    invalid = run_cmake(
                        root,
                        "ror_linux_ogre14_validate_loader_metadata("
                        f'[=[{metadata}]=] "fixture")\n',
                    )
                    self.assertNotEqual(invalid.returncode, 0)

    def test_stager_and_install_wiring_are_linux_ogre14_only(self) -> None:
        platform = PLATFORM_MODULE.read_text(encoding="utf-8")
        source = SOURCE_CMAKE.read_text(encoding="utf-8")
        stager = STAGER.read_text(encoding="utf-8")

        self.assertIn(
            'set(_ror_install_plugin_folder "lib/OGRE")',
            platform,
        )
        self.assertIn(
            'INSTALL_RPATH "$ORIGIN/lib;$ORIGIN/lib/OGRE"',
            source,
        )
        self.assertIn(
            "if (ROR_OGRE14 AND UNIX AND NOT APPLE)",
            source,
        )
        self.assertIn("tools/linux/RunRoR-ogre14", source)
        self.assertIn("StageLinuxRuntime.cmake", source)
        self.assertIn("${CONAN_RUNTIME_LIB_DIRS}", source)
        self.assertIn(
            '[=[set(ROR_LINUX_INSTALL_ROOT '
            '"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}")\n]=]',
            source,
        )
        self.assertIn("file(GET_RUNTIME_DEPENDENCIES", stager)
        self.assertIn("CONFLICTING_DEPENDENCIES_PREFIX", stager)
        self.assertIn("FOLLOW_SYMLINK_CHAIN", stager)
        for exclusion in (
            '"^/lib/"',
            '"^/lib64/"',
            '"^/usr/lib/"',
            '"^/usr/lib64/"',
        ):
            self.assertIn(exclusion, stager)
        for plugin in EXPECTED_PLUGINS:
            self.assertIn(plugin, stager)
        self.assertIn(
            "cmake_minimum_required(VERSION 3.16)",
            stager,
        )
        self.assertNotIn("cmake_path", stager)
        self.assertNotIn("cmake_path", CONTRACT_MODULE.read_text("utf-8"))
        self.assertNotIn("file(REAL_PATH", stager)
        self.assertNotIn(
            "file(REAL_PATH",
            CONTRACT_MODULE.read_text("utf-8"),
        )
        self.assertIn('"LC_ALL=C"', stager)
        self.assertIn(
            "ror_linux_ogre14_validate_installed_symlinks",
            stager,
        )

        legacy_install_contract = (
            '        set(PLUGINS_FOLDER "lib")\n'
            "        configure_file(plugins.cfg.in "
            "${CMAKE_CURRENT_BINARY_DIR}/plugins-install.cfg)\n"
            "        install(FILES "
            "${CMAKE_CURRENT_BINARY_DIR}/plugins-install.cfg "
            "DESTINATION . RENAME plugins.cfg)\n"
            "        install(PROGRAMS "
            "${CMAKE_SOURCE_DIR}/tools/linux/RunRoR DESTINATION .)\n"
            "        install(FILES "
            "${CMAKE_SOURCE_DIR}/tools/linux/.itch.toml DESTINATION .)\n"
            '        install(CODE "file(GLOB_RECURSE files '
            "${RUNTIME_OUTPUT_DIRECTORY}/*.so)\n"
            "                file(INSTALL \\${files} DESTINATION "
            "\\${CMAKE_INSTALL_PREFIX}/lib FOLLOW_SYMLINK_CHAIN)\")\n"
        )
        self.assertIn(legacy_install_contract, source)
        self.assertEqual(
            LEGACY_LAUNCHER.read_text(encoding="utf-8"),
            "#!/bin/sh\n"
            "export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:./lib/\n"
            './RoR "$@"\n',
        )

    def test_ogre14_launcher_is_cwd_independent_and_preserves_arguments(
        self,
    ) -> None:
        if os.name == "nt":
            self.skipTest(
                "the POSIX launcher execution contract runs in the Linux lane"
            )
        syntax = subprocess.run(
            ["sh", "-n", str(OGRE14_LAUNCHER)],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        self.assertEqual(
            syntax.returncode,
            0,
            msg=syntax.stdout + syntax.stderr,
        )

        with tempfile.TemporaryDirectory(
            prefix="ror-linux-launcher-contract-"
        ) as temporary:
            root = Path(temporary)
            package = root / "package with spaces"
            package.mkdir()
            (package / "lib").mkdir()
            launcher = package / "RunRoR"
            shutil.copy2(OGRE14_LAUNCHER, launcher)
            launcher.chmod(0o755)
            executable = package / "RoR"
            executable.write_text(
                "#!/bin/sh\n"
                "printf '%s\\n' \"$LD_LIBRARY_PATH\" \"$#\" \"$1\" \"$2\"\n",
                encoding="utf-8",
            )
            executable.chmod(0o755)
            environment = os.environ.copy()
            environment["LD_LIBRARY_PATH"] = "/existing/runtime"
            shell = shutil.which("dash") or shutil.which("sh") or "sh"
            result = subprocess.run(
                [shell, str(launcher), "first argument", "second"],
                cwd=root,
                env=environment,
                check=False,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            self.assertEqual(
                result.returncode,
                0,
                msg=result.stdout + result.stderr,
            )
            self.assertEqual(
                result.stdout.splitlines(),
                [
                    f"{(package / 'lib').resolve()}:/existing/runtime",
                    "2",
                    "first argument",
                    "second",
                ],
            )


if __name__ == "__main__":
    unittest.main()
