#!/usr/bin/env python3
"""Offline contract for the probe-only real RoR-OgreNext bootstrap child."""

from __future__ import annotations

from pathlib import Path
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
PROBE_ROOT = REPOSITORY_ROOT / "tools" / "ogre_next_probe"
ENTRYPOINT_PATH = (
    REPOSITORY_ROOT
    / "source"
    / "main"
    / "system"
    / "RendererOgreNextChildMain.cpp"
)
SELF_PATH = "tests/tools/test_ogre_next_child_runtime_contract.py"


class OgreNextChildRuntimeContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.entrypoint = ENTRYPOINT_PATH.read_text(encoding="utf-8")
        cls.cmake = (PROBE_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        cls.config = (
            PROBE_ROOT / "renderer_ogre_next_child_config.h.in"
        ).read_text(encoding="utf-8")
        cls.build_contract = (
            PROBE_ROOT / "ogre_next_build_contract.json.in"
        ).read_text(encoding="utf-8")
        cls.workflow = (
            REPOSITORY_ROOT / ".github/workflows/ogre-next-probe.yml"
        ).read_text(encoding="utf-8")

    def test_native_entrypoints_decode_real_argv(self) -> None:
        for token in (
            "int main(int argc, char *argv[])",
            "int WINAPI wWinMain",
            "CommandLineToArgvW(::GetCommandLineW()",
            "const_cast<const wchar_t *const *>(arguments)",
            "::LocalFree(arguments)",
            "failed-windows-command-line-decode",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.entrypoint)

    def test_bootstrap_is_exactly_rt4_pssm_headless_64(self) -> None:
        for token in (
            "ROR_OGRE_NEXT_CHILD_SHADER_MEDIA_ROOT",
            "OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1",
            "PSSM_3_CASCADE_V1",
            "configuration.enable_hdr_compositor = false",
            "initialization.initial_width = 64U",
            "initialization.initial_height = 64U",
            "initialization.maximum_frames_in_flight = 1U",
            "initialization.headless = true",
            "initialization.vertical_sync = false",
            "frontend.Initialize(initialization)",
            "frontend.Shutdown(",
            "kInfiniteRenderTimeoutNanoseconds",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.entrypoint)
        self.assertNotIn("enable_hdr_compositor = true", self.entrypoint)

    def test_entrypoint_refuses_test_seams_at_compile_time(self) -> None:
        self.assertIn(
            "#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM) || \\\n"
            "    defined(ROR_OGRE_NEXT_N2_TEST_SEAM)",
            self.entrypoint,
        )
        self.assertIn(
            '#error "RoR-OgreNext child must not compile with an '
            'Ogre-Next test seam"',
            self.entrypoint,
        )
        self.assertEqual(
            self.entrypoint.count("ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM"), 1
        )
        self.assertEqual(
            self.entrypoint.count("ROR_OGRE_NEXT_N2_TEST_SEAM"), 1
        )

    def test_skip_77_is_only_the_exact_reviewed_pssm_capability_result(self) -> None:
        exact_classifier = self.entrypoint[
            self.entrypoint.index("bool IsExactPssmCapabilityUnsupported") :
            self.entrypoint.index("BootstrapProductionFrontend")
        ]
        self.assertIn("RenderOperationCode::UNSUPPORTED", exact_classifier)
        self.assertIn(
            "kOgreNextPssmCapabilityUnsupportedDetail", exact_classifier
        )
        self.assertEqual(
            self.entrypoint.count(
                "kRendererOgreNextChildCapabilityUnsupportedExitCode = 77"
            ),
            1,
        )
        self.assertIn(
            "skipped-exact-pssm-capability-unsupported", self.entrypoint
        )
        self.assertIn(
            "g_bootstrap_observation ==\n"
            "          BootstrapObservation::EXACT_PSSM_CAPABILITY_UNSUPPORTED",
            self.entrypoint,
        )

    def test_runtime_frontend_is_seam_free_and_test_frontend_is_unchanged(self) -> None:
        runtime_library = self.cmake[
            self.cmake.index(
                "add_library(\n        ror_ogre_next_frontend_n1_runtime"
            ) :
            self.cmake.index(
                "add_executable(\n        ror_ogre_next_frontend_n1_smoke"
            )
        ]
        for token in (
            "target_compile_definitions(\n"
            "        ror_ogre_next_frontend_n1_runtime",
            "target_link_libraries(\n"
            "        ror_ogre_next_frontend_n1_runtime",
            "CXX_VISIBILITY_PRESET hidden",
            "ror_ogre_next_pssm_source_closure_verify",
        ):
            with self.subTest(runtime_token=token):
                self.assertIn(token, runtime_library)
        self.assertNotIn("TEST_SEAM", runtime_library)

        seamful_library = self.cmake[
            self.cmake.index(
                "add_library(\n        ror_ogre_next_frontend_n1 STATIC"
            ) :
            self.cmake.index(
                "add_library(\n        ror_ogre_next_frontend_n1_runtime"
            )
        ]
        self.assertIn("ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM=1", seamful_library)
        self.assertIn("ROR_OGRE_NEXT_N2_TEST_SEAM=1", seamful_library)

    def test_real_output_target_is_probe_only_and_not_staged(self) -> None:
        runtime_target = self.cmake[
            self.cmake.index(
                "add_executable(\n        ror_renderer_ogre_next_child_runtime"
            ) :
            self.cmake.index(
                "add_executable(\n        ror_renderer_child_launcher_tests"
            )
        ]
        for token in (
            "RendererOgreNextChildMain.cpp",
            "RendererOgreNextChild.cpp",
            "RendererChildIntent.cpp",
            "RendererStartupPlan.cpp",
            "RendererStartupHandoff.cpp",
            "RendererBackendPolicy.cpp",
            "PRIVATE ror_ogre_next_frontend_n1_runtime",
            'OUTPUT_NAME "RoR-OgreNext"',
            "WIN32_EXECUTABLE YES",
            "PRIVATE Shell32",
        ):
            with self.subTest(token=token):
                self.assertIn(token, runtime_target)
        for prohibited in (
            "RendererChildLauncher.cpp",
            "source/main/main.cpp",
            "AppContext",
            "SDL",
            "OIS",
            "MyGUI",
            "TEST_SEAM",
        ):
            with self.subTest(prohibited=prohibited):
                self.assertNotIn(prohibited, runtime_target)

        package = self.cmake[
            self.cmake.index("set(ROR_OGRE_NEXT_N1_PACKAGE_ROOT") :
            self.cmake.index(
                "add_custom_target(ror_ogre_next_frontend_n1_package ALL"
            )
        ]
        self.assertNotIn("ror_renderer_ogre_next_child_runtime", package)
        self.assertNotIn("RoR-OgreNext", package)
        for cmake_path in (
            REPOSITORY_ROOT / "CMakeLists.txt",
            REPOSITORY_ROOT / "source/main/CMakeLists.txt",
        ):
            with self.subTest(cmake_path=cmake_path):
                self.assertNotIn(
                    "RendererOgreNextChildMain.cpp",
                    cmake_path.read_text(encoding="utf-8"),
                )

    def test_ctest_supplies_exact_synthetic_intent(self) -> None:
        ctest = self.cmake[
            self.cmake.index(
                "if (TARGET ror_renderer_ogre_next_child_runtime)"
            ) :
            self.cmake.index(
                "if (TARGET ror_renderer_child_launcher_tests)"
            )
        ]
        for token in (
            "--ror-renderer-child-intent-version=1",
            "--ror-renderer-child-frontend=ogre-next-require",
            "--ror-renderer-child-directional-shadows=pssm",
            "--ror-renderer-child-native-backend=none",
            "RESOURCE_LOCK ror_ogre_next_n1_native_device",
            "SKIP_RETURN_CODE 77",
            "TIMEOUT 120",
        ):
            with self.subTest(token=token):
                self.assertIn(token, ctest)

    def test_media_path_and_build_provenance_are_explicit(self) -> None:
        self.assertIn(
            'ROR_OGRE_NEXT_CHILD_SCOPE "probe-only-non-admitted"', self.config
        )
        self.assertIn("@ROR_SOURCE_COMMIT@", self.config)
        self.assertIn("@ROR_OGRE_NEXT_COMMIT@", self.config)
        for token in (
            '"headless_child_bootstrap": true',
            '"headless_child_output_name": "RoR-OgreNext"',
            '"headless_child_packaged": false',
            '"headless_child_production_admitted": false',
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.build_contract)
        self.assertIn(
            "file(REAL_PATH \"${ROR_OGRE_NEXT_MEDIA_ROOT}\"",
            self.cmake,
        )
        self.assertIn(
            "The probe-only Ogre-Next child requires one representable absolute pinned media root",
            self.cmake,
        )

    def test_every_relevant_source_manifest_covers_the_entrypoint(self) -> None:
        paths = (
            "source/main/system/RendererOgreNextChildMain.cpp",
            SELF_PATH,
        )
        manifests = (
            PROBE_ROOT / "CMakeLists.txt",
            PROBE_ROOT / "cmake/VerifyN2SourceProvenance.cmake",
            REPOSITORY_ROOT / "tools/run_ogre_next_probe.py",
            REPOSITORY_ROOT / "tools/verify_ogre_next_artifact_set.py",
        )
        for manifest in manifests:
            content = manifest.read_text(encoding="utf-8")
            for path in paths:
                with self.subTest(manifest=manifest, path=path):
                    self.assertGreaterEqual(content.count(path), 1)

    def test_workflow_runs_static_and_native_runtime_contracts(self) -> None:
        self.assertEqual(self.workflow.count(f"python {SELF_PATH}"), 1)
        self.assertEqual(self.workflow.count(f"python -O {SELF_PATH}"), 1)
        self.assertIn(
            "-R '^ror_renderer_ogre_next_child_runtime$'", self.workflow
        )

    def test_docs_keep_the_child_non_admitted(self) -> None:
        integration = (
            REPOSITORY_ROOT / "doc/nextgen/OGRE_NEXT_INTEGRATION.md"
        ).read_text(encoding="utf-8")
        roadmap = (
            REPOSITORY_ROOT / "doc/nextgen/ROADMAP.md"
        ).read_text(encoding="utf-8")
        for token in (
            "probe-only `RoR-OgreNext`",
            "not installed, staged, bundled, or production-admitted",
            "64x64",
            "game bridge",
            "presentation",
        ):
            with self.subTest(token=token):
                self.assertIn(token, integration + roadmap)


if __name__ == "__main__":
    unittest.main()
