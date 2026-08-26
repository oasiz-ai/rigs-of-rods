#!/usr/bin/env python3
"""Fail closed on the modern animated grass shader contract."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
MATERIALS = ROOT / "resources/materials"
SCRIPT_PATH = MATERIALS / "grass.material"
GLSL_PATH = MATERIALS / "grass_gl3plus.glsl"
HLSL_PATH = MATERIALS / "grass_d3d11.hlsl"

SCRIPT = SCRIPT_PATH.read_text(encoding="utf-8")
GLSL = GLSL_PATH.read_text(encoding="utf-8")
HLSL = HLSL_PATH.read_text(encoding="utf-8")

DECLARATION = re.compile(
    r"^(?P<stage>vertex|fragment)_program\s+"
    r"(?P<name>\S+)\s+(?P<language>\S+)\s*$",
    re.MULTILINE,
)


@dataclass(frozen=True)
class Program:
    stage: str
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
            language=declaration.group("language"),
            body=brace_block(source, declaration.start()),
        )
    return programs


def normalized_lines(block: str, prefix: str) -> list[str]:
    return [
        " ".join(line.split())
        for line in block.splitlines()
        if line.strip().startswith(prefix)
    ]


class GrassShaderContractTests(unittest.TestCase):
    def test_only_modern_regular_sources_remain(self) -> None:
        self.assertEqual(
            {path.name for path in MATERIALS.glob("grass*")},
            {"grass.material", "grass_gl3plus.glsl", "grass_d3d11.hlsl"},
        )
        for path in (SCRIPT_PATH, GLSL_PATH, HLSL_PATH):
            with self.subTest(path=path.relative_to(ROOT)):
                self.assertTrue(path.is_file())
                self.assertFalse(path.is_symlink())
                self.assertGreater(len(path.read_bytes()), 0)
        self.assertFalse((MATERIALS / "grass.cg").exists())
        self.assertNotRegex(
            "\n".join((SCRIPT, GLSL, HLSL)),
            re.compile(
                r"(?i)(?:\bcg\b|\.cg\b|\bprofiles?\b|\barbvp1\b|"
                r"\barbfp1\b|\bvs_1_1\b|\bps_2_x\b)"
            ),
        )

    def test_public_aliases_have_exact_gl3plus_and_d3d11_closure(self) -> None:
        programs = parse_programs(SCRIPT)
        expected = {
            "grassVP": ("vertex", "unified"),
            "grassVP/GL3Plus": ("vertex", "glsl"),
            "grassVP/D3D11": ("vertex", "hlsl"),
            "grassFP": ("fragment", "unified"),
            "grassFP/GL3Plus": ("fragment", "glsl"),
            "grassFP/D3D11": ("fragment", "hlsl"),
        }
        self.assertEqual(set(programs), set(expected))
        for name, identity in expected.items():
            with self.subTest(program=name):
                self.assertEqual(
                    (programs[name].stage, programs[name].language), identity
                )
        for public in ("grassVP", "grassFP"):
            self.assertEqual(
                normalized_lines(programs[public].body, "delegate"),
                [
                    f"delegate {public}/GL3Plus",
                    f"delegate {public}/D3D11",
                ],
            )

    def test_sources_entries_selectors_and_sampler_slots_are_exact(self) -> None:
        programs = parse_programs(SCRIPT)
        for suffix, language, source in (
            ("GL3Plus", "glsl", "grass_gl3plus.glsl"),
            ("D3D11", "hlsl", "grass_d3d11.hlsl"),
        ):
            for public, selector, entry, target in (
                ("grassVP", "GRASS_VERTEX=1", "GrassVS", "vs_4_0"),
                ("grassFP", "GRASS_FRAGMENT=1", "GrassPS", "ps_4_0"),
            ):
                program = programs[f"{public}/{suffix}"]
                with self.subTest(program=public, backend=suffix):
                    self.assertEqual(program.language, language)
                    self.assertEqual(
                        normalized_lines(program.body, "source"),
                        [f"source {source}"],
                    )
                    self.assertEqual(
                        normalized_lines(program.body, "preprocessor_defines"),
                        [f"preprocessor_defines {selector}"],
                    )
                    if suffix == "GL3Plus":
                        self.assertEqual(
                            normalized_lines(program.body, "syntax"),
                            ["syntax glsl330"],
                        )
                        self.assertEqual(
                            normalized_lines(program.body, "entry_point"), []
                        )
                    else:
                        self.assertEqual(
                            normalized_lines(program.body, "entry_point"),
                            [f"entry_point {entry}"],
                        )
                        self.assertEqual(
                            normalized_lines(program.body, "target"),
                            [f"target {target}"],
                        )

        self.assertEqual(
            normalized_lines(programs["grassFP/GL3Plus"].body, "param_named "),
            [
                "param_named diffuseMap int 0",
                "param_named shadowMap0 int 1",
                "param_named shadowMap1 int 2",
                "param_named shadowMap2 int 3",
                "param_named pssmSplitPoints float4 1.0 187 774 10000.0",
            ],
        )
        for slot, name in enumerate(
            ("diffuseMap", "shadowMap0", "shadowMap1", "shadowMap2")
        ):
            with self.subTest(resource=name):
                self.assertIn(f"Texture2D {name} : register(t{slot});", HLSL)
                self.assertIn(
                    f"SamplerState {name}Sampler : register(s{slot});", HLSL
                )

    def test_animation_fade_pssm_and_alpha_semantics_are_preserved(self) -> None:
        for source, position, uv, colour in (
            (GLSL, "position", "uv0", "grassColour"),
            (HLSL, "position", "input.uv0", "output.colour"),
        ):
            with self.subTest(source="GLSL" if source is GLSL else "HLSL"):
                self.assertIn("distanceToCamera", source)
                self.assertIn("2.0 - (2.0 * distanceToCamera / fadeRange)", source)
                self.assertIn(f"if ({uv}.y == 0.0)", source)
                self.assertIn("sin(time + originalX * frequency)", source)
                self.assertIn(f"{position} += direction * offset", source)
                self.assertIn("pssmSplitPoints.y", source)
                self.assertIn("pssmSplitPoints.z", source)
                self.assertEqual(source.count("0.65 + 0.35 * shadowing"), 1)
                self.assertIn(colour, source)
                self.assertEqual(source.count("sampleOffset.zy"), 2)

        self.assertIn(
            "fragColour = vec4(litColour, diffuseSample.a) * grassColour;",
            GLSL,
        )
        self.assertIn(
            "return float4(litColour, diffuseSample.a) * input.colour;",
            HLSL,
        )

    def test_material_uses_public_programs_and_exact_texture_order(self) -> None:
        self.assertEqual(SCRIPT.count("vertex_program_ref   grassVP"), 1)
        self.assertEqual(SCRIPT.count("fragment_program_ref grassFP"), 1)
        texture_units = re.findall(
            r"^[ \t]*texture_unit[ \t]+(\S+)", SCRIPT, re.MULTILINE
        )
        self.assertEqual(
            texture_units,
            ["diffuseMap", "shadow_tex0", "shadow_tex1", "shadow_tex2"],
        )
        self.assertEqual(SCRIPT.count("content_type shadow"), 3)
        self.assertIn("alpha_rejection greater_equal 128", SCRIPT)


if __name__ == "__main__":
    unittest.main()
