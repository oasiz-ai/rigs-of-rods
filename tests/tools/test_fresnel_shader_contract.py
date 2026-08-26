#!/usr/bin/env python3
"""Fail closed on the modern Fresnel water shader and runtime contract."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
MATERIALS = ROOT / "resources/materials"
SCRIPT_PATH = MATERIALS / "fresnel.material"
GLSL_PATH = MATERIALS / "Example_Fresnel_gl3plus.glsl"
HLSL_PATH = MATERIALS / "Example_Fresnel_d3d11.hlsl"
RUNTIME_PATH = ROOT / "source/main/gfx/GfxWater.cpp"
CVAR_PATH = ROOT / "source/main/system/CVar.cpp"
CONTENT_PATH = ROOT / "source/main/resources/ContentManager.cpp"

SCRIPT = SCRIPT_PATH.read_text(encoding="utf-8")
GLSL = GLSL_PATH.read_text(encoding="utf-8")
HLSL = HLSL_PATH.read_text(encoding="utf-8")
RUNTIME = RUNTIME_PATH.read_text(encoding="utf-8")

PUBLIC_PROGRAMS = {
    "Examples/FresnelRefractReflectVP": "vertex",
    "Examples/FresnelRefractReflectFP": "fragment",
    "Examples/FresnelRefractReflectPS": "fragment",
    "Examples/ReflectFP": "fragment",
}
PRIVATE_PROGRAMS = {
    "Examples/FresnelRefractReflectVP/GL3Plus": ("vertex", "glsl"),
    "Examples/FresnelRefractReflectVP/D3D11": ("vertex", "hlsl"),
    "Examples/FresnelRefractReflectFP/GL3Plus": ("fragment", "glsl"),
    "Examples/FresnelRefractReflectFP/D3D11": ("fragment", "hlsl"),
    "Examples/ReflectFP/GL3Plus": ("fragment", "glsl"),
    "Examples/ReflectFP/D3D11": ("fragment", "hlsl"),
}
SELECTORS = {
    "Examples/FresnelRefractReflectVP": "FRESNEL_WATER_VERTEX=1",
    "Examples/FresnelRefractReflectFP": (
        "FRESNEL_WATER_FRAGMENT_FULL=1"
    ),
    "Examples/ReflectFP": "FRESNEL_WATER_FRAGMENT_REFLECTION=1",
}
HLSL_ENTRIES = {
    "Examples/FresnelRefractReflectVP": ("FresnelWaterVS", "vs_4_0"),
    "Examples/FresnelRefractReflectFP": (
        "FresnelWaterFullPS",
        "ps_4_0",
    ),
    "Examples/ReflectFP": ("FresnelWaterReflectionPS", "ps_4_0"),
}
FULL_MATERIALS = (
    "Examples/FresnelReflectionRefraction",
    "Examples/FresnelReflectionRefractioninverted",
)
REFLECTION_MATERIALS = (
    "Examples/FresnelReflection",
    "Examples/FresnelReflectioninverted",
)

DECLARATION = re.compile(
    r"^(?P<stage>vertex|fragment)_program\s+"
    r"(?P<name>\S+)\s+(?P<language>\S+)\s*$",
    re.MULTILINE,
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
    declarations = re.finditer(r"^material\s+(\S+)\s*$", source, re.MULTILINE)
    for declaration in declarations:
        name = declaration.group(1)
        if name in materials:
            raise AssertionError(f"duplicate material: {name}")
        materials[name] = brace_block(source, declaration.start())
    return materials


def stage_blocks(source: str) -> dict[str, str]:
    selector = re.compile(
        r"^#(?:if|elif) defined\((FRESNEL_WATER_[A-Z_]+)\)\s*$",
        re.MULTILINE,
    )
    matches = list(selector.finditer(source))
    blocks: dict[str, str] = {}
    for index, match in enumerate(matches):
        if index + 1 < len(matches):
            end = matches[index + 1].start()
        else:
            end = source.index("\n#else", match.end())
        blocks[match.group(1)] = source[match.start() : end]
    return blocks


def normalized_lines(block: str, prefix: str) -> list[str]:
    return [
        " ".join(line.split())
        for line in block.splitlines()
        if line.strip().startswith(prefix)
    ]


class FresnelShaderContractTests(unittest.TestCase):
    def test_only_modern_regular_sources_remain(self) -> None:
        self.assertEqual(
            {path.name for path in MATERIALS.glob("Example_Fresnel*")},
            {
                "Example_Fresnel_gl3plus.glsl",
                "Example_Fresnel_d3d11.hlsl",
            },
        )
        for path in (GLSL_PATH, HLSL_PATH):
            with self.subTest(path=path.relative_to(ROOT)):
                self.assertTrue(path.is_file())
                self.assertFalse(path.is_symlink())
                self.assertGreater(len(path.read_bytes()), 0)
        self.assertFalse((MATERIALS / "Example_Fresnel.cg").exists())
        self.assertFalse((MATERIALS / "Example_FresnelPS.asm").exists())

        modern_text = "\n".join((SCRIPT, GLSL, HLSL))
        self.assertNotRegex(
            modern_text,
            re.compile(
                r"(?i)(?:\bcg\b|\.cg\b|\basm\b|\.asm\b|\bprofiles?\b|"
                r"\bps_1_4\b|\bps_2_0\b|\barbfp1\b|\bvs_1_1\b|"
                r"\barbvp1\b)"
            ),
        )

    def test_public_aliases_have_exact_modern_backend_closure(self) -> None:
        programs = parse_programs(SCRIPT)
        self.assertEqual(
            set(programs), set(PUBLIC_PROGRAMS) | set(PRIVATE_PROGRAMS)
        )
        for name, stage in sorted(PUBLIC_PROGRAMS.items()):
            with self.subTest(public=name):
                program = programs[name]
                self.assertEqual((program.stage, program.language), (stage, "unified"))
                delegates = re.findall(
                    r"^\s*delegate\s+(\S+)", program.body, re.MULTILINE
                )
                if name == "Examples/FresnelRefractReflectPS":
                    expected = [
                        "Examples/FresnelRefractReflectFP/GL3Plus",
                        "Examples/FresnelRefractReflectFP/D3D11",
                    ]
                else:
                    expected = [f"{name}/GL3Plus", f"{name}/D3D11"]
                self.assertEqual(delegates, expected)
                self.assertNotIn("source ", program.body)

        for name, expected in sorted(PRIVATE_PROGRAMS.items()):
            with self.subTest(private=name):
                program = programs[name]
                self.assertEqual((program.stage, program.language), expected)
                self.assertNotIn("delegate ", program.body)

    def test_program_sources_selectors_entries_and_sampler_slots_are_exact(self) -> None:
        programs = parse_programs(SCRIPT)
        for public_name, selector in SELECTORS.items():
            glsl = programs[f"{public_name}/GL3Plus"]
            hlsl = programs[f"{public_name}/D3D11"]
            with self.subTest(program=public_name):
                self.assertEqual(glsl.body.count("syntax glsl330"), 1)
                self.assertEqual(
                    glsl.body.count("source Example_Fresnel_gl3plus.glsl"), 1
                )
                self.assertEqual(
                    hlsl.body.count("source Example_Fresnel_d3d11.hlsl"), 1
                )
                self.assertEqual(
                    normalized_lines(glsl.body, "preprocessor_defines"),
                    [f"preprocessor_defines {selector}"],
                )
                self.assertEqual(
                    normalized_lines(hlsl.body, "preprocessor_defines"),
                    [f"preprocessor_defines {selector}"],
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
            normalized_lines(
                programs["Examples/FresnelRefractReflectFP/GL3Plus"].body,
                "param_named",
            ),
            [
                "param_named noiseMap int 0",
                "param_named reflectMap int 1",
                "param_named refractMap int 2",
            ],
        )
        self.assertEqual(
            normalized_lines(
                programs["Examples/ReflectFP/GL3Plus"].body,
                "param_named",
            ),
            [
                "param_named noiseMap int 0",
                "param_named reflectMap int 1",
            ],
        )
        for name, program in programs.items():
            if name.endswith("/D3D11"):
                with self.subTest(d3d11_sampler_binding=name):
                    self.assertEqual(normalized_lines(program.body, "param_named"), [])

    def test_glsl_is_core_and_hlsl_resources_are_branch_local(self) -> None:
        self.assertTrue(GLSL.startswith("#version 330 core\n"))
        self.assertNotRegex(
            GLSL,
            re.compile(
                r"\b(?:attribute|varying|texture2D|texture3D|gl_FragColor|"
                r"gl_TexCoord|ftransform)\b"
            ),
        )
        expected_selectors = {
            "FRESNEL_WATER_VERTEX",
            "FRESNEL_WATER_FRAGMENT_FULL",
            "FRESNEL_WATER_FRAGMENT_REFLECTION",
        }
        self.assertEqual(set(stage_blocks(GLSL)), expected_selectors)
        hlsl_blocks = stage_blocks(HLSL)
        self.assertEqual(set(hlsl_blocks), expected_selectors)

        expected_resources = {
            "FRESNEL_WATER_VERTEX": (set(), set()),
            "FRESNEL_WATER_FRAGMENT_FULL": (
                {("Texture3D", "noiseMap", "0"),
                 ("Texture2D", "reflectMap", "1"),
                 ("Texture2D", "refractMap", "2")},
                {("noiseMapSampler", "0"),
                 ("reflectMapSampler", "1"),
                 ("refractMapSampler", "2")},
            ),
            "FRESNEL_WATER_FRAGMENT_REFLECTION": (
                {("Texture3D", "noiseMap", "0"),
                 ("Texture2D", "reflectMap", "1")},
                {("noiseMapSampler", "0"),
                 ("reflectMapSampler", "1")},
            ),
        }
        for selector, (textures, samplers) in expected_resources.items():
            block = hlsl_blocks[selector]
            with self.subTest(hlsl_branch=selector):
                self.assertEqual(
                    set(
                        re.findall(
                            r"^(Texture(?:2D|3D))\s+(\w+)\s*:\s*"
                            r"register\(t(\d+)\);",
                            block,
                            re.MULTILINE,
                        )
                    ),
                    textures,
                )
                self.assertEqual(
                    set(
                        re.findall(
                            r"^SamplerState\s+(\w+)\s*:\s*"
                            r"register\(s(\d+)\);",
                            block,
                            re.MULTILINE,
                        )
                    ),
                    samplers,
                )

    def test_audited_fresnel_distortion_projection_and_alpha_math_remains(self) -> None:
        for source, fresnel_result, position_name, normal_name in (
            (GLSL, "fresnelFactor", "gl_Position", "normal"),
            (HLSL, "output.fresnelFactor", "output.clipPosition", "input.normal"),
        ):
            with self.subTest(source="GLSL" if source is GLSL else "HLSL"):
                self.assertIn(
                    f"-0.5 * {position_name}.y + 0.5 * {position_name}.w",
                    source,
                )
                self.assertIn("timeVal * scroll", source)
                self.assertIn("noise * timeVal", source)
                self.assertIn(
                    f"{fresnel_result} = fresnelBias + fresnelScale * pow(",
                    source,
                )
                self.assertIn(f"dot(eyeDirection, {normal_name})", source)
                self.assertIn(
                    f"max(1.0 + dot(eyeDirection, {normal_name}), 0.0)",
                    source,
                )
                self.assertNotIn("clamp(", source)
                self.assertNotIn("saturate(", source)

        for source in (GLSL, HLSL):
            with self.subTest(distortion="GLSL" if source is GLSL else "HLSL"):
                self.assertEqual(source.count("0.31, 0.58, 0.23"), 2)
                self.assertEqual(source.count("distortion.x"), 2)
                self.assertEqual(source.count("distortion.y"), 2)
                self.assertEqual(source.count("distortionRange"), 4)
                self.assertEqual(source.count("0.33 +"), 1)
        self.assertIn(
            "fragColour = mix(refractionColour, reflectionColour, fresnelFactor);",
            GLSL,
        )
        self.assertIn("fragColour.a = reflectionFactor;", GLSL)
        self.assertIn(
            "return lerp(refractionColour, reflectionColour, input.fresnelFactor);",
            HLSL,
        )
        self.assertIn("outputColour.a = reflectionFactor;", HLSL)

    def test_material_consumers_keep_exact_params_and_texture_order(self) -> None:
        materials = parse_materials(SCRIPT)
        self.assertEqual(
            set(materials), set(FULL_MATERIALS) | set(REFLECTION_MATERIALS)
        )
        full_textures = [
            "perlinvolume.dds 3d",
            "Reflection 2d 0 PF_R8G8B8",
            "Refraction 2d 0 PF_R8G8B8",
        ]
        reflection_textures = full_textures[:2]
        vertex_params = [
            "param_named_auto worldViewProjMatrix worldviewproj_matrix",
            "param_named_auto eyePosition camera_position_object_space",
            "param_named fresnelBias float -0.3",
            "param_named fresnelScale float 1.4",
            "param_named fresnelPower float 8.0",
            "param_named_auto timeVal time 0.05",
            "param_named scroll float 1.0",
            "param_named scale float 4.0",
            "param_named noise float 1.0",
        ]
        fragment_params = [
            "param_named distortionRange float 0.025",
            "param_named tintColour float4 0.05 0.12 0.15 1.0",
        ]

        for name, block in materials.items():
            full = name in FULL_MATERIALS
            with self.subTest(material=name):
                references = re.findall(
                    r"^\s*(?:vertex|fragment)_program_ref\s+(\S+)",
                    block,
                    re.MULTILINE,
                )
                self.assertEqual(
                    references,
                    [
                        "Examples/FresnelRefractReflectVP",
                        (
                            "Examples/FresnelRefractReflectFP"
                            if full
                            else "Examples/ReflectFP"
                        ),
                    ],
                )
                self.assertEqual(
                    normalized_lines(block, "texture "),
                    [f"texture {item}" for item in (
                        full_textures if full else reflection_textures
                    )],
                )
                params = normalized_lines(block, "param_named")
                self.assertEqual(params, vertex_params + fragment_params)

        self.assertEqual(SCRIPT.count("texture Reflection 2d 0 PF_R8G8B8"), 4)
        self.assertEqual(SCRIPT.count("texture Refraction 2d 0 PF_R8G8B8"), 2)
        self.assertEqual(
            len(re.findall(r"fragment_program_ref\s+Examples/FresnelRefractReflectPS\b", SCRIPT)),
            0,
        )

    def test_runtime_gate_loads_the_selected_public_programs(self) -> None:
        self.assertNotIn("isSyntaxSupported", RUNTIME)
        for obsolete in ("arbfp1", "ps_2_0", "ps_1_4"):
            with self.subTest(obsolete_profile=obsolete):
                self.assertNotIn(obsolete, RUNTIME)
        for name in (
            "Examples/FresnelRefractReflectVP",
            "Examples/FresnelRefractReflectFP",
            "Examples/ReflectFP",
        ):
            with self.subTest(runtime_program=name):
                self.assertIn(f'"{name}"', RUNTIME)
        for operation in (
            "GpuProgramManager::getSingleton().getByName(program_name)",
            "program->load()",
            "program->isSupported()",
            "program->hasCompileError()",
            "catch (const Ogre::Exception&)",
        ):
            with self.subTest(runtime_check=operation):
                self.assertIn(operation, RUNTIME)
        self.assertRegex(
            RUNTIME,
            re.compile(
                r"const char\* fragment_program_name = full_gfx\s*"
                r"\? \"Examples/FresnelRefractReflectFP\"\s*"
                r": \"Examples/ReflectFP\";"
            ),
        )

    def test_fresnel_pack_is_shipped_and_full_fast_is_the_default(self) -> None:
        cvars = CVAR_PATH.read_text(encoding="utf-8")
        content = CONTENT_PATH.read_text(encoding="utf-8")
        self.assertRegex(
            cvars,
            re.compile(
                r"gfx_water_mode\s*=\s*this->cVarCreate\("
                r'"gfx_water_mode"[^\n]+"3"/\*\(int\)'
                r"GfxWaterMode::FULL_FAST\*/\);"
            ),
        )
        self.assertIn(
            'DECLARE_RESOURCE_PACK( MATERIALS,             "materials",            "MaterialsRG");',
            content,
        )
        self.assertIn(
            "this->AddResourcePack(ContentManager::ResourcePack::MATERIALS);",
            content,
        )


if __name__ == "__main__":
    unittest.main()
