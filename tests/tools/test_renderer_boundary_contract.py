#!/usr/bin/env python3
"""Static hygiene gate for the renderer-neutral public boundary."""

from __future__ import annotations

import json
import pathlib
import re
import unittest


REPOSITORY_ROOT = pathlib.Path(__file__).resolve().parents[2]
BOUNDARY_ROOT = REPOSITORY_ROOT / "source" / "main" / "gfx" / "render"
PROBE_ROOT = REPOSITORY_ROOT / "tools" / "ogre_next_probe"


def _cmake_set(source: str, variable: str) -> str:
    match = re.search(
        rf"set\(\s*{re.escape(variable)}\s+(.*?)\n\s*\)",
        source,
        re.DOTALL,
    )
    if match is None:
        raise AssertionError(f"missing CMake list {variable}")
    return match.group(1)


def _string_constant(source: str, name: str) -> str:
    match = re.search(
        rf"\b{re.escape(name)}\[\]\s*=\s*\"([^\"]+)\"",
        source,
        re.DOTALL,
    )
    if match is None:
        raise AssertionError(f"missing string constant {name}")
    return match.group(1)


class RendererBoundaryContractTests(unittest.TestCase):
    def test_code_has_no_renderer_sdk_includes_or_types(self) -> None:
        code_files = sorted(BOUNDARY_ROOT.glob("*.h")) + sorted(
            BOUNDARY_ROOT.glob("*.cpp")
        )
        self.assertTrue(code_files)
        renderer_include = re.compile(
            r"^\s*#\s*include\s*([<\"])([^>\"]*(?:Ogre|OGRE|Metal|d3d12|vulkan)[^>\"]*)[>\"]",
            re.MULTILINE,
        )
        forbidden = {
            "OGRE C++ type": re.compile(r"\bOgre::"),
            "platform graphics pointer": re.compile(
                r"\b(?:ID3D12\w+|MTL\w+|Vk\w+)\s*\*"
            ),
        }
        for path in code_files:
            text = path.read_text(encoding="utf-8")
            for match in renderer_include.finditer(text):
                is_local_header = (
                    match.group(1) == '"'
                    and (path.parent / match.group(2)).is_file()
                )
                self.assertTrue(
                    is_local_header,
                    f"{path}: renderer SDK include: {match.group(2)}",
                )
            for label, pattern in forbidden.items():
                self.assertIsNone(pattern.search(text), f"{path}: {label}")

    def test_quoted_public_includes_stay_inside_boundary(self) -> None:
        for path in sorted(BOUNDARY_ROOT.glob("*.h")):
            text = path.read_text(encoding="utf-8")
            for include in re.findall(r'^\s*#\s*include\s*"([^"]+)"', text, re.MULTILINE):
                self.assertNotIn("..", include, f"{path}: parent include escaped boundary")
                self.assertTrue(
                    (BOUNDARY_ROOT / include).is_file(),
                    f"{path}: quoted include is outside boundary: {include}",
                )

    def test_interfaces_and_fail_closed_proofs_are_present(self) -> None:
        frontend = (BOUNDARY_ROOT / "RendererFrontend.h").read_text(encoding="utf-8")
        for contract in (
            "class IRendererFrontend",
            "class NativeRenderInterop",
            "class INativeRayTracingBackend",
            "SynchronizeAssets",
            "RenderAssetDelta",
            "RasterGraphicsApi",
            "supported_outputs",
            "AcquireContext",
            "UpdateSurface",
            "ValidateFrontendSurfaceUpdate",
            "ValidateFrontendSurfaceTransition",
            "ValidateRenderFramePresentation",
            "ValidateNativeGeometryInteropProofSet",
            "ValidateGeometryLease",
            "ValidateInteropEvidence",
            "OUTSTANDING_LEASES",
            "owner/render thread",
            "native_ray_tracing_probe_passed = false",
            "native_ray_tracing_geometry_interop_ready = false",
            "geometry_interop_proven = false",
        ):
            self.assertIn(contract, frontend)

        asset_registry = (BOUNDARY_ROOT / "RenderAssetRegistry.h").read_text(
            encoding="utf-8"
        )
        for contract in (
            "class RenderAssetRegistry",
            "RenderAssetId",
            "RenderAssetReference",
            "BuildFullSnapshot",
            "full_snapshot",
            "tombstone",
        ):
            self.assertIn(contract, asset_registry)

    def test_direct_frontend_dispatch_is_shipped_without_transport_dependency(self) -> None:
        production_cmake = (
            REPOSITORY_ROOT / "source" / "main" / "CMakeLists.txt"
        ).read_text(encoding="utf-8")
        tests_cmake = (REPOSITORY_ROOT / "tests" / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        probe_cmake = (PROBE_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        umbrella = (BOUNDARY_ROOT / "RenderContracts.h").read_text(
            encoding="utf-8"
        )
        transport = (
            BOUNDARY_ROOT / "RendererFrontendTransportDispatcher.cpp"
        ).read_text(encoding="utf-8")
        direct = (BOUNDARY_ROOT / "RendererFrontendDirectDispatcher.cpp").read_text(
            encoding="utf-8"
        )

        for stem in (
            "RendererFrontendDirectDispatcher",
            "RendererFrontendPresentationPolicy",
        ):
            self.assertIn(
                f"gfx/render/{stem}.{{h,cpp}}",
                _cmake_set(production_cmake, "SOURCE_FILES"),
            )
            for variable in (
                "ROR_RENDER_CONTRACT_SOURCES",
                "ROR_RENDER_CONTRACT_STRICT_FP_SOURCES",
            ):
                self.assertIn(
                    f"gfx/render/{stem}.cpp",
                    _cmake_set(production_cmake, variable),
                )
            self.assertIn(
                f"source/main/gfx/render/{stem}.cpp", tests_cmake
            )
            self.assertIn(f"{stem}.cpp", probe_cmake)
            self.assertIn(f'#include "{stem}.h"', umbrella)

        self.assertIn("direct_dispatcher_.SynchronizeAssets", transport)
        self.assertIn("direct_dispatcher_.RenderScene", transport)
        self.assertIn("direct_dispatcher_.ResetSceneGeneration", transport)
        self.assertNotRegex(
            direct,
            r'#\s*include\s*[<"][^>"]*(?:Transport|Bridge|Channel)[^>"]*[>"]',
        )

    def test_numerical_references_are_shipping_strict_fp_sources(self) -> None:
        production_cmake = (
            REPOSITORY_ROOT / "source" / "main" / "CMakeLists.txt"
        ).read_text(encoding="utf-8")
        test_cmake = (REPOSITORY_ROOT / "tests" / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        references = sorted(BOUNDARY_ROOT.glob("*Reference.cpp"))
        self.assertTrue(references)
        for source in references:
            stem = source.stem
            self.assertIn(
                f"gfx/render/{stem}.{{h,cpp}}",
                production_cmake,
                f"{stem} is absent from the shipping source list",
            )
            self.assertGreaterEqual(
                production_cmake.count(f"gfx/render/{stem}.cpp"),
                2,
                f"{stem} must be in renderer-contract and strict-FP lists",
            )
            self.assertIn(
                f"source/main/gfx/render/{stem}.cpp",
                test_cmake,
                f"{stem} is absent from the portable contract library",
            )
            self.assertTrue(
                (
                    REPOSITORY_ROOT
                    / "tests"
                    / "gfx"
                    / "render"
                    / f"{stem}Tests.cpp"
                ).is_file(),
                f"{stem} has no focused C++ contract test",
            )

    def test_pbr_reference_is_shipped_and_kept_in_strict_fp_mode(self) -> None:
        main_cmake = (
            REPOSITORY_ROOT / "source" / "main" / "CMakeLists.txt"
        ).read_text(encoding="utf-8")
        tests_cmake = (REPOSITORY_ROOT / "tests" / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )

        self.assertIn(
            "gfx/render/PbrReference.{h,cpp}",
            _cmake_set(main_cmake, "SOURCE_FILES"),
        )
        for variable in (
            "ROR_RENDER_CONTRACT_SOURCES",
            "ROR_RENDER_CONTRACT_STRICT_FP_SOURCES",
        ):
            self.assertIn(
                "gfx/render/PbrReference.cpp", _cmake_set(main_cmake, variable)
            )
        strict_fp_start = main_cmake.index(
            "set(ROR_RENDER_CONTRACT_STRICT_FP_SOURCES"
        )
        strict_fp_end = main_cmake.index("# Authenticated input traces")
        strict_fp_block = main_cmake[strict_fp_start:strict_fp_end]
        for required in (
            "${ROR_RENDER_CONTRACT_STRICT_FP_SOURCES}",
            'COMPILE_OPTIONS "/fp:strict"',
            'COMPILE_OPTIONS "-fno-fast-math;-ffp-contract=off"',
        ):
            self.assertIn(required, strict_fp_block)

        self.assertIn(
            '"${ROR_REPOSITORY_ROOT}/source/main/gfx/render/PbrReference.cpp"',
            _cmake_set(tests_cmake, "ROR_RENDER_CONTRACT_SOURCES"),
        )
        self.assertRegex(
            tests_cmake,
            r"add_executable\(\s*ror_render_pbr_reference_tests\s+"
            r'"\$\{CMAKE_CURRENT_LIST_DIR\}/gfx/render/PbrReferenceTests\.cpp"',
        )
        self.assertIn(
            "ror_render_pbr_reference_tests",
            _cmake_set(tests_cmake, "ROR_RENDER_CONTRACT_TEST_TARGETS"),
        )
        strict_test_start = tests_cmake.index(
            "set(ROR_RENDER_CONTRACT_BUILD_TARGETS"
        )
        strict_test_end = tests_cmake.index(
            "# This test intentionally replaces global"
        )
        strict_test_block = tests_cmake[strict_test_start:strict_test_end]
        for required in (
            "${ROR_RENDER_CONTRACT_TEST_TARGETS}",
            "/fp:strict",
            "-fno-fast-math",
            "-ffp-contract=off",
            "crtfastmath.o",
            "target_link_options",
        ):
            self.assertIn(required, strict_test_block)
        self.assertIn(
            "ror_render_strict_fp_environment_tests",
            _cmake_set(tests_cmake, "ROR_RENDER_CONTRACT_TEST_TARGETS"),
        )
        strict_link_targets = _cmake_set(
            tests_cmake, "ROR_RENDER_CONTRACT_LINK_TARGETS"
        )
        for required in (
            "${ROR_RENDER_CONTRACT_TEST_TARGETS}",
            "ror_renderer_ogre14_game_host_session_tests",
            "ror_renderer_ogre_next_live_session_tests",
        ):
            self.assertIn(required, strict_link_targets)
        self.assertRegex(
            strict_test_block,
            r"foreach \(render_contract_test IN LISTS "
            r"ROR_RENDER_CONTRACT_LINK_TARGETS\)\s*"
            r"target_link_options\(\s*\$\{render_contract_test\}\s*"
            r"PRIVATE -fno-fast-math\s*\)",
        )
        self.assertRegex(
            tests_cmake,
            r"add_test\(\s*NAME\s+render_strict_fp_environment\s+"
            r"COMMAND\s+ror_render_strict_fp_environment_tests\s*\)",
        )
        self.assertTrue(
            (
                REPOSITORY_ROOT
                / "tests"
                / "gfx"
                / "render"
                / "StrictFloatingPointEnvironmentTests.cpp"
            ).is_file()
        )

    def test_reflection_runtime_and_receipt_are_shipping_strict_sources(self) -> None:
        main_cmake = (
            REPOSITORY_ROOT / "source" / "main" / "CMakeLists.txt"
        ).read_text(encoding="utf-8")
        tests_cmake = (REPOSITORY_ROOT / "tests" / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        for stem in (
            "ReflectionProbeRuntime",
            "ReflectionProbeCaptureReceipt",
        ):
            self.assertIn(
                f"gfx/render/{stem}.{{h,cpp}}",
                _cmake_set(main_cmake, "SOURCE_FILES"),
            )
            for variable in (
                "ROR_RENDER_CONTRACT_SOURCES",
                "ROR_RENDER_CONTRACT_STRICT_FP_SOURCES",
            ):
                self.assertIn(
                    f"gfx/render/{stem}.cpp", _cmake_set(main_cmake, variable)
                )
            self.assertIn(
                f'"${{ROR_REPOSITORY_ROOT}}/source/main/gfx/render/{stem}.cpp"',
                _cmake_set(tests_cmake, "ROR_RENDER_CONTRACT_SOURCES"),
            )
            self.assertTrue(
                (
                    REPOSITORY_ROOT
                    / "tests"
                    / "gfx"
                    / "render"
                    / f"{stem}Tests.cpp"
                ).is_file()
            )

        self.assertIn(
            "ror_render_reflection_probe_capture_receipt_tests",
            _cmake_set(tests_cmake, "ROR_RENDER_CONTRACT_TEST_TARGETS"),
        )
        self.assertRegex(
            tests_cmake,
            r"add_test\(\s*NAME\s+render_reflection_probe_capture_receipt\s+"
            r"COMMAND\s+ror_render_reflection_probe_capture_receipt_tests\s*\)",
        )

        runtime_header = (BOUNDARY_ROOT / "ReflectionProbeRuntime.h").read_text(
            encoding="utf-8"
        )
        receipt_header = (
            BOUNDARY_ROOT / "ReflectionProbeCaptureReceipt.h"
        ).read_text(encoding="utf-8")
        self.assertNotIn("ReflectionProbeCaptureCompletion", runtime_header)
        self.assertIn(
            "const std::vector<ReflectionProbeCaptureReceipt> &receipts",
            runtime_header,
        )
        self.assertIn("class ReflectionProbeCaptureReceipt final", receipt_header)
        self.assertIn("ReflectionProbeCaptureMeasurementResult", receipt_header)
        self.assertNotIn("native_execution_receipt", receipt_header)
        self.assertNotIn("ReflectionProbeCaptureTestAdapter", main_cmake)

    def test_pbr_reference_provenance_binds_canonical_lock_and_sources(self) -> None:
        canonical = json.loads(
            (PROBE_ROOT / "ogre-next.lock.json").read_text(encoding="utf-8")
        )
        pbr = json.loads(
            (PROBE_ROOT / "ogre-next-pbr-reference.lock.json").read_text(
                encoding="utf-8"
            )
        )
        header = (BOUNDARY_ROOT / "PbrReference.h").read_text(encoding="utf-8")

        self.assertEqual(pbr["canonical_dependency_lock"], "ogre-next.lock.json")
        self.assertEqual(pbr["ogre_next_commit"], canonical["commit"])
        self.assertEqual(
            _string_constant(header, "kPbrDirectReferenceOgreNextCommit"),
            canonical["commit"],
        )
        self.assertEqual(pbr["shader_profile"]["brdf"], "PbsBrdf::Default")
        self.assertEqual(pbr["shader_profile"]["precision_mode"], "full32")
        self.assertTrue(pbr["shader_profile"]["ggx_height_correlated"])
        self.assertFalse(pbr["shader_profile"]["fresnel_has_diffuse"])
        self.assertEqual(pbr["oracle_arithmetic"], "binary64")
        self.assertEqual(pbr["backend_relative_tolerance"], 0.01)

        expected = {
            "brdf_equations": (
                "Samples/Media/Hlms/Pbs/Any/Main/200.BRDFs_piece_ps.any",
                "kPbrDirectReferenceBrdfSourceSha256",
            ),
            "material_and_view_setup": (
                "Samples/Media/Hlms/Pbs/Any/Main/800.PixelShader_piece_ps.any",
                "kPbrDirectReferencePixelSourceSha256",
            ),
            "datablock_diffuse_upload": (
                "Components/Hlms/Pbs/src/OgreHlmsPbsDatablock.cpp",
                "kPbrDirectReferenceDatablockSourceSha256",
            ),
        }
        sources = {entry["role"]: entry for entry in pbr["sources"]}
        self.assertEqual(set(sources), set(expected))
        for role, (path, constant) in expected.items():
            self.assertEqual(sources[role]["path"], path)
            source_hash = sources[role]["sha256"]
            self.assertRegex(source_hash, r"^[0-9a-f]{64}$")
            self.assertEqual(_string_constant(header, constant), source_hash)

        tolerance = re.search(
            r"kPbrDirectReferenceBackendRelativeTolerance\s*=\s*([^;]+);",
            header,
        )
        self.assertIsNotNone(tolerance)
        self.assertEqual(
            float(tolerance.group(1)), pbr["backend_relative_tolerance"]
        )


if __name__ == "__main__":
    unittest.main()
