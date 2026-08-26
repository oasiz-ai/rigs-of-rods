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
HLSL_COMPATIBILITY_TOKEN = re.compile(
    r"\b(?:sampler1D|sampler2D|sampler3D|samplerCUBE|"
    r"tex1D|tex2D|tex3D|texCUBE)\b"
)
HLSL_LEGACY_OUTPUT = re.compile(
    r"\bout\s+float4\s+[A-Za-z_]\w*\s*:\s*(?:POSITION|COLOR)\b"
)
HLSL_RESOURCE_COUNTS = {
    "SkyX_Clouds.hlsl": (3, 3, 4),
    "SkyX_Ground.hlsl": (0, 0, 0),
    "SkyX_Lightning.hlsl": (0, 0, 0),
    "SkyX_Moon.hlsl": (2, 2, 3),
    "SkyX_Skydome.hlsl": (1, 1, 2),
    "SkyX_VolClouds.hlsl": (3, 3, 3),
    "SkyX_VolClouds_Lightning.hlsl": (3, 3, 3),
}


def declarations(source: str, direction: str) -> dict[str, str]:
    return {
        match.group("name"): match.group("type")
        for match in DECLARATION.finditer(source)
        if match.group("direction") == direction
    }


class SkyXGlslContractTests(unittest.TestCase):
    def test_hlsl_routes_and_sources_are_strict_shader_model_4(self) -> None:
        material = MATERIAL_PATH.read_text(encoding="utf-8")
        self.assertEqual(material.count("target vs_4_0"), 7)
        self.assertEqual(material.count("target ps_4_0"), 12)
        self.assertNotRegex(material, r"(?m)^\s*target\s+(?:vs_[12]|ps_[123])")
        self.assertNotIn("enable_backward_compatibility", material)

        self.assertEqual(
            {path.name for path in SKYX.glob("*.hlsl")},
            set(HLSL_RESOURCE_COUNTS),
        )
        for name, (texture_count, sampler_count, sample_count) in sorted(
            HLSL_RESOURCE_COUNTS.items()
        ):
            with self.subTest(source=name):
                source = (SKYX / name).read_text(encoding="utf-8")
                self.assertEqual(source.count("void main_vp("), 1)
                self.assertEqual(source.count("void main_fp("), 1)
                self.assertEqual(source.count(": SV_Position"), 1)
                self.assertEqual(source.count(": SV_Target"), 1)
                self.assertIsNone(HLSL_COMPATIBILITY_TOKEN.search(source))
                self.assertIsNone(HLSL_LEGACY_OUTPUT.search(source))
                self.assertEqual(
                    len(re.findall(r"(?m)^Texture(?:1D|2D|3D|Cube)\b", source)),
                    texture_count,
                )
                self.assertEqual(
                    len(re.findall(r"(?m)^SamplerState\b", source)),
                    sampler_count,
                )
                self.assertEqual(source.count(".Sample("), sample_count)

        lightning = (SKYX / "SkyX_Lightning.hlsl").read_text(
            encoding="utf-8"
        )
        fragment_start = lightning.index("void main_fp(")
        fragment = lightning[fragment_start:]
        self.assertEqual(fragment.count("float3 uv = iUV;"), 1)
        self.assertNotRegex(fragment, r"(?m)^\s*iUV\.[xyz]\s*[*+-]?=")
        self.assertIn("pow(max(intensity,0.0f)", fragment)

        lightning_glsl = (SKYX / "SkyX_Lightning.fragment").read_text(
            encoding="utf-8"
        )
        self.assertIn("pow(max(intensity,0.0)", lightning_glsl)

        moon_hlsl = (SKYX / "SkyX_Moon.hlsl").read_text(encoding="utf-8")
        self.assertIn(
            "pow(max(haloIntensity, 0.0f), uMoonPhase.z)", moon_hlsl
        )
        self.assertIn(
            "pow(max(oColor.a, 0.0f),2*haloIntensity)", moon_hlsl
        )
        moon_glsl = (SKYX / "SkyX_Moon.fragment").read_text(encoding="utf-8")
        self.assertIn(
            "pow(max(haloIntensity, 0.0), uMoonPhase.z)", moon_glsl
        )
        self.assertIn(
            "pow(max(fragColor.a, 0.0),2.0*haloIntensity)", moon_glsl
        )

    def test_fractional_pow_inputs_are_domain_safe_in_both_backends(self) -> None:
        skydome_hlsl = (SKYX / "SkyX_Skydome.hlsl").read_text(
            encoding="utf-8"
        )
        skydome_glsl = (SKYX / "SkyX_Skydome.fragment").read_text(
            encoding="utf-8"
        )
        normalized_hlsl = " ".join(skydome_hlsl.split())
        normalized_glsl = " ".join(skydome_glsl.split())

        self.assertIn(
            "float mieBase = max( 1.0f + uG2 - 2.0f * uG * cos, "
            "1.0e-6f);",
            normalized_hlsl,
        )
        self.assertIn("pow(mieBase, 1.5f)", normalized_hlsl)
        self.assertIn(
            "pow(max(nightSky, float3(0.0f, 0.0f, 0.0f)), 2.2f)",
            normalized_hlsl,
        )
        self.assertIn(
            "float mieBase = max( 1.0 + uG2 - 2.0 * uG * cos, 1.0e-6);",
            normalized_glsl,
        )
        self.assertIn("pow(mieBase, 1.5)", normalized_glsl)
        self.assertIn(
            "pow(max(nightSky, vec3(0.0)), vec3(2.2))",
            normalized_glsl,
        )

        for hlsl_name, glsl_name in (
            ("SkyX_VolClouds.hlsl", "SkyX_VolClouds.fragment"),
            (
                "SkyX_VolClouds_Lightning.hlsl",
                "SkyX_VolClouds_Lightning.fragment",
            ),
        ):
            with self.subTest(shader=hlsl_name):
                hlsl = " ".join(
                    (SKYX / hlsl_name).read_text(encoding="utf-8").split()
                )
                glsl = " ".join(
                    (SKYX / glsl_name).read_text(encoding="utf-8").split()
                )
                self.assertIn("pow(max(iDistance, 0.0f), 1.5f)", hlsl)
                self.assertIn("pow(max(vDistance, 0.0), 1.5)", glsl)
                self.assertNotIn("pow(iDistance,", hlsl)
                self.assertNotIn("pow(vDistance,", glsl)

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
