#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import importlib.util
import json
from pathlib import Path, PurePosixPath
import tempfile
import time
from unittest import mock
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOLS_ROOT = REPOSITORY_ROOT / "tools"
TOOL_PATH = TOOLS_ROOT / "run_jbeam_multi_actor_tsan_soak.py"
WORKFLOW_PATH = (
    REPOSITORY_ROOT / ".github/workflows/ogre-next-combined-tsan.yml"
)
SOUND_MANAGER_HEADER_PATH = (
    REPOSITORY_ROOT / "source/main/audio/SoundScriptManager.h"
)
SOUND_MANAGER_SOURCE_PATH = (
    REPOSITORY_ROOT / "source/main/audio/SoundScriptManager.cpp"
)
MAIN_SOURCE_PATH = REPOSITORY_ROOT / "source/main/main.cpp"

import sys

sys.path.insert(0, str(TOOLS_ROOT))
SPEC = importlib.util.spec_from_file_location("jbeam_tsan_soak", TOOL_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load JBeam TSan soak tool")
GATE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GATE)


def pass_receipt(
    *,
    elapsed: float = 600.125,
    steps: int = 22000,
    cycles: int = 11,
) -> str:
    return (
        "[RoR|D0|TSanSoak] PASS "
        f"elapsed_seconds={elapsed:.3f} physics_steps={steps} "
        f"completed_cycles={cycles} collision_responses={cycles} "
        f"actor_spawns={cycles * 2} actor_deletes={(cycles - 1) * 2}"
    )


def valid_engine_log(archive_sha256: str) -> str:
    return "\n".join(
        (
            "[RoR|ModCache|JBeam] Mounted exact archive",
            f"archive_sha256={archive_sha256}",
            "roots=1",
            "GL_RENDERER = llvmpipe (LLVM test fixture)",
            *GATE.PRESENTATION_MARKERS,
        )
    )


class JBeamMultiActorTSanSoakTests(unittest.TestCase):
    def test_presentation_gate_tracks_the_current_authenticated_ready_marker(
        self,
    ) -> None:
        marker = (
            "[RoR|RendererCombined|Startup] Transport-free OgreNext "
            "session ready after authenticated bootstrap presentation"
        )
        self.assertIn(marker, GATE.PRESENTATION_MARKERS)
        main_source = MAIN_SOURCE_PATH.read_text(encoding="utf-8")
        for fragment in (
            '"[RoR|RendererCombined|Startup] Transport-free OgreNext "',
            '"session ready after authenticated bootstrap presentation "',
        ):
            with self.subTest(fragment=fragment):
                self.assertIn(fragment, main_source)

    def test_profile_binds_exact_continuous_product_soak(self) -> None:
        profile, profile_bytes, jbeam, script = GATE.read_profile(
            REPOSITORY_ROOT
        )
        self.assertGreater(len(profile_bytes), 100)
        self.assertEqual(
            profile["fixtureId"],
            "ror-d0-full-runtime-multi-actor-tsan-soak-v1",
        )
        self.assertEqual(
            profile["expectedRuntime"]["minimumDurationSeconds"], 600
        )
        self.assertEqual(
            profile["expectedRuntime"]["durationClock"],
            "ogre-monotonic-timer",
        )
        self.assertEqual(
            profile["expectedRuntime"]["minimumCompletedCycles"], 10
        )
        self.assertEqual(
            profile["jbeamSource"]["sha256"], GATE.sha256_bytes(jbeam)
        )
        self.assertEqual(
            profile["scenarioScript"]["sha256"],
            GATE.sha256_bytes(script),
        )

    def test_pass_receipt_requires_duration_collision_and_mutation(self) -> None:
        archive_sha256 = "ab" * 32
        script_log = "\n".join(
            (GATE.START_MARKER, GATE.ARM_MARKER, pass_receipt())
        )
        telemetry = GATE.validate_logs(
            0,
            "",
            valid_engine_log(archive_sha256),
            script_log,
            archive_sha256,
        )
        self.assertEqual(telemetry["completed_cycles"], 11)
        self.assertEqual(telemetry["actor_deletes"], 20)

        bad_receipts = (
            pass_receipt(elapsed=599.999),
            pass_receipt(steps=19999),
            pass_receipt(cycles=9),
            pass_receipt().replace("collision_responses=11", "collision_responses=10"),
            pass_receipt().replace("actor_deletes=20", "actor_deletes=18"),
        )
        for receipt in bad_receipts:
            with self.subTest(receipt=receipt):
                with self.assertRaises(GATE.TSanSoakFailure):
                    GATE.validate_logs(
                        0,
                        "",
                        valid_engine_log(archive_sha256),
                        "\n".join(
                            (GATE.START_MARKER, GATE.ARM_MARKER, receipt)
                        ),
                        archive_sha256,
                    )

    def test_sanitizer_diagnostic_and_visible_fallback_fail_closed(self) -> None:
        archive_sha256 = "cd" * 32
        script_log = "\n".join(
            (GATE.START_MARKER, GATE.ARM_MARKER, pass_receipt())
        )
        for stdout, engine in (
            ("WARNING: ThreadSanitizer: data race", valid_engine_log(archive_sha256)),
            (
                "",
                valid_engine_log(archive_sha256).replace(
                    "legacy_visible_fallback=false",
                    "legacy_visible_fallback=true",
                ),
            ),
        ):
            with self.subTest(stdout=stdout):
                with self.assertRaises(GATE.TSanSoakFailure):
                    GATE.validate_logs(
                        0, stdout, engine, script_log, archive_sha256
                    )

    def test_instrumentation_audit_requires_runtime_symbol_and_no_suppressions(
        self,
    ) -> None:
        environment = {
            "LP_NUM_THREADS": "0",
            "TSAN_OPTIONS": (
                "halt_on_error=1:exitcode=66:history_size=7:"
                "second_deadlock_stack=1:log_path=/tmp/ror-tsan"
            )
        }
        dynamic = "Shared library: [libtsan.so.2]\n"
        symbols = "                 U __tsan_init\n"
        with (
            mock.patch.object(GATE.sys, "platform", "linux"),
            mock.patch.object(GATE, "Path", PurePosixPath),
            mock.patch.object(GATE.shutil, "which", side_effect=("/usr/bin/readelf", "/usr/bin/nm")),
            mock.patch.object(
                GATE,
                "run_audit_command",
                side_effect=(dynamic, symbols),
            ),
        ):
            audit = GATE.audit_tsan_instrumentation(
                Path("/tmp/RoR-Combined"), environment
            )
        self.assertEqual(audit["log_path"], "/tmp/ror-tsan")

        environment["TSAN_OPTIONS"] += ":suppressions=/tmp/hidden"
        with mock.patch.object(GATE.sys, "platform", "linux"):
            with self.assertRaises(GATE.TSanSoakFailure):
                GATE.audit_tsan_instrumentation(
                    Path("/tmp/RoR-Combined"), environment
                )

    def test_runtime_environment_forces_synchronous_hidden_llvmpipe(self) -> None:
        with mock.patch.dict(
            GATE.os.environ,
            {"LP_NUM_THREADS": "12", "SNAP_USER_COMMON": "/tmp/snap"},
            clear=True,
        ):
            isolated_home = Path("/tmp/ror-home")
            environment = GATE.build_runtime_environment(isolated_home)
        self.assertEqual(environment["LP_NUM_THREADS"], "0")
        self.assertEqual(
            environment["ROR_D0_SCENE_HOME"], str(isolated_home)
        )
        self.assertNotIn("SNAP_USER_COMMON", environment)

        environment["TSAN_OPTIONS"] = (
            "halt_on_error=1:exitcode=66:history_size=7:"
            "second_deadlock_stack=1:log_path=/tmp/ror-tsan"
        )
        environment["LP_NUM_THREADS"] = "1"
        with mock.patch.object(GATE.sys, "platform", "linux"):
            with self.assertRaises(GATE.TSanSoakFailure):
                GATE.audit_tsan_instrumentation(
                    Path("/tmp/RoR-Combined"), environment
                )

    def test_parallel_actor_audio_uses_one_manager_owned_runtime_lock(self) -> None:
        header = SOUND_MANAGER_HEADER_PATH.read_text(encoding="utf-8")
        source = SOUND_MANAGER_SOURCE_PATH.read_text(encoding="utf-8")
        self.assertIn("#include <mutex>", header)
        self.assertIn("std::recursive_mutex m_runtime_mutex;", header)

        guarded_signatures = (
            "void SoundScriptManager::trigOnce(int actor_id",
            "void SoundScriptManager::trigStart(int actor_id",
            "void SoundScriptManager::trigStop(int actor_id",
            "void SoundScriptManager::trigKill(int actor_id",
            "void SoundScriptManager::trigToggle(int actor_id",
            "bool SoundScriptManager::getTrigState(int actor_id",
            "void SoundScriptManager::modulate(int actor_id",
            "void SoundScriptManager::update(float dt)",
            "void SoundScriptManager::SetListener(Vector3 position",
            "SoundScriptInstancePtr SoundScriptManager::createInstance(",
            "void SoundScriptManager::removeInstance(",
            "void SoundScriptManager::setEnabled(bool state)",
        )
        lock = (
            "std::lock_guard<std::recursive_mutex> "
            "runtime_lock(m_runtime_mutex);"
        )
        for signature in guarded_signatures:
            with self.subTest(signature=signature):
                method_start = source.index(signature)
                body_start = source.index("{", method_start)
                self.assertIn(lock, source[body_start : body_start + 180])
        self.assertEqual(source.count(lock), len(guarded_signatures))

    def test_timeout_diagnostics_retain_live_product_logs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            logs = root / "live-logs"
            diagnostics = root / "diagnostics"
            logs.mkdir()
            diagnostics.mkdir()
            (logs / "RoR.log").write_text("engine-live\n", encoding="utf-8")
            (logs / "Angelscript.log").write_text(
                "script-live\n", encoding="utf-8"
            )

            GATE.capture_runtime_diagnostics(
                {"logs": logs},
                diagnostics,
                ("/tmp/RoR-Combined", "-map", "simple2.terrn2"),
                "captured-output\n",
                None,
                "command exceeded 900 seconds",
            )

            process = json.loads(
                (diagnostics / "process-result.json").read_text(
                    encoding="utf-8"
                )
            )
            self.assertIsNone(process["returncode"])
            self.assertEqual(
                process["failure"], "command exceeded 900 seconds"
            )
            self.assertEqual(
                (diagnostics / "RoR.log").read_text(encoding="utf-8"),
                "engine-live\n",
            )
            self.assertEqual(
                (diagnostics / "Angelscript.log").read_text(
                    encoding="utf-8"
                ),
                "script-live\n",
            )

    def test_product_command_fails_fast_on_scenario_compile_error(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            script_log = root / "Angelscript.log"
            stdout_path = root / "stdout.log"
            script_log.write_text(
                f"{GATE.SCRIPT_MEMBER} (49, 1): Error = "
                "Identifier 'Timer' is not a data type in global namespace\n",
                encoding="utf-8",
            )
            started = time.monotonic()
            with self.assertRaisesRegex(
                GATE.support.SoakFailure,
                "AngelScript rejected the exact TSan scenario",
            ):
                GATE.run_product_command(
                    (
                        sys.executable,
                        "-c",
                        "import time; time.sleep(10)",
                    ),
                    5,
                    cwd=root,
                    environment=GATE.os.environ,
                    script_log=script_log,
                    stdout_path=stdout_path,
                )
            self.assertLess(time.monotonic() - started, 3.0)

    def test_command_and_script_stay_in_combined_game_path(self) -> None:
        command = GATE.build_command(Path("/tmp/RoR-Combined"))
        self.assertIn("-map", command)
        self.assertIn("-runscript", command)
        self.assertNotIn("-truck", command)
        self.assertNotIn("--native-visual-showcase", command)
        script = (REPOSITORY_ROOT / GATE.SCRIPT_RELATIVE).read_text(
            encoding="utf-8"
        )
        for token in (
            "TARGET_SECONDS = 600.0f",
            "MINIMUM_COMPLETED_CYCLES = 10",
            "Ogre::Timer gSoakWallClock",
            "gSoakWallClock.getMilliseconds()",
            "gCompletedCycles >= MINIMUM_COMPLETED_CYCLES",
            "completed >= MINIMUM_TOTAL_PHYSICS_STEPS",
            'FailSoak("mutation-receipt-drift")',
            "MSG_SIM_DELETE_ACTOR_REQUESTED",
            "trySetDeterministicImpactVelocity",
            "game.setTrucksForcedActive(true)",
            "gActorDeletes",
        ):
            self.assertIn(token, script)
        self.assertNotIn("gSoakStartTime = game.getTime()", script)

    def test_scope_does_not_claim_force_parity_or_runtime_package(self) -> None:
        source = TOOL_PATH.read_text(encoding="utf-8")
        self.assertIn(
            "sanitizer-evidence-not-qualified-runtime-package", source
        )
        self.assertIn('"duration_clock": "ogre-monotonic-timer"', source)
        self.assertIn(
            "continuous-clean-room-multi-actor-contact-and-lifetime-mutation",
            source,
        )
        self.assertIn("not BeamNG.drive force parity", source)
        self.assertNotIn("urlopen", source)

    def test_workflow_builds_combined_game_and_runs_unsuppressed_soak(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        for token in (
            "workflow_dispatch:",
            "-DCMAKE_BUILD_TYPE=Release",
            "-DROR_OGRE_NEXT_COMBINED_RUNTIME=ON",
            "--target RoR-Combined",
            "-fsanitize=thread",
            "--require-tsan",
            "--timeout 1200",
            "elapsed_seconds\", 0) < 600",
            "legacy_visible_fallback\": False",
            "sanitizer-evidence-not-qualified-runtime-package",
            "include-hidden-files: true",
            '"lp_num_threads": 0',
            (
                "Render packaged Simple2 and semi through four Ogre-Next "
                "workers under ThreadSanitizer"
            ),
            "tools/run_playable_performance_scene.py",
            "ci.ogre-next-combined.tsan-packaged-simple2-semi",
            "ror-combined-packaged-tsan-report",
            'workers.get("requested") != 4',
            'ownership.get("visible_render_system") != "ogre-next-vulkan"',
        ):
            self.assertIn(token, workflow)
        self.assertNotIn("--native-visual-showcase", workflow)
        self.assertNotIn("suppressions=", workflow)
        self.assertNotIn("ror_ogre_next_combined_verified", workflow)

        for trigger in (
            "content-source/native_render/**",
            "doc/nextgen/FORWARD_NATIVE_ASSET_LEDGER.md",
            "resources/nextgen/native/**",
            "source/main/CMakeLists.txt",
            "source/main/gfx/render/NativeRenderAssetPackage.cpp",
            "source/main/gfx/render/NativeRenderAssetPackage.h",
            "source/main/gfx/render/NativeVisualShowcaseSceneSource.cpp",
            "source/main/gfx/render/NativeVisualShowcaseSceneSource.h",
        ):
            with self.subTest(trigger=trigger):
                self.assertIn(f"- {trigger}", workflow)

        source = TOOL_PATH.read_text(encoding="utf-8")
        process_receipt = source.index('diagnostics / "process-result.json"')
        required_script_log = source.index(
            'layout["logs"] / "Angelscript.log", "AngelScript log"'
        )
        self.assertLess(process_receipt, required_script_log)
        self.assertIn("shutil.copy2(report_path", source)


if __name__ == "__main__":
    unittest.main()
