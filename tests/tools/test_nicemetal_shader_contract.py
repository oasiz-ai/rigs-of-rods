#!/usr/bin/env python3
"""Fail closed on the modern NiceMetal program and packaging contract."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
MATERIALS = ROOT / "resources/materials"
MANAGED = ROOT / "resources/managed_materials"
NATIVE_WORKFLOW = (
    ROOT / ".github/workflows/ogre-next-combined-native.yml"
).read_text(encoding="utf-8")
TSAN_WORKFLOW = (
    ROOT / ".github/workflows/ogre-next-combined-tsan.yml"
).read_text(encoding="utf-8")

MODERN_SOURCES = (
    "nicemetal_gl3plus.glsl",
    "nicemetal_d3d11.hlsl",
)
MANAGED_MODERN_SOURCES = (
    "nicemetal_mm_gl3plus.glsl",
    "nicemetal_mm_d3d11.hlsl",
)

PUBLIC_PROGRAMS = {
    "NiceMetal_VS": "vertex",
    "NiceMetal_PS": "fragment",
    "NiceMetal_transp_PS": "fragment",
    "NiceMetal_PS_nodmg": "fragment",
    "NiceMetal_transp_PS_nodmg": "fragment",
    "NiceMetal_Reflect_VS": "vertex",
    "NiceMetal_Reflect_PS": "fragment",
    "NiceMetal_Reflect_nocolor_PS": "fragment",
    "SimpleMetal_PS": "fragment",
    "SimpleMetal_transp_PS": "fragment",
}

GLSL_SELECTORS = {
    "NiceMetal_VS": {"NICEMETAL_VERTEX_LIT=1"},
    "NiceMetal_PS": {"NICEMETAL_FRAGMENT_LIT=1"},
    "NiceMetal_transp_PS": {
        "NICEMETAL_FRAGMENT_LIT=1",
        "NICEMETAL_TRANSPARENT=1",
    },
    "NiceMetal_PS_nodmg": {
        "NICEMETAL_FRAGMENT_LIT=1",
        "NICEMETAL_NO_DAMAGE=1",
    },
    "NiceMetal_transp_PS_nodmg": {
        "NICEMETAL_FRAGMENT_LIT=1",
        "NICEMETAL_TRANSPARENT=1",
        "NICEMETAL_NO_DAMAGE=1",
    },
    "NiceMetal_Reflect_VS": {"NICEMETAL_VERTEX_REFLECTION=1"},
    "NiceMetal_Reflect_PS": {"NICEMETAL_FRAGMENT_REFLECTION=1"},
    "NiceMetal_Reflect_nocolor_PS": {
        "NICEMETAL_FRAGMENT_REFLECTION=1",
        "NICEMETAL_NO_VERTEX_COLOUR=1",
    },
    "SimpleMetal_PS": {
        "NICEMETAL_FRAGMENT_LIT=1",
        "NICEMETAL_SIMPLE=1",
    },
    "SimpleMetal_transp_PS": {
        "NICEMETAL_FRAGMENT_LIT=1",
        "NICEMETAL_SIMPLE=1",
        "NICEMETAL_TRANSPARENT=1",
    },
}

HLSL_ENTRY_POINTS = {
    "NiceMetal_VS": "NiceMetalLitVS",
    "NiceMetal_PS": "NiceMetalLitPS",
    "NiceMetal_transp_PS": "NiceMetalLitPS",
    "NiceMetal_PS_nodmg": "NiceMetalLitPS",
    "NiceMetal_transp_PS_nodmg": "NiceMetalLitPS",
    "NiceMetal_Reflect_VS": "NiceMetalReflectionVS",
    "NiceMetal_Reflect_PS": "NiceMetalReflectionPS",
    "NiceMetal_Reflect_nocolor_PS": "NiceMetalReflectionPS",
    "SimpleMetal_PS": "NiceMetalLitPS",
    "SimpleMetal_transp_PS": "NiceMetalLitPS",
}

LIT_VERTEX_PARAMS = {
    "param_named_auto worldviewproj worldviewproj_matrix",
    "param_named_auto lightPosition light_position_object_space 0",
    "param_named_auto eyePosition camera_position_object_space",
}
REFLECTION_VERTEX_PARAMS = {
    "param_named_auto camPosition camera_position_object_space",
    "param_named_auto world world_matrix",
    "param_named_auto worldViewProj worldviewproj_matrix",
}
LIT_FRAGMENT_PARAMS = {
    "param_named_auto lightDiffuse light_diffuse_colour 0",
    "param_named_auto lightSpecular light_specular_colour 0",
    "param_named exponent float 127",
    "param_named_auto ambient ambient_light_colour",
}
LIT_SAMPLERS = {
    "param_named diffusetex int 0",
    "param_named speculartex int 1",
}
DAMAGE_SAMPLER = {"param_named diffusedmgtex int 2"}
REFLECTION_SAMPLERS = {
    "param_named speculartex int 0",
    "param_named cubeMap int 1",
}

TEMPLATE_REFERENCES = {
    "managed/flexmesh_standard/specularonly_nicemetal": (
        "NiceMetal_VS_mm",
        "NiceMetal_PS_nodmg_mm",
        "NiceMetal_Reflect_VS_mm",
        "NiceMetal_Reflect_PS_mm",
    ),
    "managed/flexmesh_standard/speculardamage_nicemetal": (
        "NiceMetal_VS_mm",
        "NiceMetal_PS_mm",
        "NiceMetal_Reflect_VS_mm",
        "NiceMetal_Reflect_PS_mm",
    ),
    "managed/mesh_standard/specular_nicemetal": (
        "NiceMetal_VS_mm",
        "SimpleMetal_PS_mm",
        "NiceMetal_Reflect_VS_mm",
        "NiceMetal_Reflect_nocolor_PS_mm",
    ),
    "managed/flexmesh_transparent/specularonly_nicemetal": (
        "NiceMetal_VS_mm",
        "NiceMetal_transp_PS_nodmg_mm",
        "NiceMetal_Reflect_VS_mm",
        "NiceMetal_Reflect_PS_mm",
    ),
    "managed/flexmesh_transparent/speculardamage_nicemetal": (
        "NiceMetal_VS_mm",
        "NiceMetal_transp_PS_mm",
        "NiceMetal_Reflect_VS_mm",
        "NiceMetal_Reflect_PS_mm",
    ),
    "managed/mesh_transparent/specular_nicemetal": (
        "NiceMetal_VS_mm",
        "SimpleMetal_transp_PS_mm",
        "NiceMetal_Reflect_VS_mm",
        "NiceMetal_Reflect_nocolor_PS_mm",
    ),
}

TEMPLATE_TEXTURE_UNITS = {
    name: (
        ("Diffuse_Map", "Specular_Map", "Dmg_Diffuse_Map")
        if "speculardamage" in name
        else ("Diffuse_Map", "Specular_Map")
    )
    + ("Specular_Map", "envmaptex")
    for name in TEMPLATE_REFERENCES
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


def normalized_parameters(program: Program) -> set[str]:
    return {
        " ".join(match.group(1).split())
        for match in PARAMETER.finditer(program.body)
    }


def preprocessor_definitions(program: Program) -> set[str]:
    matches = re.findall(
        r"^\s*preprocessor_defines\s+([^\r\n]+?)\s*$",
        program.body,
        re.MULTILINE,
    )
    if len(matches) != 1:
        raise AssertionError(
            f"{program.name} must own exactly one selector declaration"
        )
    return {
        item.strip()
        for item in re.split(r"[;,]", matches[0])
        if item.strip()
    }


def material_blocks(source: str) -> dict[str, str]:
    declarations = list(
        re.finditer(r"^material\s+(\S+)\s*$", source, re.MULTILINE)
    )
    blocks: dict[str, str] = {}
    for declaration in declarations:
        name = declaration.group(1)
        if name in blocks:
            raise AssertionError(f"duplicate material template: {name}")
        blocks[name] = brace_block(source, declaration.start())
    return blocks


def hlsl_primary_stage_blocks(source: str) -> dict[str, str]:
    declaration = re.compile(
        r"^#(?:if|elif) defined\("
        r"(NICEMETAL_(?:VERTEX_LIT|VERTEX_REFLECTION|"
        r"FRAGMENT_LIT|FRAGMENT_REFLECTION))\)\s*$",
        re.MULTILINE,
    )
    matches = list(declaration.finditer(source))
    blocks: dict[str, str] = {}
    for index, match in enumerate(matches):
        end = matches[index + 1].start() if index + 1 < len(matches) else len(source)
        blocks[match.group(1)] = source[match.start() : end]
    return blocks


class NiceMetalShaderContractTests(unittest.TestCase):
    def test_sources_are_modern_regular_and_exactly_mirrored(self) -> None:
        ordinary_paths = tuple(MATERIALS / name for name in MODERN_SOURCES)
        managed_paths = tuple(
            MANAGED / name for name in MANAGED_MODERN_SOURCES
        )
        for path in ordinary_paths + managed_paths:
            with self.subTest(path=path.relative_to(ROOT)):
                self.assertTrue(path.is_file())
                self.assertFalse(path.is_symlink())
                self.assertGreater(len(path.read_bytes()), 0)

        self.assertEqual(
            ordinary_paths[0].read_bytes(), managed_paths[0].read_bytes()
        )
        self.assertEqual(
            ordinary_paths[1].read_bytes(), managed_paths[1].read_bytes()
        )
        self.assertTrue(
            ordinary_paths[0].read_text(encoding="utf-8").startswith(
                "#version 330 core\n"
            )
        )

        self.assertEqual(
            {path.name for path in MATERIALS.glob("nicemetal*")},
            {*MODERN_SOURCES, "nicemetal.program"},
        )
        self.assertEqual(
            {path.name for path in MANAGED.glob("nicemetal_mm*")},
            {*MANAGED_MODERN_SOURCES, "nicemetal_mm.program"},
        )
        self.assertFalse((MATERIALS / "nicemetal.cg").exists())
        self.assertFalse((MANAGED / "nicemetal_mm.cg").exists())

    def test_twenty_public_names_have_exact_modern_backend_closure(self) -> None:
        for managed in (False, True):
            suffix = "_mm" if managed else ""
            script_path = (
                MANAGED / "nicemetal_mm.program"
                if managed
                else MATERIALS / "nicemetal.program"
            )
            script = script_path.read_text(encoding="utf-8")
            programs = parse_programs(script)
            public = {
                f"{name}{suffix}": stage
                for name, stage in PUBLIC_PROGRAMS.items()
            }
            private = {
                f"{name}/{backend}": (stage, language)
                for name, stage in public.items()
                for backend, language in (
                    ("GL3Plus", "glsl"),
                    ("D3D11", "hlsl"),
                )
            }
            self.assertEqual(set(programs), set(public) | set(private))
            self.assertNotRegex(script, r"(?i)(?:\bcg\b|\.cg\b)")

            for name, stage in sorted(public.items()):
                with self.subTest(managed=managed, program=name):
                    program = programs[name]
                    self.assertEqual(
                        (program.stage, program.language),
                        (stage, "unified"),
                    )
                    self.assertEqual(
                        re.findall(
                            r"^\s*delegate\s+(\S+)",
                            program.body,
                            re.MULTILINE,
                        ),
                        [f"{name}/GL3Plus", f"{name}/D3D11"],
                    )
                    self.assertEqual(program.body.count("delegate "), 2)

            for name, (stage, language) in sorted(private.items()):
                with self.subTest(managed=managed, backend=name):
                    program = programs[name]
                    self.assertEqual(
                        (program.stage, program.language), (stage, language)
                    )
                    self.assertNotIn("delegate ", program.body)

    def test_selectors_entries_auto_params_and_sampler_slots_are_exact(self) -> None:
        for managed in (False, True):
            suffix = "_mm" if managed else ""
            script_path = (
                MANAGED / "nicemetal_mm.program"
                if managed
                else MATERIALS / "nicemetal.program"
            )
            programs = parse_programs(script_path.read_text(encoding="utf-8"))
            glsl_source = (
                "nicemetal_mm_gl3plus.glsl"
                if managed
                else "nicemetal_gl3plus.glsl"
            )
            hlsl_source = (
                "nicemetal_mm_d3d11.hlsl"
                if managed
                else "nicemetal_d3d11.hlsl"
            )

            for base_name, stage in sorted(PUBLIC_PROGRAMS.items()):
                name = f"{base_name}{suffix}"
                glsl = programs[f"{name}/GL3Plus"]
                hlsl = programs[f"{name}/D3D11"]
                with self.subTest(managed=managed, program=base_name):
                    self.assertEqual(glsl.body.count(f"source {glsl_source}"), 1)
                    self.assertEqual(hlsl.body.count(f"source {hlsl_source}"), 1)
                    self.assertEqual(glsl.body.count("syntax glsl330"), 1)
                    self.assertNotIn("syntax ", hlsl.body)
                    self.assertEqual(
                        preprocessor_definitions(glsl),
                        GLSL_SELECTORS[base_name],
                    )
                    self.assertEqual(
                        preprocessor_definitions(hlsl),
                        GLSL_SELECTORS[base_name],
                    )
                    self.assertEqual(
                        re.findall(
                            r"^\s*entry_point\s+(\S+)",
                            hlsl.body,
                            re.MULTILINE,
                        ),
                        [HLSL_ENTRY_POINTS[base_name]],
                    )
                    target = "vs_4_0" if stage == "vertex" else "ps_4_0"
                    self.assertEqual(hlsl.body.count(f"target {target}"), 1)

                    if base_name == "NiceMetal_VS":
                        expected_hlsl = LIT_VERTEX_PARAMS
                        expected_glsl = expected_hlsl
                    elif base_name == "NiceMetal_Reflect_VS":
                        expected_hlsl = REFLECTION_VERTEX_PARAMS
                        expected_glsl = expected_hlsl
                    elif stage == "fragment" and "Reflect" not in base_name:
                        expected_hlsl = LIT_FRAGMENT_PARAMS
                        expected_glsl = expected_hlsl | LIT_SAMPLERS
                        if base_name in {
                            "NiceMetal_PS",
                            "NiceMetal_transp_PS",
                        }:
                            expected_glsl |= DAMAGE_SAMPLER
                    else:
                        expected_hlsl = set()
                        expected_glsl = REFLECTION_SAMPLERS
                    self.assertEqual(
                        normalized_parameters(glsl), expected_glsl
                    )
                    self.assertEqual(
                        normalized_parameters(hlsl), expected_hlsl
                    )

            transparent_no_damage = programs[
                f"NiceMetal_transp_PS_nodmg{suffix}/GL3Plus"
            ]
            self.assertEqual(
                preprocessor_definitions(transparent_no_damage),
                {
                    "NICEMETAL_FRAGMENT_LIT=1",
                    "NICEMETAL_TRANSPARENT=1",
                    "NICEMETAL_NO_DAMAGE=1",
                },
            )
            self.assertEqual(
                preprocessor_definitions(
                    programs[
                        f"NiceMetal_transp_PS_nodmg{suffix}/D3D11"
                    ]
                ),
                {
                    "NICEMETAL_FRAGMENT_LIT=1",
                    "NICEMETAL_TRANSPARENT=1",
                    "NICEMETAL_NO_DAMAGE=1",
                },
            )

        glsl = (MATERIALS / "nicemetal_gl3plus.glsl").read_text(
            encoding="utf-8"
        )
        hlsl = (MATERIALS / "nicemetal_d3d11.hlsl").read_text(
            encoding="utf-8"
        )
        for selector in set().union(*GLSL_SELECTORS.values()):
            with self.subTest(selector=selector):
                self.assertIn(selector.removesuffix("=1"), glsl)
        for sampler, slot in (
            ("diffusetex", 0),
            ("speculartex", 1),
            ("diffusedmgtex", 2),
        ):
            with self.subTest(sampler=sampler):
                self.assertRegex(
                    hlsl,
                    rf"\b{sampler}\b[^;\r\n]*register\(t{slot}\)",
                )
        stage_blocks = hlsl_primary_stage_blocks(hlsl)
        self.assertEqual(
            set(stage_blocks),
            {
                "NICEMETAL_VERTEX_LIT",
                "NICEMETAL_VERTEX_REFLECTION",
                "NICEMETAL_FRAGMENT_LIT",
                "NICEMETAL_FRAGMENT_REFLECTION",
            },
        )
        lit = stage_blocks["NICEMETAL_FRAGMENT_LIT"]
        reflection = stage_blocks["NICEMETAL_FRAGMENT_REFLECTION"]
        for declaration in (
            "Texture2D diffusetex : register(t0);",
            "SamplerState diffusetexSampler : register(s0);",
            "Texture2D speculartex : register(t1);",
            "SamplerState speculartexSampler : register(s1);",
            "Texture2D diffusedmgtex : register(t2);",
            "SamplerState diffusedmgtexSampler : register(s2);",
        ):
            with self.subTest(hlsl_lit_resource=declaration):
                self.assertEqual(lit.count(declaration), 1)
                self.assertNotIn(declaration, reflection)
        for declaration in (
            "Texture2D speculartex : register(t0);",
            "SamplerState speculartexSampler : register(s0);",
            "TextureCube cubeMap : register(t1);",
            "SamplerState cubeMapSampler : register(s1);",
        ):
            with self.subTest(hlsl_reflection_resource=declaration):
                self.assertEqual(reflection.count(declaration), 1)
                self.assertNotIn(declaration, lit)

    def test_six_managed_templates_keep_exact_program_and_texture_order(self) -> None:
        script_paths = (
            MANAGED / "managed_mats_vehicles_nicemetal.material",
            MANAGED
            / "managed_mats_vehicles_transparent_nicemetal.material",
        )
        scripts = "\n".join(
            path.read_text(encoding="utf-8") for path in script_paths
        )
        blocks = material_blocks(scripts)
        self.assertEqual(set(blocks), set(TEMPLATE_REFERENCES))
        referenced_programs: set[str] = set()
        for name, expected_programs in sorted(TEMPLATE_REFERENCES.items()):
            block = blocks[name]
            with self.subTest(material=name):
                references = tuple(
                    re.findall(
                        r"^\s*(?:vertex|fragment)_program_ref\s+(\S+)",
                        block,
                        re.MULTILINE,
                    )
                )
                self.assertEqual(references, expected_programs)
                referenced_programs.update(references)
                self.assertEqual(
                    tuple(
                        re.findall(
                            r"^\s*texture_unit\s+(\S+)",
                            block,
                            re.MULTILINE,
                        )
                    ),
                    TEMPLATE_TEXTURE_UNITS[name],
                )
        self.assertTrue(
            referenced_programs.issubset(
                {
                    f"{name}_mm"
                    for name in PUBLIC_PROGRAMS
                }
            )
        )

    def test_native_and_tsan_workflows_run_trigger_and_diagnose(self) -> None:
        test_path = "tests/tools/test_nicemetal_shader_contract.py"
        trigger_paths = (
            "resources/materials/nicemetal*",
            "resources/managed_materials/nicemetal_mm*",
            "resources/managed_materials/managed_mats_vehicles_nicemetal.material",
            (
                "resources/managed_materials/"
                "managed_mats_vehicles_transparent_nicemetal.material"
            ),
            test_path,
        )
        for path in trigger_paths:
            with self.subTest(workflow="native", path=path):
                self.assertEqual(NATIVE_WORKFLOW.count(f"- {path}"), 2)
            with self.subTest(workflow="tsan", path=path):
                self.assertEqual(TSAN_WORKFLOW.count(f"- {path}"), 1)

        self.assertEqual(NATIVE_WORKFLOW.count(f"python {test_path}"), 2)
        self.assertEqual(NATIVE_WORKFLOW.count(f"python -O {test_path}"), 2)
        self.assertEqual(NATIVE_WORKFLOW.count(f"@('{test_path}')"), 1)
        self.assertEqual(
            NATIVE_WORKFLOW.count(f"@('-O', '{test_path}')"), 1
        )
        self.assertEqual(TSAN_WORKFLOW.count(f"python {test_path}"), 1)
        self.assertEqual(TSAN_WORKFLOW.count(f"python -O {test_path}"), 1)
        self.assertEqual(
            NATIVE_WORKFLOW.count("if grep -Eiq \"Program '[^']+"), 2
        )
        self.assertEqual(
            TSAN_WORKFLOW.count("if grep -Eiq \"Program '[^']+"), 1
        )
        self.assertEqual(
            NATIVE_WORKFLOW.count("$shaderDiagnostics = Select-String"), 1
        )

        bash_diagnostics = (
            "Program '(NiceMetal|SimpleMetal)[^']*' is not supported",
            "Program 'Examples/(Fresnel[^']*|ReflectFP[^']*)' is not supported",
            "Program '(grassVP|grassFP)[^']*' is not supported",
            "Program '(ambient_vs|ambient_ps|render_vs|render_ps|render_gr_ps|diffuse_vs|diffuse_ps|diffuse_ps_env|diffuse_sh_vs|diffuse_sh_ps|diffuse_sh_a_ps)[^']*' is not supported",
            "(NiceMetal|SimpleMetal|Fresnel|ReflectFP|grassVP|grassFP|grass_(gl3plus|d3d11)|ambient_vs|ambient_ps|render_vs|render_ps|render_gr_ps|diffuse_vs|diffuse_ps|diffuse_ps_env|diffuse_sh_vs|diffuse_sh_ps|diffuse_sh_a_ps|general_(gl3plus|d3d11))[^[:cntrl:]]*((compil|load)",
            "RTSS:[^[:cntrl:]]*'[^']*nicemetal[^']*'",
            r"Error: ScriptCompiler .*nicemetal[^[:cntrl:]]*\\.(program|material)",
            r"Error: ScriptCompiler .*fresnel\\.material",
            r"Error: ScriptCompiler .*grass\\.material",
            r"Error: ScriptCompiler .*general\\.(program|material)",
        )
        for diagnostic in bash_diagnostics:
            with self.subTest(diagnostic=diagnostic):
                self.assertEqual(NATIVE_WORKFLOW.count(diagnostic), 2)
                self.assertEqual(TSAN_WORKFLOW.count(diagnostic), 1)

        for diagnostic in (
            "(?:NiceMetal|SimpleMetal|Fresnel|ReflectFP|grassVP|grassFP|grass_(?:gl3plus|d3d11)|ambient_vs|ambient_ps|render_vs|render_ps|render_gr_ps|diffuse_vs|diffuse_ps|diffuse_ps_env|diffuse_sh_vs|diffuse_sh_ps|diffuse_sh_a_ps|general_(?:gl3plus|d3d11)).*(?:(?:compil|load)",
            "RTSS:.*'[^']*nicemetal[^']*'",
            r"Error: ScriptCompiler .*nicemetal.*\.(?:program|material)",
            r"Error: ScriptCompiler .*fresnel\.material",
            r"Error: ScriptCompiler .*grass\.material",
            r"Error: ScriptCompiler .*general\.(?:program|material)",
        ):
            with self.subTest(windows_diagnostic=diagnostic):
                self.assertEqual(NATIVE_WORKFLOW.count(diagnostic), 1)


if __name__ == "__main__":
    unittest.main()
