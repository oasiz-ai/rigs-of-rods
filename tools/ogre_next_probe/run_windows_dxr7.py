#!/usr/bin/env python3
"""Build, run, and attest the Windows Ogre-Next D3D11On12/DXR RT7 proof."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
from typing import Any


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
PROBE_SOURCE = Path(__file__).resolve().parent
MAIN_RUNNER_PATH = REPOSITORY_ROOT / "tools" / "run_ogre_next_probe.py"
MAIN_RUNNER_SPEC = importlib.util.spec_from_file_location(
    "run_ogre_next_probe_for_windows_dxr7", MAIN_RUNNER_PATH
)
if MAIN_RUNNER_SPEC is None or MAIN_RUNNER_SPEC.loader is None:
    raise RuntimeError("could not load the pinned Ogre-Next runner")
MAIN_RUNNER = importlib.util.module_from_spec(MAIN_RUNNER_SPEC)
MAIN_RUNNER_SPEC.loader.exec_module(MAIN_RUNNER)

REPORT_NAME = "ror-ogre-next-windows-dxr7-report.json"
ATTESTATION_NAME = "ror-ogre-next-windows-dxr7-attestation.json"
DXIL_RELATIVE = "generated/ror_ogre_next_windows_dxr7_probe.dxil"
SCHEMA = "ror.ogre_next_windows_dxr_rt7.v1"
ATTESTATION_SCHEMA = "ror.ogre_next_windows_dxr_rt7.attestation.v1"
LOCK_NAME = "windows-dxr7.lock.json"
LOCK_SHA256 = (
    "b49f61500496213576fb2cfb74915463f743a3bde61a52991991da04baa87a0e"
)
UNSUPPORTED_EXIT_CODE = 77
REQUIRED_CONFIG = "Release"
UINT32_MAX = (1 << 32) - 1
UINT64_MAX = (1 << 64) - 1
SCOPE_LIMITATION = (
    "one hardware DXR primary-ray closest-hit readback plus exact D3D11On12 "
    "Ogre device adoption; no hybrid image, GI, reflection, denoising, "
    "multi-bounce, or material parity claim"
)


class Dxr7Error(RuntimeError):
    """Raised when DXR7 code or evidence fails closed."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def is_sha256(value: object) -> bool:
    return (
        isinstance(value, str)
        and re.fullmatch(r"[0-9a-f]{64}", value) is not None
    )


def is_uint32(value: object) -> bool:
    return type(value) is int and 0 <= value <= UINT32_MAX


def is_uint64(value: object) -> bool:
    return type(value) is int and 0 <= value <= UINT64_MAX


def require_exact_keys(
    value: object, expected: set[str], label: str
) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != expected:
        raise Dxr7Error(f"{label} keys changed")
    return value


def read_json(path: Path, label: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise Dxr7Error(f"could not read {label}: {error}") from error
    if not isinstance(value, dict):
        raise Dxr7Error(f"{label} root is not an object")
    return value


def write_json_atomically(path: Path, value: dict[str, Any]) -> None:
    temporary = path.with_name(path.name + ".tmp")
    try:
        temporary.write_text(
            json.dumps(value, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        os.replace(temporary, path)
    except OSError as error:
        temporary.unlink(missing_ok=True)
        raise Dxr7Error(f"could not publish DXR7 attestation: {error}") from error


def load_dxr7_lock() -> dict[str, Any]:
    path = PROBE_SOURCE / LOCK_NAME
    try:
        source = path.read_text(encoding="utf-8")
        lock = json.loads(source)
    except (OSError, json.JSONDecodeError) as error:
        raise Dxr7Error(f"could not read Windows DXR7 lock: {error}") from error
    if source != json.dumps(lock, indent=2) + "\n":
        raise Dxr7Error("Windows DXR7 lock is not canonical JSON")
    if sha256_file(path) != LOCK_SHA256:
        raise Dxr7Error("Windows DXR7 lock moved without review")
    require_exact_keys(
        lock,
        {
            "schema",
            "platform_policy",
            "ogre_next_commit",
            "adaptation_patch",
            "shader",
            "runtime",
        },
        "Windows DXR7 lock",
    )
    patch = require_exact_keys(
        lock["adaptation_patch"], {"path", "sha256", "scope"},
        "Windows DXR7 patch lock",
    )
    shader = require_exact_keys(
        lock["shader"],
        {
            "path",
            "sha256",
            "target",
            "entry_exports",
            "compiler_provider",
            "compiler_arguments",
        },
        "Windows DXR7 shader lock",
    )
    runtime = require_exact_keys(
        lock["runtime"],
        {
            "minimum_dxr_tier",
            "software_adapter_pass_allowed",
            "required_dispatch_dimensions",
            "closest_hit_readback",
        },
        "Windows DXR7 runtime lock",
    )
    expected = {
        "schema": lock.get("schema")
        == "ror.ogre_next_windows_dxr7_toolchain.v1",
        "platform": lock.get("platform_policy")
        == "windows-x64-d3d11on12-dxr",
        "ogre_commit": lock.get("ogre_next_commit")
        == "37149a802de747f6806996fa3067b0748ecc1084",
        "patch_path": patch.get("path")
        == "patches/0004-d3d11-adopt-external-device.patch",
        "patch_hash": patch.get("sha256")
        == "ae9198d78ed5b7ec43aaf6eb57666f591191fd97fa102f10792b726d835806d9",
        "patch_scope": patch.get("scope")
        == "D3D11 plugin external-device adoption only",
        "shader_path": shader.get("path")
        == "shaders/windows_dxr7_probe.hlsl",
        "shader_hash": shader.get("sha256")
        == "3246fcd8c91b0dffa149c25223455ca136a2e1813cd75af6d5dd49368d897df3",
        "shader_target": shader.get("target") == "lib_6_5",
        "exports": shader.get("entry_exports")
        == ["RayGen", "Miss", "ClosestHit"],
        "compiler": shader.get("compiler_provider")
        == "Windows SDK dxc.exe",
        "compiler_arguments": shader.get("compiler_arguments")
        == ["-HV", "2021", "-O3", "-Qstrip_debug", "-Qstrip_reflect"],
        "tier": runtime.get("minimum_dxr_tier") == "1.1",
        "no_software": runtime.get("software_adapter_pass_allowed") is False,
        "dispatch": runtime.get("required_dispatch_dimensions") == [1, 1, 1],
        "readback": runtime.get("closest_hit_readback") == 0xD1CEB00B,
    }
    failed = sorted(name for name, passed in expected.items() if not passed)
    if failed:
        raise Dxr7Error("Windows DXR7 lock failed closed: " + ", ".join(failed))
    for record, label in ((patch, "patch"), (shader, "shader")):
        if not is_sha256(record.get("sha256")):
            raise Dxr7Error(f"Windows DXR7 {label} hash is invalid")
        artifact = PROBE_SOURCE / str(record["path"])
        if not artifact.is_file() or artifact.is_symlink():
            raise Dxr7Error(f"Windows DXR7 {label} source is missing or indirect")
        if sha256_file(artifact) != record["sha256"]:
            raise Dxr7Error(f"Windows DXR7 {label} source hash mismatch")
    return lock


def executable_path(build_dir: Path) -> Path:
    names = (
        "bin/ror_ogre_next_windows_dxr7_smoke.exe",
        f"bin/{REQUIRED_CONFIG}/ror_ogre_next_windows_dxr7_smoke.exe",
    )
    existing = [build_dir / name for name in names if (build_dir / name).is_file()]
    if len(existing) != 1:
        raise Dxr7Error(
            f"expected exactly one DXR7 executable, found {len(existing)}"
        )
    return existing[0]


def unsupported_reason_consistent(
    reason: object, adapter: dict[str, Any], ownership: dict[str, Any]
) -> bool:
    decision = adapter.get("candidate_decision")
    if reason != f"no attested DXR7 adapter: {decision}":
        return False
    no_identity = (
        adapter.get("name") == ""
        and adapter.get("luid") == ""
        and adapter.get("vendor_id") == 0
        and adapter.get("device_id") == 0
        and adapter.get("d3d12_feature_level") == 0
        and adapter.get("d3d11_feature_level") == 0
        and adapter.get("raytracing_tier") == 0
        and adapter.get("software_adapter") is False
    )
    if decision == "no_hardware_adapter":
        return no_identity
    hardware_identity = (
        isinstance(adapter.get("name"), str)
        and bool(adapter["name"])
        and isinstance(adapter.get("luid"), str)
        and re.fullmatch(r"[0-9a-f]{16}", adapter["luid"]) is not None
        and is_uint32(adapter.get("vendor_id"))
        and adapter["vendor_id"] > 0
        and is_uint32(adapter.get("device_id"))
        and adapter.get("software_adapter") is False
    )
    if decision == "d3d12_unavailable":
        return (
            hardware_identity
            and adapter.get("d3d12_feature_level") == 0
            and adapter.get("raytracing_tier") == 0
        )
    if decision == "dxr_tier_below_1_1":
        return (
            hardware_identity
            and adapter.get("d3d12_feature_level") == 0xC000
            and is_uint32(adapter.get("raytracing_tier"))
            and adapter["raytracing_tier"] < 11
        )
    return False


def validate_report(
    report: dict[str, Any],
    process_exit_code: int,
    ogre_lock: dict[str, Any],
    dxr7_lock: dict[str, Any],
    source_identity: dict[str, Any],
) -> None:
    require_exact_keys(
        report,
        {
            "schema",
            "status",
            "reason",
            "scope",
            "provenance",
            "build",
            "adapter",
            "ownership",
            "ray_tracing",
            "synchronization",
            "lifecycle",
        },
        "DXR7 report",
    )
    scope = require_exact_keys(
        report["scope"],
        {
            "external_d3d11on12_foundation",
            "hardware_dxr_pass",
            "native_ray_tracing",
            "acceleration_structure_built",
            "ray_traced_probe_readback",
            "ray_traced_image_produced",
            "hybrid_ogre_image_composite",
            "limitation",
        },
        "DXR7 scope",
    )
    provenance = require_exact_keys(
        report["provenance"],
        {
            "ror_repository",
            "ror_ref",
            "ror_commit",
            "ror_relevant_source_manifest_sha256",
            "ror_relevant_source_manifest_file_count",
            "ogre_next_repository",
            "ogre_next_branch",
            "ogre_next_commit",
            "ogre_next_archive_sha256",
            "ogre_next_license_spdx",
            "ogre_next_license_sha256",
            "dxr7_toolchain_lock_sha256",
            "ogre_adaptation_patch_path",
            "ogre_adaptation_patch_sha256",
            "hlsl_source_sha256",
            "dxc_executable_sha256",
        },
        "DXR7 provenance",
    )
    build = require_exact_keys(
        report["build"],
        {
            "platform_policy",
            "system",
            "processor",
            "compiler_id",
            "compiler_version",
            "ogre_version",
            "pointer_bits",
        },
        "DXR7 build",
    )
    adapter = require_exact_keys(
        report["adapter"],
        {
            "name",
            "luid",
            "vendor_id",
            "device_id",
            "software_adapter",
            "d3d12_feature_level",
            "d3d11_feature_level",
            "raytracing_tier",
            "candidate_decision",
        },
        "DXR7 adapter",
    )
    ownership = require_exact_keys(
        report["ownership"],
        {
            "app_owned_d3d12_device",
            "app_owned_direct_queue",
            "app_owned_fence",
            "d3d11on12_device_created",
            "d3d11on12_created_with_exact_direct_queue",
            "d3d11on12_underlying_d3d12_device_exact",
            "d3d11on12_adapter_luid_exact",
            "ogre_plugin_option",
            "ogre_external_device_option_used",
            "ogre_d3d11_device_exact",
            "ogre_external_device_active",
        },
        "DXR7 ownership",
    )
    ray_tracing = require_exact_keys(
        report["ray_tracing"],
        {
            "blas_built",
            "tlas_built",
            "state_object_created",
            "shader_identifiers_resolved",
            "dispatch_rays_called",
            "dispatch_width",
            "dispatch_height",
            "dispatch_depth",
            "readback_value",
            "closest_hit_readback_exact",
        },
        "DXR7 ray tracing",
    )
    synchronization = require_exact_keys(
        report["synchronization"],
        {"fence_before_dispatch", "fence_after_dispatch", "fence_after_ogre"},
        "DXR7 synchronization",
    )
    lifecycle = require_exact_keys(
        report["lifecycle"],
        {
            "ogre_shutdown_before_d3d11_release",
            "d3d11_context_flushed_before_release",
            "d3d11_released_before_d3d12_queue",
            "d3d12_queue_released_before_device",
            "shutdown_completed",
        },
        "DXR7 lifecycle",
    )
    status = report.get("status")
    expected_status = {0: "pass", UNSUPPORTED_EXIT_CODE: "unsupported"}.get(
        process_exit_code
    )
    boolean_fields = (
        [
            scope[field]
            for field in (
                "external_d3d11on12_foundation",
                "hardware_dxr_pass",
                "acceleration_structure_built",
                "ray_traced_probe_readback",
                "ray_traced_image_produced",
                "hybrid_ogre_image_composite",
            )
        ]
        + [value for key, value in ownership.items() if key != "ogre_plugin_option"]
        + [
            value
            for key, value in ray_tracing.items()
            if key
            not in {
                "dispatch_width",
                "dispatch_height",
                "dispatch_depth",
                "readback_value",
            }
        ]
        + list(lifecycle.values())
    )
    patch_lock = dxr7_lock["adaptation_patch"]
    shader_lock = dxr7_lock["shader"]
    checks = {
        "schema": report.get("schema") == SCHEMA,
        "status": status == expected_status,
        "status_domain": status in {"pass", "unsupported"},
        "boolean_types": all(type(value) is bool for value in boolean_fields),
        "scope_foundation": scope.get("external_d3d11on12_foundation") is True,
        "scope_no_image": scope.get("ray_traced_image_produced") is False,
        "scope_no_hybrid": scope.get("hybrid_ogre_image_composite") is False,
        "scope_limitation": scope.get("limitation") == SCOPE_LIMITATION,
        "ror_repository": provenance.get("ror_repository")
        == source_identity["repository"],
        "ror_ref": provenance.get("ror_ref") == source_identity["ref"],
        "ror_commit": provenance.get("ror_commit")
        == source_identity["commit"],
        "ror_manifest": provenance.get("ror_relevant_source_manifest_sha256")
        == source_identity["relevant_manifest_sha256"],
        "ror_manifest_count": provenance.get(
            "ror_relevant_source_manifest_file_count"
        )
        == source_identity["relevant_manifest_file_count"],
        "ogre_repository": provenance.get("ogre_next_repository")
        == ogre_lock["repository"],
        "ogre_branch": provenance.get("ogre_next_branch")
        == ogre_lock["branch"],
        "ogre_commit": provenance.get("ogre_next_commit")
        == ogre_lock["commit"],
        "ogre_archive": provenance.get("ogre_next_archive_sha256")
        == ogre_lock["archive_sha256"],
        "ogre_license": provenance.get("ogre_next_license_spdx")
        == ogre_lock["license"]["spdx"],
        "ogre_license_hash": provenance.get("ogre_next_license_sha256")
        == ogre_lock["license"]["sha256"],
        "dxr7_lock": provenance.get("dxr7_toolchain_lock_sha256")
        == LOCK_SHA256,
        "patch_path": provenance.get("ogre_adaptation_patch_path")
        == patch_lock["path"],
        "patch_hash": provenance.get("ogre_adaptation_patch_sha256")
        == patch_lock["sha256"],
        "hlsl_hash": provenance.get("hlsl_source_sha256")
        == shader_lock["sha256"],
        "dxc_hash": is_sha256(provenance.get("dxc_executable_sha256")),
        "platform": build.get("platform_policy")
        == "windows-x64-d3d11on12-dxr",
        "system": build.get("system") == "Windows",
        "processor": str(build.get("processor", "")).lower()
        in {"amd64", "x86_64"},
        "compiler": build.get("compiler_id") == "MSVC"
        and isinstance(build.get("compiler_version"), str)
        and bool(build["compiler_version"]),
        "ogre_version": build.get("ogre_version") == "3.0.0",
        "pointer_bits": build.get("pointer_bits") == 64,
        "adapter_types": isinstance(adapter.get("name"), str)
        and isinstance(adapter.get("luid"), str)
        and is_uint32(adapter.get("vendor_id"))
        and is_uint32(adapter.get("device_id"))
        and type(adapter.get("software_adapter")) is bool
        and is_uint32(adapter.get("d3d12_feature_level"))
        and is_uint32(adapter.get("d3d11_feature_level"))
        and is_uint32(adapter.get("raytracing_tier")),
        "decision_domain": adapter.get("candidate_decision")
        in {
            "accept",
            "no_hardware_adapter",
            "d3d12_unavailable",
            "dxr_tier_below_1_1",
        },
        "plugin_option": ownership.get("ogre_plugin_option")
        == "external_device",
        "dispatch_integer_types": all(
            is_uint32(ray_tracing.get(field))
            for field in (
                "dispatch_width",
                "dispatch_height",
                "dispatch_depth",
                "readback_value",
            )
        ),
        "fence_integer_types": all(
            is_uint64(synchronization.get(field))
            for field in (
                "fence_before_dispatch",
                "fence_after_dispatch",
                "fence_after_ogre",
            )
        ),
        "shutdown": lifecycle.get("shutdown_completed") is True,
    }
    if status == "pass":
        checks.update(
            {
                "no_reason": report.get("reason") == "",
                "scope_pass": scope.get("hardware_dxr_pass") is True
                and scope.get("native_ray_tracing") == "dispatch_rays"
                and scope.get("acceleration_structure_built") is True
                and scope.get("ray_traced_probe_readback") is True,
                "hardware_identity": bool(adapter.get("name"))
                and re.fullmatch(r"[0-9a-f]{16}", adapter.get("luid", ""))
                is not None
                and adapter.get("vendor_id", 0) > 0
                and adapter.get("software_adapter") is False,
                "feature_levels": adapter.get("d3d12_feature_level") == 0xC000
                and adapter.get("d3d11_feature_level") in {0xB000, 0xB100},
                "tier": adapter.get("raytracing_tier") == 11,
                "accepted": adapter.get("candidate_decision") == "accept",
                "ownership": all(
                    ownership.get(field) is True
                    for field in ownership
                    if field != "ogre_plugin_option"
                ),
                "real_dispatch": all(
                    ray_tracing.get(field) is True
                    for field in (
                        "blas_built",
                        "tlas_built",
                        "state_object_created",
                        "shader_identifiers_resolved",
                        "dispatch_rays_called",
                        "closest_hit_readback_exact",
                    )
                ),
                "dispatch_dimensions": [
                    ray_tracing.get("dispatch_width"),
                    ray_tracing.get("dispatch_height"),
                    ray_tracing.get("dispatch_depth"),
                ]
                == dxr7_lock["runtime"]["required_dispatch_dimensions"],
                "readback": ray_tracing.get("readback_value")
                == dxr7_lock["runtime"]["closest_hit_readback"],
                "fence_sequence": synchronization
                == {
                    "fence_before_dispatch": 1,
                    "fence_after_dispatch": 2,
                    "fence_after_ogre": 3,
                },
                "lifecycle": all(value is True for value in lifecycle.values()),
            }
        )
    else:
        checks.update(
            {
                "explicit_reason": isinstance(report.get("reason"), str)
                and bool(report["reason"]),
                "reason_consistent": unsupported_reason_consistent(
                    report.get("reason"), adapter, ownership
                ),
                "scope_unsupported": scope.get("hardware_dxr_pass") is False
                and scope.get("native_ray_tracing") == "unsupported"
                and scope.get("acceleration_structure_built") is False
                and scope.get("ray_traced_probe_readback") is False,
                "no_ownership_claim": all(
                    ownership.get(field) is False
                    for field in ownership
                    if field != "ogre_plugin_option"
                ),
                "no_dispatch_claim": all(
                    ray_tracing.get(field) is False
                    for field in (
                        "blas_built",
                        "tlas_built",
                        "state_object_created",
                        "shader_identifiers_resolved",
                        "dispatch_rays_called",
                        "closest_hit_readback_exact",
                    )
                )
                and all(
                    ray_tracing.get(field) == 0
                    for field in (
                        "dispatch_width",
                        "dispatch_height",
                        "dispatch_depth",
                        "readback_value",
                    )
                ),
                "no_fence_claim": all(value == 0 for value in synchronization.values()),
                "only_shutdown_claim": lifecycle
                == {
                    "ogre_shutdown_before_d3d11_release": False,
                    "d3d11_context_flushed_before_release": False,
                    "d3d11_released_before_d3d12_queue": False,
                    "d3d12_queue_released_before_device": False,
                    "shutdown_completed": True,
                },
            }
        )
    failed = sorted(name for name, passed in checks.items() if not passed)
    if failed:
        raise Dxr7Error("DXR7 report failed closed: " + ", ".join(failed))


def make_attestation(
    report_path: Path,
    executable: Path,
    dxil: Path,
    report: dict[str, Any],
    process_exit_code: int,
    source_identity: dict[str, Any],
) -> dict[str, Any]:
    passed = report.get("status") == "pass"
    return {
        "schema": ATTESTATION_SCHEMA,
        "status": report["status"],
        "process_exit_code": process_exit_code,
        "report": {
            "name": report_path.name,
            "bytes": report_path.stat().st_size,
            "sha256": sha256_file(report_path),
        },
        "executable": {
            "name": executable.name,
            "bytes": executable.stat().st_size,
            "sha256": sha256_file(executable),
        },
        "dxil": {
            "path": DXIL_RELATIVE,
            "bytes": dxil.stat().st_size,
            "sha256": sha256_file(dxil),
        },
        "ror_source": source_identity,
        "claims": {
            "external_d3d11on12_foundation": True,
            "hardware_dxr_pass": passed,
            "real_dispatch_rays": passed,
            "software_adapter_rt_pass": False,
            "hybrid_ogre_image_composite": False,
        },
        "complete": True,
    }


def validate_attestation(
    attestation: dict[str, Any],
    report_path: Path,
    executable: Path,
    dxil: Path,
    report: dict[str, Any],
    source_identity: dict[str, Any],
) -> None:
    require_exact_keys(
        attestation,
        {
            "schema",
            "status",
            "process_exit_code",
            "report",
            "executable",
            "dxil",
            "ror_source",
            "claims",
            "complete",
        },
        "DXR7 attestation",
    )
    report_record = require_exact_keys(
        attestation["report"], {"name", "bytes", "sha256"},
        "DXR7 attested report",
    )
    executable_record = require_exact_keys(
        attestation["executable"], {"name", "bytes", "sha256"},
        "DXR7 attested executable",
    )
    dxil_record = require_exact_keys(
        attestation["dxil"], {"path", "bytes", "sha256"},
        "DXR7 attested DXIL",
    )
    claims = require_exact_keys(
        attestation["claims"],
        {
            "external_d3d11on12_foundation",
            "hardware_dxr_pass",
            "real_dispatch_rays",
            "software_adapter_rt_pass",
            "hybrid_ogre_image_composite",
        },
        "DXR7 attested claims",
    )
    passed = report.get("status") == "pass"
    expected_exit = 0 if passed else UNSUPPORTED_EXIT_CODE
    checks = {
        "schema": attestation.get("schema") == ATTESTATION_SCHEMA,
        "status": attestation.get("status") == report.get("status"),
        "exit": attestation.get("process_exit_code") == expected_exit,
        "report": report_record
        == {
            "name": report_path.name,
            "bytes": report_path.stat().st_size,
            "sha256": sha256_file(report_path),
        },
        "executable": executable_record
        == {
            "name": executable.name,
            "bytes": executable.stat().st_size,
            "sha256": sha256_file(executable),
        },
        "dxil": dxil_record
        == {
            "path": DXIL_RELATIVE,
            "bytes": dxil.stat().st_size,
            "sha256": sha256_file(dxil),
        },
        "ror_source": attestation.get("ror_source") == source_identity,
        "claims": claims
        == {
            "external_d3d11on12_foundation": True,
            "hardware_dxr_pass": passed,
            "real_dispatch_rays": passed,
            "software_adapter_rt_pass": False,
            "hybrid_ogre_image_composite": False,
        },
        "complete": attestation.get("complete") is True,
    }
    failed = sorted(name for name, passed_check in checks.items() if not passed_check)
    if failed:
        raise Dxr7Error("DXR7 attestation failed closed: " + ", ".join(failed))


def validate_static_contract() -> None:
    bootstrap = (
        REPOSITORY_ROOT
        / "source/main/gfx/render/ogrenext/OgreNextD3D12DxrBootstrap.cpp"
    ).read_text(encoding="utf-8")
    patch = (
        PROBE_SOURCE / "patches/0004-d3d11-adopt-external-device.patch"
    ).read_text(encoding="utf-8")
    smoke = (PROBE_SOURCE / "src/windows_dxr7_smoke.cpp").read_text(
        encoding="utf-8"
    )
    cmake = (PROBE_SOURCE / "CMakeLists.txt").read_text(encoding="utf-8")
    for token in (
        "EnumAdapterByGpuPreference",
        "D3D12_RAYTRACING_TIER_1_1",
        "D3D11On12CreateDevice",
        "GetD3D12Device",
        "BuildRaytracingAccelerationStructure",
        "CreateStateObject",
        "GetShaderIdentifier",
        "DispatchRays",
        "closest_hit_readback_exact",
        "WaitForFence(1U)",
        "WaitForFence(2U)",
        "WaitForFence(3U)",
    ):
        if token not in bootstrap:
            raise Dxr7Error(f"DXR7 bootstrap contract token is missing: {token}")
    for token in (
        'options->find( "external_device" )',
        "mExternalDevice",
        "adoptExternalDevice",
        "D3D11_EXTERNAL_DEVICE_ACTIVE",
        "owner recreation is required",
    ):
        if token not in patch:
            raise Dxr7Error(f"DXR7 Ogre patch token is missing: {token}")
    for token in (
        'plugin_options["external_device"]',
        "ValidateDxr7PassContract",
        "kUnsupportedExitCode = 77",
        'MakeReport("pass"',
    ):
        if token not in smoke:
            raise Dxr7Error(f"DXR7 smoke contract token is missing: {token}")
    for token in (
        "ROR_OGRE_NEXT_WINDOWS_DXR7",
        "ror_ogre_next_windows_dxr7_shader",
        "ror_ogre_next_windows_dxr7_smoke",
        "SKIP_RETURN_CODE 77",
    ):
        if token not in cmake:
            raise Dxr7Error(f"DXR7 CMake contract token is missing: {token}")


def configure_build(
    build_dir: Path, generator: str | None, dxc: Path | None
) -> None:
    command = [
        "cmake",
        "-S",
        str(PROBE_SOURCE),
        "-B",
        str(build_dir),
        "-DROR_OGRE_NEXT_PROBE=ON",
        "-DROR_OGRE_NEXT_WINDOWS_DXR7=ON",
        f"-DCMAKE_BUILD_TYPE={REQUIRED_CONFIG}",
    ]
    if dxc is not None:
        resolved_dxc = dxc.expanduser().resolve()
        if not resolved_dxc.is_file() or resolved_dxc.is_symlink():
            raise Dxr7Error("--dxc must name a direct dxc.exe file")
        command.append(f"-DROR_OGRE_NEXT_DXC_EXECUTABLE={resolved_dxc}")
    if generator:
        command.extend(["-G", generator])
    elif shutil.which("ninja"):
        command.extend(["-G", "Ninja"])
    MAIN_RUNNER.run(command)


def run_proof(args: argparse.Namespace) -> dict[str, Any]:
    if platform.system().lower() != "windows" or platform.machine().lower() not in {
        "amd64",
        "x86_64",
    }:
        raise Dxr7Error("native DXR7 execution is reviewed only for Windows x64")
    ogre_lock = MAIN_RUNNER.load_lock()
    dxr7_lock = load_dxr7_lock()
    MAIN_RUNNER.require_relevant_source_clean()
    source_identity = MAIN_RUNNER.ror_source_identity()
    build_dir = MAIN_RUNNER.prepare_build_dir(
        args.build_dir, args.clean_build_dir, args.reuse_build_dir
    )
    if not args.reuse_build_dir:
        configure_build(build_dir, args.generator, args.dxc)

    build_contract = read_json(
        build_dir / MAIN_RUNNER.BUILD_CONTRACT_NAME,
        "Ogre-Next build contract",
    )
    policy = MAIN_RUNNER.detect_policy(platform.system(), platform.machine())
    MAIN_RUNNER.validate_build_contract(
        build_contract, ogre_lock, policy, source_identity
    )
    MAIN_RUNNER.run(
        [
            "cmake",
            "--build",
            str(build_dir),
            "--target",
            "ror_ogre_next_windows_dxr7_smoke",
            "--config",
            REQUIRED_CONFIG,
            "--parallel",
            str(args.jobs),
        ]
    )
    MAIN_RUNNER.require_source_identity_unchanged(source_identity)

    report_path = build_dir / REPORT_NAME
    attestation_path = build_dir / ATTESTATION_NAME
    report_path.unlink(missing_ok=True)
    attestation_path.unlink(missing_ok=True)
    executable = executable_path(build_dir)
    dxil = build_dir / DXIL_RELATIVE
    if not dxil.is_file() or dxil.is_symlink() or dxil.stat().st_size == 0:
        raise Dxr7Error("DXR7 build did not produce the exact DXIL library")
    try:
        result = subprocess.run(
            [
                str(executable),
                "--report",
                str(report_path),
                "--shader",
                str(dxil),
            ],
            check=False,
        )
    except OSError as error:
        raise Dxr7Error(f"could not execute DXR7 proof: {error}") from error
    if result.returncode not in (0, UNSUPPORTED_EXIT_CODE):
        raise Dxr7Error(f"DXR7 proof failed with exit code {result.returncode}")
    if not report_path.is_file():
        raise Dxr7Error("DXR7 proof did not publish its report")
    report = read_json(report_path, "DXR7 report")
    validate_report(
        report, result.returncode, ogre_lock, dxr7_lock, source_identity
    )
    MAIN_RUNNER.require_source_identity_unchanged(source_identity)
    attestation = make_attestation(
        report_path,
        executable,
        dxil,
        report,
        result.returncode,
        source_identity,
    )
    write_json_atomically(attestation_path, attestation)
    validate_attestation(
        attestation,
        report_path,
        executable,
        dxil,
        report,
        source_identity,
    )
    return report


def verify_existing(build_dir: Path) -> dict[str, Any]:
    report_path = build_dir / REPORT_NAME
    attestation_path = build_dir / ATTESTATION_NAME
    executable = executable_path(build_dir)
    dxil = build_dir / DXIL_RELATIVE
    report = read_json(report_path, "DXR7 report")
    attestation = read_json(attestation_path, "DXR7 attestation")
    exit_code = 0 if report.get("status") == "pass" else UNSUPPORTED_EXIT_CODE
    source_identity = MAIN_RUNNER.ror_source_identity()
    validate_report(
        report,
        exit_code,
        MAIN_RUNNER.load_lock(),
        load_dxr7_lock(),
        source_identity,
    )
    validate_attestation(
        attestation,
        report_path,
        executable,
        dxil,
        report,
        source_identity,
    )
    return report


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=MAIN_RUNNER.default_build_dir(),
        help="owned standalone Ogre-Next build directory",
    )
    parser.add_argument("--clean-build-dir", action="store_true")
    parser.add_argument("--reuse-build-dir", action="store_true")
    parser.add_argument("--generator")
    parser.add_argument("--dxc", type=Path)
    parser.add_argument(
        "--jobs", type=int, default=max(1, min(os.cpu_count() or 1, 8))
    )
    parser.add_argument("--validate-contract-only", action="store_true")
    parser.add_argument("--verify-existing", action="store_true")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.jobs <= 0:
            raise Dxr7Error("--jobs must be positive")
        if args.validate_contract_only and args.verify_existing:
            raise Dxr7Error(
                "--validate-contract-only and --verify-existing conflict"
            )
        ogre_lock = MAIN_RUNNER.load_lock()
        dxr7_lock = load_dxr7_lock()
        validate_static_contract()
        if args.validate_contract_only:
            print(
                json.dumps(
                    {
                        "schema": SCHEMA + ".contract",
                        "status": "pass",
                        "ogre_next_commit": ogre_lock["commit"],
                        "dxr7_lock_sha256": LOCK_SHA256,
                        "platform_policy": dxr7_lock["platform_policy"],
                        "network_used": False,
                        "hardware_execution": "not_evaluated",
                    },
                    indent=2,
                    sort_keys=True,
                )
            )
            return 0
        if args.verify_existing:
            report = verify_existing(args.build_dir.resolve())
        else:
            if args.reuse_build_dir and (args.generator or args.dxc):
                raise Dxr7Error(
                    "reused builds cannot change the generator or dxc.exe"
                )
            report = run_proof(args)
        print(json.dumps(report, indent=2, sort_keys=True))
        return 0
    except (Dxr7Error, MAIN_RUNNER.ProbeError) as error:
        print(f"Ogre-Next Windows DXR7 failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
