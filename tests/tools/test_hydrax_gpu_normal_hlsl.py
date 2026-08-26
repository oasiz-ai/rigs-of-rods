#!/usr/bin/env python3
"""Executable qualification for Hydrax's modern GPU-normal-map HLSL subset.

The tests compile and run a native C++ extractor against the same header used
by Perlin.cpp and FFT.cpp.  When ``ROR_HYDRAX_GLSLANG`` names an executable
glslangValidator, the exact extracted bytes are compiled and a hostile source
mutation must fail.  This is source/compiler evidence only: it does not prove
OGRE resource loading, texture/sampler binding, a rendered frame, or playability.
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
PERLIN_SOURCE = HYDRAX_ROOT / "Perlin.cpp"
FFT_SOURCE = HYDRAX_ROOT / "FFT.cpp"
MATERIAL_MANAGER_SOURCE = HYDRAX_ROOT / "MaterialManager.cpp"
NATIVE_WORKFLOW = (
    REPOSITORY_ROOT / ".github/workflows/ogre-next-combined-native.yml"
)
TSAN_WORKFLOW = (
    REPOSITORY_ROOT / ".github/workflows/ogre-next-combined-tsan.yml"
)

EXTRACTOR_SOURCE = r'''
#include <fstream>
#include <string>

#include "GpuNormalMapModernHlsl.h"

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
    if (!writeFile(outputRoot + "/vertex.hlsl", Hydrax::ModernHlsl::gpuNormalMapVertexSource()) ||
        !writeFile(outputRoot + "/perlin.hlsl", Hydrax::ModernHlsl::perlinGpuNormalMapFragmentSource()) ||
        !writeFile(outputRoot + "/fft.hlsl", Hydrax::ModernHlsl::fftGpuNormalMapFragmentSource()) ||
        !writeFile(outputRoot + "/vertex-target.txt", Hydrax::ModernHlsl::vertexTarget()) ||
        !writeFile(outputRoot + "/fragment-target.txt", Hydrax::ModernHlsl::fragmentTarget()))
    {
        return 3;
    }
    return 0;
}
'''


def _sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


class HydraxGpuNormalMapModernHlslTests(unittest.TestCase):
    def _extract(self, optimization: str) -> dict[str, bytes]:
        compiler = os.environ.get("CXX") or shutil.which("c++")
        self.assertIsNotNone(compiler, "a native C++ compiler is required")

        with tempfile.TemporaryDirectory(prefix="ror-hydrax-hlsl-extractor-") as temporary:
            root = Path(temporary)
            extractor_source = root / "extract.cpp"
            extractor_binary = root / "extract"
            extractor_source.write_text(EXTRACTOR_SOURCE, encoding="utf-8")

            compile_result = subprocess.run(
                [
                    str(compiler),
                    "-std=c++11",
                    optimization,
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(HYDRAX_ROOT),
                    str(extractor_source),
                    "-o",
                    str(extractor_binary),
                ],
                check=False,
                capture_output=True,
                text=True,
                timeout=60,
            )
            self.assertEqual(
                compile_result.returncode,
                0,
                compile_result.stdout + compile_result.stderr,
            )

            run_result = subprocess.run(
                [str(extractor_binary), str(root)],
                check=False,
                capture_output=True,
                text=True,
                timeout=10,
            )
            self.assertEqual(
                run_result.returncode,
                0,
                run_result.stdout + run_result.stderr,
            )

            names = (
                "vertex.hlsl",
                "perlin.hlsl",
                "fft.hlsl",
                "vertex-target.txt",
                "fragment-target.txt",
            )
            outputs = {name: (root / name).read_bytes() for name in names}
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
        self.assertEqual(unoptimized["vertex-target.txt"], b"vs_4_0")
        self.assertEqual(unoptimized["fragment-target.txt"], b"ps_4_0")

    def test_runtime_consumers_use_the_shared_sources_and_explicit_profiles(self) -> None:
        perlin = PERLIN_SOURCE.read_text(encoding="utf-8")
        fft = FFT_SOURCE.read_text(encoding="utf-8")
        manager = MATERIAL_MANAGER_SOURCE.read_text(encoding="latin-1")

        self.assertIn("ModernHlsl::gpuNormalMapVertexSource()", perlin)
        self.assertIn("ModernHlsl::perlinGpuNormalMapFragmentSource()", perlin)
        self.assertIn("ModernHlsl::gpuNormalMapVertexSource()", fft)
        self.assertIn("ModernHlsl::fftGpuNormalMapFragmentSource()", fft)
        self.assertIn("GpuProgramsData, HlslTargets", perlin)
        self.assertIn("GpuProgramsData, HlslTargets", fft)
        self.assertIn('HlslTargets[0] != "vs_4_0"', manager)
        self.assertIn('HlslTargets[1] != "ps_4_0"', manager)

        for source in (perlin, fft):
            self.assertNotIn("tex2D(", source)
            self.assertNotIn("sampler2D", source)
            self.assertNotIn(": COLOR", source)
            self.assertNotIn(": POSITION,\\n", source)

    def test_linux_native_and_tsan_gates_compile_the_exact_generated_sources(self) -> None:
        native = NATIVE_WORKFLOW.read_text(encoding="utf-8")
        tsan = TSAN_WORKFLOW.read_text(encoding="utf-8")
        test_path = "tests/tools/test_hydrax_gpu_normal_hlsl.py"

        self.assertEqual(native.count(f"      - {test_path}\n"), 2)
        self.assertEqual(tsan.count(f"      - {test_path}\n"), 1)
        self.assertEqual(tsan.count("      - source/main/gfx/hydrax/**\n"), 1)
        for workflow in (native, tsan):
            self.assertEqual(
                workflow.count(
                    'ROR_HYDRAX_GLSLANG="$glslang_validator" CXX=g++-11'
                ),
                2,
            )
            self.assertIn(
                "python tests/tools/test_hydrax_gpu_normal_hlsl.py",
                workflow,
            )
            self.assertIn(
                "python -O tests/tools/test_hydrax_gpu_normal_hlsl.py",
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
        cases = (
            ("vertex", "vert", "main_vp"),
            ("perlin", "frag", "main_fp"),
            ("fft", "frag", "main_fp"),
        )
        with tempfile.TemporaryDirectory(prefix="ror-hydrax-hlsl-compile-") as temporary:
            root = Path(temporary)
            for name, stage, entry_point in cases:
                source_path = root / f"{name}.hlsl"
                object_path = root / f"{name}.spv"
                source_path.write_bytes(extracted[f"{name}.hlsl"])
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
                        str(source_path),
                        "-o",
                        str(object_path),
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
                self.assertTrue(object_path.is_file())
                self.assertGreater(object_path.stat().st_size, 0)

            hostile_source = root / "hostile-perlin.hlsl"
            hostile_object = root / "hostile-perlin.spv"
            hostile_payload = extracted["perlin.hlsl"].replace(
                b".Sample(", b".SampleBROKEN(", 1
            )
            self.assertNotEqual(hostile_payload, extracted["perlin.hlsl"])
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
                    str(hostile_object),
                ],
                check=False,
                capture_output=True,
                text=True,
                timeout=60,
            )
            self.assertNotEqual(hostile.returncode, 0)
            self.assertFalse(hostile_object.exists())


if __name__ == "__main__":
    unittest.main()
