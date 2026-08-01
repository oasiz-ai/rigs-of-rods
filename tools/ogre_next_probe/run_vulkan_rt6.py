#!/usr/bin/env python3
"""Build, run, and attest the Linux Ogre-Next Vulkan KHR RT6 proof."""

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
    "run_ogre_next_probe_for_vulkan_rt6", MAIN_RUNNER_PATH
)
if MAIN_RUNNER_SPEC is None or MAIN_RUNNER_SPEC.loader is None:
    raise RuntimeError("could not load the pinned Ogre-Next runner")
MAIN_RUNNER = importlib.util.module_from_spec(MAIN_RUNNER_SPEC)
MAIN_RUNNER_SPEC.loader.exec_module(MAIN_RUNNER)

REPORT_NAME = "ror-ogre-next-vulkan-rt6-report.json"
ATTESTATION_NAME = "ror-ogre-next-vulkan-rt6-attestation.json"
SCHEMA = "ror.ogre_next_vulkan_khr_ray_dispatch_rt6.v1"
ATTESTATION_SCHEMA = "ror.ogre_next_vulkan_khr_ray_dispatch_rt6.attestation.v1"
UNSUPPORTED_EXIT_CODE = 77
REQUIRED_CONFIG = "Release"
UINT32_MAX = (1 << 32) - 1
UINT64_MAX = (1 << 64) - 1
EXPECTED_HIT_WORDS = [1381250561, 324508639, 610839776, 1]
DEVICE_CLASSES = {"other", "integrated_gpu", "discrete_gpu", "virtual_gpu", "cpu"}
CANDIDATE_DECISIONS = {
    "accept",
    "api_too_old",
    "software_or_unattested_device",
    "device_identity_unavailable",
    "graphics_queue_unavailable",
    "compute_queue_unavailable",
    "timeline_semaphore_unavailable",
    "deferred_host_operations_extension_unavailable",
    "buffer_device_address_extension_unavailable",
    "acceleration_structure_extension_unavailable",
    "ray_tracing_pipeline_extension_unavailable",
    "buffer_device_address_feature_unavailable",
    "acceleration_structure_feature_unavailable",
    "ray_tracing_pipeline_feature_unavailable",
    "output_storage_image_format_unavailable",
    "ray_tracing_properties_invalid",
    "enabled_feature_state_ambiguous",
    "enabled_extension_state_ambiguous",
}


class Rt6Error(RuntimeError):
    """Raised when the RT6 proof or its evidence fails closed."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require_sha256(value: object, label: str) -> None:
    if not isinstance(value, str) or re.fullmatch(r"[0-9a-f]{64}", value) is None:
        raise Rt6Error(f"{label} is not a lowercase SHA-256")


def api_version_tuple(value: object) -> tuple[int, int, int] | None:
    if not isinstance(value, str):
        return None
    piece = r"(0|[1-9][0-9]*)"
    match = re.fullmatch(rf"{piece}\.{piece}\.{piece}", value)
    if match is None:
        return None
    major, minor, patch = (int(item) for item in match.groups())
    if major > 0x7F or minor > 0x3FF or patch > 0xFFF:
        return None
    return major, minor, patch


def is_uint32(value: object) -> bool:
    return type(value) is int and 0 <= value <= UINT32_MAX


def is_uint64(value: object) -> bool:
    return type(value) is int and 0 <= value <= UINT64_MAX


def is_positive_uint64(value: object) -> bool:
    return is_uint64(value) and value > 0


def section(report: dict[str, Any], name: str) -> dict[str, Any]:
    value = report.get(name)
    return value if isinstance(value, dict) else {}


def no_candidate_identity(vulkan: dict[str, Any]) -> bool:
    return (
        api_version_tuple(vulkan.get("physical_device_api_version")) == (0, 0, 0)
        and vulkan.get("driver_version_packed") == 0
        and vulkan.get("vendor_id") == 0
        and vulkan.get("device_id") == 0
        and vulkan.get("device_name") == ""
        and vulkan.get("device_uuid") == ""
        and vulkan.get("device_class") == "other"
        and vulkan.get("known_software_adapter") is False
        and vulkan.get("device_identity_available") is False
        and vulkan.get("candidate_decision") == "software_or_unattested_device"
    )


def unsupported_reason_is_consistent(
    reason: object,
    vulkan: dict[str, Any],
    extensions: dict[str, Any],
    features: dict[str, Any],
) -> bool:
    if not isinstance(reason, str) or not reason:
        return False
    loader = api_version_tuple(vulkan.get("loader_api_version"))
    physical = api_version_tuple(vulkan.get("physical_device_api_version"))
    if reason == "Vulkan loader does not expose Vulkan 1.2":
        return (
            loader is not None
            and loader < (1, 2, 0)
            and no_candidate_identity(vulkan)
            and features.get("enabled_instance_extensions_exact") is False
        )
    if reason in {
        "Vulkan reported no physical devices",
        "Vulkan physical-device enumeration became empty",
    }:
        return (
            loader is not None
            and loader >= (1, 2, 0)
            and no_candidate_identity(vulkan)
            and features.get("enabled_instance_extensions_exact") is True
        )

    prefix = "no attested RT6 hardware device: "
    decision = vulkan.get("candidate_decision")
    if (
        loader is None
        or loader < (1, 2, 0)
        or not reason.startswith(prefix)
        or reason[len(prefix) :] != decision
        or decision not in CANDIDATE_DECISIONS - {"accept"}
        or not isinstance(vulkan.get("device_name"), str)
        or not isinstance(vulkan.get("device_uuid"), str)
        or re.fullmatch(r"[0-9a-f]{32}", vulkan["device_uuid"]) is None
        or type(vulkan.get("device_identity_available")) is not bool
        or features.get("enabled_instance_extensions_exact") is not True
        or extensions.get("enabled_count") != 0
        or extensions.get("enabled_set_exact") is not False
    ):
        return False

    api_ready = physical is not None and physical >= (1, 2, 0)
    hardware_class = vulkan.get("device_class") in {"integrated_gpu", "discrete_gpu"}
    not_software = vulkan.get("known_software_adapter") is False
    identity_available = (
        vulkan.get("device_identity_available") is True
        and bool(vulkan.get("device_name"))
        and vulkan.get("device_uuid") != "0" * 32
    )
    ordered_requirements: list[tuple[str, bool]] = [
        ("api_too_old", api_ready),
        ("software_or_unattested_device", hardware_class and not_software),
        ("device_identity_unavailable", identity_available),
        ("graphics_queue_unavailable", features.get("graphics_queue") is True),
        (
            "compute_queue_unavailable",
            features.get("compute_on_graphics_queue") is True,
        ),
        (
            "timeline_semaphore_unavailable",
            features.get("timeline_semaphore_supported") is True,
        ),
        (
            "deferred_host_operations_extension_unavailable",
            extensions.get("deferred_host_operations") is True,
        ),
        (
            "buffer_device_address_extension_unavailable",
            extensions.get("buffer_device_address") is True,
        ),
        (
            "acceleration_structure_extension_unavailable",
            extensions.get("acceleration_structure") is True,
        ),
        (
            "ray_tracing_pipeline_extension_unavailable",
            extensions.get("ray_tracing_pipeline") is True,
        ),
        (
            "buffer_device_address_feature_unavailable",
            features.get("buffer_device_address_supported") is True,
        ),
        (
            "acceleration_structure_feature_unavailable",
            features.get("acceleration_structure_supported") is True,
        ),
        (
            "ray_tracing_pipeline_feature_unavailable",
            features.get("ray_tracing_pipeline_supported") is True,
        ),
        (
            "output_storage_image_format_unavailable",
            features.get("output_rgba32_uint_storage_image") is True,
        ),
        (
            "ray_tracing_properties_invalid",
            features.get("ray_tracing_properties_valid") is True,
        ),
    ]
    for requirement_decision, requirement_satisfied in ordered_requirements:
        if decision == requirement_decision:
            return not requirement_satisfied
        if not requirement_satisfied:
            return False
    return False


def read_json(path: Path, label: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise Rt6Error(f"could not read {label}: {error}") from error
    if not isinstance(value, dict):
        raise Rt6Error(f"{label} must be a JSON object")
    return value


def write_json_atomically(path: Path, value: dict[str, Any]) -> None:
    temporary = path.with_name(path.name + ".tmp")
    try:
        temporary.write_text(
            json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        os.replace(temporary, path)
    except OSError as error:
        try:
            temporary.unlink(missing_ok=True)
        except OSError:
            pass
        raise Rt6Error(f"could not publish RT6 attestation: {error}") from error


def executable_path(build_dir: Path) -> Path:
    candidates = (
        build_dir / "bin" / "ror_ogre_next_vulkan_rt6_smoke",
        build_dir / "bin" / REQUIRED_CONFIG / "ror_ogre_next_vulkan_rt6_smoke",
    )
    existing = [candidate for candidate in candidates if candidate.is_file()]
    if len(existing) != 1:
        raise Rt6Error(f"expected exactly one RT6 executable, found {len(existing)}")
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
    scope = section(report, "scope")
    provenance = section(report, "provenance")
    build = section(report, "build")
    vulkan = section(report, "vulkan")
    extensions = section(report, "required_extensions")
    features = section(report, "features")
    ray_properties = section(report, "ray_properties")
    adoption = section(report, "ogre_external_adoption")
    geometry = section(report, "geometry_and_acceleration")
    dispatch = section(report, "pipeline_and_dispatch")
    timeline = section(report, "timeline")
    lifecycle = section(report, "lifecycle")
    loader = api_version_tuple(vulkan.get("loader_api_version"))
    physical = api_version_tuple(vulkan.get("physical_device_api_version"))

    section_names = (
        "scope",
        "provenance",
        "build",
        "vulkan",
        "required_extensions",
        "features",
        "ray_properties",
        "ogre_external_adoption",
        "geometry_and_acceleration",
        "pipeline_and_dispatch",
        "timeline",
        "lifecycle",
    )
    extension_boolean_fields = (
        "deferred_host_operations",
        "buffer_device_address",
        "acceleration_structure",
        "ray_tracing_pipeline",
        "enabled_set_exact",
    )
    feature_boolean_fields = (
        "graphics_queue",
        "compute_on_graphics_queue",
        "timeline_semaphore_supported",
        "timeline_semaphore_enabled",
        "buffer_device_address_supported",
        "buffer_device_address_enabled",
        "acceleration_structure_supported",
        "acceleration_structure_enabled",
        "ray_tracing_pipeline_supported",
        "ray_tracing_pipeline_enabled",
        "output_rgba32_uint_storage_image",
        "ray_tracing_properties_valid",
        "all_supported_core_features_enabled",
        "enabled_instance_extensions_exact",
    )
    adoption_boolean_fields = (
        "instance_injected_exactly",
        "physical_device_injected_exactly",
        "logical_device_injected_exactly",
        "graphics_queue_injected_exactly",
        "ogre_external_ownership_observed",
    )
    geometry_boolean_fields = ("geometry_buffer_created", "blas_built", "tlas_built")
    dispatch_boolean_fields = (
        "descriptor_set_bound",
        "ray_pipeline_created",
        "shader_binding_table_created",
        "ray_dispatch_completed",
        "output_image_copied_to_host",
        "primary_hit_observed",
    )
    lifecycle_boolean_fields = (
        "ogre_shutdown_before_owner_teardown",
        "ray_resources_destroyed_before_device",
        "timeline_destroyed_before_device",
        "device_destroyed_before_instance",
        "shutdown_completed",
    )
    boolean_groups = (
        (extensions, extension_boolean_fields),
        (features, feature_boolean_fields),
        (adoption, adoption_boolean_fields),
        (geometry, geometry_boolean_fields),
        (dispatch, dispatch_boolean_fields),
        (lifecycle, lifecycle_boolean_fields),
    )
    address_fields = (
        (geometry, "geometry_buffer_device_address"),
        (geometry, "instance_buffer_device_address"),
        (geometry, "scratch_buffer_device_address"),
        (geometry, "blas_device_address"),
        (geometry, "tlas_device_address"),
        (dispatch, "shader_binding_table_device_address"),
    )
    checks = {
        "schema": report.get("schema") == SCHEMA,
        "exit_code": expected_status is not None,
        "status": expected_status is not None and status == expected_status,
        "reason_type": isinstance(report.get("reason"), str),
        "section_types": all(isinstance(report.get(name), dict) for name in section_names),
        "ror_repository": provenance.get("ror_repository") == source_identity["repository"],
        "ror_ref": provenance.get("ror_ref") == source_identity["ref"],
        "ror_commit": provenance.get("ror_commit") == source_identity["commit"],
        "ror_manifest": provenance.get("ror_relevant_source_manifest_sha256")
        == source_identity["relevant_manifest_sha256"],
        "ror_manifest_count": provenance.get("ror_relevant_source_manifest_file_count")
        == source_identity["relevant_manifest_file_count"],
        "ogre_repository": provenance.get("ogre_next_repository") == lock["repository"],
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
        "processor": str(build.get("processor", "")).lower() in {"amd64", "x86_64"},
        "compiler": isinstance(build.get("compiler_id"), str)
        and bool(build.get("compiler_id")),
        "compiler_version": isinstance(build.get("compiler_version"), str)
        and bool(build.get("compiler_version")),
        "ogre_version": build.get("ogre_version") == "3.0.0",
        "pointer_bits": build.get("pointer_bits") == 64
        and type(build.get("pointer_bits")) is int,
        "checkpoint": scope.get("checkpoint") == "rt6",
        "scope_pass_type": type(scope.get("hardware_ray_dispatch_pass")) is bool,
        "scope_image_type": type(scope.get("ray_traced_image_produced")) is bool,
        "no_composite_claim": scope.get("ogre_native_image_composite")
        == "not_evaluated",
        "api_floor": vulkan.get("requested_instance_api_version") == "1.2.0",
        "loader_api_format": loader is not None,
        "physical_api_format": physical is not None,
        "driver_version": is_uint32(vulkan.get("driver_version_packed")),
        "vendor_id": is_uint32(vulkan.get("vendor_id")),
        "device_id": is_uint32(vulkan.get("device_id")),
        "device_name_type": isinstance(vulkan.get("device_name"), str),
        "device_class": vulkan.get("device_class") in DEVICE_CLASSES,
        "software_flag": type(vulkan.get("known_software_adapter")) is bool,
        "identity_flag": type(vulkan.get("device_identity_available")) is bool,
        "candidate_decision": vulkan.get("candidate_decision") in CANDIDATE_DECISIONS,
        "queue_family": is_uint32(vulkan.get("graphics_queue_family")),
        "queue_index": is_uint32(vulkan.get("graphics_queue_index")),
        "boolean_types": all(
            type(group.get(field)) is bool
            for group, fields in boolean_groups
            for field in fields
        ),
        "enabled_extension_count": is_uint32(extensions.get("enabled_count")),
        "ray_property_types": all(
            is_uint32(ray_properties.get(field))
            for field in (
                "shader_group_handle_size",
                "shader_group_handle_alignment",
                "shader_group_base_alignment",
                "max_ray_recursion_depth",
                "acceleration_structure_scratch_alignment",
            )
        ),
        "mirror_vertex_count": geometry.get("mirror_vertex_count") == 3
        and type(geometry.get("mirror_vertex_count")) is int,
        "address_types": all(is_uint64(group.get(field)) for group, field in address_fields),
        "dispatch_dimensions": dispatch.get("dispatch_dimensions") == [1, 1, 1]
        and all(type(item) is int for item in dispatch.get("dispatch_dimensions", [])),
        "output_format": dispatch.get("output_format")
        == "VK_FORMAT_R32G32B32A32_UINT",
        "shader_contract_type": type(dispatch.get("shader_contract_compiled")) is bool,
        "shader_contract_compiled": dispatch.get("shader_contract_compiled") is True,
        "expected_words": dispatch.get("expected_primary_hit_words")
        == EXPECTED_HIT_WORDS,
        "expected_word_types": isinstance(dispatch.get("expected_primary_hit_words"), list)
        and all(is_uint32(item) for item in dispatch["expected_primary_hit_words"]),
        "readback_types": isinstance(dispatch.get("readback_words"), list)
        and len(dispatch.get("readback_words", [])) == 4
        and all(is_uint32(item) for item in dispatch["readback_words"]),
        "timeline_types": all(
            is_uint64(timeline.get(field))
            for field in (
                "value_before_ray_dispatch",
                "value_at_ray_dispatch",
                "value_after_ogre_shutdown",
            )
        ),
        "plugin_option": adoption.get("plugin_option") == "external_instance",
        "window_option": adoption.get("first_window_option") == "external_device",
        "null_window": adoption.get("window_type") == "null",
        "shutdown": lifecycle.get("shutdown_completed") is True,
    }

    if status == "pass":
        handle_alignment = ray_properties.get("shader_group_handle_alignment")
        base_alignment = ray_properties.get("shader_group_base_alignment")
        scratch_alignment = ray_properties.get("acceleration_structure_scratch_alignment")
        checks.update(
            {
                "no_reason": report.get("reason") == "",
                "hardware_scope": scope.get("hardware_ray_dispatch_pass") is True,
                "native_rt_scope": scope.get("native_ray_tracing")
                == "hardware_dispatch_pass",
                "image_scope": scope.get("ray_traced_image_produced") is True,
                "loader_ready": loader is not None and loader >= (1, 2, 0),
                "physical_ready": physical is not None and physical >= (1, 2, 0),
                "hardware_device": vulkan.get("device_class")
                in {"integrated_gpu", "discrete_gpu"},
                "not_software": vulkan.get("known_software_adapter") is False,
                "identity_available": vulkan.get("device_identity_available") is True,
                "accepted": vulkan.get("candidate_decision") == "accept",
                "device_name": bool(vulkan.get("device_name")),
                "device_uuid": isinstance(vulkan.get("device_uuid"), str)
                and re.fullmatch(r"[0-9a-f]{32}", vulkan["device_uuid"]) is not None
                and vulkan["device_uuid"] != "0" * 32,
                "queue": features.get("graphics_queue") is True,
                "compute_queue": features.get("compute_on_graphics_queue") is True,
                "queue_index_zero": vulkan.get("graphics_queue_index") == 0,
                "timeline_support": features.get("timeline_semaphore_supported") is True,
                "timeline_enable": features.get("timeline_semaphore_enabled") is True,
                "extensions_supported": all(
                    extensions.get(field) is True
                    for field in (
                        "deferred_host_operations",
                        "buffer_device_address",
                        "acceleration_structure",
                        "ray_tracing_pipeline",
                    )
                ),
                "extensions_exact": extensions.get("enabled_count") == 4
                and extensions.get("enabled_set_exact") is True,
                "rt_features": all(
                    features.get(field) is True
                    for field in (
                        "buffer_device_address_supported",
                        "buffer_device_address_enabled",
                        "acceleration_structure_supported",
                        "acceleration_structure_enabled",
                        "ray_tracing_pipeline_supported",
                        "ray_tracing_pipeline_enabled",
                        "output_rgba32_uint_storage_image",
                        "ray_tracing_properties_valid",
                        "all_supported_core_features_enabled",
                        "enabled_instance_extensions_exact",
                    )
                ),
                "handle_size": ray_properties.get("shader_group_handle_size") == 32,
                "handle_alignment": is_uint32(handle_alignment)
                and handle_alignment > 0
                and handle_alignment & (handle_alignment - 1) == 0,
                "base_alignment": is_uint32(base_alignment)
                and base_alignment > 0
                and base_alignment & (base_alignment - 1) == 0,
                "recursion": ray_properties.get("max_ray_recursion_depth", 0) >= 1,
                "scratch_alignment": is_uint32(scratch_alignment)
                and scratch_alignment > 0
                and scratch_alignment & (scratch_alignment - 1) == 0,
                "nonzero_addresses": all(
                    is_positive_uint64(group.get(field)) for group, field in address_fields
                ),
                "geometry_address_aligned": is_positive_uint64(
                    geometry.get("geometry_buffer_device_address")
                )
                and geometry["geometry_buffer_device_address"] % 4 == 0,
                "instance_address_aligned": is_positive_uint64(
                    geometry.get("instance_buffer_device_address")
                )
                and geometry["instance_buffer_device_address"] % 16 == 0,
                "scratch_address_aligned": is_positive_uint64(
                    geometry.get("scratch_buffer_device_address")
                )
                and is_uint32(scratch_alignment)
                and scratch_alignment > 0
                and geometry["scratch_buffer_device_address"] % scratch_alignment == 0,
                "sbt_address_aligned": is_positive_uint64(
                    dispatch.get("shader_binding_table_device_address")
                )
                and is_uint32(base_alignment)
                and base_alignment > 0
                and dispatch["shader_binding_table_device_address"] % base_alignment == 0,
                "adoption": all(
                    adoption.get(field) is True
                    for field in adoption_boolean_fields
                ),
                "geometry_built": geometry.get("geometry_buffer_created") is True
                and geometry.get("blas_built") is True
                and geometry.get("tlas_built") is True,
                "pipeline_dispatch": all(
                    dispatch.get(field) is True for field in dispatch_boolean_fields
                ),
                "deterministic_hit": dispatch.get("readback_words")
                == EXPECTED_HIT_WORDS
                and any(dispatch.get("readback_words", [])),
                "timeline_sequence": timeline.get("value_before_ray_dispatch") == 1
                and timeline.get("value_at_ray_dispatch") == 2
                and timeline.get("value_after_ogre_shutdown") == 3,
                "lifecycle": all(
                    lifecycle.get(field) is True for field in lifecycle_boolean_fields
                ),
            }
        )
    else:
        checks.update(
            {
                "explicit_reason": isinstance(report.get("reason"), str)
                and bool(report.get("reason")),
                "reason_consistent": unsupported_reason_is_consistent(
                    report.get("reason"), vulkan, extensions, features
                ),
                "no_hardware_scope": scope.get("hardware_ray_dispatch_pass") is False,
                "no_native_rt_scope": scope.get("native_ray_tracing") == "not_executed",
                "no_image_scope": scope.get("ray_traced_image_produced") is False,
                "candidate_not_accepted": vulkan.get("candidate_decision") != "accept",
                "no_enabled_device_state": extensions.get("enabled_count") == 0
                and extensions.get("enabled_set_exact") is False
                and features.get("timeline_semaphore_enabled") is False
                and features.get("buffer_device_address_enabled") is False
                and features.get("acceleration_structure_enabled") is False
                and features.get("ray_tracing_pipeline_enabled") is False
                and features.get("all_supported_core_features_enabled") is False,
                "no_adoption": all(
                    adoption.get(field) is False for field in adoption_boolean_fields
                ),
                "no_geometry": all(
                    geometry.get(field) is False for field in geometry_boolean_fields
                )
                and all(group.get(field) == 0 for group, field in address_fields[:5]),
                "no_dispatch": all(
                    dispatch.get(field) is False for field in dispatch_boolean_fields
                )
                and dispatch.get("shader_binding_table_device_address") == 0
                and dispatch.get("readback_words") == [0, 0, 0, 0],
                "no_timeline": timeline.get("value_before_ray_dispatch") == 0
                and timeline.get("value_at_ray_dispatch") == 0
                and timeline.get("value_after_ogre_shutdown") == 0,
                "no_order_claims": all(
                    lifecycle.get(field) is False
                    for field in lifecycle_boolean_fields
                    if field != "shutdown_completed"
                ),
            }
        )
    failed = sorted(name for name, passed in checks.items() if not passed)
    if failed:
        raise Rt6Error("RT6 report failed closed: " + ", ".join(failed))


def make_attestation(
    report_path: Path,
    executable: Path,
    report: dict[str, Any],
    process_exit_code: int,
    source_identity: dict[str, Any],
) -> dict[str, Any]:
    passed = report.get("status") == "pass"
    return {
        "schema": ATTESTATION_SCHEMA,
        "status": report["status"],
        "process_exit_code": process_exit_code,
        "report": {"name": report_path.name, "sha256": sha256_file(report_path)},
        "executable": {"name": executable.name, "sha256": sha256_file(executable)},
        "ror_source": source_identity,
        "claims": {
            "hardware_ray_dispatch_pass": passed,
            "native_ray_tracing": "hardware_dispatch_pass" if passed else "not_executed",
            "deterministic_primary_hit_verified": passed,
            "spirv_1_4_shaders_compiled": True,
            "expected_primary_hit_words": EXPECTED_HIT_WORDS,
            "software_adapter_rt_pass": False,
            "ogre_native_image_composite": "not_evaluated",
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
    passed = report.get("status") == "pass"
    expected_exit = 0 if passed else UNSUPPORTED_EXIT_CODE
    checks = {
        "schema": attestation.get("schema") == ATTESTATION_SCHEMA,
        "status": attestation.get("status") == report.get("status"),
        "exit": attestation.get("process_exit_code") == expected_exit,
        "report_name": report_record.get("name") == report_path.name,
        "report_hash": report_record.get("sha256") == sha256_file(report_path),
        "executable_name": executable_record.get("name") == executable.name,
        "executable_hash": executable_record.get("sha256") == sha256_file(executable),
        "ror_source": attestation.get("ror_source") == source_identity,
        "hardware_dispatch": claims.get("hardware_ray_dispatch_pass") is passed,
        "native_rt": claims.get("native_ray_tracing")
        == ("hardware_dispatch_pass" if passed else "not_executed"),
        "primary_hit": claims.get("deterministic_primary_hit_verified") is passed,
        "shaders_compiled": claims.get("spirv_1_4_shaders_compiled") is True,
        "expected_words": claims.get("expected_primary_hit_words") == EXPECTED_HIT_WORDS,
        "software_rt": claims.get("software_adapter_rt_pass") is False,
        "no_composite": claims.get("ogre_native_image_composite") == "not_evaluated",
        "complete": attestation.get("complete") is True,
    }
    for value, label in (
        (report_record.get("sha256"), "attested report hash"),
        (executable_record.get("sha256"), "attested executable hash"),
    ):
        require_sha256(value, label)
    failed = sorted(name for name, passed_check in checks.items() if not passed_check)
    if failed:
        raise Rt6Error("RT6 attestation failed closed: " + ", ".join(failed))


def validate_static_contract() -> None:
    bootstrap = (
        REPOSITORY_ROOT
        / "source/main/gfx/render/ogrenext/OgreNextVulkanRayTracingBootstrap.cpp"
    ).read_text(encoding="utf-8")
    smoke = (PROBE_SOURCE / "src/vulkan_rt6_smoke.cpp").read_text(encoding="utf-8")
    cmake = (PROBE_SOURCE / "CMakeLists.txt").read_text(encoding="utf-8")
    contract = (
        REPOSITORY_ROOT / "source/main/gfx/render/ogrenext/OgreNextVulkanRt6Contract.cpp"
    ).read_text(encoding="utf-8")
    for token in (
        "VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME",
        "VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME",
        "VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME",
        "VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME",
        "VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR",
        "vkCmdBuildAccelerationStructuresKHR",
        "vkCreateRayTracingPipelinesKHR",
        "vkGetRayTracingShaderGroupHandlesKHR",
        "vkCmdTraceRaysKHR",
        "VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR",
        "vkCmdCopyImageToBuffer",
        "kExpectedPrimaryHit",
        "readback_words == kExpectedPrimaryHit",
        "CompileRt6Shaders",
        "minAccelerationStructureScratchOffsetAlignment",
        "AlignUp(scratch_base, scratch_alignment)",
        "VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR",
        "external_instance.instanceExtensions.clear()",
        "external_device.deviceExtensions.clear()",
        "mIsExternal",
    ):
        if token not in bootstrap:
            raise Rt6Error(f"RT6 bootstrap contract token is missing: {token}")
    for token in (
        "SOFTWARE_OR_UNATTESTED_DEVICE",
        "DEFERRED_HOST_OPERATIONS_EXTENSION_UNAVAILABLE",
        "RAY_TRACING_PROPERTIES_INVALID",
        "MarkRayDispatched",
        "MarkRayResourcesDestroyed",
    ):
        if token not in contract:
            raise Rt6Error(f"RT6 policy contract token is missing: {token}")
    for token in (
        'plugin_options["external_instance"]',
        'window_parameters["external_device"]',
        'window_parameters["windowType"] = "null"',
        "ProveTimelineQueue(1U)",
        "ValidateShaderContract()",
        "ProveRayTracingDispatch(2U)",
        "ProveTimelineQueue(3U)",
        "kUnsupportedExitCode = 77",
    ):
        if token not in smoke:
            raise Rt6Error(f"RT6 smoke contract token is missing: {token}")
    for token in (
        "ROR_OGRE_NEXT_VULKAN_RT6",
        "ror_ogre_next_vulkan_rt6_smoke",
        "shaderc_combined",
        "SKIP_RETURN_CODE 77",
    ):
        if token not in cmake:
            raise Rt6Error(f"RT6 CMake contract token is missing: {token}")


def configure_build(build_dir: Path, generator: str | None) -> None:
    command = [
        "cmake",
        "-S",
        str(PROBE_SOURCE),
        "-B",
        str(build_dir),
        "-DROR_OGRE_NEXT_PROBE=ON",
        "-DROR_OGRE_NEXT_VULKAN_RT6=ON",
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
        raise Rt6Error("native RT6 execution is reviewed only for Linux x86_64")
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
    MAIN_RUNNER.validate_build_contract(build_contract, lock, policy, source_identity)
    MAIN_RUNNER.run(
        [
            "cmake",
            "--build",
            str(build_dir),
            "--target",
            "ror_ogre_next_vulkan_rt6_smoke",
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
        raise Rt6Error(f"could not execute RT6 proof: {error}") from error
    if result.returncode not in (0, UNSUPPORTED_EXIT_CODE):
        raise Rt6Error(f"RT6 proof failed with exit code {result.returncode}")
    if not report_path.is_file():
        raise Rt6Error("RT6 proof did not publish its report")
    report = read_json(report_path, "RT6 report")
    validate_report(report, result.returncode, lock, source_identity)
    MAIN_RUNNER.require_source_identity_unchanged(source_identity)
    attestation = make_attestation(
        report_path, executable, report, result.returncode, source_identity
    )
    write_json_atomically(attestation_path, attestation)
    validate_attestation(attestation, report_path, executable, report, source_identity)
    return report


def verify_existing(build_dir: Path) -> dict[str, Any]:
    report_path = build_dir / REPORT_NAME
    attestation_path = build_dir / ATTESTATION_NAME
    executable = executable_path(build_dir)
    report = read_json(report_path, "RT6 report")
    attestation = read_json(attestation_path, "RT6 attestation")
    exit_code = 0 if report.get("status") == "pass" else UNSUPPORTED_EXIT_CODE
    lock = MAIN_RUNNER.load_lock()
    source_identity = MAIN_RUNNER.ror_source_identity()
    validate_report(report, exit_code, lock, source_identity)
    validate_attestation(attestation, report_path, executable, report, source_identity)
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
            raise Rt6Error("--jobs must be positive")
        if args.validate_contract_only and args.verify_existing:
            raise Rt6Error("--validate-contract-only and --verify-existing conflict")
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
                        "native_ray_tracing": "requires_actual_dispatch",
                        "expected_primary_hit_words": EXPECTED_HIT_WORDS,
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
                raise Rt6Error("reused builds cannot change the generator")
            report = run_proof(args)
        print(json.dumps(report, indent=2, sort_keys=True))
        return 0
    except (Rt6Error, MAIN_RUNNER.ProbeError) as error:
        print(f"Ogre-Next Vulkan RT6 failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
