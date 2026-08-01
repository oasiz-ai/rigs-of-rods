#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

from contextlib import redirect_stdout
import importlib.util
import io
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = REPOSITORY_ROOT / "tools/macos_bundle_smoke.py"
PROBE_PATH = REPOSITORY_ROOT / "tools/macos_gl3_capability_probe.mm"

SPEC = importlib.util.spec_from_file_location("macos_bundle_smoke", TOOL_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load macOS bundle smoke tool")
SMOKE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SMOKE)


def completed(returncode: int, output: str) -> subprocess.CompletedProcess[bytes]:
    return subprocess.CompletedProcess(
        args=["fixture"],
        returncode=returncode,
        stdout=output.encode("utf-8"),
    )


class MacOSBundleSmokeTests(unittest.TestCase):
    def test_capability_probe_has_an_unambiguous_contract(self) -> None:
        self.assertTrue(
            SMOKE.classify_gl3_capability(
                0, "ROR_MACOS_GL3_CAPABILITY=available version=4.1\n"
            )
        )
        self.assertFalse(
            SMOKE.classify_gl3_capability(
                78,
                "ROR_MACOS_GL3_CAPABILITY=unavailable "
                "reason=accelerated-core-pixel-format-unavailable\n",
            )
        )

        invalid_results = (
            (0, ""),
            (78, ""),
            (
                0,
                "ROR_MACOS_GL3_CAPABILITY=available\n"
                "ROR_MACOS_GL3_CAPABILITY=unavailable\n",
            ),
            (70, "ROR_MACOS_GL3_CAPABILITY=error\n"),
            (-11, "ROR_MACOS_GL3_CAPABILITY=unavailable\n"),
        )
        for returncode, output in invalid_results:
            with self.subTest(returncode=returncode, output=output):
                with self.assertRaises(SMOKE.SmokeFailure):
                    SMOKE.classify_gl3_capability(returncode, output)

    def test_pre_render_checks_require_clean_exit_and_both_markers(self) -> None:
        SMOKE.validate_pre_render(
            "version",
            0,
            "[RoR|Startup|Paths] process='bundle'\n"
            "Version Information: Rigs of Rods\n",
            SMOKE.VERSION_MARKER,
        )
        for returncode, output in (
            (-11, "[RoR|Startup|Paths]\nVersion Information: Rigs of Rods"),
            (1, "[RoR|Startup|Paths]\nVersion Information: Rigs of Rods"),
            (0, "Version Information: Rigs of Rods"),
            (0, "[RoR|Startup|Paths]"),
        ):
            with self.subTest(returncode=returncode, output=output):
                with self.assertRaises(SMOKE.SmokeFailure):
                    SMOKE.validate_pre_render(
                        "version",
                        returncode,
                        output,
                        SMOKE.VERSION_MARKER,
                    )

    def test_renderer_smoke_requires_all_runtime_evidence(self) -> None:
        engine_log = "\n".join(
            (
                *SMOKE.ENGINE_REQUIRED_MARKERS[:-2],
                *SMOKE.SHUTDOWN_ENGINE_REQUIRED_MARKERS,
            )
        )
        script_log = "\n".join(SMOKE.SCRIPT_REQUIRED_MARKERS)
        SMOKE.validate_runtime_smoke(0, "normal stdout", engine_log, script_log)

        for missing_marker in SMOKE.ENGINE_REQUIRED_MARKERS:
            with self.subTest(missing_marker=missing_marker):
                with self.assertRaises(SMOKE.SmokeFailure):
                    SMOKE.validate_runtime_smoke(
                        0,
                        "normal stdout",
                        engine_log.replace(missing_marker, ""),
                        script_log,
                    )

    def test_renderer_smoke_requires_unique_ordered_shutdown(self) -> None:
        prefix = "\n".join(SMOKE.ENGINE_REQUIRED_MARKERS[:-2])
        shutdown = "\n".join(SMOKE.SHUTDOWN_ENGINE_REQUIRED_MARKERS)
        script_log = "\n".join(SMOKE.SCRIPT_REQUIRED_MARKERS)
        for invalid_shutdown in (
            "\n".join(reversed(SMOKE.SHUTDOWN_ENGINE_REQUIRED_MARKERS)),
            shutdown
            + "\n"
            + SMOKE.SHUTDOWN_ENGINE_REQUIRED_MARKERS[1],
        ):
            with self.subTest(invalid_shutdown=invalid_shutdown):
                with self.assertRaises(SMOKE.SmokeFailure):
                    SMOKE.validate_runtime_smoke(
                        0,
                        "normal stdout",
                        prefix + "\n" + invalid_shutdown,
                        script_log,
                    )

    def test_application_crash_is_never_reclassified_as_a_capability_skip(self) -> None:
        with self.assertRaisesRegex(
            SMOKE.SmokeFailure, "terminated by signal 11"
        ):
            SMOKE.validate_runtime_smoke(
                -11,
                "",
                "RenderingAPIException: OpenGL 3.0 is not supported",
                "",
            )

    def test_fatal_diagnostic_fails_even_after_clean_exit(self) -> None:
        with self.assertRaisesRegex(
            SMOKE.SmokeFailure, "fatal renderer/runtime diagnostic"
        ):
            SMOKE.validate_runtime_smoke(
                0,
                "Segmentation fault",
                "\n".join(
                    (
                        *SMOKE.ENGINE_REQUIRED_MARKERS[:-2],
                        *SMOKE.SHUTDOWN_ENGINE_REQUIRED_MARKERS,
                    )
                ),
                "\n".join(SMOKE.SCRIPT_REQUIRED_MARKERS),
            )

    def test_unavailable_host_runs_pre_render_checks_but_not_renderer(self) -> None:
        version_output = (
            "[RoR|Startup|Paths] process='bundle'\n"
            "Version Information: Rigs of Rods\n"
        )
        help_output = (
            "[RoR|Startup|Paths] process='bundle'\n"
            "Command Line Arguments: --help (this)\n"
        )
        probe_output = (
            "ROR_MACOS_GL3_CAPABILITY=unavailable "
            "reason=accelerated-core-context-unavailable\n"
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            engine_log = root / "RoR.log"
            script_log = root / "Angelscript.log"
            engine_log.write_text("existing engine log", encoding="utf-8")
            script_log.write_text("existing script log", encoding="utf-8")
            arguments = [
                "--executable",
                str(root / "RoR"),
                "--probe",
                str(root / "probe"),
                "--runtime-stdout",
                str(root / "runtime.stdout"),
                "--engine-log",
                str(engine_log),
                "--script-log",
                str(script_log),
                "--pre-render-log",
                str(root / "pre-render.log"),
                "--probe-log",
                str(root / "probe.log"),
            ]
            mocked_results = (
                completed(0, version_output),
                completed(0, help_output),
                completed(78, probe_output),
            )
            with mock.patch.object(
                SMOKE, "run_command", side_effect=mocked_results
            ) as run_command:
                with redirect_stdout(io.StringIO()):
                    self.assertEqual(SMOKE.main(arguments), 0)
            self.assertEqual(run_command.call_count, 3)
            self.assertIn(
                SMOKE.VERSION_MARKER,
                (root / "pre-render.log").read_text(encoding="utf-8"),
            )
            self.assertEqual(
                (root / "probe.log").read_text(encoding="utf-8"),
                probe_output,
            )
            self.assertFalse((root / "runtime.stdout").exists())
            self.assertEqual(
                engine_log.read_text(encoding="utf-8"), "existing engine log"
            )
            self.assertEqual(
                script_log.read_text(encoding="utf-8"), "existing script log"
            )

    def test_available_host_requires_fresh_full_renderer_logs(self) -> None:
        version_output = (
            "[RoR|Startup|Paths] process='bundle'\n"
            "Version Information: Rigs of Rods\n"
        )
        help_output = (
            "[RoR|Startup|Paths] process='bundle'\n"
            "Command Line Arguments: --help (this)\n"
        )
        probe_output = (
            "ROR_MACOS_GL3_CAPABILITY=available "
            "version=4.1 renderer=Fixture\n"
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            engine_log = root / "RoR.log"
            script_log = root / "Angelscript.log"
            engine_log.write_text("stale engine log", encoding="utf-8")
            script_log.write_text("stale script log", encoding="utf-8")
            arguments = [
                "--executable",
                str(root / "RoR"),
                "--probe",
                str(root / "probe"),
                "--runtime-stdout",
                str(root / "runtime.stdout"),
                "--engine-log",
                str(engine_log),
                "--script-log",
                str(script_log),
                "--pre-render-log",
                str(root / "pre-render.log"),
                "--probe-log",
                str(root / "probe.log"),
            ]
            results = iter(
                (
                    completed(0, version_output),
                    completed(0, help_output),
                    completed(0, probe_output),
                    completed(0, "runtime stdout"),
                )
            )

            def run_fixture(
                command: tuple[str, ...], timeout: int
            ) -> subprocess.CompletedProcess[bytes]:
                del command, timeout
                result = next(results)
                if result.stdout == b"runtime stdout":
                    engine_log.write_text(
                        "\n".join(
                            (
                                *SMOKE.ENGINE_REQUIRED_MARKERS[:-2],
                                *SMOKE.SHUTDOWN_ENGINE_REQUIRED_MARKERS,
                            )
                        ),
                        encoding="utf-8",
                    )
                    script_log.write_text(
                        "\n".join(SMOKE.SCRIPT_REQUIRED_MARKERS),
                        encoding="utf-8",
                    )
                return result

            with mock.patch.object(
                SMOKE, "run_command", side_effect=run_fixture
            ) as run_command:
                with redirect_stdout(io.StringIO()):
                    self.assertEqual(SMOKE.main(arguments), 0)
            self.assertEqual(run_command.call_count, 4)
            self.assertEqual(
                (root / "runtime.stdout").read_text(encoding="utf-8"),
                "runtime stdout",
            )

    def test_native_probe_matches_ogre_gl3plus_context_requirements(self) -> None:
        source = PROBE_PATH.read_text(encoding="utf-8")
        for required_attribute in (
            "NSOpenGLPFAScreenMask",
            "NSOpenGLProfileVersion3_2Core",
            "NSOpenGLPFANoRecovery",
            "NSOpenGLPFAAccelerated",
            "NSOpenGLPFADoubleBuffer",
        ):
            with self.subTest(required_attribute=required_attribute):
                self.assertIn(required_attribute, source)
        self.assertIn("CAPABILITY_UNAVAILABLE = 78", source)


if __name__ == "__main__":
    unittest.main()
