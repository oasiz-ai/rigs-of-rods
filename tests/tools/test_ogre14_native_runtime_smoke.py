#!/usr/bin/env python3
"""Tests for the fail-closed native OGRE 14 renderer smoke."""

from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL = REPOSITORY_ROOT / "tools" / "ogre14_native_runtime_smoke.py"
SCRIPT = REPOSITORY_ROOT / "resources" / "scripts" / (
    "example_ci_bundle_smoke.as"
)
SPEC = importlib.util.spec_from_file_location(
    "ogre14_native_runtime_smoke",
    TOOL,
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot import native runtime smoke from {TOOL}")
SMOKE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = SMOKE
SPEC.loader.exec_module(SMOKE)

EXPECTED_SCRIPT_NAME = "example_ci_bundle_smoke.as"
EXPECTED_SCRIPT_MARKERS = (
    "[RoR|CI|BundleSmoke] START",
    "[RoR|CI|BundleSmoke] PASS frames=10",
)
EXPECTED_COMMON_ENGINE_MARKERS = (
    "[RoR|Startup|Paths]",
    "[RoR|Startup|Rendering] Starting renderer '",
    "[RoR|Startup|Rendering] Creating render window with settings:",
    "RenderSystem::_createRenderWindow",
    "*-*-* OGRE Shutdown",
)
EXPECTED_LINUX_ENGINE_MARKERS = (
    "Installing plugin: GL 3+ RenderSystem",
    "OpenGL 3+ Rendering Subsystem created.",
    "OpenGL 3+ Renderer Started",
    "RenderSystem Name: OpenGL 3+ Rendering Subsystem",
)
EXPECTED_WINDOWS_ENGINE_PATTERNS = (
    r"Installing plugin: [^\r\n]*D3D11[^\r\n]*RenderSystem",
    r"Starting renderer '[^']*(?:Direct3D11|D3D11)[^']*'",
    r"RenderSystem Name: [^\r\n]*(?:Direct3D11|D3D11)",
)


def script_log() -> str:
    return "\n".join(EXPECTED_SCRIPT_MARKERS)


def engine_log(platform: str, user_directory: Path) -> str:
    common = (
        f"[RoR|Startup|Paths] process='runtime', "
        f"user='{user_directory}', logs='{user_directory / 'logs'}'\n"
        "[RoR|Startup|Rendering] Starting renderer "
    )
    if platform == "linux-x86_64":
        renderer = (
            "'OpenGL 3+ Rendering Subsystem' "
            "(without auto-creating render window)\n"
            "[RoR|Startup|Rendering] Creating render window with settings:\n"
            "RenderSystem::_createRenderWindow\n"
            + "\n".join(EXPECTED_LINUX_ENGINE_MARKERS)
            + "\nDevice Name: llvmpipe (LLVM 15.0.7, 256 bits)\n"
        )
    else:
        renderer = (
            "'Direct3D11 Rendering Subsystem' "
            "(without auto-creating render window)\n"
            "[RoR|Startup|Rendering] Creating render window with settings:\n"
            "D3D11RenderSystem::_createRenderWindow\n"
            "Installing plugin: D3D11 RenderSystem\n"
            "RenderSystem Name: Direct3D11 Rendering Subsystem\n"
        )
    return common + renderer + "*-*-* OGRE Shutdown\n"


def completed(output: str = "runtime stdout") -> subprocess.CompletedProcess[bytes]:
    return subprocess.CompletedProcess(
        args=["fixture"],
        returncode=0,
        stdout=output.encode("utf-8"),
    )


class Ogre14NativeRuntimeSmokeTests(unittest.TestCase):
    def test_probe_contract_is_frozen_to_real_script_and_markers(self) -> None:
        self.assertEqual(SMOKE.SCRIPT_NAME, EXPECTED_SCRIPT_NAME)
        self.assertEqual(
            SMOKE.SCRIPT_REQUIRED_MARKERS,
            EXPECTED_SCRIPT_MARKERS,
        )
        self.assertEqual(
            SMOKE.COMMON_ENGINE_REQUIRED_MARKERS,
            EXPECTED_COMMON_ENGINE_MARKERS,
        )
        self.assertEqual(
            SMOKE.LINUX_ENGINE_REQUIRED_MARKERS,
            EXPECTED_LINUX_ENGINE_MARKERS,
        )
        self.assertEqual(
            SMOKE.WINDOWS_ENGINE_REQUIRED_PATTERNS,
            EXPECTED_WINDOWS_ENGINE_PATTERNS,
        )
        source = SCRIPT.read_text(encoding="utf-8")
        for marker in EXPECTED_SCRIPT_MARKERS:
            self.assertIn(marker, source)
        self.assertIn("gCiBundleSmokeFrame == 10", source)
        self.assertIn("game.quitGame();", source)

    def test_linux_requires_gl3plus_llvmpipe_and_ten_frames(self) -> None:
        user_directory = Path("/isolated/.rigsofrods")
        valid_engine = engine_log("linux-x86_64", user_directory)
        SMOKE.validate_runtime_evidence(
            "linux-x86_64",
            returncode=0,
            runtime_output="normal stdout",
            engine_log=valid_engine,
            script_log=script_log(),
            expected_user_directory=user_directory,
        )
        missing_evidence = (
            *EXPECTED_COMMON_ENGINE_MARKERS,
            *EXPECTED_LINUX_ENGINE_MARKERS,
            "llvmpipe",
        )
        for missing in missing_evidence:
            with self.subTest(missing=missing):
                with self.assertRaises(SMOKE.SmokeFailure):
                    SMOKE.validate_runtime_evidence(
                        "linux-x86_64",
                        returncode=0,
                        runtime_output="normal stdout",
                        engine_log=valid_engine.replace(missing, ""),
                        script_log=script_log(),
                        expected_user_directory=user_directory,
                    )

    def test_windows_requires_d3d11_and_ten_frames(self) -> None:
        user_directory = Path(r"C:\isolated\My Games\Rigs of Rods")
        valid_engine = engine_log("windows-x86_64", user_directory)
        SMOKE.validate_runtime_evidence(
            "windows-x86_64",
            returncode=0,
            runtime_output="normal stdout",
            engine_log=valid_engine,
            script_log=script_log(),
            expected_user_directory=user_directory,
        )
        for replacement in (
            valid_engine.replace("D3D11", "GL3Plus").replace(
                "Direct3D11",
                "OpenGL 3+",
            ),
            valid_engine.replace(
                "RenderSystem Name: Direct3D11 Rendering Subsystem",
                "",
            ),
        ):
            with self.assertRaises(SMOKE.SmokeFailure):
                SMOKE.validate_runtime_evidence(
                    "windows-x86_64",
                    returncode=0,
                    runtime_output="normal stdout",
                    engine_log=replacement,
                    script_log=script_log(),
                    expected_user_directory=user_directory,
                )

    def test_script_markers_are_unique_ordered_and_exact(self) -> None:
        user_directory = Path("/isolated/.rigsofrods")
        engine = engine_log("linux-x86_64", user_directory)
        invalid_logs = (
            EXPECTED_SCRIPT_MARKERS[0],
            EXPECTED_SCRIPT_MARKERS[1],
            "\n".join(reversed(EXPECTED_SCRIPT_MARKERS)),
            script_log() + "\n" + EXPECTED_SCRIPT_MARKERS[1],
            script_log().replace("frames=10", "frames=9"),
        )
        for invalid in invalid_logs:
            with self.subTest(invalid=invalid):
                with self.assertRaises(SMOKE.SmokeFailure):
                    SMOKE.validate_runtime_evidence(
                        "linux-x86_64",
                        returncode=0,
                        runtime_output="normal stdout",
                        engine_log=engine,
                        script_log=invalid,
                        expected_user_directory=user_directory,
                    )

    def test_crash_fatal_diagnostic_and_wrong_home_fail_closed(self) -> None:
        user_directory = Path("/isolated/.rigsofrods")
        engine = engine_log("linux-x86_64", user_directory)
        with self.assertRaisesRegex(SMOKE.SmokeFailure, "signal 11"):
            SMOKE.validate_runtime_evidence(
                "linux-x86_64",
                returncode=-11,
                runtime_output="",
                engine_log="",
                script_log="",
                expected_user_directory=user_directory,
            )
        with self.assertRaisesRegex(SMOKE.SmokeFailure, "fatal"):
            SMOKE.validate_runtime_evidence(
                "linux-x86_64",
                returncode=0,
                runtime_output="RenderingAPIException",
                engine_log=engine,
                script_log=script_log(),
                expected_user_directory=user_directory,
            )
        with self.assertRaisesRegex(SMOKE.SmokeFailure, "isolated"):
            SMOKE.validate_runtime_evidence(
                "linux-x86_64",
                returncode=0,
                runtime_output="normal stdout",
                engine_log=engine.replace(
                    str(user_directory),
                    "/host/home/.rigsofrods",
                ),
                script_log=script_log(),
                expected_user_directory=user_directory,
            )

    def test_runtime_environment_isolated_and_forces_llvmpipe(self) -> None:
        root = Path("/absolute/isolated")
        base = {
            "HOME": "/real/home",
            "LD_AUDIT": "/tmp/injected-audit.so",
            "LD_LIBRARY_PATH": "/tmp/injected-library",
            "LD_PRELOAD": "/tmp/injected-preload.so",
            "MESA_LOADER_DRIVER_OVERRIDE": "host-driver",
            "ROR_D0_SCENE_HOME": "/host/scene",
        }
        linux = SMOKE.runtime_environment(
            "linux-x86_64",
            root,
            base,
        )
        self.assertEqual(linux["ROR_D0_SCENE_HOME"], str(root))
        self.assertEqual(linux["GALLIUM_DRIVER"], "llvmpipe")
        self.assertEqual(linux["LIBGL_ALWAYS_SOFTWARE"], "1")
        self.assertEqual(linux["HOME"], "/real/home")
        self.assertNotIn("MESA_LOADER_DRIVER_OVERRIDE", linux)
        self.assertNotIn("LD_AUDIT", linux)
        self.assertNotIn("LD_LIBRARY_PATH", linux)
        self.assertNotIn("LD_PRELOAD", linux)

        windows = SMOKE.runtime_environment(
            "windows-x86_64",
            root,
            {
                "SYSTEMROOT": r"C:\Windows",
                "Path": r"C:\host-tools;C:\conan\bin",
                "GALLIUM_DRIVER": "host",
                "LIBGL_ALWAYS_SOFTWARE": "1",
                "DyLd_LiBrArY_PaTh": r"C:\injected",
            },
        )
        self.assertNotIn("GALLIUM_DRIVER", windows)
        self.assertNotIn("LIBGL_ALWAYS_SOFTWARE", windows)
        self.assertNotIn("Path", windows)
        self.assertNotIn("DyLd_LiBrArY_PaTh", windows)
        self.assertEqual(
            windows["PATH"],
            r"C:\Windows\System32;C:\Windows",
        )

    def test_full_smoke_runs_outside_package_and_writes_report(self) -> None:
        for platform, executable_name, user_parts in (
            ("linux-x86_64", "RunRoR", (".rigsofrods",)),
            (
                "windows-x86_64",
                "RoR.exe",
                ("My Games", "Rigs of Rods"),
            ),
        ):
            with self.subTest(platform=platform):
                with tempfile.TemporaryDirectory(
                    prefix="ror-ogre14-native-smoke-"
                ) as temporary:
                    temporary_root = Path(temporary)
                    runtime = temporary_root / "runtime"
                    runtime.mkdir()
                    executable = runtime / executable_name
                    executable.write_bytes(b"fixture")
                    executable.chmod(0o755)
                    log_root = temporary_root / "logs"
                    user_directory = log_root.resolve().joinpath(*user_parts)

                    def run_fixture(
                        command: tuple[str, ...],
                        *,
                        cwd: Path,
                        environment: dict[str, str],
                        timeout: int,
                    ) -> subprocess.CompletedProcess[bytes]:
                        self.assertEqual(
                            command,
                            (
                                str(executable.resolve()),
                                "-runscript",
                                EXPECTED_SCRIPT_NAME,
                            ),
                        )
                        self.assertFalse(SMOKE.is_within(runtime, cwd))
                        self.assertEqual(
                            environment["ROR_D0_SCENE_HOME"],
                            str(log_root.resolve()),
                        )
                        self.assertEqual(timeout, 30)
                        logs = user_directory / "logs"
                        logs.mkdir(parents=True)
                        (logs / "RoR.log").write_text(
                            engine_log(platform, user_directory),
                            encoding="utf-8",
                        )
                        (logs / "Angelscript.log").write_text(
                            script_log(),
                            encoding="utf-8",
                        )
                        return completed()

                    with (
                        mock.patch.dict(
                            os.environ,
                            {"SystemRoot": r"C:\Windows"},
                            clear=False,
                        ),
                        mock.patch.object(
                            SMOKE,
                            "run_runtime",
                            side_effect=run_fixture,
                        ),
                    ):
                        report = SMOKE.smoke(
                            runtime.resolve(),
                            platform,
                            log_root.resolve(),
                            timeout=30,
                        )
                    self.assertEqual(report["frames"], 10)
                    self.assertEqual(report["script"], EXPECTED_SCRIPT_NAME)
                    self.assertTrue(
                        (log_root / "runtime.stdout.log").is_file()
                    )
                    self.assertTrue(
                        (log_root / "runtime-smoke-report.json").is_file()
                    )

    def test_stale_logs_relative_paths_and_nested_roots_fail(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="ror-ogre14-native-smoke-hostile-"
        ) as temporary:
            temporary_root = Path(temporary)
            runtime = temporary_root / "runtime"
            runtime.mkdir()
            launcher = runtime / "RunRoR"
            launcher.write_bytes(b"fixture")
            launcher.chmod(0o755)
            log_root = temporary_root / "logs"
            logs = log_root / ".rigsofrods" / "logs"
            logs.mkdir(parents=True)
            (logs / "RoR.log").write_text("stale", encoding="utf-8")
            (logs / "Angelscript.log").write_text(
                "stale",
                encoding="utf-8",
            )
            with mock.patch.object(
                SMOKE,
                "run_runtime",
                return_value=completed(),
            ):
                with self.assertRaisesRegex(SMOKE.SmokeFailure, "refreshed"):
                    SMOKE.smoke(
                        runtime.resolve(),
                        "linux-x86_64",
                        log_root.resolve(),
                        timeout=30,
                    )
            with self.assertRaisesRegex(SMOKE.SmokeFailure, "absolute"):
                SMOKE.smoke(
                    Path("relative-runtime"),
                    "linux-x86_64",
                    log_root.resolve(),
                    timeout=30,
                )
            with self.assertRaisesRegex(SMOKE.SmokeFailure, "contain"):
                SMOKE.smoke(
                    runtime.resolve(),
                    "linux-x86_64",
                    (runtime / "logs").resolve(),
                    timeout=30,
                )

    def test_tool_has_no_capability_skip_or_worldmodel_dependency(self) -> None:
        source = TOOL.read_text(encoding="utf-8").casefold()
        self.assertNotIn("capability", source)
        self.assertNotIn("skip", source)
        self.assertNotIn("source/main/worldmodel", source)
        self.assertNotIn("tests/worldmodel", source)


if __name__ == "__main__":
    unittest.main()
