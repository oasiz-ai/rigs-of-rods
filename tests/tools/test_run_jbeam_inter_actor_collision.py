#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock


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


def conservation_receipt(
    *,
    contacts: int = 343,
    residual: str = "3.985739861966006e-08",
) -> str:
    return (
        "[RoR|Determinism|ContactConservation] PASS schema=2 "
        f"contacts={contacts} fixed_steps=2000 "
        f"maximum_normalized_linear_impulse_residual={residual} "
        "maximum_angular_impulse_delta_magnitude_nms=0.0075 "
        "summed_angular_impulse_delta_x_nms=-0.01 "
        "summed_angular_impulse_delta_y_nms=0.02 "
        "summed_angular_impulse_delta_z_nms=-0.03 "
        "summed_isolated_contact_work_j=-12.5 "
        "summed_isolated_contact_kinetic_energy_delta_j=-11.25 "
        "summed_isolated_contact_integration_energy_delta_j=1.25 "
        "whole_step_shared_node_energy=not_audited"
    )


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
        self.assertEqual(
            GATE.sha256_bytes((REPOSITORY_ROOT / GATE.PROFILE_RELATIVE).read_bytes()),
            "24d4d21a1a171f11f6bc970fc3dd4d3b885b5ddb7ebf2f145b90a642da416fea",
        )

    def test_runtime_authorities_must_be_direct_regular_files(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "RoR-Combined"
            executable.write_bytes(b"runtime")
            self.assertEqual(
                GATE.resolve_direct_file(executable, "executable"),
                executable.resolve(strict=True),
            )
            with mock.patch.object(Path, "is_symlink", return_value=True):
                with self.assertRaisesRegex(
                    GATE.CollisionGateFailure, "missing or indirect"
                ):
                    GATE.resolve_direct_file(executable, "executable")

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
                conservation_receipt(),
                "[RoR|ModCache|JBeam] Added exact root "
                "'ror_jbeam_spawn_fixture' nodes=6, beams=15, hydros=1",
            )
        )
        telemetry = GATE.validate_logs(
            0, "", engine_log, script_log, archive_sha, True
        )
        self.assertEqual(telemetry["maximum_relative_velocity_change"], 7.2)
        self.assertEqual(telemetry["broken_beams"], 0)
        self.assertEqual(
            telemetry["contact_conservation"]["contact_count"], 343
        )
        self.assertEqual(
            telemetry["contact_conservation"][
                "whole_step_shared_node_energy"
            ],
            "not_audited",
        )

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

        for bad_engine in (
            engine_log.replace(conservation_receipt(), ""),
            engine_log + "\n" + conservation_receipt(),
            engine_log.replace(
                conservation_receipt(),
                conservation_receipt(residual="1.000001e-6"),
            ),
            engine_log.replace(
                conservation_receipt(),
                conservation_receipt(residual="nan"),
            ),
            engine_log.replace(
                "summed_isolated_contact_kinetic_energy_delta_j=-11.25",
                "summed_isolated_contact_kinetic_energy_delta_j=999",
            ),
            engine_log.replace(
                "whole_step_shared_node_energy=not_audited",
                "whole_step_shared_node_energy=audited",
            ),
        ):
            with self.subTest(bad_engine=bad_engine[-120:]):
                with self.assertRaises(GATE.CollisionGateFailure):
                    GATE.validate_logs(
                        0, "", bad_engine, script_log, archive_sha, True
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
            telemetry = {
                "contact_conservation": {
                    "contact_count": 343,
                    "fixed_steps": GATE.EXPECTED_STEPS,
                }
            }
            self.assertEqual(
                GATE.bind_conservation_to_trace(telemetry, result)[
                    "contact_count"
                ],
                343,
            )
            telemetry["contact_conservation"]["contact_count"] = 342
            with self.assertRaises(GATE.CollisionGateFailure):
                GATE.bind_conservation_to_trace(telemetry, result)

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
            "clean-room-normaltype-native-inter-actor-contact-conservation-",
            source,
        )
        self.assertIn("not-beamng-force-parity", source)
        self.assertIn("CONTACT_CONSERVATION_PASS_PATTERN", source)
        self.assertIn("bind_conservation_to_trace", source)
        self.assertIn("contact_summary", source)
        self.assertIn("--allow-worker-count-difference", source)
        self.assertNotIn("urlopen", source)


if __name__ == "__main__":
    unittest.main()
