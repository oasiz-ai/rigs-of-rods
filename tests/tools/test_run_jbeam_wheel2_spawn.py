#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import copy
import importlib.util
import io
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
import zipfile


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOLS_ROOT = REPOSITORY_ROOT / "tools"
TOOL_PATH = TOOLS_ROOT / "run_jbeam_wheel2_spawn.py"
ACTOR_HEADER = REPOSITORY_ROOT / "source/main/physics/Actor.h"
ACTOR_BINDING = (
    REPOSITORY_ROOT / "source/main/scripting/bindings/ActorAngelscript.cpp"
)
WHEEL_PLAN = (
    REPOSITORY_ROOT
    / "source/main/resources/beamng/JBeamWheel2Approximation.cpp"
)
RIGDEF_ADAPTER = (
    REPOSITORY_ROOT / "source/main/resources/beamng/JBeamToRigDef.cpp"
)

sys.path.insert(0, str(TOOLS_ROOT))
SPEC = importlib.util.spec_from_file_location("run_jbeam_wheel2_spawn", TOOL_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load JBeam Wheel2 spawn tool")
WHEEL2 = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(WHEEL2)


def comparison(
    left: Path,
    right: Path,
    left_workers: int = 1,
    right_workers: int = 8,
    *,
    steps: int = WHEEL2.EXPECTED_STEPS,
) -> dict[str, object]:
    def side(path: Path, workers: int) -> dict[str, object]:
        return {
            "error": {"byte_offset": 0, "code": "none", "step_index": 0},
            "label": str(path),
            "metadata": {
                "digest_schema_version": 3,
                "first_physics_step": 0,
                "physics_flags": 1,
                "physics_step_denominator": 2000,
                "physics_step_numerator": 1,
                "scenario_id": WHEEL2.SCENARIO_ID,
                "worker_count": workers,
            },
            "step": None,
        }

    return {
        "difference": "none",
        "first_divergent_step": None,
        "format": "ror-d0-state-trace-comparison-v2",
        "left": side(left, left_workers),
        "metadata_field": "none",
        "right": side(right, right_workers),
        "status": "match",
        "steps_compared": steps,
    }


def inspection(
    trace: Path,
    workers: int = 1,
    *,
    contact_count: int = 0,
) -> dict[str, object]:
    if contact_count:
        summary: dict[str, object] = {
            "contact_step_count": 10,
            "first_contact_physics_step": 100,
            "last_contact_physics_step": 109,
            "maximum_contact_count": contact_count,
            "total_contact_count": 10 * contact_count,
        }
    else:
        summary = {
            "contact_step_count": 0,
            "first_contact_physics_step": None,
            "last_contact_physics_step": None,
            "maximum_contact_count": 0,
            "total_contact_count": 0,
        }
    return {
        "bytes_read": trace.stat().st_size,
        "contact_summary": summary,
        "final_step": {
            "actor_count": 1,
            "contact_count": contact_count,
            "input_digest": None,
            "physics_step": WHEEL2.EXPECTED_STEPS - 1,
            "state_digest": "ab" * 32,
        },
        "format": "ror-d0-state-trace-inspection-v2",
        "has_final_step": True,
        "metadata": {
            "first_physics_step": 0,
            "physics_flags": 1,
            "physics_step_denominator": 2000,
            "physics_step_numerator": 1,
            "scenario_id": WHEEL2.SCENARIO_ID,
            "state_digest_schema_version": 3,
            "worker_count": workers,
        },
        "path": str(trace),
        "status": "valid",
        "step_count": WHEEL2.EXPECTED_STEPS,
    }


def pass_receipt(**overrides: object) -> str:
    values: dict[str, object] = {
        "total_mass": "6.90000000000000000e+01",
        "minimum_node_mass": "2.47500002384185791e-01",
        "center_drop": "7.50000000000000000e-01",
        "final_center_speed": "1.00000000000000002e-03",
        "final_node_speed": "2.00000000000000004e-03",
        "maximum_position": "5.13000000000000000e+02",
        "maximum_velocity": "4.10000000000000000e+00",
        "broken": 0,
    }
    values.update(overrides)
    return (
        "[RoR|J3|Wheel2Spawn] PASS actors=1 nodes=73 beams=392 "
        "structural_nodes=9 rim_nodes=32 tire_nodes=32 "
        "wheel_tire_nodes=32 contacters=32 ground_contact_nodes=73 "
        "cab_triangles=0 collision_cab_triangles=0 steps=20000 "
        "audit_stride=100 audit_samples=201 "
        f"total_mass={values['total_mass']} "
        f"minimum_node_mass={values['minimum_node_mass']} "
        f"sampled_center_drop={values['center_drop']} "
        f"final_center_speed={values['final_center_speed']} "
        f"final_maximum_node_speed={values['final_node_speed']} "
        f"maximum_sampled_abs_position={values['maximum_position']} "
        f"maximum_sampled_abs_velocity={values['maximum_velocity']} "
        f"broken_beams={values['broken']} "
        "pressure_volume=false friction=false braking=false "
        "propulsion=false steering=false rolling=false driveability=false "
        "source_parity=false playability=false gravity_response=false "
        "contact_behavior=false per_step_numeric_bounds=false"
    )


def engine_log(archive_sha256: str) -> str:
    return "\n".join(
        (
            "[RoR|ModCache|JBeam] Mounted exact archive",
            f"archive_sha256={archive_sha256}",
            "roots=1",
            "[RoR|Determinism] Recording state trace",
            "scenario=2026082701",
            "limit=20000",
            "with 20000 fixed-step records (trace step limit reached)",
            "[RoR|ModCache|JBeam] Added exact root "
            "'ror_jbeam_wheel2_fixture' nodes=9, beams=8, hydros=0",
        )
    )


class JBeamWheel2SpawnTests(unittest.TestCase):
    def test_fixture_profile_matches_exact_sources_and_bounded_claims(self) -> None:
        profile, profile_bytes, jbeam, script = WHEEL2.read_profile(
            REPOSITORY_ROOT
        )
        self.assertEqual(
            profile["fixtureId"], "ror-jbeam-authenticated-wheel2-spawn-v1"
        )
        self.assertEqual(profile["expectedRuntime"], WHEEL2.EXPECTED_TOPOLOGY)
        self.assertEqual(
            profile["batchBoundaryStateEnvelope"],
            WHEEL2.EXPECTED_BATCH_BOUNDARY_ENVELOPE,
        )
        self.assertEqual(profile["qualifiedClaims"], WHEEL2.EXPECTED_CLAIMS)
        for excluded in (
            "pressureVolume",
            "friction",
            "braking",
            "propulsion",
            "steering",
            "rolling",
            "gravityResponse",
            "contactBehavior",
            "perStepNumericStateBounds",
            "settleBehavior",
            "driveability",
            "sourceEngineParity",
            "playability",
        ):
            self.assertIs(profile["qualifiedClaims"][excluded], False)
        self.assertEqual(
            profile["jbeamSource"]["sha256"], WHEEL2.sha256_bytes(jbeam)
        )
        self.assertEqual(
            profile["scenarioScript"]["sha256"],
            WHEEL2.sha256_bytes(script),
        )
        self.assertEqual(
            WHEEL2.sha256_bytes(profile_bytes),
            WHEEL2.sha256_bytes(
                (REPOSITORY_ROOT / WHEEL2.PROFILE_RELATIVE).read_bytes()
            ),
        )

    def test_profile_parser_rejects_duplicate_and_nonfinite_json(self) -> None:
        for source in (
            '{"schema":1,"schema":1}',
            '{"schema":NaN}',
            '{"schema":Infinity}',
            '{"schema":-Infinity}',
            '{"schema":1e10000}',
        ):
            with self.subTest(source=source):
                with self.assertRaises(WHEEL2.Wheel2SpawnFailure):
                    WHEEL2.decode_strict_json(source, "test document")

    def test_profile_nested_fields_require_exact_json_types(self) -> None:
        mutations = (
            lambda profile: profile["expectedRuntime"].update(
                fixedSteps=True
            ),
            lambda profile: profile["batchBoundaryStateEnvelope"].update(
                brokenBeams=False
            ),
            lambda profile: profile["qualifiedClaims"].update(
                playability=0
            ),
        )
        for mutate in mutations:
            with self.subTest(mutate=mutate):
                with tempfile.TemporaryDirectory() as directory:
                    root = Path(directory)
                    for relative in (
                        WHEEL2.PROFILE_RELATIVE,
                        WHEEL2.JBEAM_RELATIVE,
                        WHEEL2.SCRIPT_RELATIVE,
                    ):
                        destination = root / relative
                        destination.parent.mkdir(parents=True, exist_ok=True)
                        shutil.copyfile(REPOSITORY_ROOT / relative, destination)
                    profile_path = root / WHEEL2.PROFILE_RELATIVE
                    profile = json.loads(profile_path.read_text(encoding="utf-8"))
                    mutate(profile)
                    profile_path.write_text(
                        json.dumps(profile) + "\n", encoding="utf-8"
                    )
                    with self.assertRaises(WHEEL2.Wheel2SpawnFailure):
                        WHEEL2.read_profile(root)

    def test_direct_reader_rejects_oversized_nonregular_and_symlink(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            regular = root / "regular"
            regular.write_bytes(b"1234")
            with self.assertRaises(WHEEL2.Wheel2SpawnFailure):
                WHEEL2.read_direct_bytes(regular, "oversized", 3)
            with self.assertRaises(WHEEL2.Wheel2SpawnFailure):
                WHEEL2.read_direct_bytes(root, "directory", 100)
            indirect = root / "indirect"
            indirect.symlink_to(regular)
            with self.assertRaises(WHEEL2.Wheel2SpawnFailure):
                WHEEL2.read_direct_bytes(indirect, "symlink", 100)

    def test_profile_rejects_changed_source_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for relative in (
                WHEEL2.PROFILE_RELATIVE,
                WHEEL2.JBEAM_RELATIVE,
                WHEEL2.SCRIPT_RELATIVE,
            ):
                destination = root / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(REPOSITORY_ROOT / relative, destination)
            with (root / WHEEL2.JBEAM_RELATIVE).open(
                "ab"
            ) as stream:
                stream.write(b"\n// changed\n")
            with self.assertRaises(WHEEL2.Wheel2SpawnFailure):
                WHEEL2.read_profile(root)

    def test_exact_staging_is_exclusive_and_direct(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "input.json"
            digest = WHEEL2.stage_exact_bytes(path, b"{}\n", "test")
            self.assertEqual(digest, WHEEL2.sha256_bytes(b"{}\n"))
            self.assertFalse(path.is_symlink())
            with self.assertRaises(WHEEL2.Wheel2SpawnFailure):
                WHEEL2.stage_exact_bytes(path, b"{}\n", "test")

    def test_archive_is_deterministic_and_contains_only_jbeam(self) -> None:
        _, _, jbeam, _ = WHEEL2.read_profile(REPOSITORY_ROOT)
        first = WHEEL2.build_archive_bytes({WHEEL2.JBEAM_MEMBER: jbeam})
        second = WHEEL2.build_archive_bytes({WHEEL2.JBEAM_MEMBER: jbeam})
        self.assertEqual(first, second)
        with zipfile.ZipFile(io.BytesIO(first)) as archive:
            self.assertEqual(archive.namelist(), [WHEEL2.JBEAM_MEMBER])
            self.assertEqual(archive.read(WHEEL2.JBEAM_MEMBER), jbeam)

    def test_direct_copy_is_exclusive_and_rejects_indirect_paths(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            source.write_bytes(b"trace-bytes")
            destination = root / "destination"
            copied, digest = WHEEL2.copy_direct_file_exclusive(
                source, destination, "test trace"
            )
            self.assertEqual(copied, len(b"trace-bytes"))
            self.assertEqual(digest, WHEEL2.sha256_bytes(b"trace-bytes"))
            self.assertEqual(destination.read_bytes(), b"trace-bytes")
            with self.assertRaises(WHEEL2.Wheel2SpawnFailure):
                WHEEL2.copy_direct_file_exclusive(
                    source, root / "oversized-copy", "oversized source", 3
                )
            with self.assertRaises(WHEEL2.Wheel2SpawnFailure):
                WHEEL2.copy_direct_file_exclusive(
                    source, destination, "existing destination"
                )
            indirect = root / "indirect"
            indirect.symlink_to(source)
            with self.assertRaises(WHEEL2.Wheel2SpawnFailure):
                WHEEL2.copy_direct_file_exclusive(
                    indirect, root / "indirect-copy", "indirect source"
                )
            symlink_destination = root / "symlink-destination"
            symlink_destination.symlink_to(root / "outside")
            with self.assertRaises(WHEEL2.Wheel2SpawnFailure):
                WHEEL2.copy_direct_file_exclusive(
                    source, symlink_destination, "indirect destination"
                )

    def test_log_gate_requires_product_topology_and_false_limitations(self) -> None:
        archive_sha = "cd" * 32
        script_log = "\n".join(
            (WHEEL2.START_MARKER, WHEEL2.ARM_MARKER, pass_receipt())
        )
        telemetry = WHEEL2.validate_logs(
            0,
            "",
            engine_log(archive_sha),
            script_log,
            archive_sha,
            True,
            WHEEL2.EXPECTED_BATCH_BOUNDARY_ENVELOPE,
        )
        self.assertEqual(telemetry["broken_beams"], 0)
        self.assertGreater(telemetry["total_mass_kg"], 0)
        self.assertGreater(telemetry["minimum_node_mass_kg"], 0)

        hostiles = (
            script_log.replace("rim_nodes=32", "rim_nodes=31", 1),
            script_log.replace("pressure_volume=false", "pressure_volume=true"),
            script_log.replace("gravity_response=false", "gravity_response=true"),
            script_log.replace(
                WHEEL2.START_MARKER,
                WHEEL2.START_MARKER + " gravity_response=true",
            ),
            script_log + "\n" + WHEEL2.ARM_MARKER,
            script_log + " playability=true",
            script_log + "\n" + pass_receipt(),
            script_log.replace(WHEEL2.ARM_MARKER, ""),
        )
        for hostile in hostiles:
            with self.subTest(hostile=hostile[-100:]):
                with self.assertRaises(WHEEL2.Wheel2SpawnFailure):
                    WHEEL2.validate_logs(
                        0,
                        "",
                        engine_log(archive_sha),
                        hostile,
                        archive_sha,
                        True,
                        WHEEL2.EXPECTED_BATCH_BOUNDARY_ENVELOPE,
                    )

    def test_log_gate_rejects_nonfinite_or_out_of_envelope_values(self) -> None:
        archive_sha = "ef" * 32
        for receipt in (
            pass_receipt(total_mass="nan"),
            pass_receipt(minimum_node_mass="0"),
            pass_receipt(center_drop="101"),
            pass_receipt(final_center_speed="10001"),
            pass_receipt(final_node_speed="10001"),
            pass_receipt(maximum_position="1000001"),
            pass_receipt(maximum_velocity="10001"),
            pass_receipt(total_mass="1", minimum_node_mass="1"),
            pass_receipt(final_center_speed="3", final_node_speed="2"),
            pass_receipt(final_node_speed="5", maximum_velocity="4"),
            pass_receipt(broken=1),
        ):
            with self.subTest(receipt=receipt[-150:]):
                with self.assertRaises(WHEEL2.Wheel2SpawnFailure):
                    WHEEL2.validate_logs(
                        0,
                        "",
                        engine_log(archive_sha),
                        "\n".join(
                            (WHEEL2.START_MARKER, WHEEL2.ARM_MARKER, receipt)
                        ),
                        archive_sha,
                        True,
                        WHEEL2.EXPECTED_BATCH_BOUNDARY_ENVELOPE,
                    )

    def test_trace_comparison_requires_exact_schema_and_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            left = root / "left.rortrace"
            right = root / "right.rortrace"
            left.write_bytes(b"left")
            right.write_bytes(b"right")
            payload = comparison(left, right)
            accepted = WHEEL2.validate_trace_comparison(
                payload, left, right, 1, 8
            )
            self.assertEqual(accepted["steps_compared"], WHEEL2.EXPECTED_STEPS)
            for mutate in (
                lambda candidate: candidate.update(extra=True),
                lambda candidate: candidate.update(steps_compared=True),
                lambda candidate: candidate["right"]["metadata"].update(
                    worker_count=1
                ),
                lambda candidate: candidate["left"]["error"].update(
                    code="truncated"
                ),
            ):
                candidate = copy.deepcopy(payload)
                mutate(candidate)
                with self.assertRaises(WHEEL2.Wheel2SpawnFailure):
                    WHEEL2.validate_trace_comparison(
                        candidate, left, right, 1, 8
                    )

    def test_trace_inspection_accepts_exact_empty_or_nonempty_summary(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            trace = Path(directory) / "trace.rortrace"
            trace.write_bytes(b"trace-bytes")
            for contacts in (0, 4):
                payload = inspection(trace, contact_count=contacts)
                accepted = WHEEL2.validate_trace_inspection(payload, trace, 1)
                self.assertEqual(
                    accepted["final_step"]["contact_count"], contacts
                )
            hostile = inspection(trace)
            hostile["final_step"]["actor_count"] = True
            with self.assertRaises(WHEEL2.Wheel2SpawnFailure):
                WHEEL2.validate_trace_inspection(hostile, trace, 1)
            hostile = inspection(trace, contact_count=4)
            hostile["contact_summary"]["last_contact_physics_step"] = 20000
            with self.assertRaises(WHEEL2.Wheel2SpawnFailure):
                WHEEL2.validate_trace_inspection(hostile, trace, 1)

    def test_command_and_qualification_workers_are_exact(self) -> None:
        command = WHEEL2.build_command(Path("/tmp/RoR"))
        self.assertEqual(
            command[-8:],
            (
                "-checkcache",
                "-map",
                WHEEL2.TERRAIN,
                "-truck",
                WHEEL2.VEHICLE,
                "-enter",
                "-runscript",
                WHEEL2.SCRIPT_MEMBER,
            ),
        )
        with self.assertRaises(SystemExit):
            WHEEL2.parse_args(
                [
                    "--executable",
                    "/tmp/RoR",
                    "--trace-tool",
                    "/tmp/trace",
                    "--artifact-dir",
                    "/tmp/artifacts",
                    "--workers",
                    "1",
                ]
            )

    def test_native_topology_and_script_audit_are_closed(self) -> None:
        script = (REPOSITORY_ROOT / WHEEL2.SCRIPT_RELATIVE).read_text(
            encoding="utf-8"
        )
        header = ACTOR_HEADER.read_text(encoding="utf-8")
        binding = ACTOR_BINDING.read_text(encoding="utf-8")
        plan = WHEEL_PLAN.read_text(encoding="utf-8")
        adapter = RIGDEF_ADAPTER.read_text(encoding="utf-8")
        for method in (
            "getNodeCount",
            "getBeamCount",
            "getBrokenBeamCount",
            "getWheelNodeCount",
            "isNodeWheelRim",
            "isNodeWheelTire",
            "getContacterCount",
            "getGroundContactEnabledNodeCount",
            "trySetDeterministicImpactPlacementAndVelocity",
        ):
            self.assertIn(method, script)
            self.assertIn(method, header)
            self.assertIn(method, binding)
        self.assertIn(
            "TryMultiply(source.num_rays, 4U, generated_nodes)", plan
        )
        self.assertIn(
            "TryMultiply(source.num_rays, 24U, generated_beams)", plan
        )
        self.assertIn("wheel.braking = RoR::WheelBraking::NONE", adapter)
        self.assertIn("wheel.propulsion = RoR::WheelPropulsion::NONE", adapter)
        self.assertIn("game.setTrucksForcedActive(true)", script)
        self.assertGreaterEqual(
            script.count("game.setTrucksForcedActive(false)"), 2
        )
        self.assertIn("actor.clearEventSimulatedValues()", script)
        self.assertIn("EXPECTED_AUDIT_STRIDE = 100", script)
        self.assertIn("EXPECTED_AUDIT_SAMPLES = 201", script)

    def test_runner_report_and_file_policies_are_fail_closed(self) -> None:
        source = TOOL_PATH.read_text(encoding="utf-8")
        for required in (
            "O_NOFOLLOW",
            "O_EXCL",
            'allow_nan=False',
            '"ror-j3-authenticated-jbeam-wheel2-spawn-v1"',
            'tuple(args.workers) != (1, 8)',
            '"source_inventory"',
            '"qualified_claims"',
            '"batch_boundary_state_envelope"',
            '"runtime_topology"',
        ):
            self.assertIn(required, source)
        self.assertNotIn("assert ", source)
        self.assertNotIn("shutil.copy2", source)
        self.assertNotIn(".write_text(", source)
        self.assertNotIn("package_support.write_archive", source)


if __name__ == "__main__":
    unittest.main()
