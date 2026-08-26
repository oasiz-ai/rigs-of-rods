#!/usr/bin/env python3
"""Fail closed on the packaged SkyX core-GLSL source contract."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
SKYX = ROOT / "resources/SkyX"
MATERIAL_PATH = SKYX / "SkyX.material"
NATIVE_WORKFLOW = (
    ROOT / ".github/workflows/ogre-next-combined-native.yml"
).read_text(encoding="utf-8")
TSAN_WORKFLOW = (
    ROOT / ".github/workflows/ogre-next-combined-tsan.yml"
).read_text(encoding="utf-8")

SHADER_INTERFACES = {
    "SkyX_Clouds": {"vPosition": "vec3"},
    "SkyX_Ground": {
        "vDirection": "vec3",
        "vRayleighColor": "vec3",
    },
    "SkyX_Lightning": {"vData": "vec4", "vUV": "vec3"},
    "SkyX_Moon": {"vUVYLength": "vec4"},
    "SkyX_Skydome": {
        "vDirection": "vec3",
        "vHeight": "float",
        "vMieColor": "vec3",
        "vOpacity": "float",
        "vRayleighColor": "vec3",
        "vUV": "vec2",
    },
    "SkyX_VolClouds": {
        "v3DCoord": "vec3",
        "vDistance": "float",
        "vEyePixel": "vec3",
        "vNoiseUV": "vec2",
        "vOpacity": "float",
    },
    "SkyX_VolClouds_Lightning": {
        "v3DCoord": "vec3",
        "vDistance": "float",
        "vEyePixel": "vec3",
        "vNoiseUV": "vec2",
        "vOpacity": "float",
        "vPositionAtt": "vec4",
    },
}

DECLARATION = re.compile(
    r"^(?P<direction>in|out)\s+"
    r"(?P<type>float|vec[234])\s+"
    r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*;\s*(?://.*)?$",
    re.MULTILINE,
)
COMPATIBILITY_TOKEN = re.compile(
    r"\b(?:attribute|varying|texture2D|texture3D|gl_FragColor|"
    r"gl_ModelViewProjectionMatrix|gl_TexCoord|gl_Color|gl_Vertex)\b"
)


def declarations(source: str, direction: str) -> dict[str, str]:
    return {
        match.group("name"): match.group("type")
        for match in DECLARATION.finditer(source)
        if match.group("direction") == direction
    }


class SkyXGlslContractTests(unittest.TestCase):
    def test_all_shader_pairs_are_core_glsl150_with_exact_interfaces(self) -> None:
        expected_stems = set(SHADER_INTERFACES)
        self.assertEqual(
            {path.stem for path in SKYX.glob("*.vertex")},
            expected_stems,
        )
        self.assertEqual(
            {path.stem for path in SKYX.glob("*.fragment")},
            expected_stems,
        )

        for stem, interface in sorted(SHADER_INTERFACES.items()):
            with self.subTest(shader=stem):
                vertex = (SKYX / f"{stem}.vertex").read_text(
                    encoding="utf-8"
                )
                fragment = (SKYX / f"{stem}.fragment").read_text(
                    encoding="utf-8"
                )

                for stage, source in (("vertex", vertex), ("fragment", fragment)):
                    with self.subTest(stage=stage):
                        self.assertTrue(source.startswith("#version 150\n"))
                        self.assertEqual(source.count("#version 150"), 1)
                        self.assertIsNone(COMPATIBILITY_TOKEN.search(source))

                self.assertNotIn("gl_PerVertex", vertex)
                self.assertEqual(vertex.count("uniform mat4 uWorldViewProj;"), 1)
                self.assertEqual(
                    vertex.count("gl_Position = uWorldViewProj * vertex;"),
                    1,
                )
                self.assertEqual(declarations(vertex, "out"), interface)
                self.assertEqual(declarations(fragment, "in"), interface)
                self.assertEqual(fragment.count("out vec4 fragColor;"), 1)
                self.assertGreaterEqual(fragment.count("fragColor"), 2)

        skydome_fragment = (
            SKYX / "SkyX_Skydome.fragment"
        ).read_text(encoding="utf-8")
        self.assertEqual(skydome_fragment.count(", vec3(2.2))"), 2)
        self.assertNotIn(", 2.2)", skydome_fragment)

    def test_glsl_vertex_programs_bind_the_explicit_transform_uniform(self) -> None:
        material = MATERIAL_PATH.read_text(encoding="utf-8")
        binding = "param_named_auto uWorldViewProj worldviewproj_matrix"

        for stem in sorted(SHADER_INTERFACES):
            program = f"vertex_program {stem}_VP_GLSL glsl"
            start = material.index(program)
            end = material.index("\nvertex_program ", start + len(program))
            block = material[start:end]
            with self.subTest(program=program):
                self.assertEqual(block.count(f"source {stem}.vertex"), 1)
                self.assertEqual(block.count(binding), 1)

        # The seven existing HLSL programs and the seven converted GLSL
        # programs must each own exactly one transform binding.
        self.assertEqual(material.count(binding), 14)
        self.assertEqual(material.count("texture SkyX_Starfield.png\n"), 2)
        self.assertNotIn("texture SkyX_Starfield.png gamma", material)

    def test_native_and_tsan_workflows_trigger_and_run_the_contract(self) -> None:
        test_path = "tests/tools/test_skyx_glsl_contract.py"
        unsupported_program_gate = (
            "Program '[^']+\\\\.(glsl|glsles)' is not supported"
        )
        skyx_program_gate = "Program 'SkyX_[^']+' is not supported"
        skyx_material_gate = "Error: ScriptCompiler .*SkyX\\\\.material"
        windows_program_gate = "Program '[^']+' is not supported"
        windows_material_gate = "Error: ScriptCompiler .*SkyX\\.material"

        self.assertEqual(NATIVE_WORKFLOW.count("- resources/SkyX/**"), 2)
        self.assertEqual(NATIVE_WORKFLOW.count(f"- {test_path}"), 2)
        self.assertEqual(NATIVE_WORKFLOW.count(f"python {test_path}"), 2)
        self.assertEqual(NATIVE_WORKFLOW.count(f"python -O {test_path}"), 2)
        self.assertEqual(
            NATIVE_WORKFLOW.count(f"@('{test_path}')"),
            1,
        )
        self.assertEqual(
            NATIVE_WORKFLOW.count(f"@('-O', '{test_path}')"),
            1,
        )
        self.assertIn(unsupported_program_gate, NATIVE_WORKFLOW)
        self.assertGreaterEqual(NATIVE_WORKFLOW.count(skyx_program_gate), 2)
        self.assertGreaterEqual(NATIVE_WORKFLOW.count(skyx_material_gate), 2)
        self.assertEqual(
            NATIVE_WORKFLOW.count("$shaderDiagnostics = Select-String"),
            1,
        )
        self.assertEqual(NATIVE_WORKFLOW.count(windows_program_gate), 1)
        self.assertEqual(NATIVE_WORKFLOW.count(windows_material_gate), 1)

        self.assertEqual(TSAN_WORKFLOW.count("- resources/SkyX/**"), 1)
        self.assertEqual(TSAN_WORKFLOW.count(f"- {test_path}"), 1)
        self.assertEqual(TSAN_WORKFLOW.count(f"python {test_path}"), 1)
        self.assertEqual(TSAN_WORKFLOW.count(f"python -O {test_path}"), 1)
        self.assertIn(unsupported_program_gate, TSAN_WORKFLOW)
        self.assertEqual(TSAN_WORKFLOW.count(skyx_program_gate), 1)
        self.assertEqual(TSAN_WORKFLOW.count(skyx_material_gate), 1)


if __name__ == "__main__":
    unittest.main()
