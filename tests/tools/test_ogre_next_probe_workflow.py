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

    def test_every_probe_layer_is_required_in_normal_and_optimized_python(self) -> None:
        for test_path in (
            "tests/tools/test_ogre_next_probe_contract.py",
            "tests/tools/test_ogre_next_frame_probe.py",
            SELF_PATH,
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
            "libshaderc-dev",
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

    def test_linux_selects_shadercs_packaged_shared_abi(self) -> None:
        cmake = (
            REPOSITORY_ROOT / "tools" / "ogre_next_probe" / "CMakeLists.txt"
        ).read_text(encoding="utf-8")
        self.assertIn("ROR_OGRE_NEXT_SHADERC_SHARED_LIBRARY", cmake)
        self.assertIn("NAMES shaderc_shared shaderc", cmake)
        self.assertIn('MATCHES "\\\\.a$"', cmake)
        self.assertIn("set(Vulkan_SHADERC_LIB_REL", cmake)
        self.assertIn("set(Vulkan_SHADERC_LIB_DBG", cmake)
        self.assertNotIn("find_package(glslang", cmake)
        self.assertNotIn("glslang::", cmake)
        self.assertNotIn("glslang-dev", self.workflow)
        self.assertNotIn("glslang-tools", self.workflow)

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
        ):
            with self.subTest(artifact=artifact):
                self.assertIn(artifact, self.workflow)
        lifecycle = self.workflow.index("- name: Re-run native lifecycle tests")
        revalidate = self.workflow.index(
            "- name: Revalidate the exact artifacts selected for upload"
        )
        upload = self.workflow.index("- name: Upload exact reports and UI-free frame")
        self.assertLess(lifecycle, revalidate)
        self.assertLess(revalidate, upload)
        self.assertGreaterEqual(
            self.workflow.count("tools/validate_ogre_next_frame_probe.py"), 2
        )

    def test_verified_wrapper_owns_source_and_build_lifecycle(self) -> None:
        self.assertIn("tools/run_ogre_next_probe.py", self.workflow)
        self.assertNotIn("FETCHCONTENT_SOURCE_DIR_OGRE_NEXT", self.workflow)
        self.assertNotIn("git clone", self.workflow)
        self.assertNotIn("ogre-next master", self.workflow.lower())


if __name__ == "__main__":
    unittest.main()
