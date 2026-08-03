#!/usr/bin/env python3
"""Build, run, and attest the Linux Ogre-Next external Vulkan RT5 proof."""

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
    "run_ogre_next_probe_for_vulkan_rt5", MAIN_RUNNER_PATH
)
if MAIN_RUNNER_SPEC is None or MAIN_RUNNER_SPEC.loader is None:
    raise RuntimeError("could not load the pinned Ogre-Next runner")
MAIN_RUNNER = importlib.util.module_from_spec(MAIN_RUNNER_SPEC)
MAIN_RUNNER_SPEC.loader.exec_module(MAIN_RUNNER)

REPORT_NAME = "ror-ogre-next-vulkan-rt5-report.json"
ATTESTATION_NAME = "ror-ogre-next-vulkan-rt5-attestation.json"
SCHEMA = "ror.ogre_next_vulkan_external_device_rt5.v1"
ATTESTATION_SCHEMA = (
    "ror.ogre_next_vulkan_external_device_rt5.attestation.v1"
)
UNSUPPORTED_EXIT_CODE = 77
REQUIRED_CONFIG = "Release"
UINT32_MAX = (1 << 32) - 1


class Rt5Error(RuntimeError):
    """Raised when the RT5 proof or evidence fails closed."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require_sha256(value: object, label: str) -> None:
    if not isinstance(value, str) or re.fullmatch(r"[0-9a-f]{64}", value) is None:
        raise Rt5Error(f"{label} is not a lowercase SHA-256")


def api_version_tuple(value: object) -> tuple[int, int, int] | None:
    if not isinstance(value, str):
        return None
    piece_pattern = r"(0|[1-9][0-9]*)"
    match = re.fullmatch(
        rf"{piece_pattern}\.{piece_pattern}\.{piece_pattern}", value
    )
    if match is None:
        return None
    major, minor, patch = (int(piece) for piece in match.groups())
    if major > 0x7F or minor > 0x3FF or patch > 0xFFF:
        return None
    return major, minor, patch


def is_uint32(value: object) -> bool:
    return type(value) is int and 0 <= value <= UINT32_MAX


def is_uint64(value: object) -> bool:
    return type(value) is int and 0 <= value <= (1 << 64) - 1


def unsupported_reason_is_consistent(
    reason: object,
    vulkan: dict[str, Any],
    enabled: dict[str, Any],
) -> bool:
    if not isinstance(reason, str) or not reason:
        return False
    loader_version = api_version_tuple(vulkan.get("loader_api_version"))
    physical_version = api_version_tuple(
        vulkan.get("physical_device_api_version")
    )
    decision = vulkan.get("candidate_decision")
    device_class = vulkan.get("device_class")
    known_software = vulkan.get("known_software_adapter")
    loader_ready = loader_version is not None and loader_version >= (1, 2, 0)
    no_candidate_identity = (
        physical_version == (0, 0, 0)
        and vulkan.get("driver_version_packed") == 0
        and vulkan.get("vendor_id") == 0
        and vulkan.get("device_id") == 0
        and vulkan.get("device_name") == ""
        and vulkan.get("device_uuid") == ""
        and device_class == "other"
        and known_software is False
        and enabled.get("graphics_queue_available") is False
        and enabled.get("timeline_semaphore_supported") is False
        and enabled.get("all_supported_core_features_enabled") is False
        and enabled.get("enabled_instance_extensions_exact") is False
        and enabled.get("enabled_device_extensions_exact") is False
    )

    if reason == "Vulkan loader does not expose Vulkan 1.2":
        return (
            loader_version is not None
            and loader_version < (1, 2, 0)
            and no_candidate_identity
            and decision == "software_or_unattested_device"
        )
    if reason in {
        "Vulkan reported no physical devices",
        "Vulkan physical-device enumeration became empty",
    }:
        return (
            loader_version is not None
            and loader_version >= (1, 2, 0)
            and no_candidate_identity
            and decision == "software_or_unattested_device"
        )

    prefix = "no attested RT5 hardware device: "
    if (
        not loader_ready
        or not reason.startswith(prefix)
        or reason[len(prefix) :] != decision
        or not isinstance(vulkan.get("device_name"), str)
        or not vulkan["device_name"]
        or not isinstance(vulkan.get("device_uuid"), str)
        or re.fullmatch(r"[0-9a-f]{32}", vulkan["device_uuid"]) is None
        or vulkan["device_uuid"] == "0" * 32
        or enabled.get("enabled_instance_extensions_exact") is not True
        or enabled.get("enabled_device_extensions_exact") is not False
    ):
        return False
    hardware_class = device_class in {"integrated_gpu", "discrete_gpu"}
    api_ready = physical_version is not None and physical_version >= (1, 2, 0)
    graphics_ready = enabled.get("graphics_queue_available") is True
    timeline_ready = enabled.get("timeline_semaphore_supported") is True
    preceding_ready = api_ready and hardware_class and known_software is False
    if decision == "api_too_old":
        return physical_version is not None and physical_version < (1, 2, 0)
    if decision == "software_or_unattested_device":
        return api_ready and (known_software is True or not hardware_class)
    if decision == "graphics_queue_unavailable":
        return preceding_ready and not graphics_ready
    if decision == "timeline_semaphore_unavailable":
        return preceding_ready and graphics_ready and not timeline_ready
    if decision == "enabled_feature_state_ambiguous":
        return (
            preceding_ready
            and graphics_ready
            and timeline_ready
            and enabled.get("all_supported_core_features_enabled") is False
        )
    if decision == "enabled_extension_state_ambiguous":
        return (
            preceding_ready
            and graphics_ready
            and timeline_ready
            and enabled.get("all_supported_core_features_enabled") is True
            and enabled.get("enabled_device_extensions_exact") is False
        )
    return False


def read_json(path: Path, label: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise Rt5Error(f"could not read {label}: {error}") from error
    if not isinstance(value, dict):
        raise Rt5Error(f"{label} must be a JSON object")
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
        try:
            temporary.unlink(missing_ok=True)
        except OSError:
            pass
        raise Rt5Error(f"could not publish RT5 attestation: {error}") from error


def executable_path(build_dir: Path) -> Path:
    candidates = (
        build_dir / "bin" / "ror_ogre_next_vulkan_rt5_smoke",
        build_dir / "bin" / REQUIRED_CONFIG / "ror_ogre_next_vulkan_rt5_smoke",
    )
    existing = [candidate for candidate in candidates if candidate.is_file()]
    if len(existing) != 1:
        raise Rt5Error(
            "expected exactly one RT5 executable, found "
            + str(len(existing))
        )
    return existing[0]


def validate_report(
    report: dict[str, Any],
    process_exit_code: int,
    lock: dict[str, Any],
    source_identity: dict[str, Any],
) -> None:
    status = report.get("status")
    expected_status = {0: "pass", UNSUPPORTED_EXIT_CODE: "unsupported"}.get(
        process_exit_code
    )
    provenance = report.get("provenance", {})
    build = report.get("build", {})
    scope = report.get("scope", {})
    vulkan = report.get("vulkan", {})
    enabled = report.get("enabled_state_contract", {})
    adoption = report.get("ogre_external_adoption", {})
    timeline = report.get("timeline", {})
    lifecycle = report.get("lifecycle", {})
    loader_version = api_version_tuple(vulkan.get("loader_api_version"))
    physical_version = api_version_tuple(
        vulkan.get("physical_device_api_version")
    )
    enabled_boolean_fields = (
        "graphics_queue_available",
        "timeline_semaphore_supported",
        "timeline_semaphore_enabled",
        "all_supported_core_features_enabled",
        "enabled_instance_extensions_exact",
        "enabled_device_extensions_exact",
    )
    adoption_boolean_fields = (
        "instance_injected_exactly",
        "physical_device_injected_exactly",
        "logical_device_injected_exactly",
        "graphics_queue_injected_exactly",
        "ogre_external_ownership_observed",
    )
    lifecycle_boolean_fields = (
        "ogre_shutdown_before_owner_teardown",
        "timeline_destroyed_before_device",
        "device_destroyed_before_instance",
        "shutdown_completed",
    )

    checks = {
        "schema": report.get("schema") == SCHEMA,
        "exit_code": expected_status is not None,
        "status": expected_status is not None and status == expected_status,
        "reason_type": isinstance(report.get("reason"), str),
        "ror_repository": provenance.get("ror_repository")
        == source_identity["repository"],
        "ror_ref": provenance.get("ror_ref") == source_identity["ref"],
        "ror_commit": provenance.get("ror_commit")
        == source_identity["commit"],
        "ror_manifest": provenance.get(
            "ror_relevant_source_manifest_sha256"
        )
        == source_identity["relevant_manifest_sha256"],
        "ror_manifest_count": provenance.get(
            "ror_relevant_source_manifest_file_count"
        )
        == source_identity["relevant_manifest_file_count"],
        "ogre_repository": provenance.get("ogre_next_repository")
        == lock["repository"],
        "ogre_branch": provenance.get("ogre_next_branch") == lock["branch"],
        "ogre_commit": provenance.get("ogre_next_commit") == lock["commit"],
        "ogre_archive": provenance.get("ogre_next_archive_sha256")
        == lock["archive_sha256"],
        "ogre_license": provenance.get("ogre_next_license_spdx")
        == lock["license"]["spdx"],
        "ogre_license_hash": provenance.get("ogre_next_license_sha256")
        == lock["license"]["sha256"],
        "platform": build.get("platform_policy") == "linux-x86_64-vulkan",
        "system": build.get("system") == "Linux",
        "processor": str(build.get("processor", "")).lower()
        in {"amd64", "x86_64"},
        "compiler": isinstance(build.get("compiler_id"), str)
        and bool(build["compiler_id"]),
        "ogre_version": build.get("ogre_version") == "3.0.0",
        "pointer_bits": build.get("pointer_bits") == 64,
        "foundation_scope": scope.get("external_instance_device_foundation")
        is True,
        "no_rt_claim": scope.get("native_ray_tracing") == "not_evaluated",
        "no_rt_image": scope.get("ray_traced_image_produced") is False,
        "no_acceleration_structure": scope.get("acceleration_structure_built")
        is False,
        "api_floor": vulkan.get("requested_instance_api_version") == "1.2.0",
        "loader_api_format": loader_version is not None,
        "physical_api_format": physical_version is not None,
        "driver_version": is_uint32(vulkan.get("driver_version_packed")),
        "vendor_id": is_uint32(vulkan.get("vendor_id")),
        "device_id": is_uint32(vulkan.get("device_id")),
        "device_name_type": isinstance(vulkan.get("device_name"), str),
        "device_class": vulkan.get("device_class")
        in {"other", "integrated_gpu", "discrete_gpu", "virtual_gpu", "cpu"},
        "software_flag": type(vulkan.get("known_software_adapter")) is bool,
        "candidate_decision": vulkan.get("candidate_decision")
        in {
            "accept",
            "api_too_old",
            "software_or_unattested_device",
            "graphics_queue_unavailable",
            "timeline_semaphore_unavailable",
            "enabled_feature_state_ambiguous",
            "enabled_extension_state_ambiguous",
        },
        "queue_family_type": is_uint32(vulkan.get("graphics_queue_family")),
        "queue_index_type": is_uint32(vulkan.get("graphics_queue_index")),
        "enabled_boolean_types": all(
            type(enabled.get(field)) is bool for field in enabled_boolean_fields
        ),
        "adoption_boolean_types": all(
            type(adoption.get(field)) is bool
            for field in adoption_boolean_fields
        ),
        "timeline_before_type": is_uint64(timeline.get("value_before_ogre")),
        "timeline_after_type": is_uint64(
            timeline.get("value_after_ogre_shutdown")
        ),
        "lifecycle_boolean_types": all(
            type(lifecycle.get(field)) is bool
            for field in lifecycle_boolean_fields
        ),
        "plugin_option": adoption.get("plugin_option") == "external_instance",
        "window_option": adoption.get("first_window_option")
        == "external_device",
        "null_window": adoption.get("window_type") == "null",
        "exact_instance_extension_count": enabled.get(
            "claimed_instance_extension_count"
        )
        == 0
        and type(enabled.get("claimed_instance_extension_count")) is int,
        "exact_device_extension_count": enabled.get(
            "claimed_device_extension_count"
        )
        == 0
        and type(enabled.get("claimed_device_extension_count")) is int,
        "shutdown": lifecycle.get("shutdown_completed") is True,
    }
    if status == "pass":
        checks.update(
            {
                "no_reason": report.get("reason") == "",
                "hardware_pass": scope.get("hardware_bootstrap_pass") is True,
                "loader_api_ready": loader_version is not None
                and loader_version >= (1, 2, 0),
                "physical_api_ready": physical_version is not None
                and physical_version >= (1, 2, 0),
                "attested_device": vulkan.get("device_class")
                in {"integrated_gpu", "discrete_gpu"},
                "device_name": isinstance(vulkan.get("device_name"), str)
                and bool(vulkan["device_name"]),
                "not_software": vulkan.get("known_software_adapter") is False,
                "candidate_accepted": vulkan.get("candidate_decision")
                == "accept",
                "device_uuid": isinstance(vulkan.get("device_uuid"), str)
                and re.fullmatch(r"[0-9a-f]{32}", vulkan["device_uuid"])
                is not None
                and vulkan["device_uuid"] != "0" * 32,
                "queue_family": is_uint32(
                    vulkan.get("graphics_queue_family")
                )
                and vulkan["graphics_queue_family"] < UINT32_MAX,
                "queue_index": vulkan.get("graphics_queue_index") == 0
                and type(vulkan.get("graphics_queue_index")) is int,
                "graphics_queue": enabled.get("graphics_queue_available")
                is True,
                "timeline_supported": enabled.get(
                    "timeline_semaphore_supported"
                )
                is True,
                "timeline_enabled": enabled.get("timeline_semaphore_enabled")
                is True,
                "core_features_exact": enabled.get(
                    "all_supported_core_features_enabled"
                )
                is True,
                "instance_extensions_exact": enabled.get(
                    "enabled_instance_extensions_exact"
                )
                is True,
                "device_extensions_exact": enabled.get(
                    "enabled_device_extensions_exact"
                )
                is True,
                "instance_identity": adoption.get("instance_injected_exactly")
                is True,
                "physical_identity": adoption.get(
                    "physical_device_injected_exactly"
                )
                is True,
                "device_identity": adoption.get(
                    "logical_device_injected_exactly"
                )
                is True,
                "queue_identity": adoption.get(
                    "graphics_queue_injected_exactly"
                )
                is True,
                "external_ownership": adoption.get(
                    "ogre_external_ownership_observed"
                )
                is True,
                "timeline_submit": timeline.get("queue_submit_and_host_wait")
                is True,
                "timeline_before": timeline.get("value_before_ogre") == 1,
                "timeline_after": timeline.get("value_after_ogre_shutdown")
                == 2,
                "ogre_first": lifecycle.get(
                    "ogre_shutdown_before_owner_teardown"
                )
                is True,
                "timeline_first": lifecycle.get(
                    "timeline_destroyed_before_device"
                )
                is True,
                "device_before_instance": lifecycle.get(
                    "device_destroyed_before_instance"
                )
                is True,
            }
        )
    else:
        checks.update(
            {
                "explicit_reason": isinstance(report.get("reason"), str)
                and bool(report["reason"]),
                "reason_consistent": unsupported_reason_is_consistent(
                    report.get("reason"), vulkan, enabled
                ),
                "no_hardware_pass": scope.get("hardware_bootstrap_pass")
                is False,
                "candidate_not_accepted": vulkan.get("candidate_decision")
                != "accept",
                "no_instance_identity_claim": adoption.get(
                    "instance_injected_exactly"
                )
                is False,
                "no_device_identity_claim": adoption.get(
                    "logical_device_injected_exactly"
                )
                is False,
                "no_queue_identity_claim": adoption.get(
                    "graphics_queue_injected_exactly"
                )
                is False,
                "no_physical_identity_claim": adoption.get(
                    "physical_device_injected_exactly"
                )
                is False,
                "no_external_ownership_claim": adoption.get(
                    "ogre_external_ownership_observed"
                )
                is False,
                "timeline_not_enabled": enabled.get(
                    "timeline_semaphore_enabled"
                )
                is False,
                "timeline_not_claimed": timeline.get("value_before_ogre") == 0
                and timeline.get("value_after_ogre_shutdown") == 0,
            }
        )
    failed = sorted(name for name, passed in checks.items() if not passed)
    if failed:
        raise Rt5Error("RT5 report failed closed: " + ", ".join(failed))


def make_attestation(
    report_path: Path,
    executable: Path,
    report: dict[str, Any],
    process_exit_code: int,
    source_identity: dict[str, Any],
) -> dict[str, Any]:
    return {
        "schema": ATTESTATION_SCHEMA,
        "status": report["status"],
        "process_exit_code": process_exit_code,
        "report": {
            "name": report_path.name,
            "sha256": sha256_file(report_path),
        },
        "executable": {
            "name": executable.name,
            "sha256": sha256_file(executable),
        },
        "ror_source": source_identity,
        "claims": {
            "external_instance_device_foundation": True,
            "hardware_bootstrap_pass": report["status"] == "pass",
            "native_ray_tracing": "not_evaluated",
            "software_adapter_rt_pass": False,
        },
        "complete": True,
    }


def validate_attestation(
    attestation: dict[str, Any],
    report_path: Path,
    executable: Path,
    report: dict[str, Any],
    source_identity: dict[str, Any],
) -> None:
    report_record = attestation.get("report", {})
    executable_record = attestation.get("executable", {})
    claims = attestation.get("claims", {})
    expected_exit = 0 if report.get("status") == "pass" else 77
    checks = {
        "schema": attestation.get("schema") == ATTESTATION_SCHEMA,
        "status": attestation.get("status") == report.get("status"),
        "exit": attestation.get("process_exit_code") == expected_exit,
        "report_name": report_record.get("name") == report_path.name,
        "report_hash": report_record.get("sha256") == sha256_file(report_path),
        "executable_name": executable_record.get("name") == executable.name,
        "executable_hash": executable_record.get("sha256")
        == sha256_file(executable),
        "ror_source": attestation.get("ror_source") == source_identity,
        "foundation": claims.get("external_instance_device_foundation") is True,
        "hardware": claims.get("hardware_bootstrap_pass")
        is (report.get("status") == "pass"),
        "native_rt": claims.get("native_ray_tracing") == "not_evaluated",
        "software_rt": claims.get("software_adapter_rt_pass") is False,
        "complete": attestation.get("complete") is True,
    }
    for value, label in (
        (report_record.get("sha256"), "attested report hash"),
        (executable_record.get("sha256"), "attested executable hash"),
    ):
        require_sha256(value, label)
    failed = sorted(name for name, passed in checks.items() if not passed)
    if failed:
        raise Rt5Error("RT5 attestation failed closed: " + ", ".join(failed))


def validate_static_contract() -> None:
    bootstrap = (
        REPOSITORY_ROOT
        / "source/main/gfx/render/ogrenext/OgreNextVulkanExternalDeviceBootstrap.cpp"
    ).read_text(encoding="utf-8")
    smoke = (PROBE_SOURCE / "src/vulkan_rt5_smoke.cpp").read_text(
        encoding="utf-8"
    )
    cmake = (PROBE_SOURCE / "CMakeLists.txt").read_text(encoding="utf-8")
    for token in (
        "VK_API_VERSION_1_2",
        "VkPhysicalDeviceVulkan12Features",
        "timelineSemaphore = VK_TRUE",
        "device_info.pEnabledFeatures = &selected.features2.features",
        "external_instance.instanceExtensions.clear()",
        "external_device.deviceExtensions.clear()",
        "mIsExternal",
        "getVkInstance() == impl_->instance",
        "mGraphicsQueue.mQueue == impl_->graphics_queue",
        "vkQueueSubmit",
        "vkWaitSemaphores",
        "vkDestroySemaphore",
        "vkDestroyDevice",
        "vkDestroyInstance",
    ):
        if token not in bootstrap:
            raise Rt5Error(f"RT5 bootstrap contract token is missing: {token}")
    for token in (
        'plugin_options["external_instance"]',
        'window_parameters["external_device"]',
        'window_parameters["windowType"] = "null"',
        '"native_ray_tracing',
        '"not_evaluated',
        "kUnsupportedExitCode = 77",
    ):
        if token not in smoke:
            raise Rt5Error(f"RT5 smoke contract token is missing: {token}")
    for token in (
        "ROR_OGRE_NEXT_VULKAN_RT5",
        "ror_ogre_next_vulkan_rt5_smoke",
        "SKIP_RETURN_CODE 77",
    ):
        if token not in cmake:
            raise Rt5Error(f"RT5 CMake contract token is missing: {token}")


def configure_build(build_dir: Path, generator: str | None) -> None:
    command = [
        "cmake",
        "-S",
        str(PROBE_SOURCE),
        "-B",
        str(build_dir),
        "-DROR_OGRE_NEXT_PROBE=ON",
        "-DROR_OGRE_NEXT_VULKAN_RT5=ON",
        f"-DCMAKE_BUILD_TYPE={REQUIRED_CONFIG}",
    ]
    if generator:
        command.extend(["-G", generator])
    elif shutil.which("ninja"):
        command.extend(["-G", "Ninja"])
    MAIN_RUNNER.run(command)


def run_proof(args: argparse.Namespace) -> dict[str, Any]:
    if platform.system().lower() != "linux" or platform.machine().lower() not in {
        "amd64",
        "x86_64",
    }:
        raise Rt5Error("native RT5 execution is reviewed only for Linux x86_64")
    lock = MAIN_RUNNER.load_lock()
    MAIN_RUNNER.load_linux_shader_toolchain_lock()
    MAIN_RUNNER.require_relevant_source_clean()
    source_identity = MAIN_RUNNER.ror_source_identity()
    build_dir = MAIN_RUNNER.prepare_build_dir(
        args.build_dir, args.clean_build_dir, args.reuse_build_dir
    )
    if not args.reuse_build_dir:
        configure_build(build_dir, args.generator)

    build_contract = read_json(
        build_dir / MAIN_RUNNER.BUILD_CONTRACT_NAME, "Ogre-Next build contract"
    )
    policy = MAIN_RUNNER.detect_policy(platform.system(), platform.machine())
    MAIN_RUNNER.validate_build_contract(
        build_contract, lock, policy, source_identity
    )
    MAIN_RUNNER.run(
        [
            "cmake",
            "--build",
            str(build_dir),
            "--target",
            "ror_ogre_next_vulkan_rt5_smoke",
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
    try:
        result = subprocess.run(
            [str(executable), "--report", str(report_path)], check=False
        )
    except OSError as error:
        raise Rt5Error(f"could not execute RT5 proof: {error}") from error
    if result.returncode not in (0, UNSUPPORTED_EXIT_CODE):
        raise Rt5Error(f"RT5 proof failed with exit code {result.returncode}")
    if not report_path.is_file():
        raise Rt5Error("RT5 proof did not publish its report")
    report = read_json(report_path, "RT5 report")
    validate_report(report, result.returncode, lock, source_identity)
    MAIN_RUNNER.require_source_identity_unchanged(source_identity)

    attestation = make_attestation(
        report_path, executable, report, result.returncode, source_identity
    )
    write_json_atomically(attestation_path, attestation)
    validate_attestation(
        attestation, report_path, executable, report, source_identity
    )
    return report


def verify_existing(build_dir: Path) -> dict[str, Any]:
    report_path = build_dir / REPORT_NAME
    attestation_path = build_dir / ATTESTATION_NAME
    executable = executable_path(build_dir)
    report = read_json(report_path, "RT5 report")
    attestation = read_json(attestation_path, "RT5 attestation")
    exit_code = 0 if report.get("status") == "pass" else UNSUPPORTED_EXIT_CODE
    lock = MAIN_RUNNER.load_lock()
    source_identity = MAIN_RUNNER.ror_source_identity()
    validate_report(report, exit_code, lock, source_identity)
    validate_attestation(
        attestation, report_path, executable, report, source_identity
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
            raise Rt5Error("--jobs must be positive")
        if args.validate_contract_only and args.verify_existing:
            raise Rt5Error(
                "--validate-contract-only and --verify-existing conflict"
            )
        lock = MAIN_RUNNER.load_lock()
        validate_static_contract()
        if args.validate_contract_only:
            print(
                json.dumps(
                    {
                        "schema": SCHEMA + ".contract",
                        "status": "pass",
                        "ogre_next_commit": lock["commit"],
                        "platform_policy": "linux-x86_64-vulkan",
                        "network_used": False,
                        "native_ray_tracing": "not_evaluated",
                    },
                    indent=2,
                    sort_keys=True,
                )
            )
            return 0
        if args.verify_existing:
            report = verify_existing(args.build_dir.resolve())
        else:
            if args.reuse_build_dir and args.generator:
                raise Rt5Error("reused builds cannot change the generator")
            report = run_proof(args)
        print(json.dumps(report, indent=2, sort_keys=True))
        return 0
    except (Rt5Error, MAIN_RUNNER.ProbeError) as error:
        print(f"Ogre-Next Vulkan RT5 failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
