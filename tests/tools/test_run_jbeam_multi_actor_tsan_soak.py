#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import importlib.util
from pathlib import Path
from unittest import mock
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOLS_ROOT = REPOSITORY_ROOT / "tools"
TOOL_PATH = TOOLS_ROOT / "run_jbeam_multi_actor_tsan_soak.py"
WORKFLOW_PATH = (
    REPOSITORY_ROOT / ".github/workflows/ogre-next-combined-tsan.yml"
)

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
            *GATE.PRESENTATION_MARKERS,
        )
    )


class JBeamMultiActorTSanSoakTests(unittest.TestCase):
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
            "TSAN_OPTIONS": (
                "halt_on_error=1:exitcode=66:history_size=7:"
                "second_deadlock_stack=1:log_path=/tmp/ror-tsan"
            )
        }
        dynamic = "Shared library: [libtsan.so.2]\n"
        symbols = "                 U __tsan_init\n"
        with (
            mock.patch.object(GATE.sys, "platform", "linux"),
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
            "MSG_SIM_DELETE_ACTOR_REQUESTED",
            "trySetDeterministicImpactVelocity",
            "game.setTrucksForcedActive(true)",
            "gActorDeletes",
        ):
            self.assertIn(token, script)

    def test_scope_does_not_claim_force_parity_or_runtime_package(self) -> None:
        source = TOOL_PATH.read_text(encoding="utf-8")
        self.assertIn(
            "sanitizer-evidence-not-qualified-runtime-package", source
        )
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
            "--timeout 900",
            "elapsed_seconds\", 0) < 600",
            "legacy_visible_fallback\": False",
            "sanitizer-evidence-not-qualified-runtime-package",
        ):
            self.assertIn(token, workflow)
        self.assertNotIn("--native-visual-showcase", workflow)
        self.assertNotIn("suppressions=", workflow)
        self.assertNotIn("ror_ogre_next_combined_verified", workflow)


if __name__ == "__main__":
    unittest.main()
