#!/usr/bin/env python3
"""Fail closed on the source-tree RTShader modern-only compatibility pack."""

from collections import Counter
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
RTSHADER = ROOT / "resources/rtshader"

FAMILY_SYMBOLS = {
    "FFPLib_Common": {
        "FFP_Add",
        "FFP_Assign",
        "FFP_Construct",
        "FFP_DotProduct",
        "FFP_Lerp",
        "FFP_Modulate",
        "FFP_Subtract",
    },
    "FFPLib_Fog": {
        "FFP_PixelFog_Depth",
        "FFP_PixelFog_Exp",
        "FFP_PixelFog_Exp2",
        "FFP_PixelFog_Linear",
        "FFP_VertexFog_Exp",
        "FFP_VertexFog_Exp2",
        "FFP_VertexFog_Linear",
    },
    "FFPLib_Lighting": {
        "FFP_Light_Directional_Diffuse",
        "FFP_Light_Directional_DiffuseSpecular",
        "FFP_Light_Point_Diffuse",
        "FFP_Light_Point_DiffuseSpecular",
        "FFP_Light_Spot_Diffuse",
        "FFP_Light_Spot_DiffuseSpecular",
    },
    "FFPLib_Texturing": {
        "FFP_AddSigned",
        "FFP_AddSmooth",
        "FFP_GenerateTexCoord_EnvMap_Normal",
        "FFP_GenerateTexCoord_EnvMap_Reflect",
        "FFP_GenerateTexCoord_EnvMap_Sphere",
        "FFP_GenerateTexCoord_Projection",
        "FFP_ModulateX2",
        "FFP_ModulateX4",
        "FFP_SampleTexture",
        "FFP_SampleTextureProj",
        "FFP_TransformTexCoord",
    },
    "FFPLib_Transform": {"FFP_Transform"},
    "SGXLib_IntegratedPSSM": {
        "SGX_ApplyShadowFactor_Diffuse",
        "SGX_ComputeShadowFactor_PSSM3",
        "SGX_CopyDepth",
        "SGX_ModulateScalar",
        "_SGX_ShadowPCF4",
    },
    "SGXLib_NormalMapLighting": {
        "SGX_ConstructTBNMatrix",
        "SGX_FetchNormal",
        "SGX_Generate_Parallax_Texcoord",
        "SGX_Light_Directional_Diffuse",
        "SGX_Light_Directional_DiffuseSpecular",
        "SGX_Light_Point_Diffuse",
        "SGX_Light_Point_DiffuseSpecular",
        "SGX_Light_Spot_Diffuse",
        "SGX_Light_Spot_DiffuseSpecular",
        "SGX_TransformNormal",
        "SGX_TransformPosition",
    },
    "SGXLib_PerPixelLighting": {
        "SGX_Light_Directional_Diffuse",
        "SGX_Light_Directional_DiffuseSpecular",
        "SGX_Light_Point_Diffuse",
        "SGX_Light_Point_DiffuseSpecular",
        "SGX_Light_Spot_Diffuse",
        "SGX_Light_Spot_DiffuseSpecular",
        "SGX_TransformNormal",
        "SGX_TransformPosition",
    },
    "SampleLib_ReflectionMap": {"SGX_ApplyReflectionMap"},
}

# These are the overload counts provided by the retired Cg surface. GLSL has
# two intentional extra conversion overloads, so modern sources may exceed
# these counts but may not remove any of the dependency entry points.
MINIMUM_OVERLOADS = {
    "FFPLib_Common": {
        "FFP_Add": 4,
        "FFP_Assign": 5,
        "FFP_Construct": 4,
        "FFP_DotProduct": 4,
        "FFP_Lerp": 5,
        "FFP_Modulate": 4,
        "FFP_Subtract": 4,
    },
    "FFPLib_Fog": {symbol: 1 for symbol in FAMILY_SYMBOLS["FFPLib_Fog"]},
    "FFPLib_Lighting": {
        symbol: 1 for symbol in FAMILY_SYMBOLS["FFPLib_Lighting"]
    },
    "FFPLib_Texturing": {
        "FFP_AddSigned": 4,
        "FFP_AddSmooth": 4,
        "FFP_GenerateTexCoord_EnvMap_Normal": 2,
        "FFP_GenerateTexCoord_EnvMap_Reflect": 2,
        "FFP_GenerateTexCoord_EnvMap_Sphere": 2,
        "FFP_GenerateTexCoord_Projection": 1,
        "FFP_ModulateX2": 4,
        "FFP_ModulateX4": 4,
        "FFP_SampleTexture": 4,
        "FFP_SampleTextureProj": 1,
        "FFP_TransformTexCoord": 2,
    },
    "FFPLib_Transform": {"FFP_Transform": 1},
    "SGXLib_IntegratedPSSM": {
        symbol: 1 for symbol in FAMILY_SYMBOLS["SGXLib_IntegratedPSSM"]
    },
    "SGXLib_NormalMapLighting": {
        **{
            symbol: 1
            for symbol in FAMILY_SYMBOLS["SGXLib_NormalMapLighting"]
        },
        "SGX_TransformNormal": 2,
    },
    "SGXLib_PerPixelLighting": {
        symbol: 1 for symbol in FAMILY_SYMBOLS["SGXLib_PerPixelLighting"]
    },
    "SampleLib_ReflectionMap": {"SGX_ApplyReflectionMap": 2},
}

FUNCTION_DEFINITION = re.compile(
    r"(?m)^\s*(?:void|float)\s+([A-Za-z_]\w*)\s*\("
)
LEGACY_TOKEN = re.compile(r"(?i)(?:\bcg\b|\.cg\b|\basm\b|\.asm\b)")


class RTShaderModernOnlyContractTests(unittest.TestCase):
    def test_exact_modern_only_file_closure(self) -> None:
        expected = {"RTShaderSystem.material"}
        for stem in FAMILY_SYMBOLS:
            expected.add(f"{stem}.glsl")
            expected.add(f"{stem}.hlsl")

        actual = {path.name for path in RTSHADER.iterdir() if path.is_file()}
        self.assertEqual(actual, expected)
        self.assertFalse(list(RTSHADER.rglob("*.cg")))
        self.assertFalse(list(RTSHADER.rglob("*.asm")))

        for path in sorted(RTSHADER.iterdir()):
            with self.subTest(path=path.name):
                self.assertTrue(path.is_file())
                self.assertFalse(path.is_symlink())
                self.assertGreater(path.stat().st_size, 0)

    def test_glsl_and_hlsl_preserve_the_rtshader_dependency_abi(self) -> None:
        for stem, expected_symbols in sorted(FAMILY_SYMBOLS.items()):
            for suffix in ("glsl", "hlsl"):
                with self.subTest(stem=stem, suffix=suffix):
                    source = (RTSHADER / f"{stem}.{suffix}").read_text(
                        encoding="utf-8"
                    )
                    definitions = Counter(FUNCTION_DEFINITION.findall(source))
                    self.assertEqual(set(definitions), expected_symbols)
                    for symbol, minimum in MINIMUM_OVERLOADS[stem].items():
                        self.assertGreaterEqual(definitions[symbol], minimum)
                    self.assertNotIn("void main", source)

    def test_surviving_sources_and_material_have_no_legacy_cg_route(self) -> None:
        for path in sorted(RTSHADER.iterdir()):
            with self.subTest(path=path.name):
                source = path.read_text(encoding="utf-8")
                self.assertIsNone(LEGACY_TOKEN.search(source))

        material = (RTSHADER / "RTShaderSystem.material").read_text(
            encoding="utf-8"
        )
        self.assertNotRegex(material, r"(?i)\bsource\s+\S+")

    def test_glsl_environment_mapping_applies_rotation_matrices(self) -> None:
        texturing = (RTSHADER / "FFPLib_Texturing.glsl").read_text(
            encoding="utf-8"
        )
        self.assertNotRegex(texturing, r"\(\s*mView\s*,")
        self.assertNotIn("mWorldIT * vec4(vNormal, 1.0)", texturing)
        self.assertNotRegex(
            texturing, r"mWorld\s*\*\s*vec4\(vNormal, 1\.0\)"
        )
        self.assertEqual(
            texturing.count("mat3(mWorldIT) * vNormal"), 4
        )
        self.assertEqual(texturing.count("mat3(mWorld) * vNormal"), 2)
        self.assertEqual(
            texturing.count("mat3(mView) * vWorldNormal"), 6
        )

    def test_live_ogre14_rtshader_route_is_the_pinned_package_media(self) -> None:
        cmake = (ROOT / "source/main/CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        app_context = (ROOT / "source/main/AppContext.cpp").read_text(
            encoding="utf-8"
        )
        runtime_cpp = "\n".join(
            path.read_text(encoding="utf-8", errors="replace")
            for path in (ROOT / "source/main").rglob("*.cpp")
        )

        # The source-tree folder still becomes rtshader.zip for compatibility.
        self.assertIn(
            'recursive_zip_folder("${CMAKE_SOURCE_DIR}/resources"', cmake
        )

        # OGRE14 does not consume that compatibility archive. Its live RTSS
        # resource group receives the pinned package's RTShaderLib directory.
        self.assertIn(
            '"${_ror_ogre14_media_root}/RTShaderLib"', cmake
        )
        self.assertIn(
            '"${RUNTIME_OUTPUT_DIRECTORY}/resources/ogre14/RTShaderLib"',
            cmake,
        )
        self.assertIn(
            'PathCombine(ogre14_media_path, "RTShaderLib")', app_context
        )
        self.assertIn(
            "resource_groups.addResourceLocation(\n"
            "            rtshader_lib_path,\n"
            '            "FileSystem",\n'
            "            Ogre::RGN_INTERNAL);",
            app_context,
        )
        self.assertNotRegex(
            runtime_cpp,
            r"AddResourcePack\s*\(\s*(?:ContentManager::)?"
            r"ResourcePack::RTSHADER\b",
        )


if __name__ == "__main__":
    unittest.main()
