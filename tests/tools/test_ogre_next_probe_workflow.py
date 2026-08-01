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
            "CMakeLists.txt",
            "source/main/CMakeLists.txt",
            "tests/CMakeLists.txt",
            "source/main/gfx/RendererBackendPolicy.*",
            "source/main/gfx/render/**",
            "tests/gfx/RendererBackendPolicyTests.cpp",
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

        for prohibited in ("libshaderc-dev", "glslang-dev", "spirv-tools"):
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
        self.assertIn("MachineIndependent|GenericCodeGen|OSDependent", self.workflow)
        self.assertIn("ror_ogre_next_frame_probe", self.workflow)
        self.assertIn("ror_ogre_next_frontend_n1_smoke", self.workflow)
        self.assertIn(
            "ror-ogre-next-frontend-rt4-pbr-v1-isolation.bin",
            self.workflow,
        )

    def test_byte_hashed_probe_inputs_are_checkout_stable(self) -> None:
        attributes = (REPOSITORY_ROOT / ".gitattributes").read_text(
            encoding="utf-8"
        )
        for path in (
            "tools/ogre_next_probe/**",
            "tools/run_ogre_next_probe.py",
            "tools/validate_ogre_next_frame_probe.py",
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
            "ror-ogre-next-frontend-rt4-pbr-v1-attestation.json",
            "ror-ogre-next-pssm-shadow-report.json",
            "ror-ogre-next-pssm-shadow-isolation.bin",
            "bin/ror_ogre_next_pssm_shadow_smoke",
            "ror-ogre-next-n1-package/bin/ror_ogre_next_frontend_n1_smoke",
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
        upload = self.workflow.index("- name: Upload exact reports and UI-free frame")
        self.assertLess(lifecycle, revalidate)
        self.assertLess(revalidate, complete)
        self.assertLess(complete, n2_complete)
        self.assertLess(n2_complete, n3_complete)
        self.assertLess(n3_complete, upload)
        self.assertIn("verify_ogre_next_artifact_set.py", self.workflow)
        self.assertIn("--verify-metal-n2-evidence", self.workflow)
        self.assertIn("--verify-metal-n3-evidence", self.workflow)
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
            "ror-ogre-next-metal-n3-report.json",
            "ror-ogre-next-metal-n3-attestation.json",
            "bin/ror_ogre_next_metal_n3_smoke",
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
        n1_native = self.workflow.index(
            "- name: Prove N1 lifecycle and media-integrity failures independently"
        )
        self.assertLess(n1, n1_native)
        self.assertLess(n1_native, n2)
        self.assertLess(n2, n3)
        self.assertLess(n3, legacy)
        self.assertIn("--checkpoint n1", self.workflow)
        self.assertIn("--checkpoint n2", self.workflow)
        self.assertIn("--checkpoint n3", self.workflow)
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
            "Require directional PSSM pass or explicit unsupported evidence",
            self.workflow,
        )

    def test_verified_wrapper_owns_source_and_build_lifecycle(self) -> None:
        self.assertIn("tools/run_ogre_next_probe.py", self.workflow)
        self.assertNotIn("FETCHCONTENT_SOURCE_DIR_OGRE_NEXT", self.workflow)
        self.assertNotIn("git clone", self.workflow)
        self.assertNotIn("ogre-next master", self.workflow.lower())


if __name__ == "__main__":
    unittest.main()
