#!/usr/bin/env python3
"""Fail closed on the modern general material shader contract."""

from __future__ import annotations

from collections import Counter
from dataclasses import dataclass
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
MATERIALS = ROOT / "resources/materials"
PROGRAM_PATH = MATERIALS / "general.program"
MATERIAL_PATH = MATERIALS / "general.material"
GLSL_PATH = MATERIALS / "general_gl3plus.glsl"
HLSL_PATH = MATERIALS / "general_d3d11.hlsl"

PROGRAM_SCRIPT = PROGRAM_PATH.read_text(encoding="utf-8")
MATERIAL_SCRIPT = MATERIAL_PATH.read_text(encoding="utf-8")
GLSL = GLSL_PATH.read_text(encoding="utf-8")
HLSL = HLSL_PATH.read_text(encoding="utf-8")

PUBLIC_PROGRAMS = {
    "ambient_vs": "vertex",
    "ambient_ps": "fragment",
    "render_vs": "vertex",
    "render_ps": "fragment",
    "render_gr_ps": "fragment",
    "diffuse_vs": "vertex",
    "diffuse_ps": "fragment",
    "diffuse_ps_env": "fragment",
    "diffuse_sh_vs": "vertex",
    "diffuse_sh_ps": "fragment",
    "diffuse_sh_a_ps": "fragment",
}

SELECTORS = {
    "ambient_vs": "GENERAL_VERTEX_AMBIENT=1",
    "ambient_ps": "GENERAL_FRAGMENT_AMBIENT=1",
    "render_vs": "GENERAL_VERTEX_RENDER=1",
    "render_ps": "GENERAL_FRAGMENT_RENDER=1",
    "render_gr_ps": "GENERAL_FRAGMENT_RENDER_GRASS=1",
    "diffuse_vs": "GENERAL_VERTEX_DIFFUSE=1",
    "diffuse_ps": "GENERAL_FRAGMENT_DIFFUSE=1",
    "diffuse_ps_env": "GENERAL_FRAGMENT_ENV=1",
    "diffuse_sh_vs": "GENERAL_VERTEX_SHADOW=1",
    "diffuse_sh_ps": "GENERAL_FRAGMENT_SHADOW=1",
    "diffuse_sh_a_ps": "GENERAL_FRAGMENT_SHADOW_ALPHA=1",
}

HLSL_ENTRIES = {
    "ambient_vs": ("GeneralAmbientVS", "vs_4_0"),
    "ambient_ps": ("GeneralAmbientPS", "ps_4_0"),
    "render_vs": ("GeneralRenderVS", "vs_4_0"),
    "render_ps": ("GeneralRenderPS", "ps_4_0"),
    "render_gr_ps": ("GeneralRenderGrassPS", "ps_4_0"),
    "diffuse_vs": ("GeneralDiffuseVS", "vs_4_0"),
    "diffuse_ps": ("GeneralDiffusePS", "ps_4_0"),
    "diffuse_ps_env": ("GeneralEnvironmentPS", "ps_4_0"),
    "diffuse_sh_vs": ("GeneralShadowVS", "vs_4_0"),
    "diffuse_sh_ps": ("GeneralShadowPS", "ps_4_0"),
    "diffuse_sh_a_ps": ("GeneralShadowAlphaPS", "ps_4_0"),
}

VERTEX_PARAMETERS = {
    "ambient_vs": {
        "param_named_auto wvpMat worldviewproj_matrix",
    },
    "render_vs": {
        "param_named_auto wMat world_matrix",
        "param_named_auto wvpMat worldviewproj_matrix",
    },
    "diffuse_vs": {
        "param_named_auto wMat world_matrix",
        "param_named_auto wvpMat worldviewproj_matrix",
        "param_named_auto fogParams fog_params",
    },
    "diffuse_sh_vs": {
        "param_named_auto wMat world_matrix",
        "param_named_auto wvpMat worldviewproj_matrix",
        "param_named_auto fogParams fog_params",
        "param_named_auto texWVPMat0 texture_worldviewproj_matrix 0",
        "param_named_auto texWVPMat1 texture_worldviewproj_matrix 1",
        "param_named_auto texWVPMat2 texture_worldviewproj_matrix 2",
    },
}

LIT_PARAMETERS = {
    "param_named_auto ambient surface_ambient_colour",
    "param_named_auto lightDif0 light_diffuse_colour 0",
    "param_named_auto lightSpec0 light_specular_colour 0",
    "param_named_auto matDif surface_diffuse_colour",
    "param_named_auto matSpec surface_specular_colour",
    "param_named_auto matShininess surface_shininess",
    "param_named_auto fogColor fog_colour",
    "param_named_auto camPos camera_position",
    "param_named_auto lightPos0 light_position 0",
    "param_named_auto iTWMat inverse_transpose_world_matrix",
}

SHADOW_PARAMETERS = LIT_PARAMETERS | {
    "param_named_auto invShadowMapSize0 inverse_texture_size 0",
    "param_named_auto invShadowMapSize1 inverse_texture_size 1",
    "param_named_auto invShadowMapSize2 inverse_texture_size 2",
    "param_named pssmSplitPoints float4 1.0 187 774 10000.0",
}

BASE_PARAMETERS = {
    **VERTEX_PARAMETERS,
    "ambient_ps": {
        "param_named_auto ambient ambient_light_colour",
        "param_named_auto matDif surface_diffuse_colour",
    },
    "render_ps": {
        "param_named_auto ambient surface_ambient_colour",
        "param_named_auto matDif surface_diffuse_colour",
    },
    "render_gr_ps": set(),
    "diffuse_ps": LIT_PARAMETERS,
    "diffuse_ps_env": LIT_PARAMETERS,
    "diffuse_sh_ps": SHADOW_PARAMETERS,
    "diffuse_sh_a_ps": SHADOW_PARAMETERS,
}

GLSL_SAMPLERS = {
    "ambient_ps": {"param_named diffuseMap int 0"},
    "diffuse_ps": {
        "param_named diffuseMap int 0",
        "param_named normalMap int 1",
    },
    "diffuse_ps_env": {
        "param_named diffuseMap int 0",
        "param_named normalMap int 1",
        "param_named envMap int 2",
    },
    "diffuse_sh_ps": {
        "param_named diffuseMap int 0",
        "param_named normalMap int 1",
        "param_named shadowMap0 int 2",
        "param_named shadowMap1 int 3",
        "param_named shadowMap2 int 4",
    },
    "diffuse_sh_a_ps": {
        "param_named diffuseMap int 0",
        "param_named diffuseAlpha int 1",
        "param_named normalMap int 2",
        "param_named shadowMap0 int 3",
        "param_named shadowMap1 int 4",
        "param_named shadowMap2 int 5",
    },
}

MATERIAL_PROGRAMS = {
    "ppx_shadow": ("diffuse_sh_vs", "diffuse_sh_ps"),
    "ppx_shadow_a": ("diffuse_sh_vs", "diffuse_sh_a_ps"),
    "ppx_shadow_sel": ("diffuse_sh_vs", "diffuse_sh_ps"),
    "ppx_diffuse": ("diffuse_vs", "diffuse_ps"),
    "ppx_render": ("render_vs", "render_ps"),
    "ppx_render_gr": ("render_vs", "render_gr_ps"),
    "ppx_env": ("diffuse_vs", "diffuse_ps_env"),
    "ppx_glass_env": (
        "ambient_vs",
        "ambient_ps",
        "diffuse_vs",
        "diffuse_ps_env",
    ),
    "ppx_glass": (
        "ambient_vs",
        "ambient_ps",
        "diffuse_vs",
        "diffuse_ps",
    ),
}

MATERIAL_TEXTURE_UNITS = {
    "ppx_shadow": (
        "diffuseMap", "normalMap", "shadow_tex0", "shadow_tex1", "shadow_tex2"
    ),
    "ppx_shadow_a": (
        "diffuseMap", "diffuseAlpha", "normalMap",
        "shadow_tex0", "shadow_tex1", "shadow_tex2",
    ),
    "ppx_shadow_sel": (
        "diffuseMap", "normalMap", "shadow_tex0", "shadow_tex1", "shadow_tex2"
    ),
    "ppx_diffuse": ("diffuseMap", "normalMap"),
    "ppx_render": (),
    "ppx_render_gr": (),
    "ppx_env": ("diffuseMap", "normalMap", "envMap"),
    "ppx_glass_env": (
        "diffuseMap", "diffuseMap", "normalMap", "envMap"
    ),
    "ppx_glass": ("diffuseMap", "diffuseMap", "normalMap"),
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


def parse_materials(source: str) -> dict[str, str]:
    materials: dict[str, str] = {}
    declarations = re.finditer(
        r"^material\s+(\S+)(?:\s+//[^\r\n]*)?\s*$",
        source,
        re.MULTILINE,
    )
    for declaration in declarations:
        name = declaration.group(1)
        if name in materials:
            raise AssertionError(f"duplicate material: {name}")
        materials[name] = brace_block(source, declaration.start())
    return materials


def normalized_parameters(program: Program) -> set[str]:
    return {
        " ".join(match.group(1).split())
        for match in PARAMETER.finditer(program.body)
    }


def normalized_lines(block: str, prefix: str) -> list[str]:
    return [
        " ".join(line.split())
        for line in block.splitlines()
        if line.strip().startswith(prefix)
    ]


class GeneralShaderContractTests(unittest.TestCase):
    def test_only_modern_regular_sources_remain(self) -> None:
        self.assertEqual(
            {path.name for path in MATERIALS.glob("general*")},
            {
                "general.material",
                "general.program",
                "general_gl3plus.glsl",
                "general_d3d11.hlsl",
            },
        )
        for path in (PROGRAM_PATH, MATERIAL_PATH, GLSL_PATH, HLSL_PATH):
            with self.subTest(path=path.relative_to(ROOT)):
                self.assertTrue(path.is_file())
                self.assertFalse(path.is_symlink())
                self.assertGreater(len(path.read_bytes()), 0)
        self.assertFalse((MATERIALS / "general.cg").exists())
        self.assertTrue(GLSL.startswith("#version 330 core\n"))

        modern_text = "\n".join((PROGRAM_SCRIPT, GLSL, HLSL))
        self.assertNotRegex(
            modern_text,
            re.compile(
                r"(?i)(?:\bcg\b|\.cg\b|\bprofiles?\b|\bps_2_x\b|"
                r"\bps_2_0\b|\barbfp1\b|\bvs_1_1\b|\barbvp1\b)"
            ),
        )

    def test_public_aliases_have_exact_modern_backend_closure(self) -> None:
        programs = parse_programs(PROGRAM_SCRIPT)
        private = {
            f"{name}/{backend}": (stage, language)
            for name, stage in PUBLIC_PROGRAMS.items()
            for backend, language in (("GL3Plus", "glsl"), ("D3D11", "hlsl"))
        }
        self.assertEqual(set(programs), set(PUBLIC_PROGRAMS) | set(private))

        for name, stage in sorted(PUBLIC_PROGRAMS.items()):
            with self.subTest(public=name):
                program = programs[name]
                self.assertEqual((program.stage, program.language), (stage, "unified"))
                self.assertEqual(
                    re.findall(
                        r"^\s*delegate\s+(\S+)", program.body, re.MULTILINE
                    ),
                    [f"{name}/GL3Plus", f"{name}/D3D11"],
                )
                self.assertNotIn("source ", program.body)
                self.assertEqual(normalized_parameters(program), set())

        for name, expected in sorted(private.items()):
            with self.subTest(private=name):
                program = programs[name]
                self.assertEqual((program.stage, program.language), expected)
                self.assertNotIn("delegate ", program.body)

    def test_selectors_entries_auto_params_and_sampler_slots_are_exact(self) -> None:
        programs = parse_programs(PROGRAM_SCRIPT)
        for public_name in sorted(PUBLIC_PROGRAMS):
            glsl = programs[f"{public_name}/GL3Plus"]
            hlsl = programs[f"{public_name}/D3D11"]
            with self.subTest(program=public_name):
                self.assertEqual(
                    normalized_lines(glsl.body, "syntax"), ["syntax glsl330"]
                )
                self.assertEqual(
                    normalized_lines(glsl.body, "source"),
                    ["source general_gl3plus.glsl"],
                )
                self.assertEqual(
                    normalized_lines(hlsl.body, "source"),
                    ["source general_d3d11.hlsl"],
                )
                self.assertEqual(
                    normalized_lines(glsl.body, "preprocessor_defines"),
                    [f"preprocessor_defines {SELECTORS[public_name]}"],
                )
                self.assertEqual(
                    normalized_lines(hlsl.body, "preprocessor_defines"),
                    [f"preprocessor_defines {SELECTORS[public_name]}"],
                )
                entry, target = HLSL_ENTRIES[public_name]
                self.assertEqual(
                    normalized_lines(hlsl.body, "entry_point"),
                    [f"entry_point {entry}"],
                )
                self.assertEqual(
                    normalized_lines(hlsl.body, "target"), [f"target {target}"]
                )
                self.assertEqual(
                    normalized_parameters(glsl),
                    BASE_PARAMETERS[public_name]
                    | GLSL_SAMPLERS.get(public_name, set()),
                )
                self.assertEqual(
                    normalized_parameters(hlsl), BASE_PARAMETERS[public_name]
                )

    def test_material_consumers_keep_public_names_and_texture_order(self) -> None:
        materials = parse_materials(MATERIAL_SCRIPT)
        self.assertEqual(set(materials), set(MATERIAL_PROGRAMS))
        for name, expected_programs in MATERIAL_PROGRAMS.items():
            block = materials[name]
            with self.subTest(material=name):
                self.assertEqual(
                    tuple(
                        re.findall(
                            r"^\s*(?:vertex|fragment)_program_ref\s+(\S+)",
                            block,
                            re.MULTILINE,
                        )
                    ),
                    expected_programs,
                )
                self.assertEqual(
                    tuple(
                        re.findall(
                            r"^\s*texture_unit\s+(\S+)", block, re.MULTILINE
                        )
                    ),
                    MATERIAL_TEXTURE_UNITS[name],
                )
        referenced = set(
            re.findall(
                r"^\s*(?:vertex|fragment)_program_ref\s+(\S+)",
                MATERIAL_SCRIPT,
                re.MULTILINE,
            )
        )
        self.assertEqual(referenced, set(PUBLIC_PROGRAMS))

    def test_glsl_is_core_and_preserves_lighting_fog_env_alpha_and_pssm(self) -> None:
        self.assertNotRegex(
            GLSL,
            re.compile(
                r"\b(?:attribute|varying|texture2D|textureCube|gl_FragColor|"
                r"gl_TexCoord|ftransform)\b"
            ),
        )
        for selector in SELECTORS.values():
            with self.subTest(selector=selector):
                name = selector.removesuffix("=1")
                self.assertRegex(GLSL, rf"defined\({re.escape(name)}\)")

        required_fragments = (
            "float normalY = clamp(abs(generalObjectNormal.y), 0.0, 1.0);",
            "pow(max(1.0 - 2.0 * acos(normalY) / 3.141592654, 0.0), power)",
            "generalObjectBitangent = cross(tangent, normal);",
            "fogParams.x * (gl_Position.z - fogParams.y) * fogParams.w",
            "mat3(iTWMat) * objectNormal",
            "reflect(-cameraDirection, mappedNormal)",
            "diffuseTex.a = texture(diffuseAlpha, generalUv.xy).g;",
            "generalUv.z <= pssmSplitPoints.y",
            "generalUv.z <= pssmSplitPoints.z",
            "diffuseColour * (0.25 + 0.75 * shadowing)",
            "specularColour * shadowing",
            "fragColour = vec4(vec3(bridge), bridge);",
        )
        for fragment in required_fragments:
            with self.subTest(glsl_semantic=fragment):
                self.assertIn(fragment, GLSL)
        self.assertEqual(GLSL.count("shadowMap, uv - sampleOffset.xy"), 1)
        self.assertEqual(GLSL.count("shadowMap, uv + sampleOffset.xy"), 1)
        self.assertEqual(GLSL.count("shadowMap, uv + sampleOffset.zy"), 1)
        self.assertEqual(GLSL.count("shadowMap, uv - sampleOffset.zy"), 1)

    def test_hlsl_has_exact_sm4_resource_and_sampler_registers(self) -> None:
        texture_bindings = Counter(
            re.findall(
                r"^Texture(2D|Cube)\s+(\w+)\s*:\s*register\(t(\d+)\);",
                HLSL,
                re.MULTILINE,
            )
        )
        self.assertEqual(
            texture_bindings,
            Counter(
                {
                    ("2D", "diffuseMap", "0"): 3,
                    ("2D", "normalMap", "1"): 2,
                    ("Cube", "envMap", "2"): 1,
                    ("2D", "diffuseAlpha", "1"): 1,
                    ("2D", "normalMap", "2"): 1,
                    ("2D", "shadowMap0", "2"): 1,
                    ("2D", "shadowMap1", "3"): 1,
                    ("2D", "shadowMap2", "4"): 1,
                    ("2D", "shadowMap0", "3"): 1,
                    ("2D", "shadowMap1", "4"): 1,
                    ("2D", "shadowMap2", "5"): 1,
                }
            ),
        )
        sampler_bindings = Counter(
            re.findall(
                r"^SamplerState\s+(\w+)Sampler\s*:\s*register\(s(\d+)\);",
                HLSL,
                re.MULTILINE,
            )
        )
        self.assertEqual(
            sampler_bindings,
            Counter(
                (name, slot)
                for _, name, slot in texture_bindings.elements()
            ),
        )

        required_fragments = (
            "float normalY = saturate(abs(input.objectNormal.y));",
            "pow(max(1.0 - 2.0 * acos(normalY) / 3.141592654, 0.0), power)",
            "output.objectBitangent = cross(input.tangent, input.normal);",
            "fogParams.x * (output.clipPosition.z - fogParams.y) * fogParams.w",
            "mul((float3x3)iTWMat, objectNormal)",
            "reflect(-cameraDirection, mappedNormal)",
            "diffuseTex.a = diffuseAlpha.Sample(diffuseAlphaSampler, input.uv.xy).g;",
            "input.uv.z <= pssmSplitPoints.y",
            "input.uv.z <= pssmSplitPoints.z",
            "diffuseColour * (0.25 + 0.75 * shadowing)",
            "specularColour * shadowing",
            "return float4(bridge, bridge, bridge, bridge);",
        )
        for fragment in required_fragments:
            with self.subTest(hlsl_semantic=fragment):
                self.assertIn(fragment, HLSL)
        self.assertEqual(HLSL.count("shadowSampler, uv - sampleOffset.xy"), 1)
        self.assertEqual(HLSL.count("shadowSampler, uv + sampleOffset.xy"), 1)
        self.assertEqual(HLSL.count("shadowSampler, uv + sampleOffset.zy"), 1)
        self.assertEqual(HLSL.count("shadowSampler, uv - sampleOffset.zy"), 1)


if __name__ == "__main__":
    unittest.main()
