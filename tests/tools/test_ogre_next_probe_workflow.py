#!/usr/bin/env python3
"""Offline contract for the opt-in OGRE-Next cross-platform CI matrix."""

from __future__ import annotations

from pathlib import Path
import re
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
WORKFLOW_PATH = REPOSITORY_ROOT / ".github/workflows/ogre-next-probe.yml"
SELF_PATH = "tests/tools/test_ogre_next_probe_workflow.py"


class OgreNextProbeWorkflowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.workflow = WORKFLOW_PATH.read_text(encoding="utf-8")

    def test_matrix_is_explicit_and_fail_closed(self) -> None:
        self.assertIn("fail-fast: false", self.workflow)
        self.assertIn("timeout-minutes: 120", self.workflow)
        for runner, policy in (
            ("macos-15", "macos-arm64-metal"),
            ("windows-2022", "windows-x64-d3d11"),
            ("ubuntu-24.04", "linux-x86_64-vulkan"),
        ):
            with self.subTest(runner=runner):
                self.assertIn(f"runner: {runner}", self.workflow)
                self.assertIn(f"platform: {policy}", self.workflow)
        self.assertIn('toolset: "14.44"', self.workflow)
        self.assertNotIn("surface:", self.workflow)

    def test_every_action_is_pinned_to_an_immutable_commit(self) -> None:
        uses = re.findall(r"^\s*-?\s*uses:\s*([^\s#]+)", self.workflow, re.MULTILINE)
        self.assertGreaterEqual(len(uses), 4)
        for action in uses:
            with self.subTest(action=action):
                self.assertRegex(action, r"^[^@]+@[0-9a-f]{40}$")

    def test_all_renderer_policy_inputs_trigger_the_probe(self) -> None:
        for path in (
            ".gitattributes",
            "CMakeLists.txt",
            "source/main/CMakeLists.txt",
            "tests/CMakeLists.txt",
            "cmake/RendererLauncherPackageConfig.cmake",
            "source/main/gfx/RendererBackendPolicy.*",
            "source/main/gfx/RendererStartupHandoff.*",
            "source/main/gfx/RendererStartupPlan.*",
            "source/main/system/RendererChildIntent.*",
            "source/main/system/RendererChildLauncher.*",
            "source/main/system/RendererLauncherMain.cpp",
            "source/main/system/RendererLauncherPackageConfig.h.in",
            "source/main/system/RendererPublicLauncher.*",
            "source/main/gfx/render/**",
            "tests/gfx/RendererBackendPolicyTests.cpp",
            "tests/gfx/RendererChildIntentTests.cpp",
            "tests/gfx/RendererChildLauncherFakeChild.cpp",
            "tests/gfx/RendererChildLauncherTests.cpp",
            "tests/gfx/RendererPublicLauncherLegacyChild.cpp",
            "tests/gfx/RendererPublicLauncherTests.cpp",
            "tests/gfx/RendererStartupHandoffTests.cpp",
            "tests/gfx/RendererStartupPlanTests.cpp",
            "tests/gfx/render/**",
        ):
            with self.subTest(path=path):
                self.assertEqual(self.workflow.count(f"- {path}"), 2)

    def test_every_probe_layer_is_required_in_normal_and_optimized_python(self) -> None:
        for test_path in (
            "tests/tools/test_ogre_next_probe_contract.py",
            "tests/tools/test_ogre_next_frame_probe.py",
            "tests/tools/test_ogre_next_frontend_n1_contract.py",
            "tests/tools/test_ogre_next_pssm_shadow_contract.py",
            "tests/tools/test_ogre_next_metal_n2_contract.py",
            "tests/tools/test_ogre_next_vulkan_rt5_contract.py",
            "tests/tools/test_ogre_next_linux_static_closure.py",
            "tests/tools/test_ogre_next_metal_n3_contract.py",
            "tests/tools/test_ogre_next_metal_n4_contract.py",
            SELF_PATH,
            "tests/tools/test_verify_ogre_next_artifact_set.py",
        ):
            with self.subTest(test_path=test_path):
                self.assertEqual(self.workflow.count(test_path), 2)
        self.assertIn("--validate-contract-only", self.workflow)
        self.assertIn("--output-on-failure", self.workflow)
        self.assertIn(
            "- name: Run fail-closed offline contracts\n        shell: bash",
            self.workflow,
        )

    def test_linux_uses_a_declared_software_vulkan_device(self) -> None:
        for required in (
            "libvulkan-dev",
            "libx11-dev",
            "libxt-dev",
            "libxaw7-dev",
            "mesa-vulkan-drivers",
            "vulkaninfo --summary",
            "VK_ICD_FILENAMES",
            "LIBGL_ALWAYS_SOFTWARE=1",
            "Linux x86_64 Vulkan null-window",
        ):
            with self.subTest(required=required):
                self.assertIn(required, self.workflow)

        for prohibited in (
            "libfreetype6-dev",
            "libshaderc-dev",
            "glslang-dev",
            "spirv-tools",
        ):
            with self.subTest(prohibited=prohibited):
                self.assertNotIn(prohibited, self.workflow)

    def test_linux_builds_and_audits_the_pinned_static_shader_closure(self) -> None:
        probe_root = REPOSITORY_ROOT / "tools" / "ogre_next_probe"
        cmake = (probe_root / "CMakeLists.txt").read_text(encoding="utf-8")
        cmake += (probe_root / "cmake" / "PinnedOgreNext.cmake").read_text(
            encoding="utf-8"
        )
        self.assertIn("linux-shader-toolchain.lock.json", cmake)
        self.assertIn("FetchContent_MakeAvailable(shaderc)", cmake)
        self.assertIn("set(Vulkan_SHADERC_LIB_REL shaderc_combined", cmake)
        self.assertIn("ror_ogre_next_linux_static_closure_verify", cmake)
        self.assertIn("set(Vulkan_SHADERC_LIB_REL", cmake)
        self.assertIn("set(Vulkan_SHADERC_LIB_DBG", cmake)
        self.assertNotIn("find_package(glslang", cmake)
        self.assertNotIn("ROR_OGRE_NEXT_SHADERC_SHARED_LIBRARY", cmake)
        self.assertNotIn("glslang-dev", self.workflow)
        self.assertNotIn("libshaderc-dev", self.workflow)
        self.assertIn("cmp tools/ogre_next_probe/linux-shader-toolchain.lock.json", self.workflow)
        self.assertIn("lib(freetype|shaderc|glslang", self.workflow)
        self.assertIn("MachineIndependent|GenericCodeGen|OSDependent", self.workflow)
        self.assertIn("ror_ogre_next_frame_probe", self.workflow)
        self.assertIn("ror_ogre_next_frontend_n1_smoke", self.workflow)
        for artifact in (
            "ror-ogre-next-frontend-rt4-pbr-v1-isolation.bin",
            "ror-ogre-next-frontend-rt4-pbr-v1-hdr-compositor.bin",
            "ror-ogre-next-frontend-rt4-pbr-v1-repeat/",
        ):
            with self.subTest(artifact=artifact):
                self.assertIn(artifact, self.workflow)

    def test_startup_plan_is_built_and_run_on_every_probe_platform(self) -> None:
        cmake = (
            REPOSITORY_ROOT / "tools" / "ogre_next_probe" / "CMakeLists.txt"
        ).read_text(encoding="utf-8")
        runner = (REPOSITORY_ROOT / "tools" / "run_ogre_next_probe.py").read_text(
            encoding="utf-8"
        )
        verifier = (
            REPOSITORY_ROOT / "tools" / "verify_ogre_next_artifact_set.py"
        ).read_text(encoding="utf-8")
        prelink = (
            REPOSITORY_ROOT
            / "tools"
            / "ogre_next_probe"
            / "cmake"
            / "VerifyN2SourceProvenance.cmake"
        ).read_text(encoding="utf-8")
        target_block = cmake[
            cmake.index("add_executable(\n        ror_renderer_startup_plan_tests") :
            cmake.index("target_include_directories(\n        ror_renderer_startup_plan_tests")
        ]
        handoff_target_block = cmake[
            cmake.index(
                "add_executable(\n        ror_renderer_startup_handoff_tests"
            ) :
            cmake.index(
                "target_include_directories(\n        ror_renderer_startup_handoff_tests"
            )
        ]
        intent_target_block = cmake[
            cmake.index(
                "add_executable(\n        ror_renderer_child_intent_tests"
            ) :
            cmake.index(
                "target_include_directories(\n"
                "        ror_renderer_child_intent_tests"
            )
        ]
        launcher_target_block = cmake[
            cmake.index(
                "add_executable(\n        ror_renderer_child_launcher_tests"
            ) :
            cmake.index(
                "target_include_directories(\n"
                "        ror_renderer_child_launcher_tests"
            )
        ]
        public_child_target_block = cmake[
            cmake.index(
                "add_executable(\n"
                "        ror_renderer_public_launcher_legacy_child"
            ) :
            cmake.index(
                "set_target_properties(\n"
                "        ror_renderer_public_launcher_legacy_child"
            )
        ]
        public_entrypoint_target_block = cmake[
            cmake.index(
                "add_executable(\n"
                "        ror_renderer_public_launcher_entrypoint"
            ) :
            cmake.index(
                "target_include_directories(\n"
                "        ror_renderer_public_launcher_entrypoint"
            )
        ]
        policy_language_marker = (
            "set_target_properties(\n"
            "        ror_renderer_backend_policy_tests\n"
            "        ror_renderer_startup_plan_tests"
        )
        policy_language_start = cmake.index(policy_language_marker)
        policy_language_block = cmake[
            policy_language_start : cmake.index(
                "endif ()", policy_language_start
            )
        ]
        cmake_manifest = cmake[
            cmake.index("list(APPEND _ror_relevant_source_files") :
            cmake.index("list(FILTER _ror_relevant_source_files")
        ]
        clean_paths = cmake[
            cmake.index("set(_ror_n2_relevant_source_paths") :
            cmake.index("execute_process(", cmake.index("set(_ror_n2_relevant_source_paths"))
        ]
        runner_manifest = runner[
            runner.index("RELEVANT_SOURCE_PATHS = (") :
            runner.index("\n)\n\n\nclass ProbeError")
        ]
        verifier_manifest = verifier[
            verifier.index("RELEVANT_SOURCE_PATHS = (") :
            verifier.index("\n)\nRT4_ATTESTATION_SCHEMA")
        ]
        prelink_clean_paths = prelink[
            prelink.index("set(_ror_n2_relevant_source_paths") :
            prelink.index("execute_process(")
        ]
        prelink_manifest = prelink[
            prelink.index("list(APPEND _ror_n2_relevant_source_files") :
            prelink.index("list(FILTER _ror_n2_relevant_source_files")
        ]
        for token in (
            "ror_renderer_backend_policy_tests",
            "tests/gfx/RendererBackendPolicyTests.cpp",
            "add_test(NAME ror_renderer_backend_policy",
            "ror_renderer_startup_plan_tests",
            "tests/gfx/RendererStartupPlanTests.cpp",
            "source/main/gfx/RendererStartupPlan.cpp",
            "source/main/gfx/RendererBackendPolicy.cpp",
            "add_test(NAME ror_renderer_startup_plan",
            "ror_renderer_startup_handoff_tests",
            "tests/gfx/RendererStartupHandoffTests.cpp",
            "source/main/gfx/RendererStartupHandoff.cpp",
            "add_test(NAME ror_renderer_startup_handoff",
            "ror_renderer_child_intent_tests",
            "tests/gfx/RendererChildIntentTests.cpp",
            "source/main/system/RendererChildIntent.cpp",
            "add_test(NAME ror_renderer_child_intent",
            "_ror_n1_package_dependencies\n"
            "        ror_renderer_backend_policy_tests",
            "ror_renderer_child_launcher_fake_child",
            "tests/gfx/RendererChildLauncherFakeChild.cpp",
            "ror_renderer_child_launcher_tests",
            "tests/gfx/RendererChildLauncherTests.cpp",
            "source/main/system/RendererChildLauncher.cpp",
            "add_test(NAME ror_renderer_child_launcher",
            "ror_renderer_public_launcher_legacy_child",
            "tests/gfx/RendererPublicLauncherLegacyChild.cpp",
            "ror_renderer_public_launcher_entrypoint",
            "source/main/system/RendererLauncherMain.cpp",
            "source/main/system/RendererPublicLauncher.cpp",
            "cmake/RendererLauncherPackageConfig.cmake",
            "source/main/system/RendererLauncherPackageConfig.h.in",
            "add_test(NAME ror_renderer_public_launcher_entrypoint",
        ):
            self.assertIn(token, cmake)
        for path in (
            "cmake/RendererLauncherPackageConfig.cmake",
            "source/main/gfx/RendererBackendPolicy.cpp",
            "source/main/gfx/RendererBackendPolicy.h",
            "source/main/gfx/RendererStartupHandoff.cpp",
            "source/main/gfx/RendererStartupHandoff.h",
            "source/main/gfx/RendererStartupPlan.cpp",
            "source/main/gfx/RendererStartupPlan.h",
            "source/main/system/RendererChildIntent.cpp",
            "source/main/system/RendererChildIntent.h",
            "source/main/system/RendererChildLauncher.cpp",
            "source/main/system/RendererChildLauncher.h",
            "source/main/system/RendererLauncherMain.cpp",
            "source/main/system/RendererLauncherPackageConfig.h.in",
            "source/main/system/RendererPublicLauncher.cpp",
            "source/main/system/RendererPublicLauncher.h",
            "tests/gfx/RendererBackendPolicyTests.cpp",
            "tests/gfx/RendererChildIntentTests.cpp",
            "tests/gfx/RendererChildLauncherFakeChild.cpp",
            "tests/gfx/RendererChildLauncherTests.cpp",
            "tests/gfx/RendererPublicLauncherLegacyChild.cpp",
            "tests/gfx/RendererPublicLauncherTests.cpp",
            "tests/gfx/RendererStartupHandoffTests.cpp",
            "tests/gfx/RendererStartupPlanTests.cpp",
        ):
            with self.subTest(provenance_path=path):
                self.assertEqual(cmake_manifest.count(f'"{path}"'), 1)
                self.assertEqual(clean_paths.count(path), 1)
                self.assertEqual(runner_manifest.count(f'"{path}"'), 1)
                self.assertEqual(verifier_manifest.count(f'"{path}"'), 1)
                self.assertEqual(prelink_clean_paths.count(path), 1)
                self.assertEqual(prelink_manifest.count(f'"{path}"'), 1)
        self.assertEqual(runner_manifest, verifier_manifest)
        for source in (
            "tests/gfx/RendererStartupPlanTests.cpp",
            "source/main/gfx/RendererStartupPlan.cpp",
            "source/main/gfx/RendererBackendPolicy.cpp",
        ):
            with self.subTest(target_source=source):
                self.assertEqual(target_block.count(source), 1)
        for source in (
            "tests/gfx/RendererStartupHandoffTests.cpp",
            "source/main/gfx/RendererStartupHandoff.cpp",
            "source/main/gfx/RendererStartupPlan.cpp",
            "source/main/gfx/RendererBackendPolicy.cpp",
        ):
            with self.subTest(handoff_target_source=source):
                self.assertEqual(handoff_target_block.count(source), 1)
        for source in (
            "tests/gfx/RendererChildIntentTests.cpp",
            "source/main/system/RendererChildIntent.cpp",
            "source/main/gfx/RendererStartupHandoff.cpp",
            "source/main/gfx/RendererStartupPlan.cpp",
            "source/main/gfx/RendererBackendPolicy.cpp",
        ):
            with self.subTest(intent_target_source=source):
                self.assertEqual(intent_target_block.count(source), 1)
        self.assertNotIn("RendererChildLauncher.cpp", intent_target_block)
        package_dependencies = cmake[
            cmake.index("set(_ror_n1_package_dependencies") :
            cmake.index(")", cmake.index("set(_ror_n1_package_dependencies"))
        ]
        self.assertEqual(
            package_dependencies.count("ror_renderer_child_intent_tests"), 1
        )
        for source in (
            "tests/gfx/RendererChildLauncherTests.cpp",
            "source/main/system/RendererChildIntent.cpp",
            "source/main/system/RendererChildLauncher.cpp",
            "source/main/gfx/RendererStartupHandoff.cpp",
            "source/main/gfx/RendererStartupPlan.cpp",
            "source/main/gfx/RendererBackendPolicy.cpp",
        ):
            with self.subTest(launcher_target_source=source):
                self.assertEqual(launcher_target_block.count(source), 1)
        self.assertEqual(
            public_child_target_block.count(
                "tests/gfx/RendererPublicLauncherLegacyChild.cpp"
            ),
            1,
        )
        for source in (
            "source/main/system/RendererLauncherMain.cpp",
            "source/main/system/RendererPublicLauncher.cpp",
            "source/main/system/RendererChildIntent.cpp",
            "source/main/system/RendererChildLauncher.cpp",
            "source/main/gfx/RendererStartupHandoff.cpp",
            "source/main/gfx/RendererStartupPlan.cpp",
            "source/main/gfx/RendererBackendPolicy.cpp",
        ):
            with self.subTest(public_entrypoint_target_source=source):
                self.assertEqual(public_entrypoint_target_block.count(source), 1)
        for target in (
            "ror_renderer_backend_policy_tests",
            "ror_renderer_startup_plan_tests",
            "ror_renderer_startup_handoff_tests",
            "ror_renderer_child_intent_tests",
            "ror_renderer_child_launcher_fake_child",
            "ror_renderer_child_launcher_tests",
            "ror_renderer_public_launcher_legacy_child",
            "ror_renderer_public_launcher_entrypoint",
        ):
            with self.subTest(cxx11_policy_target=target):
                self.assertEqual(policy_language_block.count(target), 1)
        self.assertIn("CXX_STANDARD 11", policy_language_block)
        self.assertIn("CXX_STANDARD_REQUIRED YES", policy_language_block)
        self.assertIn("CXX_EXTENSIONS NO", policy_language_block)

    def test_public_launcher_entrypoint_stays_outside_the_n1_package(self) -> None:
        cmake = (
            REPOSITORY_ROOT / "tools" / "ogre_next_probe" / "CMakeLists.txt"
        ).read_text(encoding="utf-8")
        entrypoint_start = cmake.index(
            "add_executable(\n        ror_renderer_public_launcher_entrypoint"
        )
        entrypoint_end = cmake.index("\n    set(_ror_n1_targets", entrypoint_start)
        entrypoint = cmake[entrypoint_start:entrypoint_end]
        package_start = cmake.index(
            "add_custom_command(\n        OUTPUT "
            '"${ROR_OGRE_NEXT_N1_PACKAGE_STAMP}"'
        )
        package_end = cmake.index(
            "add_custom_target(ror_ogre_next_frontend_n1_package",
            package_start,
        )
        package = cmake[package_start:package_end]

        for token in (
            'OUTPUT_NAME "RoR"',
            'OUTPUT_NAME "RoR-Ogre14"',
            "renderer-public-entrypoint-tests",
            "WIN32_EXECUTABLE YES",
            "WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE",
            "PRIVATE Shell32",
            "PRIVATE -municode",
        ):
            with self.subTest(entrypoint_contract=token):
                self.assertIn(token, cmake)
        for prohibited in (
            "ror_renderer_public_launcher_entrypoint",
            "ror_renderer_public_launcher_legacy_child",
            "renderer-public-entrypoint-tests",
            "RoR-Ogre14",
        ):
            with self.subTest(n1_package_exclusion=prohibited):
                self.assertNotIn(prohibited, package)
        self.assertIn(
            "ror_renderer_public_launcher_legacy_child", entrypoint
        )

    def test_renderer_child_launcher_fails_closed_at_process_boundary(self) -> None:
        source = (
            REPOSITORY_ROOT
            / "source"
            / "main"
            / "system"
            / "RendererChildLauncher.cpp"
        ).read_text(encoding="utf-8")
        tests = (
            REPOSITORY_ROOT
            / "tests"
            / "gfx"
            / "RendererChildLauncherTests.cpp"
        ).read_text(encoding="utf-8")
        launch = source[
            source.index(
                "RendererChildLaunchFailure LaunchRendererChildAndPropagateExit("
            ) :
        ]
        self.assertLess(
            launch.index("handoff.package_platform != host_platform"),
            launch.index("RendererFrontendChildExecutableName(handoff)"),
        )
        for token in (
            "GetFinalPathNameByHandleW",
            "IsSupportedFinalDosExecutablePath",
            "PROC_THREAD_ATTRIBUTE_HANDLE_LIST",
            "STARTF_USESTDHANDLES",
            "absent_handle_sentinel",
            "JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE",
            "CREATE_SUSPENDED",
            "AssignProcessToJobObject",
            "ResumeThread",
            "ExitProcess(child_exit_code)",
            "execv(child_path.c_str()",
        ):
            with self.subTest(process_boundary_token=token):
                self.assertIn(token, source)
        self.assertLess(
            source.index("AssignProcessToJobObject"),
            source.index("ResumeThread"),
        )
        self.assertNotIn("CreateProcessA", source)
        self.assertNotIn("system(", source)
        self.assertNotIn("execvp", source)
        for token in (
            "foreign_platforms_rejected == 2U",
            "RoR-OgreNext.exe",
            "TestPosixExecFailureRestoresCloseOnExec",
            "--invoke-launcher-null-stdio",
            "--invoke-launcher-invalid-stdio",
            "CreateSymbolicLinkW",
            "regression skipped; CreateSymbolicLinkW error=",
        ):
            with self.subTest(launcher_test_token=token):
                self.assertIn(token, tests)

    def test_byte_hashed_probe_inputs_are_checkout_stable(self) -> None:
        attributes = (REPOSITORY_ROOT / ".gitattributes").read_text(
            encoding="utf-8"
        )
        for path in (
            "cmake/RendererLauncherPackageConfig.cmake",
            "source/main/gfx/RendererBackendPolicy.*",
            "source/main/gfx/RendererStartupHandoff.*",
            "source/main/gfx/RendererStartupPlan.*",
            "source/main/system/RendererChildIntent.*",
            "source/main/system/RendererChildLauncher.*",
            "source/main/system/RendererLauncherMain.cpp",
            "source/main/system/RendererLauncherPackageConfig.h.in",
            "source/main/system/RendererPublicLauncher.*",
            "source/main/gfx/render/**",
            "tests/gfx/RendererBackendPolicyTests.cpp",
            "tests/gfx/RendererChildIntentTests.cpp",
            "tests/gfx/RendererChildLauncherFakeChild.cpp",
            "tests/gfx/RendererChildLauncherTests.cpp",
            "tests/gfx/RendererPublicLauncherLegacyChild.cpp",
            "tests/gfx/RendererPublicLauncherTests.cpp",
            "tests/gfx/RendererStartupHandoffTests.cpp",
            "tests/gfx/RendererStartupPlanTests.cpp",
            "tools/ogre_next_probe/**",
            "tools/run_ogre_next_probe.py",
            "tools/validate_ogre_next_frame_probe.py",
            "tools/verify_ogre_next_artifact_set.py",
            "doc/nextgen/evidence/OGRE_NEXT_METAL_*",
        ):
            with self.subTest(path=path):
                self.assertIn(f"{path} text eol=lf", attributes)

    def test_reports_and_exact_frame_are_always_retained(self) -> None:
        self.assertIn("if: always()", self.workflow)
        self.assertIn("actions/upload-artifact@043fb46d", self.workflow)
        self.assertIn("if-no-files-found: error", self.workflow)
        for artifact in (
            "ogre-next-build-contract.json",
            "ror-ogre-next-probe-report.json",
            "ror-ogre-next-frame-probe-report.json",
            "ror-ogre-next-frame-probe.ppm",
            "ror-ogre-next-frontend-n1-report.json",
            "ror-ogre-next-frontend-n1.ppm",
            "ror-ogre-next-frontend-rt4-pbr-v1-report.json",
            "ror-ogre-next-frontend-rt4-pbr-v1.ppm",
            "ror-ogre-next-frontend-rt4-pbr-v1-isolation.bin",
            "ror-ogre-next-frontend-rt4-pbr-v1-hdr-compositor.bin",
            "ror-ogre-next-frontend-rt4-pbr-v1-repeat/",
            "ror-ogre-next-frontend-rt4-pbr-v1-attestation.json",
            "ror-ogre-next-pssm-shadow-report.json",
            "ror-ogre-next-pssm-shadow-isolation.bin",
            "bin/ror_ogre_next_pssm_shadow_smoke",
            "ror-ogre-next-n1-package/bin/ror_ogre_next_frontend_n1_smoke",
            "ror-ogre-next-n1-package/share/",
            "ror-ogre-next-metal-n2-report.json",
            "ror-ogre-next-metal-n2-probe.bin",
            "ror-ogre-next-metal-n2-attestation.json",
            "bin/ror_ogre_next_metal_n2_smoke",
            "ror-ogre-next-metal-n3-report.json",
            "ror-ogre-next-metal-n3-raster.bin",
            "ror-ogre-next-metal-n3-contribution.bin",
            "ror-ogre-next-metal-n3-hybrid.bin",
            "ror-ogre-next-metal-n3-attestation.json",
            "bin/ror_ogre_next_metal_n3_smoke",
            "ror-ogre-next-metal-n4-directional-shadow-report.json",
            "ror-ogre-next-metal-n4-raster.bin",
            "ror-ogre-next-metal-n4-visibility-r16.bin",
            "ror-ogre-next-metal-n4-ray-lineage-r32.bin",
            "ror-ogre-next-metal-n4-hybrid.bin",
            "bin/ror_ogre_next_metal_n4_directional_shadow_smoke",
        ):
            with self.subTest(artifact=artifact):
                self.assertIn(artifact, self.workflow)
        lifecycle = self.workflow.index("- name: Re-run native lifecycle tests")
        revalidate = self.workflow.index(
            "- name: Revalidate the exact artifacts selected for upload"
        )
        complete = self.workflow.index(
            "- name: Require every exact upload artifact"
        )
        n2_complete = self.workflow.index(
            "- name: Verify attested Apple Metal N2 pass or skip evidence"
        )
        n3_complete = self.workflow.index(
            "- name: Verify attested Apple Metal N3 pass or skip evidence"
        )
        n4_complete = self.workflow.index(
            "- name: Verify Apple Metal N4 directional-shadow pass or skip evidence"
        )
        upload = self.workflow.index("- name: Upload exact reports and UI-free frame")
        self.assertLess(lifecycle, revalidate)
        self.assertLess(revalidate, complete)
        self.assertLess(complete, n2_complete)
        self.assertLess(n2_complete, n3_complete)
        self.assertLess(n3_complete, n4_complete)
        self.assertLess(n4_complete, upload)
        self.assertIn("verify_ogre_next_artifact_set.py", self.workflow)
        self.assertIn("--verify-metal-n2-evidence", self.workflow)
        self.assertIn("--verify-metal-n3-evidence", self.workflow)
        self.assertIn("--verify-metal-n4-evidence", self.workflow)
        for anchor in (
            "ROR_OGRE_NEXT_EXPECTED_ROR_REPOSITORY",
            "ROR_OGRE_NEXT_EXPECTED_ROR_REF",
            "ROR_OGRE_NEXT_EXPECTED_ROR_COMMIT",
            "github.sha",
        ):
            self.assertIn(anchor, self.workflow)

    def test_n3_upload_is_a_self_contained_verifier_bundle(self) -> None:
        start = self.workflow.index(
            "- name: Upload attested Apple Metal N3 hybrid evidence"
        )
        end = self.workflow.index(
            "- name: Upload attested Vulkan RT5 external-device evidence"
        )
        bundle = self.workflow[start:end]
        for artifact in (
            "ogre-next-build-contract.json",
            "ror-ogre-next-frontend-rt4-pbr-v1-report.json",
            "ror-ogre-next-frontend-rt4-pbr-v1.ppm",
            "ror-ogre-next-frontend-rt4-pbr-v1-isolation.bin",
            "ror-ogre-next-frontend-rt4-pbr-v1-attestation.json",
            "ror-ogre-next-n1-package/bin/ror_ogre_next_frontend_n1_smoke",
            "ror-ogre-next-n1-package/share/",
            "ror-ogre-next-metal-n3-report.json",
            "ror-ogre-next-metal-n3-attestation.json",
            "bin/ror_ogre_next_metal_n3_smoke",
        ):
            with self.subTest(artifact=artifact):
                self.assertIn(artifact, bundle)

    def test_n4_upload_retains_report_executable_and_exact_readbacks(self) -> None:
        start = self.workflow.index(
            "- name: Upload verified Apple Metal N4 directional-shadow evidence"
        )
        end = self.workflow.index(
            "- name: Upload attested Vulkan RT5 external-device evidence"
        )
        bundle = self.workflow[start:end]
        for artifact in (
            "ogre-next-build-contract.json",
            "ror-ogre-next-metal-n4-directional-shadow-report.json",
            "ror-ogre-next-metal-n4-raster.bin",
            "ror-ogre-next-metal-n4-visibility-r16.bin",
            "ror-ogre-next-metal-n4-ray-lineage-r32.bin",
            "ror-ogre-next-metal-n4-hybrid.bin",
            "bin/ror_ogre_next_metal_n4_directional_shadow_smoke",
        ):
            with self.subTest(artifact=artifact):
                self.assertIn(artifact, bundle)

    def test_n1_is_independent_of_legacy_frame_runtime(self) -> None:
        n1 = self.workflow.index(
            "- name: Build, render, and validate the independent N1 frontend"
        )
        legacy = self.workflow.index(
            "- name: Build, render, read back, and validate the legacy probes"
        )
        n2 = self.workflow.index(
            "- name: Build and validate the independent Apple Metal N2 proof"
        )
        n3 = self.workflow.index(
            "- name: Build and validate the independent Apple Metal N3 hybrid proof"
        )
        n4 = self.workflow.index(
            "- name: Build and validate the Apple Metal N4 directional-shadow proof"
        )
        n1_native = self.workflow.index(
            "- name: Prove N1 lifecycle and media-integrity failures independently"
        )
        self.assertLess(n1, n1_native)
        self.assertLess(n1_native, n2)
        self.assertLess(n2, n3)
        self.assertLess(n3, n4)
        self.assertLess(n4, legacy)
        self.assertIn("--checkpoint n1", self.workflow)
        self.assertIn("--checkpoint n2", self.workflow)
        self.assertIn("--checkpoint n3", self.workflow)
        self.assertIn(
            "--target ror_ogre_next_metal_n4_directional_shadow_report",
            self.workflow,
        )
        self.assertIn("--checkpoint legacy", self.workflow)
        self.assertIn("--reuse-build-dir", self.workflow)
        self.assertIn(
            "-R '^ror_ogre_next_frontend_(n1_|rt4_)'", self.workflow
        )
        self.assertGreaterEqual(
            self.workflow.count("tools/validate_ogre_next_frame_probe.py"), 2
        )
        self.assertIn(
            "if: always() && runner.os == 'macOS'", self.workflow
        )
        self.assertIn(
            "Upload attested Apple Metal N2 capability evidence", self.workflow
        )
        self.assertIn(
            "Upload attested Apple Metal N3 hybrid evidence", self.workflow
        )
        self.assertIn(
            "Require directional PSSM native pass",
            self.workflow,
        )

    def test_verified_wrapper_owns_source_and_build_lifecycle(self) -> None:
        self.assertIn("tools/run_ogre_next_probe.py", self.workflow)
        self.assertNotIn("FETCHCONTENT_SOURCE_DIR_OGRE_NEXT", self.workflow)
        self.assertNotIn("git clone", self.workflow)
        self.assertNotIn("ogre-next master", self.workflow.lower())


if __name__ == "__main__":
    unittest.main()
