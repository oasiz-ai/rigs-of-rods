#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOLS_ROOT = REPOSITORY_ROOT / "tools"
TOOL_PATH = TOOLS_ROOT / "run_jbeam_inter_actor_collision.py"

import sys

sys.path.insert(0, str(TOOLS_ROOT))
SPEC = importlib.util.spec_from_file_location("jbeam_collision", TOOL_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load JBeam collision tool")
GATE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GATE)


def inspection(
    workers: int = 1,
    *,
    total_contacts: int = 343,
    first_contact: int | None = 0,
) -> dict[str, object]:
    contact_steps = 299 if total_contacts > 0 else 0
    maximum = 20 if total_contacts > 0 else 0
    last_contact = 1999 if total_contacts > 0 else None
    return {
        "format": "ror-d0-state-trace-inspection-v2",
        "status": "valid",
        "metadata": {
            "state_digest_schema_version":
                GATE.EXPECTED_STATE_DIGEST_SCHEMA_VERSION,
            "worker_count": workers,
            "scenario_id": GATE.SCENARIO_ID,
            "first_physics_step": 0,
            "physics_step_numerator": 1,
            "physics_step_denominator": 2000,
        },
        "step_count": GATE.EXPECTED_STEPS,
        "bytes_read": 192136,
        "contact_summary": {
            "total_contact_count": total_contacts,
            "contact_step_count": contact_steps,
            "maximum_contact_count": maximum,
            "first_contact_physics_step": first_contact,
            "last_contact_physics_step": last_contact,
        },
        "has_final_step": True,
        "final_step": {
            "physics_step": GATE.EXPECTED_STEPS - 1,
            "actor_count": GATE.EXPECTED_ACTORS,
            "contact_count": 1 if total_contacts > 0 else 0,
            "state_digest": "ab" * 32,
        },
    }


class JBeamInterActorCollisionTests(unittest.TestCase):
    def test_profile_binds_exact_sources_and_topology(self) -> None:
        profile, jbeam, script = GATE.read_profile(REPOSITORY_ROOT)
        self.assertEqual(
            profile["fixtureId"],
            "ror-jbeam-authenticated-inter-actor-collision-v1",
        )
        expected = profile["expectedRuntime"]
        self.assertEqual(expected["actors"], 2)
        self.assertEqual(expected["collisionCabsPerActor"], 5)
        self.assertEqual(expected["contactersPerActor"], 0)
        self.assertEqual(expected["fixedSteps"], 2000)
        self.assertEqual(
            profile["jbeamSource"]["sha256"], GATE.sha256_bytes(jbeam)
        )
        self.assertEqual(
            profile["scenarioScript"]["sha256"], GATE.sha256_bytes(script)
        )

    def test_log_gate_requires_contact_response_receipt(self) -> None:
        archive_sha = "ab" * 32
        pass_receipt = (
            "[RoR|J2|InterActorCollision] PASS actors=2 nodes=12 "
            "beams=32 cab_triangles=10 collision_cabs=10 contacters=0 "
            "hydros=2 steps=2000 maximum_relative_velocity_change=7.2 "
            "maximum_vertical_separation=0.6 broken_beams=0"
        )
        script_log = "\n".join(
            (GATE.START_MARKER, GATE.ARM_MARKER, pass_receipt)
        )
        engine_log = "\n".join(
            (
                "[RoR|ModCache|JBeam] Mounted exact archive",
                f"archive_sha256={archive_sha}",
                "roots=1",
                "[RoR|Determinism] Recording state trace",
                "scenario=2026082106",
                "limit=2000",
                "with 2000 fixed-step records (trace step limit reached)",
                "[RoR|ModCache|JBeam] Added exact root "
                "'ror_jbeam_spawn_fixture' nodes=6, beams=15, hydros=1",
            )
        )
        telemetry = GATE.validate_logs(
            0, "", engine_log, script_log, archive_sha, True
        )
        self.assertEqual(telemetry["maximum_relative_velocity_change"], 7.2)
        self.assertEqual(telemetry["broken_beams"], 0)

        for bad in (
            script_log.replace("broken_beams=0", "broken_beams=1"),
            script_log.replace(GATE.ARM_MARKER, ""),
            script_log + "\n" + pass_receipt,
        ):
            with self.subTest(bad=bad[-80:]):
                with self.assertRaises(GATE.CollisionGateFailure):
                    GATE.validate_logs(
                        0, "", engine_log, bad, archive_sha, True
                    )

    def test_trace_inspection_requires_native_contact_summary(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            tool = root / "trace-tool.py"
            trace = root / "trace.rortrace"
            trace.write_bytes(b"trace")

            def write(payload: dict[str, object]) -> None:
                tool.write_text(
                    "#!/usr/bin/env python3\n"
                    "import json\n"
                    f"print(json.dumps({payload!r}))\n",
                    encoding="utf-8",
                )
                tool.chmod(0o755)

            write(inspection())
            result = GATE.inspect_trace(tool, trace, 1, 10)
            self.assertEqual(
                result["contact_summary"]["total_contact_count"], 343
            )

            write(inspection(total_contacts=0, first_contact=None))
            with self.assertRaises(GATE.CollisionGateFailure):
                GATE.inspect_trace(tool, trace, 1, 10)

            write(inspection(first_contact=1))
            with self.assertRaises(GATE.CollisionGateFailure):
                GATE.inspect_trace(tool, trace, 1, 10)

    def test_command_is_product_scene_without_cli_vehicle(self) -> None:
        command = GATE.build_command(Path("/tmp/RoR"))
        self.assertIn("-checkcache", command)
        self.assertIn("-map", command)
        self.assertIn(GATE.TERRAIN, command)
        self.assertIn("-runscript", command)
        self.assertIn(GATE.SCRIPT_MEMBER, command)
        self.assertNotIn("-truck", command)

    def test_script_owns_two_external_actors_and_exact_collision_mode(self) -> None:
        script = (REPOSITORY_ROOT / GATE.SCRIPT_RELATIVE).read_text(
            encoding="utf-8"
        )
        for token in (
            "LOWER_ACTOR_ID = 2101",
            "UPPER_ACTOR_ID = 2102",
            'console.cVarSet("sim_no_collisions", "false")',
            'console.cVarSet("sim_no_self_collisions", "false")',
            "getCollisionCabTriangleCount",
            "getContacterCount",
            "trySetDeterministicImpactVelocity",
            "gObservedCollisionResponse",
            "game.setTrucksForcedActive(true)",
        ):
            self.assertIn(token, script)
        self.assertGreaterEqual(
            script.count("game.setTrucksForcedActive(false)"), 2
        )

    def test_report_scope_does_not_claim_force_parity(self) -> None:
        source = TOOL_PATH.read_text(encoding="utf-8")
        self.assertIn(
            "clean-room-normaltype-native-inter-actor-contact-not-force-parity",
            source,
        )
        self.assertIn("contact_summary", source)
        self.assertIn("--allow-worker-count-difference", source)
        self.assertNotIn("urlopen", source)


if __name__ == "__main__":
    unittest.main()
