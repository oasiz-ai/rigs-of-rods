#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run authenticated product-path JBeam inter-actor collision evidence.

The tool performs no downloads. It packages the project-original structural
fixture, spawns two exact instances above the terrain, executes native
node-to-NORMALTYPE-cab contact, and compares canonical one-worker/eight-worker
state traces. This is bounded RoR execution evidence, not BeamNG.drive force
parity or broad third-party-mod compatibility.
"""

from __future__ import annotations

import argparse
from decimal import Decimal
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import shutil
import sys
from typing import Sequence

import run_calibrated_beam_soak as support
import run_jbeam_spawn_soak as package_support


PROFILE_RELATIVE = Path(
    "tests/fixtures/beamng/jbeam_inter_actor_collision/fixture-profile.json"
)
JBEAM_RELATIVE = Path(
    "tests/fixtures/beamng/jbeam_spawn_soak/"
    "vehicles/ror_jbeam_spawn/main.jbeam"
)
SCRIPT_RELATIVE = Path(
    "resources/scripts/example_jbeam_inter_actor_collision.as"
)
JBEAM_MEMBER = "vehicles/ror_jbeam_spawn/main.jbeam"
JBEAM_ARCHIVE = "RoRJBeamInterActorCollision.zip"
SCRIPT_MEMBER = "example_jbeam_inter_actor_collision.as"
VEHICLE = "ror_jbeam_spawn_fixture.jbeam"
TERRAIN = "simple2.terrn2"
SCENARIO_ID = 2026082106
EXPECTED_STEPS = 2000
EXPECTED_ACTORS = 2
EXPECTED_STATE_DIGEST_SCHEMA_VERSION = 3
EXPECTED_PHYSICS_FLAGS = 1
CONTACT_ACCEPTANCE_CANONICALIZATION = (
    "ror-contact-acceptance-sorted-decimal-json-v1"
)

START_MARKER = (
    "[RoR|J2|InterActorCollision] START scenario=2026082106 "
    "vehicle=ror_jbeam_spawn_fixture.jbeam actors=2 steps=2000 "
    "initial_vertical_gap=0.01 closing_speed=1"
)
ARM_MARKER = (
    "[RoR|J2|InterActorCollision] ARMED actors=2 nodes=12 beams=32 "
    "cab_triangles=10 collision_cabs=10 contacters=0 hydros=2 "
    "initial_vertical_gap=0.01 closing_speed=1 first_step=0 batch=10"
)
PASS_PATTERN = re.compile(
    r"\[RoR\|J2\|InterActorCollision\] PASS actors=2 nodes=12 "
    r"beams=32 cab_triangles=10 collision_cabs=10 contacters=0 "
    r"hydros=2 steps=2000 "
    r"maximum_relative_velocity_change=(?P<relative>[-+0-9.eE]+) "
    r"maximum_vertical_separation=(?P<separation>[-+0-9.eE]+) "
    r"broken_beams=(?P<broken>[0-9]+)"
)
CONTACT_CONSERVATION_PASS_PATTERN = re.compile(
    r"\[RoR\|Determinism\|ContactConservation\] PASS "
    r"schema=(?P<schema>[0-9]+) contacts=(?P<contacts>[0-9]+) "
    r"fixed_steps=(?P<steps>[0-9]+) "
    r"maximum_normalized_linear_impulse_residual=(?P<linear>[-+0-9.eE]+) "
    r"maximum_angular_impulse_delta_magnitude_nms=(?P<angular_max>[-+0-9.eE]+) "
    r"summed_angular_impulse_delta_x_nms=(?P<angular_x>[-+0-9.eE]+) "
    r"summed_angular_impulse_delta_y_nms=(?P<angular_y>[-+0-9.eE]+) "
    r"summed_angular_impulse_delta_z_nms=(?P<angular_z>[-+0-9.eE]+) "
    r"summed_isolated_contact_work_j=(?P<work>[-+0-9.eE]+) "
    r"summed_isolated_contact_kinetic_energy_delta_j="
    r"(?P<kinetic>[-+0-9.eE]+) "
    r"summed_isolated_contact_integration_energy_delta_j="
    r"(?P<integration>[-+0-9.eE]+) "
    r"whole_step_shared_node_energy=audited "
    r"audited_fixed_steps=(?P<audited_steps>[0-9]+) "
    r"whole_step_contact_count=(?P<whole_contacts>[0-9]+) "
    r"summed_unique_node_count=(?P<unique_nodes>[0-9]+) "
    r"summed_shared_node_count=(?P<shared_nodes>[0-9]+) "
    r"maximum_node_contact_multiplicity=(?P<max_multiplicity>[0-9]+) "
    r"summed_whole_step_contact_work_j=(?P<whole_work>[-+0-9.eE]+) "
    r"summed_whole_step_contact_kinetic_energy_delta_j="
    r"(?P<whole_kinetic>[-+0-9.eE]+) "
    r"summed_whole_step_contact_integration_energy_delta_j="
    r"(?P<whole_integration>[-+0-9.eE]+) "
    r"summed_shared_node_cross_term_j=(?P<shared_cross>[-+0-9.eE]+)"
)
FATAL_MARKERS = (
    "[RoR|J2|InterActorCollision] FAIL",
    "[RoR|JBeam] Rejected actor spawn",
    "[RoR|ModCache|JBeam] Refused",
    "[RoR|ModCache|JBeam] Rejected",
    "State trace snapshot failed",
    "State trace append failed",
    "[RoR|Determinism|ContactConservation] FAIL",
    "could not be finished",
    "Validation Failed: Sampler error:",
    "GL_INVALID_",
    "RenderingAPIException",
    "OGRE EXCEPTION",
    "EXC_BAD_ACCESS",
    "Segmentation fault",
)


class CollisionGateFailure(RuntimeError):
    """Fail-closed diagnostic for invalid input, execution, or evidence."""


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _strict_json_object(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise CollisionGateFailure(f"duplicate JSON object key: {key}")
        result[key] = value
    return result


def _reject_json_constant(token: str) -> object:
    raise CollisionGateFailure(f"non-finite JSON constant: {token}")


def _require_finite_json(value: object, label: str) -> None:
    if isinstance(value, float) and not math.isfinite(value):
        raise CollisionGateFailure(f"{label} contains a non-finite number")
    if isinstance(value, dict):
        for child in value.values():
            _require_finite_json(child, label)
    elif isinstance(value, list):
        for child in value:
            _require_finite_json(child, label)


def decode_strict_json(source: str, label: str) -> object:
    try:
        value = json.loads(
            source,
            object_pairs_hook=_strict_json_object,
            parse_constant=_reject_json_constant,
        )
    except CollisionGateFailure:
        raise
    except (json.JSONDecodeError, OverflowError, TypeError, ValueError) as error:
        raise CollisionGateFailure(f"{label} is not strict JSON") from error
    _require_finite_json(value, label)
    return value


def is_finite_number(value: object) -> bool:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return False
    try:
        return math.isfinite(float(value))
    except (OverflowError, ValueError):
        return False


CONTACT_ACCEPTANCE_KEYS = {
    "contactCount",
    "maximumAngularImpulseDeltaMagnitudeNms",
    "maximumNormalizedLinearImpulseResidual",
    "maximumRelativeVelocityChangeMps",
    "maximumVerticalSeparationM",
    "schema",
    "summedAngularImpulseDeltaMagnitudeNms",
    "summedSharedNodeCrossTermJ",
    "summedWholeStepContactIntegrationEnergyDeltaJ",
    "summedWholeStepContactKineticEnergyDeltaJ",
    "summedWholeStepContactWorkJ",
}

NONNEGATIVE_ACCEPTANCE_KEYS = {
    "maximumAngularImpulseDeltaMagnitudeNms",
    "maximumNormalizedLinearImpulseResidual",
    "maximumRelativeVelocityChangeMps",
    "maximumVerticalSeparationM",
    "summedAngularImpulseDeltaMagnitudeNms",
}


def validate_contact_acceptance(value: object) -> dict[str, object]:
    if not isinstance(value, dict) or set(value) != CONTACT_ACCEPTANCE_KEYS:
        raise CollisionGateFailure("contact acceptance schema drifted")
    if type(value.get("schema")) is not int or value["schema"] != 1:
        raise CollisionGateFailure("contact acceptance schema is unsupported")
    for key in CONTACT_ACCEPTANCE_KEYS - {"schema"}:
        bounds = value.get(key)
        if not isinstance(bounds, dict) or set(bounds) != {"minimum", "maximum"}:
            raise CollisionGateFailure(f"contact acceptance {key} bounds drifted")
        minimum = bounds.get("minimum")
        maximum = bounds.get("maximum")
        if key == "contactCount":
            if (
                type(minimum) is not int
                or type(maximum) is not int
                or minimum < 1
            ):
                raise CollisionGateFailure(
                    "contact acceptance contactCount bounds are invalid"
                )
        elif not is_finite_number(minimum) or not is_finite_number(maximum):
            raise CollisionGateFailure(f"contact acceptance {key} bounds are invalid")
        if minimum > maximum:
            raise CollisionGateFailure(f"contact acceptance {key} bounds are reversed")
        if key in NONNEGATIVE_ACCEPTANCE_KEYS and minimum < 0:
            raise CollisionGateFailure(
                f"contact acceptance {key} minimum must be nonnegative"
            )
    return value


def _canonical_decimal_json(value: object) -> str:
    if isinstance(value, dict):
        return "{" + ",".join(
            json.dumps(key, ensure_ascii=True)
            + ":"
            + _canonical_decimal_json(value[key])
            for key in sorted(value)
        ) + "}"
    if type(value) is int:
        return str(value)
    if type(value) is float:
        number = Decimal(str(value))
        if number == 0:
            return "0"
        return format(number.normalize(), "f")
    raise CollisionGateFailure(
        f"contact acceptance contains unsupported canonical type: {type(value).__name__}"
    )


def contact_acceptance_canonical_bytes(value: object) -> bytes:
    accepted = validate_contact_acceptance(value)
    return _canonical_decimal_json(accepted).encode("ascii")


def contact_acceptance_sha256(value: object) -> str:
    return sha256_bytes(contact_acceptance_canonical_bytes(value))


def enforce_contact_acceptance(
    acceptance: object,
    telemetry: object,
) -> None:
    accepted = validate_contact_acceptance(acceptance)
    if not isinstance(telemetry, dict):
        raise CollisionGateFailure("contact telemetry is missing")
    conservation = telemetry.get("contact_conservation")
    if not isinstance(conservation, dict):
        raise CollisionGateFailure("contact conservation telemetry is missing")
    bindings = {
        "contactCount": conservation.get("contact_count"),
        "maximumRelativeVelocityChangeMps": telemetry.get(
            "maximum_relative_velocity_change"
        ),
        "maximumVerticalSeparationM": telemetry.get(
            "maximum_vertical_separation"
        ),
        "maximumNormalizedLinearImpulseResidual": conservation.get(
            "maximum_normalized_linear_impulse_residual"
        ),
        "maximumAngularImpulseDeltaMagnitudeNms": conservation.get(
            "maximum_angular_impulse_delta_magnitude_nms"
        ),
        "summedAngularImpulseDeltaMagnitudeNms": conservation.get(
            "summed_angular_impulse_delta_magnitude_nms"
        ),
        "summedWholeStepContactWorkJ": conservation.get(
            "summed_whole_step_contact_work_j"
        ),
        "summedWholeStepContactKineticEnergyDeltaJ": conservation.get(
            "summed_whole_step_contact_kinetic_energy_delta_j"
        ),
        "summedWholeStepContactIntegrationEnergyDeltaJ": conservation.get(
            "summed_whole_step_contact_integration_energy_delta_j"
        ),
        "summedSharedNodeCrossTermJ": conservation.get(
            "summed_shared_node_cross_term_j"
        ),
    }
    for key, measurement in bindings.items():
        if key == "contactCount":
            valid_measurement = type(measurement) is int
        else:
            valid_measurement = is_finite_number(measurement)
        if not valid_measurement:
            raise CollisionGateFailure(f"contact acceptance {key} measurement is invalid")
        bounds = accepted[key]
        if measurement < bounds["minimum"] or measurement > bounds["maximum"]:
            raise CollisionGateFailure(
                f"contact acceptance {key} measurement is outside the scenario envelope"
            )
    contact_count = int(bindings["contactCount"])
    maximum_angular_delta = float(
        bindings["maximumAngularImpulseDeltaMagnitudeNms"]
    )
    summed_angular_delta = float(
        bindings["summedAngularImpulseDeltaMagnitudeNms"]
    )
    angular_ceiling = contact_count * maximum_angular_delta
    if summed_angular_delta > angular_ceiling and not math.isclose(
        summed_angular_delta,
        angular_ceiling,
        rel_tol=1e-12,
        abs_tol=1e-12,
    ):
        raise CollisionGateFailure(
            "summed angular impulse delta exceeds the per-contact triangle bound"
        )


def resolve_direct_file(path: Path, label: str) -> Path:
    if path.is_symlink() or not path.is_file():
        raise CollisionGateFailure(f"{label} is missing or indirect: {path}")
    return path.resolve(strict=True)


def read_profile(repository: Path) -> tuple[dict[str, object], bytes, bytes, bytes]:
    profile_path = repository / PROFILE_RELATIVE
    jbeam_path = repository / JBEAM_RELATIVE
    script_path = repository / SCRIPT_RELATIVE
    for path, label in (
        (profile_path, "fixture profile"),
        (jbeam_path, "JBeam source"),
        (script_path, "scenario script"),
    ):
        if not path.is_file() or path.is_symlink():
            raise CollisionGateFailure(f"{label} is missing or indirect: {path}")
    try:
        profile_bytes = profile_path.read_bytes()
        profile = decode_strict_json(
            profile_bytes.decode("utf-8"), "fixture profile"
        )
    except (OSError, UnicodeDecodeError) as error:
        raise CollisionGateFailure("fixture profile is not strict JSON") from error
    try:
        jbeam = support.canonical_lf_text(jbeam_path.read_bytes(), "JBeam source")
        script = support.canonical_lf_text(script_path.read_bytes(), "scenario script")
    except support.SoakFailure as error:
        raise CollisionGateFailure(str(error)) from error
    expected_keys = {
        "authorship",
        "contactAcceptance",
        "documentationProfile",
        "execution",
        "expectedRuntime",
        "fixtureId",
        "jbeamSource",
        "license",
        "prohibitedInputs",
        "rootPart",
        "scenarioScript",
        "schema",
    }
    if not isinstance(profile, dict) or set(profile) != expected_keys:
        raise CollisionGateFailure("fixture profile schema drifted")
    if (
        profile.get("schema") != 2
        or profile.get("fixtureId")
        != "ror-jbeam-authenticated-inter-actor-collision-v2"
        or profile.get("authorship") != "original-clean-room"
        or profile.get("license") != "GPL-3.0-or-later"
        or profile.get("execution") != "authenticated-product-path"
        or profile.get("rootPart") != "ror_jbeam_spawn_fixture"
        or profile.get("jbeamSource")
        != {
            "path": JBEAM_RELATIVE.as_posix(),
            "sha256": sha256_bytes(jbeam),
        }
        or profile.get("scenarioScript")
        != {
            "path": SCRIPT_RELATIVE.as_posix(),
            "sha256": sha256_bytes(script),
        }
        or profile.get("expectedRuntime")
        != {
            "actors": 2,
            "cabTrianglesPerActor": 5,
            "collisionCabsPerActor": 5,
            "contactersPerActor": 0,
            "fixedSteps": EXPECTED_STEPS,
            "initialVerticalGap": 0.01,
            "initialVerticalRelativeSpeed": 1,
            "jbeamHydrosPerActor": 1,
            "nodesPerActor": 6,
            "physicsStepDenominator": 2000,
            "physicsStepNumerator": 1,
            "runtimeBeamsPerActor": 16,
        }
        or profile.get("prohibitedInputs")
        != [
            "lua-execution",
            "ogre-script-execution",
            "network",
            "external-assets",
            "third-party-mod-data",
        ]
    ):
        raise CollisionGateFailure("fixture profile does not match exact sources")
    validate_contact_acceptance(profile.get("contactAcceptance"))
    return profile, profile_bytes, jbeam, script


def stage_profile_bytes(destination: Path, payload: bytes) -> str:
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    try:
        descriptor = os.open(destination, flags, 0o644)
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(payload)
    except OSError as error:
        raise CollisionGateFailure("fixture profile staging failed closed") from error
    try:
        staged_payload = destination.read_bytes()
    except OSError as error:
        raise CollisionGateFailure("fixture profile staging is unreadable") from error
    if destination.is_symlink() or not destination.is_file() or staged_payload != payload:
        raise CollisionGateFailure("fixture profile staging changed bytes")
    return sha256_bytes(payload)


def build_command(executable: Path) -> tuple[str, ...]:
    command = [str(executable)]
    if sys.platform == "darwin":
        command.extend(("-ApplePersistenceIgnoreState", "YES"))
    command.extend(
        (
            "-checkcache",
            "-map",
            TERRAIN,
            "-enter",
            "-runscript",
            SCRIPT_MEMBER,
        )
    )
    return tuple(command)


def validate_logs(
    returncode: int,
    stdout: str,
    engine_log: str,
    script_log: str,
    archive_sha256: str,
    require_scan_receipt: bool,
    contact_acceptance: object,
) -> dict[str, object]:
    if returncode != 0:
        raise CollisionGateFailure(f"RoR collision gate exited with {returncode}")
    for marker in (START_MARKER, ARM_MARKER):
        if marker not in script_log:
            raise CollisionGateFailure(f"AngelScript log missed marker: {marker}")
    engine_markers = (
        "[RoR|ModCache|JBeam] Mounted exact archive",
        f"archive_sha256={archive_sha256}",
        "roots=1",
        "[RoR|Determinism] Recording state trace",
        f"scenario={SCENARIO_ID}",
        f"limit={EXPECTED_STEPS}",
        f"with {EXPECTED_STEPS} fixed-step records (trace step limit reached)",
    )
    if require_scan_receipt:
        engine_markers += (
            "[RoR|ModCache|JBeam] Added exact root "
            "'ror_jbeam_spawn_fixture'",
            "nodes=6, beams=15, hydros=1",
        )
    for marker in engine_markers:
        if marker not in engine_log:
            raise CollisionGateFailure(f"engine log missed marker: {marker}")
    combined = "\n".join((stdout, engine_log, script_log))
    for marker in FATAL_MARKERS:
        if marker in combined:
            raise CollisionGateFailure(f"runtime logged a fatal marker: {marker}")
    matches = list(PASS_PATTERN.finditer(script_log))
    if len(matches) != 1:
        raise CollisionGateFailure(
            f"expected one collision PASS receipt, found {len(matches)}"
        )
    relative = float(matches[0].group("relative"))
    separation = float(matches[0].group("separation"))
    broken = int(matches[0].group("broken"))
    if not (0.1 < relative <= 1.0e7):
        raise CollisionGateFailure("relative-velocity response is outside bounds")
    if not (0.03 < separation <= 1.0e7) or broken != 0:
        raise CollisionGateFailure("collision separation/breakage is outside bounds")

    conservation_matches = list(
        CONTACT_CONSERVATION_PASS_PATTERN.finditer(engine_log)
    )
    if len(conservation_matches) != 1:
        raise CollisionGateFailure(
            "expected one native contact-conservation PASS receipt, "
            f"found {len(conservation_matches)}"
        )
    conservation_match = conservation_matches[0]
    conservation: dict[str, object] = {
        "schema": int(conservation_match.group("schema")),
        "contact_count": int(conservation_match.group("contacts")),
        "fixed_steps": int(conservation_match.group("steps")),
        "maximum_normalized_linear_impulse_residual": float(
            conservation_match.group("linear")
        ),
        "maximum_angular_impulse_delta_magnitude_nms": float(
            conservation_match.group("angular_max")
        ),
        "summed_angular_impulse_delta_x_nms": float(
            conservation_match.group("angular_x")
        ),
        "summed_angular_impulse_delta_y_nms": float(
            conservation_match.group("angular_y")
        ),
        "summed_angular_impulse_delta_z_nms": float(
            conservation_match.group("angular_z")
        ),
        "summed_isolated_contact_work_j": float(
            conservation_match.group("work")
        ),
        "summed_isolated_contact_kinetic_energy_delta_j": float(
            conservation_match.group("kinetic")
        ),
        "summed_isolated_contact_integration_energy_delta_j": float(
            conservation_match.group("integration")
        ),
        "whole_step_shared_node_energy": "audited",
        "audited_fixed_steps": int(
            conservation_match.group("audited_steps")
        ),
        "whole_step_contact_count": int(
            conservation_match.group("whole_contacts")
        ),
        "summed_unique_node_count": int(
            conservation_match.group("unique_nodes")
        ),
        "summed_shared_node_count": int(
            conservation_match.group("shared_nodes")
        ),
        "maximum_node_contact_multiplicity": int(
            conservation_match.group("max_multiplicity")
        ),
        "summed_whole_step_contact_work_j": float(
            conservation_match.group("whole_work")
        ),
        "summed_whole_step_contact_kinetic_energy_delta_j": float(
            conservation_match.group("whole_kinetic")
        ),
        "summed_whole_step_contact_integration_energy_delta_j": float(
            conservation_match.group("whole_integration")
        ),
        "summed_shared_node_cross_term_j": float(
            conservation_match.group("shared_cross")
        ),
    }
    conservation["summed_angular_impulse_delta_magnitude_nms"] = math.hypot(
        conservation["summed_angular_impulse_delta_x_nms"],
        conservation["summed_angular_impulse_delta_y_nms"],
        conservation["summed_angular_impulse_delta_z_nms"],
    )
    scalar_values = (
        value for value in conservation.values() if isinstance(value, float)
    )
    if (
        conservation["schema"] != 3
        or conservation["contact_count"] <= 0
        or conservation["fixed_steps"] != EXPECTED_STEPS
        or conservation["audited_fixed_steps"] != EXPECTED_STEPS
        or conservation["whole_step_contact_count"]
        != conservation["contact_count"]
        or conservation["summed_unique_node_count"] <= 0
        or conservation["summed_unique_node_count"]
        > conservation["whole_step_contact_count"] * 4
        or conservation["summed_shared_node_count"] <= 0
        or conservation["summed_shared_node_count"]
        > conservation["summed_unique_node_count"]
        or conservation["maximum_node_contact_multiplicity"] < 2
        or conservation["maximum_node_contact_multiplicity"]
        > conservation["whole_step_contact_count"]
        or any(not math.isfinite(value) for value in scalar_values)
        or conservation["maximum_normalized_linear_impulse_residual"] < 0.0
        or conservation["maximum_normalized_linear_impulse_residual"] > 1.0e-6
        or conservation["maximum_angular_impulse_delta_magnitude_nms"] < 0.0
        or conservation[
            "summed_isolated_contact_integration_energy_delta_j"
        ] < 0.0
        or conservation[
            "summed_whole_step_contact_integration_energy_delta_j"
        ] < 0.0
    ):
        raise CollisionGateFailure(
            "native contact-conservation receipt is outside acceptance bounds"
        )
    isolated_work = float(conservation["summed_isolated_contact_work_j"])
    isolated_kinetic = float(
        conservation["summed_isolated_contact_kinetic_energy_delta_j"]
    )
    isolated_integration = float(
        conservation["summed_isolated_contact_integration_energy_delta_j"]
    )
    if isolated_kinetic != isolated_work + isolated_integration:
        raise CollisionGateFailure(
            "native isolated-contact energy identity is inconsistent"
        )
    whole_work = float(conservation["summed_whole_step_contact_work_j"])
    whole_kinetic = float(
        conservation["summed_whole_step_contact_kinetic_energy_delta_j"]
    )
    whole_integration = float(
        conservation["summed_whole_step_contact_integration_energy_delta_j"]
    )
    shared_cross = float(conservation["summed_shared_node_cross_term_j"])
    if (
        whole_work != isolated_work
        or whole_kinetic != whole_work + whole_integration
        or shared_cross != whole_integration - isolated_integration
    ):
        raise CollisionGateFailure(
            "native whole-step shared-node energy identity is inconsistent"
        )
    telemetry = {
        "broken_beams": broken,
        "contact_conservation": conservation,
        "maximum_relative_velocity_change": relative,
        "maximum_vertical_separation": separation,
    }
    enforce_contact_acceptance(contact_acceptance, telemetry)
    return telemetry


def bind_conservation_to_trace(
    telemetry: dict[str, object],
    inspection: dict[str, object],
) -> dict[str, object]:
    conservation = telemetry.get("contact_conservation")
    summary = inspection.get("contact_summary")
    if not isinstance(conservation, dict) or not isinstance(summary, dict):
        raise CollisionGateFailure(
            "native conservation telemetry or trace summary is missing"
        )
    if conservation.get("contact_count") != summary.get("total_contact_count"):
        raise CollisionGateFailure(
            "native conservation contact count does not bind to the trace"
        )
    return conservation


def require_exact_object(
    value: object,
    expected_keys: set[str],
    label: str,
) -> dict[str, object]:
    if not isinstance(value, dict) or set(value) != expected_keys:
        raise CollisionGateFailure(f"{label} key set changed")
    return value


def require_exact_int(
    value: object,
    expected: int,
    label: str,
) -> int:
    if type(value) is not int or value != expected:
        raise CollisionGateFailure(f"{label}={value!r}; expected exact integer {expected}")
    return value


def validate_trace_metadata(
    value: object,
    workers: int,
    digest_key: str,
    label: str,
) -> dict[str, object]:
    metadata_keys = {
        digest_key,
        "first_physics_step",
        "physics_flags",
        "physics_step_denominator",
        "physics_step_numerator",
        "scenario_id",
        "worker_count",
    }
    metadata = require_exact_object(value, metadata_keys, label)
    expected = {
        digest_key: EXPECTED_STATE_DIGEST_SCHEMA_VERSION,
        "first_physics_step": 0,
        "physics_flags": EXPECTED_PHYSICS_FLAGS,
        "physics_step_denominator": 2000,
        "physics_step_numerator": 1,
        "scenario_id": SCENARIO_ID,
        "worker_count": workers,
    }
    for key, expected_value in expected.items():
        require_exact_int(metadata[key], expected_value, f"{label} {key}")
    return metadata


def validate_trace_comparison(
    value: object,
    left: Path,
    right: Path,
    left_workers: int,
    right_workers: int,
) -> dict[str, object]:
    payload = require_exact_object(
        value,
        {
            "difference",
            "first_divergent_step",
            "format",
            "left",
            "metadata_field",
            "right",
            "status",
            "steps_compared",
        },
        "trace comparison",
    )
    if (
        payload["format"] != "ror-d0-state-trace-comparison-v2"
        or payload["status"] != "match"
        or payload["difference"] != "none"
        or payload["metadata_field"] != "none"
        or payload["first_divergent_step"] is not None
    ):
        raise CollisionGateFailure("trace comparison did not report an exact match")
    require_exact_int(
        payload["steps_compared"], EXPECTED_STEPS, "trace comparison steps"
    )
    for side_name, trace, workers in (
        ("left", left, left_workers),
        ("right", right, right_workers),
    ):
        side = require_exact_object(
            payload[side_name],
            {"error", "label", "metadata", "step"},
            f"{side_name} trace comparison",
        )
        if side["label"] != str(trace) or side["step"] is not None:
            raise CollisionGateFailure(
                f"{side_name} trace comparison binding changed"
            )
        validate_trace_metadata(
            side["metadata"],
            workers,
            "digest_schema_version",
            f"{side_name} trace comparison metadata",
        )
        error = require_exact_object(
            side["error"],
            {"byte_offset", "code", "step_index"},
            f"{side_name} trace comparison error",
        )
        if error["code"] != "none":
            raise CollisionGateFailure(
                f"{side_name} trace comparison reported an input error"
            )
        require_exact_int(
            error["byte_offset"], 0, f"{side_name} trace error byte offset"
        )
        require_exact_int(
            error["step_index"], 0, f"{side_name} trace error step index"
        )
    return payload


def compare_traces(
    trace_tool: Path,
    left: Path,
    right: Path,
    left_workers: int,
    right_workers: int,
    timeout: int,
) -> dict[str, object]:
    completed = support.run_command(
        (
            str(trace_tool),
            "--allow-worker-count-difference",
            str(left),
            str(right),
        ),
        timeout,
    )
    output = support.decode_output(completed.stdout)
    try:
        payload = decode_strict_json(output, "trace comparator output")
    except CollisionGateFailure as error:
        raise CollisionGateFailure("trace comparator emitted invalid JSON") from error
    if completed.returncode != 0:
        raise CollisionGateFailure(f"collision traces diverged: {output}")
    return validate_trace_comparison(
        payload, left, right, left_workers, right_workers
    )


def validate_trace_inspection(
    value: object,
    trace: Path,
    workers: int,
) -> dict[str, object]:
    payload = require_exact_object(
        value,
        {
            "bytes_read",
            "contact_summary",
            "final_step",
            "format",
            "has_final_step",
            "metadata",
            "path",
            "status",
            "step_count",
        },
        "trace inspection",
    )
    if (
        payload["format"] != "ror-d0-state-trace-inspection-v2"
        or payload["status"] != "valid"
        or payload["path"] != str(trace)
        or payload["has_final_step"] is not True
    ):
        raise CollisionGateFailure("trace inspection binding changed")
    require_exact_int(payload["step_count"], EXPECTED_STEPS, "trace step count")
    trace_bytes = trace.stat().st_size
    require_exact_int(payload["bytes_read"], trace_bytes, "trace bytes read")
    if trace_bytes <= 0:
        raise CollisionGateFailure("trace is empty")
    validate_trace_metadata(
        payload["metadata"],
        workers,
        "state_digest_schema_version",
        "trace inspection metadata",
    )
    summary = require_exact_object(
        payload["contact_summary"],
        {
            "contact_step_count",
            "first_contact_physics_step",
            "last_contact_physics_step",
            "maximum_contact_count",
            "total_contact_count",
        },
        "trace contact summary",
    )
    for key in summary:
        if type(summary[key]) is not int:
            raise CollisionGateFailure(f"trace contact summary {key} is not an integer")
    total_contacts = summary["total_contact_count"]
    contact_steps = summary["contact_step_count"]
    maximum_contacts = summary["maximum_contact_count"]
    first_contact = summary["first_contact_physics_step"]
    last_contact = summary["last_contact_physics_step"]
    if (
        total_contacts <= 0
        or contact_steps <= 0
        or contact_steps > EXPECTED_STEPS
        or maximum_contacts <= 0
        or first_contact != 0
        or last_contact < first_contact
        or last_contact >= EXPECTED_STEPS
        or contact_steps > total_contacts
        or maximum_contacts > total_contacts
        or total_contacts > contact_steps * maximum_contacts
    ):
        raise CollisionGateFailure("trace contact summary is inconsistent")
    final_step = require_exact_object(
        payload["final_step"],
        {
            "actor_count",
            "contact_count",
            "input_digest",
            "physics_step",
            "state_digest",
        },
        "trace final step",
    )
    require_exact_int(
        final_step["physics_step"], EXPECTED_STEPS - 1, "final physics step"
    )
    require_exact_int(final_step["actor_count"], EXPECTED_ACTORS, "final actor count")
    if (
        type(final_step["contact_count"]) is not int
        or final_step["contact_count"] < 0
        or final_step["contact_count"] > maximum_contacts
        or final_step["input_digest"] is not None
        or not isinstance(final_step["state_digest"], str)
        or re.fullmatch(r"[0-9a-f]{64}", final_step["state_digest"]) is None
    ):
        raise CollisionGateFailure("trace final step is inconsistent")
    if (
        final_step["physics_step"] > last_contact
        and final_step["contact_count"] != 0
    ):
        raise CollisionGateFailure("trace final contact count contradicts its summary")
    return payload


def inspect_trace(
    trace_tool: Path,
    trace: Path,
    workers: int,
    timeout: int,
) -> dict[str, object]:
    completed = support.run_command(
        (str(trace_tool), "--inspect", str(trace)), timeout
    )
    output = support.decode_output(completed.stdout)
    try:
        payload = decode_strict_json(output, "trace inspector output")
    except CollisionGateFailure as error:
        raise CollisionGateFailure("trace inspector emitted invalid JSON") from error
    if completed.returncode != 0:
        raise CollisionGateFailure(f"collision trace inspection failed: {output}")
    return validate_trace_inspection(payload, trace, workers)


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--trace-tool", required=True, type=Path)
    parser.add_argument(
        "--repository",
        type=Path,
        default=Path(__file__).resolve().parents[1],
    )
    parser.add_argument("--runtime-content", type=Path)
    parser.add_argument("--artifact-dir", required=True, type=Path)
    parser.add_argument("--runs", type=int, default=1)
    parser.add_argument("--workers", type=int, nargs="+", default=(1, 8))
    parser.add_argument("--timeout", type=int, default=300)
    args = parser.parse_args(argv)
    if args.runs <= 0 or args.timeout <= 0:
        parser.error("--runs and --timeout must be positive")
    if not args.workers or any(value <= 0 for value in args.workers):
        parser.error("--workers must contain positive integers")
    if len(set(args.workers)) != len(args.workers):
        parser.error("--workers must not contain duplicates")
    return args


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    executable = resolve_direct_file(args.executable, "executable")
    trace_tool = resolve_direct_file(args.trace_tool, "trace tool")
    repository = args.repository.resolve()
    artifact_dir = args.artifact_dir.resolve()
    if artifact_dir.exists():
        raise CollisionGateFailure(f"artifact directory already exists: {artifact_dir}")
    artifact_dir.mkdir(parents=True)

    profile, profile_bytes, jbeam, script = read_profile(repository)
    contact_acceptance = profile["contactAcceptance"]
    acceptance_sha256 = contact_acceptance_sha256(contact_acceptance)
    inputs = artifact_dir / "inputs"
    inputs.mkdir()
    staged_profile = inputs / "fixture-profile.json"
    profile_sha256 = stage_profile_bytes(staged_profile, profile_bytes)
    runtime_content = (
        support.infer_runtime_content(executable)
        if args.runtime_content is None
        else args.runtime_content.resolve()
    )
    if not runtime_content.is_dir():
        raise CollisionGateFailure(f"runtime content is missing: {runtime_content}")
    package_support.verify_runtime_terrain(runtime_content)

    isolated_home = artifact_dir / "work" / "jbeam-collision-home"
    layout = support.runtime_layout(isolated_home, sys.platform)
    for key in ("config", "logs", "mods"):
        layout[key].mkdir(parents=True, exist_ok=True)
    archive_sha256 = package_support.write_archive(
        layout["mods"] / JBEAM_ARCHIVE,
        {JBEAM_MEMBER: jbeam},
    )
    scripts = layout["user"] / "scripts"
    scripts.mkdir(parents=True, exist_ok=True)
    runtime_script = scripts / SCRIPT_MEMBER
    runtime_script.write_bytes(script)
    if support.sha256_file(runtime_script) != sha256_bytes(script):
        raise CollisionGateFailure("trusted runtime script staging changed bytes")

    traces = artifact_dir / "traces"
    diagnostics = artifact_dir / "diagnostics"
    traces.mkdir()
    diagnostics.mkdir()
    baseline: Path | None = None
    baseline_workers = 0
    cache_initialized = False
    results: list[dict[str, object]] = []
    state_comparisons: list[dict[str, object]] = []
    baseline_conservation: dict[str, object] | None = None

    for workers in args.workers:
        for run_index in range(1, args.runs + 1):
            support.write_runtime_config(layout["config"], workers, not cache_initialized)
            for trace in layout["logs"].glob("*.rortrace"):
                trace.unlink()
            for name in ("RoR.log", "Angelscript.log"):
                try:
                    (layout["logs"] / name).unlink()
                except FileNotFoundError:
                    pass

            environment = os.environ.copy()
            environment.pop("SNAP_USER_COMMON", None)
            environment["ROR_D0_SCENE_HOME"] = str(isolated_home)
            environment["ALSOFT_DRIVERS"] = "null"
            environment["ALSOFT_LOGLEVEL"] = "0"
            completed = support.run_command(
                build_command(executable),
                args.timeout,
                cwd=executable.parent,
                environment=environment,
            )
            stdout = support.decode_output(completed.stdout)
            engine_log = support.read_required(layout["logs"] / "RoR.log", "RoR log")
            script_log = support.read_required(
                layout["logs"] / "Angelscript.log", "AngelScript log"
            )
            telemetry = validate_logs(
                completed.returncode,
                stdout,
                engine_log,
                script_log,
                archive_sha256,
                not cache_initialized,
                contact_acceptance,
            )
            cache_initialized = True
            runtime_trace = support.find_single_trace(layout["logs"])
            label = f"worker-{workers:02d}-run-{run_index:02d}"
            trace_path = traces / f"{label}.rortrace"
            shutil.copy2(runtime_trace, trace_path)
            (diagnostics / f"{label}.stdout").write_text(stdout, encoding="utf-8")
            (diagnostics / f"{label}.RoR.log").write_text(engine_log, encoding="utf-8")
            (diagnostics / f"{label}.Angelscript.log").write_text(
                script_log, encoding="utf-8"
            )
            compare_traces(trace_tool, trace_path, trace_path, workers, workers, args.timeout)
            inspection = inspect_trace(trace_tool, trace_path, workers, args.timeout)
            conservation = bind_conservation_to_trace(telemetry, inspection)
            if baseline_conservation is None:
                baseline_conservation = conservation
            elif conservation != baseline_conservation:
                raise CollisionGateFailure(
                    "native contact-conservation telemetry changed across "
                    "worker counts or repeated runs"
                )
            if baseline is None:
                baseline = trace_path
                baseline_workers = workers
            else:
                comparison = compare_traces(
                    trace_tool,
                    baseline,
                    trace_path,
                    baseline_workers,
                    workers,
                    args.timeout,
                )
                state_comparisons.append(
                    {
                        "difference": comparison["difference"],
                        "first_divergent_step": comparison["first_divergent_step"],
                        "format": comparison["format"],
                        "left_metadata": comparison["left"]["metadata"],
                        "left_trace": str(baseline.relative_to(artifact_dir)),
                        "right_metadata": comparison["right"]["metadata"],
                        "right_trace": str(trace_path.relative_to(artifact_dir)),
                        "status": comparison["status"],
                        "steps_compared": comparison["steps_compared"],
                    }
                )
            trace_sha256 = support.sha256_file(trace_path)
            results.append(
                {
                    "contact_summary": inspection["contact_summary"],
                    "final_state_digest": inspection["final_step"]["state_digest"],
                    "run": run_index,
                    "telemetry": telemetry,
                    "trace": str(trace_path.relative_to(artifact_dir)),
                    "trace_bytes": inspection["bytes_read"],
                    "trace_sha256": trace_sha256,
                    "workers": workers,
                }
            )
            print(
                "J2 inter-actor collision matched: "
                f"workers={workers} run={run_index}/{args.runs} "
                f"contacts={inspection['contact_summary']['total_contact_count']} "
                f"sha256={trace_sha256}"
            )

    report = {
        "contact_acceptance": contact_acceptance,
        "contact_acceptance_canonicalization": (
            CONTACT_ACCEPTANCE_CANONICALIZATION
        ),
        "contact_acceptance_sha256": acceptance_sha256,
        "documentation_profile": profile["documentationProfile"],
        "executable": str(executable),
        "executable_sha256": support.sha256_file(executable),
        "fixture_id": profile["fixtureId"],
        "fixture_profile": str(staged_profile.relative_to(artifact_dir)),
        "fixture_profile_sha256": profile_sha256,
        "format": "ror-j2-authenticated-inter-actor-collision-v4",
        "jbeam_archive_sha256": archive_sha256,
        "jbeam_source_sha256": sha256_bytes(jbeam),
        "machine": platform.machine(),
        "platform": platform.platform(),
        "repository_commit": support.git_output(repository, ("rev-parse", "HEAD")),
        "results": results,
        "runs_per_worker": args.runs,
        "scenario_id": SCENARIO_ID,
        "scope": (
            "clean-room-normaltype-native-inter-actor-contact-conservation-"
            "not-beamng-force-parity"
        ),
        "script_sha256": sha256_bytes(script),
        "state_comparisons": state_comparisons,
        "steps": EXPECTED_STEPS,
        "workers": list(args.workers),
    }
    temporary = artifact_dir / "report.json.tmp"
    final = artifact_dir / "report.json"
    temporary.write_text(
        json.dumps(report, allow_nan=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    os.replace(temporary, final)
    print(f"J2 authenticated inter-actor collision passed: {final}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except CollisionGateFailure as error:
        print(f"JBeam inter-actor collision failed: {error}", file=sys.stderr)
        raise SystemExit(1)
