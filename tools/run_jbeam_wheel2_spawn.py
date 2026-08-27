#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the authenticated JBeam pressureWheel-to-native-Wheel2 spawn gate.

The tool performs no downloads. It stages exact project-owned fixture bytes,
builds a deterministic one-member archive, executes the product cache/import/
spawn path for 20,000 fixed steps, and requires exact one/eight-worker state
trace equality. The claim is limited to generated topology counts and node
classes, finite positive state sampled at exact 100-step batch boundaries,
and zero broken beams. It does not
qualify pressure-volume, friction, braking, propulsion, steering, rolling,
driveability, source-engine parity, playability, settle behavior, gravity
response, contact behavior, or per-step numeric state bounds.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import math
import os
from pathlib import Path
import platform
import re
import stat
import sys
from typing import Mapping, Sequence
import zipfile

import run_calibrated_beam_soak as support
import run_jbeam_spawn_soak as package_support


PROFILE_RELATIVE = Path(
    "tests/fixtures/beamng/jbeam_wheel2_spawn/fixture-profile.json"
)
JBEAM_RELATIVE = Path(
    "tests/fixtures/beamng/jbeam_wheel2_spawn/"
    "vehicles/ror_jbeam_wheel2/main.jbeam"
)
SCRIPT_RELATIVE = Path("resources/scripts/example_jbeam_wheel2_spawn.as")
JBEAM_MEMBER = "vehicles/ror_jbeam_wheel2/main.jbeam"
JBEAM_ARCHIVE = "RoRJBeamWheel2Spawn.zip"
SCRIPT_MEMBER = "example_jbeam_wheel2_spawn.as"
VEHICLE = "ror_jbeam_wheel2_fixture.jbeam"
TERRAIN = "simple2.terrn2"
SCENARIO_ID = 2026082701
EXPECTED_STEPS = 20000
EXPECTED_ACTORS = 1
EXPECTED_STATE_DIGEST_SCHEMA_VERSION = 3
EXPECTED_PHYSICS_FLAGS = 1
EXPECTED_TOPOLOGY = {
    "auditSamples": 201,
    "auditStrideFixedSteps": 100,
    "cabTriangles": 0,
    "collisionCabTriangles": 0,
    "contacters": 32,
    "fixedSteps": EXPECTED_STEPS,
    "generatedWheelBeams": 384,
    "generatedWheelNodes": 64,
    "groundContactNodes": 73,
    "initialTranslationY": 0.75,
    "initialVelocityMps": 0,
    "physicsStepDenominator": 2000,
    "physicsStepNumerator": 1,
    "raysPerWheel": 16,
    "runtimeBeams": 392,
    "runtimeNodes": 73,
    "runtimeRimNodes": 32,
    "runtimeStructuralNodes": 9,
    "runtimeTireNodes": 32,
    "sourceStructuralBeams": 8,
    "sourceStructuralNodes": 9,
    "sourceWheels": 1,
    "wheelTireNodeCount": 32,
}
EXPECTED_BATCH_BOUNDARY_ENVELOPE = {
    "brokenBeams": 0,
    "maximumSampledAbsolutePositionM": 1000000,
    "maximumSampledAbsoluteVelocityMps": 10000,
    "maximumSampledCenterOfMassDropM": 100,
    "maximumFinalCenterSpeedMps": 10000,
    "maximumFinalNodeSpeedMps": 10000,
    "minimumSampledCenterOfMassDropM": 0,
    "minimumFinalCenterSpeedMps": 0,
    "minimumFinalNodeSpeedMps": 0,
    "minimumMaximumSampledAbsolutePositionExclusiveM": 0,
    "minimumMaximumSampledAbsoluteVelocityMps": 0,
    "minimumNodeMassExclusiveKg": 0,
    "minimumTotalMassExclusiveKg": 0,
    "schema": 1,
}
EXPECTED_CLAIMS = {
    "authenticatedProductImportSpawn": True,
    "batchBoundaryFinitePositiveNodeState": True,
    "boundedBatchBoundaryState": True,
    "braking": False,
    "contactBehavior": False,
    "driveability": False,
    "exactGeneratedTopologyCountsAndNodeClasses": True,
    "exactOneEightWorkerTraceEquality": True,
    "friction": False,
    "gravityResponse": False,
    "perStepNumericStateBounds": False,
    "playability": False,
    "pressureVolume": False,
    "propulsion": False,
    "rolling": False,
    "settleBehavior": False,
    "sourceEngineParity": False,
    "steering": False,
    "zeroBrokenBeams": True,
}
START_MARKER = (
    "[RoR|J3|Wheel2Spawn] START scenario=2026082701 "
    "vehicle=ror_jbeam_wheel2_fixture.jbeam steps=20000 "
    "pressure_volume=false friction=false braking=false "
    "propulsion=false steering=false rolling=false driveability=false "
    "source_parity=false playability=false gravity_response=false "
    "contact_behavior=false per_step_numeric_bounds=false"
)
ARM_MARKER = (
    "[RoR|J3|Wheel2Spawn] ARMED actors=1 nodes=73 beams=392 "
    "structural_nodes=9 rim_nodes=32 tire_nodes=32 "
    "wheel_tire_nodes=32 contacters=32 ground_contact_nodes=73 "
    "cab_triangles=0 collision_cab_triangles=0 first_step=0 "
    "audit_stride=100 audit_samples_expected=201"
)
PASS_PATTERN = re.compile(
    r"\[RoR\|J3\|Wheel2Spawn\] PASS actors=1 nodes=73 beams=392 "
    r"structural_nodes=9 rim_nodes=32 tire_nodes=32 "
    r"wheel_tire_nodes=32 contacters=32 ground_contact_nodes=73 "
    r"cab_triangles=0 collision_cab_triangles=0 steps=20000 "
    r"audit_stride=100 audit_samples=201 "
    r"total_mass=(?P<total_mass>[-+0-9.eE]+) "
    r"minimum_node_mass=(?P<minimum_node_mass>[-+0-9.eE]+) "
    r"sampled_center_drop=(?P<center_drop>[-+0-9.eE]+) "
    r"final_center_speed=(?P<final_center_speed>[-+0-9.eE]+) "
    r"final_maximum_node_speed=(?P<final_node_speed>[-+0-9.eE]+) "
    r"maximum_sampled_abs_position=(?P<maximum_position>[-+0-9.eE]+) "
    r"maximum_sampled_abs_velocity=(?P<maximum_velocity>[-+0-9.eE]+) "
    r"broken_beams=(?P<broken>[0-9]+) "
    r"pressure_volume=false friction=false braking=false "
    r"propulsion=false steering=false rolling=false driveability=false "
    r"source_parity=false playability=false gravity_response=false "
    r"contact_behavior=false per_step_numeric_bounds=false$",
    re.MULTILINE,
)
FATAL_MARKERS = (
    "[RoR|J3|Wheel2Spawn] FAIL",
    "[RoR|JBeam] Rejected actor spawn",
    "[RoR|ModCache|JBeam] Refused",
    "[RoR|ModCache|JBeam] Rejected",
    "State trace snapshot failed",
    "State trace append failed",
    "could not be finished",
    "Validation Failed: Sampler error:",
    "GL_INVALID_",
    "RenderingAPIException",
    "OGRE EXCEPTION",
    "EXC_BAD_ACCESS",
    "Segmentation fault",
)


class Wheel2SpawnFailure(RuntimeError):
    """Fail-closed diagnostic for invalid input, execution, or evidence."""


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _strict_json_object(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise Wheel2SpawnFailure(f"duplicate JSON object key: {key}")
        result[key] = value
    return result


def _reject_json_constant(token: str) -> object:
    raise Wheel2SpawnFailure(f"non-finite JSON constant: {token}")


def _require_finite_json(value: object, label: str) -> None:
    if isinstance(value, float) and not math.isfinite(value):
        raise Wheel2SpawnFailure(f"{label} contains a non-finite number")
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
    except Wheel2SpawnFailure:
        raise
    except (json.JSONDecodeError, OverflowError, TypeError, ValueError) as error:
        raise Wheel2SpawnFailure(f"{label} is not strict JSON") from error
    _require_finite_json(value, label)
    return value


def exact_json_equal(left: object, right: object) -> bool:
    if type(left) is not type(right):
        return False
    if isinstance(left, dict):
        return set(left) == set(right) and all(
            exact_json_equal(left[key], right[key]) for key in left
        )
    if isinstance(left, list):
        return len(left) == len(right) and all(
            exact_json_equal(left_value, right_value)
            for left_value, right_value in zip(left, right)
        )
    return left == right


def read_direct_bytes(path: Path, label: str, byte_limit: int) -> bytes:
    try:
        before = os.lstat(path)
    except OSError as error:
        raise Wheel2SpawnFailure(f"{label} is missing or indirect: {path}") from error
    if not stat.S_ISREG(before.st_mode):
        raise Wheel2SpawnFailure(f"{label} is not a direct regular file")
    flags = os.O_RDONLY
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    try:
        descriptor = os.open(path, flags)
    except OSError as error:
        raise Wheel2SpawnFailure(f"{label} is missing or indirect: {path}") from error
    try:
        metadata = os.fstat(descriptor)
        if (
            not stat.S_ISREG(metadata.st_mode)
            or metadata.st_size > byte_limit
            or (before.st_dev, before.st_ino)
            != (metadata.st_dev, metadata.st_ino)
        ):
            raise Wheel2SpawnFailure(f"{label} is not a bounded regular file")
        chunks: list[bytes] = []
        remaining = byte_limit + 1
        while remaining > 0:
            chunk = os.read(descriptor, min(65536, remaining))
            if not chunk:
                break
            chunks.append(chunk)
            remaining -= len(chunk)
        payload = b"".join(chunks)
        after = os.lstat(path)
        if (
            len(payload) != metadata.st_size
            or len(payload) > byte_limit
            or not stat.S_ISREG(after.st_mode)
            or (after.st_dev, after.st_ino, after.st_size)
            != (metadata.st_dev, metadata.st_ino, metadata.st_size)
        ):
            raise Wheel2SpawnFailure(f"{label} changed or exceeded its byte limit")
        return payload
    finally:
        os.close(descriptor)


def read_profile(
    repository: Path,
) -> tuple[dict[str, object], bytes, bytes, bytes]:
    profile_path = repository / PROFILE_RELATIVE
    jbeam_path = repository / JBEAM_RELATIVE
    script_path = repository / SCRIPT_RELATIVE
    profile_bytes = read_direct_bytes(profile_path, "fixture profile", 131072)
    jbeam_raw = read_direct_bytes(jbeam_path, "JBeam source", 1048576)
    script_raw = read_direct_bytes(script_path, "scenario script", 1048576)
    try:
        profile = decode_strict_json(
            profile_bytes.decode("utf-8"), "fixture profile"
        )
        jbeam = support.canonical_lf_text(jbeam_raw, "JBeam source")
        script = support.canonical_lf_text(script_raw, "scenario script")
    except UnicodeDecodeError as error:
        raise Wheel2SpawnFailure("authenticated input is not UTF-8") from error
    except support.SoakFailure as error:
        raise Wheel2SpawnFailure(str(error)) from error
    expected_keys = {
        "authorship",
        "documentationProfile",
        "execution",
        "expectedRuntime",
        "fixtureId",
        "jbeamSource",
        "license",
        "prohibitedInputs",
        "qualifiedClaims",
        "rootPart",
        "scenarioScript",
        "schema",
        "batchBoundaryStateEnvelope",
    }
    if not isinstance(profile, dict) or set(profile) != expected_keys:
        raise Wheel2SpawnFailure("fixture profile schema drifted")
    expected_jbeam = {
        "path": JBEAM_RELATIVE.relative_to(
            "tests/fixtures/beamng/jbeam_wheel2_spawn"
        ).as_posix(),
        "sha256": sha256_bytes(jbeam),
    }
    expected_script = {
        "path": SCRIPT_RELATIVE.as_posix(),
        "sha256": sha256_bytes(script),
    }
    if (
        type(profile.get("schema")) is not int
        or profile["schema"] != 1
        or profile.get("fixtureId")
        != "ror-jbeam-authenticated-wheel2-spawn-v1"
        or profile.get("documentationProfile")
        != "beamng-docs-0.38.5.0-2026-07-27"
        or profile.get("authorship") != "original-clean-room"
        or profile.get("license") != "GPL-3.0-or-later"
        or profile.get("execution") != "authenticated-product-path"
        or profile.get("rootPart") != "ror_jbeam_wheel2_fixture"
        or not exact_json_equal(profile.get("jbeamSource"), expected_jbeam)
        or not exact_json_equal(profile.get("scenarioScript"), expected_script)
        or not exact_json_equal(profile.get("expectedRuntime"), EXPECTED_TOPOLOGY)
        or not exact_json_equal(
            profile.get("batchBoundaryStateEnvelope"),
            EXPECTED_BATCH_BOUNDARY_ENVELOPE,
        )
        or not exact_json_equal(profile.get("qualifiedClaims"), EXPECTED_CLAIMS)
        or not exact_json_equal(profile.get("prohibitedInputs"), [
            "lua-execution",
            "ogre-script-execution",
            "network",
            "external-assets",
            "third-party-mod-data",
        ])
    ):
        raise Wheel2SpawnFailure("fixture profile does not match exact sources")
    return profile, profile_bytes, jbeam, script


def stage_exact_bytes(destination: Path, payload: bytes, label: str) -> str:
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    try:
        descriptor = os.open(destination, flags, 0o600)
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
    except OSError as error:
        raise Wheel2SpawnFailure(f"could not stage exact {label}") from error
    staged = read_direct_bytes(destination, f"staged {label}", len(payload))
    if staged != payload:
        raise Wheel2SpawnFailure(f"staged {label} changed bytes")
    return sha256_bytes(staged)


def build_archive_bytes(members: Mapping[str, bytes]) -> bytes:
    if not members or len(members) != len(set(members)):
        raise Wheel2SpawnFailure("archive member inventory is invalid")
    buffer = io.BytesIO()
    try:
        with zipfile.ZipFile(
            buffer,
            "w",
            compression=zipfile.ZIP_DEFLATED,
            compresslevel=9,
            strict_timestamps=True,
        ) as archive:
            for name in sorted(members):
                archive.writestr(
                    package_support.deterministic_zip_info(name), members[name]
                )
        payload = buffer.getvalue()
        with zipfile.ZipFile(io.BytesIO(payload), "r") as archive:
            if (
                archive.namelist() != sorted(members)
                or archive.testzip() is not None
            ):
                raise Wheel2SpawnFailure("derived archive inventory is not exact")
            for name, member in members.items():
                if archive.read(name) != member:
                    raise Wheel2SpawnFailure(
                        f"derived archive member changed: {name}"
                    )
    except (OSError, KeyError, zipfile.BadZipFile) as error:
        raise Wheel2SpawnFailure("could not build exact derived archive") from error
    return payload


def copy_direct_file_exclusive(
    source: Path,
    destination: Path,
    label: str,
    byte_limit: int = 2147483648,
) -> tuple[int, str]:
    try:
        before = os.lstat(source)
    except OSError as error:
        raise Wheel2SpawnFailure(f"{label} source is missing or indirect") from error
    if not stat.S_ISREG(before.st_mode) or not 0 < before.st_size <= byte_limit:
        raise Wheel2SpawnFailure(f"{label} source is not a bounded regular file")
    read_flags = os.O_RDONLY
    write_flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    if hasattr(os, "O_NOFOLLOW"):
        read_flags |= os.O_NOFOLLOW
        write_flags |= os.O_NOFOLLOW
    try:
        source_descriptor = os.open(source, read_flags)
    except OSError as error:
        raise Wheel2SpawnFailure(f"{label} source is missing or indirect") from error
    destination_descriptor = -1
    try:
        source_metadata = os.fstat(source_descriptor)
        if (
            not stat.S_ISREG(source_metadata.st_mode)
            or (before.st_dev, before.st_ino, before.st_size)
            != (
                source_metadata.st_dev,
                source_metadata.st_ino,
                source_metadata.st_size,
            )
        ):
            raise Wheel2SpawnFailure(f"{label} source identity changed")
        try:
            destination_descriptor = os.open(
                destination, write_flags, 0o600
            )
        except OSError as error:
            raise Wheel2SpawnFailure(
                f"could not stage exact {label} destination"
            ) from error
        digest = hashlib.sha256()
        copied = 0
        while copied < source_metadata.st_size:
            chunk = os.read(
                source_descriptor,
                min(1048576, source_metadata.st_size - copied),
            )
            if not chunk:
                raise Wheel2SpawnFailure(f"{label} source ended early")
            digest.update(chunk)
            offset = 0
            while offset < len(chunk):
                written = os.write(destination_descriptor, chunk[offset:])
                if written <= 0:
                    raise Wheel2SpawnFailure(f"{label} destination write failed")
                offset += written
            copied += len(chunk)
        if os.read(source_descriptor, 1):
            raise Wheel2SpawnFailure(f"{label} source exceeded its declared size")
        os.fsync(destination_descriptor)
        destination_metadata = os.fstat(destination_descriptor)
        after = os.lstat(source)
        if (
            not stat.S_ISREG(destination_metadata.st_mode)
            or destination_metadata.st_size != copied
            or not stat.S_ISREG(after.st_mode)
            or (
                after.st_dev,
                after.st_ino,
                after.st_size,
                after.st_mtime_ns,
                after.st_ctime_ns,
            )
            != (
                source_metadata.st_dev,
                source_metadata.st_ino,
                source_metadata.st_size,
                source_metadata.st_mtime_ns,
                source_metadata.st_ctime_ns,
            )
        ):
            raise Wheel2SpawnFailure(f"{label} source or destination changed")
        return copied, digest.hexdigest()
    finally:
        os.close(source_descriptor)
        if destination_descriptor >= 0:
            os.close(destination_descriptor)


def resolve_direct_file(path: Path, label: str) -> Path:
    if path.is_symlink() or not path.is_file():
        raise Wheel2SpawnFailure(f"{label} is missing or indirect: {path}")
    return path.resolve(strict=True)


def build_command(executable: Path) -> tuple[str, ...]:
    command = [str(executable)]
    if sys.platform == "darwin":
        command.extend(("-ApplePersistenceIgnoreState", "YES"))
    command.extend(
        (
            "-checkcache",
            "-map",
            TERRAIN,
            "-truck",
            VEHICLE,
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
    envelope: dict[str, object],
) -> dict[str, object]:
    if returncode != 0:
        raise Wheel2SpawnFailure(f"RoR Wheel2 spawn gate exited with {returncode}")
    for marker in (START_MARKER, ARM_MARKER):
        marker_matches = list(
            re.finditer(re.escape(marker) + r"$", script_log, re.MULTILINE)
        )
        if len(marker_matches) != 1:
            raise Wheel2SpawnFailure(
                "AngelScript log did not contain exactly one closed marker: "
                f"{marker}"
            )
    engine_markers = (
        "[RoR|ModCache|JBeam] Mounted exact archive",
        f"archive_sha256={archive_sha256}",
        "roots=1",
        "[RoR|Determinism] Recording state trace",
        "scenario=2026082701",
        "limit=20000",
        "with 20000 fixed-step records (trace step limit reached)",
    )
    if require_scan_receipt:
        engine_markers += (
            "[RoR|ModCache|JBeam] Added exact root "
            "'ror_jbeam_wheel2_fixture'",
            "nodes=9, beams=8, hydros=0",
        )
    for marker in engine_markers:
        if marker not in engine_log:
            raise Wheel2SpawnFailure(f"engine log missed marker: {marker}")
    combined = "\n".join((stdout, engine_log, script_log))
    for marker in FATAL_MARKERS:
        if marker in combined:
            raise Wheel2SpawnFailure(f"runtime logged a fatal marker: {marker}")
    matches = list(PASS_PATTERN.finditer(script_log))
    if len(matches) != 1:
        raise Wheel2SpawnFailure(
            f"expected one Wheel2 PASS receipt, found {len(matches)}"
        )
    match = matches[0]
    try:
        numeric = {
            "sampled_center_drop_m": float(match.group("center_drop")),
            "final_center_speed_mps": float(
                match.group("final_center_speed")
            ),
            "final_maximum_node_speed_mps": float(
                match.group("final_node_speed")
            ),
            "maximum_sampled_absolute_position_m": float(
                match.group("maximum_position")
            ),
            "maximum_sampled_absolute_velocity_mps": float(
                match.group("maximum_velocity")
            ),
            "minimum_node_mass_kg": float(match.group("minimum_node_mass")),
            "total_mass_kg": float(match.group("total_mass")),
        }
    except ValueError as error:
        raise Wheel2SpawnFailure("Wheel2 telemetry number is invalid") from error
    if not all(math.isfinite(value) for value in numeric.values()):
        raise Wheel2SpawnFailure("Wheel2 telemetry contains a non-finite value")
    broken = int(match.group("broken"))
    if (
        numeric["total_mass_kg"]
        <= float(envelope["minimumTotalMassExclusiveKg"])
        or numeric["minimum_node_mass_kg"]
        <= float(envelope["minimumNodeMassExclusiveKg"])
        or not float(envelope["minimumSampledCenterOfMassDropM"])
        <= numeric["sampled_center_drop_m"]
        <= float(envelope["maximumSampledCenterOfMassDropM"])
        or not float(envelope["minimumFinalCenterSpeedMps"])
        <= numeric["final_center_speed_mps"]
        <= float(envelope["maximumFinalCenterSpeedMps"])
        or not float(envelope["minimumFinalNodeSpeedMps"])
        <= numeric["final_maximum_node_speed_mps"]
        <= float(envelope["maximumFinalNodeSpeedMps"])
        or not float(
            envelope["minimumMaximumSampledAbsolutePositionExclusiveM"]
        )
        < numeric["maximum_sampled_absolute_position_m"]
        <= float(envelope["maximumSampledAbsolutePositionM"])
        or not float(envelope["minimumMaximumSampledAbsoluteVelocityMps"])
        <= numeric["maximum_sampled_absolute_velocity_mps"]
        <= float(envelope["maximumSampledAbsoluteVelocityMps"])
        or numeric["total_mass_kg"]
        < numeric["minimum_node_mass_kg"] * EXPECTED_TOPOLOGY["runtimeNodes"]
        or numeric["final_maximum_node_speed_mps"]
        < numeric["final_center_speed_mps"]
        or numeric["maximum_sampled_absolute_velocity_mps"]
        < numeric["final_maximum_node_speed_mps"]
        or broken != envelope["brokenBeams"]
    ):
        raise Wheel2SpawnFailure("Wheel2 telemetry left the safety envelope")
    return {**numeric, "broken_beams": broken}


def require_exact_object(
    value: object, keys: set[str], label: str
) -> dict[str, object]:
    if not isinstance(value, dict) or set(value) != keys:
        raise Wheel2SpawnFailure(f"{label} key set changed")
    return value


def require_exact_int(value: object, expected: int, label: str) -> None:
    if type(value) is not int or value != expected:
        raise Wheel2SpawnFailure(f"{label} changed")


def validate_trace_metadata(
    value: object, workers: int, digest_key: str, label: str
) -> dict[str, object]:
    metadata = require_exact_object(
        value,
        {
            digest_key,
            "first_physics_step",
            "physics_flags",
            "physics_step_denominator",
            "physics_step_numerator",
            "scenario_id",
            "worker_count",
        },
        label,
    )
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
        raise Wheel2SpawnFailure("trace comparison did not report an exact match")
    require_exact_int(payload["steps_compared"], EXPECTED_STEPS, "trace steps")
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
            raise Wheel2SpawnFailure(f"{side_name} trace binding changed")
        validate_trace_metadata(
            side["metadata"],
            workers,
            "digest_schema_version",
            f"{side_name} comparison metadata",
        )
        error = require_exact_object(
            side["error"],
            {"byte_offset", "code", "step_index"},
            f"{side_name} trace error",
        )
        if error["code"] != "none":
            raise Wheel2SpawnFailure(f"{side_name} trace reported an error")
        require_exact_int(error["byte_offset"], 0, "trace error byte offset")
        require_exact_int(error["step_index"], 0, "trace error step index")
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
    payload = decode_strict_json(output, "trace comparator output")
    if completed.returncode != 0:
        raise Wheel2SpawnFailure(f"Wheel2 traces diverged: {output}")
    return validate_trace_comparison(
        payload, left, right, left_workers, right_workers
    )


def validate_trace_inspection(
    value: object, trace: Path, workers: int
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
        raise Wheel2SpawnFailure("trace inspection binding changed")
    require_exact_int(payload["step_count"], EXPECTED_STEPS, "trace step count")
    trace_bytes = trace.stat().st_size
    require_exact_int(payload["bytes_read"], trace_bytes, "trace bytes read")
    if trace_bytes <= 0:
        raise Wheel2SpawnFailure("trace is empty")
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
    for key in (
        "contact_step_count",
        "maximum_contact_count",
        "total_contact_count",
    ):
        if type(summary[key]) is not int or summary[key] < 0:
            raise Wheel2SpawnFailure(f"trace contact summary {key} is invalid")
    if summary["contact_step_count"] == 0:
        if (
            summary["total_contact_count"] != 0
            or summary["maximum_contact_count"] != 0
            or summary["first_contact_physics_step"] is not None
            or summary["last_contact_physics_step"] is not None
        ):
            raise Wheel2SpawnFailure("empty contact summary is inconsistent")
    else:
        first = summary["first_contact_physics_step"]
        last = summary["last_contact_physics_step"]
        if (
            type(first) is not int
            or type(last) is not int
            or first < 0
            or last < first
            or last >= EXPECTED_STEPS
            or summary["maximum_contact_count"] <= 0
            or summary["total_contact_count"]
            < summary["contact_step_count"]
            or summary["total_contact_count"]
            > summary["contact_step_count"] * summary["maximum_contact_count"]
        ):
            raise Wheel2SpawnFailure("nonempty contact summary is inconsistent")
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
    require_exact_int(final_step["physics_step"], EXPECTED_STEPS - 1, "final step")
    require_exact_int(final_step["actor_count"], EXPECTED_ACTORS, "actor count")
    if (
        type(final_step["contact_count"]) is not int
        or final_step["contact_count"] < 0
        or final_step["contact_count"] > summary["maximum_contact_count"]
        or final_step["input_digest"] is not None
        or not isinstance(final_step["state_digest"], str)
        or re.fullmatch(r"[0-9a-f]{64}", final_step["state_digest"]) is None
    ):
        raise Wheel2SpawnFailure("trace final step is inconsistent")
    return payload


def inspect_trace(
    trace_tool: Path, trace: Path, workers: int, timeout: int
) -> dict[str, object]:
    completed = support.run_command(
        (str(trace_tool), "--inspect", str(trace)), timeout
    )
    output = support.decode_output(completed.stdout)
    payload = decode_strict_json(output, "trace inspector output")
    if completed.returncode != 0:
        raise Wheel2SpawnFailure(f"Wheel2 trace inspection failed: {output}")
    return validate_trace_inspection(payload, trace, workers)


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--trace-tool", required=True, type=Path)
    parser.add_argument(
        "--repository", type=Path, default=Path(__file__).resolve().parents[1]
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
    if tuple(args.workers) != (1, 8):
        parser.error("--workers must be exactly 1 8 for qualification")
    return args


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    executable = resolve_direct_file(args.executable, "executable")
    trace_tool = resolve_direct_file(args.trace_tool, "trace tool")
    repository = args.repository.resolve()
    artifact_dir = args.artifact_dir.resolve()
    if artifact_dir.exists():
        raise Wheel2SpawnFailure(f"artifact directory already exists: {artifact_dir}")
    artifact_dir.mkdir(parents=True)

    profile, profile_bytes, jbeam, script = read_profile(repository)
    inputs = artifact_dir / "inputs"
    inputs.mkdir()
    staged_profile = inputs / "fixture-profile.json"
    staged_jbeam = inputs / "main.jbeam"
    staged_script = inputs / SCRIPT_MEMBER
    profile_sha256 = stage_exact_bytes(
        staged_profile, profile_bytes, "fixture profile"
    )
    jbeam_sha256 = stage_exact_bytes(staged_jbeam, jbeam, "JBeam source")
    script_sha256 = stage_exact_bytes(staged_script, script, "scenario script")
    staged_archive = inputs / JBEAM_ARCHIVE
    archive_bytes = build_archive_bytes({JBEAM_MEMBER: jbeam})
    archive_sha256 = stage_exact_bytes(
        staged_archive, archive_bytes, "derived JBeam archive"
    )

    runtime_content = (
        support.infer_runtime_content(executable)
        if args.runtime_content is None
        else args.runtime_content.resolve()
    )
    if not runtime_content.is_dir():
        raise Wheel2SpawnFailure(f"runtime content is missing: {runtime_content}")
    package_support.verify_runtime_terrain(runtime_content)

    isolated_home = artifact_dir / "work" / "jbeam-wheel2-spawn-home"
    layout = support.runtime_layout(isolated_home, sys.platform)
    for key in ("config", "logs", "mods"):
        layout[key].mkdir(parents=True, exist_ok=True)
    runtime_archive = layout["mods"] / JBEAM_ARCHIVE
    if stage_exact_bytes(
        runtime_archive, archive_bytes, "runtime JBeam archive"
    ) != archive_sha256:
        raise Wheel2SpawnFailure("runtime archive staging changed bytes")
    scripts = layout["user"] / "scripts"
    scripts.mkdir(parents=True, exist_ok=True)
    runtime_script = scripts / SCRIPT_MEMBER
    if stage_exact_bytes(
        runtime_script, script, "runtime scenario script"
    ) != script_sha256:
        raise Wheel2SpawnFailure("runtime script staging changed bytes")

    traces = artifact_dir / "traces"
    diagnostics = artifact_dir / "diagnostics"
    traces.mkdir()
    diagnostics.mkdir()
    baseline: Path | None = None
    baseline_workers = 0
    baseline_telemetry: dict[str, object] | None = None
    cache_initialized = False
    results: list[dict[str, object]] = []
    state_comparisons: list[dict[str, object]] = []

    for workers in args.workers:
        for run_index in range(1, args.runs + 1):
            support.write_runtime_config(
                layout["config"], workers, not cache_initialized
            )
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
            engine_log = support.read_required(
                layout["logs"] / "RoR.log", "RoR log"
            )
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
                profile["batchBoundaryStateEnvelope"],
            )
            cache_initialized = True
            if baseline_telemetry is None:
                baseline_telemetry = telemetry
            elif telemetry != baseline_telemetry:
                raise Wheel2SpawnFailure(
                    "Wheel2 counts/classes or batch-boundary telemetry changed "
                    "across "
                    "workers or runs"
                )
            runtime_trace = support.find_single_trace(layout["logs"])
            label = f"worker-{workers:02d}-run-{run_index:02d}"
            trace_path = traces / f"{label}.rortrace"
            trace_bytes, trace_sha256 = copy_direct_file_exclusive(
                runtime_trace, trace_path, "state trace"
            )
            for suffix, payload in (
                ("stdout", stdout),
                ("RoR.log", engine_log),
                ("Angelscript.log", script_log),
            ):
                stage_exact_bytes(
                    diagnostics / f"{label}.{suffix}",
                    payload.encode("utf-8"),
                    f"{label} {suffix}",
                )
            compare_traces(
                trace_tool,
                trace_path,
                trace_path,
                workers,
                workers,
                args.timeout,
            )
            inspection = inspect_trace(
                trace_tool, trace_path, workers, args.timeout
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
                        "first_divergent_step": comparison[
                            "first_divergent_step"
                        ],
                        "format": comparison["format"],
                        "left_metadata": comparison["left"]["metadata"],
                        "left_trace": str(baseline.relative_to(artifact_dir)),
                        "right_metadata": comparison["right"]["metadata"],
                        "right_trace": str(trace_path.relative_to(artifact_dir)),
                        "status": comparison["status"],
                        "steps_compared": comparison["steps_compared"],
                    }
                )
            results.append(
                {
                    "contact_summary": inspection["contact_summary"],
                    "final_state_digest": inspection["final_step"][
                        "state_digest"
                    ],
                    "run": run_index,
                    "telemetry": telemetry,
                    "trace": str(trace_path.relative_to(artifact_dir)),
                    "trace_bytes": trace_bytes,
                    "trace_sha256": trace_sha256,
                    "workers": workers,
                }
            )
            print(
                "J3 authenticated Wheel2 spawn matched: "
                f"workers={workers} run={run_index}/{args.runs} "
                f"sha256={trace_sha256}"
            )

    source_inventory = [
        {
            "bytes": len(profile_bytes),
            "path": str(staged_profile.relative_to(artifact_dir)),
            "sha256": profile_sha256,
        },
        {
            "bytes": len(jbeam),
            "path": str(staged_jbeam.relative_to(artifact_dir)),
            "sha256": jbeam_sha256,
        },
        {
            "bytes": len(script),
            "path": str(staged_script.relative_to(artifact_dir)),
            "sha256": script_sha256,
        },
        {
            "bytes": staged_archive.stat().st_size,
            "path": str(staged_archive.relative_to(artifact_dir)),
            "sha256": archive_sha256,
        },
    ]
    report = {
        "documentation_profile": profile["documentationProfile"],
        "executable": str(executable),
        "executable_sha256": support.sha256_file(executable),
        "fixture_id": profile["fixtureId"],
        "fixture_profile": str(staged_profile.relative_to(artifact_dir)),
        "fixture_profile_sha256": profile_sha256,
        "format": "ror-j3-authenticated-jbeam-wheel2-spawn-v1",
        "jbeam_archive_sha256": archive_sha256,
        "jbeam_source_sha256": jbeam_sha256,
        "machine": platform.machine(),
        "platform": platform.platform(),
        "qualified_claims": profile["qualifiedClaims"],
        "repository_commit": support.git_output(repository, ("rev-parse", "HEAD")),
        "results": results,
        "runs_per_worker": args.runs,
        "scenario_id": SCENARIO_ID,
        "scope": (
            "clean-room-authenticated-pressurewheel-to-native-wheel2-"
            "spawn-topology-counts-node-classes-finite-batch-boundary-"
            "state-not-vehicle-parity"
        ),
        "script_sha256": script_sha256,
        "batch_boundary_state_envelope": profile[
            "batchBoundaryStateEnvelope"
        ],
        "source_inventory": source_inventory,
        "state_comparisons": state_comparisons,
        "steps": EXPECTED_STEPS,
        "runtime_topology": profile["expectedRuntime"],
        "workers": list(args.workers),
    }
    final = artifact_dir / "report.json"
    report_bytes = (
        json.dumps(report, allow_nan=False, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")
    stage_exact_bytes(
        final,
        report_bytes,
        "qualification report",
    )
    print(f"J3 authenticated JBeam Wheel2 spawn passed: {final}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Wheel2SpawnFailure as error:
        print(f"JBeam Wheel2 spawn failed: {error}", file=sys.stderr)
        raise SystemExit(1)
