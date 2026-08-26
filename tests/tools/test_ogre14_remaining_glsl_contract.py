#!/usr/bin/env python3
"""Fail closed on the last packaged OGRE14 GLSL compatibility sources."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
OGRE_CORE = ROOT / "resources/OgreCore"
RTSHADER = ROOT / "resources/rtshader"
NATIVE_WORKFLOW = (
    ROOT / ".github/workflows/ogre-next-combined-native.yml"
).read_text(encoding="utf-8")
TSAN_WORKFLOW = (
    ROOT / ".github/workflows/ogre-next-combined-tsan.yml"
).read_text(encoding="utf-8")

RTSS_TEXTURE_CALLS = {
    "FFPLib_Common.glsl": 0,
    "FFPLib_Fog.glsl": 0,
    "FFPLib_Lighting.glsl": 0,
    "FFPLib_Texturing.glsl": 6,
    "FFPLib_Transform.glsl": 0,
    "SGXLib_IntegratedPSSM.glsl": 4,
    "SGXLib_NormalMapLighting.glsl": 2,
    "SGXLib_PerPixelLighting.glsl": 0,
    "SampleLib_ReflectionMap.glsl": 4,
}
COMPATIBILITY_TOKEN = re.compile(
    r"\b(?:attribute|varying|texture1D|texture2D|texture3D|textureCube|"
    r"texture2DArray|texture2DProj|texture2DLod|textureCubeLod|shadow2D|"
    r"ftransform|gl_Vertex|gl_Normal|gl_Color|gl_SecondaryColor|"
    r"gl_MultiTexCoord[0-7]|gl_FogCoord|gl_ModelViewMatrix|"
    r"gl_ProjectionMatrix|gl_ModelViewProjectionMatrix|gl_TextureMatrix|"
    r"gl_NormalMatrix|gl_FrontColor|gl_BackColor|gl_FrontSecondaryColor|"
    r"gl_BackSecondaryColor|gl_TexCoord|gl_FogFragCoord|gl_FragColor|"
    r"gl_FragData)\b"
)


def program_block(script: str, declaration: str) -> str:
    start = script.index(declaration)
    open_brace = script.index("{", start + len(declaration))
    depth = 0
    for offset, character in enumerate(script[open_brace:], start=open_brace):
        if character == "{":
            depth += 1
        elif character == "}":
            depth -= 1
            if depth == 0:
                return script[start : offset + 1]
    raise AssertionError(f"unclosed program block: {declaration}")


class Ogre14RemainingGlslContractTests(unittest.TestCase):
    def test_complete_desktop_shader_corpus_has_no_compatibility_tokens(self) -> None:
        desktop_extensions = {".glsl", ".vertex", ".fragment"}
        desktop_sources = sorted(
            path
            for path in (ROOT / "resources").rglob("*")
            if path.is_file() and path.suffix in desktop_extensions
        )
        self.assertTrue(desktop_sources)

        versionless = set()
        for path in desktop_sources:
            with self.subTest(path=path.relative_to(ROOT)):
                self.assertFalse(path.is_symlink())
                source = path.read_text(encoding="utf-8")
                self.assertIsNone(COMPATIBILITY_TOKEN.search(source))
                versions = re.findall(
                    r"(?m)^\s*#version\s+([0-9]+)\b", source
                )
                self.assertLessEqual(len(versions), 1)
                if versions:
                    self.assertGreaterEqual(int(versions[0]), 130)
                else:
                    versionless.add(path.relative_to(ROOT).as_posix())

        self.assertEqual(
            versionless,
            {
                f"resources/rtshader/{filename}"
                for filename in RTSS_TEXTURE_CALLS
            },
        )

    def test_stdquad_is_a_bound_core_glsl150_program(self) -> None:
        shader_path = OGRE_CORE / "StdQuad_vp.glsl"
        program_path = OGRE_CORE / "StdQuad_vp.program"
        self.assertFalse(shader_path.is_symlink())
        self.assertFalse(program_path.is_symlink())

        shader = shader_path.read_text(encoding="utf-8")
        self.assertTrue(shader.startswith("#version 150\n"))
        self.assertEqual(shader.count("#version 150"), 1)
        self.assertIsNone(COMPATIBILITY_TOKEN.search(shader))
        for contract in (
            "in vec4 vertex;",
            "out gl_PerVertex { vec4 gl_Position; };",
            "out vec2 uv;",
            "uniform mat4 worldViewProj;",
            "gl_Position = worldViewProj * vertex;",
            "vec2 inPos = sign(vertex.xy);",
        ):
            with self.subTest(contract=contract):
                self.assertEqual(shader.count(contract), 1)

        script = program_path.read_text(encoding="utf-8")
        glsl = program_block(
            script,
            "vertex_program Ogre/Compositor/StdQuad_GLSL_vp glsl",
        )
        self.assertEqual(glsl.count("source StdQuad_vp.glsl"), 1)
        self.assertEqual(
            glsl.count(
                "param_named_auto worldViewProj worldviewproj_matrix"
            ),
            1,
        )

        unified = program_block(
            script,
            "vertex_program Ogre/Compositor/StdQuad_vp unified",
        )
        delegates = re.findall(r"^\s*delegate\s+(\S+)", unified, re.MULTILINE)
        self.assertEqual(
            delegates,
            [
                "Ogre/Compositor/StdQuad_HLSL_vp",
                "Ogre/Compositor/StdQuad_GLSL_vp",
                "Ogre/Compositor/StdQuad_GLSLES_vp",
                "Ogre/Compositor/StdQuad_Cg_vp",
            ],
        )

    def test_rtss_dependencies_are_versionless_core_fragments(self) -> None:
        ownership = (
            "RTSS dependency fragment: the generated program owns the GLSL "
            "version."
        )
        self.assertEqual(
            {path.name for path in RTSHADER.glob("*.glsl")},
            set(RTSS_TEXTURE_CALLS),
        )
        for filename, texture_calls in sorted(RTSS_TEXTURE_CALLS.items()):
            with self.subTest(filename=filename):
                path = RTSHADER / filename
                self.assertFalse(path.is_symlink())
                source = path.read_text(encoding="utf-8")
                self.assertNotRegex(source, r"(?m)^\s*#version\b")
                self.assertNotIn("void main", source)
                self.assertEqual(source.count(ownership), 1)
                self.assertIsNone(COMPATIBILITY_TOKEN.search(source))
                self.assertEqual(
                    len(re.findall(r"\btexture\s*\(", source)),
                    texture_calls,
                )

    def test_intentional_gles100_source_is_outside_the_desktop_corpus(self) -> None:
        gles_path = OGRE_CORE / "StdQuad_vp.glsles"
        self.assertFalse(gles_path.is_symlink())
        gles = gles_path.read_text(encoding="utf-8")
        self.assertTrue(gles.startswith("#version 100\n"))

        script = (OGRE_CORE / "StdQuad_vp.program").read_text(
            encoding="utf-8"
        )
        gles_program = program_block(
            script,
            "vertex_program Ogre/Compositor/StdQuad_GLSLES_vp glsles",
        )
        self.assertEqual(gles_program.count("source StdQuad_vp.glsles"), 1)
        self.assertNotIn(gles_path.name, RTSS_TEXTURE_CALLS)

    def test_repo_rtss_pack_is_not_mistaken_for_live_ogre14_media(self) -> None:
        cmake = (ROOT / "source/main/CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        app_context = (ROOT / "source/main/AppContext.cpp").read_text(
            encoding="utf-8"
        )
        content_manager = (
            ROOT / "source/main/resources/ContentManager.cpp"
        ).read_text(encoding="utf-8")
        runtime_cpp = "\n".join(
            path.read_text(encoding="utf-8", errors="replace")
            for path in (ROOT / "source/main").rglob("*.cpp")
        )

        # The source-tree pack is still emitted for compatibility, but the
        # current OGRE14 RTSS consumes exact package media from OgreInternal.
        self.assertIn(
            'recursive_zip_folder("${CMAKE_SOURCE_DIR}/resources"', cmake
        )
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
            "DECLARE_RESOURCE_PACK( RTSHADER,", content_manager
        )
        self.assertNotRegex(
            runtime_cpp,
            r"AddResourcePack\s*\(\s*(?:ContentManager::)?"
            r"ResourcePack::RTSHADER\b",
        )

    def test_native_and_tsan_workflows_trigger_and_run_the_contract(self) -> None:
        test_path = "tests/tools/test_ogre14_remaining_glsl_contract.py"
        stdquad_program_gate = (
            "Program 'Ogre/Compositor/StdQuad_[^']+' is not supported"
        )
        bash_stdquad_script_gate = (
            r"Error: ScriptCompiler .*StdQuad_vp\\.program"
        )
        windows_stdquad_script_gate = (
            r"Error: ScriptCompiler .*StdQuad_vp\.program"
        )

        for path in (
            "resources/OgreCore/StdQuad_vp.glsl",
            "resources/OgreCore/StdQuad_vp.program",
            "resources/rtshader/**",
            test_path,
        ):
            with self.subTest(workflow="native", path=path):
                self.assertEqual(NATIVE_WORKFLOW.count(f"- {path}"), 2)
            with self.subTest(workflow="tsan", path=path):
                self.assertEqual(TSAN_WORKFLOW.count(f"- {path}"), 1)

        self.assertEqual(
            NATIVE_WORKFLOW.count(f"python {test_path}"), 2
        )
        self.assertEqual(
            NATIVE_WORKFLOW.count(f"python -O {test_path}"), 2
        )
        self.assertEqual(
            NATIVE_WORKFLOW.count(f"@('{test_path}')"), 1
        )
        self.assertEqual(
            NATIVE_WORKFLOW.count(f"@('-O', '{test_path}')"), 1
        )
        self.assertEqual(
            NATIVE_WORKFLOW.count(stdquad_program_gate), 2
        )
        self.assertEqual(
            NATIVE_WORKFLOW.count(bash_stdquad_script_gate), 2
        )
        self.assertEqual(
            NATIVE_WORKFLOW.count(windows_stdquad_script_gate), 1
        )

        self.assertEqual(TSAN_WORKFLOW.count(f"python {test_path}"), 1)
        self.assertEqual(TSAN_WORKFLOW.count(f"python -O {test_path}"), 1)
        self.assertEqual(TSAN_WORKFLOW.count(stdquad_program_gate), 1)
        self.assertEqual(TSAN_WORKFLOW.count(bash_stdquad_script_gate), 1)


if __name__ == "__main__":
    unittest.main()
