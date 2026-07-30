#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import ast
from pathlib import Path
import re
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
RESOURCE_ROOT = REPOSITORY_ROOT / "resources/postprocess"
MATH_HEADER = REPOSITORY_ROOT / "source/main/gfx/PostProcessMath.h"
PROGRAM_PATH = RESOURCE_ROOT / "ror_postprocess_v0a.program"
MATERIAL_PATH = RESOURCE_ROOT / "ror_postprocess_v0a.material"
COMPOSITOR_PATH = RESOURCE_ROOT / "ror_postprocess_v0a.compositor"
GLSL_VERTEX_PATH = RESOURCE_ROOT / "ror_postprocess_v0a_gl3plus.vert"
GLSL_FRAGMENT_PATH = RESOURCE_ROOT / "ror_postprocess_v0a_gl3plus.frag"
HLSL_PATH = RESOURCE_ROOT / "ror_postprocess_v0a_d3d11.hlsl"

EXPECTED_RESOURCE_FILES = {
    "ror_postprocess_v0a.compositor",
    "ror_postprocess_v0a.material",
    "ror_postprocess_v0a.program",
    "ror_postprocess_v0a_d3d11.hlsl",
    "ror_postprocess_v0a_gl3plus.frag",
    "ror_postprocess_v0a_gl3plus.vert",
}

CONFIG_CONSTANTS = {
    "ROR_V0A_EXPOSURE": "exposure",
    "ROR_V0A_CONTRAST": "contrast",
    "ROR_V0A_SATURATION": "saturation",
    "ROR_V0A_FXAA_EDGE_THRESHOLD": "fxaa_edge_threshold",
    "ROR_V0A_FXAA_EDGE_THRESHOLD_MIN": "fxaa_edge_threshold_min",
    "ROR_V0A_FXAA_BLEND_LIMIT": "fxaa_blend_limit",
}

LOCKED_NON_CONFIG_CONSTANTS = {
    "ROR_V0A_SHOULDER": 0.12,
    "ROR_V0A_LUMA_RED": 0.2126,
    "ROR_V0A_LUMA_GREEN": 0.7152,
    "ROR_V0A_LUMA_BLUE": 0.0722,
}


def _evaluate_number(expression: str) -> float:
    node = ast.parse(expression.strip(), mode="eval").body

    def evaluate(current: ast.expr) -> float:
        if isinstance(current, ast.Constant) and isinstance(
            current.value, (int, float)
        ):
            return float(current.value)
        if isinstance(current, ast.BinOp) and isinstance(current.op, ast.Div):
            denominator = evaluate(current.right)
            if denominator == 0.0:
                raise ValueError("zero denominator")
            return evaluate(current.left) / denominator
        if isinstance(current, ast.UnaryOp) and isinstance(
            current.op, (ast.UAdd, ast.USub)
        ):
            value = evaluate(current.operand)
            return value if isinstance(current.op, ast.UAdd) else -value
        raise ValueError(f"unsupported numeric expression: {expression!r}")

    return evaluate(node)


def _extract_braced_block(source: str, header: str) -> str:
    header_offset = source.find(header)
    if header_offset < 0:
        raise ValueError(f"missing block header: {header}")
    opening = source.find("{", header_offset + len(header))
    if opening < 0:
        raise ValueError(f"missing opening brace after: {header}")

    depth = 0
    for offset in range(opening, len(source)):
        if source[offset] == "{":
            depth += 1
        elif source[offset] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : offset]
    raise ValueError(f"unterminated block: {header}")


def _extract_shader_constants(source: str) -> dict[str, float]:
    matches = re.findall(
        r"(?:static\s+)?const\s+float\s+"
        r"(ROR_V0A_[A-Z0-9_]+)\s*=\s*([^;]+);",
        source,
    )
    return {name: _evaluate_number(value) for name, value in matches}


def _extract_config_defaults(source: str) -> dict[str, float]:
    defaults: dict[str, float] = {}
    for field in CONFIG_CONSTANTS.values():
        match = re.search(
            rf"\bdouble\s+{re.escape(field)}\s*=\s*([^;]+);",
            source,
        )
        if match is None:
            raise ValueError(f"missing PostProcessConfig field: {field}")
        defaults[field] = _evaluate_number(match.group(1))
    return defaults


def _squash_whitespace(source: str) -> str:
    return re.sub(r"\s+", " ", source).strip()


class PostProcessResourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.program = PROGRAM_PATH.read_text(encoding="utf-8")
        cls.material = MATERIAL_PATH.read_text(encoding="utf-8")
        cls.compositor = COMPOSITOR_PATH.read_text(encoding="utf-8")
        cls.glsl_vertex = GLSL_VERTEX_PATH.read_text(encoding="utf-8")
        cls.glsl_fragment = GLSL_FRAGMENT_PATH.read_text(encoding="utf-8")
        cls.hlsl = HLSL_PATH.read_text(encoding="utf-8")
        cls.math_header = MATH_HEADER.read_text(encoding="utf-8")

    def test_exact_resource_inventory_and_portable_line_endings(self) -> None:
        actual_files = {
            path.relative_to(RESOURCE_ROOT).as_posix()
            for path in RESOURCE_ROOT.rglob("*")
            if path.is_file()
        }
        self.assertEqual(actual_files, EXPECTED_RESOURCE_FILES)

        attributes = (
            REPOSITORY_ROOT / ".gitattributes"
        ).read_text(encoding="utf-8")
        self.assertEqual(
            attributes.count("resources/postprocess/** text eol=lf"),
            1,
        )
        for path in RESOURCE_ROOT.iterdir():
            if path.is_file():
                self.assertNotIn(b"\r", path.read_bytes(), path.name)

    def test_program_inventory_namespaces_and_backends_are_exact(self) -> None:
        definitions = re.findall(
            r"^(vertex_program|fragment_program)\s+(\S+)\s+(\S+)\s*$",
            self.program,
            flags=re.MULTILINE,
        )
        self.assertEqual(
            definitions,
            [
                (
                    "vertex_program",
                    "RoR/PostProcess/V0A/Vertex/GL3Plus",
                    "glsl",
                ),
                (
                    "fragment_program",
                    "RoR/PostProcess/V0A/Fragment/GL3Plus",
                    "glsl",
                ),
                (
                    "vertex_program",
                    "RoR/PostProcess/V0A/Vertex/D3D11",
                    "hlsl",
                ),
                (
                    "fragment_program",
                    "RoR/PostProcess/V0A/Fragment/D3D11",
                    "hlsl",
                ),
                (
                    "vertex_program",
                    "RoR/PostProcess/V0A/Vertex",
                    "unified",
                ),
                (
                    "fragment_program",
                    "RoR/PostProcess/V0A/Fragment",
                    "unified",
                ),
            ],
        )

        glsl_vertex = _extract_braced_block(
            self.program,
            "vertex_program RoR/PostProcess/V0A/Vertex/GL3Plus glsl",
        )
        glsl_fragment = _extract_braced_block(
            self.program,
            "fragment_program RoR/PostProcess/V0A/Fragment/GL3Plus glsl",
        )
        hlsl_vertex = _extract_braced_block(
            self.program,
            "vertex_program RoR/PostProcess/V0A/Vertex/D3D11 hlsl",
        )
        hlsl_fragment = _extract_braced_block(
            self.program,
            "fragment_program RoR/PostProcess/V0A/Fragment/D3D11 hlsl",
        )

        for block, source_name in (
            (glsl_vertex, GLSL_VERTEX_PATH.name),
            (glsl_fragment, GLSL_FRAGMENT_PATH.name),
        ):
            self.assertIn("syntax glsl330", block)
            self.assertIn(f"source {source_name}", block)
            self.assertNotIn("entry_point", block)

        self.assertIn(f"source {HLSL_PATH.name}", hlsl_vertex)
        self.assertIn("entry_point V0A_vs", hlsl_vertex)
        self.assertIn("target vs_4_0", hlsl_vertex)
        self.assertIn(f"source {HLSL_PATH.name}", hlsl_fragment)
        self.assertIn("entry_point V0A_ps", hlsl_fragment)
        self.assertIn("target ps_4_0", hlsl_fragment)
        self.assertEqual(
            self.program.count(
                "param_named_auto uWorldViewProj worldviewproj_matrix"
            ),
            2,
        )
        self.assertEqual(
            self.program.count(
                "param_named_auto uInvTextureSize inverse_texture_size 0"
            ),
            2,
        )
        self.assertEqual(
            self.program.count("param_named uScene int 0"),
            1,
        )

    def test_unified_programs_delegate_only_to_the_paired_backends(self) -> None:
        vertex = _extract_braced_block(
            self.program,
            "vertex_program RoR/PostProcess/V0A/Vertex unified",
        )
        fragment = _extract_braced_block(
            self.program,
            "fragment_program RoR/PostProcess/V0A/Fragment unified",
        )
        self.assertEqual(
            re.findall(r"\bdelegate\s+(\S+)", vertex),
            [
                "RoR/PostProcess/V0A/Vertex/GL3Plus",
                "RoR/PostProcess/V0A/Vertex/D3D11",
            ],
        )
        self.assertEqual(
            re.findall(r"\bdelegate\s+(\S+)", fragment),
            [
                "RoR/PostProcess/V0A/Fragment/GL3Plus",
                "RoR/PostProcess/V0A/Fragment/D3D11",
            ],
        )

    def test_material_and_compositor_are_namespaced_and_fail_closed(self) -> None:
        self.assertEqual(
            re.findall(r"^material\s+(\S+)\s*$", self.material, re.MULTILINE),
            ["RoR/PostProcess/V0A/LdrFxaa"],
        )
        self.assertEqual(
            re.findall(
                r"^compositor\s+(\S+)\s*$",
                self.compositor,
                re.MULTILINE,
            ),
            ["RoR/PostProcess/V0A/LdrFxaa"],
        )
        for token in (
            "lighting off",
            "depth_check off",
            "depth_write off",
            "cull_hardware none",
            "cull_software none",
            "polygon_mode_overrideable false",
            "vertex_program_ref RoR/PostProcess/V0A/Vertex",
            "fragment_program_ref RoR/PostProcess/V0A/Fragment",
            "texture_unit RoR/PostProcess/V0A/Scene",
            "tex_address_mode clamp",
            "filtering none",
        ):
            self.assertIn(token, self.material)

        for token in (
            "texture ror_v0a_scene target_width target_height "
            "PF_BYTE_RGBA no_fsaa",
            "target ror_v0a_scene",
            "input previous",
            "target_output",
            "input none",
            "pass render_quad",
            "material RoR/PostProcess/V0A/LdrFxaa",
            "input 0 ror_v0a_scene",
        ):
            self.assertIn(token, self.compositor)
        self.assertEqual(self.compositor.count("pass render_quad"), 1)

    def test_shader_entry_points_and_interstage_contract_are_explicit(self) -> None:
        self.assertTrue(self.glsl_vertex.startswith("#version 330 core\n"))
        self.assertTrue(self.glsl_fragment.startswith("#version 330 core\n"))
        self.assertEqual(self.glsl_vertex.count("void main()"), 1)
        self.assertEqual(self.glsl_fragment.count("void main()"), 1)
        self.assertIn(
            "layout(location = 0) in vec4 vertex;",
            self.glsl_vertex,
        )
        self.assertIn("out vec2 vUv;", self.glsl_vertex)
        self.assertIn("in vec2 vUv;", self.glsl_fragment)
        self.assertIn("out vec4 fragColor;", self.glsl_fragment)

        self.assertEqual(
            len(re.findall(r"\bV0A_vs\s*\(", self.hlsl)),
            1,
        )
        self.assertEqual(
            len(re.findall(r"\bV0A_ps\s*\(", self.hlsl)),
            1,
        )
        self.assertIn("float4 position : SV_Position;", self.hlsl)
        self.assertIn("float2 uv : TEXCOORD0;", self.hlsl)
        self.assertIn(
            "RorV0AVertexOutput V0A_vs(float4 position : POSITION)",
            self.hlsl,
        )
        self.assertIn(
            "float4 V0A_ps(RorV0AVertexOutput input) : SV_Target",
            self.hlsl,
        )

    def test_shader_constants_match_the_cpu_oracle(self) -> None:
        expected = _extract_config_defaults(self.math_header)
        shoulder = re.search(
            r"\bconst\s+double\s+shoulder\s*=\s*([^;]+);",
            self.math_header,
        )
        self.assertIsNotNone(shoulder)
        self.assertEqual(
            _evaluate_number(shoulder.group(1)),
            LOCKED_NON_CONFIG_CONSTANTS["ROR_V0A_SHOULDER"],
        )

        expected_shader_constants = {
            shader_name: expected[config_name]
            for shader_name, config_name in CONFIG_CONSTANTS.items()
        }
        expected_shader_constants.update(LOCKED_NON_CONFIG_CONSTANTS)
        for source in (self.glsl_fragment, self.hlsl):
            self.assertEqual(
                _extract_shader_constants(source),
                expected_shader_constants,
            )

        for token in (
            "color.red * 0.2126",
            "color.green * 0.7152",
            "color.blue * 0.0722",
        ):
            self.assertIn(token, self.math_header)

    def test_color_curve_precedes_five_sample_fxaa(self) -> None:
        glsl_main = _extract_braced_block(
            self.glsl_fragment,
            "void main()",
        )
        hlsl_main = _extract_braced_block(
            self.hlsl,
            "float4 V0A_ps(RorV0AVertexOutput input) : SV_Target",
        )
        self.assertEqual(
            len(re.findall(r"\btexture\s*\(", self.glsl_fragment)),
            5,
        )
        self.assertEqual(self.hlsl.count(".Sample("), 5)
        for main in (glsl_main, hlsl_main):
            curve_offsets = [
                match.start()
                for match in re.finditer(r"\brorV0AColorCurve\s*\(", main)
            ]
            resolve_offset = main.find("rorV0AResolveFxaa(")
            self.assertEqual(len(curve_offsets), 5)
            self.assertGreater(resolve_offset, max(curve_offsets))
            for direction in ("center", "north", "south", "east", "west"):
                self.assertRegex(
                    main,
                    rf"\b{direction}\s*=\s*rorV0AColorCurve\s*\(",
                )
            self.assertIn("centerSample.a", main)

    def test_curve_and_edge_operation_order_matches_the_cpu_oracle(self) -> None:
        for source, curve_header, fxaa_header in (
            (
                self.glsl_fragment,
                "vec3 rorV0AColorCurve(vec3 inputColor)",
                "vec3 rorV0AResolveFxaa(",
            ),
            (
                self.hlsl,
                "float3 rorV0AColorCurve(float3 inputColor)",
                "float3 rorV0AResolveFxaa(",
            ),
        ):
            curve = _squash_whitespace(
                _extract_braced_block(source, curve_header)
            )
            curve_markers = (
                "inputColor * ROR_V0A_EXPOSURE",
                "rorV0ALuma(exposed)",
                "(exposed -",
                "* ROR_V0A_SATURATION",
                "(saturated -",
                "* ROR_V0A_CONTRAST",
                "max(contrasted,",
                "(1.0 + ROR_V0A_SHOULDER) * positive",
                "ROR_V0A_SHOULDER * positive",
                "return clamp(",
            )
            previous = -1
            for marker in curve_markers:
                offset = curve.find(marker)
                self.assertGreater(offset, previous, marker)
                previous = offset

            fxaa = _squash_whitespace(
                _extract_braced_block(source, fxaa_header)
            )
            fxaa_markers = (
                "float minimumLuma = min(",
                "float maximumLuma = max(",
                "float lumaRange = maximumLuma - minimumLuma",
                "float edgeThreshold = max(",
                "if (lumaRange <= edgeThreshold)",
                "float northSouthGradient = abs(",
                "float eastWestGradient = abs(",
                "if (northSouthGradient >= eastWestGradient)",
                "float blend = clamp(",
                "(lumaRange - edgeThreshold) / lumaRange",
                "ROR_V0A_FXAA_BLEND_LIMIT",
                "center + (neighborAverage - center) * blend",
                "return clamp(",
            )
            previous = -1
            for marker in fxaa_markers:
                offset = fxaa.find(marker)
                self.assertGreater(offset, previous, marker)
                previous = offset

    def test_shader_sources_have_no_unbounded_or_nondeterministic_paths(self) -> None:
        shader_sources = {
            GLSL_VERTEX_PATH.name: self.glsl_vertex,
            GLSL_FRAGMENT_PATH.name: self.glsl_fragment,
            HLSL_PATH.name: self.hlsl,
        }
        forbidden_tokens = (
            "pow",
            "exp",
            "log",
            "sin",
            "cos",
            "tan",
            "sqrt",
            "noise",
            "random",
            "rand",
            "time",
            "clock",
            "discard",
            "textureGrad",
            "textureLod",
            "SampleGrad",
            "SampleLevel",
            "Gather",
            "ddx",
            "ddy",
        )
        for name, source in shader_sources.items():
            for token in forbidden_tokens:
                self.assertIsNone(
                    re.search(rf"\b{re.escape(token)}\b", source),
                    f"{name}: forbidden token {token}",
                )
            for loop_keyword in ("for", "while", "do"):
                self.assertIsNone(
                    re.search(rf"\b{loop_keyword}\b", source),
                    f"{name}: unbounded control flow {loop_keyword}",
                )

        all_resources = "\n".join(
            (
                self.program,
                self.material,
                self.compositor,
                *shader_sources.values(),
            )
        )
        self.assertIsNone(re.search(r"\bcg\b", all_resources, re.IGNORECASE))
        self.assertNotIn(".cg", all_resources.lower())
        self.assertIsNone(
            re.search(r"\b(?:auto_?exposure|bloom)\b", all_resources, re.I)
        )


if __name__ == "__main__":
    unittest.main()
