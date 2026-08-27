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
RUN_ROR_LAUNCHER = REPOSITORY_ROOT / "tools" / "linux" / "RunRoR"
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


def stage_plugin_chain(
    *,
    source_directory: Path,
    destination_directory: Path,
    plugin_name: str,
) -> subprocess.CompletedProcess[str]:
    """Stage one plugin family and record its resolved destination."""

    output = destination_directory.parent / "staged-plugin.txt"
    result = run_cmake(
        destination_directory.parent,
        "ror_linux_ogre14_stage_plugin_chain(\n"
        f'    "{source_directory.as_posix()}"\n'
        f'    "{destination_directory.as_posix()}"\n'
        f'    "{plugin_name}")\n'
        "ror_linux_ogre14_resolve_plugin(\n"
        "    staged_chain staged_real\n"
        f'    "{destination_directory.as_posix()}"\n'
        f'    "{plugin_name}")\n'
        "ror_linux_ogre14_validate_installed_plugins(\n"
        f'    "{destination_directory.as_posix()}" "{plugin_name}")\n'
        f'file(WRITE "{output.as_posix()}" '
        '"${staged_chain}\\n${staged_real}\\n")\n',
    )
    if result.returncode == 0:
        lines = output.read_text(encoding="utf-8").splitlines()
        result.staged_chain = tuple(lines[0].split(";"))
        result.staged_real = lines[1]
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

    @unittest.skipIf(
        os.name == "nt",
        "Linux plugin SONAME chain execution runs in the Linux lane",
    )
    def test_plugin_staging_builds_exact_chain_without_mutating_source(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory(
            prefix="ror-linux-plugin-stage-spaces-"
        ) as temporary:
            root = Path(temporary)
            source = root / "source package" / "lib" / "OGRE plugins"
            destination = root / "relocated package" / "lib" / "OGRE"
            source.mkdir(parents=True)
            destination.mkdir(parents=True)
            plugin_name = "Codec_FreeImage"
            source_real = source / f"{plugin_name}.so.14.5"
            source_bytes = b"contained-plugin-binary\x00fixture"
            source_real.write_bytes(source_bytes)
            source_link = source / f"{plugin_name}.so"
            source_link.symlink_to(source_real.name)

            source_entries_before = {
                path.name: (
                    "symlink" if path.is_symlink() else "file",
                    os.readlink(path) if path.is_symlink() else path.read_bytes(),
                )
                for path in source.iterdir()
            }
            result = stage_plugin_chain(
                source_directory=source,
                destination_directory=destination,
                plugin_name=plugin_name,
            )
            self.assertEqual(
                result.returncode,
                0,
                msg=result.stdout + result.stderr,
            )

            destination_unversioned = destination / f"{plugin_name}.so"
            destination_abi = destination / f"{plugin_name}.so.14.5"
            destination_real = destination / f"{plugin_name}.so.14.5.2"
            self.assertEqual(
                {path.name for path in destination.iterdir()},
                {
                    destination_unversioned.name,
                    destination_abi.name,
                    destination_real.name,
                },
            )
            self.assertTrue(destination_unversioned.is_symlink())
            self.assertTrue(destination_abi.is_symlink())
            self.assertFalse(destination_real.is_symlink())
            self.assertEqual(
                os.readlink(destination_unversioned),
                destination_abi.name,
            )
            self.assertEqual(
                os.readlink(destination_abi),
                destination_real.name,
            )
            self.assertFalse(os.path.isabs(os.readlink(destination_unversioned)))
            self.assertFalse(os.path.isabs(os.readlink(destination_abi)))
            self.assertEqual(destination_real.read_bytes(), source_bytes)
            self.assertEqual(
                native_path_text(result.staged_real),
                native_path_text(destination_real.resolve()),
            )
            self.assertEqual(
                {
                    native_path_text(path)
                    for path in result.staged_chain
                },
                {
                    native_path_text(destination_unversioned),
                    native_path_text(destination_abi),
                    native_path_text(destination_real),
                },
            )

            source_entries_after = {
                path.name: (
                    "symlink" if path.is_symlink() else "file",
                    os.readlink(path) if path.is_symlink() else path.read_bytes(),
                )
                for path in source.iterdir()
            }
            self.assertEqual(source_entries_after, source_entries_before)
            self.assertEqual(source_real.read_bytes(), source_bytes)
            self.assertEqual(os.readlink(source_link), source_real.name)

            destination_entries_before_duplicate = {
                path.name: (
                    "symlink" if path.is_symlink() else "file",
                    os.readlink(path) if path.is_symlink() else path.read_bytes(),
                )
                for path in destination.iterdir()
            }
            duplicate = stage_plugin_chain(
                source_directory=source,
                destination_directory=destination,
                plugin_name=plugin_name,
            )
            self.assertNotEqual(duplicate.returncode, 0)
            self.assertIn(
                "destination already contains",
                duplicate.stdout + duplicate.stderr,
            )
            destination_entries_after_duplicate = {
                path.name: (
                    "symlink" if path.is_symlink() else "file",
                    os.readlink(path) if path.is_symlink() else path.read_bytes(),
                )
                for path in destination.iterdir()
            }
            self.assertEqual(
                destination_entries_after_duplicate,
                destination_entries_before_duplicate,
            )

            canonical_source = root / "canonical source plugins"
            canonical_destination = root / "canonical relocated plugins"
            canonical_source.mkdir()
            canonical_destination.mkdir()
            canonical_real = (
                canonical_source / f"{plugin_name}.so.14.5.2"
            )
            canonical_real.write_bytes(source_bytes)
            canonical_abi = canonical_source / f"{plugin_name}.so.14.5"
            canonical_abi.symlink_to(canonical_real.name)
            canonical_unversioned = canonical_source / f"{plugin_name}.so"
            canonical_unversioned.symlink_to(canonical_abi.name)
            canonical = stage_plugin_chain(
                source_directory=canonical_source,
                destination_directory=canonical_destination,
                plugin_name=plugin_name,
            )
            self.assertEqual(
                canonical.returncode,
                0,
                msg=canonical.stdout + canonical.stderr,
            )
            self.assertEqual(
                (
                    canonical_destination / f"{plugin_name}.so.14.5.2"
                ).read_bytes(),
                source_bytes,
            )
            self.assertEqual(
                os.readlink(
                    canonical_destination / f"{plugin_name}.so.14.5"
                ),
                f"{plugin_name}.so.14.5.2",
            )
            self.assertEqual(os.readlink(canonical_abi), canonical_real.name)
            self.assertEqual(
                os.readlink(canonical_unversioned),
                canonical_abi.name,
            )

    @unittest.skipIf(
        os.name == "nt",
        "Linux plugin SONAME chain execution runs in the Linux lane",
    )
    def test_plugin_resolution_rejects_invalid_exact_chains(self) -> None:
        cases = ("broken", "cyclic", "escaped", "unexpected")
        for case in cases:
            with self.subTest(case=case), tempfile.TemporaryDirectory(
                prefix=f"ror-linux-plugin-{case}-"
            ) as temporary:
                root = Path(temporary)
                plugin_dir = root / "source plugins"
                plugin_dir.mkdir()
                plugin_name = "Plugin_ParticleFX"
                unversioned = plugin_dir / f"{plugin_name}.so"
                abi = plugin_dir / f"{plugin_name}.so.14.5"

                if case == "broken":
                    unversioned.symlink_to(abi.name)
                elif case == "cyclic":
                    unversioned.symlink_to(abi.name)
                    abi.symlink_to(unversioned.name)
                elif case == "escaped":
                    outside = root / "outside.so"
                    outside.write_bytes(b"outside")
                    unversioned.symlink_to(abi.name)
                    abi.symlink_to("../../outside.so")
                else:
                    abi.write_bytes(b"fixture")
                    unversioned.symlink_to(abi.name)
                    (plugin_dir / f"{plugin_name}.so.14.5.9").write_bytes(
                        b"unexpected"
                    )

                result = run_cmake(
                    root,
                    "ror_linux_ogre14_resolve_plugin(\n"
                    f'    chain real "{plugin_dir.as_posix()}" '
                    f'"{plugin_name}")\n',
                )
                self.assertNotEqual(result.returncode, 0)
                diagnostics = result.stdout + result.stderr
                if case == "cyclic":
                    self.assertIn("symlink cycle", diagnostics)
                elif case == "escaped":
                    self.assertIn("escapes its root", diagnostics)
                elif case == "unexpected":
                    self.assertIn("unexpected ABI entry", diagnostics)

    @unittest.skipUnless(
        os.name == "posix"
        and shutil.which("cc") is not None
        and shutil.which("readelf") is not None,
        "ELF SONAME proof requires a Linux compiler and readelf",
    )
    def test_plugin_canonicalization_preserves_embedded_abi_soname(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory(
            prefix="ror-linux-plugin-soname-"
        ) as temporary:
            root = Path(temporary)
            source = root / "source plugins"
            destination = root / "relocated plugins"
            source.mkdir()
            destination.mkdir()
            plugin_name = "Plugin_ParticleFX"
            source_code = root / "fixture.c"
            source_code.write_text(
                "int ror_plugin_fixture(void) { return 14; }\n",
                encoding="utf-8",
            )
            source_real = source / f"{plugin_name}.so.14.5"
            compiled = subprocess.run(
                [
                    shutil.which("cc") or "cc",
                    "-shared",
                    "-fPIC",
                    f"-Wl,-soname,{plugin_name}.so.14.5",
                    "-o",
                    str(source_real),
                    str(source_code),
                ],
                check=False,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            self.assertEqual(
                compiled.returncode,
                0,
                msg=compiled.stdout + compiled.stderr,
            )
            (source / f"{plugin_name}.so").symlink_to(source_real.name)

            result = stage_plugin_chain(
                source_directory=source,
                destination_directory=destination,
                plugin_name=plugin_name,
            )
            self.assertEqual(
                result.returncode,
                0,
                msg=result.stdout + result.stderr,
            )
            destination_abi = destination / f"{plugin_name}.so.14.5"
            destination_real = destination / f"{plugin_name}.so.14.5.2"
            metadata = subprocess.run(
                [shutil.which("readelf") or "readelf", "-d", destination_real],
                check=False,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            self.assertEqual(
                metadata.returncode,
                0,
                msg=metadata.stdout + metadata.stderr,
            )
            self.assertRegex(
                metadata.stdout,
                rf"\(SONAME\).*\[{plugin_name}\.so\.14\.5\]",
            )
            self.assertTrue(destination_abi.is_symlink())
            self.assertEqual(os.readlink(destination_abi), destination_real.name)

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
        self.assertIn(
            'set(_ror_linux_installed_game_executable "RoR-Ogre14")',
            source,
        )
        self.assertIn(
            "ROR_LINUX_INSTALLED_EXECUTABLE_NAME",
            stager,
        )
        self.assertIn(
            "ror_ogre14_cmakedeps_runtime_search_dirs(",
            source,
        )
        self.assertIn(
            "ror_ogre14_install_set_list_code(",
            source,
        )
        self.assertNotIn("CONAN_RUNTIME_LIB_DIRS", source)
        self.assertIn(
            '[=[set(ROR_LINUX_INSTALL_ROOT '
            '"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}")\n]=]',
            source,
        )
        self.assertIn("file(GET_RUNTIME_DEPENDENCIES", stager)
        self.assertIn("CONFLICTING_DEPENDENCIES_PREFIX", stager)
        self.assertIn("FOLLOW_SYMLINK_CHAIN", stager)
        self.assertIn(
            "ror_linux_ogre14_stage_plugin_chain(",
            stager,
        )
        self.assertNotIn("_ror_plugin_sources", stager)
        self.assertNotIn(
            'file(COPY\n        "${_ror_plugin_source}"',
            stager,
        )
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

        standard_install_contract = (
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
        self.assertIn(standard_install_contract, source)
        expected_launcher = (
            "#!/bin/sh\n"
            "\n"
            "ror_launcher_dir=$(\n"
            "    CDPATH='' cd -P \"$(dirname \"$0\")\" 2>/dev/null && pwd -P\n"
            ") || exit 1\n"
            "\n"
            'if [ -n "${LD_LIBRARY_PATH:-}" ]; then\n'
            '    LD_LIBRARY_PATH="${ror_launcher_dir}/lib:${LD_LIBRARY_PATH}"\n'
            "else\n"
            '    LD_LIBRARY_PATH="${ror_launcher_dir}/lib"\n'
            "fi\n"
            "export LD_LIBRARY_PATH\n"
            "\n"
            'exec "${ror_launcher_dir}/RoR" "$@"\n'
        )
        self.assertEqual(
            RUN_ROR_LAUNCHER.read_text(encoding="utf-8"),
            expected_launcher,
        )
        self.assertEqual(
            OGRE14_LAUNCHER.read_text(encoding="utf-8"),
            expected_launcher,
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
