#!/usr/bin/env python3
"""Runtime contract tests for platform-specific OGRE 14 configuration."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
PLATFORM_MODULE = REPOSITORY_ROOT / "cmake" / "Ogre14Platform.cmake"
PLUGIN_TEMPLATE = REPOSITORY_ROOT / "source" / "main" / "plugins.cfg.in"


def native_path_text(value: str | Path) -> str:
    """Normalize CMake's slash style to the current host path syntax."""

    return os.path.normcase(os.path.normpath(os.fspath(value)))


def select_lockfile(system_name: str, processor: str) -> subprocess.CompletedProcess:
    with tempfile.TemporaryDirectory(prefix="ror-ogre14-platform-") as directory:
        output_path = Path(directory) / "selected.txt"
        script_path = Path(directory) / "select.cmake"
        script_path.write_text(
            f'include("{PLATFORM_MODULE.as_posix()}")\n'
            "ror_select_ogre14_lockfile(\n"
            f'    selected "{system_name}" "{processor}")\n'
            f'file(WRITE "{output_path.as_posix()}" "${{selected}}")\n',
            encoding="utf-8",
        )
        result = subprocess.run(
            ["cmake", "-P", str(script_path)],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        if result.returncode == 0:
            result.selected_lockfile = output_path.read_text(encoding="utf-8")
        return result


def select_runtime_contract(
    system_name: str,
    processor: str,
) -> subprocess.CompletedProcess:
    with tempfile.TemporaryDirectory(prefix="ror-ogre14-runtime-") as directory:
        output_path = Path(directory) / "selected.txt"
        script_path = Path(directory) / "select.cmake"
        script_path.write_text(
            f'include("{PLATFORM_MODULE.as_posix()}")\n'
            "ror_ogre14_runtime_contract(\n"
            f'    runtime "{system_name}" "{processor}")\n'
            f'file(WRITE "{output_path.as_posix()}"\n'
            '    "${runtime_PACKAGE_PLUGIN_SUBDIR}\\n"\n'
            '    "${runtime_RENDERER_PLUGIN}\\n"\n'
            '    "${runtime_INSTALL_PLUGIN_FOLDER}\\n"\n'
            '    "${runtime_ACTIVE_PLUGINS}\\n"\n'
            '    "${runtime_PLUGIN_BINARIES_USE_DEBUG_SUFFIX}\\n")\n',
            encoding="utf-8",
        )
        result = subprocess.run(
            ["cmake", "-P", str(script_path)],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        if result.returncode == 0:
            (
                result.package_plugin_subdir,
                result.renderer_plugin,
                result.install_plugin_folder,
                result.active_plugins,
                result.plugin_binaries_use_debug_suffix,
            ) = output_path.read_text(encoding="utf-8").splitlines()
        return result


def render_plugins_config(
    system_name: str,
    processor: str,
    *,
    debug: bool,
) -> subprocess.CompletedProcess:
    with tempfile.TemporaryDirectory(prefix="ror-ogre14-plugins-") as directory:
        output_path = (
            Path(directory) / ("plugins_d.cfg" if debug else "plugins.cfg")
        )
        script_path = Path(directory) / "render.cmake"
        script_path.write_text(
            f'include("{PLATFORM_MODULE.as_posix()}")\n'
            "ror_ogre14_runtime_contract(\n"
            f'    runtime "{system_name}" "{processor}")\n'
            "ror_ogre14_plugin_template_contract(\n"
            '    comments "${runtime_RENDERER_PLUGIN}")\n'
            'set(PLUGINS_FOLDER "${runtime_INSTALL_PLUGIN_FOLDER}")\n'
            'set(CFG_COMMENT_RENDERSYSTEM_D3D9 "${comments_D3D9}")\n'
            'set(CFG_COMMENT_RENDERSYSTEM_D3D11 "${comments_D3D11}")\n'
            'set(CFG_COMMENT_RENDERSYSTEM_GL "${comments_GL}")\n'
            'set(CFG_COMMENT_RENDERSYSTEM_GL3PLUS "${comments_GL3PLUS}")\n'
            'set(CFG_COMMENT_RENDERSYSTEM_METAL "${comments_METAL}")\n'
            'set(CFG_COMMENT_PLUGIN_CG "${comments_CG}")\n'
            'set(CFG_OGRE_PLUGIN_CAELUM "# disabled")\n'
            'set(CFG_OGRE_PLUGIN_CAELUM_D "# disabled")\n'
            # OGRE 14 applies `_d` when it resolves the physical Windows DLL;
            # both config files must therefore contain unsuffixed tokens.
            f'set(_template "{PLUGIN_TEMPLATE.as_posix()}")\n'
            f'configure_file("${{_template}}" "{output_path.as_posix()}" '
            "@ONLY)\n",
            encoding="utf-8",
        )
        result = subprocess.run(
            ["cmake", "-P", str(script_path)],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        if result.returncode == 0:
            result.plugins_config = output_path.read_text(encoding="utf-8")
        return result


def select_package_roots(
    *,
    multi_config: bool,
    build_type: str,
    configuration_types: str,
    package_variables: dict[str, str],
    raw_package_variables: dict[str, str] | None = None,
    package_file_variables: dict[str, str] | None = None,
) -> subprocess.CompletedProcess:
    with tempfile.TemporaryDirectory(prefix="ror-ogre14-roots-") as directory:
        temporary_root = Path(directory)
        package_paths = {
            label: temporary_root / label
            for label in set(package_variables.values())
        }
        for package_path in package_paths.values():
            package_path.mkdir()
        file_paths = {
            label: temporary_root / label
            for label in set((package_file_variables or {}).values())
        }
        for file_path in file_paths.values():
            file_path.write_text("not a package directory", encoding="utf-8")
        variable_lines = "".join(
            f'set({variable} "{package_paths[label].as_posix()}")\n'
            for variable, label in sorted(package_variables.items())
        )
        variable_lines += "".join(
            f'set({variable} "{value}")\n'
            for variable, value in sorted(
                (raw_package_variables or {}).items()
            )
        )
        variable_lines += "".join(
            f'set({variable} "{file_paths[label].as_posix()}")\n'
            for variable, label in sorted(
                (package_file_variables or {}).items()
            )
        )
        output_path = temporary_root / "selected.txt"
        script_path = temporary_root / "select.cmake"
        script_path.write_text(
            f'include("{PLATFORM_MODULE.as_posix()}")\n'
            f"{variable_lines}"
            "ror_ogre14_package_roots(\n"
            f'    roots {"ON" if multi_config else "OFF"} '
            f'"{build_type}" "{configuration_types}")\n'
            f'file(WRITE "{output_path.as_posix()}"\n'
            '    "${roots_RELEASE}\\n"\n'
            '    "${roots_DEBUG}\\n"\n'
            '    "${roots_MEDIA}\\n")\n',
            encoding="utf-8",
        )
        result = subprocess.run(
            ["cmake", "-P", str(script_path)],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        result.package_paths = {
            label: native_path_text(path)
            for label, path in package_paths.items()
        }
        result.package_file_paths = {
            label: native_path_text(path)
            for label, path in file_paths.items()
        }
        if result.returncode == 0:
            result.package_roots = tuple(
                native_path_text(value)
                for value in output_path.read_text(
                    encoding="utf-8"
                ).splitlines()
            )
        return result


def select_media_root(
    *,
    system_name: str,
    layout: str,
    missing_components: tuple[str, ...] = (),
) -> subprocess.CompletedProcess:
    with tempfile.TemporaryDirectory(prefix="ror-ogre14-media-") as directory:
        temporary_root = Path(directory)
        package_root = temporary_root / "package"
        if layout == "versioned-share":
            media_root = package_root / "share" / "OGRE-14.5" / "Media"
        elif layout == "package-root":
            media_root = package_root / "Media"
        else:
            raise ValueError(f"unsupported fixture layout: {layout}")
        package_root.mkdir()
        for component in ("Main", "RTShaderLib", "Terrain"):
            if component not in missing_components:
                (media_root / component).mkdir(parents=True, exist_ok=True)
        output_path = temporary_root / "selected.txt"
        script_path = temporary_root / "select-media.cmake"
        script_path.write_text(
            f'include("{PLATFORM_MODULE.as_posix()}")\n'
            "ror_ogre14_media_root(\n"
            f'    media "{package_root.as_posix()}" "{system_name}")\n'
            f'file(WRITE "{output_path.as_posix()}" "${{media}}\\n")\n',
            encoding="utf-8",
        )
        result = subprocess.run(
            ["cmake", "-P", str(script_path)],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        result.expected_media_root = native_path_text(media_root)
        if result.returncode == 0:
            result.media_root = native_path_text(
                output_path.read_text(encoding="utf-8").strip()
            )
        return result


def active_config_values(config: str, key: str) -> list[str]:
    prefix = f"{key}="
    return [
        stripped.partition("=")[2].strip()
        for line in config.splitlines()
        if (stripped := line.strip()).startswith(prefix)
        and not stripped.startswith("#")
    ]


class Ogre14PlatformContractTests(unittest.TestCase):
    def test_supported_platforms_select_exact_pinned_lockfiles(self) -> None:
        cases = (
            (
                "Darwin",
                "arm64",
                "cmake/conan/locks/ror-ogre14-macos-arm64-release.lock",
            ),
            (
                "Darwin",
                "aarch64",
                "cmake/conan/locks/ror-ogre14-macos-arm64-release.lock",
            ),
            (
                "Linux",
                "x86_64",
                "cmake/conan/locks/ror-ogre14-linux-x86_64-release.lock",
            ),
            (
                "Linux",
                "AMD64",
                "cmake/conan/locks/ror-ogre14-linux-x86_64-release.lock",
            ),
            (
                "Windows",
                "AMD64",
                "cmake/conan/locks/ror-ogre14-windows-x86_64-release.lock",
            ),
            (
                "Windows",
                "x86_64",
                "cmake/conan/locks/ror-ogre14-windows-x86_64-release.lock",
            ),
        )
        for system_name, processor, expected in cases:
            with self.subTest(system=system_name, processor=processor):
                result = select_lockfile(system_name, processor)
                self.assertEqual(
                    result.returncode,
                    0,
                    msg=result.stdout + result.stderr,
                )
                self.assertEqual(result.selected_lockfile, expected)

    def test_unsupported_targets_fail_closed(self) -> None:
        for system_name, processor in (
            ("Darwin", "x86_64"),
            ("Linux", "arm64"),
            ("Windows", "ARM64"),
            ("FreeBSD", "x86_64"),
            ("", ""),
        ):
            with self.subTest(system=system_name, processor=processor):
                result = select_lockfile(system_name, processor)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(
                    "has no pinned dependency graph",
                    result.stdout + result.stderr,
                )

    def test_supported_platforms_select_exact_runtime_contract(self) -> None:
        cases = (
            (
                "Darwin",
                "arm64",
                (
                    "lib/OGRE",
                    "RenderSystem_GL3Plus",
                    "../PlugIns",
                    (
                        "Codec_FreeImage;RenderSystem_GL3Plus;"
                        "Plugin_ParticleFX;Plugin_OctreeSceneManager"
                    ),
                    "OFF",
                ),
            ),
            (
                "Darwin",
                "aarch64",
                (
                    "lib/OGRE",
                    "RenderSystem_GL3Plus",
                    "../PlugIns",
                    (
                        "Codec_FreeImage;RenderSystem_GL3Plus;"
                        "Plugin_ParticleFX;Plugin_OctreeSceneManager"
                    ),
                    "OFF",
                ),
            ),
            (
                "Linux",
                "x86_64",
                (
                    "lib/OGRE",
                    "RenderSystem_GL3Plus",
                    "lib/OGRE",
                    (
                        "Codec_FreeImage;RenderSystem_GL3Plus;"
                        "Plugin_ParticleFX;Plugin_OctreeSceneManager"
                    ),
                    "OFF",
                ),
            ),
            (
                "Linux",
                "AMD64",
                (
                    "lib/OGRE",
                    "RenderSystem_GL3Plus",
                    "lib/OGRE",
                    (
                        "Codec_FreeImage;RenderSystem_GL3Plus;"
                        "Plugin_ParticleFX;Plugin_OctreeSceneManager"
                    ),
                    "OFF",
                ),
            ),
            (
                "Windows",
                "AMD64",
                (
                    "bin",
                    "RenderSystem_Direct3D11",
                    ".",
                    (
                        "Codec_FreeImage;RenderSystem_Direct3D11;"
                        "Plugin_ParticleFX;Plugin_OctreeSceneManager"
                    ),
                    "ON",
                ),
            ),
            (
                "Windows",
                "x86_64",
                (
                    "bin",
                    "RenderSystem_Direct3D11",
                    ".",
                    (
                        "Codec_FreeImage;RenderSystem_Direct3D11;"
                        "Plugin_ParticleFX;Plugin_OctreeSceneManager"
                    ),
                    "ON",
                ),
            ),
        )
        for system_name, processor, expected in cases:
            with self.subTest(system=system_name, processor=processor):
                result = select_runtime_contract(system_name, processor)
                self.assertEqual(
                    result.returncode,
                    0,
                    msg=result.stdout + result.stderr,
                )
                actual = (
                    result.package_plugin_subdir,
                    result.renderer_plugin,
                    result.install_plugin_folder,
                    result.active_plugins,
                    result.plugin_binaries_use_debug_suffix,
                )
                self.assertEqual(actual, expected)

    def test_unsupported_targets_have_no_runtime_contract(self) -> None:
        for system_name, processor in (
            ("Darwin", "x86_64"),
            ("Linux", "arm64"),
            ("Windows", "ARM64"),
            ("FreeBSD", "x86_64"),
            ("", ""),
        ):
            with self.subTest(system=system_name, processor=processor):
                result = select_runtime_contract(system_name, processor)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(
                    "has no runtime contract",
                    result.stdout + result.stderr,
                )

    def test_generated_plugin_configs_activate_only_platform_renderer(
        self,
    ) -> None:
        cases = (
            (
                "Darwin",
                "arm64",
                "../PlugIns",
                "RenderSystem_GL3Plus",
                False,
            ),
            (
                "Linux",
                "x86_64",
                "lib/OGRE",
                "RenderSystem_GL3Plus",
                False,
            ),
            (
                "Windows",
                "AMD64",
                ".",
                "RenderSystem_Direct3D11",
                True,
            ),
        )
        for system_name, processor, folder, renderer, _uses_debug_suffix in cases:
            for debug in (False, True):
                with self.subTest(
                    system=system_name,
                    processor=processor,
                    debug=debug,
                ):
                    result = render_plugins_config(
                        system_name,
                        processor,
                        debug=debug,
                    )
                    self.assertEqual(
                        result.returncode,
                        0,
                        msg=result.stdout + result.stderr,
                    )
                    self.assertEqual(
                        active_config_values(
                            result.plugins_config,
                            "PluginFolder",
                        ),
                        [folder],
                    )
                    self.assertEqual(
                        active_config_values(result.plugins_config, "Plugin"),
                        [
                            "Codec_FreeImage",
                            renderer,
                            "Plugin_ParticleFX",
                            "Plugin_OctreeSceneManager",
                        ],
                    )

    def test_conan_package_roots_are_configuration_specific(self) -> None:
        result = select_package_roots(
            multi_config=True,
            build_type="",
            configuration_types="Debug;Release",
            package_variables={
                "ogre3d_PACKAGE_FOLDER_RELEASE": "release",
                "ogre3d_PACKAGE_FOLDER_DEBUG": "debug",
            },
        )
        self.assertEqual(
            result.returncode,
            0,
            msg=result.stdout + result.stderr,
        )
        self.assertEqual(
            result.package_roots,
            (
                result.package_paths["release"],
                result.package_paths["debug"],
                result.package_paths["release"],
            ),
        )

        for build_type in ("Release", "Debug", "RelWithDebInfo"):
            with self.subTest(build_type=build_type):
                variable = (
                    "ogre3d_PACKAGE_FOLDER_"
                    f"{build_type.upper()}"
                )
                single = select_package_roots(
                    multi_config=False,
                    build_type=build_type,
                    configuration_types="",
                    package_variables={variable: "current"},
                )
                self.assertEqual(
                    single.returncode,
                    0,
                    msg=single.stdout + single.stderr,
                )
                expected = single.package_paths["current"]
                self.assertEqual(
                    single.package_roots,
                    (expected, expected, expected),
                )

    def test_invalid_multi_config_package_roots_fail_closed(self) -> None:
        cases = (
            (
                "empty configuration set",
                "",
                {
                    "ogre3d_PACKAGE_FOLDER_RELEASE": "release",
                    "ogre3d_PACKAGE_FOLDER_DEBUG": "debug",
                },
            ),
            (
                "missing Debug package",
                "Debug;Release",
                {
                    "ogre3d_PACKAGE_FOLDER_RELEASE": "release",
                },
            ),
            (
                "shared package root",
                "Debug;Release",
                {
                    "ogre3d_PACKAGE_FOLDER_RELEASE": "shared",
                    "ogre3d_PACKAGE_FOLDER_DEBUG": "shared",
                },
            ),
            (
                "unsupported configuration",
                "Debug;Release;RelWithDebInfo",
                {
                    "ogre3d_PACKAGE_FOLDER_RELEASE": "release",
                    "ogre3d_PACKAGE_FOLDER_DEBUG": "debug",
                },
            ),
        )
        for label, configuration_types, package_variables in cases:
            with self.subTest(case=label):
                result = select_package_roots(
                    multi_config=True,
                    build_type="",
                    configuration_types=configuration_types,
                    package_variables=package_variables,
                )
                self.assertNotEqual(result.returncode, 0)

    def test_invalid_single_config_package_roots_fail_closed(self) -> None:
        empty_build_type = select_package_roots(
            multi_config=False,
            build_type="",
            configuration_types="",
            package_variables={},
        )
        self.assertNotEqual(empty_build_type.returncode, 0)

        relative_root = select_package_roots(
            multi_config=False,
            build_type="Release",
            configuration_types="",
            package_variables={},
            raw_package_variables={
                "ogre3d_PACKAGE_FOLDER_RELEASE": "relative/package",
            },
        )
        self.assertNotEqual(relative_root.returncode, 0)

        file_root = select_package_roots(
            multi_config=False,
            build_type="Release",
            configuration_types="",
            package_variables={},
            package_file_variables={
                "ogre3d_PACKAGE_FOLDER_RELEASE": "package-file",
            },
        )
        self.assertNotEqual(file_root.returncode, 0)

        missing_variable = select_package_roots(
            multi_config=False,
            build_type="Debug",
            configuration_types="",
            package_variables={},
        )
        self.assertNotEqual(missing_variable.returncode, 0)

    def test_media_layout_is_exact_for_each_native_platform(self) -> None:
        cases = (
            ("Linux", "versioned-share"),
            ("Darwin", "package-root"),
            ("Windows", "package-root"),
        )
        for system_name, layout in cases:
            with self.subTest(system_name=system_name):
                result = select_media_root(
                    system_name=system_name,
                    layout=layout,
                )
                self.assertEqual(
                    result.returncode,
                    0,
                    msg=result.stdout + result.stderr,
                )
                self.assertEqual(
                    result.media_root,
                    result.expected_media_root,
                )

    def test_wrong_or_incomplete_media_layout_fails_closed(self) -> None:
        cases = (
            ("Linux", "package-root", ()),
            ("Darwin", "versioned-share", ()),
            ("Windows", "versioned-share", ()),
            ("Linux", "versioned-share", ("RTShaderLib",)),
            ("Windows", "package-root", ("Main", "Terrain")),
        )
        for system_name, layout, missing in cases:
            with self.subTest(
                system_name=system_name,
                layout=layout,
                missing=missing,
            ):
                result = select_media_root(
                    system_name=system_name,
                    layout=layout,
                    missing_components=missing,
                )
                self.assertNotEqual(result.returncode, 0)
                diagnostics = result.stdout + result.stderr
                self.assertIn("pinned OGRE 14 package media root", diagnostics)
                for component in missing:
                    self.assertIn(component, diagnostics)

        unsupported = select_media_root(
            system_name="FreeBSD",
            layout="package-root",
        )
        self.assertNotEqual(unsupported.returncode, 0)
        self.assertIn(
            "no media layout for FreeBSD",
            unsupported.stdout + unsupported.stderr,
        )

    def test_macos_stager_owns_both_plugin_config_names(self) -> None:
        stager = (
            REPOSITORY_ROOT
            / "cmake"
            / "macos"
            / "StageMacOSBundle.cmake"
        ).read_text(encoding="utf-8")
        workflow = (
            REPOSITORY_ROOT
            / ".github"
            / "workflows"
            / "macos-native.yml"
        ).read_text(encoding="utf-8")
        self.assertIn(
            "foreach(_plugins_config IN ITEMS plugins.cfg plugins_d.cfg)",
            stager,
        )
        self.assertIn(
            'test -L "$app/Contents/MacOS/plugins_d.cfg"',
            workflow,
        )
        self.assertIn(
            '"$app/Contents/Resources/plugins_d.cfg"',
            workflow,
        )
        self.assertIn(
            "ROR_CONTENT directory contains no regular files",
            stager,
        )


if __name__ == "__main__":
    unittest.main()
