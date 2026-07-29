#!/usr/bin/env python3
"""Static contract tests for the isolated OGRE 14 native CI workflow."""

from __future__ import annotations

from pathlib import Path
import re
import subprocess
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = REPOSITORY_ROOT / ".github" / "workflows" / "ogre14-native.yml"
MACOS_WORKFLOW = (
    REPOSITORY_ROOT / ".github" / "workflows" / "macos-native.yml"
)
AUDITOR = REPOSITORY_ROOT / "tools" / "ogre14_runtime_audit.py"
RENDER_SMOKE = (
    REPOSITORY_ROOT / "tools" / "ogre14_native_runtime_smoke.py"
)
CONAN_SOURCE_FALLBACK = (
    'core.sources:download_urls=["origin", '
    '"https://c3i.jfrog.io/artifactory/conan-center-backup-sources/"]'
)

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
EXPECTED_SCRIPT_MARKERS = (
    "[RoR|CI|BundleSmoke] START",
    "[RoR|CI|BundleSmoke] PASS frames=10",
)


class Ogre14NativeWorkflowContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.text = WORKFLOW.read_text(encoding="utf-8")
        cls.macos_text = MACOS_WORKFLOW.read_text(encoding="utf-8")
        cls.auditor_text = AUDITOR.read_text(encoding="utf-8")
        cls.render_smoke_text = RENDER_SMOKE.read_text(encoding="utf-8")

    def assert_conan_source_fallback_contract(self, text: str) -> None:
        self.assertEqual(text.count("core.sources:download_urls="), 1)
        self.assertEqual(text.count(CONAN_SOURCE_FALLBACK), 1)
        self.assertNotIn("tools.files.download:verify=False", text)
        self.assertNotIn("tools.files.download:verify=false", text)

    def test_workflow_is_isolated_read_only_and_cancellable(self) -> None:
        text = self.text
        self.assertIn("name: OGRE 14 native Release", text)
        self.assertIn("branches: [master]", text)
        self.assertIn("pull_request:", text)
        self.assertIn("workflow_dispatch:", text)
        self.assertIn("contents: read", text)
        self.assertIn("group: ogre14-native-${{ github.ref }}", text)
        self.assertIn("cancel-in-progress: true", text)
        self.assertNotIn("secrets.", text)
        for mutation in ("butler", "git push", "gh release", "npm publish"):
            with self.subTest(mutation=mutation):
                self.assertNotIn(mutation, text.casefold())

    def test_matrix_pins_both_native_release_targets(self) -> None:
        text = self.text
        required = (
            "runner: ubuntu-22.04",
            "platform: linux-x86_64",
            "profile: cmake/conan/profiles/linux-x86_64-release",
            "compiler: gcc11",
            "cc: gcc-11",
            "cxx: g++-11",
            "runner: windows-2022",
            "platform: windows-x86_64",
            "profile: cmake/conan/profiles/windows-x86_64-release",
            "compiler: msvc1944",
            "toolset: \"14.44\"",
            "Version 19\\\\.44\\\\.",
            "timeout-minutes: 180",
        )
        for contract in required:
            with self.subTest(contract=contract):
                self.assertIn(contract, text)
        self.assertNotIn("runner: windows-2025", text)

    def test_conan_graph_and_cache_are_locked_and_platform_isolated(
        self,
    ) -> None:
        text = self.text
        self.assertIn("conan==2.31.1", text)
        self.assertIn("https://center2.conan.io", text)
        self.assertIn(
            "https://nexus.anotherfoxguy.com/repository/rigs-of-rods/",
            text,
        )
        self.assertIn(
            "CONAN_HOME: ${{ github.workspace }}/.ci-conan/"
            "${{ matrix.platform }}",
            text,
        )
        self.assertIn("cmake/conan/locks/*.lock", text)
        self.assertIn("cmake/conan/profiles/*", text)
        self.assertIn("cmake/conan/recipes/**", text)
        self.assertIn("assert_ogre14_app_graph.py", text)
        self.assertIn('--platform "${{ matrix.platform }}"', text)
        self.assertIn("-DCONAN_HOST_PROFILE=", text)
        self.assertIn("-DCONAN_BUILD_PROFILE=", text)
        self.assertIn(
            'conan cache clean "*" --source --build --download',
            text,
        )
        self.assertNotIn("restore-keys:", text)

    def test_conan_source_fallback_is_exact_and_cross_platform(self) -> None:
        for workflow, text in (
            (WORKFLOW.name, self.text),
            (MACOS_WORKFLOW.name, self.macos_text),
        ):
            with self.subTest(workflow=workflow):
                self.assert_conan_source_fallback_contract(text)

    def test_conan_source_fallback_rejects_unsafe_variants(self) -> None:
        unsafe_variants = {
            "backup before origin": CONAN_SOURCE_FALLBACK.replace(
                '["origin", "https://',
                '["https://',
            ).replace(
                'backup-sources/"]',
                'backup-sources/", "origin"]',
            ),
            "origin omitted": CONAN_SOURCE_FALLBACK.replace(
                '["origin", ',
                "[",
            ),
            "unencrypted backup": CONAN_SOURCE_FALLBACK.replace(
                "https://c3i.jfrog.io",
                "http://c3i.jfrog.io",
            ),
        }
        for name, unsafe in unsafe_variants.items():
            with self.subTest(variant=name):
                mutated = self.text.replace(CONAN_SOURCE_FALLBACK, unsafe)
                with self.assertRaises(AssertionError):
                    self.assert_conan_source_fallback_contract(mutated)

        with self.assertRaises(AssertionError):
            self.assert_conan_source_fallback_contract(
                self.text
                + "\ncore.sources:download_urls=[\"origin\"]\n"
            )
        with self.assertRaises(AssertionError):
            self.assert_conan_source_fallback_contract(
                self.text + "\ntools.files.download:verify=False\n"
            )

    def test_local_conan_recipe_bytes_are_platform_stable(self) -> None:
        listed = subprocess.run(
            [
                "git",
                "ls-files",
                "--",
                "cmake/conan/recipes",
                "cmake/conan/locks/*.lock",
            ],
            cwd=REPOSITORY_ROOT,
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(
            listed.returncode,
            0,
            msg=listed.stdout + listed.stderr,
        )
        tracked_inputs = tuple(listed.stdout.splitlines())
        self.assertTrue(tracked_inputs)
        for relative_path in tracked_inputs:
            with self.subTest(path=relative_path):
                result = subprocess.run(
                    [
                        "git",
                        "check-attr",
                        "text",
                        "eol",
                        "--",
                        relative_path,
                    ],
                    cwd=REPOSITORY_ROOT,
                    check=False,
                    capture_output=True,
                    text=True,
                )
                self.assertEqual(
                    result.returncode,
                    0,
                    msg=result.stdout + result.stderr,
                )
                self.assertIn(
                    f"{relative_path}: text: set",
                    result.stdout,
                )
                self.assertIn(
                    f"{relative_path}: eol: lf",
                    result.stdout,
                )
        active_patches = (
            (
                "cmake/conan/recipes/ogre3d/patches/14.5.2/"
                "relocatable-install-paths.patch"
            ),
            (
                "cmake/conan/recipes/ogre3d/patches/14.5.2/"
                "bounds-safe-shadow-texture-projectors.patch"
            ),
            (
                "cmake/conan/recipes/ogre3d/patches/14.5.2/"
                "defer-glsl-program-validation.patch"
            ),
            "cmake/conan/recipes/mygui/patches/3.4.0/ogre14-api.patch",
        )
        for relative_path in active_patches:
            with self.subTest(patch=relative_path):
                result = subprocess.run(
                    [
                        "git",
                        "check-attr",
                        "whitespace",
                        "--",
                        relative_path,
                    ],
                    cwd=REPOSITORY_ROOT,
                    check=False,
                    capture_output=True,
                    text=True,
                )
                self.assertEqual(
                    result.returncode,
                    0,
                    msg=result.stdout + result.stderr,
                )
                self.assertIn(
                    f"{relative_path}: whitespace: unset",
                    result.stdout,
                )

    def test_build_install_relocation_and_audit_are_mandatory(self) -> None:
        text = self.text
        required = (
            "-DCMAKE_BUILD_TYPE=Release",
            "-DROR_BUILD_TESTS=ON",
            "-DROR_CREATE_CONTENT_FOLDER=ON",
            "-DROR_OGRE14=ON",
            "ctest",
            "cmake --install",
            "cmake -E rename",
            "tools/ogre14_runtime_audit.py",
            "--load-plugins",
            "--smoke",
            "--forbidden-prefix \"$GITHUB_WORKSPACE\"",
            "--forbidden-prefix \"$CONAN_HOME\"",
            "xvfb-run",
            "GALLIUM_DRIVER: llvmpipe",
            "LIBGL_ALWAYS_SOFTWARE: \"1\"",
        )
        for contract in required:
            with self.subTest(contract=contract):
                self.assertIn(contract, text)
        self.assertEqual(text.count("cmake --install"), 1)
        self.assertEqual(text.count("cmake -E rename"), 1)

    def test_real_native_render_probes_are_mandatory_and_isolated(
        self,
    ) -> None:
        text = self.text
        required = (
            "tools/ogre14_native_runtime_smoke.py",
            "Render ten frames with relocated Linux GL3Plus",
            "Render ten frames with relocated Windows D3D11",
            "ROR_D0_SCENE_HOME:",
            "--root \"${GITHUB_WORKSPACE}/artifacts/runtime-"
            "${{ matrix.platform }}\"",
            "--log-root \"$ROR_D0_SCENE_HOME\"",
            "--timeout 120",
            "+extension GLX +render -noreset",
            "native-runtime-smoke-${{ matrix.platform }}",
        )
        for contract in required:
            with self.subTest(contract=contract):
                self.assertIn(contract, text)
        self.assertEqual(
            text.count("python tools/ogre14_native_runtime_smoke.py"),
            2,
        )
        self.assertNotIn("MESA_LOADER_DRIVER_OVERRIDE", text)
        self.assertNotIn("continue-on-error:", text)
        self.assertNotRegex(
            text.casefold(),
            r"(?:skip|ignore).*(?:renderer|render probe)",
        )

        smoke = self.render_smoke_text
        self.assertIn('SCRIPT_NAME = "example_ci_bundle_smoke.as"', smoke)
        for marker in EXPECTED_SCRIPT_MARKERS:
            with self.subTest(marker=marker):
                self.assertIn(marker, smoke)
        self.assertIn('"GALLIUM_DRIVER"] = "llvmpipe"', smoke)
        self.assertIn('"LIBGL_ALWAYS_SOFTWARE"] = "1"', smoke)
        self.assertIn("WINDOWS_ENGINE_REQUIRED_PATTERNS", smoke)
        self.assertIn("COMMON_ENGINE_REQUIRED_MARKERS", smoke)
        self.assertIn("require_fresh_log(", smoke)

    def test_plugin_contract_is_frozen_to_literal_release_sets(self) -> None:
        auditor = self.auditor_text
        for platform, plugins in EXPECTED_PLUGINS.items():
            with self.subTest(platform=platform):
                self.assertIn(f'"{platform}": (', auditor)
                for plugin in plugins:
                    self.assertIn(f'"{plugin}"', auditor)
                self.assertIn(
                    f'"{platform}": '
                    f'"{EXPECTED_PLUGIN_FOLDERS[platform]}"',
                    auditor,
                )
        self.assertIn('f"{plugin}.so.14.5"', auditor)
        self.assertIn('f"{plugin}.so.14.5.2"', auditor)
        self.assertIn('[dumpbin, "/nologo", "/imports"', auditor)

    def test_failure_diagnostics_and_verified_runtime_are_artifacts(
        self,
    ) -> None:
        text = self.text
        self.assertIn("if: always()", text)
        self.assertIn("actions/upload-artifact@v7", text)
        self.assertIn("runtime-${{ matrix.platform }}", text)
        self.assertIn("logs-${{ matrix.platform }}", text)
        self.assertIn("runtime-audit.json", text)
        self.assertIn("runtime-audit.log", text)
        self.assertIn("native-runtime-smoke-${{ matrix.platform }}", text)
        self.assertIn("conan-graph.json", text)
        self.assertIn("LastTest.log", text)
        self.assertIn("retention-days: 14", text)

    def test_workflow_does_not_touch_recorder_paths(self) -> None:
        lowered = self.text.casefold()
        self.assertNotIn("source/main/worldmodel", lowered)
        self.assertNotIn("tests/worldmodel", lowered)
        self.assertIsNone(re.search(r"\bgit\s+(?:clean|reset|restore)\b", lowered))


if __name__ == "__main__":
    unittest.main()
