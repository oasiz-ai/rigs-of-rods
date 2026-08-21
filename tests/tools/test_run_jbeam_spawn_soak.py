#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOLS_ROOT = REPOSITORY_ROOT / "tools"
TOOL_PATH = TOOLS_ROOT / "run_jbeam_spawn_soak.py"
ACTOR_HEADER = REPOSITORY_ROOT / "source/main/physics/Actor.h"
ACTOR_BINDING = (
    REPOSITORY_ROOT
    / "source/main/scripting/bindings/ActorAngelscript.cpp"
)

sys.path.insert(0, str(TOOLS_ROOT))
SPEC = importlib.util.spec_from_file_location("run_jbeam_spawn_soak", TOOL_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load JBeam spawn-soak tool")
SOAK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SOAK)


def comparison(
    left_workers: int = 1,
    right_workers: int = 8,
    *,
    status: str = "match",
    steps: int = SOAK.EXPECTED_STEPS,
) -> dict[str, object]:
    def side(workers: int) -> dict[str, object]:
        return {
            "metadata": {
                "worker_count": workers,
                "scenario_id": SOAK.SCENARIO_ID,
                "first_physics_step": 0,
                "physics_step_numerator": 1,
                "physics_step_denominator": 2000,
            }
        }

    return {
        "format": "ror-d0-state-trace-comparison-v2",
        "status": status,
        "steps_compared": steps,
        "left": side(left_workers),
        "right": side(right_workers),
    }


class JBeamSpawnSoakTests(unittest.TestCase):
    def test_fixture_profile_matches_exact_sources(self) -> None:
        profile, jbeam, script = SOAK.read_profile(REPOSITORY_ROOT)
        self.assertEqual(
            profile["fixtureId"], "ror-jbeam-authenticated-spawn-soak-v6"
        )
        self.assertEqual(profile["authorship"], "original-clean-room")
        self.assertEqual(profile["execution"], "authenticated-product-path")
        self.assertEqual(profile["expectedRuntime"]["nodes"], 6)
        self.assertEqual(profile["expectedRuntime"]["runtimeBeams"], 16)
        self.assertEqual(profile["expectedRuntime"]["cabTriangles"], 5)
        self.assertEqual(
            profile["expectedRuntime"]["collisionCabTriangles"], 5
        )
        self.assertEqual(profile["expectedRuntime"]["contacters"], 0)
        self.assertEqual(
            profile["expectedRuntime"]["groundContactNodes"], 6
        )
        self.assertEqual(profile["expectedRuntime"]["jbeamHydros"], 1)
        self.assertEqual(
            profile["expectedRuntime"]["jbeamSupportBeams"], 1
        )
        self.assertEqual(profile["expectedRuntime"]["nodeMassKg"], 20)
        self.assertEqual(profile["expectedRuntime"]["totalMassKg"], 120)
        self.assertEqual(profile["expectedRuntime"]["fixedSteps"], 120000)
        self.assertEqual(profile["expectedRuntime"]["impactTranslationY"], 2)
        self.assertEqual(profile["expectedRuntime"]["impactVelocityY"], -4)
        self.assertEqual(
            profile["jbeamSource"]["sha256"], SOAK.sha256_bytes(jbeam)
        )
        self.assertEqual(
            profile["scenarioScript"]["sha256"], SOAK.sha256_bytes(script)
        )

    def test_archive_is_deterministic_and_contains_only_jbeam(self) -> None:
        _, jbeam, _ = SOAK.read_profile(REPOSITORY_ROOT)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first = root / "first.zip"
            second = root / "second.zip"
            first_sha = SOAK.write_archive(
                first, {SOAK.JBEAM_MEMBER: jbeam}
            )
            second_sha = SOAK.write_archive(
                second, {SOAK.JBEAM_MEMBER: jbeam}
            )
            self.assertEqual(first_sha, second_sha)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            with zipfile.ZipFile(first) as archive:
                self.assertEqual(archive.namelist(), [SOAK.JBEAM_MEMBER])
                self.assertEqual(archive.read(SOAK.JBEAM_MEMBER), jbeam)

    def test_log_gate_requires_product_and_physics_receipts(self) -> None:
        pass_receipt = (
            "[RoR|J2|SpawnSoak] PASS actors=1 nodes=6 beams=16 "
            "cab_triangles=5 collision_cab_triangles=5 contacters=0 "
            "ground_contact_nodes=6 "
            "hydros=1 support_beams=1 total_mass=120 steps=120000 "
            "hydro_steps=120000 "
            "support_steps=120000 support_compression_steps=73000 "
            "max_abs_position=5.13e2 max_abs_velocity=4.1 "
            "minimum_com_drop=2.0 peak_com_speed=4.1 broken_beams=0"
        )
        script_log = "\n".join(
            (SOAK.START_MARKER, SOAK.ARM_MARKER, pass_receipt)
        )
        archive_sha = "ab" * 32
        engine_log = "\n".join(
            (
                "[RoR|ModCache|JBeam] Mounted exact archive",
                f"archive_sha256={archive_sha}",
                "roots=1",
                "[RoR|Determinism] Recording state trace",
                "scenario=2026082105",
                "limit=120000",
                "with 120000 fixed-step records (trace step limit reached)",
                "[RoR|ModCache|JBeam] Added exact root "
                "'ror_jbeam_spawn_fixture' nodes=6, beams=15, hydros=1",
            )
        )
        telemetry = SOAK.validate_logs(
            0, "", engine_log, script_log, archive_sha, True
        )
        self.assertEqual(telemetry["max_abs_position"], 513.0)
        self.assertEqual(telemetry["broken_beams"], 0)
        self.assertEqual(telemetry["minimum_com_drop"], 2.0)
        self.assertEqual(telemetry["support_accepted_steps"], 120000)
        self.assertEqual(telemetry["support_compression_steps"], 73000)

        for bad in (
            script_log.replace("hydro_steps=120000", "hydro_steps=119999"),
            script_log + "\n" + pass_receipt,
            script_log.replace(SOAK.ARM_MARKER, ""),
        ):
            with self.subTest(bad=bad[-80:]):
                with self.assertRaises(SOAK.SpawnSoakFailure):
                    SOAK.validate_logs(
                        0, "", engine_log, bad, archive_sha, True
                    )
        with self.assertRaises(SOAK.SpawnSoakFailure):
            SOAK.validate_logs(
                0,
                "OGRE EXCEPTION",
                engine_log,
                script_log,
                archive_sha,
                True,
            )

    def test_trace_comparison_requires_all_steps_and_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            tool = root / "trace-tool.py"
            left = root / "left.rortrace"
            right = root / "right.rortrace"
            left.write_bytes(b"left")
            right.write_bytes(b"right")
            tool.write_text(
                "#!/usr/bin/env python3\n"
                "import json\n"
                f"print(json.dumps({comparison()!r}))\n",
                encoding="utf-8",
            )
            tool.chmod(0o755)
            result = SOAK.compare_traces(tool, left, right, 1, 8, 10)
            self.assertEqual(result["steps_compared"], 120000)

            tool.write_text(
                "#!/usr/bin/env python3\n"
                "import json\n"
                f"print(json.dumps({comparison(steps=119999)!r}))\n",
                encoding="utf-8",
            )
            with self.assertRaises(SOAK.SpawnSoakFailure):
                SOAK.compare_traces(tool, left, right, 1, 8, 10)

    def test_script_and_native_hydro_audit_are_closed(self) -> None:
        script = (REPOSITORY_ROOT / SOAK.SCRIPT_RELATIVE).read_text(
            encoding="utf-8"
        )
        header = ACTOR_HEADER.read_text(encoding="utf-8")
        binding = ACTOR_BINDING.read_text(encoding="utf-8")
        for method in (
            "getJBeamHydroRuntimeCount",
            "getJBeamHydroRuntimeFaultCount",
            "hasFiniteJBeamHydroRuntimeState",
            "getJBeamHydroMinimumAcceptedStepCount",
            "getJBeamHydroMaximumAcceptedStepCount",
            "getJBeamSupportRuntimeCount",
            "getJBeamSupportRuntimeFaultCount",
            "hasFiniteJBeamSupportRuntimeState",
            "getJBeamSupportMinimumAcceptedStepCount",
            "getJBeamSupportMaximumAcceptedStepCount",
            "getJBeamSupportMinimumCompressionStepCount",
            "getBeamCount",
            "getCabTriangleCount",
            "getCollisionCabTriangleCount",
            "getContacterCount",
            "getGroundContactEnabledNodeCount",
            "trySetDeterministicImpactPlacementAndVelocity",
        ):
            self.assertIn(method, script)
            self.assertIn(method, header)
            self.assertIn(method, binding)
        self.assertIn("game.setTrucksForcedActive(true)", script)
        self.assertGreaterEqual(
            script.count("game.setTrucksForcedActive(false)"), 2
        )
        self.assertIn("actor.clearEventSimulatedValues()", script)
        self.assertIn("gObservedTerrainImpactResponse", script)

    def test_command_and_report_scope_remain_bounded(self) -> None:
        command = SOAK.build_command(Path("/tmp/RoR"))
        self.assertEqual(
            command[-8:],
            (
                "-checkcache",
                "-map",
                SOAK.TERRAIN,
                "-truck",
                SOAK.VEHICLE,
                "-enter",
                "-runscript",
                SOAK.SCRIPT_MEMBER,
            ),
        )
        source = TOOL_PATH.read_text(encoding="utf-8")
        self.assertIn(
            "clean-room-structural-hydro-support-normaltype-product-path",
            source,
        )
        self.assertIn("ror-j2-authenticated-jbeam-spawn-soak-v6", source)
        self.assertIn("--allow-worker-count-difference", source)
        self.assertIn('"state_comparisons": state_comparisons', source)
        self.assertNotIn("urlopen", source)
        self.assertNotIn("requests.", source)


if __name__ == "__main__":
    unittest.main()
