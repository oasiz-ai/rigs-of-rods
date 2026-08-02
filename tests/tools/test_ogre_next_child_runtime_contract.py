#!/usr/bin/env python3
"""Offline contract for the probe-only real RoR-OgreNext bootstrap child."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import platform
import subprocess
import tempfile
import unittest
from unittest import mock


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
RUNNER_PATH = PROBE_ROOT / "run_child_runtime_receipt.py"
VALIDATOR_PATH = PROBE_ROOT / "validate_child_runtime_receipt.py"


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


RECEIPT_RUNNER = load_module("ror_child_receipt_runner", RUNNER_PATH)
RECEIPT_VALIDATOR = load_module("ror_child_receipt_validator", VALIDATOR_PATH)


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
        cls.receipt_runner = RUNNER_PATH.read_text(encoding="utf-8")
        cls.receipt_validator = VALIDATOR_PATH.read_text(encoding="utf-8")

    def make_receipt_fixture(
        self, root: Path, requested_policy: str | None = None
    ) -> Path:
        host = platform.system()
        policies = {
            "macos-arm64-metal": (
                "Darwin",
                "macos-arm64-metal",
                "arm64",
                "RenderSystem_Metal",
            ),
            "windows-x64-d3d11": (
                "Windows",
                "windows-x64-d3d11",
                "AMD64",
                "RenderSystem_Direct3D11",
            ),
            "linux-x86_64-vulkan": (
                "Linux",
                "linux-x86_64-vulkan",
                "x86_64",
                "RenderSystem_Vulkan",
            ),
        }
        host_policies = {
            "Darwin": "macos-arm64-metal",
            "Windows": "windows-x64-d3d11",
            "Linux": "linux-x86_64-vulkan",
        }
        selected = requested_policy or host_policies[host]
        system, policy, processor, renderer = policies[selected]
        contract = {
            "schema_version": 6,
            "ror_source": {
                "repository": "https://github.com/oasiz-ai/rigs-of-rods",
                "ref": "codex/receipt-test",
                "commit": "1" * 40,
                "relevant_manifest_sha256": "2" * 64,
                "relevant_manifest_file_count": 1,
            },
            "provenance": {
                "repository": "https://github.com/OGRECave/ogre-next",
                "branch": "v3-0",
                "commit": "3" * 40,
                "archive_sha256": "4" * 64,
            },
            "platform": {
                "policy": policy,
                "system": system,
                "processor": processor,
                "renderer_target": renderer,
            },
            "components": {
                "headless_child_bootstrap": True,
                "headless_child_output_name": "RoR-OgreNext",
                "headless_child_execution_receipt_schema": (
                    RECEIPT_VALIDATOR.RECEIPT_SCHEMA
                ),
                "headless_child_execution_receipt_required": True,
                "headless_child_binary_retained": True,
                "headless_child_logs_retained": True,
                "headless_child_packaged": False,
                "headless_child_production_admitted": False,
            },
        }
        (root / RECEIPT_VALIDATOR.BUILD_CONTRACT_NAME).write_text(
            json.dumps(contract, sort_keys=True) + "\n", encoding="utf-8"
        )
        child = root.joinpath(
            *RECEIPT_VALIDATOR.expected_child_relative(contract).split("/")
        )
        child.parent.mkdir(parents=True)
        child.write_bytes(b"probe-only-child-binary-fixture\n")
        return child

    def run_receipt_observation(
        self, root: Path, child: Path, exit_code: int, stdout: bytes, stderr: bytes
    ) -> int:
        completed = subprocess.CompletedProcess(
            [str(child)], exit_code, stdout=stdout, stderr=stderr
        )
        with (
            mock.patch.object(
                RECEIPT_RUNNER.subprocess, "run", return_value=completed
            ),
            mock.patch.object(
                RECEIPT_RUNNER.secrets,
                "token_hex",
                return_value="a" * 64,
            ),
        ):
            return RECEIPT_RUNNER.run_child(
                root, child, timeout_seconds=1, emit_output=False
            )

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

    def test_receipt_wrapper_records_and_validates_pass_on_every_policy(self) -> None:
        for policy in RECEIPT_VALIDATOR.PLATFORM_BACKENDS:
            with self.subTest(policy=policy), tempfile.TemporaryDirectory(
                prefix="ror-child-receipt-pass-"
            ) as temp:
                root = Path(temp).resolve()
                child = self.make_receipt_fixture(root, policy)
                ending = b"\r\n" if policy == "windows-x64-d3d11" else b"\n"
                result = self.run_receipt_observation(
                    root,
                    child,
                    0,
                    b"native renderer log" + ending
                    + RECEIPT_VALIDATOR.SUCCESS_LINE + ending,
                    b"",
                )
                self.assertEqual(result, 0)
                receipt = RECEIPT_VALIDATOR.validate_receipt(
                    root, require_pass_or_skip=True
                )
                self.assertEqual(receipt["outcome"], "pass")
                self.assertEqual(
                    receipt["execution"]["execution_nonce"], "a" * 64
                )
                self.assertEqual(
                    receipt["execution"]["timestamp_policy"],
                    RECEIPT_VALIDATOR.TIMESTAMP_POLICY,
                )

    def test_receipt_wrapper_records_only_exact_platform_skip(self) -> None:
        for policy in RECEIPT_VALIDATOR.PLATFORM_BACKENDS:
            with self.subTest(policy=policy), tempfile.TemporaryDirectory(
                prefix="ror-child-receipt-skip-"
            ) as temp:
                root = Path(temp).resolve()
                child = self.make_receipt_fixture(root, policy)
                ending = b"\r\n" if policy == "windows-x64-d3d11" else b"\n"
                result = self.run_receipt_observation(
                    root,
                    child,
                    77,
                    b"",
                    RECEIPT_VALIDATOR.SKIP_LINE + ending,
                )
                self.assertEqual(result, 77)
                receipt = RECEIPT_VALIDATOR.validate_receipt(
                    root, require_pass_or_skip=True
                )
                self.assertEqual(receipt["outcome"], "skip")

    def test_receipt_rejects_cross_platform_marker_line_endings(self) -> None:
        cases = (
            ("windows-x64-d3d11", b"\n"),
            ("macos-arm64-metal", b"\r\n"),
            ("linux-x86_64-vulkan", b"\r\n"),
        )
        for policy, wrong_ending in cases:
            for exit_code, line in (
                (0, RECEIPT_VALIDATOR.SUCCESS_LINE),
                (77, RECEIPT_VALIDATOR.SKIP_LINE),
            ):
                with (
                    self.subTest(
                        policy=policy,
                        exit_code=exit_code,
                        wrong_ending=wrong_ending,
                    ),
                    tempfile.TemporaryDirectory(
                        prefix="ror-child-receipt-ending-"
                    ) as temp,
                ):
                    root = Path(temp).resolve()
                    child = self.make_receipt_fixture(root, policy)
                    stdout = line + wrong_ending if exit_code == 0 else b""
                    stderr = line + wrong_ending if exit_code == 77 else b""
                    result = self.run_receipt_observation(
                        root, child, exit_code, stdout, stderr
                    )
                    self.assertEqual(
                        result, RECEIPT_RUNNER.WRAPPER_FAILURE_EXIT_CODE
                    )
                    receipt = RECEIPT_VALIDATOR.validate_receipt(root)
                    self.assertEqual(receipt["outcome"], "failure")
                    expected_reason = (
                        "invalid-pass-observation"
                        if exit_code == 0
                        else "invalid-skip-observation"
                    )
                    self.assertEqual(receipt["reason"], expected_reason)
                    with self.assertRaises(
                        RECEIPT_VALIDATOR.ReceiptValidationError
                    ):
                        RECEIPT_VALIDATOR.validate_receipt(
                            root, require_pass_or_skip=True
                        )

    def test_receipt_exists_for_ordinary_nonzero_child_failure(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="ror-child-receipt-failure-"
        ) as temp:
            root = Path(temp).resolve()
            child = self.make_receipt_fixture(root, "macos-arm64-metal")
            result = self.run_receipt_observation(
                root,
                child,
                70,
                b"",
                b"RoR Ogre-Next child: initialization-failed\n",
            )
            self.assertEqual(result, RECEIPT_RUNNER.WRAPPER_FAILURE_EXIT_CODE)
            receipt_path = root / RECEIPT_VALIDATOR.RECEIPT_NAME
            self.assertTrue(receipt_path.is_file())
            receipt = RECEIPT_VALIDATOR.validate_receipt(root)
            self.assertEqual(receipt["outcome"], "failure")
            self.assertEqual(receipt["process"]["exit_code"], 70)
            self.assertEqual(receipt["reason"], "child-nonzero-failure")

    def test_receipt_validator_rejects_binary_and_log_tamper(self) -> None:
        for target in ("binary", "stdout", "stderr"):
            with self.subTest(target=target), tempfile.TemporaryDirectory(
                prefix="ror-child-receipt-tamper-"
            ) as temp:
                root = Path(temp).resolve()
                child = self.make_receipt_fixture(root, "macos-arm64-metal")
                self.assertEqual(
                    self.run_receipt_observation(
                        root,
                        child,
                        0,
                        RECEIPT_VALIDATOR.SUCCESS_LINE + b"\n",
                        b"",
                    ),
                    0,
                )
                paths = {
                    "binary": child,
                    "stdout": root / RECEIPT_VALIDATOR.STDOUT_LOG_NAME,
                    "stderr": root / RECEIPT_VALIDATOR.STDERR_LOG_NAME,
                }
                with paths[target].open("ab") as output:
                    output.write(b"tamper")
                with self.assertRaises(
                    RECEIPT_VALIDATOR.ReceiptValidationError
                ):
                    RECEIPT_VALIDATOR.validate_receipt(root)

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
