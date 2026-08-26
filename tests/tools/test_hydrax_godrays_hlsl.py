#!/usr/bin/env python3
"""Compiler-backed qualification for Hydrax's generated God Rays shaders.

This is source/compiler evidence only. It does not prove OGRE resource loading,
texture/sampler binding, a rendered frame, or playability.
"""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
HYDRAX_ROOT = REPOSITORY_ROOT / "source" / "main" / "gfx" / "hydrax"
GOD_RAYS_SOURCE = HYDRAX_ROOT / "GodRaysManager.cpp"
GOD_RAYS_HEADER = HYDRAX_ROOT / "GodRaysManager.h"
MATERIAL_MANAGER_SOURCE = HYDRAX_ROOT / "MaterialManager.cpp"
HLSL_HEADER = HYDRAX_ROOT / "GodRaysModernHlsl.h"
GLSL_HEADER = HYDRAX_ROOT / "GodRaysModernGlsl.h"
NATIVE_WORKFLOW = (
    REPOSITORY_ROOT / ".github/workflows/ogre-next-combined-native.yml"
)
TSAN_WORKFLOW = (
    REPOSITORY_ROOT / ".github/workflows/ogre-next-combined-tsan.yml"
)

EXTRACTOR_SOURCE = r'''
#include <fstream>
#include <string>

#include "GodRaysModernGlsl.h"
#include "GodRaysModernHlsl.h"

namespace
{
bool writeFile(const std::string& path, const char* payload)
{
    std::ofstream stream(path.c_str(), std::ios::binary);
    stream << payload;
    return stream.good();
}
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        return 2;
    }

    const std::string outputRoot(argv[1]);
    if (!writeFile(outputRoot + "/rays-vertex.hlsl",
                   Hydrax::ModernHlsl::godRaysVertexSource(false)) ||
        !writeFile(outputRoot + "/rays-intersections-vertex.hlsl",
                   Hydrax::ModernHlsl::godRaysVertexSource(true)) ||
        !writeFile(outputRoot + "/rays-green.hlsl",
                   Hydrax::ModernHlsl::godRaysFragmentSource(false, false)) ||
        !writeFile(outputRoot + "/rays-blue.hlsl",
                   Hydrax::ModernHlsl::godRaysFragmentSource(false, true)) ||
        !writeFile(outputRoot + "/rays-intersections-green.hlsl",
                   Hydrax::ModernHlsl::godRaysFragmentSource(true, false)) ||
        !writeFile(outputRoot + "/rays-intersections-blue.hlsl",
                   Hydrax::ModernHlsl::godRaysFragmentSource(true, true)) ||
        !writeFile(outputRoot + "/depth-vertex.hlsl",
                   Hydrax::ModernHlsl::godRaysDepthVertexSource()) ||
        !writeFile(outputRoot + "/depth-fragment.hlsl",
                   Hydrax::ModernHlsl::godRaysDepthFragmentSource()) ||
        !writeFile(outputRoot + "/rays-vertex.vert",
                   Hydrax::ModernGlsl::godRaysVertexSource(false)) ||
        !writeFile(outputRoot + "/rays-intersections-vertex.vert",
                   Hydrax::ModernGlsl::godRaysVertexSource(true)) ||
        !writeFile(outputRoot + "/rays-green.frag",
                   Hydrax::ModernGlsl::godRaysFragmentSource(false, false)) ||
        !writeFile(outputRoot + "/rays-blue.frag",
                   Hydrax::ModernGlsl::godRaysFragmentSource(false, true)) ||
        !writeFile(outputRoot + "/rays-intersections-green.frag",
                   Hydrax::ModernGlsl::godRaysFragmentSource(true, false)) ||
        !writeFile(outputRoot + "/rays-intersections-blue.frag",
                   Hydrax::ModernGlsl::godRaysFragmentSource(true, true)) ||
        !writeFile(outputRoot + "/depth-vertex.vert",
                   Hydrax::ModernGlsl::godRaysDepthVertexSource()) ||
        !writeFile(outputRoot + "/depth-fragment.frag",
                   Hydrax::ModernGlsl::godRaysDepthFragmentSource()))
    {
        return 3;
    }
    return 0;
}
'''

HLSL_CASES = (
    ("rays-vertex.hlsl", "vert", "main_vp"),
    ("rays-intersections-vertex.hlsl", "vert", "main_vp"),
    ("rays-green.hlsl", "frag", "main_fp"),
    ("rays-blue.hlsl", "frag", "main_fp"),
    ("rays-intersections-green.hlsl", "frag", "main_fp"),
    ("rays-intersections-blue.hlsl", "frag", "main_fp"),
    ("depth-vertex.hlsl", "vert", "main_vp"),
    ("depth-fragment.hlsl", "frag", "main_fp"),
)

GLSL_CASES = (
    ("rays-vertex.vert", "vert"),
    ("rays-intersections-vertex.vert", "vert"),
    ("rays-green.frag", "frag"),
    ("rays-blue.frag", "frag"),
    ("rays-intersections-green.frag", "frag"),
    ("rays-intersections-blue.frag", "frag"),
    ("depth-vertex.vert", "vert"),
    ("depth-fragment.frag", "frag"),
)

GLSL_LINK_CASES = (
    ("rays-vertex.vert", "rays-green.frag"),
    ("rays-vertex.vert", "rays-blue.frag"),
    ("rays-intersections-vertex.vert", "rays-intersections-green.frag"),
    ("rays-intersections-vertex.vert", "rays-intersections-blue.frag"),
    ("depth-vertex.vert", "depth-fragment.frag"),
)

ALL_CASE_NAMES = tuple(name for name, _, _ in HLSL_CASES) + tuple(
    name for name, _ in GLSL_CASES
)


def _sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


class HydraxGodRaysModernShaderTests(unittest.TestCase):
    def _extract(self, optimization: str) -> dict[str, bytes]:
        compiler = os.environ.get("CXX") or shutil.which("c++")
        self.assertIsNotNone(compiler, "a native C++ compiler is required")

        with tempfile.TemporaryDirectory(
            prefix="ror-hydrax-godrays-extractor-"
        ) as temporary:
            root = Path(temporary)
            source = root / "extract.cpp"
            executable = root / "extract"
            source.write_text(EXTRACTOR_SOURCE, encoding="utf-8")
            compiled = subprocess.run(
                [
                    str(compiler),
                    "-std=c++11",
                    optimization,
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(HYDRAX_ROOT),
                    str(source),
                    "-o",
                    str(executable),
                ],
                check=False,
                capture_output=True,
                text=True,
                timeout=60,
            )
            self.assertEqual(
                compiled.returncode,
                0,
                compiled.stdout + compiled.stderr,
            )
            extracted = subprocess.run(
                [str(executable), str(root)],
                check=False,
                capture_output=True,
                text=True,
                timeout=10,
            )
            self.assertEqual(
                extracted.returncode,
                0,
                extracted.stdout + extracted.stderr,
            )
            outputs = {name: (root / name).read_bytes() for name in ALL_CASE_NAMES}
            for name, payload in outputs.items():
                self.assertTrue(payload, f"native extractor produced empty {name}")
            return outputs

    def test_native_o0_and_o2_extractors_emit_identical_exact_sources(self) -> None:
        unoptimized = self._extract("-O0")
        optimized = self._extract("-O2")
        self.assertEqual(
            {name: _sha256(payload) for name, payload in unoptimized.items()},
            {name: _sha256(payload) for name, payload in optimized.items()},
        )

    def test_runtime_consumes_shared_modern_sources(self) -> None:
        runtime = GOD_RAYS_SOURCE.read_text(encoding="utf-8")
        runtime_header = GOD_RAYS_HEADER.read_text(encoding="utf-8")
        hlsl_header = HLSL_HEADER.read_text(encoding="utf-8")
        glsl_header = GLSL_HEADER.read_text(encoding="utf-8")

        for symbol in (
            "godRaysVertexSource",
            "godRaysFragmentSource",
            "godRaysDepthVertexSource",
            "godRaysDepthFragmentSource",
        ):
            self.assertIn(f"ModernHlsl::{symbol}", runtime)
            self.assertIn(f"ModernGlsl::{symbol}", runtime)
        self.assertIn('#include "GodRaysModernGlsl.h"', runtime)
        self.assertEqual(
            runtime.count(
                'const Ogre::String HlslTargets[2] = {"vs_4_0", "ps_4_0"}'
            ),
            2,
        )
        self.assertEqual(runtime.count("GpuProgramsData,\n\t\t\t\tHlslTargets"), 2)
        self.assertEqual(
            runtime.count(
                "GpuProgramsCreated = "
                "mMaterialManager->fillGpuProgramsToPass("
            ),
            4,
        )
        self.assertEqual(runtime.count("if (!GpuProgramsCreated)"), 2)
        self.assertIn("bool _createMaterials", runtime_header)
        self.assertIn("if (!_createMaterials(HC))", runtime)
        self.assertLess(
            runtime.index("if (!_createMaterials(HC))"),
            runtime.index("_createGodRays();"),
        )
        failure_blocks = runtime.split("if (!GpuProgramsCreated)")[1:]
        self.assertEqual(len(failure_blocks), 2)
        for block in failure_blocks:
            closing_brace = block.index("\n\t\t}")
            self.assertIn("return false;", block[:closing_brace])
        self.assertEqual(
            runtime.count(
                'VP_Parameters->setNamedAutoConstant("uWorldViewProj"'
            ),
            3,
        )
        self.assertNotIn(
            "mHydrax->getShaderMode() != MaterialManager::SM_GLSL",
            runtime,
        )
        for compatibility_token in (
            "gl_Vertex",
            "gl_ModelViewProjectionMatrix",
            "gl_FragColor",
            "texture2D",
            "varying",
        ):
            self.assertNotIn(compatibility_token, runtime)

        material_manager = MATERIAL_MANAGER_SOURCE.read_text(encoding="utf-8")
        self.assertIn(
            "HLGpuProgram->hasCompileError() || !HLGpuProgram->isSupported()",
            material_manager,
        )
        self.assertEqual(
            material_manager.count(
                "removeGpuProgramIfPresent(GpuProgramNames[0]);"
            ),
            2,
        )
        rejection = material_manager.index("HLGpuProgram->hasCompileError()")
        cleanup = material_manager.index("removeGpuProgramIfPresent(Name);", rejection)
        failure_return = material_manager.index("return false;", cleanup)
        success_return = material_manager.index("return true;", rejection)
        self.assertLess(rejection, cleanup)
        self.assertLess(cleanup, failure_return)
        self.assertLess(failure_return, success_return)
        for legacy_token in (
            "tex2D(",
            "sampler2D",
            ": COLOR",
            "out float4 oPosition : POSITION",
        ):
            self.assertNotIn(legacy_token, hlsl_header)
        self.assertIn(": SV_Position", hlsl_header)
        self.assertIn(": SV_Target", hlsl_header)
        self.assertIn("Texture2D uDepthMap : register(t0)", hlsl_header)
        self.assertIn(
            "SamplerState uDepthMapSampler : register(s0)",
            hlsl_header,
        )

        self.assertEqual(glsl_header.count("#version 330 core"), 8)
        for compatibility_token in (
            "gl_Vertex",
            "gl_ModelViewProjectionMatrix",
            "gl_FragColor",
            "texture2D",
            "varying",
        ):
            self.assertNotIn(compatibility_token, glsl_header)
        self.assertIn("layout(location = 0) in vec4 vertex;", glsl_header)
        self.assertIn(
            "layout(location = 0) out vec4 fragmentColor;",
            glsl_header,
        )
        self.assertIn("texture(uDepthMap, depthUv).r", glsl_header)
        self.assertEqual(
            glsl_header.count("gl_Position = uWorldViewProj * vertex;"),
            3,
        )

    def test_native_and_tsan_workflows_compile_the_exact_sources(self) -> None:
        test_path = "tests/tools/test_hydrax_godrays_hlsl.py"
        native = NATIVE_WORKFLOW.read_text(encoding="utf-8")
        tsan = TSAN_WORKFLOW.read_text(encoding="utf-8")

        self.assertEqual(native.count(f"      - {test_path}\n"), 2)
        self.assertEqual(tsan.count(f"      - {test_path}\n"), 1)
        compiler_prefix = (
            'ROR_HYDRAX_GLSLANG="$glslang_validator" CXX=g++-11 \\\n'
            "            "
        )
        for workflow in (native, tsan):
            self.assertIn(
                compiler_prefix + f"python {test_path}",
                workflow,
            )
            self.assertIn(
                compiler_prefix + f"python -O {test_path}",
                workflow,
            )

    def test_exact_extracted_sources_compile_and_hostile_mutation_fails(self) -> None:
        compiler_text = os.environ.get("ROR_HYDRAX_GLSLANG")
        if not compiler_text:
            self.skipTest("ROR_HYDRAX_GLSLANG does not name a real compiler")
        compiler = Path(compiler_text).expanduser().absolute()
        self.assertTrue(compiler.is_file(), f"missing compiler: {compiler}")
        self.assertFalse(compiler.is_symlink(), "compiler path must not be a symlink")
        self.assertTrue(os.access(compiler, os.X_OK), "compiler is not executable")

        extracted = self._extract("-O2")
        with tempfile.TemporaryDirectory(
            prefix="ror-hydrax-godrays-compile-"
        ) as temporary:
            root = Path(temporary)
            for name, stage, entry_point in HLSL_CASES:
                source = root / name
                output = root / f"{name}.spv"
                source.write_bytes(extracted[name])
                result = subprocess.run(
                    [
                        str(compiler),
                        "-D",
                        "-V",
                        "--target-env",
                        "vulkan1.1",
                        "-S",
                        stage,
                        "-e",
                        entry_point,
                        str(source),
                        "-o",
                        str(output),
                    ],
                    check=False,
                    capture_output=True,
                    text=True,
                    timeout=60,
                )
                self.assertEqual(
                    result.returncode,
                    0,
                    result.stdout + result.stderr,
                )
                self.assertTrue(output.is_file())
                self.assertGreater(output.stat().st_size, 0)

            for name, stage in GLSL_CASES:
                source = root / name
                output = root / f"{name}.spv"
                source.write_bytes(extracted[name])
                result = subprocess.run(
                    [
                        str(compiler),
                        "-G",
                        "--target-env",
                        "opengl",
                        "--auto-map-bindings",
                        "--auto-map-locations",
                        "-S",
                        stage,
                        str(source),
                        "-o",
                        str(output),
                    ],
                    check=False,
                    capture_output=True,
                    text=True,
                    timeout=60,
                )
                self.assertEqual(
                    result.returncode,
                    0,
                    result.stdout + result.stderr,
                )
                self.assertTrue(output.is_file())
                self.assertGreater(output.stat().st_size, 0)

            for vertex_name, fragment_name in GLSL_LINK_CASES:
                output = root / f"{vertex_name}-{fragment_name}.spv"
                result = subprocess.run(
                    [
                        str(compiler),
                        "-G",
                        "--target-env",
                        "opengl",
                        "--auto-map-bindings",
                        "--auto-map-locations",
                        "-l",
                        str(root / vertex_name),
                        str(root / fragment_name),
                        "-o",
                        str(output),
                    ],
                    check=False,
                    capture_output=True,
                    text=True,
                    timeout=60,
                )
                self.assertEqual(
                    result.returncode,
                    0,
                    result.stdout + result.stderr,
                )
                self.assertTrue(output.is_file())
                self.assertGreater(output.stat().st_size, 0)

            hostile_source = root / "hostile.hlsl"
            hostile_output = root / "hostile.spv"
            hostile_payload = extracted["rays-intersections-green.hlsl"].replace(
                b".Sample(", b".SampleBROKEN(", 1
            )
            self.assertNotEqual(
                hostile_payload,
                extracted["rays-intersections-green.hlsl"],
            )
            hostile_source.write_bytes(hostile_payload)
            hostile = subprocess.run(
                [
                    str(compiler),
                    "-D",
                    "-V",
                    "--target-env",
                    "vulkan1.1",
                    "-S",
                    "frag",
                    "-e",
                    "main_fp",
                    str(hostile_source),
                    "-o",
                    str(hostile_output),
                ],
                check=False,
                capture_output=True,
                text=True,
                timeout=60,
            )
            self.assertNotEqual(hostile.returncode, 0)
            self.assertFalse(hostile_output.exists())

            hostile_glsl_source = root / "hostile.frag"
            hostile_glsl_output = root / "hostile-glsl.spv"
            hostile_glsl_payload = extracted[
                "rays-intersections-green.frag"
            ].replace(b"texture(uDepthMap", b"textureBROKEN(uDepthMap", 1)
            self.assertNotEqual(
                hostile_glsl_payload,
                extracted["rays-intersections-green.frag"],
            )
            hostile_glsl_source.write_bytes(hostile_glsl_payload)
            hostile_glsl = subprocess.run(
                [
                    str(compiler),
                    "-G",
                    "--target-env",
                    "opengl",
                    "--auto-map-bindings",
                    "--auto-map-locations",
                    "-S",
                    "frag",
                    str(hostile_glsl_source),
                    "-o",
                    str(hostile_glsl_output),
                ],
                check=False,
                capture_output=True,
                text=True,
                timeout=60,
            )
            self.assertNotEqual(hostile_glsl.returncode, 0)
            self.assertFalse(hostile_glsl_output.exists())


if __name__ == "__main__":
    unittest.main()
