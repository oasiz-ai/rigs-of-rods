#!/usr/bin/env python3
"""Static contracts for the OgreNext-first native product CI workflow."""

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
APP_COMMAND_LINE = (
    REPOSITORY_ROOT / "source" / "main" / "system" / "AppCommandLine.cpp"
)
MAIN_SOURCE = REPOSITORY_ROOT / "source" / "main" / "main.cpp"
APPLICATION_SOURCE = REPOSITORY_ROOT / "source" / "main" / "Application.cpp"
ACTOR_MANAGER_HEADER = (
    REPOSITORY_ROOT / "source" / "main" / "physics" / "ActorManager.h"
)
ACTOR_MANAGER_SOURCE = (
    REPOSITORY_ROOT / "source" / "main" / "physics" / "ActorManager.cpp"
)
APP_CONTEXT_HEADER = REPOSITORY_ROOT / "source" / "main" / "AppContext.h"
APP_CONTEXT_SOURCE = REPOSITORY_ROOT / "source" / "main" / "AppContext.cpp"
CONSOLE_HEADER = (
    REPOSITORY_ROOT / "source" / "main" / "system" / "Console.h"
)
CONSOLE_SOURCE = (
    REPOSITORY_ROOT / "source" / "main" / "system" / "Console.cpp"
)
ENVIRONMENT_MAP_HEADER = (
    REPOSITORY_ROOT / "source" / "main" / "gfx" / "EnvironmentMap.h"
)
ENVIRONMENT_MAP_SOURCE = (
    REPOSITORY_ROOT / "source" / "main" / "gfx" / "EnvironmentMap.cpp"
)
TEST_CMAKE = REPOSITORY_ROOT / "tests" / "CMakeLists.txt"
MYGUI_RESOURCE_ROOT = REPOSITORY_ROOT / "resources" / "mygui"
CONAN_SOURCE_FALLBACK = (
    'core.sources:download_urls=["origin", '
    '"https://c3i.jfrog.io/artifactory/conan-center-backup-sources/"]'
)
AUTOMATIC_TEXTURE_BUILD_STEP = (
    "Build automatic texture native lifecycle gate"
)
AUTOMATIC_TEXTURE_TEST_STEP = "Run exact automatic texture host gates"
AUTOMATIC_TEXTURE_NATIVE_TARGET = (
    "ror_ogre_next_demo_material_source_native_tests"
)
AUTOMATIC_TEXTURE_HOST_TESTS = (
    "ogre14_source_texture_decoder",
    "ogre14_authenticated_texture_receipt",
    "ogre14_selected_texture_source",
    "ogre_next_demo_material_source_native",
    "ogre14_authenticated_material_script_native_integration",
    "ogre_next_n1_policy",
    "ogre_next_n1_particle_runtime",
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
        cls.app_command_line_text = APP_COMMAND_LINE.read_text(
            encoding="utf-8"
        )
        cls.main_source_text = MAIN_SOURCE.read_text(encoding="utf-8")
        cls.application_source_text = APPLICATION_SOURCE.read_text(
            encoding="utf-8"
        )
        cls.actor_manager_header_text = ACTOR_MANAGER_HEADER.read_text(
            encoding="utf-8"
        )
        cls.actor_manager_source_text = ACTOR_MANAGER_SOURCE.read_text(
            encoding="utf-8"
        )
        cls.app_context_header_text = APP_CONTEXT_HEADER.read_text(
            encoding="utf-8"
        )
        cls.app_context_source_text = APP_CONTEXT_SOURCE.read_text(
            encoding="utf-8"
        )
        cls.console_header_text = CONSOLE_HEADER.read_text(encoding="utf-8")
        cls.console_source_text = CONSOLE_SOURCE.read_text(encoding="utf-8")
        cls.environment_map_header_text = ENVIRONMENT_MAP_HEADER.read_text(
            encoding="utf-8"
        )
        cls.environment_map_source_text = ENVIRONMENT_MAP_SOURCE.read_text(
            encoding="utf-8"
        )
        cls.test_cmake_text = TEST_CMAKE.read_text(encoding="utf-8")

    def workflow_step(self, text: str, name: str) -> str:
        matches = tuple(
            re.finditer(
                rf"(?ms)^      - name: {re.escape(name)}$.*?"
                r"(?=^      - (?:name:|uses:)|\Z)",
                text,
            )
        )
        self.assertEqual(
            len(matches),
            1,
            msg=f"expected one workflow step named {name!r}",
        )
        return matches[0].group(0)

    def mutate_workflow_step(
        self,
        text: str,
        name: str,
        old: str,
        new: str,
    ) -> str:
        step = self.workflow_step(text, name)
        self.assertEqual(
            step.count(old),
            1,
            msg=f"mutation input is not unique in {name!r}: {old!r}",
        )
        mutated_step = step.replace(old, new, 1)
        return text.replace(step, mutated_step, 1)

    def assert_automatic_texture_host_gate_contract(
        self,
        text: str,
        build_directory: str,
    ) -> None:
        build_step = self.workflow_step(
            text,
            AUTOMATIC_TEXTURE_BUILD_STEP,
        )
        self.assertEqual(
            tuple(
                line.strip()
                for line in build_step.splitlines()
                if line.strip().startswith("--target ")
            ),
            (f"--target {AUTOMATIC_TEXTURE_NATIVE_TARGET}",),
        )
        self.assertEqual(
            build_step.count(f'--build "{build_directory}"'),
            1,
        )
        self.assertEqual(build_step.count("--config Release"), 1)

        test_step = self.workflow_step(
            text,
            AUTOMATIC_TEXTURE_TEST_STEP,
        )
        commands = tuple(
            re.findall(
                r"(?ms)^          ctest \\\n.*?"
                r"(?=^          ctest \\\n|\Z)",
                test_step,
            )
        )
        self.assertEqual(len(commands), len(AUTOMATIC_TEXTURE_HOST_TESTS))
        self.assertEqual(
            test_step.count("--no-tests=error"),
            len(AUTOMATIC_TEXTURE_HOST_TESTS),
        )
        self.assertNotRegex(test_step, r"-R\s+['\"][^'\"]*\|")

        for command, test_name in zip(
            commands,
            AUTOMATIC_TEXTURE_HOST_TESTS,
        ):
            self.assertEqual(
                command.count(f'--test-dir "{build_directory}"'),
                1,
            )
            self.assertEqual(
                command.count("--build-config Release"),
                1,
            )
            self.assertEqual(
                command.count("--output-on-failure"),
                1,
            )
            self.assertEqual(
                command.count("--no-tests=error"),
                1,
            )
            self.assertEqual(command.count("-R "), 1)
            self.assertEqual(
                command.count(f"-R '^{test_name}$'"),
                1,
            )

    def automatic_texture_combined_gate_step(
        self,
        build_directory: str,
    ) -> str:
        combined = "|".join(AUTOMATIC_TEXTURE_HOST_TESTS)
        return (
            f"      - name: {AUTOMATIC_TEXTURE_TEST_STEP}\n"
            "        shell: bash\n"
            "        run: |\n"
            "          ctest \\\n"
            f'            --test-dir "{build_directory}" \\\n'
            "            --build-config Release \\\n"
            "            --output-on-failure \\\n"
            "            --no-tests=error \\\n"
            f"            -R '^({combined})$'\n\n"
        )

    def assert_conan_source_fallback_contract(self, text: str) -> None:
        self.assertEqual(text.count("core.sources:download_urls="), 1)
        self.assertEqual(text.count(CONAN_SOURCE_FALLBACK), 1)
        self.assertNotIn("tools.files.download:verify=False", text)
        self.assertNotIn("tools.files.download:verify=false", text)

    def test_workflow_is_isolated_read_only_and_cancellable(self) -> None:
        text = self.text
        self.assertIn("name: OgreNext-first native Release", text)
        self.assertIn("branches: [master]", text)
        self.assertIn("pull_request:", text)
        self.assertIn("workflow_dispatch:", text)
        self.assertIn("contents: read", text)
        self.assertIn("group: renderer-suite-native-${{ github.ref }}", text)
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

    def test_automatic_texture_gates_use_exact_registered_test_names(
        self,
    ) -> None:
        workflows = (
            (
                WORKFLOW.name,
                self.text,
                "build-ogre14-${{ matrix.platform }}",
            ),
            (MACOS_WORKFLOW.name, self.macos_text, "build-macos-ogre14"),
        )
        for workflow_name, text, build_directory in workflows:
            with self.subTest(workflow=workflow_name):
                self.assert_automatic_texture_host_gate_contract(
                    text,
                    build_directory,
                )

        for test_name in AUTOMATIC_TEXTURE_HOST_TESTS:
            with self.subTest(registered_test=test_name):
                self.assertEqual(
                    len(
                        re.findall(
                            rf"(?m)^\s+NAME {re.escape(test_name)}$",
                            self.test_cmake_text,
                        )
                    ),
                    1,
                )

    def test_automatic_texture_gate_rejects_step_scoped_mutations(
        self,
    ) -> None:
        workflows = (
            (
                WORKFLOW.name,
                self.text,
                "build-ogre14-${{ matrix.platform }}",
            ),
            (MACOS_WORKFLOW.name, self.macos_text, "build-macos-ogre14"),
        )
        for workflow_name, text, build_directory in workflows:
            renamed_target = self.mutate_workflow_step(
                text,
                AUTOMATIC_TEXTURE_BUILD_STEP,
                f"--target {AUTOMATIC_TEXTURE_NATIVE_TARGET}",
                f"--target {AUTOMATIC_TEXTURE_NATIVE_TARGET}_wrong",
            )
            renamed_target += (
                "\n# A matching token outside the named build step must not "
                "satisfy the contract.\n"
                f"# --target {AUTOMATIC_TEXTURE_NATIVE_TARGET}\n"
            )

            wrong_ctest_prefix = self.mutate_workflow_step(
                text,
                AUTOMATIC_TEXTURE_TEST_STEP,
                "-R '^ogre14_source_texture_decoder$'",
                "-R '^ror_ogre14_source_texture_decoder$'",
            )
            wrong_ctest_prefix += (
                "\n# A matching regex outside the named test step must not "
                "satisfy the contract.\n"
                "# -R '^ogre14_source_texture_decoder$'\n"
            )

            missing_no_tests_error = self.mutate_workflow_step(
                text,
                AUTOMATIC_TEXTURE_TEST_STEP,
                "--no-tests=error \\\n"
                "            -R '^ogre14_source_texture_decoder$'",
                "--no-tests=ignore \\\n"
                "            -R '^ogre14_source_texture_decoder$'",
            )
            unanchored_regex = self.mutate_workflow_step(
                text,
                AUTOMATIC_TEXTURE_TEST_STEP,
                "-R '^ogre14_selected_texture_source$'",
                "-R 'ogre14_selected_texture_source'",
            )
            missing_lifecycle = self.mutate_workflow_step(
                text,
                AUTOMATIC_TEXTURE_TEST_STEP,
                (
                    "-R '^ogre14_authenticated_material_script_"
                    "native_integration$'"
                ),
                "-R '^ogre14_authenticated_material_script_receipt$'",
            )

            exact_test_step = self.workflow_step(
                text,
                AUTOMATIC_TEXTURE_TEST_STEP,
            )
            combined_alternation = text.replace(
                exact_test_step,
                self.automatic_texture_combined_gate_step(
                    build_directory,
                ),
                1,
            )

            mutations = (
                ("target renamed outside-step shadow", renamed_target),
                ("wrong CTest prefix outside-step shadow", wrong_ctest_prefix),
                ("missing no-tests error", missing_no_tests_error),
                ("unanchored regex", unanchored_regex),
                ("missing ContentManager lifecycle", missing_lifecycle),
                ("combined regex alternation", combined_alternation),
            )
            for mutation_name, mutated in mutations:
                with self.subTest(
                    workflow=workflow_name,
                    mutation=mutation_name,
                ):
                    with self.assertRaises(AssertionError):
                        self.assert_automatic_texture_host_gate_contract(
                            mutated,
                            build_directory,
                        )

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

    def test_macos_cityworld_scenes_require_verified_gl3_capability(
        self,
    ) -> None:
        text = self.macos_text
        capability_id = "id: renderer_capability"
        capability_gate = (
            "if: steps.renderer_capability.outputs.available == 'true'"
        )
        available_marker = (
            "^ROR_MACOS_GL3_CAPABILITY=available "
        )
        self.assertEqual(text.count(capability_id), 1)
        self.assertEqual(text.count(capability_gate), 2)
        self.assertEqual(text.count(available_marker), 1)
        self.assertIn(
            'echo "available=true" >> "$GITHUB_OUTPUT"',
            text,
        )
        self.assertIn(
            'echo "available=false" >> "$GITHUB_OUTPUT"',
            text,
        )
        self.assertLess(
            text.index(capability_id),
            text.index(
                "Drive CityWorld LED streetlight with macOS arm64 GL3Plus"
            ),
        )
        self.assertLess(
            text.index(capability_id),
            text.index(
                "Render CityWorld Bridge streetlight with macOS arm64 GL3Plus"
            ),
        )
        self.assertEqual(text.count("--postprocess-mode v0a"), 1)
        self.assertIn(
            "Render CityWorld Bridge streetlight with macOS arm64 "
            "GL3Plus and V0A",
            text,
        )

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
            (
                "cmake/conan/recipes/mygui/patches/3.4.0/"
                "honor-toolchain-cxx-standard.patch"
            ),
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

    def test_mygui_preserves_the_pinned_toolchain_cpp_standard(self) -> None:
        recipe_dir = (
            REPOSITORY_ROOT / "cmake" / "conan" / "recipes" / "mygui"
        )
        conandata = (recipe_dir / "conandata.yml").read_text(encoding="utf-8")
        patch = (
            recipe_dir
            / "patches"
            / "3.4.0"
            / "honor-toolchain-cxx-standard.patch"
        ).read_text(encoding="utf-8")
        patch_path = (
            'patch_file: "patches/3.4.0/'
            'honor-toolchain-cxx-standard.patch"'
        )
        self.assertEqual(conandata.count(patch_path), 1)
        self.assertIn("-SET(CMAKE_CXX_STANDARD 11)", patch)
        self.assertIn(
            "if (NOT DEFINED CMAKE_CXX_STANDARD "
            "OR CMAKE_CXX_STANDARD LESS 14)",
            patch,
        )
        self.assertIn("+set(CMAKE_CXX_STANDARD_REQUIRED ON)", patch)

    def test_build_install_relocation_and_audit_are_mandatory(self) -> None:
        text = self.text
        required = (
            "-DCMAKE_BUILD_TYPE=Release",
            "-DROR_BUILD_TESTS=ON",
            "-DROR_CREATE_CONTENT_FOLDER=ON",
            "Configure OgreNext-first native Release",
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
        self.assertNotIn("-DROR_OGRE14=", text)
        self.assertNotIn("-DROR_RENDERER_PUBLIC_LAUNCHER=", text)
        self.assertNotIn("-DROR_OGRE_NEXT_PRODUCTION_PACKAGE=", text)
        self.assertEqual(text.count("cmake --install"), 1)
        self.assertEqual(text.count("cmake -E rename"), 1)

    def test_managed_material_source_authority_is_built_and_tested_natively(
        self,
    ) -> None:
        for marker in (
            "Ogre14ManagedMaterialSourceAdapterTests.cpp",
            "Ogre14ManagedMaterialSourceAdapter.cpp",
            "Ogre14AuthenticatedTextureReceipt.cpp",
            "Ogre14SelectedTextureSource.cpp",
            "ror_ogre14_managed_material_source_adapter_tests",
            "NAME ogre14_managed_material_source_adapter",
        ):
            with self.subTest(marker=marker):
                self.assertIn(marker, self.test_cmake_text)
        self.assertIn("--target all", self.text)
        self.assertIn("Run native test suite", self.text)
        self.assertIn("ctest", self.text)

    def test_cli_audit_commands_are_ui_free_before_renderer_startup(
        self,
    ) -> None:
        command_line = self.app_command_line_text
        self.assertEqual(
            command_line.count("RoR::WriteCommandLineInfo("),
            2,
        )
        self.assertEqual(command_line.count("stdout,"), 2)
        self.assertNotIn("ErrorUtils::ShowInfo", command_line)
        self.assertNotIn("MessageBox", command_line)
        main_source = self.main_source_text
        parse = main_source.index("processCommandLine(argc, argv)")
        help_exit = main_source.index(
            "AppState::PRINT_HELP_EXIT",
            parse,
        )
        version_exit = main_source.index(
            "AppState::PRINT_VERSION_EXIT",
            help_exit,
        )
        rendering = main_source.index(
            "SetUpRendering(", version_exit
        )
        self.assertLess(parse, help_exit)
        self.assertLess(help_exit, version_exit)
        self.assertLess(version_exit, rendering)

    def test_warning_texture_upload_matches_renderer_storage(self) -> None:
        main_source = self.main_source_text
        begin = main_source.index(
            "if (!App::diag_warning_texture->getBool())"
        )
        end = main_source.index(
            "App::GetContentManager()->AddResourcePack",
            begin,
        )
        upload = main_source[begin:end]
        self.assertNotIn("Ogre::uchar data[3]", upload)
        self.assertNotIn("Ogre::PF_BYTE_RGB", upload)
        self.assertNotIn("Ogre::PixelBox pixels(1, 1, 1", upload)
        for storage_query in (
            "warning_buffer->getWidth()",
            "warning_buffer->getHeight()",
            "warning_buffer->getDepth()",
            "warning_buffer->getFormat()",
            "Ogre::PixelUtil::getMemorySize(",
        ):
            self.assertEqual(upload.count(storage_query), 1)
        self.assertIn(
            "std::vector<Ogre::uchar> warning_data(",
            upload,
        )
        self.assertIn(
            "warning_buffer->blitFromMemory(warning_pixels);",
            upload,
        )

    def test_mygui_d3d11_shader_names_match_the_runtime_loader(self) -> None:
        for stage in ("VP", "FP"):
            with self.subTest(stage=stage):
                required = (
                    MYGUI_RESOURCE_ROOT / f"MyGUI_{stage}.hlsl"
                ).read_bytes()
                legacy = (
                    MYGUI_RESOURCE_ROOT / f"MyGUI_Ogre_{stage}.hlsl"
                ).read_bytes()
                self.assertEqual(required, legacy)
                self.assertIn(b"void main(", required)
                self.assertIn(b"SV_", required)

    def test_ogre_linked_config_test_uses_dependency_cpp_standard(
        self,
    ) -> None:
        target = "ror_terrain_bundle_config_syntax_tests"
        conditional_blocks = tuple(
            fragment.split("endif ()", 1)[0]
            for fragment in self.test_cmake_text.split(
                f"if (TARGET {target})"
            )[1:]
        )
        standard_blocks = tuple(
            block
            for block in conditional_blocks
            if "CXX_STANDARD 14" in block
        )
        self.assertEqual(len(standard_blocks), 1)
        standard_block = standard_blocks[0]
        for contract in (
            "set_target_properties(",
            target,
            "CXX_STANDARD 14",
            "CXX_STANDARD_REQUIRED YES",
            "CXX_EXTENSIONS NO",
        ):
            with self.subTest(contract=contract):
                self.assertEqual(standard_block.count(contract), 1)

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
            "tools/run_cityworld_bridge_scene.py",
            "Drive CityWorld bridge with relocated Linux GL3Plus",
            "Drive CityWorld bridge with relocated Windows D3D11",
            '--executable "${GITHUB_WORKSPACE}/artifacts/runtime-'
            '${{ matrix.platform }}/RunRoR"',
            '--executable "${GITHUB_WORKSPACE}/artifacts/runtime-'
            '${{ matrix.platform }}/RoR.exe"',
            '--runtime-content "${GITHUB_WORKSPACE}/artifacts/runtime-'
            '${{ matrix.platform }}/content"',
            '--artifact-dir "${GITHUB_WORKSPACE}/artifacts/'
            'cityworld-bridge-${{ matrix.platform }}"',
            "--postprocess-mode v0a",
            "--timeout 300",
            "do not constitute physical GPU or vendor performance",
        )
        for contract in required:
            with self.subTest(contract=contract):
                self.assertIn(contract, text)
        self.assertEqual(
            text.count("python tools/ogre14_native_runtime_smoke.py"),
            2,
        )
        self.assertEqual(
            text.count("tools/run_cityworld_bridge_scene.py"),
            3,
        )
        self.assertEqual(
            text.count("python tools/run_cityworld_bridge_scene.py"),
            1,
        )
        self.assertEqual(text.count("--postprocess-mode v0a"), 1)
        self.assertEqual(text.count('"--postprocess-mode"'), 2)
        linux_scene_start = text.index(
            "Drive CityWorld bridge with relocated Linux GL3Plus"
        )
        windows_scene_start = text.index(
            "Drive CityWorld bridge with relocated Windows D3D11"
        )
        upload_start = text.index(
            "Upload runtime, audit, and diagnostics",
            windows_scene_start,
        )
        linux_scene = text[linux_scene_start:windows_scene_start]
        windows_scene = text[windows_scene_start:upload_start]
        self.assertIn("xvfb-run -a", linux_scene)
        self.assertIn("GALLIUM_DRIVER: llvmpipe", linux_scene)
        self.assertIn('LIBGL_ALWAYS_SOFTWARE: "1"', linux_scene)
        self.assertNotIn("xvfb-run", windows_scene)
        self.assertNotIn("GALLIUM_DRIVER", windows_scene)
        self.assertNotIn("MESA_LOADER_DRIVER_OVERRIDE", text)
        self.assertNotIn("continue-on-error:", text)
        self.assertIn("--generation-timeout 600", text)
        self.assertIn("--generation-workers 1", text)
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

    def test_linux_cityworld_crash_evidence_is_fail_closed(self) -> None:
        text = self.text
        linux_start = text.index(
            "Drive CityWorld bridge with relocated Linux GL3Plus"
        )
        windows_start = text.index(
            "Stage Windows CityWorld crash symbols and enable LocalDumps",
            linux_start,
        )
        gate = text[linux_start:windows_start]
        for contract in (
            "timeout-minutes: 12",
            "cityworld-bridge-crash-evidence-${{ matrix.platform }}",
            "cat /proc/sys/kernel/core_pattern",
            "trap restore_core_pattern EXIT",
            "kernel.core_pattern=${core_dir}/core.%e.%p",
            "ulimit -c unlimited",
            'driver_exit_code="${PIPESTATUS[0]}"',
            "find \"$core_dir\" -maxdepth 1 -type f -name 'core.*'",
            "thread apply all bt full",
            "info sharedlibrary",
            "collect_linux_cityworld_crash_evidence.py",
            "--process-diagnostic",
            "diagnostics/runtime-process.json",
            "--core-dir \"$core_dir\"",
            "--backtrace \"$backtrace\"",
            "--output \"$manifest\"",
            'exit "$evidence_exit_code"',
            'exit "$driver_exit_code"',
        ):
            with self.subTest(contract=contract):
                self.assertIn(contract, gate)
        self.assertIn("gdb \\", text)
        self.assertNotIn("continue-on-error", gate)

    def test_windows_cityworld_crash_evidence_is_fail_closed(self) -> None:
        text = self.text
        setup_start = text.index(
            "Stage Windows CityWorld crash symbols and enable LocalDumps"
        )
        async_start = text.index(
            "Drive CityWorld bridge with relocated Windows D3D11 async physics",
            setup_start,
        )
        sync_start = text.index(
            "Isolate CityWorld bridge with relocated Windows D3D11 sync physics",
            async_start,
        )
        cleanup_start = text.index(
            "Disable Windows CityWorld LocalDumps override",
            sync_start,
        )
        upload_start = text.index(
            "Upload runtime, audit, and diagnostics",
            cleanup_start,
        )
        setup = text[setup_start:async_start]
        async_gate = text[async_start:sync_start]
        sync_gate = text[sync_start:cleanup_start]
        cleanup = text[cleanup_start:upload_start]

        for contract in (
            "windows-cityworld-crash-evidence",
            "Windows Error Reporting\\LocalDumps\\RoR-Ogre14.exe",
            '"DumpCount"',
            '"DumpType"',
            "-Value 8",
            "-Value 2",
            'Join-Path $buildRoot "bin/RoR-Ogre14.pdb"',
            "Production RoR-Ogre14.pdb is missing",
            'pdb = "symbols/RoR-Ogre14.pdb"',
            "executable_sha256",
            "pdb_sha256",
            '"symbols.json"',
        ):
            with self.subTest(setup_contract=contract):
                self.assertIn(contract, setup)
        self.assertNotIn("Get-ChildItem", setup)

        for gate, mode, artifact, dump in (
            (
                async_gate,
                "async",
                "cityworld-bridge-${{ matrix.platform }}",
                "dumps/async",
            ),
            (
                sync_gate,
                "sync",
                "cityworld-bridge-sync-${{ matrix.platform }}",
                "dumps/sync",
            ),
        ):
            with self.subTest(mode=mode):
                self.assertIn(dump, gate)
                self.assertIn('"--physics-mode"', gate)
                self.assertIn(f'"{mode}"', gate)
                self.assertIn(artifact, gate)
                self.assertIn("RedirectStandardOutput", gate)
                self.assertIn("RedirectStandardError", gate)
                self.assertIn(
                    "collect_windows_cityworld_crash_evidence.py",
                    gate,
                )
                self.assertIn("-PropertyType ExpandString", gate)
                self.assertIn("--driver-exit-code $process.ExitCode", gate)
                self.assertIn("diagnostics/runtime-process.json", gate)
                self.assertIn("--poll-attempts 40", gate)
                self.assertIn("--poll-interval-ms 250", gate)
                self.assertIn(f'"{mode}-dumps.json"', gate)
                self.assertIn("exit $evidenceExitCode", gate)
                self.assertIn("exit $process.ExitCode", gate)
                self.assertNotIn("continue-on-error", gate)

        self.assertIn("!cancelled()", sync_gate)
        self.assertIn(
            "steps.windows-cityworld-crash-evidence.outcome == 'success'",
            sync_gate,
        )
        self.assertNotIn("success()", sync_gate)
        self.assertIn("Remove-Item -LiteralPath $werKey", cleanup)
        self.assertIn(
            "artifacts/cityworld-bridge-crash-evidence-"
            "${{ matrix.platform }}",
            text[upload_start:],
        )
        self.assertIn(
            "artifacts/cityworld-bridge-sync-${{ matrix.platform }}",
            text[upload_start:],
        )

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

    def test_shutdown_stops_callbacks_and_releases_window_integrations(
        self,
    ) -> None:
        text = self.main_source_text
        worker_helper = text.index("bool ReleaseWorkerRuntime() noexcept")
        private_workers = text.index(
            "ShutdownWorkerRuntime()",
            worker_helper,
        )
        general_workers = text.index(
            "App::DestroyThreadPool()",
            private_workers,
        )
        worker_marker = text.index(
            "Physics and graphics worker pools released",
            general_workers,
        )
        self.assertLess(private_workers, general_workers)
        self.assertLess(general_workers, worker_marker)
        self.assertIn(
            "return clean_release;",
            text[worker_helper:text.index("void ReleaseRendererRuntime()")],
        )

        helper_start = text.index("void ReleaseWindowBoundRuntime(")
        detach = text.index("DetachRenderWindowEvents()", helper_start)
        input_cleanup = text.index("App::DestroyInputEngine()", detach)
        gui_cleanup = text.index(
            "App::GetGuiManager()->ShutdownMyGUI()",
            input_cleanup,
        )
        listener_cleanup = text.index(
            "removeRenderQueueListener",
            gui_cleanup,
        )
        overlay_cleanup = text.index(
            "delete overlay_system",
            listener_cleanup,
        )
        helper_end = text.index("class WindowBoundRuntimeGuard", overlay_cleanup)
        self.assertLess(detach, input_cleanup)
        self.assertLess(input_cleanup, gui_cleanup)
        self.assertLess(gui_cleanup, listener_cleanup)
        self.assertLess(listener_cleanup, overlay_cleanup)
        self.assertIn(
            "[RoR|Shutdown] Window-bound runtime integrations released",
            text[helper_start:helper_end],
        )

        main_start = text.index("int main(")
        renderer_guard = text.index(
            "RendererRuntimeGuard renderer_runtime_guard",
            main_start,
        )
        guard = text.index(
            "WindowBoundRuntimeGuard window_bound_runtime_guard",
            main_start,
        )
        worker_guard = text.index(
            "WorkerRuntimeGuard worker_runtime_guard",
            guard,
        )
        fatal_scene_gate = text.index(
            "ApplicationFatalShutdownGate fatal_scene_runtime_gate",
            worker_guard,
        )
        try_start = text.index("try", worker_guard)
        self.assertLess(renderer_guard, guard)
        self.assertLess(guard, try_start)
        self.assertLess(worker_guard, try_start)
        self.assertLess(fatal_scene_gate, try_start)

        fatal_catch = text.index(
            "catch (const ApplicationFatalError& fatal)",
            try_start,
        )
        fatal_sequence = text.index(
            "RunApplicationFatalShutdownSequence(",
            fatal_catch,
        )
        fatal_worker_release = text.index(
            "worker_runtime_guard.Release()",
            fatal_sequence,
        )
        fatal_scene_release = text.index(
            "fatal_scene_runtime_gate.Release()",
            fatal_worker_release,
        )
        fatal_fail_stop = text.index(
            "FailStopApplication(fatal.exit_code())",
            fatal_scene_release,
        )
        self.assertLess(fatal_sequence, fatal_worker_release)
        self.assertLess(fatal_worker_release, fatal_scene_release)
        self.assertLess(fatal_scene_release, fatal_fail_stop)
        self.assertIn("std::_Exit(exit_code)", text)

        renderer_helper = text.index("void ReleaseRendererRuntime()")
        envmap_release = text.index(
            "GetEnvMap().Shutdown()",
            renderer_helper,
        )
        envmap_returned = text.index(
            "Environment map shutdown returned",
            envmap_release,
        )
        root_release = text.index(
            "ShutdownRendering()",
            envmap_returned,
        )
        runtime_released = text.index(
            "Renderer runtime released",
            root_release,
        )
        self.assertLess(envmap_release, envmap_returned)
        self.assertLess(envmap_returned, root_release)
        self.assertLess(root_release, runtime_released)

        gfx_scene_static = self.application_source_text.index(
            "static GfxScene             g_gfx_scene;"
        )
        game_context_static = self.application_source_text.index(
            "static GameContext          g_game_context;"
        )
        self.assertLess(gfx_scene_static, game_context_static)
        self.assertIn(
            "bool           ShutdownWorkerRuntime() noexcept;",
            self.actor_manager_header_text,
        )
        actor_destructor = self.actor_manager_source_text.index(
            "ActorManager::~ActorManager()"
        )
        actor_shutdown = self.actor_manager_source_text.index(
            "bool ActorManager::ShutdownWorkerRuntime() noexcept"
        )
        self.assertIn(
            "this->ShutdownWorkerRuntime();",
            self.actor_manager_source_text[
                actor_destructor:actor_shutdown
            ],
        )
        self.assertIn(
            "m_sim_thread_pool.reset();",
            self.actor_manager_source_text[actor_shutdown:],
        )

        queue_end = text.index('OgreProfileEnd("RoR message queue")')
        shutdown_guard = text.index(
            "AppState::SHUTDOWN",
            queue_end,
        )
        capture_update = text.index(
            "App::UpdateWorldModelCaptureRequest()",
            queue_end,
        )
        self.assertLess(shutdown_guard, capture_update)
        self.assertIn(
            "[RoR|Shutdown] Leaving the main loop after the shutdown "
            "message",
            text[queue_end:capture_update],
        )

        immediate_queue_guard = text.index(
            "Once shutdown is committed",
            queue_end - 6000,
        )
        chained_messages = text.index(
            "// Process chained messages",
            immediate_queue_guard,
        )
        self.assertLess(immediate_queue_guard, chained_messages)

        self.assertIn(
            "bool                 DetachRenderWindowEvents() noexcept;",
            self.app_context_header_text,
        )
        self.assertIn(
            "bool                 ShutdownRendering() noexcept;",
            self.app_context_header_text,
        )
        self.assertIn(
            "bool AppContext::DetachRenderWindowEvents() noexcept",
            self.app_context_source_text,
        )
        self.assertIn("catch (...)", self.app_context_source_text)
        self.assertIn("return clean_detach;", self.app_context_source_text)
        self.assertIn(
            "m_render_window_registered = false",
            self.app_context_header_text,
        )
        self.assertIn(
            "m_window_event_listener_registered = false",
            self.app_context_header_text,
        )
        app_context_destructor = self.app_context_source_text.index(
            "AppContext::~AppContext()"
        )
        renderer_shutdown = self.app_context_source_text.index(
            "bool AppContext::ShutdownRendering() noexcept",
            app_context_destructor,
        )
        self.assertIn(
            "this->ShutdownRendering();",
            self.app_context_source_text[
                app_context_destructor:renderer_shutdown
            ],
        )
        self.assertIn(
            "if (m_rendering_shutdown)",
            self.app_context_source_text[renderer_shutdown:],
        )
        destroy_target = self.app_context_source_text.index(
            "destroyRenderTarget",
            renderer_shutdown,
        )
        detach_in_destructor = self.app_context_source_text.index(
            "this->DetachRenderWindowEvents()",
            renderer_shutdown,
        )
        self.assertLess(detach_in_destructor, destroy_target)

        self.assertIn("~Console() noexcept override;", self.console_header_text)
        console_destructor = self.console_source_text.index(
            "Console::~Console()"
        )
        remove_log_listener = self.console_source_text.index(
            "default_log->removeListener(this)",
            console_destructor,
        )
        detached_log_marker = self.console_source_text.index(
            "Console log listener detached",
            remove_log_listener,
        )
        self.assertLess(remove_log_listener, detached_log_marker)

        renderer_start = self.app_context_source_text.index(
            "Renderer root teardown starting",
            renderer_shutdown,
        )
        delete_root = self.app_context_source_text.index(
            "delete m_ogre_root",
            renderer_start,
        )
        renderer_complete = self.app_context_source_text.index(
            "Renderer root teardown completed",
            delete_root,
        )
        self.assertLess(renderer_start, delete_root)
        self.assertLess(delete_root, renderer_complete)

        self.assertIn(
            "bool Shutdown() noexcept;",
            self.environment_map_header_text,
        )
        envmap_shutdown = self.environment_map_source_text.index(
            "bool RoR::GfxEnvmap::Shutdown() noexcept"
        )
        remove_viewports = self.environment_map_source_text.index(
            "removeAllViewports()",
            envmap_shutdown,
        )
        destroy_camera = self.environment_map_source_text.index(
            "destroyCamera",
            envmap_shutdown,
        )
        viewport_failure_guard = self.environment_map_source_text.index(
            "viewports_released = false",
            remove_viewports,
        )
        guarded_camera_release = self.environment_map_source_text.index(
            "m_cameras[face] != nullptr && viewports_released",
            viewport_failure_guard,
        )
        all_faces_guard = self.environment_map_source_text.index(
            "if (all_face_resources_released)",
            destroy_camera,
        )
        release_texture = self.environment_map_source_text.index(
            "m_rtt_texture.reset()",
            all_faces_guard,
        )
        release_marker = self.environment_map_source_text.index(
            "Environment map renderer resources released",
            release_texture,
        )
        self.assertLess(remove_viewports, destroy_camera)
        self.assertLess(viewport_failure_guard, guarded_camera_release)
        self.assertLess(guarded_camera_release, destroy_camera)
        self.assertLess(destroy_camera, all_faces_guard)
        self.assertLess(destroy_camera, release_texture)
        self.assertLess(release_texture, release_marker)

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
        self.assertIn(
            "cityworld-bridge-${{ matrix.platform }}",
            text,
        )
        self.assertIn(
            "cityworld-bridge-${{ matrix.platform }}.driver.log",
            text,
        )
        self.assertIn("conan-graph.json", text)
        self.assertIn(
            "--diagnostics-directory \\\n              artifacts/linux-x86_64-storefront-clean-failure",
            text,
        )
        self.assertIn(
            "artifacts/${{ matrix.platform }}-storefront-clean-failure",
            text,
        )
        self.assertIn("LastTest.log", text)
        self.assertIn("retention-days: 14", text)

    def test_workflow_does_not_touch_recorder_paths(self) -> None:
        lowered = self.text.casefold()
        self.assertNotIn("source/main/worldmodel", lowered)
        self.assertNotIn("tests/worldmodel", lowered)
        self.assertIsNone(re.search(r"\bgit\s+(?:clean|reset|restore)\b", lowered))


if __name__ == "__main__":
    unittest.main()
