#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import copy
import importlib.util
import json
import math
from pathlib import Path
import shutil
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
    bytes_read: int = 192136,
    path: str = "trace.rortrace",
    total_contacts: int = 218,
    first_contact: int | None = 0,
) -> dict[str, object]:
    contact_steps = 178 if total_contacts > 0 else 0
    maximum = 20 if total_contacts > 0 else 0
    last_contact = 1994 if total_contacts > 0 else None
    return {
        "format": "ror-d0-state-trace-inspection-v2",
        "status": "valid",
        "path": path,
        "metadata": {
            "state_digest_schema_version": GATE.EXPECTED_STATE_DIGEST_SCHEMA_VERSION,
            "worker_count": workers,
            "scenario_id": GATE.SCENARIO_ID,
            "first_physics_step": 0,
            "physics_step_numerator": 1,
            "physics_step_denominator": 2000,
            "physics_flags": GATE.EXPECTED_PHYSICS_FLAGS,
        },
        "step_count": GATE.EXPECTED_STEPS,
        "bytes_read": bytes_read,
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
            "contact_count": 0,
            "input_digest": None,
            "state_digest": "ab" * 32,
        },
    }


def comparison(
    left: Path,
    right: Path,
    left_workers: int = 1,
    right_workers: int = 8,
) -> dict[str, object]:
    def side(path: Path, workers: int) -> dict[str, object]:
        return {
            "label": str(path),
            "metadata": {
                "digest_schema_version": GATE.EXPECTED_STATE_DIGEST_SCHEMA_VERSION,
                "worker_count": workers,
                "scenario_id": GATE.SCENARIO_ID,
                "first_physics_step": 0,
                "physics_step_numerator": 1,
                "physics_step_denominator": 2000,
                "physics_flags": GATE.EXPECTED_PHYSICS_FLAGS,
            },
            "step": None,
            "error": {"code": "none", "byte_offset": 0, "step_index": 0},
        }

    return {
        "format": "ror-d0-state-trace-comparison-v2",
        "status": "match",
        "difference": "none",
        "metadata_field": "none",
        "steps_compared": GATE.EXPECTED_STEPS,
        "first_divergent_step": None,
        "left": side(left, left_workers),
        "right": side(right, right_workers),
    }


def write_tool_output(path: Path, output: str) -> None:
    path.write_text(
        "#!/usr/bin/env python3\n" f"print({output!r})\n",
        encoding="utf-8",
    )
    path.chmod(0o755)


def contact_acceptance() -> dict[str, object]:
    return {
        "schema": 1,
        "contactCount": {"minimum": 128, "maximum": 256},
        "maximumRelativeVelocityChangeMps": {"minimum": 5, "maximum": 8},
        "maximumVerticalSeparationM": {"minimum": 0.5, "maximum": 1},
        "maximumNormalizedLinearImpulseResidual": {
            "minimum": 0,
            "maximum": 1e-6,
        },
        "maximumAngularImpulseDeltaMagnitudeNms": {
            "minimum": 1,
            "maximum": 2,
        },
        "summedAngularImpulseDeltaMagnitudeNms": {
            "minimum": 4,
            "maximum": 8,
        },
        "summedWholeStepContactWorkJ": {"minimum": -3000, "maximum": -1800},
        "summedWholeStepContactKineticEnergyDeltaJ": {
            "minimum": 1200,
            "maximum": 3000,
        },
        "summedWholeStepContactIntegrationEnergyDeltaJ": {
            "minimum": 3500,
            "maximum": 5500,
        },
        "summedSharedNodeCrossTermJ": {
            "minimum": -2500,
            "maximum": -1200,
        },
    }


def conservation_receipt(
    *,
    contacts: int = 218,
    residual: str = "9.479808970995164e-7",
    angular_max: str = "1.4753589780552108",
    angular_x: str = "4.2011899660349945",
    angular_y: str = "-2.898420564306775",
    angular_z: str = "2.7621285626027703",
) -> str:
    return (
        "[RoR|Determinism|ContactConservation] PASS schema=3 "
        f"contacts={contacts} fixed_steps=2000 "
        f"maximum_normalized_linear_impulse_residual={residual} "
        f"maximum_angular_impulse_delta_magnitude_nms={angular_max} "
        f"summed_angular_impulse_delta_x_nms={angular_x} "
        f"summed_angular_impulse_delta_y_nms={angular_y} "
        f"summed_angular_impulse_delta_z_nms={angular_z} "
        "summed_isolated_contact_work_j=-2383.5962537237647 "
        "summed_isolated_contact_kinetic_energy_delta_j=3937.669503639797 "
        "summed_isolated_contact_integration_energy_delta_j=6321.2657573635615 "
        "whole_step_shared_node_energy=audited "
        f"audited_fixed_steps=2000 whole_step_contact_count={contacts} "
        "summed_unique_node_count=742 summed_shared_node_count=49 "
        "maximum_node_contact_multiplicity=12 "
        "summed_whole_step_contact_work_j=-2383.5962537237647 "
        "summed_whole_step_contact_kinetic_energy_delta_j=1982.7861239381718 "
        "summed_whole_step_contact_integration_energy_delta_j=4366.3823776619365 "
        "summed_shared_node_cross_term_j=-1954.883379701625"
    )


def accepted_telemetry() -> dict[str, object]:
    return {
        "maximum_relative_velocity_change": 6.937932014465332,
        "maximum_vertical_separation": 0.6513442993164062,
        "contact_conservation": {
            "contact_count": 218,
            "maximum_normalized_linear_impulse_residual": 9.479808970995164e-7,
            "maximum_angular_impulse_delta_magnitude_nms": 1.4753589780552108,
            "summed_angular_impulse_delta_magnitude_nms": 5.803463887598127,
            "summed_whole_step_contact_work_j": -2383.5962537237647,
            "summed_whole_step_contact_kinetic_energy_delta_j": 1982.7861239381718,
            "summed_whole_step_contact_integration_energy_delta_j": 4366.3823776619365,
            "summed_shared_node_cross_term_j": -1954.883379701625,
        },
    }


class JBeamInterActorCollisionTests(unittest.TestCase):
    def test_profile_binds_exact_sources_and_topology(self) -> None:
        profile, profile_bytes, jbeam, script = GATE.read_profile(REPOSITORY_ROOT)
        self.assertEqual(
            profile["fixtureId"],
            "ror-jbeam-authenticated-inter-actor-collision-v2",
        )
        self.assertEqual(profile["schema"], 2)
        self.assertEqual(profile["contactAcceptance"], contact_acceptance())
        self.assertEqual(
            GATE.contact_acceptance_sha256(profile["contactAcceptance"]),
            "730c384619186cc96291f9893ad54c9fb4c66dec74a982666f139968fec438ca",
        )
        self.assertEqual(
            GATE.CONTACT_ACCEPTANCE_CANONICALIZATION,
            "ror-contact-acceptance-sorted-decimal-json-v1",
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
            GATE.sha256_bytes(profile_bytes),
            "ad51da07ec2986e76bf324be71c364276887c3ed662daaed5707917332ff8288",
        )

    def test_fixture_json_and_acceptance_are_strict_and_finite(self) -> None:
        self.assertEqual(
            GATE.decode_strict_json('{"a":1}', "test JSON"), {"a": 1}
        )
        for payload in (
            '{"a":1,"a":2}',
            '{"value":NaN}',
            '{"value":Infinity}',
            '{"value":-Infinity}',
            '{"value":1e9999}',
        ):
            with self.subTest(payload=payload):
                with self.assertRaises(GATE.CollisionGateFailure):
                    GATE.decode_strict_json(payload, "test JSON")

        accepted = contact_acceptance()
        self.assertEqual(GATE.validate_contact_acceptance(accepted), accepted)
        malformed: list[dict[str, object]] = []
        for key in tuple(accepted):
            candidate = copy.deepcopy(accepted)
            candidate.pop(key)
            malformed.append(candidate)
        candidate = copy.deepcopy(accepted)
        candidate["unexpected"] = {"minimum": 0, "maximum": 1}
        malformed.append(candidate)
        candidate = copy.deepcopy(accepted)
        candidate["schema"] = 2
        malformed.append(candidate)
        for value in (True, 128.5, "128", None):
            candidate = copy.deepcopy(accepted)
            candidate["contactCount"]["minimum"] = value
            malformed.append(candidate)
        for value in (False, "8", None, math.nan, math.inf, 10**400):
            candidate = copy.deepcopy(accepted)
            candidate["maximumRelativeVelocityChangeMps"]["maximum"] = value
            malformed.append(candidate)
        candidate = copy.deepcopy(accepted)
        candidate["maximumVerticalSeparationM"] = {
            "minimum": 2,
            "maximum": 1,
        }
        malformed.append(candidate)
        candidate = copy.deepcopy(accepted)
        candidate["summedAngularImpulseDeltaMagnitudeNms"]["minimum"] = -1
        malformed.append(candidate)
        for value in malformed:
            with self.subTest(value=value):
                with self.assertRaises(GATE.CollisionGateFailure):
                    GATE.validate_contact_acceptance(value)

    def test_acceptance_digest_is_canonical_and_tamper_evident(self) -> None:
        accepted = contact_acceptance()
        reordered = {key: accepted[key] for key in reversed(tuple(accepted))}
        canonical = GATE.contact_acceptance_canonical_bytes(accepted)
        self.assertEqual(
            canonical,
            (
                b'{"contactCount":{"maximum":256,"minimum":128},'
                b'"maximumAngularImpulseDeltaMagnitudeNms":{"maximum":2,"minimum":1},'
                b'"maximumNormalizedLinearImpulseResidual":{"maximum":0.000001,"minimum":0},'
                b'"maximumRelativeVelocityChangeMps":{"maximum":8,"minimum":5},'
                b'"maximumVerticalSeparationM":{"maximum":1,"minimum":0.5},'
                b'"schema":1,'
                b'"summedAngularImpulseDeltaMagnitudeNms":{"maximum":8,"minimum":4},'
                b'"summedSharedNodeCrossTermJ":{"maximum":-1200,"minimum":-2500},'
                b'"summedWholeStepContactIntegrationEnergyDeltaJ":{"maximum":5500,"minimum":3500},'
                b'"summedWholeStepContactKineticEnergyDeltaJ":{"maximum":3000,"minimum":1200},'
                b'"summedWholeStepContactWorkJ":{"maximum":-1800,"minimum":-3000}}'
            ),
        )
        self.assertNotIn(b"e-", canonical)
        self.assertFalse(canonical.endswith(b"\n"))
        self.assertEqual(
            GATE.contact_acceptance_sha256(accepted),
            GATE.contact_acceptance_sha256(reordered),
        )
        tampered = copy.deepcopy(accepted)
        tampered["contactCount"]["maximum"] = 257
        self.assertNotEqual(
            GATE.contact_acceptance_sha256(accepted),
            GATE.contact_acceptance_sha256(tampered),
        )

    def test_profile_staging_uses_the_exact_validated_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repository = Path(directory) / "repository"
            for relative in (
                GATE.PROFILE_RELATIVE,
                GATE.JBEAM_RELATIVE,
                GATE.SCRIPT_RELATIVE,
            ):
                source = REPOSITORY_ROOT / relative
                destination = repository / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, destination)
            profile, profile_bytes, _, _ = GATE.read_profile(repository)
            (repository / GATE.PROFILE_RELATIVE).write_text(
                '{"hostile":"replacement"}\n', encoding="utf-8"
            )
            staged = Path(directory) / "artifact" / "inputs" / "fixture-profile.json"
            staged.parent.mkdir(parents=True)
            digest = GATE.stage_profile_bytes(staged, profile_bytes)
            self.assertEqual(staged.read_bytes(), profile_bytes)
            self.assertEqual(digest, GATE.sha256_bytes(profile_bytes))
            self.assertEqual(
                GATE.decode_strict_json(staged.read_text(encoding="utf-8"), "staged"),
                profile,
            )

    def test_scenario_acceptance_is_inclusive_and_fail_closed(self) -> None:
        accepted = contact_acceptance()
        baseline = accepted_telemetry()
        self.assertIsNone(GATE.enforce_contact_acceptance(accepted, baseline))

        bindings = {
            "contactCount": ("contact_conservation", "contact_count"),
            "maximumRelativeVelocityChangeMps": (
                None,
                "maximum_relative_velocity_change",
            ),
            "maximumVerticalSeparationM": (
                None,
                "maximum_vertical_separation",
            ),
            "maximumNormalizedLinearImpulseResidual": (
                "contact_conservation",
                "maximum_normalized_linear_impulse_residual",
            ),
            "maximumAngularImpulseDeltaMagnitudeNms": (
                "contact_conservation",
                "maximum_angular_impulse_delta_magnitude_nms",
            ),
            "summedAngularImpulseDeltaMagnitudeNms": (
                "contact_conservation",
                "summed_angular_impulse_delta_magnitude_nms",
            ),
            "summedWholeStepContactWorkJ": (
                "contact_conservation",
                "summed_whole_step_contact_work_j",
            ),
            "summedWholeStepContactKineticEnergyDeltaJ": (
                "contact_conservation",
                "summed_whole_step_contact_kinetic_energy_delta_j",
            ),
            "summedWholeStepContactIntegrationEnergyDeltaJ": (
                "contact_conservation",
                "summed_whole_step_contact_integration_energy_delta_j",
            ),
            "summedSharedNodeCrossTermJ": (
                "contact_conservation",
                "summed_shared_node_cross_term_j",
            ),
        }

        for acceptance_key, (parent, telemetry_key) in bindings.items():
            bounds = accepted[acceptance_key]
            for boundary in (bounds["minimum"], bounds["maximum"]):
                candidate = copy.deepcopy(baseline)
                target = candidate if parent is None else candidate[parent]
                target[telemetry_key] = boundary
                with self.subTest(key=acceptance_key, boundary=boundary):
                    self.assertIsNone(
                        GATE.enforce_contact_acceptance(accepted, candidate)
                    )

            for outside in (
                math.nextafter(float(bounds["minimum"]), -math.inf),
                math.nextafter(float(bounds["maximum"]), math.inf),
            ):
                candidate = copy.deepcopy(baseline)
                target = candidate if parent is None else candidate[parent]
                target[telemetry_key] = outside
                with self.subTest(key=acceptance_key, outside=outside):
                    with self.assertRaises(GATE.CollisionGateFailure):
                        GATE.enforce_contact_acceptance(accepted, candidate)

        for vector in ((4.0, 0.0, 0.0), (0.0, -4.0, 0.0), (0.0, 0.0, 4.0)):
            candidate = copy.deepcopy(baseline)
            candidate["contact_conservation"][
                "summed_angular_impulse_delta_magnitude_nms"
            ] = math.hypot(*vector)
            self.assertIsNone(
                GATE.enforce_contact_acceptance(accepted, candidate)
            )

        for invalid in (0.0, 1e300, math.nan, math.inf):
            candidate = copy.deepcopy(baseline)
            candidate["contact_conservation"][
                "summed_angular_impulse_delta_magnitude_nms"
            ] = invalid
            with self.subTest(invalid=invalid):
                with self.assertRaises(GATE.CollisionGateFailure):
                    GATE.enforce_contact_acceptance(accepted, candidate)

        contradictory_acceptance = copy.deepcopy(accepted)
        contradictory_acceptance["contactCount"] = {"minimum": 4, "maximum": 4}
        contradictory_acceptance["maximumAngularImpulseDeltaMagnitudeNms"] = {
            "minimum": 1,
            "maximum": 1,
        }
        contradictory = copy.deepcopy(baseline)
        contradictory["contact_conservation"]["contact_count"] = 4
        contradictory["contact_conservation"][
            "maximum_angular_impulse_delta_magnitude_nms"
        ] = 1
        contradictory["contact_conservation"][
            "summed_angular_impulse_delta_magnitude_nms"
        ] = 5
        with self.assertRaisesRegex(
            GATE.CollisionGateFailure, "triangle bound"
        ):
            GATE.enforce_contact_acceptance(
                contradictory_acceptance, contradictory
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
            0,
            "",
            engine_log,
            script_log,
            archive_sha,
            True,
            contact_acceptance(),
        )
        self.assertEqual(telemetry["maximum_relative_velocity_change"], 7.2)
        self.assertEqual(telemetry["broken_beams"], 0)
        self.assertEqual(
            telemetry["contact_conservation"]["contact_count"], 218
        )
        self.assertEqual(
            telemetry["contact_conservation"][
                "whole_step_shared_node_energy"
            ],
            "audited",
        )
        self.assertEqual(
            telemetry["contact_conservation"]["audited_fixed_steps"],
            GATE.EXPECTED_STEPS,
        )
        self.assertEqual(
            telemetry["contact_conservation"][
                "summed_shared_node_cross_term_j"
            ],
            -1954.883379701625,
        )
        self.assertEqual(
            telemetry["contact_conservation"][
                "summed_angular_impulse_delta_magnitude_nms"
            ],
            math.hypot(
                4.2011899660349945,
                -2.898420564306775,
                2.7621285626027703,
            ),
        )
        rotated_engine_log = engine_log.replace(
            conservation_receipt(),
            conservation_receipt(
                angular_x="0",
                angular_y="-5.803463887598127",
                angular_z="0",
            ),
        )
        rotated = GATE.validate_logs(
            0,
            "",
            rotated_engine_log,
            script_log,
            archive_sha,
            True,
            contact_acceptance(),
        )
        self.assertEqual(
            rotated["contact_conservation"][
                "summed_angular_impulse_delta_magnitude_nms"
            ],
            5.803463887598127,
        )

        for bad in (
            script_log.replace("broken_beams=0", "broken_beams=1"),
            script_log.replace(GATE.ARM_MARKER, ""),
            script_log + "\n" + pass_receipt,
            script_log.replace(
                "maximum_relative_velocity_change=7.2",
                "maximum_relative_velocity_change=4.999",
            ),
            script_log.replace(
                "maximum_relative_velocity_change=7.2",
                "maximum_relative_velocity_change=8.001",
            ),
            script_log.replace(
                "maximum_vertical_separation=0.6",
                "maximum_vertical_separation=0.499",
            ),
            script_log.replace(
                "maximum_vertical_separation=0.6",
                "maximum_vertical_separation=1.001",
            ),
        ):
            with self.subTest(bad=bad[-80:]):
                with self.assertRaises(GATE.CollisionGateFailure):
                    GATE.validate_logs(
                        0,
                        "",
                        engine_log,
                        bad,
                        archive_sha,
                        True,
                        contact_acceptance(),
                    )

        for bad_engine in (
            engine_log.replace(conservation_receipt(), ""),
            engine_log + "\n" + conservation_receipt(),
            engine_log.replace("PASS schema=3", "PASS schema=2"),
            engine_log.replace(
                conservation_receipt(),
                conservation_receipt(residual="1.000001e-6"),
            ),
            engine_log.replace(
                conservation_receipt(),
                conservation_receipt(residual="nan"),
            ),
            engine_log.replace(
                conservation_receipt(),
                conservation_receipt(contacts=127),
            ),
            engine_log.replace(
                conservation_receipt(),
                conservation_receipt(contacts=257),
            ),
            engine_log.replace(
                conservation_receipt(),
                conservation_receipt(angular_max="0.9999"),
            ),
            engine_log.replace(
                conservation_receipt(),
                conservation_receipt(angular_max="2.0001"),
            ),
            engine_log.replace(
                conservation_receipt(),
                conservation_receipt(
                    angular_x="0",
                    angular_y="0",
                    angular_z="0",
                ),
            ),
            engine_log.replace(
                conservation_receipt(),
                conservation_receipt(
                    angular_x="1e300",
                    angular_y="0",
                    angular_z="0",
                ),
            ),
            engine_log.replace(
                "summed_isolated_contact_kinetic_energy_delta_j=3937.669503639797",
                "summed_isolated_contact_kinetic_energy_delta_j=999",
            ),
            engine_log.replace(
                "whole_step_shared_node_energy=audited",
                "whole_step_shared_node_energy=not_audited",
            ),
            engine_log.replace("audited_fixed_steps=2000", "audited_fixed_steps=1999"),
            engine_log.replace(
                "whole_step_contact_count=218",
                "whole_step_contact_count=217",
            ),
            engine_log.replace(
                "summed_unique_node_count=742",
                "summed_unique_node_count=0",
            ),
            engine_log.replace(
                "summed_shared_node_count=49",
                "summed_shared_node_count=743",
            ),
            engine_log.replace(
                "maximum_node_contact_multiplicity=12",
                "maximum_node_contact_multiplicity=1",
            ),
            engine_log.replace(
                "summed_whole_step_contact_kinetic_energy_delta_j=1982.7861239381718",
                "summed_whole_step_contact_kinetic_energy_delta_j=1982",
            ),
            engine_log.replace(
                "summed_shared_node_cross_term_j=-1954.883379701625",
                "summed_shared_node_cross_term_j=-1954",
            ),
        ):
            with self.subTest(bad_engine=bad_engine[-120:]):
                with self.assertRaises(GATE.CollisionGateFailure):
                    GATE.validate_logs(
                        0,
                        "",
                        bad_engine,
                        script_log,
                        archive_sha,
                        True,
                        contact_acceptance(),
                    )

    def test_trace_comparison_requires_the_exact_match_schema(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            tool = root / "trace-tool.py"
            left = root / "left.rortrace"
            right = root / "right.rortrace"
            left.write_bytes(b"left")
            right.write_bytes(b"right")

            def write(payload: dict[str, object]) -> None:
                write_tool_output(
                    tool,
                    json.dumps(payload, allow_nan=False, separators=(",", ":")),
                )

            accepted = comparison(left, right)
            write(accepted)
            result = GATE.compare_traces(tool, left, right, 1, 8, 10)
            self.assertEqual(result["difference"], "none")

            mutations: list[dict[str, object]] = []
            for key in ("difference", "left"):
                candidate = copy.deepcopy(accepted)
                candidate.pop(key)
                mutations.append(candidate)
            candidate = copy.deepcopy(accepted)
            candidate["unexpected"] = None
            mutations.append(candidate)
            for key, value in (
                ("difference", "state_digest"),
                ("metadata_field", "worker_count"),
                ("steps_compared", True),
                ("first_divergent_step", 12),
            ):
                candidate = copy.deepcopy(accepted)
                candidate[key] = value
                mutations.append(candidate)
            candidate = copy.deepcopy(accepted)
            candidate["left"]["label"] = "wrong.rortrace"
            mutations.append(candidate)
            candidate = copy.deepcopy(accepted)
            candidate["left"]["step"] = {}
            mutations.append(candidate)
            candidate = copy.deepcopy(accepted)
            candidate["left"]["unexpected"] = None
            mutations.append(candidate)
            candidate = copy.deepcopy(accepted)
            candidate["left"]["error"]["code"] = "truncated"
            mutations.append(candidate)
            candidate = copy.deepcopy(accepted)
            candidate["left"]["error"]["byte_offset"] = True
            mutations.append(candidate)
            candidate = copy.deepcopy(accepted)
            candidate["right"]["metadata"]["worker_count"] = True
            mutations.append(candidate)
            candidate = copy.deepcopy(accepted)
            candidate["right"]["metadata"]["physics_flags"] = 0
            mutations.append(candidate)
            candidate = copy.deepcopy(accepted)
            candidate["right"]["metadata"]["unexpected"] = 0
            mutations.append(candidate)
            for candidate in mutations:
                with self.subTest(candidate=candidate):
                    write(candidate)
                    with self.assertRaises(GATE.CollisionGateFailure):
                        GATE.compare_traces(tool, left, right, 1, 8, 10)

            raw = json.dumps(accepted, separators=(",", ":"))
            hostile_outputs = (
                raw.replace(
                    '"status":"match"',
                    '"status":"match","status":"match"',
                    1,
                ),
                raw.replace('"steps_compared":2000', '"steps_compared":NaN', 1),
                raw.replace(
                    '"steps_compared":2000', '"steps_compared":Infinity', 1
                ),
            )
            for output in hostile_outputs:
                with self.subTest(output=output[-80:]):
                    write_tool_output(tool, output)
                    with self.assertRaises(GATE.CollisionGateFailure):
                        GATE.compare_traces(tool, left, right, 1, 8, 10)

    def test_trace_inspection_requires_native_contact_summary(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            tool = root / "trace-tool.py"
            trace = root / "trace.rortrace"
            trace.write_bytes(b"trace")

            def write(payload: dict[str, object]) -> None:
                write_tool_output(
                    tool,
                    json.dumps(payload, allow_nan=False, separators=(",", ":")),
                )

            accepted = inspection(path=str(trace), bytes_read=trace.stat().st_size)
            write(accepted)
            result = GATE.inspect_trace(tool, trace, 1, 10)
            self.assertEqual(
                result["contact_summary"]["total_contact_count"], 218
            )
            telemetry = {
                "contact_conservation": {
                    "contact_count": 218,
                    "fixed_steps": GATE.EXPECTED_STEPS,
                }
            }
            self.assertEqual(
                GATE.bind_conservation_to_trace(telemetry, result)[
                    "contact_count"
                ],
                218,
            )
            telemetry["contact_conservation"]["contact_count"] = 217
            with self.assertRaises(GATE.CollisionGateFailure):
                GATE.bind_conservation_to_trace(telemetry, result)

            mutations: list[dict[str, object]] = []
            for key in ("bytes_read", "final_step"):
                candidate = copy.deepcopy(accepted)
                candidate.pop(key)
                mutations.append(candidate)
            candidate = copy.deepcopy(accepted)
            candidate["unexpected"] = None
            mutations.append(candidate)
            for key, value in (
                ("path", "wrong.rortrace"),
                ("step_count", True),
                ("bytes_read", 0),
                ("bytes_read", True),
                ("bytes_read", trace.stat().st_size + 1),
            ):
                candidate = copy.deepcopy(accepted)
                candidate[key] = value
                mutations.append(candidate)
            candidate = copy.deepcopy(accepted)
            candidate["metadata"]["physics_flags"] = False
            mutations.append(candidate)
            candidate = copy.deepcopy(accepted)
            candidate["metadata"]["physics_flags"] = 0
            mutations.append(candidate)
            candidate = copy.deepcopy(accepted)
            candidate["metadata"]["unexpected"] = 0
            mutations.append(candidate)
            candidate = copy.deepcopy(accepted)
            candidate["contact_summary"]["total_contact_count"] = True
            mutations.append(candidate)
            candidate = copy.deepcopy(accepted)
            candidate["contact_summary"]["maximum_contact_count"] = 219
            mutations.append(candidate)
            candidate = copy.deepcopy(accepted)
            candidate["contact_summary"]["total_contact_count"] = 4000
            mutations.append(candidate)
            candidate = copy.deepcopy(accepted)
            candidate["contact_summary"]["first_contact_physics_step"] = 1
            mutations.append(candidate)
            candidate = copy.deepcopy(accepted)
            candidate["contact_summary"]["last_contact_physics_step"] = 2000
            mutations.append(candidate)
            candidate = copy.deepcopy(accepted)
            candidate["final_step"]["actor_count"] = True
            mutations.append(candidate)
            candidate = copy.deepcopy(accepted)
            candidate["final_step"]["contact_count"] = 21
            mutations.append(candidate)
            candidate = copy.deepcopy(accepted)
            candidate["final_step"]["input_digest"] = "cd" * 32
            mutations.append(candidate)
            candidate = copy.deepcopy(accepted)
            candidate["final_step"]["state_digest"] = "AB" * 32
            mutations.append(candidate)
            candidate = copy.deepcopy(accepted)
            candidate["final_step"]["unexpected"] = 0
            mutations.append(candidate)
            for candidate in mutations:
                with self.subTest(candidate=candidate):
                    write(candidate)
                    with self.assertRaises(GATE.CollisionGateFailure):
                        GATE.inspect_trace(tool, trace, 1, 10)

            raw = json.dumps(accepted, separators=(",", ":"))
            hostile_outputs = (
                raw.replace(
                    '"status":"valid"',
                    '"status":"valid","status":"valid"',
                    1,
                ),
                raw.replace(
                    f'"bytes_read":{trace.stat().st_size}',
                    '"bytes_read":NaN',
                    1,
                ),
                raw.replace(
                    f'"bytes_read":{trace.stat().st_size}',
                    '"bytes_read":Infinity',
                    1,
                ),
            )
            for output in hostile_outputs:
                with self.subTest(output=output[-80:]):
                    write_tool_output(tool, output)
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
        self.assertIn("ror-j2-authenticated-inter-actor-collision-v4", source)
        self.assertIn("bind_conservation_to_trace", source)
        self.assertIn("contact_summary", source)
        self.assertIn("contact_acceptance_sha256", source)
        self.assertIn("contact_acceptance_canonicalization", source)
        self.assertIn(
            "ror-contact-acceptance-sorted-decimal-json-v1", source
        )
        self.assertIn("summed_angular_impulse_delta_magnitude_nms", source)
        self.assertIn("--allow-worker-count-difference", source)
        self.assertNotIn("ror-j2-authenticated-inter-actor-collision-v3", source)
        self.assertNotIn("urlopen", source)


if __name__ == "__main__":
    unittest.main()
