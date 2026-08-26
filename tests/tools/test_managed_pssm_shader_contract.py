#!/usr/bin/env python3
"""Fail closed on the modern managed-material PSSM shader contract."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
FAMILY = ROOT / "resources/managed_materials/shadows/pssm/on"
PROGRAM_PATH = FAMILY / "depthshadows.program"
GLSL_PATH = FAMILY / "pssm_gl3plus.glsl"
HLSL_PATH = FAMILY / "pssm_d3d11.hlsl"
RECEIVER_MATERIAL_PATH = FAMILY / "shadows.material"
CASTER_MATERIAL_PATH = FAMILY / "shadows_depth.material"
SHARED_PATH = FAMILY / "shared/pssm_shared.program"

PROGRAM_SCRIPT = PROGRAM_PATH.read_text(encoding="utf-8")
GLSL = GLSL_PATH.read_text(encoding="utf-8")
HLSL = HLSL_PATH.read_text(encoding="utf-8")
RECEIVER_MATERIAL = RECEIVER_MATERIAL_PATH.read_text(encoding="utf-8")
CASTER_MATERIAL = CASTER_MATERIAL_PATH.read_text(encoding="utf-8")
SHARED = SHARED_PATH.read_text(encoding="utf-8")

PUBLIC_PROGRAMS = {
    "PSSM/shadow_caster_vs": "vertex",
    "PSSM/shadow_caster_ps": "fragment",
    "PSSM/shadow_caster_alpha_ps": "fragment",
    "PSSM/shadow_receiver_vs": "vertex",
    "PSSM/shadow_receiver_ps": "fragment",
}

SELECTORS = {
    "PSSM/shadow_caster_vs": "PSSM_CASTER_VERTEX=1",
    "PSSM/shadow_caster_ps": "PSSM_CASTER_FRAGMENT=1",
    "PSSM/shadow_caster_alpha_ps": "PSSM_CASTER_ALPHA_FRAGMENT=1",
    "PSSM/shadow_receiver_vs": "PSSM_RECEIVER_VERTEX=1",
    "PSSM/shadow_receiver_ps": "PSSM_RECEIVER_FRAGMENT=1",
}

HLSL_ENTRIES = {
    "PSSM/shadow_caster_vs": ("PssmShadowCasterVS", "vs_4_0"),
    "PSSM/shadow_caster_ps": ("PssmShadowCasterPS", "ps_4_0"),
    "PSSM/shadow_caster_alpha_ps": (
        "PssmShadowCasterAlphaPS",
        "ps_4_0",
    ),
    "PSSM/shadow_receiver_vs": ("PssmShadowReceiverVS", "vs_4_0"),
    "PSSM/shadow_receiver_ps": ("PssmShadowReceiverPS", "ps_4_0"),
}

CASTER_VERTEX_PARAMS = {
    "param_named_auto wvpMat worldviewproj_matrix",
}
RECEIVER_VERTEX_PARAMS = {
    "param_named_auto lightPosition light_position_object_space 0",
    "param_named_auto eyePosition camera_position_object_space",
    "param_named_auto worldViewProjMatrix worldviewproj_matrix",
    "param_named_auto texWorldViewProjMatrix0 texture_worldviewproj_matrix 0",
    "param_named_auto texWorldViewProjMatrix1 texture_worldviewproj_matrix 1",
    "param_named_auto texWorldViewProjMatrix2 texture_worldviewproj_matrix 2",
}
RECEIVER_FRAGMENT_PARAMS = {
    "param_named_auto lightDiffuse derived_light_diffuse_colour 0",
    "param_named_auto lightSpecular derived_light_specular_colour 0",
    "param_named_auto ambient derived_ambient_light_colour",
    "param_named_auto invShadowMapSize0 inverse_texture_size 0",
    "param_named_auto invShadowMapSize1 inverse_texture_size 1",
    "param_named_auto invShadowMapSize2 inverse_texture_size 2",
}
GLSL_RECEIVER_SAMPLERS = {
    "param_named shadowMap0 int 0",
    "param_named shadowMap1 int 1",
    "param_named shadowMap2 int 2",
    "param_named diffuse int 3",
    "param_named specular int 4",
    "param_named normalMap int 5",
}

DECLARATION = re.compile(
    r"^(?P<stage>vertex|fragment)_program\s+"
    r"(?P<name>\S+)\s+(?P<language>\S+)\s*$",
    re.MULTILINE,
)
PARAMETER = re.compile(
    r"^\s*(param_named(?:_auto)?\s+[^/\r\n]+?)\s*$", re.MULTILINE
)


@dataclass(frozen=True)
class Program:
    stage: str
    name: str
    language: str
    body: str


def brace_block(source: str, declaration_start: int) -> str:
    opening = source.index("{", declaration_start)
    depth = 0
    for offset, character in enumerate(source[opening:], start=opening):
        if character == "{":
            depth += 1
        elif character == "}":
            depth -= 1
            if depth == 0:
                return source[declaration_start : offset + 1]
    raise AssertionError("unclosed script block")


def parse_programs(source: str) -> dict[str, Program]:
    programs: dict[str, Program] = {}
    for declaration in DECLARATION.finditer(source):
        name = declaration.group("name")
        if name in programs:
            raise AssertionError(f"duplicate GPU program: {name}")
        programs[name] = Program(
            stage=declaration.group("stage"),
            name=name,
            language=declaration.group("language"),
            body=brace_block(source, declaration.start()),
        )
    return programs


def normalized_lines(body: str, prefix: str) -> list[str]:
    return [
        " ".join(line.strip().split())
        for line in body.splitlines()
        if line.strip().startswith(prefix)
    ]


def normalized_parameters(program: Program) -> set[str]:
    return {
        " ".join(match.group(1).split())
        for match in PARAMETER.finditer(program.body)
    }


class ManagedPssmShaderContractTests(unittest.TestCase):
    def test_family_has_no_legacy_shader_or_program_language(self) -> None:
        legacy = sorted(
            path.relative_to(ROOT).as_posix()
            for path in FAMILY.rglob("*")
            if path.is_file() and path.suffix.lower() in {".cg", ".asm"}
        )
        self.assertEqual(legacy, [])
        self.assertNotRegex(PROGRAM_SCRIPT, r"(?m)^\s*(?:vertex|fragment)_program\s+\S+\s+cg\s*$")
        self.assertNotIn("profiles ", PROGRAM_SCRIPT)
        self.assertNotIn("source pssm.cg", PROGRAM_SCRIPT)

    def test_public_aliases_delegate_only_to_gl3plus_and_d3d11(self) -> None:
        programs = parse_programs(PROGRAM_SCRIPT)
        expected_names = set(PUBLIC_PROGRAMS)
        expected_names.update(
            f"{public}/{backend}"
            for public in PUBLIC_PROGRAMS
            for backend in ("GL3Plus", "D3D11")
        )
        self.assertEqual(set(programs), expected_names)

        for public, stage in PUBLIC_PROGRAMS.items():
            with self.subTest(program=public):
                self.assertEqual(
                    (programs[public].stage, programs[public].language),
                    (stage, "unified"),
                )
                self.assertEqual(
                    normalized_lines(programs[public].body, "delegate"),
                    [
                        f"delegate {public}/GL3Plus",
                        f"delegate {public}/D3D11",
                    ],
                )

    def test_backend_sources_selectors_entries_and_targets_are_exact(self) -> None:
        programs = parse_programs(PROGRAM_SCRIPT)
        for public, stage in PUBLIC_PROGRAMS.items():
            for backend, language, source in (
                ("GL3Plus", "glsl", "pssm_gl3plus.glsl"),
                ("D3D11", "hlsl", "pssm_d3d11.hlsl"),
            ):
                program = programs[f"{public}/{backend}"]
                with self.subTest(program=public, backend=backend):
                    self.assertEqual((program.stage, program.language), (stage, language))
                    self.assertEqual(
                        normalized_lines(program.body, "source"),
                        [f"source {source}"],
                    )
                    self.assertEqual(
                        normalized_lines(program.body, "preprocessor_defines"),
                        [f"preprocessor_defines {SELECTORS[public]}"],
                    )
                    if backend == "GL3Plus":
                        self.assertEqual(
                            normalized_lines(program.body, "syntax"),
                            ["syntax glsl330"],
                        )
                        self.assertEqual(
                            normalized_lines(program.body, "entry_point"), []
                        )
                        self.assertEqual(
                            normalized_lines(program.body, "target"), []
                        )
                    else:
                        entry, target = HLSL_ENTRIES[public]
                        self.assertEqual(
                            normalized_lines(program.body, "entry_point"),
                            [f"entry_point {entry}"],
                        )
                        self.assertEqual(
                            normalized_lines(program.body, "target"),
                            [f"target {target}"],
                        )

    def test_parameter_bindings_and_sampler_slots_are_preserved(self) -> None:
        programs = parse_programs(PROGRAM_SCRIPT)
        for backend in ("GL3Plus", "D3D11"):
            with self.subTest(backend=backend, program="caster_vs"):
                self.assertEqual(
                    normalized_parameters(
                        programs[f"PSSM/shadow_caster_vs/{backend}"]
                    ),
                    CASTER_VERTEX_PARAMS,
                )
            with self.subTest(backend=backend, program="receiver_vs"):
                self.assertEqual(
                    normalized_parameters(
                        programs[f"PSSM/shadow_receiver_vs/{backend}"]
                    ),
                    RECEIVER_VERTEX_PARAMS,
                )
            expected_receiver = set(RECEIVER_FRAGMENT_PARAMS)
            if backend == "GL3Plus":
                expected_receiver.update(GLSL_RECEIVER_SAMPLERS)
            with self.subTest(backend=backend, program="receiver_ps"):
                receiver = programs[f"PSSM/shadow_receiver_ps/{backend}"]
                self.assertEqual(
                    normalized_parameters(receiver), expected_receiver
                )
                self.assertEqual(
                    normalized_lines(receiver.body, "shared_params_ref"),
                    ["shared_params_ref pssm_params"],
                )

        self.assertEqual(
            normalized_parameters(
                programs["PSSM/shadow_caster_alpha_ps/GL3Plus"]
            ),
            {"param_named alphaMap int 0"},
        )
        self.assertEqual(
            normalized_parameters(
                programs["PSSM/shadow_caster_alpha_ps/D3D11"]
            ),
            set(),
        )

        samplers = tuple(f"shadowMap{index}" for index in range(3)) + (
            "diffuse",
            "specular",
            "normalMap",
        )
        for slot, sampler in enumerate(samplers):
            with self.subTest(sampler=sampler):
                self.assertIn(
                    f"Texture2D {sampler} : register(t{slot});", HLSL
                )
                self.assertIn(
                    f"SamplerState {sampler}Sampler : register(s{slot});",
                    HLSL,
                )
        self.assertIn("Texture2D alphaMap : register(t0);", HLSL)
        self.assertIn("SamplerState alphaMapSampler : register(s0);", HLSL)

    def test_caster_depth_and_alpha_cutout_semantics_are_preserved(self) -> None:
        self.assertIn("pssmDepth = gl_Position.zw;", GLSL)
        self.assertIn("gl_Position.z = max(gl_Position.z, 0.0);", GLSL)
        self.assertIn("pssmDepth.x / pssmDepth.y", GLSL)
        self.assertIn("texture(alphaMap, pssmUv).a", GLSL)
        self.assertIn("if (alpha <= 0.5)", GLSL)
        self.assertIn("discard;", GLSL)

        self.assertIn("output.depth = output.clipPosition.zw;", HLSL)
        self.assertIn(
            "output.clipPosition.z = max(output.clipPosition.z, 0.0);",
            HLSL,
        )
        self.assertIn("input.depth.x / input.depth.y", HLSL)
        self.assertIn(
            "alphaMap.Sample(alphaMapSampler, input.uv).a", HLSL
        )
        self.assertIn("clip(alpha > 0.5 ? 1.0 : -1.0);", HLSL)

    def test_receiver_split_projection_bias_and_pcf_are_preserved(self) -> None:
        for source, uv, light_position in (
            (GLSL, "pssmUv", "pssmLightPosition"),
            (HLSL, "input.uv", "input.lightPosition"),
        ):
            with self.subTest(source="GLSL" if source is GLSL else "HLSL"):
                self.assertIn(f"{uv}.z <= pssmSplitPoints.y", source)
                self.assertIn(f"{uv}.z <= pssmSplitPoints.z", source)
                for index in range(3):
                    self.assertIn(f"shadowMap{index}", source)
                    self.assertIn(f"{light_position}{index}", source)
                    self.assertIn(f"invShadowMapSize{index}.xy", source)
                self.assertIn(
                    "sampleOffset = vec3(offset, -offset.x) * 0.3"
                    if source is GLSL
                    else "sampleOffset = float3(offset, -offset.x) * 0.3",
                    source,
                )
                self.assertEqual(source.count("sampleOffset.xy).r"), 2)
                self.assertEqual(source.count("sampleOffset.zy).r"), 2)
                self.assertIn(
                    "float shadowDepth = shadowMapPosition.z;", source
                )
                self.assertNotRegex(
                    source,
                    r"shadowDepth\s*=\s*shadowMapPosition\.z\s*[+-]",
                )
                self.assertIn("return coverage / 5.0;", source)

    def test_receiver_lighting_and_alpha_semantics_are_preserved(self) -> None:
        for source, diffuse_sample, specular_sample, output_alpha in (
            (
                GLSL,
                "texture(diffuse, pssmUv.xy)",
                "texture(specular, pssmUv.xy)",
                "fragColour.a = diffuseColour.a;",
            ),
            (
                HLSL,
                "diffuse.Sample(diffuseSampler, input.uv.xy)",
                "specular.Sample(\n        specularSampler, input.uv.xy)",
                "colour.a = diffuseColour.a;",
            ),
        ):
            with self.subTest(source="GLSL" if source is GLSL else "HLSL"):
                self.assertIn(diffuse_sample, source)
                self.assertIn(specular_sample, source)
                self.assertIn("float shininess = specularColour.a;", source)
                self.assertIn("shininess * 52.0", source)
                self.assertIn(
                    "pow(max(nDotH, 0.0), shininess * 52.0)", source
                )
                self.assertNotIn("pow(nDotH,", source)
                self.assertIn("0.3 + 0.7 * shadowing", source)
                self.assertIn("lightDiffuse * diffuseTerm * shadowScale", source)
                self.assertIn(
                    "lightSpecular * specularColour", source
                )
                self.assertIn(output_alpha, source)

        # The legacy input exists to preserve the six-sampler layout but was
        # intentionally never sampled by this receiver implementation.
        self.assertIn("uniform sampler2D normalMap;", GLSL)
        self.assertNotIn("texture(normalMap", GLSL)
        self.assertIn("Texture2D normalMap : register(t5);", HLSL)
        self.assertNotIn("normalMap.Sample", HLSL)

    def test_material_and_shared_parameter_references_are_unchanged(self) -> None:
        self.assertEqual(
            RECEIVER_MATERIAL.count(
                "vertex_program_ref PSSM/shadow_receiver_vs"
            ),
            1,
        )
        self.assertEqual(
            RECEIVER_MATERIAL.count(
                "fragment_program_ref PSSM/shadow_receiver_ps"
            ),
            1,
        )
        texture_units = re.findall(
            r"^[ \t]*texture_unit[ \t]+(\S+)",
            RECEIVER_MATERIAL,
            re.MULTILINE,
        )
        self.assertEqual(
            texture_units,
            ["shadow_tex0", "shadow_tex1", "shadow_tex2", "Diffuse_Map"],
        )
        self.assertEqual(RECEIVER_MATERIAL.count("content_type shadow"), 3)
        self.assertEqual(
            CASTER_MATERIAL.count(
                "vertex_program_ref PSSM/shadow_caster_vs"
            ),
            1,
        )
        self.assertEqual(
            CASTER_MATERIAL.count(
                "fragment_program_ref PSSM/shadow_caster_alpha_ps"
            ),
            1,
        )
        self.assertRegex(
            SHARED,
            r"shared_params\s+pssm_params\s*\{\s*"
            r"shared_param_named\s+pssmSplitPoints\s+float4\s*\}",
        )


if __name__ == "__main__":
    unittest.main()
