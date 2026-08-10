#!/usr/bin/env python3
"""Offline contract for the standalone real RoR-OgreNext child."""

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
ORCHESTRATION_PATH = ENTRYPOINT_PATH.with_name("RendererOgreNextChild.cpp")
ORCHESTRATION_HEADER_PATH = ENTRYPOINT_PATH.with_name(
    "RendererOgreNextChild.h"
)
SELF_PATH = "tests/tools/test_ogre_next_child_runtime_contract.py"
RUNNER_PATH = PROBE_ROOT / "run_child_runtime_receipt.py"
VALIDATOR_PATH = PROBE_ROOT / "validate_child_runtime_receipt.py"
PROCESS_CLOSURE_PATH = PROBE_ROOT / "verify_child_runtime_process_closure.py"


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


RECEIPT_RUNNER = load_module("ror_child_receipt_runner", RUNNER_PATH)
RECEIPT_VALIDATOR = load_module("ror_child_receipt_validator", VALIDATOR_PATH)
PROCESS_CLOSURE = load_module("ror_child_process_closure", PROCESS_CLOSURE_PATH)


class OgreNextChildRuntimeContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.entrypoint = ENTRYPOINT_PATH.read_text(encoding="utf-8")
        cls.orchestration = ORCHESTRATION_PATH.read_text(encoding="utf-8")
        cls.orchestration_header = ORCHESTRATION_HEADER_PATH.read_text(
            encoding="utf-8"
        )
        cls.live_session = ENTRYPOINT_PATH.with_name(
            "RendererOgreNextLiveSession.cpp"
        ).read_text(encoding="utf-8")
        cls.production_session = ENTRYPOINT_PATH.with_name(
            "RendererOgreNextProductionSession.cpp"
        ).read_text(encoding="utf-8")
        cls.dispatcher_header = (
            REPOSITORY_ROOT
            / "source"
            / "main"
            / "gfx"
            / "render"
            / "RendererFrontendTransportDispatcher.h"
        ).read_text(encoding="utf-8")
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
        self,
        root: Path,
        requested_policy: str | None = None,
        *,
        packaged: bool = False,
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
                "headless_child_process_model": (
                    "single-process-reviewed-source-closure-v1"
                ),
                "headless_child_packaged": packaged,
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
                RECEIPT_RUNNER, "_execute_child", return_value=completed
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

    def test_bridge_orchestration_decodes_before_callbacks_without_adoption(
        self,
    ) -> None:
        parse_position = self.orchestration.index(
            "ParseRendererBridgeEndpoint("
        )
        preflight_position = self.orchestration.index(
            "runtime.collect_native_preflight()"
        )
        frontend_position = self.orchestration.index(
            "runtime.bootstrap_frontend(result.frontend_request)"
        )
        self.assertLess(parse_position, preflight_position)
        self.assertLess(preflight_position, frontend_position)
        for token in (
            "RendererBridgeRole::PRESENTATION_FRONTEND",
            "IsValidRendererBridgeEndpoint(bridge.endpoint)",
            "ClassifyRendererBridgeEndpointFailure(bridge.status)",
            "bridge.forwarded_arguments",
            "ACCEPTED_PRODUCTION_BRIDGE_ORCHESTRATION",
            "REJECTED_BRIDGE_ENDPOINT",
            "REJECTED_BRIDGE_ROLE",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.orchestration)
        for forbidden in (
            "RendererBridgeChannel",
            ".Adopt(",
            "CloseHandle(",
            "ReadFile(",
            "WriteFile(",
            "fcntl(",
            "::close(",
            "::read(",
            "::write(",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, self.orchestration)
        self.assertIn(
            '#include "RendererBridgeEndpoint.h"',
            self.orchestration_header,
        )
        self.assertNotIn("RendererBridgeChannel", self.orchestration_header)
        for path in (
            "source/main/system/RendererBridgeEndpoint.cpp",
            "source/main/system/RendererBridgeEndpoint.h",
        ):
            self.assertIn(path, PROCESS_CLOSURE.ROR_PATHS)

    def test_bridge_endpoint_is_linked_into_both_child_contract_targets(
        self,
    ) -> None:
        test_target = self.cmake[
            self.cmake.index(
                "add_executable(\n        ror_renderer_ogre_next_child_tests"
            ) : self.cmake.index(
                "add_executable(\n        ror_renderer_ogre_next_live_session_tests"
            )
        ]
        runtime_target = self.cmake[
            self.cmake.index(
                "add_executable(\n        ror_renderer_ogre_next_child_runtime"
            ) : self.cmake.index(
                "target_include_directories(\n"
                "        ror_renderer_ogre_next_child_runtime"
            )
        ]
        for target in (test_target, runtime_target):
            self.assertEqual(
                target.count(
                    "source/main/system/RendererBridgeEndpoint.cpp"
                ),
                1,
            )

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
            "RendererBridgeChannel.cpp",
            "RendererOgreNextLiveSession.cpp",
            "RendererOgreNextProductionSession.cpp",
            "RendererPackagedMediaPath.cpp",
            "ror_ogre_next_frontend_n1_runtime",
            "ror_renderer_ogre_next_sdl_window_runtime",
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
            "OIS",
            "MyGUI",
            "TEST_SEAM",
        ):
            with self.subTest(prohibited=prohibited):
                self.assertNotIn(prohibited, runtime_target)

        package = self.cmake[
            self.cmake.index(
                'add_custom_command(\n'
                '        OUTPUT "${ROR_OGRE_NEXT_N1_PACKAGE_STAMP}"'
            ) :
            self.cmake.index(
                "add_custom_target(ror_ogre_next_frontend_n1_package ALL"
            )
        ]
        self.assertNotIn("ror_renderer_ogre_next_child_runtime", package)
        self.assertNotIn("RoR-OgreNext", package)

    def test_production_bridge_runs_owned_relocatable_live_session(self) -> None:
        for token in (
            '#include "RendererPackagedMediaPath.h"',
            "ResolveRendererPackagedMediaPath(",
            "std::filesystem::path(g_packaged_media.shader_media_root).u8string()",
            "RunRendererOgreNextProductionSession(",
            "COMPLETED_PRODUCTION_BRIDGE_SESSION",
        ):
            with self.subTest(entrypoint_token=token):
                self.assertIn(token, self.entrypoint)
        self.assertNotIn("BridgeSessionUnavailable", self.entrypoint)
        self.assertNotIn("ExitCode = 78", self.entrypoint)
        self.assertNotIn(
            "ROR_OGRE_NEXT_CHILD_PRESENTATION_MEDIA_ROOT", self.config
        )
        for token in (
            "channel.Adopt()",
            "RenderTransportStreamDecoder",
            "RendererFrontendTransportDispatcher",
            "RenderBridgeControlKind::PEER_READY",
            "EncodeInputEventTransportFrame(",
            "EncodeRenderBridgeAcknowledgementFrame(",
            "RenderBridgeControlKind::REQUEST_GRACEFUL_SHUTDOWN",
        ):
            with self.subTest(live_token=token):
                self.assertIn(token, self.live_session)
        dispatch = self.live_session.index("dispatcher.Dispatch(frame, policy)")
        acknowledgement = self.live_session.index(
            "EncodeRenderBridgeAcknowledgementFrame(", dispatch
        )
        self.assertLess(dispatch, acknowledgement)
        for token in (
            "RendererOgreNextSdlWindowRuntime adapter",
            "RendererOgreNextWindowHost host",
            "OgreNextN1Frontend frontend",
            "OgreNextN1PresentationMode::PRODUCTION_RUN_LOOP",
            "presentation.gpu_only_output = true",
            "PSSM_3_CASCADE_V1",
            "RunRendererOgreNextLiveSession(endpoint, live_runtime)",
            "audit.source_readbacks != 0U",
            "audit.cpu_window_copy",
        ):
            with self.subTest(production_token=token):
                self.assertIn(token, self.production_session)
        for cmake_path in (
            REPOSITORY_ROOT / "CMakeLists.txt",
            REPOSITORY_ROOT / "source/main/CMakeLists.txt",
        ):
            with self.subTest(cmake_path=cmake_path):
                self.assertNotIn(
                    "RendererOgreNextChildMain.cpp",
                    cmake_path.read_text(encoding="utf-8"),
                )

    def test_surface_readiness_and_failure_exit_contracts(self) -> None:
        for token in (
            "ready.surface = runtime.initial_surface",
            "RenderBridgeControlKind::SURFACE_CHANGED",
            "observation.surface.surface_revision >=",
            "SameSurface(observation.surface, announced_surface)",
            "result.peer_ready_sent = true",
            "FAILED_PEER_CLOSED_BEFORE_READY",
            "surface_changed || observation.surface.suspended",
            "channel.TryReadSome(bytes.data(), bytes.size())",
            "runtime.idle_poll_interval_milliseconds",
            "policy.retire_scene_without_render",
        ):
            with self.subTest(live_surface_token=token):
                self.assertIn(token, self.live_session)
        frame_poll = self.live_session.index(
            "runtime.poll(runtime.context, frame.sequence"
        )
        surface_control = self.live_session.index(
            "send_surface_change(observation.surface)", frame_poll
        )
        dispatch = self.live_session.index(
            "dispatcher.Dispatch(frame, policy)", surface_control
        )
        self.assertLess(surface_control, dispatch)
        self.assertIn("SCENE_FRAME_RETIRED", self.dispatcher_header)

        for token in (
            "MakeBridgeSurface(",
            "observation->surface = MakeBridgeSurface(",
            "live_runtime.initial_surface = initial_bridge_surface",
            "live_runtime.idle_poll_interval_milliseconds = 4U",
            "result.live.completed && result.live.peer_ready_sent",
        ):
            with self.subTest(production_surface_token=token):
                self.assertIn(token, self.production_session)

        for token in (
            "kRendererOgreNextChildPrePeerReadyFailureExitCode = 73",
            "kRendererOgreNextChildPostPeerReadyFailureExitCode = 74",
            "PRE_PEER_READY = 1U",
            "PEER_READY_SENT = 2U",
            "ResolveRendererOgreNextProductionFailureExitCode(",
        ):
            with self.subTest(readiness_token=token):
                self.assertIn(token, self.orchestration_header)
        self.assertIn(
            "ResolveRendererOgreNextProductionFailureExitCode(result)",
            self.entrypoint,
        )
        self.assertIn(
            "g_production_session.live.peer_ready_sent", self.entrypoint
        )
        exit_code_for = self.entrypoint.index("int ExitCodeFor(")
        self.assertLess(
            self.entrypoint.index(
                "ResolveRendererOgreNextProductionFailureExitCode(result)",
                exit_code_for,
            ),
            self.entrypoint.index(
                "BootstrapObservation::EXACT_PSSM_CAPABILITY_UNSUPPORTED",
                exit_code_for,
            ),
        )
        self.assertIn(
            "case RendererOgreNextChildStatus::REJECTED_STARTUP_PLAN:",
            self.orchestration,
        )
        self.assertIn(
            "case RendererOgreNextChildStatus::"
            "REJECTED_UNSUPPORTED_STARTUP_PATH:",
            self.orchestration,
        )

    def test_ctest_runs_and_independently_validates_the_receipt(self) -> None:
        ctest = self.cmake[
            self.cmake.index(
                "if (TARGET ror_renderer_ogre_next_child_runtime)"
            ) :
            self.cmake.index(
                "if (TARGET ror_renderer_child_launcher_tests)"
            )
        ]
        for token in (
            "run_child_runtime_receipt.py",
            "validate_child_runtime_receipt.py",
            "--timeout-seconds 110",
            "--require-pass-or-skip",
            "RESOURCE_LOCK ror_ogre_next_n1_native_device",
            "SKIP_RETURN_CODE 77",
            "TIMEOUT 120",
            "DEPENDS ror_renderer_ogre_next_child_runtime",
        ):
            with self.subTest(token=token):
                self.assertIn(token, ctest)
        for argument in RECEIPT_VALIDATOR.INTENT_ARGUMENTS:
            with self.subTest(intent_argument=argument):
                self.assertEqual(self.receipt_validator.count(argument), 1)
        self.assertIn("*INTENT_ARGUMENTS", self.receipt_runner)

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

    def test_receipt_scope_tracks_non_admitted_product_stage(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="ror-child-product-receipt-pass-"
        ) as temp:
            root = Path(temp).resolve()
            child = self.make_receipt_fixture(
                root, "macos-arm64-metal", packaged=True
            )
            result = self.run_receipt_observation(
                root,
                child,
                0,
                RECEIPT_VALIDATOR.SUCCESS_LINE + b"\n",
                b"",
            )
            self.assertEqual(result, 0)
            receipt = RECEIPT_VALIDATOR.validate_receipt(
                root, require_pass_or_skip=True
            )
            self.assertEqual(
                receipt["scope"],
                {
                    "probe_only": False,
                    "packaged": True,
                    "production_admitted": False,
                    "process_model": (
                        RECEIPT_VALIDATOR.RECEIPT_PROCESS_MODEL
                    ),
                },
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

    def test_receipt_rejects_contradictory_launch_status_and_exit_code(self) -> None:
        for launch_status, exit_code in (
            ("timeout", 0),
            ("launch-error", 70),
            ("exited", None),
        ):
            with (
                self.subTest(
                    launch_status=launch_status, exit_code=exit_code
                ),
                tempfile.TemporaryDirectory(
                    prefix="ror-child-receipt-process-tamper-"
                ) as temp,
            ):
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
                receipt_path = root / RECEIPT_VALIDATOR.RECEIPT_NAME
                receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
                receipt["process"]["launch_status"] = launch_status
                receipt["process"]["exit_code"] = exit_code
                receipt_path.write_text(
                    json.dumps(receipt, sort_keys=True) + "\n",
                    encoding="utf-8",
                )
                with self.assertRaisesRegex(
                    RECEIPT_VALIDATOR.ReceiptValidationError,
                    "contradictory",
                ):
                    RECEIPT_VALIDATOR.validate_receipt(root)

    def test_receipt_rejects_boolean_version_values(self) -> None:
        for target in ("receipt", "intent"):
            with self.subTest(target=target), tempfile.TemporaryDirectory(
                prefix="ror-child-receipt-version-tamper-"
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
                receipt_path = root / RECEIPT_VALIDATOR.RECEIPT_NAME
                receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
                if target == "receipt":
                    receipt["schema_version"] = True
                else:
                    receipt["intent_contract"]["version"] = True
                receipt_path.write_text(
                    json.dumps(receipt, sort_keys=True) + "\n",
                    encoding="utf-8",
                )
                with self.assertRaises(
                    RECEIPT_VALIDATOR.ReceiptValidationError
                ):
                    RECEIPT_VALIDATOR.validate_receipt(root)

    def test_media_path_and_build_provenance_are_explicit(self) -> None:
        self.assertIn(
            'ROR_OGRE_NEXT_CHILD_SCOPE "@ROR_OGRE_NEXT_CHILD_SCOPE@"', self.config
        )
        self.assertIn(
            'set(ROR_OGRE_NEXT_CHILD_SCOPE "probe-only-non-admitted")',
            self.cmake,
        )
        self.assertIn(
            'set(ROR_OGRE_NEXT_CHILD_SCOPE "production-package-non-admitted")',
            self.cmake,
        )
        self.assertIn("@ROR_SOURCE_COMMIT@", self.config)
        self.assertIn("@ROR_OGRE_NEXT_COMMIT@", self.config)
        for token in (
            '"schema_version": 6',
            '"headless_child_bootstrap": true',
            '"headless_child_output_name": "RoR-OgreNext"',
            '"headless_child_execution_receipt_schema": '
            '"ror.ogre_next_child_runtime_execution_receipt.v1"',
            '"headless_child_execution_receipt_required": true',
            '"headless_child_binary_retained": true',
            '"headless_child_logs_retained": true',
            '"headless_child_process_model": '
            '"single-process-reviewed-source-closure-v1"',
            '"headless_child_packaged": @ROR_OGRE_NEXT_CHILD_PACKAGED_JSON@',
            '"headless_child_production_admitted": false',
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.build_contract)
        self.assertIn(
            'file(TO_CMAKE_PATH "${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT}"',
            self.cmake,
        )
        self.assertIn(
            "The probe-only Ogre-Next child requires one representable "
            "absolute staged media root",
            self.cmake,
        )
        self.assertIn(
            "ror_renderer_ogre_next_child_runtime\n"
            "            ror_ogre_next_frontend_n1_package",
            self.cmake,
        )
        self.assertIn(
            'REQUIRED_FILES "${ROR_OGRE_NEXT_N1_PACKAGE_STAMP}"',
            self.cmake,
        )

    def test_every_relevant_source_manifest_covers_the_entrypoint(self) -> None:
        paths = (
            "source/main/system/RendererOgreNextChildMain.cpp",
            "source/main/system/RendererBridgeEndpoint.cpp",
            "source/main/system/RendererBridgeEndpoint.h",
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
            "-R '^ror_renderer_ogre_next_child_runtime(_receipt)?$'",
            self.workflow,
        )
        for token in (
            "Resolve and revalidate the upload-bound Ogre-Next child evidence",
            "Cryptographically attest the Ogre-Next child receipt and binary",
            "Stage and verify the GitHub-signed Ogre-Next child evidence",
            "actions/attest@508db95dd578ae2727ebd6217d5ba78e4fbda05d",
            "steps.ogre_next_child_runtime_evidence.outputs.receipt",
            "steps.ogre_next_child_runtime_evidence.outputs.child",
            "attest_ogre_next_child_runtime.outputs.bundle-path",
            "gh attestation verify",
            "ror-ogre-next-child-runtime-execution-receipt.json",
            "ror-ogre-next-child-runtime.stdout.log",
            "ror-ogre-next-child-runtime.stderr.log",
            "ror-ogre-next-child-runtime-execution-receipt.sigstore.jsonl",
        ):
            with self.subTest(workflow_token=token):
                self.assertIn(token, self.workflow)
        child_attestation = self.workflow[
            self.workflow.index(
                "- name: Cryptographically attest the Ogre-Next child"
            ) : self.workflow.index(
                "- name: Stage and verify the GitHub-signed Ogre-Next child"
            )
        ]
        self.assertNotIn("RoR-OgreNext*", child_attestation)
        self.assertNotIn("actions/attest-build-provenance@", child_attestation)
        self.assertNotIn(
            "renderer-ogre-next-child-runtime/Release/\n", self.workflow
        )

    def test_receipt_validator_is_exact_and_fail_closed(self) -> None:
        for token in (
            "duplicate JSON key",
            "build contract schema 6",
            "child binary artifact binding mismatch",
            "child execution log binding mismatch",
            "child receipt outcome classification mismatch",
            "child launch status and exit code are contradictory",
            'b"\\r\\n" if platform_policy == "windows-x64-d3d11"',
            'return "failure", "invalid-skip-observation", 78',
            'return "skip", "exact-pssm-capability-unsupported", 77',
            'NONCE_POLICY = "os-csprng-256-bit-v1"',
            'TIMESTAMP_POLICY = "omitted-no-wall-clock-v1"',
        ):
            with self.subTest(validator_token=token):
                self.assertIn(token, self.receipt_validator)
        self.assertNotIn("challenge_nonce", self.receipt_runner)
        self.assertNotIn("challenge_nonce", self.receipt_validator)

    def test_runtime_process_closure_forbids_descendant_creation_calls(self) -> None:
        for call in (
            "fork()",
            "posix_spawn()",
            "execve()",
            "CreateProcessW()",
            "ShellExecuteA()",
            "popen()",
            "system()",
        ):
            with self.subTest(call=call):
                self.assertIsNotNone(PROCESS_CLOSURE.FORBIDDEN_CALL.search(call))
        ignored = PROCESS_CLOSURE._scrub_comments_and_literals(
            '// fork()\\\n system()\n'
            "/* CreateProcessW() */\n"
            '"execve() and an escaped quote: \\\""\n'
            "'p'\n"
            'R"marker(popen())marker"\n'
        )
        self.assertIsNone(PROCESS_CLOSURE.FORBIDDEN_CALL.search(ignored))
        for bypass in (
            '"/*"; fork(); "*/"',
            'R"open(/*)open"; execve(); R"close(*/)close"',
            '"//"; CreateProcessW();',
        ):
            with self.subTest(bypass=bypass):
                scrubbed = PROCESS_CLOSURE._scrub_comments_and_literals(bypass)
                self.assertIsNotNone(
                    PROCESS_CLOSURE.FORBIDDEN_CALL.search(scrubbed)
                )
        for malformed in (
            "/* unterminated",
            '"unterminated',
            'R"marker(unterminated',
        ):
            with self.subTest(malformed=malformed), self.assertRaises(
                PROCESS_CLOSURE.ProcessClosureError
            ):
                PROCESS_CLOSURE._scrub_comments_and_literals(malformed)
        for token in (
            "ror_renderer_ogre_next_child_process_closure_verify",
            "verify_child_runtime_process_closure.py",
            "--ror-root",
            "--ogre-root",
            "add_dependencies(\n"
            "        ror_renderer_ogre_next_child_runtime",
        ):
            with self.subTest(cmake_token=token):
                self.assertIn(token, self.cmake)

    def test_posix_timeout_kills_and_reaps_the_child_process_group(self) -> None:
        process = mock.Mock()
        process.pid = 8675
        process.communicate.side_effect = (
            subprocess.TimeoutExpired(["child"], 1),
            (b"partial stdout", b"partial stderr"),
        )
        with (
            mock.patch.object(
                RECEIPT_RUNNER.subprocess, "Popen", return_value=process
            ) as popen,
            mock.patch.object(RECEIPT_RUNNER, "POSIX_PROCESS_GROUPS", True),
            # Windows does not export these POSIX-only names. Create both
            # seams explicitly so every CI host still exercises the timeout
            # branch instead of making the contract test host-dependent.
            mock.patch.object(
                RECEIPT_RUNNER.os, "killpg", create=True
            ) as killpg,
            mock.patch.object(
                RECEIPT_RUNNER.signal, "SIGKILL", 9, create=True
            ),
            self.assertRaises(subprocess.TimeoutExpired) as timeout,
        ):
            RECEIPT_RUNNER._execute_child(["child"], 1)
        self.assertEqual(timeout.exception.stdout, b"partial stdout")
        self.assertEqual(timeout.exception.stderr, b"partial stderr")
        popen.assert_called_once_with(
            ["child"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            start_new_session=True,
        )
        killpg.assert_called_once_with(8675, 9)
        self.assertEqual(process.communicate.call_count, 2)

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
            "execution_nonce",
            "GitHub-attest",
            "actions/attest",
        ):
            with self.subTest(token=token):
                self.assertIn(token, integration + roadmap)


if __name__ == "__main__":
    unittest.main()
