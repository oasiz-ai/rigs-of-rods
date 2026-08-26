#!/usr/bin/env python3
"""Fail closed on the packaged MyGUI GLSL 130 shader contract."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
MYGUI = ROOT / "resources/mygui"
NATIVE_WORKFLOW = (
    ROOT / ".github/workflows/ogre-next-combined-native.yml"
).read_text(encoding="utf-8")
TSAN_WORKFLOW = (
    ROOT / ".github/workflows/ogre-next-combined-tsan.yml"
).read_text(encoding="utf-8")


class MyGuiGlslContractTests(unittest.TestCase):
    def test_glsl130_vertex_source_uses_the_ogre_sso_injection_path(self) -> None:
        vertex = (MYGUI / "MyGUI_VP.glsl").read_text(encoding="utf-8")
        reference = (MYGUI / "MyGUI_Ogre_VP.glsl").read_text(
            encoding="utf-8"
        )

        self.assertTrue(vertex.startswith("#version 130\n"))
        self.assertNotIn("gl_PerVertex", vertex)
        self.assertNotIn("vec4 gl_Position", vertex)
        for contract in (
            "in vec4 position;",
            "in vec4 uv0;",
            "in vec4 colour;",
            "uniform float YFlipScale;",
            "out vec4 outUV0;",
            "out vec4 outColor;",
            "gl_Position = vpos;",
        ):
            with self.subTest(contract=contract):
                self.assertEqual(vertex.count(contract), 1)
                self.assertEqual(reference.count(contract), 1)

    def test_glsl130_fragment_interface_matches_the_vertex_source(self) -> None:
        fragment = (MYGUI / "MyGUI_FP.glsl").read_text(encoding="utf-8")
        self.assertTrue(fragment.startswith("#version 130\n"))
        for contract in (
            "in vec4 outUV0;",
            "in vec4 outColor;",
            "out vec4 fragColor;",
            "texture(sampler, outUV0.xy)",
        ):
            with self.subTest(contract=contract):
                self.assertEqual(fragment.count(contract), 1)

    def test_combined_runtime_workflows_run_and_trigger_the_contract(self) -> None:
        for workflow in (NATIVE_WORKFLOW, TSAN_WORKFLOW):
            with self.subTest(workflow="native-or-tsan"):
                self.assertIn("- resources/mygui/**", workflow)
                self.assertIn(
                    "- tests/tools/test_mygui_glsl_contract.py", workflow
                )
                self.assertIn(
                    "python tests/tools/test_mygui_glsl_contract.py", workflow
                )
                self.assertIn(
                    "python -O tests/tools/test_mygui_glsl_contract.py",
                    workflow,
                )
                self.assertIn("MyGUI GLSL compilation failed", workflow)
                self.assertIn("is not supported", workflow)


if __name__ == "__main__":
    unittest.main()
