#!/usr/bin/env python3
"""Fail closed on Caelum's modern GL3Plus and D3D11 shader closure."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
CAELUM = ROOT / "resources/caelum"

SCRIPT_NAMES = (
    "DepthComposer.material",
    "DepthRender.program",
    "GroundFog.material",
    "GroundFog.program",
    "Haze.program",
    "LayeredClouds.material",
    "MinimalCompositorVP.program",
    "PointStarfield.material",
    "Precipitation.material",
    "SkyDome.material",
    "moon.material",
)
SCRIPT_PATHS = tuple(CAELUM / name for name in SCRIPT_NAMES)
SCRIPTS = {path.name: path.read_text(encoding="utf-8") for path in SCRIPT_PATHS}
SCRIPT = "\n".join(SCRIPTS.values())

LEGACY_SOURCES = {
    "CaelumGroundFog.cg",
    "CaelumLayeredClouds.cg",
    "CaelumPhaseMoon.cg",
    "CaelumPointStarfield.cg",
    "CaelumSkyDome.cg",
    "DepthComposer.cg",
    "MinimalCompositorVP.cg",
    "Precipitation.cg",
}
MODERN_STEMS = {
    "CaelumGroundFog",
    "CaelumLayeredClouds",
    "CaelumPhaseMoon",
    "CaelumPointStarfield",
    "CaelumSkyDome",
    "DepthComposer",
    "MinimalCompositorVP",
    "Precipitation",
}


@dataclass(frozen=True)
class ProgramContract:
    stage: str
    source_stem: str
    selector: str
    hlsl_entry: str


PUBLIC_PROGRAMS = {
    "CaelumGroundFogVP": ProgramContract(
        "vertex", "CaelumGroundFog", "CAELUM_GROUND_FOG_VERTEX=1",
        "CaelumGroundFogVS",
    ),
    "CaelumGroundFogFP": ProgramContract(
        "fragment", "CaelumGroundFog", "CAELUM_GROUND_FOG_FRAGMENT=1",
        "CaelumGroundFogPS",
    ),
    "CaelumGroundFogDomeVP": ProgramContract(
        "vertex", "CaelumGroundFog", "CAELUM_GROUND_FOG_DOME_VERTEX=1",
        "CaelumGroundFogDomeVS",
    ),
    "CaelumGroundFogDomeFP": ProgramContract(
        "fragment", "CaelumGroundFog", "CAELUM_GROUND_FOG_DOME_FRAGMENT=1",
        "CaelumGroundFogDomePS",
    ),
    "CaelumLayeredCloudsVP": ProgramContract(
        "vertex", "CaelumLayeredClouds", "CAELUM_LAYERED_CLOUDS_VERTEX=1",
        "CaelumLayeredCloudsVS",
    ),
    "CaelumLayeredCloudsFP": ProgramContract(
        "fragment", "CaelumLayeredClouds", "CAELUM_LAYERED_CLOUDS_FRAGMENT=1",
        "CaelumLayeredCloudsPS",
    ),
    "Caelum/PhaseMoonFP": ProgramContract(
        "fragment", "CaelumPhaseMoon", "CAELUM_PHASE_MOON_FRAGMENT=1",
        "CaelumPhaseMoonPS",
    ),
    "Caelum/StarPointVP": ProgramContract(
        "vertex", "CaelumPointStarfield", "CAELUM_POINT_STAR_VERTEX=1",
        "CaelumPointStarVS",
    ),
    "Caelum/StarPointFP": ProgramContract(
        "fragment", "CaelumPointStarfield", "CAELUM_POINT_STAR_FRAGMENT=1",
        "CaelumPointStarPS",
    ),
    "CaelumSkyDomeVP": ProgramContract(
        "vertex", "CaelumSkyDome", "CAELUM_SKYDOME_VERTEX=1",
        "CaelumSkyDomeVS",
    ),
    "CaelumSkyDomeFP": ProgramContract(
        "fragment", "CaelumSkyDome", "CAELUM_SKYDOME_FRAGMENT_HAZE=1",
        "CaelumSkyDomePS",
    ),
    "CaelumSkyDomeFP_NoHaze": ProgramContract(
        "fragment", "CaelumSkyDome", "CAELUM_SKYDOME_FRAGMENT_NO_HAZE=1",
        "CaelumSkyDomePS",
    ),
    "CaelumHazeVP": ProgramContract(
        "vertex", "CaelumSkyDome", "CAELUM_HAZE_VERTEX=1", "CaelumHazeVS",
    ),
    "CaelumHazeFP": ProgramContract(
        "fragment", "CaelumSkyDome", "CAELUM_HAZE_FRAGMENT=1", "CaelumHazePS",
    ),
    "Caelum/DepthComposerFP_Dummy": ProgramContract(
        "fragment", "DepthComposer", "CAELUM_DEPTH_COMPOSER_FRAGMENT=1",
        "CaelumDepthComposerPS",
    ),
    "Caelum/DepthComposerFP_DebugDepthRender": ProgramContract(
        "fragment", "DepthComposer",
        "CAELUM_DEPTH_COMPOSER_FRAGMENT=1,CAELUM_DEPTH_DEBUG_RENDER=1",
        "CaelumDepthComposerPS",
    ),
    "Caelum/DepthComposerFP_ExpGroundFog": ProgramContract(
        "fragment", "DepthComposer",
        "CAELUM_DEPTH_COMPOSER_FRAGMENT=1,CAELUM_DEPTH_EXP_GROUND_FOG=1",
        "CaelumDepthComposerPS",
    ),
    "Caelum/DepthComposerFP_SkyDomeHaze": ProgramContract(
        "fragment", "DepthComposer",
        "CAELUM_DEPTH_COMPOSER_FRAGMENT=1,CAELUM_DEPTH_SKY_DOME_HAZE=1",
        "CaelumDepthComposerPS",
    ),
    "Caelum/DepthComposerFP_SkyDomeHaze_ExpGroundFog": ProgramContract(
        "fragment", "DepthComposer",
        "CAELUM_DEPTH_COMPOSER_FRAGMENT=1,CAELUM_DEPTH_EXP_GROUND_FOG=1,"
        "CAELUM_DEPTH_SKY_DOME_HAZE=1",
        "CaelumDepthComposerPS",
    ),
    "Caelum/DepthRenderVP": ProgramContract(
        "vertex", "DepthComposer", "CAELUM_DEPTH_RENDER_VERTEX=1",
        "CaelumDepthRenderVS",
    ),
    "Caelum/DepthRenderFP": ProgramContract(
        "fragment", "DepthComposer", "CAELUM_DEPTH_RENDER_FRAGMENT=1",
        "CaelumDepthRenderPS",
    ),
    "Caelum/DepthRenderAlphaRejectionVP": ProgramContract(
        "vertex", "DepthComposer", "CAELUM_DEPTH_ALPHA_VERTEX=1",
        "CaelumDepthAlphaVS",
    ),
    "Caelum/DepthRenderAlphaRejectionFP": ProgramContract(
        "fragment", "DepthComposer", "CAELUM_DEPTH_ALPHA_FRAGMENT=1",
        "CaelumDepthAlphaPS",
    ),
    "Caelum/MinimalCompositorVP": ProgramContract(
        "vertex", "MinimalCompositorVP",
        "CAELUM_MINIMAL_COMPOSITOR_VERTEX=1", "CaelumMinimalCompositorVS",
    ),
    "Caelum/PrecipitationVP": ProgramContract(
        "vertex", "Precipitation", "CAELUM_PRECIPITATION_VERTEX=1",
        "CaelumPrecipitationVS",
    ),
    "Caelum/PrecipitationFP": ProgramContract(
        "fragment", "Precipitation", "CAELUM_PRECIPITATION_FRAGMENT=1",
        "CaelumPrecipitationPS",
    ),
}

BASE_PARAMETERS = {
    "CaelumGroundFogVP": {
        "param_named_auto worldViewProj worldviewproj_matrix",
        "param_named_auto world world_matrix",
    },
    "CaelumGroundFogFP": {
        "param_named_auto camPos camera_position",
        "param_named fogDensity float 0",
        "param_named fogVerticalDecay float 0",
        "param_named fogGroundLevel float 0",
        "param_named fogColour float4 0 0 0 0",
    },
    "CaelumGroundFogDomeVP": {
        "param_named_auto worldViewProj worldviewproj_matrix",
    },
    "CaelumGroundFogDomeFP": {
        "param_named fogColour float4 0 0 0 0",
        "param_named fogDensity float 0",
        "param_named fogVerticalDecay float 0",
        "param_named fogGroundLevel float 0",
        "param_named cameraHeight float 0",
    },
    "CaelumLayeredCloudsVP": {
        "param_named_auto worldViewProj worldviewproj_matrix",
        "param_named_auto worldMatrix world_matrix",
        "param_named sunDirection float3 -1 -1 0",
    },
    "CaelumLayeredCloudsFP": {
        "param_named sunLightColour float4 1 1 1 1",
        "param_named sunSphereColour float4 1 1 1 1",
        "param_named sunDirection float4 1 1 1 1",
        "param_named fogColour float4 0 0 0 0",
        "param_named cloudMassInvScale float 1.2",
        "param_named cloudDetailInvScale float 4.8",
        "param_named cloudMassOffset float2 0 0",
        "param_named cloudDetailOffset float2 0.5 0.5",
        "param_named cloudMassBlend float 0.9",
        "param_named cloudDetailBlend float 0.5",
        "param_named cloudCoverageThreshold float 0.9",
        "param_named cloudSharpness float 4",
        "param_named cloudThickness float 3",
        "param_named_auto camera_position camera_position",
        "param_named layerHeight float 0",
        "param_named cloudUVFactor float -1",
        "param_named heightRedFactor float -1",
        "param_named nearFadeDist float -1",
        "param_named farFadeDist float -1",
        "param_named fadeDistMeasurementVector float3 0 1 1",
    },
    "Caelum/PhaseMoonFP": {"param_named phase float 0.3"},
    "Caelum/StarPointVP": {
        "param_named_auto worldviewproj_matrix worldviewproj_matrix",
        "param_named_auto render_target_flipping render_target_flipping",
        "param_named mag_scale float -1",
        "param_named mag0_size float -1",
        "param_named min_size float -1",
        "param_named max_size float -1",
        "param_named aspect_ratio float -1",
    },
    "Caelum/StarPointFP": set(),
    "CaelumSkyDomeVP": {
        "param_named_auto worldViewProj worldviewproj_matrix",
        "param_named sunDirection float3 1 0 0",
    },
    "CaelumSkyDomeFP": {
        "param_named offset float 0",
        "param_named hazeColour float4 0 0 0 0",
    },
    "CaelumSkyDomeFP_NoHaze": {"param_named offset float 0"},
    "CaelumHazeVP": {
        "param_named_auto worldViewProj worldviewproj_matrix",
        "param_named_auto camPos camera_position",
    },
    "CaelumHazeFP": {"param_named_auto fogColour fog_colour"},
    "Caelum/DepthComposerFP_Dummy": set(),
    "Caelum/DepthComposerFP_DebugDepthRender": {
        "param_named invViewProjMatrix float4x4 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0",
    },
    "Caelum/DepthComposerFP_ExpGroundFog": {
        "param_named invViewProjMatrix float4x4 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0",
        "param_named worldCameraPos float4 0 0 0 0",
        "param_named groundFogDensity float 0.1",
        "param_named groundFogVerticalDecay float 0.2",
        "param_named groundFogBaseLevel float 5",
        "param_named groundFogColour float4 1 0 1 1",
    },
    "Caelum/DepthComposerFP_SkyDomeHaze": {
        "param_named invViewProjMatrix float4x4 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0",
        "param_named worldCameraPos float4 0 0 0 0",
        "param_named sunDirection float3 0 1 0",
        "param_named hazeColour float3 0.1 0.2 0.6",
    },
    "Caelum/DepthComposerFP_SkyDomeHaze_ExpGroundFog": {
        "param_named invViewProjMatrix float4x4 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0",
        "param_named worldCameraPos float4 0 0 0 0",
        "param_named sunDirection float3 0 1 0",
        "param_named hazeColour float3 0.1 0.2 0.6",
        "param_named groundFogDensity float 0.1",
        "param_named groundFogVerticalDecay float 0.2",
        "param_named groundFogBaseLevel float 5",
        "param_named groundFogColour float4 1 0 1 1",
    },
    "Caelum/DepthRenderVP": {
        "param_named_auto wvpMatrix worldviewproj_matrix",
    },
    "Caelum/DepthRenderFP": set(),
    "Caelum/DepthRenderAlphaRejectionVP": {
        "param_named_auto wvpMatrix worldviewproj_matrix",
    },
    "Caelum/DepthRenderAlphaRejectionFP": set(),
    "Caelum/MinimalCompositorVP": {
        "param_named_auto worldviewproj_matrix worldviewproj_matrix",
    },
    "Caelum/PrecipitationVP": {
        "param_named_auto worldviewproj_matrix worldviewproj_matrix",
    },
    "Caelum/PrecipitationFP": set(),
}

GLSL_SAMPLERS = {
    "CaelumLayeredCloudsFP": {
        "param_named cloud_shape1 int 0",
        "param_named cloud_shape2 int 1",
        "param_named cloud_detail int 2",
    },
    "Caelum/PhaseMoonFP": {"param_named moonDisc int 0"},
    "CaelumSkyDomeFP": {
        "param_named gradientsMap int 0",
        "param_named atmRelativeDepth int 1",
    },
    "CaelumSkyDomeFP_NoHaze": {
        "param_named gradientsMap int 0",
        "param_named atmRelativeDepth int 1",
    },
    "CaelumHazeFP": {
        "param_named atmRelativeDepth int 0",
        "param_named gradientsMap int 1",
    },
    "Caelum/DepthComposerFP_Dummy": {
        "param_named screenTexture int 0",
        "param_named depthTexture int 1",
    },
    "Caelum/DepthComposerFP_DebugDepthRender": {
        "param_named screenTexture int 0",
        "param_named depthTexture int 1",
    },
    "Caelum/DepthComposerFP_ExpGroundFog": {
        "param_named screenTexture int 0",
        "param_named depthTexture int 1",
    },
    "Caelum/DepthComposerFP_SkyDomeHaze": {
        "param_named screenTexture int 0",
        "param_named depthTexture int 1",
        "param_named atmRelativeDepth int 2",
    },
    "Caelum/DepthComposerFP_SkyDomeHaze_ExpGroundFog": {
        "param_named screenTexture int 0",
        "param_named depthTexture int 1",
        "param_named atmRelativeDepth int 2",
    },
    "Caelum/DepthRenderAlphaRejectionFP": {"param_named mainTex int 0"},
    "Caelum/PrecipitationFP": {
        "param_named scene int 0",
        "param_named samplerPrec int 1",
    },
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


def normalized_parameters(block: str) -> set[str]:
    return {" ".join(match.group(1).split()) for match in PARAMETER.finditer(block)}


class CaelumShaderContractTests(unittest.TestCase):
    def test_cg_family_is_replaced_by_exact_modern_source_pairs(self) -> None:
        self.assertEqual({path.name for path in CAELUM.glob("*.cg")}, set())
        for name in LEGACY_SOURCES:
            self.assertFalse((CAELUM / name).exists(), name)

        expected_modern = {
            f"{stem}_{backend}.{extension}"
            for stem in MODERN_STEMS
            for backend, extension in (
                ("gl3plus", "glsl"),
                ("d3d11", "hlsl"),
            )
        }
        actual_modern = {
            path.name
            for pattern in ("*.glsl", "*.hlsl")
            for path in CAELUM.glob(pattern)
        }
        self.assertEqual(actual_modern, expected_modern)
        for name in expected_modern:
            path = CAELUM / name
            with self.subTest(source=name):
                self.assertTrue(path.is_file())
                self.assertFalse(path.is_symlink())
                self.assertGreater(len(path.read_bytes()), 0)
                if name.endswith(".glsl"):
                    self.assertTrue(
                        path.read_text(encoding="utf-8").startswith(
                            "#version 330 core\n"
                        )
                    )

        modern_text = "\n".join(
            path.read_text(encoding="utf-8")
            for path in (*SCRIPT_PATHS, *(CAELUM / name for name in expected_modern))
        )
        self.assertNotRegex(
            modern_text,
            re.compile(
                r"(?i)(?:\bcg\b|\.cg\b|\bprofiles?\b|\barbvp1\b|"
                r"\barbfp1\b|\bvp30\b|\bfp30\b|\bvs_[123]_\d\b|"
                r"\bps_[123]_\d\b)"
            ),
        )

    def test_all_public_aliases_have_exact_two_backend_closure(self) -> None:
        programs = parse_programs(SCRIPT)
        expected_names = set(PUBLIC_PROGRAMS)
        expected_names.update(
            f"{name}/{backend}"
            for name in PUBLIC_PROGRAMS
            for backend in ("GL3Plus", "D3D11")
        )
        self.assertEqual(set(programs), expected_names)
        self.assertEqual(len(PUBLIC_PROGRAMS), 26)

        for name, contract in PUBLIC_PROGRAMS.items():
            public = programs[name]
            with self.subTest(public=name):
                self.assertEqual(
                    (public.stage, public.language),
                    (contract.stage, "unified"),
                )
                self.assertEqual(
                    normalized_lines(public.body, "delegate"),
                    [
                        f"delegate {name}/GL3Plus",
                        f"delegate {name}/D3D11",
                    ],
                )
            self.assertEqual(
                (programs[f"{name}/GL3Plus"].stage,
                 programs[f"{name}/GL3Plus"].language),
                (contract.stage, "glsl"),
            )
            self.assertEqual(
                (programs[f"{name}/D3D11"].stage,
                 programs[f"{name}/D3D11"].language),
                (contract.stage, "hlsl"),
            )

    def test_sources_selectors_entries_targets_and_parameters_are_exact(self) -> None:
        programs = parse_programs(SCRIPT)
        for name, contract in PUBLIC_PROGRAMS.items():
            glsl = programs[f"{name}/GL3Plus"]
            hlsl = programs[f"{name}/D3D11"]
            glsl_source = (CAELUM / f"{contract.source_stem}_gl3plus.glsl").read_text(
                encoding="utf-8"
            )
            hlsl_source = (CAELUM / f"{contract.source_stem}_d3d11.hlsl").read_text(
                encoding="utf-8"
            )
            with self.subTest(program=name):
                self.assertEqual(
                    normalized_lines(glsl.body, "source"),
                    [f"source {contract.source_stem}_gl3plus.glsl"],
                )
                self.assertEqual(
                    normalized_lines(hlsl.body, "source"),
                    [f"source {contract.source_stem}_d3d11.hlsl"],
                )
                self.assertEqual(
                    normalized_lines(glsl.body, "syntax"),
                    ["syntax glsl330"],
                )
                self.assertEqual(
                    normalized_lines(glsl.body, "preprocessor_defines"),
                    [f"preprocessor_defines {contract.selector}"],
                )
                self.assertEqual(
                    normalized_lines(hlsl.body, "preprocessor_defines"),
                    [f"preprocessor_defines {contract.selector}"],
                )
                self.assertEqual(
                    normalized_lines(hlsl.body, "entry_point"),
                    [f"entry_point {contract.hlsl_entry}"],
                )
                self.assertEqual(
                    normalized_lines(hlsl.body, "target"),
                    [
                        "target vs_4_0"
                        if contract.stage == "vertex"
                        else "target ps_4_0"
                    ],
                )
                self.assertEqual(
                    normalized_parameters(glsl.body),
                    BASE_PARAMETERS[name] | GLSL_SAMPLERS.get(name, set()),
                )
                self.assertEqual(
                    normalized_parameters(hlsl.body), BASE_PARAMETERS[name]
                )
                for define in contract.selector.split(","):
                    macro = define.split("=", 1)[0]
                    self.assertIn(macro, glsl_source)
                    self.assertIn(macro, hlsl_source)
                self.assertRegex(
                    hlsl_source,
                    re.compile(rf"\b{re.escape(contract.hlsl_entry)}\s*\("),
                )

    def test_sampler_slots_match_original_texture_unit_order(self) -> None:
        expected_hlsl_resources = {
            "CaelumLayeredClouds_d3d11.hlsl": (
                ("Texture2D", "cloud_shape1", "cloud_shape1Sampler", 0),
                ("Texture2D", "cloud_shape2", "cloud_shape2Sampler", 1),
                ("Texture2D", "cloud_detail", "cloud_detailSampler", 2),
            ),
            "CaelumPhaseMoon_d3d11.hlsl": (
                ("Texture2D", "moonDisc", "moonDiscSampler", 0),
            ),
            "CaelumSkyDome_d3d11.hlsl": (
                ("Texture2D", "gradientsMap", "gradientsMapSampler", 0),
                ("Texture1D", "atmRelativeDepth", "atmRelativeDepthSampler", 1),
                ("Texture1D", "atmRelativeDepth", "atmRelativeDepthSampler", 0),
                ("Texture2D", "gradientsMap", "gradientsMapSampler", 1),
            ),
            "DepthComposer_d3d11.hlsl": (
                ("Texture2D", "screenTexture", "screenTextureSampler", 0),
                ("Texture2D", "depthTexture", "depthTextureSampler", 1),
                ("Texture1D", "atmRelativeDepth", "atmRelativeDepthSampler", 2),
                ("Texture2D", "mainTex", "mainTexSampler", 0),
            ),
            "Precipitation_d3d11.hlsl": (
                ("Texture2D", "scene", "sceneSampler", 0),
                ("Texture2D", "samplerPrec", "precipitationSampler", 1),
            ),
        }
        for source_name, resources in expected_hlsl_resources.items():
            source = (CAELUM / source_name).read_text(encoding="utf-8")
            for texture_type, texture_name, sampler_name, slot in resources:
                with self.subTest(source=source_name, resource=texture_name, slot=slot):
                    self.assertIn(
                        f"{texture_type} {texture_name} : register(t{slot});",
                        source,
                    )
                    self.assertIn(
                        f"SamplerState {sampler_name} : register(s{slot});",
                        source,
                    )

    def test_atmosphere_cloud_moon_star_depth_and_precipitation_math_remains(self) -> None:
        source_pairs = {
            stem: (
                (CAELUM / f"{stem}_gl3plus.glsl").read_text(encoding="utf-8"),
                (CAELUM / f"{stem}_d3d11.hlsl").read_text(encoding="utf-8"),
            )
            for stem in MODERN_STEMS
        }
        semantic_markers = {
            "CaelumGroundFog": (
                "abs(value) < 0.0001",
                "exp(verticalDecay * (baseLevel - startHeight))",
                "inverseViewSine < 0.0",
            ),
            "CaelumLayeredClouds": (
                "cloudCoverageThreshold",
                "exp(cloudSharpness * intensity) - 1.0",
                "CaelumMagicColourMix",
                "farFadeDist - nearFadeDist",
            ),
            "CaelumPhaseMoon": (
                "phaseValue / 2.0",
                "luminance * alpha",
                "colour.rgb /= luminance",
            ),
            "CaelumPointStarfield": (
                "exp(mag_scale",
                "fade * fade",
                "1.5 * exp(-(squaredLength * 8.0))",
            ),
            "CaelumSkyDome": (
                "sunlightScatteringFactor",
                "atmRelativeDepth",
                "hazeAbsorption",
            ),
            "DepthComposer": (
                "invViewProjMatrix",
                "worldPosition /= worldPosition.w",
                "CAELUM_DEPTH_EXP_GROUND_FOG",
                "CAELUM_DEPTH_SKY_DOME_HAZE",
            ),
            "MinimalCompositorVP": (
                "sign(",
                "signedPosition.y",
            ),
            "Precipitation": (
                "intensity / 4.0",
                "deltaX.z",
                "min(min(firstLayer, secondLayer), thirdLayer)",
            ),
        }
        for stem, markers in semantic_markers.items():
            for backend, source in zip(("GL3Plus", "D3D11"), source_pairs[stem]):
                for marker in markers:
                    with self.subTest(stem=stem, backend=backend, marker=marker):
                        self.assertIn(marker.lower(), source.lower())

    def test_haze_fractional_pow_inputs_are_domain_safe_in_both_backends(self) -> None:
        for stem in ("CaelumSkyDome", "DepthComposer"):
            glsl = (CAELUM / f"{stem}_gl3plus.glsl").read_text(
                encoding="utf-8"
            )
            hlsl = (CAELUM / f"{stem}_d3d11.hlsl").read_text(
                encoding="utf-8"
            )
            with self.subTest(stem=stem, backend="GL3Plus"):
                self.assertIn(
                    "pow(max(1.0 - sunY, 0.0), inverseHazeHeight)", glsl
                )
                self.assertNotIn("pow(1.0 - sunY,", glsl)
            with self.subTest(stem=stem, backend="D3D11"):
                self.assertIn(
                    "pow(max(1.0 - sunY, 0.0), inverseHazeHeight)", hlsl
                )
                self.assertNotIn("pow(1.0 - sunY,", hlsl)

    def test_materials_still_bind_only_the_public_aliases(self) -> None:
        references = re.findall(
            r"^\s*(?:vertex|fragment)_program_ref\s+(\S+)",
            SCRIPT,
            re.MULTILINE,
        )
        expected_counts = {
            "CaelumGroundFogVP": 1,
            "CaelumGroundFogFP": 1,
            "CaelumGroundFogDomeVP": 1,
            "CaelumGroundFogDomeFP": 1,
            "CaelumLayeredCloudsVP": 1,
            "CaelumLayeredCloudsFP": 1,
            "Caelum/PhaseMoonFP": 1,
            "Caelum/StarPointVP": 1,
            "Caelum/StarPointFP": 1,
            "CaelumSkyDomeVP": 1,
            "CaelumSkyDomeFP": 1,
            "Caelum/DepthRenderVP": 1,
            "Caelum/DepthRenderFP": 1,
            "Caelum/DepthRenderAlphaRejectionVP": 1,
            "Caelum/DepthRenderAlphaRejectionFP": 1,
            "Caelum/MinimalCompositorVP": 5,
            "Caelum/DepthComposerFP_Dummy": 1,
            "Caelum/DepthComposerFP_DebugDepthRender": 1,
            "Caelum/DepthComposerFP_ExpGroundFog": 1,
            "Caelum/DepthComposerFP_SkyDomeHaze": 1,
            "Caelum/DepthComposerFP_SkyDomeHaze_ExpGroundFog": 1,
            "Caelum/PrecipitationVP": 1,
            "Caelum/PrecipitationFP": 1,
        }
        self.assertEqual(
            {name: references.count(name) for name in sorted(set(references))},
            expected_counts,
        )
        self.assertFalse(any(name.endswith(("/GL3Plus", "/D3D11")) for name in references))


if __name__ == "__main__":
    unittest.main()
