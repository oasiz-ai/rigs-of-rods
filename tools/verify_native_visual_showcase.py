#!/usr/bin/env python3
"""Fail-closed verifier for the forward-native visual showcase report."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import sys
from pathlib import Path
from typing import Any


CONTRACT_SCHEMA = "ror.native_visual_showcase_contract.v1"
REPORT_SCHEMA = "ror.native_visual_showcase_report.v1"
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")
ALLOWED_NATIVE_APIS = {"metal", "d3d12", "vulkan"}
ALLOWED_ORIGINS = {
    "project_original",
    "clean_room_recreation",
    "rights_cleared_derivative",
}
ALLOWED_COLOR_SPACES = {"linear_hdr", "srgb"}


class VerificationError(ValueError):
    pass


def _fail(message: str) -> None:
    raise VerificationError(message)


def _dict(value: Any, field: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        _fail(f"{field} must be an object")
    return value


def _list(value: Any, field: str) -> list[Any]:
    if not isinstance(value, list):
        _fail(f"{field} must be an array")
    return value


def _string(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value:
        _fail(f"{field} must be a non-empty string")
    return value


def _integer(value: Any, field: str, minimum: int = 0) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        _fail(f"{field} must be an integer >= {minimum}")
    return value


def _number(value: Any, field: str, minimum: float = 0.0) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        _fail(f"{field} must be a finite number")
    result = float(value)
    if not math.isfinite(result) or result < minimum:
        _fail(f"{field} must be a finite number >= {minimum}")
    return result


def _boolean(value: Any, field: str) -> bool:
    if not isinstance(value, bool):
        _fail(f"{field} must be a Boolean")
    return value


def _sha256(value: Any, field: str) -> str:
    result = _string(value, field)
    if not SHA256_RE.fullmatch(result):
        _fail(f"{field} must be a lowercase SHA-256")
    return result


def _unique_strings(value: Any, field: str) -> list[str]:
    output = [_string(item, f"{field}[]") for item in _list(value, field)]
    if len(output) != len(set(output)):
        _fail(f"{field} contains duplicates")
    return output


def _strict_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    output: dict[str, Any] = {}
    for key, value in pairs:
        if key in output:
            _fail(f"duplicate JSON key {key}")
        output[key] = value
    return output


def _reject_json_constant(value: str) -> None:
    _fail(f"non-finite JSON constant {value}")


def _load_json(path: Path, field: str) -> dict[str, Any]:
    try:
        raw = path.read_bytes()
    except OSError as exc:
        _fail(f"cannot read {field}: {exc}")
    try:
        value = json.loads(
            raw.decode("utf-8"),
            object_pairs_hook=_strict_object,
            parse_constant=_reject_json_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        _fail(f"{field} is not strict UTF-8 JSON: {exc}")
    return _dict(value, field)


def _validate_contract(contract: dict[str, Any]) -> tuple[dict[str, Any], list[str]]:
    if contract.get("schema") != CONTRACT_SCHEMA:
        _fail("unsupported showcase contract schema")
    if _integer(contract.get("version"), "contract.version", 1) != 1:
        _fail("showcase contract version must equal 1")
    if contract.get("product_path") != "forward-native":
        _fail("showcase contract must require the forward-native product path")
    _integer(contract.get("minimum_width"), "contract.minimum_width", 1)
    _integer(contract.get("minimum_height"), "contract.minimum_height", 1)
    _integer(
        contract.get("minimum_main_sequence_frames"),
        "contract.minimum_main_sequence_frames",
        1,
    )
    modes = _unique_strings(contract.get("required_modes"), "contract.required_modes")
    if set(modes) != {"raster_high", "native_rt_ultra"}:
        _fail("contract must require raster_high and native_rt_ultra exactly")
    artifacts = _unique_strings(
        contract.get("required_artifacts"), "contract.required_artifacts"
    )
    if not artifacts:
        _fail("contract must require acceptance artifacts")

    pass_objects = _list(contract.get("required_passes"), "contract.required_passes")
    passes: dict[str, Any] = {}
    for index, raw_pass in enumerate(pass_objects):
        item = _dict(raw_pass, f"contract.required_passes[{index}]")
        pass_id = _string(item.get("id"), f"contract pass {index}.id")
        if pass_id in passes:
            _fail(f"duplicate contract pass {pass_id}")
        _integer(item.get("version"), f"contract pass {pass_id}.version", 1)
        pass_modes = _unique_strings(item.get("modes"), f"contract pass {pass_id}.modes")
        if not pass_modes or not set(pass_modes).issubset(modes):
            _fail(f"contract pass {pass_id} has invalid modes")
        _unique_strings(item.get("depends_on"), f"contract pass {pass_id}.depends_on")
        _integer(
            item.get("minimum_completed_frames"),
            f"contract pass {pass_id}.minimum_completed_frames",
            1,
        )
        _string(item.get("witness_metric"), f"contract pass {pass_id}.witness_metric")
        _number(
            item.get("minimum_witness_value"),
            f"contract pass {pass_id}.minimum_witness_value",
            0.0,
        )
        passes[pass_id] = item

    if not passes:
        _fail("contract must contain rendering passes")
    for pass_id, item in passes.items():
        for dependency in item["depends_on"]:
            if dependency not in passes:
                _fail(f"contract pass {pass_id} has unknown dependency {dependency}")
            if not set(item["modes"]).issubset(passes[dependency]["modes"]):
                _fail(
                    f"contract pass {pass_id} cannot execute with dependency {dependency}"
                )

    visiting: set[str] = set()
    visited: set[str] = set()

    def visit(pass_id: str) -> None:
        if pass_id in visited:
            return
        if pass_id in visiting:
            _fail(f"contract pass dependency cycle includes {pass_id}")
        visiting.add(pass_id)
        for dependency in passes[pass_id]["depends_on"]:
            visit(dependency)
        visiting.remove(pass_id)
        visited.add(pass_id)

    for pass_id in passes:
        visit(pass_id)
    return passes, artifacts


def _validate_artifacts(
    report: dict[str, Any], contract: dict[str, Any], required: list[str]
) -> None:
    artifacts = _list(report.get("artifacts"), "report.artifacts")
    by_name: dict[str, dict[str, Any]] = {}
    modes = set(contract["required_modes"])
    for index, raw_artifact in enumerate(artifacts):
        item = _dict(raw_artifact, f"report.artifacts[{index}]")
        name = _string(item.get("name"), f"artifact {index}.name")
        if name in by_name:
            _fail(f"duplicate artifact {name}")
        if name not in required:
            _fail(f"unrecognized showcase artifact {name}")
        mode = _string(item.get("mode"), f"artifact {name}.mode")
        if mode not in modes:
            _fail(f"artifact {name} has unknown mode")
        _sha256(item.get("sha256"), f"artifact {name}.sha256")
        _integer(
            item.get("width"),
            f"artifact {name}.width",
            contract["minimum_width"],
        )
        _integer(
            item.get("height"),
            f"artifact {name}.height",
            contract["minimum_height"],
        )
        _integer(item.get("frame_id"), f"artifact {name}.frame_id", 1)
        color_space = _string(item.get("color_space"), f"artifact {name}.color_space")
        if color_space not in ALLOWED_COLOR_SPACES:
            _fail(f"artifact {name} has unsupported color space")
        if not _boolean(item.get("accepted"), f"artifact {name}.accepted"):
            _fail(f"artifact {name} is not accepted")
        by_name[name] = item
    missing = sorted(set(required) - set(by_name))
    if missing:
        _fail("missing showcase artifacts: " + ", ".join(missing))


def _validate_pass(
    evidence: dict[str, Any], pass_contract: dict[str, Any], mode_id: str
) -> None:
    pass_id = pass_contract["id"]
    if evidence.get("id") != pass_id:
        _fail(f"mode {mode_id} pass identity mismatch for {pass_id}")
    if _integer(evidence.get("version"), f"pass {pass_id}.version", 1) != int(
        pass_contract["version"]
    ):
        _fail(f"mode {mode_id} pass {pass_id} version mismatch")
    if evidence.get("status") != "executed":
        _fail(f"mode {mode_id} pass {pass_id} did not execute")
    completed = _integer(
        evidence.get("completed_frames"),
        f"pass {pass_id}.completed_frames",
        int(pass_contract["minimum_completed_frames"]),
    )
    _integer(
        evidence.get("state_verifications"),
        f"pass {pass_id}.state_verifications",
        completed,
    )
    _integer(
        evidence.get("gpu_timestamp_samples"),
        f"pass {pass_id}.gpu_timestamp_samples",
        completed,
    )
    p50 = _number(
        evidence.get("gpu_time_ns_p50"), f"pass {pass_id}.gpu_time_ns_p50", 1.0
    )
    p95 = _number(
        evidence.get("gpu_time_ns_p95"), f"pass {pass_id}.gpu_time_ns_p95", p50
    )
    if p95 < p50:
        _fail(f"mode {mode_id} pass {pass_id} p95 is below p50")
    if _integer(
        evidence.get("production_content_readbacks"),
        f"pass {pass_id}.production_content_readbacks",
        0,
    ) != 0:
        _fail(f"mode {mode_id} pass {pass_id} performed a production content readback")
    _sha256(evidence.get("input_lineage"), f"pass {pass_id}.input_lineage")
    _sha256(evidence.get("output_lineage"), f"pass {pass_id}.output_lineage")

    witness = _dict(evidence.get("witness"), f"pass {pass_id}.witness")
    if witness.get("metric") != pass_contract["witness_metric"]:
        _fail(f"mode {mode_id} pass {pass_id} has the wrong witness metric")
    _number(
        witness.get("value"),
        f"pass {pass_id}.witness.value",
        float(pass_contract["minimum_witness_value"]),
    )
    before = _sha256(witness.get("before_sha256"), f"pass {pass_id}.witness.before")
    after = _sha256(witness.get("after_sha256"), f"pass {pass_id}.witness.after")
    if before == after:
        _fail(f"mode {mode_id} pass {pass_id} has no visible off/on change")


def _validate_mode(
    mode: dict[str, Any], contract: dict[str, Any], passes: dict[str, Any]
) -> str:
    mode_id = _string(mode.get("id"), "report mode id")
    if mode_id not in contract["required_modes"]:
        _fail(f"unknown report mode {mode_id}")
    completed = _integer(
        mode.get("completed_frames"),
        f"mode {mode_id}.completed_frames",
        contract["minimum_main_sequence_frames"],
    )
    _integer(mode.get("warmup_frames"), f"mode {mode_id}.warmup_frames", 120)
    for field in (
        "legacy_render_passes",
        "visible_legacy_assets",
        "legacy_material_fallbacks",
        "missing_resource_fallbacks",
        "runtime_asset_conversions",
        "production_content_readbacks",
        "production_framebuffer_readbacks",
    ):
        if _integer(mode.get(field), f"mode {mode_id}.{field}", 0) != 0:
            _fail(f"mode {mode_id} requires {field}=0")
    if not _boolean(
        mode.get("acceptance_readbacks_scoped_to_artifacts"),
        f"mode {mode_id}.acceptance_readbacks_scoped_to_artifacts",
    ):
        _fail(f"mode {mode_id} has unscoped acceptance readbacks")

    raw_evidence = _list(mode.get("passes"), f"mode {mode_id}.passes")
    evidence_by_id: dict[str, dict[str, Any]] = {}
    for index, raw_pass in enumerate(raw_evidence):
        item = _dict(raw_pass, f"mode {mode_id}.passes[{index}]")
        pass_id = _string(item.get("id"), f"mode {mode_id} pass id")
        if pass_id in evidence_by_id:
            _fail(f"mode {mode_id} contains duplicate pass {pass_id}")
        if pass_id not in passes or mode_id not in passes[pass_id]["modes"]:
            _fail(f"mode {mode_id} contains unrecognized pass {pass_id}")
        evidence_by_id[pass_id] = item
    required_ids = {
        pass_id for pass_id, item in passes.items() if mode_id in item["modes"]
    }
    missing = sorted(required_ids - set(evidence_by_id))
    if missing:
        _fail(f"mode {mode_id} is missing passes: " + ", ".join(missing))
    for pass_id in sorted(required_ids):
        _validate_pass(evidence_by_id[pass_id], passes[pass_id], mode_id)
        if int(evidence_by_id[pass_id]["completed_frames"]) > completed:
            _fail(f"mode {mode_id} pass {pass_id} exceeds mode frame lineage")
    return mode_id


def verify(contract_path: Path, report_path: Path) -> None:
    contract_bytes = contract_path.read_bytes()
    contract = _load_json(contract_path, "contract")
    passes, required_artifacts = _validate_contract(contract)
    report = _load_json(report_path, "report")
    if report.get("schema") != REPORT_SCHEMA:
        _fail("unsupported showcase report schema")
    if _sha256(report.get("contract_sha256"), "report.contract_sha256") != hashlib.sha256(
        contract_bytes
    ).hexdigest():
        _fail("showcase report contract hash mismatch")
    if report.get("renderer") != "ogre-next":
        _fail("showcase report renderer must be ogre-next")
    if report.get("product_path") != "forward-native":
        _fail("showcase report must use the forward-native product path")
    commit = _string(report.get("commit"), "report.commit")
    if not COMMIT_RE.fullmatch(commit):
        _fail("report.commit must be a lowercase 40-hex Git commit")
    if _boolean(report.get("source_dirty"), "report.source_dirty"):
        _fail("showcase evidence cannot come from a dirty source tree")
    _sha256(report.get("package_sha256"), "report.package_sha256")
    if report.get("asset_origin") not in ALLOWED_ORIGINS:
        _fail("showcase asset origin is not redistributable forward-native content")
    if not _boolean(report.get("provenance_approved"), "report.provenance_approved"):
        _fail("showcase asset provenance is not approved")

    hardware = _dict(report.get("hardware"), "report.hardware")
    api = _string(hardware.get("native_api"), "report.hardware.native_api")
    if api not in ALLOWED_NATIVE_APIS:
        _fail("native RT evidence uses an unsupported API")
    for field in (
        "native_rt_requested",
        "native_rt_admitted",
        "supports_ray_tracing",
        "same_renderer_device",
        "same_renderer_queue",
    ):
        if not _boolean(hardware.get(field), f"report.hardware.{field}"):
            _fail(f"native RT Ultra requires hardware.{field}=true")
    if hardware.get("fallback_reason") not in (None, ""):
        _fail("native RT Ultra evidence cannot contain a fallback reason")

    modes = _list(report.get("modes"), "report.modes")
    seen_modes: set[str] = set()
    for index, raw_mode in enumerate(modes):
        mode = _dict(raw_mode, f"report.modes[{index}]")
        mode_id = _validate_mode(mode, contract, passes)
        if mode_id in seen_modes:
            _fail(f"duplicate report mode {mode_id}")
        seen_modes.add(mode_id)
    missing_modes = sorted(set(contract["required_modes"]) - seen_modes)
    if missing_modes:
        _fail("missing showcase modes: " + ", ".join(missing_modes))
    _validate_artifacts(report, contract, required_artifacts)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--contract", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        verify(args.contract, args.report)
    except (OSError, VerificationError) as exc:
        print(f"native visual showcase verification failed: {exc}", file=sys.stderr)
        return 1
    print("native visual showcase verification passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
