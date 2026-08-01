#!/usr/bin/env python3
"""Fail unless every required OGRE-Next CI artifact exists and is nonempty."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import struct
import sys


REQUIRED_ARTIFACTS = (
    "ogre-next-build-contract.json",
    "ror-ogre-next-probe-report.json",
    "ror-ogre-next-frame-probe-report.json",
    "ror-ogre-next-frame-probe.ppm",
    "ror-ogre-next-frontend-n1-report.json",
    "ror-ogre-next-frontend-n1.ppm",
    "ror-ogre-next-frontend-rt4-pbr-v1-report.json",
    "ror-ogre-next-frontend-rt4-pbr-v1.ppm",
    "ror-ogre-next-frontend-rt4-pbr-v1-isolation.bin",
    "ror-ogre-next-frontend-rt4-pbr-v1-reflection.bin",
    "ror-ogre-next-frontend-rt4-pbr-v1-attestation.json",
    "ror-ogre-next-pssm-shadow-report.json",
)
PSSM_REPORT_ARTIFACT = "ror-ogre-next-pssm-shadow-report.json"
PSSM_EVIDENCE_ARTIFACT = "ror-ogre-next-pssm-shadow-isolation.bin"
PSSM_EXECUTABLE_STEM = "ror_ogre_next_pssm_shadow_smoke"
PSSM_REPORT_SCHEMA = "ror.ogre_next_pssm_shadow_smoke.v2"
PSSM_UNSUPPORTED_DETAIL = (
    "PSSM_3_CASCADE_V1 native capability gate rejected the required atlas "
    "or PCF4 support"
)
RT4_REPORT_ARTIFACT = "ror-ogre-next-frontend-rt4-pbr-v1-report.json"
RT4_PPM_ARTIFACT = "ror-ogre-next-frontend-rt4-pbr-v1.ppm"
RT4_ISOLATION_ARTIFACT = "ror-ogre-next-frontend-rt4-pbr-v1-isolation.bin"
RT4_REFLECTION_ARTIFACT = "ror-ogre-next-frontend-rt4-pbr-v1-reflection.bin"
RT4_ATTESTATION_ARTIFACT = (
    "ror-ogre-next-frontend-rt4-pbr-v1-attestation.json"
)
RT4_PACKAGE_EXECUTABLE_STEM = "ror_ogre_next_frontend_n1_smoke"
REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
PINNED_LOCK_PATH = REPOSITORY_ROOT / "tools/ogre_next_probe/ogre-next.lock.json"
NORMAL_MAP_SOURCE_LOCK_PATH = (
    REPOSITORY_ROOT
    / "tools/ogre_next_probe/ogre-next-normal-map-source.lock.json"
)
NORMAL_MAP_SOURCE_LOCK_SHA256 = (
    "7d180c54c54e7cc26b0081753c621b7164551d2b631c1127f818fbb22645f682"
)
ROR_SOURCE_REPOSITORY = "https://github.com/oasiz-ai/rigs-of-rods"
RELEVANT_SOURCE_PATHS = (
    "source/main/gfx/render",
    "tools/ogre_next_probe",
    "tools/run_ogre_next_probe.py",
    "tools/validate_ogre_next_frame_probe.py",
    "tools/verify_ogre_next_artifact_set.py",
)
RT4_ATTESTATION_SCHEMA = (
    "ror.ogre_next_frontend_rt4_pbr_v1.attestation.v3"
)
RT4_INTEGRITY_MODEL = (
    "self-contained-checksums-plus-independent-semantics; "
    "not-a-cryptographic-signature"
)
RT4_REPORT_SCHEMA = "ror.ogre_next_frontend_rt4_pbr_v1_smoke.v2"
RT4_REFLECTION_SCHEMA = "ror.ogre_next_rt4_reflection_probes.v1"
RT4_REFLECTION_RESOLUTION = 32
RT4_REFLECTION_FACE_COUNT = 6
RT4_REFLECTION_RAW_BYTES = (
    RT4_REFLECTION_RESOLUTION
    * RT4_REFLECTION_RESOLUTION
    * RT4_REFLECTION_FACE_COUNT
    * 8
)
RT4_REFLECTION_FILTERED_DIMENSIONS = (32, 16)
RT4_REFLECTION_FILTERED_BYTES = sum(
    dimension * dimension * RT4_REFLECTION_FACE_COUNT * 8
    for dimension in RT4_REFLECTION_FILTERED_DIMENSIONS
)
RT4_REFLECTION_EVIDENCE_BYTES = (
    RT4_REFLECTION_RAW_BYTES + RT4_REFLECTION_FILTERED_BYTES
)
RT4_REFLECTION_BACKENDS = {
    "macos-arm64-metal": "OGRE_NEXT_METAL",
    "windows-x64-d3d11": "OGRE_NEXT_D3D11",
    "linux-x86_64-vulkan": "OGRE_NEXT_VULKAN",
}
RT4_EXECUTABLE_IDENTITY_SCHEMA = (
    "ror.ogre_next_frontend_n1.build_identity.v1"
)
RT4_EXPECTED_VARIANTS = (
    ("baseline", "none"),
    ("base_color", "base_color_rgb"),
    ("roughness_g", "packed_green_roughness"),
    ("metallic_b", "packed_blue_metallic"),
    ("emissive", "emissive_rgb"),
    ("normal_rg", "canonical_positive_z_normal_rg"),
    ("sampler_uv", "sampler_address_over_uv0"),
)
RT4_EXPECTED_RETIREMENT = {
    "schema": "ror.ogre_next_rt4_texture_retirement.v1",
    "derived_allocation": "normal_RG8_UNORM",
    "isolated_from_visual_variants": True,
    "native_image_rg8_staging": {
        "version": 1,
        "verified_uploads": 2,
        "verified_mip_levels": 3,
        "verified_rows": 5,
        "verified_texels": 14,
        "verified_rg_bytes": 28,
        "verified_padded_source_rows": 5,
        "exact_source_rg_to_native_image": True,
    },
    "transitions": [
        {"revision": 1, "width": 2, "height": 2, "mip_levels": 1},
        {
            "revision": 2,
            "width": 4,
            "height": 2,
            "mip_levels": 2,
            "padded_rows": True,
        },
        {"revision": 3, "width": 2, "height": 2, "mip_levels": 1},
    ],
    "exact_extent_and_mip_transitions": True,
    "renders_through_transitions_and_restart": True,
    "find_texture_no_throw_rejected_old_names": True,
    "audits": {
        "initial": {
            "creates": 1,
            "destroys": 0,
            "live": 1,
            "retired_name_lookups": 0,
            "retired_name_rejections": 0,
        },
        "expanded": {
            "creates": 2,
            "destroys": 1,
            "live": 1,
            "retired_name_lookups": 1,
            "retired_name_rejections": 1,
        },
        "restored": {
            "creates": 3,
            "destroys": 2,
            "live": 1,
            "retired_name_lookups": 2,
            "retired_name_rejections": 2,
        },
        "first_shutdown": {
            "creates": 3,
            "destroys": 3,
            "live": 0,
            "retired_name_lookups": 3,
            "retired_name_rejections": 3,
        },
        "restarted": {
            "creates": 4,
            "destroys": 3,
            "live": 1,
            "retired_name_lookups": 3,
            "retired_name_rejections": 3,
        },
        "final_shutdown": {
            "creates": 4,
            "destroys": 4,
            "live": 0,
            "retired_name_lookups": 4,
            "retired_name_rejections": 4,
        },
    },
}
RT4_EXPECTED_TEXTURE_ALLOCATIONS = {
    "version": 1,
    "live_source_textures": 4,
    "sampled_rgba_allocations": 2,
    "roughness_r8_allocations": 1,
    "metallic_r8_allocations": 1,
    "normal_rg8_allocations": 1,
    "unused_packed_rgba_allocations": 0,
    "exact_usage": True,
}
RT4_EXPECTED_LIFECYCLE = {
    "unsupported_depth_failed_before_submission": True,
    "non_uniform_scale_rejected_before_submission": True,
    "double_sided_pbs_readback": True,
    "lifetime_snapshot_identity_replay": True,
    "lifetime_completed_frame_queries": True,
    "process_global_root_exclusion": True,
    "live_texture_replacement_retirement": True,
    "replacement_audit": {
        "creates": 17,
        "destroys": 12,
        "live": 5,
        "retired_name_lookups": 12,
        "retired_name_rejections": 12,
        "exact_usage": True,
    },
    "shutdown_reinitialize_render_shutdown": True,
}
RT4_ROLLBACK_STAGES = (
    "after_create",
    "after_set_resolution",
    "after_set_mipmaps",
    "after_set_pixel_format",
    "after_schedule_transition",
)
RT4_EXPECTED_TEXTURE_UPLOAD_ROLLBACK = {
    "schema": "ror.ogre_next_rt4_texture_upload_rollback.v1",
    "derived_allocation": "normal_RG8_UNORM",
    "injected_post_create_stage_count": len(RT4_ROLLBACK_STAGES),
    "stages": [
        {
            "name": name,
            "audits": {
                "after_failure": {
                    "creates": 1,
                    "destroys": 1,
                    "live": 0,
                    "retired_name_lookups": 1,
                    "retired_name_rejections": 1,
                    "exact_usage": True,
                },
                "after_retry": {
                    "creates": 2,
                    "destroys": 1,
                    "live": 1,
                    "retired_name_lookups": 1,
                    "retired_name_rejections": 1,
                    "exact_usage": True,
                },
                "after_replacement": {
                    "creates": 3,
                    "destroys": 2,
                    "live": 1,
                    "retired_name_lookups": 2,
                    "retired_name_rejections": 2,
                    "exact_usage": True,
                },
                "after_shutdown": {
                    "creates": 3,
                    "destroys": 3,
                    "live": 0,
                    "retired_name_lookups": 3,
                    "retired_name_rejections": 3,
                    "exact_usage": False,
                },
            },
        }
        for name in RT4_ROLLBACK_STAGES
    ],
    "clean_retry_replacement_shutdown": True,
}
PLATFORM_CONTRACTS = {
    "macos-arm64-metal": {
        "systems": {"Darwin"},
        "processors": {"arm64", "aarch64"},
        "renderer_target": "RenderSystem_Metal",
        "renderer_name": "Metal Rendering Subsystem",
        "device_option_name": "Rendering Device",
        "compiler_ids": {"AppleClang"},
        "binary_format": "mach-o-64",
        "binary_architecture": "arm64",
    },
    "windows-x64-d3d11": {
        "systems": {"Windows"},
        "processors": {"AMD64", "amd64", "x86_64"},
        "renderer_target": "RenderSystem_Direct3D11",
        "renderer_name": "Direct3D11 Rendering Subsystem",
        "device_option_name": "Rendering Device",
        "compiler_ids": {"MSVC"},
        "binary_format": "pe32+",
        "binary_architecture": "x86_64",
    },
    "linux-x86_64-vulkan": {
        "systems": {"Linux"},
        "processors": {"AMD64", "amd64", "x86_64"},
        "renderer_target": "RenderSystem_Vulkan",
        "renderer_name": "Vulkan Rendering Subsystem",
        "device_option_name": "Device",
        "compiler_ids": {"GNU", "Clang"},
        "binary_format": "elf64",
        "binary_architecture": "x86_64",
    },
}
METAL_N2_REQUIRED_ARTIFACTS = (
    "ror-ogre-next-metal-n2-report.json",
    "ror-ogre-next-metal-n2-attestation.json",
    "bin/ror_ogre_next_metal_n2_smoke",
)
METAL_N2_PROBE_ARTIFACT = "ror-ogre-next-metal-n2-probe.bin"
METAL_N3_REQUIRED_ARTIFACTS = (
    "ror-ogre-next-metal-n3-report.json",
    "ror-ogre-next-metal-n3-attestation.json",
    "bin/ror_ogre_next_metal_n3_smoke",
)
METAL_N3_IMAGE_ARTIFACTS = (
    ("raster_only_hdr", "ror-ogre-next-metal-n3-raster.bin"),
    ("rt_contribution", "ror-ogre-next-metal-n3-contribution.bin"),
    ("hybrid_hdr", "ror-ogre-next-metal-n3-hybrid.bin"),
)
METAL_N3_SCOPE = (
    "same-device Metal primary-ray hit contribution composited into exact "
    "UI-free Ogre-Next HDR target; no GI, reflection, denoising, multi-bounce, "
    "or material parity claim"
)
METAL_N3_REQUIRED_PROOF_BOOLEANS = (
    "exact_exported_vertex_slice_used",
    "exact_exported_index_slice_used",
    "exact_exported_color_image_used",
    "gpu_composite_not_cpu_postprocess",
    "hybrid_changes_only_on_contribution",
    "all_channels_finite",
    "second_camera_changes_contribution_hash",
    "camera_mismatch_rejected",
    "snapshot_transform_mismatch_rejected",
    "off_axis_far_plane_hit_passed",
    "released_frame_allows_extent_change",
    "submitted_device_loss_and_timeout_paths_tested",
    "simultaneous_rt4_n3",
    "textured_rt4_geometry_rendered",
    "calibrated_directional_light_applied",
    "exact_48_byte_vertex_layout_exported",
    "texture_allocation_audit_exact",
    "texture_teardown_audit_exact",
    "view_dependent_output_ready",
    "hybrid_composite_ready",
)


class ArtifactSetError(RuntimeError):
    """Raised when a required artifact is missing, empty, or indirect."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _read_json_object(path: Path, label: str) -> dict[str, object]:
    def reject_duplicate_keys(pairs: list[tuple[str, object]]) -> dict[str, object]:
        result: dict[str, object] = {}
        for key, value in pairs:
            if key in result:
                raise ArtifactSetError(
                    f"invalid {label}: duplicate JSON object key {key!r}"
                )
            result[key] = value
        return result

    try:
        value = json.loads(
            path.read_text(encoding="utf-8"),
            object_pairs_hook=reject_duplicate_keys,
        )
    except (OSError, json.JSONDecodeError) as error:
        raise ArtifactSetError(f"invalid {label}: {error}") from error
    if not isinstance(value, dict):
        raise ArtifactSetError(f"invalid {label}: root is not an object")
    return value


def _verify_attested_file(
    entry: object,
    path: Path,
    expected_name: str,
    checkpoint: str,
    label: str,
) -> None:
    if not isinstance(entry, dict):
        raise ArtifactSetError(f"invalid {checkpoint} {label} attestation")
    expected = {
        "path": expected_name,
        "bytes": path.stat().st_size,
        "sha256": sha256_file(path),
    }
    if not _json_exact(entry, expected):
        raise ArtifactSetError(f"{checkpoint} {label} attestation mismatch")


def _json_exact(actual: object, expected: object) -> bool:
    """Compare JSON values without Python's bool/int equality aliasing."""
    if type(actual) is not type(expected):
        return False
    if isinstance(expected, dict):
        return set(actual) == set(expected) and all(
            _json_exact(actual[key], value) for key, value in expected.items()
        )
    if isinstance(expected, list):
        return len(actual) == len(expected) and all(
            _json_exact(left, right) for left, right in zip(actual, expected)
        )
    return actual == expected


def _require_exact_keys(
    value: object, expected: set[str], label: str
) -> dict[str, object]:
    if not isinstance(value, dict) or set(value) != expected:
        raise ArtifactSetError(f"{label} fields are incomplete or unexpected")
    return value


def _relevant_source_manifest(
    repository_root: Path = REPOSITORY_ROOT,
) -> dict[str, int | str]:
    selected: set[Path] = set()
    for relative in RELEVANT_SOURCE_PATHS:
        path = repository_root / relative
        if path.is_symlink():
            raise ArtifactSetError(
                f"RoR relevant source is indirect: {relative}"
            )
        if path.is_dir():
            selected.update(path.rglob("*"))
        else:
            selected.add(path)
    entries: list[tuple[str, int, str]] = []
    for path in sorted(selected, key=lambda item: item.as_posix()):
        try:
            relative = path.relative_to(repository_root)
        except ValueError as error:
            raise ArtifactSetError("RoR relevant source escaped repository") from error
        if "__pycache__" in relative.parts or path.suffix in (".pyc", ".pyo"):
            continue
        if path.name == ".DS_Store":
            continue
        if path.is_symlink():
            raise ArtifactSetError(
                "RoR relevant source is indirect: " + relative.as_posix()
            )
        if path.is_dir():
            continue
        if not path.is_file():
            raise ArtifactSetError(
                "RoR relevant source is missing or irregular: "
                + relative.as_posix()
            )
        entries.append(
            (relative.as_posix(), path.stat().st_size, sha256_file(path))
        )
    if not entries:
        raise ArtifactSetError("RoR relevant source manifest is empty")
    serialized = "".join(
        f"{relative}|{size}|{digest}\n"
        for relative, size, digest in entries
    ).encode("utf-8")
    return {
        "sha256": hashlib.sha256(serialized).hexdigest(),
        "file_count": len(entries),
    }


def _git_output(repository_root: Path, *arguments: str) -> str:
    try:
        result = subprocess.run(
            ["git", "-C", str(repository_root), *arguments],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
    except OSError as error:
        raise ArtifactSetError(
            f"could not execute Git for RoR provenance: {error}"
        ) from error
    value = result.stdout.strip()
    if result.returncode != 0 or not value:
        raise ArtifactSetError("could not resolve RoR Git provenance")
    return value


def _current_source_identity(
    repository_root: Path = REPOSITORY_ROOT,
    expected_repository: str | None = None,
    expected_ref: str | None = None,
    expected_commit: str | None = None,
) -> dict[str, object]:
    repository = (
        expected_repository
        or os.environ.get("ROR_OGRE_NEXT_EXPECTED_ROR_REPOSITORY")
        or ROR_SOURCE_REPOSITORY
    )
    commit = (
        expected_commit
        or os.environ.get("ROR_OGRE_NEXT_EXPECTED_ROR_COMMIT")
        or os.environ.get("GITHUB_SHA")
        or _git_output(repository_root, "rev-parse", "HEAD")
    )
    ref = (
        expected_ref
        or os.environ.get("ROR_OGRE_NEXT_EXPECTED_ROR_REF")
        or _git_output(repository_root, "rev-parse", "--abbrev-ref", "HEAD")
    )
    if re.fullmatch(r"[0-9a-f]{40}", commit) is None or re.fullmatch(
        r"[A-Za-z0-9._/-]+", ref
    ) is None or repository != ROR_SOURCE_REPOSITORY:
        raise ArtifactSetError("RoR Git provenance is not canonical")
    git_commit = _git_output(repository_root, "rev-parse", "HEAD")
    if commit != git_commit:
        raise ArtifactSetError("expected RoR commit differs from checked-out source")
    manifest = _relevant_source_manifest(repository_root)
    return {
        "repository": repository,
        "ref": ref,
        "commit": commit,
        "relevant_manifest_sha256": manifest["sha256"],
        "relevant_manifest_file_count": manifest["file_count"],
    }


def _read_pinned_lock() -> dict[str, object]:
    lock = _read_json_object(PINNED_LOCK_PATH, "pinned OGRE-Next lock")
    if (
        type(lock.get("schema_version")) is not int
        or lock.get("schema_version") != 3
        or lock.get("name") != "OGRE-Next"
    ):
        raise ArtifactSetError("pinned OGRE-Next lock identity is invalid")
    return lock


def _read_build_contract(
    root: Path, expected_source: dict[str, object]
) -> dict[str, object]:
    contract = _read_json_object(
        root / REQUIRED_ARTIFACTS[0], "OGRE-Next build contract"
    )
    _require_exact_keys(
        contract,
        {
            "schema_version",
            "ror_source",
            "provenance",
            "patches",
            "dependencies",
            "shader_media",
            "reflection_shader_media",
            "platform",
            "abi",
            "components",
            "compiler",
        },
        "OGRE-Next build contract",
    )
    ror_source = contract.get("ror_source")
    ogre_source = contract.get("provenance")
    shader_media = contract.get("shader_media")
    reflection_shader_media = contract.get("reflection_shader_media")
    patches = contract.get("patches")
    platform = contract.get("platform")
    dependencies = contract.get("dependencies")
    abi = contract.get("abi")
    components = contract.get("components")
    compiler = contract.get("compiler")
    notice = (
        shader_media.get("third_party_notice")
        if isinstance(shader_media, dict)
        else None
    )
    if (
        type(contract.get("schema_version")) is not int
        or contract.get("schema_version") != 3
        or not isinstance(ror_source, dict)
        or not isinstance(ogre_source, dict)
        or not isinstance(ror_source.get("repository"), str)
        or not ror_source["repository"]
        or not isinstance(ror_source.get("ref"), str)
        or not ror_source["ref"]
        or not isinstance(ror_source.get("commit"), str)
        or re.fullmatch(r"[0-9a-f]{40}", ror_source["commit"]) is None
        or not _is_sha256(ror_source.get("relevant_manifest_sha256"))
        or not _is_positive_int(ror_source.get("relevant_manifest_file_count"))
        or not isinstance(ogre_source.get("repository"), str)
        or not ogre_source["repository"]
        or not isinstance(ogre_source.get("branch"), str)
        or not ogre_source["branch"]
        or not isinstance(ogre_source.get("commit"), str)
        or re.fullmatch(r"[0-9a-f]{40}", ogre_source["commit"]) is None
        or not _is_sha256(ogre_source.get("archive_sha256"))
        or not isinstance(ogre_source.get("license_spdx"), str)
        or not ogre_source["license_spdx"]
        or not _is_sha256(ogre_source.get("license_sha256"))
        or not isinstance(shader_media, dict)
        or not isinstance(shader_media.get("root"), str)
        or not shader_media["root"]
        or not isinstance(shader_media.get("license_expression"), str)
        or not shader_media["license_expression"]
        or not isinstance(notice, dict)
        or not isinstance(notice.get("source_path"), str)
        or not notice["source_path"]
        or not _is_sha256(notice.get("source_sha256"))
        or not isinstance(notice.get("notice_path"), str)
        or not notice["notice_path"]
        or not _is_sha256(notice.get("notice_sha256"))
        or not isinstance(reflection_shader_media, dict)
        or not isinstance(patches, list)
        or not isinstance(platform, dict)
        or platform.get("policy")
        not in PLATFORM_CONTRACTS
    ):
        raise ArtifactSetError("OGRE-Next build contract source identity is invalid")

    lock = _read_pinned_lock()
    policy = PLATFORM_CONTRACTS[platform["policy"]]
    rapidjson = lock.get("dependencies", {}).get("rapidjson", {})
    lock_abi = lock.get("abi_contract", {})
    expected_abi = {
        key: value for key, value in lock_abi.items() if key != "simd"
    }
    expected_simd = lock_abi.get("simd", {}).get(platform["policy"])
    expected_abi.update(
        {
            "simd_enabled": lock_abi.get("simd", {}).get("enabled"),
            "simd_alignment": lock_abi.get("simd", {}).get("alignment"),
            "simd_family": expected_simd,
            "simd_neon": expected_simd == "neon",
            "simd_sse2": expected_simd == "sse2",
        }
    )
    expected_dependencies = {
        "rapidjson": {
            "tag": rapidjson.get("tag"),
            "archive_sha256": rapidjson.get("archive_sha256"),
            "source_archive_license_spdx": rapidjson.get("license_spdx"),
            "compiled_headers_license_spdx": rapidjson.get(
                "compiled_headers_spdx"
            ),
            "license_sha256": rapidjson.get("license_sha256"),
        }
    }
    expected_components = {
        "hlms_pbs": True,
        "compositor2_core": True,
        "json_materials": True,
        "mesh_lod": True,
        "dds_codec": True,
        "native_ray_tracing": "not_evaluated",
    }
    expected_platform = {
        "policy": platform["policy"],
        "system": platform.get("system"),
        "processor": platform.get("processor"),
        "renderer_target": policy["renderer_target"],
        "device_option_name": policy["device_option_name"],
    }
    expected_ogre = {
        "repository": lock.get("repository"),
        "branch": lock.get("branch"),
        "commit": lock.get("commit"),
        "archive_sha256": lock.get("archive_sha256"),
        "license_spdx": lock.get("license", {}).get("spdx"),
        "license_sha256": lock.get("license", {}).get("sha256"),
    }
    compiler_valid = (
        isinstance(compiler, dict)
        and set(compiler) == {"id", "version", "build_type"}
        and compiler.get("id") in policy["compiler_ids"]
        and isinstance(compiler.get("version"), str)
        and re.fullmatch(r"[A-Za-z0-9.+_-]+", compiler["version"]) is not None
        and compiler.get("build_type") == "Release"
    )
    exact_checks = {
        "source": _json_exact(ror_source, expected_source),
        "ogre": _json_exact(ogre_source, expected_ogre),
        "dependencies": _json_exact(dependencies, expected_dependencies),
        "shader_media": _json_exact(shader_media, lock.get("shader_media")),
        "reflection_shader_media": _json_exact(
            reflection_shader_media, lock.get("reflection_shader_media")
        ),
        "patches": _json_exact(patches, lock.get("patches")),
        "platform": _json_exact(platform, expected_platform)
        and platform.get("system") in policy["systems"]
        and platform.get("processor") in policy["processors"],
        "abi": _json_exact(abi, expected_abi),
        "components": _json_exact(components, expected_components),
        "compiler": compiler_valid,
    }
    failed = sorted(name for name, passed in exact_checks.items() if not passed)
    if failed:
        raise ArtifactSetError(
            "OGRE-Next build contract identity mismatch: " + ", ".join(failed)
        )
    return contract


def _is_positive_int(value: object) -> bool:
    return type(value) is int and value > 0


def _is_sha256(value: object) -> bool:
    return (
        isinstance(value, str)
        and re.fullmatch(r"[0-9a-f]{64}", value) is not None
    )


def _fnv1a64(payload: bytes) -> str:
    value = 14695981039346656037
    for byte in payload:
        value ^= byte
        value = (value * 1099511628211) & ((1 << 64) - 1)
    return f"{value:016x}"


def _expected_rt4_build_identity(
    build_contract: dict[str, object], report: dict[str, object]
) -> str:
    platform = build_contract["platform"]
    compiler = build_contract["compiler"]
    source = build_contract["ror_source"]
    ogre = build_contract["provenance"]
    provenance = report.get("provenance")
    if not all(
        isinstance(value, dict)
        for value in (platform, compiler, source, ogre, provenance)
    ):
        raise ArtifactSetError("RT4 executable build identity inputs are missing")
    return (
        f"{RT4_EXECUTABLE_IDENTITY_SCHEMA}"
        f"|platform={platform['policy']}"
        f"|compiler={compiler['id']}-{compiler['version']}-{compiler['build_type']}"
        f"|ror_commit={source['commit']}"
        f"|ror_manifest={source['relevant_manifest_sha256']}"
        f"|ogre_commit={ogre['commit']}"
        f"|ogre_archive={ogre['archive_sha256']}"
        "|shader_manifest="
        f"{provenance.get('shader_media_manifest_sha256')}"
    )


def _verify_mach_o_64(payload: bytes) -> dict[str, str]:
    if len(payload) < 32:
        raise ArtifactSetError("RT4 executable has a truncated Mach-O header")
    magic, cpu, _, file_type, command_count, command_bytes, _, _ = (
        struct.unpack_from("<IiiIIIII", payload, 0)
    )
    if (
        magic != 0xFEEDFACF
        or cpu != 0x0100000C
        or file_type != 2
        or command_count < 2
        or command_bytes < 96
        or 32 + command_bytes > len(payload)
    ):
        raise ArtifactSetError("RT4 executable is not an arm64 Mach-O executable")
    offset = 32
    has_executable_text = False
    has_entrypoint = False
    for _ in range(command_count):
        if offset + 8 > 32 + command_bytes:
            raise ArtifactSetError("RT4 Mach-O load commands are truncated")
        command, command_size = struct.unpack_from("<II", payload, offset)
        if command_size < 8 or offset + command_size > 32 + command_bytes:
            raise ArtifactSetError("RT4 Mach-O load command layout is invalid")
        if command == 0x19 and command_size >= 72:  # LC_SEGMENT_64
            values = struct.unpack_from("<II16sQQQQiiII", payload, offset)
            segment = values[2].split(b"\0", 1)[0]
            file_offset, file_size = values[5], values[6]
            initial_protection = values[8]
            if (
                segment == b"__TEXT"
                and initial_protection & 0x4
                and file_size > 0
                and file_offset + file_size <= len(payload)
            ):
                has_executable_text = True
        if command == 0x80000028 and command_size >= 24:  # LC_MAIN
            entry_offset = struct.unpack_from("<Q", payload, offset + 8)[0]
            if 0 < entry_offset < len(payload):
                has_entrypoint = True
        offset += command_size
    if offset != 32 + command_bytes or not has_executable_text or not has_entrypoint:
        raise ArtifactSetError("RT4 Mach-O executable structure is incomplete")
    return {"format": "mach-o-64", "architecture": "arm64"}


def _verify_pe32_plus(payload: bytes) -> dict[str, str]:
    if len(payload) < 0x100 or payload[:2] != b"MZ":
        raise ArtifactSetError("RT4 executable has an invalid PE DOS header")
    pe_offset = struct.unpack_from("<I", payload, 0x3C)[0]
    if pe_offset + 24 > len(payload) or payload[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise ArtifactSetError("RT4 executable has an invalid PE signature")
    machine, section_count, _, _, _, optional_size, characteristics = (
        struct.unpack_from("<HHIIIHH", payload, pe_offset + 4)
    )
    optional_offset = pe_offset + 24
    if (
        machine != 0x8664
        or not 1 <= section_count <= 96
        or optional_size < 112
        or optional_offset + optional_size > len(payload)
        or characteristics & 0x0002 == 0
        or characteristics & 0x2000 != 0
        or struct.unpack_from("<H", payload, optional_offset)[0] != 0x20B
        or struct.unpack_from("<I", payload, optional_offset + 16)[0] == 0
    ):
        raise ArtifactSetError("RT4 executable is not an x64 PE32+ executable")
    section_offset = optional_offset + optional_size
    has_executable_code = False
    for index in range(section_count):
        offset = section_offset + index * 40
        if offset + 40 > len(payload):
            raise ArtifactSetError("RT4 PE section table is truncated")
        raw_size, raw_offset = struct.unpack_from("<II", payload, offset + 16)
        flags = struct.unpack_from("<I", payload, offset + 36)[0]
        if (
            flags & 0x20
            and flags & 0x20000000
            and raw_size > 0
            and raw_offset + raw_size <= len(payload)
        ):
            has_executable_code = True
    if not has_executable_code:
        raise ArtifactSetError("RT4 PE executable has no executable code section")
    return {"format": "pe32+", "architecture": "x86_64"}


def _verify_elf64(payload: bytes) -> dict[str, str]:
    if (
        len(payload) < 64
        or payload[:4] != b"\x7fELF"
        or payload[4] != 2
        or payload[5] != 1
        or payload[6] != 1
    ):
        raise ArtifactSetError("RT4 executable has an invalid ELF64 header")
    file_type, machine = struct.unpack_from("<HH", payload, 16)
    entrypoint, program_offset = struct.unpack_from("<QQ", payload, 24)
    program_entry_size, program_count = struct.unpack_from("<HH", payload, 54)
    if (
        file_type not in (2, 3)
        or machine != 62
        or entrypoint == 0
        or program_entry_size < 56
        or program_count == 0
        or program_offset + program_entry_size * program_count > len(payload)
    ):
        raise ArtifactSetError("RT4 executable is not an x86_64 ELF executable")
    has_executable_load = False
    for index in range(program_count):
        offset = program_offset + index * program_entry_size
        segment_type, flags = struct.unpack_from("<II", payload, offset)
        file_offset = struct.unpack_from("<Q", payload, offset + 8)[0]
        file_size = struct.unpack_from("<Q", payload, offset + 32)[0]
        if (
            segment_type == 1
            and flags & 0x1
            and file_size > 0
            and file_offset + file_size <= len(payload)
        ):
            has_executable_load = True
    if not has_executable_load:
        raise ArtifactSetError("RT4 ELF executable has no executable load segment")
    return {"format": "elf64", "architecture": "x86_64"}


def _requires_posix_executable_permission(
    binary_format: str, host_os_name: str | None = None
) -> bool:
    """Return whether this host can meaningfully enforce Unix execute bits."""
    effective_os_name = os.name if host_os_name is None else host_os_name
    return effective_os_name == "posix" and binary_format in (
        "mach-o-64",
        "elf64",
    )


def _verify_rt4_executable(
    path: Path,
    build_contract: dict[str, object],
    report: dict[str, object],
) -> None:
    size = path.stat().st_size
    if size < 64 * 1024 or size > 512 * 1024 * 1024:
        raise ArtifactSetError("RT4 executable byte count is structurally implausible")
    try:
        payload = path.read_bytes()
    except OSError as error:
        raise ArtifactSetError(f"could not read RT4 executable: {error}") from error
    policy_name = build_contract["platform"]["policy"]
    policy = PLATFORM_CONTRACTS[policy_name]
    verifier = {
        "mach-o-64": _verify_mach_o_64,
        "pe32+": _verify_pe32_plus,
        "elf64": _verify_elf64,
    }[policy["binary_format"]]
    structure = verifier(payload)
    expected_structure = {
        "format": policy["binary_format"],
        "architecture": policy["binary_architecture"],
    }
    if structure != expected_structure:
        raise ArtifactSetError("RT4 executable platform structure mismatch")
    # Windows filesystems do not carry POSIX executable mode bits.  The PE
    # policy is already validated structurally above, and a Windows-hosted
    # unit test may intentionally exercise a synthetic Mach-O/ELF fixture.
    # Enforce Unix execute permission only on hosts where that metadata exists;
    # never reinterpret a missing Windows mode bit as evidence about the
    # packaged foreign binary.
    if _requires_posix_executable_permission(policy["binary_format"]) and (
        path.stat().st_mode & 0o111 == 0
    ):
        raise ArtifactSetError("RT4 packaged executable has no execute permission")
    identity = _expected_rt4_build_identity(build_contract, report)
    if payload.count(identity.encode()) != 1:
        raise ArtifactSetError(
            "RT4 executable build identity is missing or ambiguous"
        )
    required_tokens = (
        RT4_REPORT_SCHEMA,
        "--modern-pbr",
        policy["renderer_name"],
        '\"raster_feature_tier\": \"MODERN_PBR_RT4_V1\"',
        "linear_RGBA8_positive_Z_to_RG8_UNORM",
    )
    missing = [token for token in required_tokens if token.encode() not in payload]
    if missing:
        raise ArtifactSetError(
            "RT4 executable build identity is missing or ambiguous"
        )


def _changed_pixels(
    baseline: bytes, variant: bytes, bytes_per_pixel: int
) -> int:
    if (
        bytes_per_pixel <= 0
        or len(baseline) != len(variant)
        or len(baseline) % bytes_per_pixel != 0
    ):
        raise ArtifactSetError("RT4 isolation attachment layout is invalid")
    return sum(
        baseline[offset : offset + bytes_per_pixel]
        != variant[offset : offset + bytes_per_pixel]
        for offset in range(0, len(baseline), bytes_per_pixel)
    )


def _quantize_unit_float(value: float) -> int:
    return int(math.floor(max(0.0, min(1.0, value)) * 255.0 + 0.5))


def _attachment_metrics(payload: bytes, hdr: bool) -> dict[str, object]:
    bytes_per_pixel = 8 if hdr else 4
    if len(payload) == 0 or len(payload) % bytes_per_pixel != 0:
        raise ArtifactSetError("RT4 attachment byte layout is invalid")
    rgb = bytearray()
    colour_counts: dict[bytes, int] = {}
    minimum_luminance = math.inf
    maximum_luminance = -math.inf
    if hdr:
        pixels = struct.iter_unpack("<4e", payload)
    else:
        pixels = (
            tuple(payload[offset : offset + 4])
            for offset in range(0, len(payload), 4)
        )
    for channels in pixels:
        if hdr:
            if not all(math.isfinite(channel) for channel in channels):
                raise ArtifactSetError("RT4 HDR isolation contains non-finite data")
            if any(channel < 0.0 for channel in channels[:3]):
                raise ArtifactSetError("RT4 HDR isolation contains negative RGB energy")
            if not 0.99 <= channels[3] <= 1.01:
                raise ArtifactSetError("RT4 HDR isolation alpha is not opaque")
            linear = channels[:3]
            quantized = bytes(_quantize_unit_float(value) for value in linear)
        else:
            if channels[3] < 250:
                raise ArtifactSetError("RT4 SDR isolation alpha is not opaque")
            linear = tuple(value / 255.0 for value in channels[:3])
            quantized = bytes(channels[:3])
        rgb.extend(quantized)
        colour_counts[quantized] = colour_counts.get(quantized, 0) + 1
        luminance = (
            0.2126 * linear[0]
            + 0.7152 * linear[1]
            + 0.0722 * linear[2]
        )
        minimum_luminance = min(minimum_luminance, luminance)
        maximum_luminance = max(maximum_luminance, luminance)
    pixel_count = len(payload) // bytes_per_pixel
    return {
        "exact_attachment_fnv1a64": _fnv1a64(payload),
        "rgb8_fnv1a64": _fnv1a64(bytes(rgb)),
        "distinct_rgb8_values": len(colour_counts),
        "non_background_pixels": pixel_count - max(colour_counts.values()),
        "minimum_luminance": minimum_luminance,
        "maximum_luminance": maximum_luminance,
        "rgb": bytes(rgb),
    }


def _reported_metric_matches(reported: object, computed: float) -> bool:
    return (
        isinstance(reported, (int, float))
        and not isinstance(reported, bool)
        and math.isfinite(float(reported))
        and math.isclose(
            float(reported), computed, rel_tol=2.0e-7, abs_tol=2.0e-7
        )
    )


def _is_nonzero_u64_hex(value: object) -> bool:
    return (
        isinstance(value, str)
        and re.fullmatch(r"[0-9a-f]{16}", value) is not None
        and value != "0" * 16
    )


def _is_bounded_evidence_string(value: object) -> bool:
    return (
        isinstance(value, str)
        and 0 < len(value) <= 512
        and "\x00" not in value
    )


def _reflection_half_metrics(
    payload: bytes, label: str
) -> dict[str, int | float]:
    if not payload or len(payload) % 8 != 0:
        raise ArtifactSetError(f"RT4 {label} RGBA16F layout is invalid")
    finite_components = 0
    nonzero_rgb_components = 0
    max_absolute_rgb = 0.0
    for channels in struct.iter_unpack("<4e", payload):
        if not all(math.isfinite(channel) for channel in channels):
            raise ArtifactSetError(
                f"RT4 {label} reflection evidence contains non-finite data"
            )
        finite_components += 4
        for channel in channels[:3]:
            magnitude = abs(channel)
            if magnitude > 0.0:
                nonzero_rgb_components += 1
            max_absolute_rgb = max(max_absolute_rgb, magnitude)
    return {
        "finite_component_count": finite_components,
        "nonzero_rgb_component_count": nonzero_rgb_components,
        "distinct_texel_count": len(
            {payload[offset : offset + 8] for offset in range(0, len(payload), 8)}
        ),
        "max_absolute_rgb": max_absolute_rgb,
    }


def _verify_rt4_reflection_semantics(
    report: dict[str, object],
    reflection_path: Path,
    build_contract: dict[str, object],
) -> list[dict[str, object]]:
    try:
        evidence = reflection_path.read_bytes()
    except OSError as error:
        raise ArtifactSetError(
            f"could not read RT4 reflection evidence: {error}"
        ) from error
    if len(evidence) != RT4_REFLECTION_EVIDENCE_BYTES:
        raise ArtifactSetError(
            "RT4 reflection evidence is truncated or has trailing bytes"
        )
    reflection = report.get("reflection_probes")
    if not isinstance(reflection, dict):
        raise ArtifactSetError("RT4 reflection report is missing")
    _require_exact_keys(
        reflection,
        {
            "schema",
            "evidence_file",
            "evidence_bytes",
            "backend",
            "render_system",
            "device_name",
            "driver_version",
            "pixel_format",
            "byte_order",
            "row_padding_included",
            "subresource_order",
            "ui_included",
            "same_device_exact_replay",
            "capture",
            "runtime_audit",
            "raw",
            "filtered",
        },
        "RT4 reflection report",
    )
    platform = build_contract.get("platform")
    if not isinstance(platform, dict):
        raise ArtifactSetError("RT4 reflection platform contract is missing")
    policy_name = platform.get("policy")
    policy = PLATFORM_CONTRACTS.get(str(policy_name))
    expected_backend = RT4_REFLECTION_BACKENDS.get(str(policy_name))
    controls = {
        "schema": reflection.get("schema") == RT4_REFLECTION_SCHEMA,
        "file": reflection.get("evidence_file") == reflection_path.name,
        "bytes": _json_exact(
            reflection.get("evidence_bytes"), RT4_REFLECTION_EVIDENCE_BYTES
        ),
        "backend": expected_backend is not None
        and reflection.get("backend") == expected_backend,
        "render_system": policy is not None
        and reflection.get("render_system") == policy["renderer_name"]
        and reflection.get("render_system") == report.get("renderer"),
        "device": _is_bounded_evidence_string(reflection.get("device_name")),
        "driver": _is_bounded_evidence_string(
            reflection.get("driver_version")
        ),
        "format": reflection.get("pixel_format") == "RGBA16_FLOAT",
        "byte_order": reflection.get("byte_order") == "little_endian",
        "tight_rows": reflection.get("row_padding_included") is False,
        "order": reflection.get("subresource_order")
        == "raw_face_major_then_filtered_mip_major_face_major",
        "ui_free": reflection.get("ui_included") is False,
        "replay": reflection.get("same_device_exact_replay") is True,
    }
    failed_controls = sorted(
        name for name, passed in controls.items() if not passed
    )
    if failed_controls:
        raise ArtifactSetError(
            "RT4 reflection controls failed: " + ", ".join(failed_controls)
        )

    capture = reflection.get("capture")
    if not isinstance(capture, dict):
        raise ArtifactSetError("RT4 reflection capture lineage is missing")
    _require_exact_keys(
        capture,
        {
            "render_frame_id",
            "simulation_tick",
            "probe_id",
            "content_revision",
            "candidate_generation",
            "deterministic_seed",
            "resolution",
        },
        "RT4 reflection capture lineage",
    )
    capture_checks = {
        "frame": _json_exact(capture.get("render_frame_id"), 1),
        "tick": _json_exact(capture.get("simulation_tick"), 1),
        "probe": _json_exact(capture.get("probe_id"), 1),
        "revision": _json_exact(capture.get("content_revision"), 1),
        "generation": _json_exact(capture.get("candidate_generation"), 1),
        "seed": _is_nonzero_u64_hex(capture.get("deterministic_seed")),
        "resolution": _json_exact(
            capture.get("resolution"), RT4_REFLECTION_RESOLUTION
        ),
    }
    failed_capture = sorted(
        name for name, passed in capture_checks.items() if not passed
    )
    if failed_capture:
        raise ArtifactSetError(
            "RT4 reflection capture lineage failed: "
            + ", ".join(failed_capture)
        )

    runtime = reflection.get("runtime_audit")
    if not isinstance(runtime, dict):
        raise ArtifactSetError("RT4 reflection runtime audit is missing")
    _require_exact_keys(
        runtime,
        {
            "version",
            "successful_capture_count",
            "failed_capture_count",
            "live_probe_count",
            "blend_resolution",
            "blend_texture_ready",
            "committed_state_digest",
            "native_execution_evidence",
            "capture_digest",
            "canonical_filtered_payload_bytes",
            "completed_face_count",
            "completed_mip_count",
            "ui_free_capture",
            "reserved_render_queue_excluded",
        },
        "RT4 reflection runtime audit",
    )
    runtime_checks = {
        "version": _json_exact(runtime.get("version"), 2),
        "success": _json_exact(runtime.get("successful_capture_count"), 1),
        "failure": _json_exact(runtime.get("failed_capture_count"), 0),
        "live": _json_exact(runtime.get("live_probe_count"), 1),
        "blend_resolution": _json_exact(runtime.get("blend_resolution"), 2048),
        "blend_ready": runtime.get("blend_texture_ready") is True,
        "state_digest": _is_nonzero_u64_hex(
            runtime.get("committed_state_digest")
        ),
        "native_evidence": _is_nonzero_u64_hex(
            runtime.get("native_execution_evidence")
        ),
        "capture_digest": _is_nonzero_u64_hex(runtime.get("capture_digest")),
        "payload": _json_exact(
            runtime.get("canonical_filtered_payload_bytes"),
            RT4_REFLECTION_FILTERED_BYTES,
        ),
        "faces": _json_exact(
            runtime.get("completed_face_count"), RT4_REFLECTION_FACE_COUNT
        ),
        "mips": _json_exact(
            runtime.get("completed_mip_count"),
            len(RT4_REFLECTION_FILTERED_DIMENSIONS),
        ),
        "ui_free": runtime.get("ui_free_capture") is True,
        "queue_excluded": runtime.get("reserved_render_queue_excluded") is True,
    }
    failed_runtime = sorted(
        name for name, passed in runtime_checks.items() if not passed
    )
    if failed_runtime:
        raise ArtifactSetError(
            "RT4 reflection runtime audit failed: "
            + ", ".join(failed_runtime)
        )

    for name, offset, byte_count, dimensions in (
        (
            "raw",
            0,
            RT4_REFLECTION_RAW_BYTES,
            (RT4_REFLECTION_RESOLUTION,),
        ),
        (
            "filtered",
            RT4_REFLECTION_RAW_BYTES,
            RT4_REFLECTION_FILTERED_BYTES,
            RT4_REFLECTION_FILTERED_DIMENSIONS,
        ),
    ):
        section = reflection.get(name)
        if not isinstance(section, dict):
            raise ArtifactSetError(f"RT4 reflection {name} report is missing")
        _require_exact_keys(
            section,
            {
                "offset",
                "bytes",
                "face_count",
                "mip_dimensions",
                "exact_fnv1a64",
                "finite_component_count",
                "nonzero_rgb_component_count",
                "distinct_texel_count",
                "max_absolute_rgb",
            },
            f"RT4 reflection {name} report",
        )
        payload = evidence[offset : offset + byte_count]
        metrics = _reflection_half_metrics(payload, name)
        checks = {
            "offset": _json_exact(section.get("offset"), offset),
            "bytes": _json_exact(section.get("bytes"), byte_count),
            "faces": _json_exact(
                section.get("face_count"), RT4_REFLECTION_FACE_COUNT
            ),
            "dimensions": _json_exact(
                section.get("mip_dimensions"), list(dimensions)
            ),
            "hash": section.get("exact_fnv1a64") == _fnv1a64(payload),
            "finite": _json_exact(
                section.get("finite_component_count"),
                metrics["finite_component_count"],
            ),
            "nonzero": _json_exact(
                section.get("nonzero_rgb_component_count"),
                metrics["nonzero_rgb_component_count"],
            )
            and int(metrics["nonzero_rgb_component_count"]) > 0,
            "distinct": _json_exact(
                section.get("distinct_texel_count"),
                metrics["distinct_texel_count"],
            )
            and int(metrics["distinct_texel_count"]) >= 2,
            "maximum": _reported_metric_matches(
                section.get("max_absolute_rgb"),
                float(metrics["max_absolute_rgb"]),
            )
            and float(metrics["max_absolute_rgb"]) > 0.0,
        }
        failed = sorted(key for key, passed in checks.items() if not passed)
        if failed:
            raise ArtifactSetError(
                f"RT4 reflection {name} evidence failed: "
                + ", ".join(failed)
            )

    offset = 0
    reflection_slices: list[dict[str, object]] = []
    for texture, dimensions in (
        ("raw", (RT4_REFLECTION_RESOLUTION,)),
        ("filtered", RT4_REFLECTION_FILTERED_DIMENSIONS),
    ):
        for mip, dimension in enumerate(dimensions):
            slice_bytes = dimension * dimension * 8
            for face in range(RT4_REFLECTION_FACE_COUNT):
                payload = evidence[offset : offset + slice_bytes]
                _reflection_half_metrics(
                    payload, f"{texture} mip {mip} face {face}"
                )
                reflection_slices.append(
                    {
                        "texture": texture,
                        "mip": mip,
                        "face": face,
                        "offset": offset,
                        "bytes": slice_bytes,
                        "sha256": hashlib.sha256(payload).hexdigest(),
                    }
                )
                offset += slice_bytes
    filtered_mip_one_offset = RT4_REFLECTION_RAW_BYTES * 2
    filtered_mip_one = _reflection_half_metrics(
        evidence[filtered_mip_one_offset:], "filtered mip one"
    )
    if (
        offset != len(evidence)
        or len(reflection_slices) != 18
        or int(filtered_mip_one["nonzero_rgb_component_count"]) == 0
        or int(filtered_mip_one["distinct_texel_count"]) < 2
        or float(filtered_mip_one["max_absolute_rgb"]) <= 0.0
    ):
        raise ArtifactSetError("RT4 reflection subresource coverage is incomplete")
    return reflection_slices


def _verify_rt4_semantics(
    report: dict[str, object],
    ppm_path: Path,
    isolation_path: Path,
    build_contract: dict[str, object],
) -> list[dict[str, object]]:
    try:
        ppm = ppm_path.read_bytes()
        isolation_payload = isolation_path.read_bytes()
    except OSError as error:
        raise ArtifactSetError(f"could not read RT4 raster evidence: {error}") from error
    width = 192
    height = 128
    header = b"P6\n192 128\n255\n"
    if not ppm.startswith(header) or len(ppm) != len(header) + width * height * 3:
        raise ArtifactSetError("RT4 PPM is not the exact 192x128 RGB8 contract")
    ppm_pixels = ppm[len(header) :]
    colours = [
        ppm_pixels[offset : offset + 3]
        for offset in range(0, len(ppm_pixels), 3)
    ]
    colour_counts: dict[bytes, int] = {}
    for colour in colours:
        colour_counts[colour] = colour_counts.get(colour, 0) + 1
    ppm_non_background = len(colours) - max(colour_counts.values())

    _require_exact_keys(
        report,
        {
            "schema",
            "status",
            "executable_build_identity",
            "provenance",
            "platform_policy",
            "renderer",
            "adapter",
            "catalog",
            "texture_allocations",
            "texture_upload_rollback",
            "texture_retirement",
            "texture_isolation",
            "tangent_handedness",
            "reflection_probes",
            "hdr",
            "sdr",
            "lifecycle",
        },
        "RT4 report",
    )
    if report.get("schema") != RT4_REPORT_SCHEMA or report.get("status") != "pass":
        raise ArtifactSetError("RT4 report schema or status is invalid")
    if report.get("executable_build_identity") != _expected_rt4_build_identity(
        build_contract, report
    ):
        raise ArtifactSetError("RT4 report executable build identity mismatch")
    hdr = report.get("hdr")
    sdr = report.get("sdr")
    isolation = report.get("texture_isolation")
    if not isinstance(hdr, dict) or not isinstance(sdr, dict):
        raise ArtifactSetError("RT4 HDR/SDR report metrics are missing")
    if not isinstance(isolation, dict):
        raise ArtifactSetError("RT4 isolation report is missing")
    _require_exact_keys(
        hdr,
        {
            "format",
            "width",
            "height",
            "distinct_rgb8_values",
            "non_background_pixels",
            "minimum_luminance",
            "maximum_luminance",
            "exact_attachment_fnv1a64",
            "rgb8_fnv1a64",
        },
        "RT4 HDR metrics",
    )
    _require_exact_keys(
        sdr,
        {
            "format",
            "width",
            "height",
            "distinct_rgb8_values",
            "non_background_pixels",
            "minimum_luminance",
            "maximum_luminance",
            "exact_attachment_fnv1a64",
            "rgb8_fnv1a64",
        },
        "RT4 SDR metrics",
    )
    _require_exact_keys(
        isolation,
        {
            "schema",
            "evidence_file",
            "width",
            "height",
            "geometry_identical",
            "material_factors_constants_identical",
            "camera_identical",
            "lights_identical",
            "ui_included",
            "variants",
            "evidence_bytes",
        },
        "RT4 isolation report",
    )
    variants = isolation.get("variants")
    controls = {
        "schema": isolation.get("schema")
        == "ror.ogre_next_rt4_texture_isolation.v1",
        "file": isolation.get("evidence_file") == isolation_path.name,
        "extent": _json_exact(isolation.get("width"), width)
        and _json_exact(isolation.get("height"), height),
        "bytes": _is_positive_int(isolation.get("evidence_bytes"))
        and isolation["evidence_bytes"] <= len(isolation_payload),
        "geometry": isolation.get("geometry_identical") is True,
        "factors": isolation.get("material_factors_constants_identical") is True,
        "camera": isolation.get("camera_identical") is True,
        "lights": isolation.get("lights_identical") is True,
        "ui_free": isolation.get("ui_included") is False,
        "variant_count": isinstance(variants, list)
        and len(variants) == len(RT4_EXPECTED_VARIANTS),
    }
    failed_controls = sorted(
        name for name, passed in controls.items() if not passed
    )
    if failed_controls:
        raise ArtifactSetError(
            "RT4 isolation controls failed: " + ", ".join(failed_controls)
        )
    if not isinstance(variants, list):
        raise ArtifactSetError("RT4 isolation variants are invalid")

    offset = 0
    baseline: dict[str, bytes] = {}
    exact_hashes: dict[str, set[str]] = {"hdr": set(), "sdr": set()}
    slice_attestations: list[dict[str, object]] = []
    for index, (entry, expected) in enumerate(zip(variants, RT4_EXPECTED_VARIANTS)):
        if not isinstance(entry, dict):
            raise ArtifactSetError("RT4 isolation variant is not an object")
        _require_exact_keys(
            entry,
            {"name", "changed_input", "asset_sequence", "hdr", "sdr"},
            f"RT4 {expected[0]} isolation variant",
        )
        if (
            entry.get("name") != expected[0]
            or entry.get("changed_input") != expected[1]
            or not _json_exact(entry.get("asset_sequence"), index + 1)
        ):
            raise ArtifactSetError("RT4 isolation variant identity mismatch")
        for attachment, bytes_per_pixel in (("hdr", 8), ("sdr", 4)):
            reported = entry.get(attachment)
            expected_bytes = width * height * bytes_per_pixel
            if not isinstance(reported, dict):
                raise ArtifactSetError(
                    f"RT4 {expected[0]} {attachment} metadata is missing"
                )
            _require_exact_keys(
                reported,
                {
                    "offset",
                    "bytes",
                    "exact_fnv1a64",
                    "changed_pixels_from_baseline",
                },
                f"RT4 {expected[0]} {attachment} metadata",
            )
            if (
                not _json_exact(reported.get("offset"), offset)
                or not _json_exact(reported.get("bytes"), expected_bytes)
                or offset + expected_bytes > len(isolation_payload)
            ):
                raise ArtifactSetError(
                    f"RT4 {expected[0]} {attachment} slice layout mismatch"
                )
            payload = isolation_payload[offset : offset + expected_bytes]
            _attachment_metrics(payload, attachment == "hdr")
            fnv = _fnv1a64(payload)
            if reported.get("exact_fnv1a64") != fnv:
                raise ArtifactSetError(
                    f"RT4 {expected[0]} {attachment} exact hash mismatch"
                )
            exact_hashes[attachment].add(fnv)
            if index == 0:
                changed = 0
                baseline[attachment] = payload
            else:
                changed = _changed_pixels(
                    baseline[attachment], payload, bytes_per_pixel
                )
            if (
                not _json_exact(reported.get("changed_pixels_from_baseline"), changed)
                or (index != 0 and changed < 64)
            ):
                raise ArtifactSetError(
                    f"RT4 {expected[0]} {attachment} semantic delta mismatch"
                )
            slice_attestations.append(
                {
                    "variant": expected[0],
                    "attachment": attachment,
                    "offset": offset,
                    "bytes": expected_bytes,
                    "sha256": hashlib.sha256(payload).hexdigest(),
                }
            )
            offset += expected_bytes

    if not _json_exact(isolation.get("evidence_bytes"), offset):
        raise ArtifactSetError("RT4 texture-isolation byte extent drifted")
    if any(
        len(hashes) != len(RT4_EXPECTED_VARIANTS)
        for hashes in exact_hashes.values()
    ):
        raise ArtifactSetError("RT4 isolation variants are not all distinct")

    handedness = report.get("tangent_handedness")
    if not isinstance(handedness, dict):
        raise ArtifactSetError("RT4 tangent-handedness report is missing")
    _require_exact_keys(
        handedness,
        {
            "schema",
            "evidence_file",
            "evidence_offset",
            "evidence_bytes",
            "authored_tangent_format",
            "positive_tangent_w",
            "negative_tangent_w",
            "position_normal_tangent_xyz_uv0_identical",
            "material_camera_lights_identical",
            "ui_included",
            "positive",
            "negative",
            "hdr_changed_pixels",
            "sdr_changed_pixels",
        },
        "RT4 tangent-handedness report",
    )
    handedness_start = offset
    if (
        handedness.get("schema")
        != "ror.ogre_next_rt4_tangent_handedness.v1"
        or handedness.get("evidence_file") != isolation_path.name
        or not _json_exact(handedness.get("evidence_offset"), handedness_start)
        or handedness.get("authored_tangent_format") != "FLOAT4"
        or not _json_exact(handedness.get("positive_tangent_w"), 1)
        or not _json_exact(handedness.get("negative_tangent_w"), -1)
        or handedness.get("position_normal_tangent_xyz_uv0_identical") is not True
        or handedness.get("material_camera_lights_identical") is not True
        or handedness.get("ui_included") is not False
    ):
        raise ArtifactSetError("RT4 tangent-handedness controls failed")
    handedness_blocks: dict[str, dict[str, bytes]] = {
        "positive": {},
        "negative": {},
    }
    for sign in ("positive", "negative"):
        sign_report = handedness.get(sign)
        if not isinstance(sign_report, dict):
            raise ArtifactSetError(f"RT4 {sign} tangent evidence is missing")
        _require_exact_keys(
            sign_report, {"hdr", "sdr"}, f"RT4 {sign} tangent evidence"
        )
        for attachment, bytes_per_pixel in (("hdr", 8), ("sdr", 4)):
            reported = sign_report.get(attachment)
            expected_bytes = width * height * bytes_per_pixel
            if not isinstance(reported, dict):
                raise ArtifactSetError(
                    f"RT4 {sign} tangent {attachment} metadata is missing"
                )
            _require_exact_keys(
                reported,
                {"offset", "bytes", "exact_fnv1a64"},
                f"RT4 {sign} tangent {attachment} metadata",
            )
            if (
                not _json_exact(reported.get("offset"), offset)
                or not _json_exact(reported.get("bytes"), expected_bytes)
                or offset + expected_bytes > len(isolation_payload)
            ):
                raise ArtifactSetError(
                    f"RT4 {sign} tangent {attachment} slice layout mismatch"
                )
            payload = isolation_payload[offset : offset + expected_bytes]
            _attachment_metrics(payload, attachment == "hdr")
            if reported.get("exact_fnv1a64") != _fnv1a64(payload):
                raise ArtifactSetError(
                    f"RT4 {sign} tangent {attachment} exact hash mismatch"
                )
            handedness_blocks[sign][attachment] = payload
            slice_attestations.append(
                {
                    "variant": f"tangent_{sign}_w",
                    "attachment": attachment,
                    "offset": offset,
                    "bytes": expected_bytes,
                    "sha256": hashlib.sha256(payload).hexdigest(),
                }
            )
            offset += expected_bytes
    if (
        not _json_exact(
            handedness.get("evidence_bytes"), offset - handedness_start
        )
        or offset != len(isolation_payload)
    ):
        raise ArtifactSetError("RT4 tangent-handedness byte extent drifted")
    for attachment, bytes_per_pixel in (("hdr", 8), ("sdr", 4)):
        changed = _changed_pixels(
            handedness_blocks["positive"][attachment],
            handedness_blocks["negative"][attachment],
            bytes_per_pixel,
        )
        if (
            not _json_exact(
                handedness.get(f"{attachment}_changed_pixels"), changed
            )
            or changed < 64
        ):
            raise ArtifactSetError(
                f"RT4 tangent-w sign produced no exact {attachment.upper()} effect"
            )
    baseline_sdr_rgb = bytes(
        channel
        for pixel_offset in range(0, len(baseline["sdr"]), 4)
        for channel in baseline["sdr"][pixel_offset : pixel_offset + 3]
    )
    hdr_metrics = _attachment_metrics(baseline["hdr"], True)
    sdr_metrics = _attachment_metrics(baseline["sdr"], False)
    if hdr_metrics["rgb"] != bytes(
        _quantize_unit_float(channel)
        for channels in struct.iter_unpack("<4e", baseline["hdr"])
        for channel in channels[:3]
    ):
        raise ArtifactSetError("RT4 HDR RGB derivation is inconsistent")
    hdr_energy = hdr_metrics["maximum_luminance"]
    hdr_minimum = hdr_metrics["minimum_luminance"]
    sdr_maximum = sdr_metrics["maximum_luminance"]
    sdr_minimum = sdr_metrics["minimum_luminance"]
    report_checks = {
        "ppm_baseline": ppm_pixels == baseline_sdr_rgb,
        "retirement": _json_exact(
            report.get("texture_retirement"), RT4_EXPECTED_RETIREMENT
        ),
        "texture_allocations": _json_exact(
            report.get("texture_allocations"),
            RT4_EXPECTED_TEXTURE_ALLOCATIONS,
        ),
        "texture_upload_rollback": _json_exact(
            report.get("texture_upload_rollback"),
            RT4_EXPECTED_TEXTURE_UPLOAD_ROLLBACK,
        ),
        "lifecycle": _json_exact(
            report.get("lifecycle"), RT4_EXPECTED_LIFECYCLE
        ),
        "hdr_format": hdr.get("format") == "RGBA16_FLOAT",
        "hdr_extent": _json_exact(hdr.get("width"), width)
        and _json_exact(hdr.get("height"), height),
        "hdr_exact": all(
            _json_exact(hdr.get(field), hdr_metrics[field])
            for field in (
                "exact_attachment_fnv1a64",
                "rgb8_fnv1a64",
                "distinct_rgb8_values",
                "non_background_pixels",
            )
        ),
        "hdr_minimum": _reported_metric_matches(
            hdr.get("minimum_luminance"), hdr_minimum
        )
        and hdr_minimum >= 0.0,
        "hdr_maximum": _reported_metric_matches(
            hdr.get("maximum_luminance"), hdr_energy
        )
        and hdr_energy > 1.05,
        "hdr_geometry": hdr_metrics["distinct_rgb8_values"] >= 2
        and hdr_metrics["non_background_pixels"] >= 512,
        "sdr_format": sdr.get("format") == "RGBA8_SRGB",
        "sdr_extent": _json_exact(sdr.get("width"), width)
        and _json_exact(sdr.get("height"), height),
        "sdr_exact": all(
            _json_exact(sdr.get(field), sdr_metrics[field])
            for field in (
                "exact_attachment_fnv1a64",
                "rgb8_fnv1a64",
                "distinct_rgb8_values",
                "non_background_pixels",
            )
        ),
        "sdr_minimum": _reported_metric_matches(
            sdr.get("minimum_luminance"), sdr_minimum
        ),
        "sdr_maximum": _reported_metric_matches(
            sdr.get("maximum_luminance"), sdr_maximum
        ),
        "sdr_ppm_hash": sdr_metrics["rgb8_fnv1a64"] == _fnv1a64(ppm_pixels),
        "sdr_distinct": sdr_metrics["distinct_rgb8_values"]
        == len(colour_counts),
        "sdr_geometry": sdr_metrics["non_background_pixels"]
        == ppm_non_background
        and ppm_non_background >= 512
        and sdr_maximum - sdr_minimum > 0.05,
    }
    failed_report = sorted(
        name for name, passed in report_checks.items() if not passed
    )
    if failed_report:
        raise ArtifactSetError(
            "RT4 PPM/isolation report mismatch: " + ", ".join(failed_report)
        )
    return slice_attestations


def _verify_rt4(
    root: Path,
    manifest: list[dict[str, object]],
    build_contract: dict[str, object],
) -> None:
    if (
        not NORMAL_MAP_SOURCE_LOCK_PATH.is_file()
        or sha256_file(NORMAL_MAP_SOURCE_LOCK_PATH)
        != NORMAL_MAP_SOURCE_LOCK_SHA256
    ):
        raise ArtifactSetError("reviewed normal-map source lock is missing or changed")
    report_path = root / RT4_REPORT_ARTIFACT
    ppm_path = root / RT4_PPM_ARTIFACT
    isolation_path = root / RT4_ISOLATION_ARTIFACT
    reflection_path = root / RT4_REFLECTION_ARTIFACT
    attestation_path = root / RT4_ATTESTATION_ARTIFACT
    report = _read_json_object(report_path, "RT4 report")
    attestation = _read_json_object(attestation_path, "RT4 attestation")
    _require_exact_keys(
        attestation,
        {
            "schema",
            "status",
            "integrity_model",
            "source",
            "ogre_next",
            "shader_media",
            "files",
            "isolation_slices",
            "reflection_slices",
        },
        "RT4 attestation",
    )
    if (
        attestation.get("schema")
        != RT4_ATTESTATION_SCHEMA
        or attestation.get("status") != "pass"
        or attestation.get("integrity_model") != RT4_INTEGRITY_MODEL
    ):
        raise ArtifactSetError("RT4 attestation schema or status is invalid")

    platform_contract = build_contract["platform"]
    executable_suffix = (
        ".exe" if platform_contract.get("policy") == "windows-x64-d3d11" else ""
    )
    executable_relative = (
        "ror-ogre-next-n1-package/bin/"
        f"{RT4_PACKAGE_EXECUTABLE_STEM}{executable_suffix}"
    )
    executable_path = root / executable_relative
    if executable_path.is_symlink() or not executable_path.is_file():
        raise ArtifactSetError(f"missing: {executable_relative}")
    if executable_path.stat().st_size <= 0:
        raise ArtifactSetError(f"empty: {executable_relative}")
    _verify_rt4_executable(executable_path, build_contract, report)

    files = attestation.get("files")
    if not isinstance(files, dict) or set(files) != {
        "build_contract",
        "report",
        "ppm",
        "isolation",
        "reflection",
        "executable",
    }:
        raise ArtifactSetError("RT4 attested file set is invalid")
    for key, path, relative in (
        ("build_contract", root / REQUIRED_ARTIFACTS[0], REQUIRED_ARTIFACTS[0]),
        ("report", report_path, RT4_REPORT_ARTIFACT),
        ("ppm", ppm_path, RT4_PPM_ARTIFACT),
        ("isolation", isolation_path, RT4_ISOLATION_ARTIFACT),
        ("reflection", reflection_path, RT4_REFLECTION_ARTIFACT),
        ("executable", executable_path, executable_relative),
    ):
        _verify_attested_file(files.get(key), path, relative, "RT4", key)

    ror_source = build_contract["ror_source"]
    ogre_source = build_contract["provenance"]
    shader_contract = build_contract["shader_media"]
    notice = shader_contract["third_party_notice"]
    provenance = report.get("provenance")
    if not isinstance(provenance, dict):
        raise ArtifactSetError("RT4 report provenance is missing")
    expected_source = {
        "repository": ror_source.get("repository"),
        "ref": ror_source.get("ref"),
        "commit": ror_source.get("commit"),
        "relevant_manifest_sha256": ror_source.get("relevant_manifest_sha256"),
        "relevant_manifest_file_count": ror_source.get(
            "relevant_manifest_file_count"
        ),
    }
    expected_ogre = {
        key: ogre_source.get(key)
        for key in (
            "repository",
            "branch",
            "commit",
            "archive_sha256",
            "license_spdx",
            "license_sha256",
        )
    }
    expected_ogre["normal_map_source_lock_sha256"] = (
        NORMAL_MAP_SOURCE_LOCK_SHA256
    )
    expected_shader = {
        "root": shader_contract.get("root"),
        "license_expression": shader_contract.get("license_expression"),
        "source_path": notice.get("source_path"),
        "source_sha256": notice.get("source_sha256"),
        "notice_path": notice.get("notice_path"),
        "notice_sha256": notice.get("notice_sha256"),
        "manifest_sha256": provenance.get("shader_media_manifest_sha256"),
        "manifest_file_count": provenance.get("shader_media_manifest_file_count"),
    }
    expected_provenance = {
        "ror_repository": expected_source["repository"],
        "ror_ref": expected_source["ref"],
        "ror_commit": expected_source["commit"],
        "ror_relevant_source_manifest_sha256": expected_source[
            "relevant_manifest_sha256"
        ],
        "ror_relevant_source_manifest_file_count": expected_source[
            "relevant_manifest_file_count"
        ],
        "ogre_next_commit": expected_ogre["commit"],
        "ogre_next_archive_sha256": expected_ogre["archive_sha256"],
        "normal_map_source_lock_sha256": NORMAL_MAP_SOURCE_LOCK_SHA256,
        "shader_media_root": expected_shader["root"],
        "shader_media_license_expression": expected_shader[
            "license_expression"
        ],
        "shader_media_notice_path": expected_shader["notice_path"],
        "shader_media_notice_sha256": expected_shader["notice_sha256"],
        "shader_media_manifest_sha256": expected_shader["manifest_sha256"],
        "shader_media_manifest_file_count": expected_shader[
            "manifest_file_count"
        ],
    }
    if not _is_sha256(expected_shader["manifest_sha256"]) or not _is_positive_int(
        expected_shader["manifest_file_count"]
    ):
        raise ArtifactSetError("RT4 shader-media manifest identity is invalid")
    if not _json_exact(attestation.get("source"), expected_source):
        raise ArtifactSetError("RT4 source attestation mismatch")
    if not _json_exact(attestation.get("ogre_next"), expected_ogre):
        raise ArtifactSetError("RT4 Ogre attestation mismatch")
    if not _json_exact(attestation.get("shader_media"), expected_shader):
        raise ArtifactSetError("RT4 shader-media attestation mismatch")
    if not _json_exact(provenance, expected_provenance):
        raise ArtifactSetError("RT4 build-contract provenance mismatch")

    policy = PLATFORM_CONTRACTS[platform_contract["policy"]]
    expected_adapter = {
        "frontend_version": "n1-ogre-3.0-" + expected_ogre["commit"],
        "native_mesh_path": "Ogre v2 Mesh plus immutable VertexArrayObject",
        "material_path": "HLMS PBS metallic-roughness",
        "brdf": "PbsBrdf::Default height-correlated GGX",
        "pbr_datablock_readback_verified": True,
        "raster_feature_tier": "MODERN_PBR_RT4_V1",
        "vertex_layout": "position_normal_tangent_uv0",
        "base_color_upload": "RGBA8_UNORM_SRGB",
        "metallic_roughness_upload": (
            "linear_G_to_R8_roughness_B_to_R8_metallic"
        ),
        "emissive_upload": "RGBA8_UNORM_SRGB",
        "normal_upload": "linear_RGBA8_positive_Z_to_RG8_UNORM",
        "padded_source_rows_verified": True,
        "portable_sampler_mapping_verified": True,
        "normal_texture_admitted": True,
        "normal_slot": "PBSM_NORMAL",
        "normal_uv_source": 0,
        "normal_scale": 1,
        "normal_map_weight": 1,
        "normal_positive_z_tolerance_decoded": "1/255",
        "occlusion_texture_admitted": False,
        "occlusion_blocker": "pinned_HLMS_PBS_has_no_ambient_only_AO_slot",
        "runtime_media_root": "explicit_absolute",
        "package_media_relative_path": (
            "share/rigsofrods/ogre-next/Samples/Media"
        ),
        "relocated_executable": True,
        "compositor2": True,
        "ui_included": False,
        "cpu_readback_completed": True,
        "analytic_lights_calibrated": True,
        "directional_lux_to_native_power_scale": 1.0 / 1024.0,
        "maximum_directional_lights": 1,
        "constant_environment_only": False,
        "native_interop": False,
        "ray_tracing": False,
    }
    expected_catalog = {
        "registry_id": 0x4E315F534D4F4B45,
        "sequence": 7,
        "baseline_sequence": 1,
        "live_replacement_count": 6,
        "referenced_texture_count": 4,
        "referenced_sampler_count": 1,
        "unreferenced_assets_not_uploaded": True,
        "transactional_replay_after_restart": True,
    }
    report_contract_checks = {
        "platform": report.get("platform_policy") == platform_contract["policy"],
        "renderer": report.get("renderer") == policy["renderer_name"],
        "adapter": _json_exact(report.get("adapter"), expected_adapter),
        "catalog": _json_exact(report.get("catalog"), expected_catalog),
    }
    failed_report_contract = sorted(
        name for name, passed in report_contract_checks.items() if not passed
    )
    if failed_report_contract:
        raise ArtifactSetError(
            "RT4 report contract mismatch: " + ", ".join(failed_report_contract)
        )

    computed_slices = _verify_rt4_semantics(
        report, ppm_path, isolation_path, build_contract
    )
    if not _json_exact(attestation.get("isolation_slices"), computed_slices):
        raise ArtifactSetError("RT4 SHA-256 slice attestation mismatch")
    reflection_slices = _verify_rt4_reflection_semantics(
        report, reflection_path, build_contract
    )
    if not _json_exact(
        attestation.get("reflection_slices"), reflection_slices
    ):
        raise ArtifactSetError("RT4 reflection SHA-256 slice attestation mismatch")
    manifest.append(
        {
            "path": executable_relative,
            "bytes": executable_path.stat().st_size,
            "sha256": sha256_file(executable_path),
        }
    )


def _number_matches(value: object, expected: float) -> bool:
    return (
        type(value) in (int, float)
        and math.isfinite(float(value))
        and math.isclose(float(value), expected, rel_tol=1.0e-7, abs_tol=1.0e-7)
    )


def _expected_pssm_provenance(
    build_contract: dict[str, object], report: dict[str, object]
) -> dict[str, object]:
    source = build_contract["ror_source"]
    ogre = build_contract["provenance"]
    provenance = report.get("provenance")
    if not all(isinstance(value, dict) for value in (source, ogre, provenance)):
        raise ArtifactSetError("PSSM provenance inputs are missing")
    return {
        "ror_repository": source.get("repository"),
        "ror_ref": source.get("ref"),
        "ror_commit": source.get("commit"),
        "ror_relevant_source_manifest_sha256": source.get(
            "relevant_manifest_sha256"
        ),
        "ogre_next_commit": ogre.get("commit"),
        "ogre_next_archive_sha256": ogre.get("archive_sha256"),
        "shader_media_manifest_sha256": provenance.get(
            "shader_media_manifest_sha256"
        ),
        "executable_build_identity": _expected_rt4_build_identity(
            build_contract, report
        ),
    }


def _verify_pssm_executable(
    path: Path,
    build_contract: dict[str, object],
    report: dict[str, object],
) -> None:
    size = path.stat().st_size
    if size < 64 * 1024 or size > 512 * 1024 * 1024:
        raise ArtifactSetError("PSSM executable byte count is structurally implausible")
    try:
        payload = path.read_bytes()
    except OSError as error:
        raise ArtifactSetError(f"could not read PSSM executable: {error}") from error
    policy_name = build_contract["platform"]["policy"]
    policy = PLATFORM_CONTRACTS[policy_name]
    verifier = {
        "mach-o-64": _verify_mach_o_64,
        "pe32+": _verify_pe32_plus,
        "elf64": _verify_elf64,
    }[policy["binary_format"]]
    expected_structure = {
        "format": policy["binary_format"],
        "architecture": policy["binary_architecture"],
    }
    if verifier(payload) != expected_structure:
        raise ArtifactSetError("PSSM executable platform structure mismatch")
    if policy["binary_format"] in ("mach-o-64", "elf64") and (
        path.stat().st_mode & 0o111 == 0
    ):
        raise ArtifactSetError("PSSM packaged executable has no execute permission")
    identity = _expected_rt4_build_identity(build_contract, report)
    if payload.count(identity.encode()) != 1:
        raise ArtifactSetError("PSSM executable build identity is missing or ambiguous")
    required_tokens = (
        PSSM_REPORT_SCHEMA,
        "--media-root",
        "PSSM_3_CASCADE_V1",
        PSSM_UNSUPPORTED_DETAIL,
        policy["renderer_name"],
    )
    missing = [token for token in required_tokens if token.encode() not in payload]
    if missing:
        raise ArtifactSetError("PSSM executable contract strings are incomplete")


def _pssm_pair_metrics(
    baseline: bytes,
    shadowed: bytes,
    *,
    hdr: bool,
    receiver: tuple[int, int, int, int],
    occluder: tuple[int, int, int, int],
) -> tuple[int, int]:
    width = 192
    height = 128
    bytes_per_pixel = 8 if hdr else 4
    expected_bytes = width * height * bytes_per_pixel
    if len(baseline) != expected_bytes or len(shadowed) != expected_bytes:
        raise ArtifactSetError("PSSM evidence pair extent is invalid")
    changed = 0
    darkened = 0
    for pixel_index in range(width * height):
        offset = pixel_index * bytes_per_pixel
        left = baseline[offset : offset + bytes_per_pixel]
        right = shadowed[offset : offset + bytes_per_pixel]
        if hdr:
            left_channels = struct.unpack("<4e", left)
            right_channels = struct.unpack("<4e", right)
            if not all(
                math.isfinite(channel)
                for channel in left_channels + right_channels
            ):
                raise ArtifactSetError("PSSM HDR evidence contains non-finite data")
            left_luminance = (
                0.2126 * left_channels[0]
                + 0.7152 * left_channels[1]
                + 0.0722 * left_channels[2]
            )
            right_luminance = (
                0.2126 * right_channels[0]
                + 0.7152 * right_channels[1]
                + 0.0722 * right_channels[2]
            )
        else:
            left_luminance = 0.2126 * left[0] + 0.7152 * left[1] + 0.0722 * left[2]
            right_luminance = (
                0.2126 * right[0] + 0.7152 * right[1] + 0.0722 * right[2]
            )
        if left == right:
            continue
        changed += 1
        x = pixel_index % width
        y = pixel_index // width
        in_receiver = receiver[0] <= x <= receiver[1] and receiver[2] <= y <= receiver[3]
        in_occluder = occluder[0] <= x <= occluder[1] and occluder[2] <= y <= occluder[3]
        if not in_receiver or in_occluder:
            raise ArtifactSetError("PSSM visual change escaped its reviewed receiver region")
        if right_luminance < left_luminance:
            darkened += 1
    if changed < 16 or darkened * 10 < changed * 9:
        raise ArtifactSetError("PSSM evidence has no isolated receiver-local shadow")
    return changed, darkened


def _verify_pssm_pass(
    root: Path,
    report: dict[str, object],
    manifest: list[dict[str, object]],
) -> None:
    _require_exact_keys(
        report,
        {
            "schema",
            "status",
            "provenance",
            "platform_policy",
            "renderer",
            "shadow_contract",
            "isolation",
            "distant_cascade_proof",
            "lifecycle",
            "evidence",
        },
        "PSSM pass report",
    )
    shadow_contract = _require_exact_keys(
        report.get("shadow_contract"),
        {
            "version",
            "mode",
            "cascade_count",
            "split_points_m",
            "blend_points_m",
            "fade_point_m",
            "atlas",
            "filter",
            "programmatic_compositor2",
            "ui_included",
            "backend_substitution",
            "split_stable_tangent_projection",
            "native_definition_split_and_runtime_bias_readback",
            "runtime_normal_offset_bias",
        },
        "PSSM shadow contract",
    )
    expected_splits = (0.5, 7.81633186, 45.2411156, 350.0)
    expected_blends = (6.90179062, 40.5630188)
    splits = shadow_contract.get("split_points_m")
    blends = shadow_contract.get("blend_points_m")
    biases = shadow_contract.get("runtime_normal_offset_bias")
    contract_checks = {
        "version": _json_exact(shadow_contract.get("version"), 1),
        "mode": shadow_contract.get("mode") == "PSSM_3_CASCADE_V1",
        "cascade_count": _json_exact(shadow_contract.get("cascade_count"), 3),
        "splits": isinstance(splits, list)
        and len(splits) == 4
        and all(_number_matches(value, expected) for value, expected in zip(splits, expected_splits)),
        "blends": isinstance(blends, list)
        and len(blends) == 2
        and all(_number_matches(value, expected) for value, expected in zip(blends, expected_blends)),
        "fade": _number_matches(shadow_contract.get("fade_point_m"), 254.610474),
        "atlas": _json_exact(
            shadow_contract.get("atlas"),
            {"format": "D32_FLOAT", "width": 2048, "height": 3072},
        ),
        "filter": shadow_contract.get("filter") == "PCF_4x4",
        "compositor": shadow_contract.get("programmatic_compositor2") is True,
        "ui_free": shadow_contract.get("ui_included") is False,
        "native": shadow_contract.get("backend_substitution") is False
        and shadow_contract.get("split_stable_tangent_projection") is True
        and shadow_contract.get(
            "native_definition_split_and_runtime_bias_readback"
        )
        is True,
        "biases": isinstance(biases, list)
        and len(biases) == 3
        and all(
            type(value) in (int, float)
            and math.isfinite(float(value))
            and float(value) >= 168.0
            for value in biases
        ),
    }
    failed_contract = sorted(
        name for name, passed in contract_checks.items() if not passed
    )
    if failed_contract:
        raise ArtifactSetError(
            "PSSM shadow contract mismatch: " + ", ".join(failed_contract)
        )

    isolation = _require_exact_keys(
        report.get("isolation"),
        {
            "controlled_visual_change",
            "nonvisual_snapshot_identity_changed",
            "changed_pixels_outside_reviewed_receiver_region",
            "changed_pixels_inside_reviewed_occluder_region",
            "hdr_changed_receiver_pixels",
            "hdr_darkened_receiver_pixels",
            "sdr_changed_receiver_pixels",
            "sdr_darkened_receiver_pixels",
            "normalized_visibility_mask_0x1_verified",
            "shadow_disabled_default_equals_explicit",
            "shadow_disabled_exact_fnv1a64",
        },
        "PSSM isolation",
    )
    isolation_controls = {
        "controlled_change": isolation.get("controlled_visual_change")
        == "occluder_instance_casts_shadow",
        "snapshot_identity_disclosed": isolation.get(
            "nonvisual_snapshot_identity_changed"
        )
        is True,
        "receiver_only": _json_exact(
            isolation.get("changed_pixels_outside_reviewed_receiver_region"), 0
        )
        and _json_exact(
            isolation.get("changed_pixels_inside_reviewed_occluder_region"), 0
        ),
        "mask": isolation.get("normalized_visibility_mask_0x1_verified") is True,
        "disabled": isolation.get("shadow_disabled_default_equals_explicit")
        is True,
        "disabled_hash": isinstance(
            isolation.get("shadow_disabled_exact_fnv1a64"), str
        )
        and re.fullmatch(
            r"[0-9a-f]{16}", isolation["shadow_disabled_exact_fnv1a64"]
        )
        is not None,
    }
    failed_isolation = sorted(
        name for name, passed in isolation_controls.items() if not passed
    )
    if failed_isolation:
        raise ArtifactSetError(
            "PSSM isolation controls failed: " + ", ".join(failed_isolation)
        )

    lifecycle = _require_exact_keys(
        report.get("lifecycle"),
        {
            "shadow_frames_completed",
            "shadow_node_creates",
            "shadow_node_destroys",
            "workspace_node_definition_creates",
            "workspace_node_definition_destroys",
            "receiver_datablock_creates",
            "receiver_datablock_destroys",
            "receiver_clone_same_frame_retry_verified",
            "workspace_node_same_frame_retry_verified",
        },
        "PSSM lifecycle",
    )
    if not _json_exact(
        lifecycle,
        {
            "shadow_frames_completed": 8,
            "shadow_node_creates": 8,
            "shadow_node_destroys": 8,
            "workspace_node_definition_creates": 8,
            "workspace_node_definition_destroys": 8,
            "receiver_datablock_creates": 8,
            "receiver_datablock_destroys": 8,
            "receiver_clone_same_frame_retry_verified": True,
            "workspace_node_same_frame_retry_verified": True,
        },
    ):
        raise ArtifactSetError("PSSM lifecycle and transactional retry proof is invalid")

    evidence = _require_exact_keys(
        report.get("evidence"),
        {
            "file",
            "bytes",
            "hdr_no_occluder_fnv1a64",
            "hdr_occluder_fnv1a64",
            "sdr_no_occluder_fnv1a64",
            "sdr_occluder_fnv1a64",
            "cascade_2_sdr_no_occluder_fnv1a64",
            "cascade_2_sdr_occluder_fnv1a64",
            "cascade_3_sdr_no_occluder_fnv1a64",
            "cascade_3_sdr_occluder_fnv1a64",
        },
        "PSSM evidence metadata",
    )
    evidence_path = root / PSSM_EVIDENCE_ARTIFACT
    if evidence_path.is_symlink() or not evidence_path.is_file():
        raise ArtifactSetError(f"missing: {PSSM_EVIDENCE_ARTIFACT}")
    try:
        payload = evidence_path.read_bytes()
    except OSError as error:
        raise ArtifactSetError(f"could not read PSSM evidence: {error}") from error
    if evidence.get("file") != PSSM_EVIDENCE_ARTIFACT or not _json_exact(
        evidence.get("bytes"), len(payload)
    ):
        raise ArtifactSetError("PSSM evidence identity or byte count is invalid")
    segment_specs = (
        ("hdr_no_occluder_fnv1a64", 192 * 128 * 8),
        ("hdr_occluder_fnv1a64", 192 * 128 * 8),
        ("sdr_no_occluder_fnv1a64", 192 * 128 * 4),
        ("sdr_occluder_fnv1a64", 192 * 128 * 4),
        ("cascade_2_sdr_no_occluder_fnv1a64", 192 * 128 * 4),
        ("cascade_2_sdr_occluder_fnv1a64", 192 * 128 * 4),
        ("cascade_3_sdr_no_occluder_fnv1a64", 192 * 128 * 4),
        ("cascade_3_sdr_occluder_fnv1a64", 192 * 128 * 4),
    )
    slices: list[bytes] = []
    offset = 0
    for field, size in segment_specs:
        segment = payload[offset : offset + size]
        if len(segment) != size or evidence.get(field) != _fnv1a64(segment):
            raise ArtifactSetError(f"PSSM evidence slice mismatch: {field}")
        slices.append(segment)
        offset += size
    if offset != len(payload):
        raise ArtifactSetError("PSSM evidence has trailing bytes")
    near_region = (34, 158, 18, 110)
    near_occluder = (80, 111, 48, 79)
    off_axis_region = (68, 191, 18, 110)
    off_axis_occluder = (192, 192, 128, 128)
    hdr_metrics = _pssm_pair_metrics(
        slices[0], slices[1], hdr=True, receiver=near_region, occluder=near_occluder
    )
    sdr_metrics = _pssm_pair_metrics(
        slices[2], slices[3], hdr=False, receiver=near_region, occluder=near_occluder
    )
    reported_near = (
        isolation.get("hdr_changed_receiver_pixels"),
        isolation.get("hdr_darkened_receiver_pixels"),
        isolation.get("sdr_changed_receiver_pixels"),
        isolation.get("sdr_darkened_receiver_pixels"),
    )
    if not all(
        _json_exact(reported, computed)
        for reported, computed in zip(reported_near, hdr_metrics + sdr_metrics)
    ):
        raise ArtifactSetError("PSSM near-cascade report differs from evidence")

    distant = report.get("distant_cascade_proof")
    if not isinstance(distant, list) or len(distant) != 2:
        raise ArtifactSetError("PSSM distant-cascade proof is missing")
    expected_distant = ((1, 20.0, 12.5), (2, 100.0, 62.5))
    for index, (entry, expected) in enumerate(zip(distant, expected_distant)):
        entry = _require_exact_keys(
            entry,
            {
                "cascade_index",
                "receiver_depth_m",
                "occluder_depth_m",
                "off_axis",
                "sdr_changed_receiver_pixels",
                "sdr_darkened_receiver_pixels",
            },
            f"PSSM distant cascade {index + 2}",
        )
        computed = _pssm_pair_metrics(
            slices[4 + index * 2],
            slices[5 + index * 2],
            hdr=False,
            receiver=off_axis_region,
            occluder=off_axis_occluder,
        )
        if not (
            _json_exact(entry.get("cascade_index"), expected[0])
            and _number_matches(entry.get("receiver_depth_m"), expected[1])
            and _number_matches(entry.get("occluder_depth_m"), expected[2])
            and entry.get("off_axis") is True
            and _json_exact(entry.get("sdr_changed_receiver_pixels"), computed[0])
            and _json_exact(entry.get("sdr_darkened_receiver_pixels"), computed[1])
        ):
            raise ArtifactSetError("PSSM distant-cascade report differs from evidence")
    manifest.append(
        {
            "path": PSSM_EVIDENCE_ARTIFACT,
            "bytes": evidence_path.stat().st_size,
            "sha256": sha256_file(evidence_path),
        }
    )


def _verify_pssm(
    root: Path,
    manifest: list[dict[str, object]],
    build_contract: dict[str, object],
) -> None:
    report_path = root / PSSM_REPORT_ARTIFACT
    report = _read_json_object(report_path, "PSSM report")
    if report.get("schema") != PSSM_REPORT_SCHEMA:
        raise ArtifactSetError("PSSM report schema is invalid")
    provenance = _require_exact_keys(
        report.get("provenance"),
        {
            "ror_repository",
            "ror_ref",
            "ror_commit",
            "ror_relevant_source_manifest_sha256",
            "ogre_next_commit",
            "ogre_next_archive_sha256",
            "shader_media_manifest_sha256",
            "executable_build_identity",
        },
        "PSSM provenance",
    )
    if not _is_sha256(provenance.get("shader_media_manifest_sha256")):
        raise ArtifactSetError("PSSM shader-media manifest identity is invalid")
    if not _json_exact(
        provenance, _expected_pssm_provenance(build_contract, report)
    ):
        raise ArtifactSetError("PSSM report provenance mismatch")
    platform_name = build_contract["platform"]["policy"]
    policy = PLATFORM_CONTRACTS[platform_name]
    if (
        report.get("platform_policy") != platform_name
        or report.get("renderer") != policy["renderer_name"]
    ):
        raise ArtifactSetError("PSSM platform or renderer identity mismatch")
    executable_suffix = ".exe" if platform_name == "windows-x64-d3d11" else ""
    executable_relative = f"bin/{PSSM_EXECUTABLE_STEM}{executable_suffix}"
    executable_path = root / executable_relative
    if executable_path.is_symlink() or not executable_path.is_file():
        raise ArtifactSetError(f"missing: {executable_relative}")
    _verify_pssm_executable(executable_path, build_contract, report)
    manifest.append(
        {
            "path": executable_relative,
            "bytes": executable_path.stat().st_size,
            "sha256": sha256_file(executable_path),
        }
    )
    status = report.get("status")
    if status == "pass":
        _verify_pssm_pass(root, report, manifest)
        return
    if status != "unsupported":
        raise ArtifactSetError("PSSM report did not pass or fail closed")
    _require_exact_keys(
        report,
        {
            "schema",
            "status",
            "provenance",
            "platform_policy",
            "renderer",
            "capability_evidence",
            "backend_substitution",
        },
        "PSSM unsupported report",
    )
    capability = _require_exact_keys(
        report.get("capability_evidence"),
        {
            "code",
            "reason",
            "required_atlas_width",
            "required_atlas_height",
            "required_format",
            "required_filter",
            "observed_maximum_texture_dimension",
            "atlas_dimensions_supported",
            "texture_gather_supported",
            "d32_render_target_supported",
        },
        "PSSM unsupported capability evidence",
    )
    maximum = capability.get("observed_maximum_texture_dimension")
    booleans = (
        capability.get("atlas_dimensions_supported"),
        capability.get("texture_gather_supported"),
        capability.get("d32_render_target_supported"),
    )
    unsupported_valid = (
        capability.get("code") == "PSSM_REQUIRED_NATIVE_CAPABILITY_MISSING"
        and capability.get("reason") == PSSM_UNSUPPORTED_DETAIL
        and _json_exact(capability.get("required_atlas_width"), 2048)
        and _json_exact(capability.get("required_atlas_height"), 3072)
        and capability.get("required_format") == "D32_FLOAT"
        and capability.get("required_filter") == "PCF_4x4_TEXTURE_GATHER"
        and type(maximum) is int
        and maximum > 0
        and all(type(value) is bool for value in booleans)
        and booleans[0] is (maximum >= 3072)
        and not all(booleans)
        and report.get("backend_substitution") is False
    )
    if not unsupported_valid:
        raise ArtifactSetError("PSSM unsupported capability evidence is not exact")
    if (root / PSSM_EVIDENCE_ARTIFACT).exists():
        raise ArtifactSetError("unsupported PSSM report retained stale pass evidence")


def _metal_n3_image_metrics(payload: bytes) -> dict[str, int | float | str]:
    width = 96
    height = 64
    if len(payload) != width * height * 8:
        raise ArtifactSetError("Metal N3 image extent/byte count mismatch")
    luminance_sum = 0.0
    nontrivial_pixels = 0
    for offset in range(0, len(payload), 8):
        channels = struct.unpack_from("<4e", payload, offset)
        if not all(math.isfinite(channel) for channel in channels):
            raise ArtifactSetError("Metal N3 image contains non-finite data")
        luminance_sum += (
            0.2126 * channels[0]
            + 0.7152 * channels[1]
            + 0.0722 * channels[2]
        )
        if any(abs(channel) > 1.0e-6 for channel in channels[:3]):
            nontrivial_pixels += 1
    return {
        "width": width,
        "height": height,
        "format": "RGBA16_FLOAT",
        "bytes": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
        "mean_luminance": luminance_sum / (width * height),
        "nontrivial_pixels": nontrivial_pixels,
    }


def _verify_metal_n3_reported_metrics(
    reported: object,
    computed: dict[str, int | float | str],
    label: str,
) -> None:
    if not isinstance(reported, dict):
        raise ArtifactSetError(f"Metal N3 {label} report metrics are missing")
    mean = reported.get("mean_luminance")
    checks = {
        field: reported.get(field) == computed[field]
        for field in ("width", "height", "format", "bytes", "sha256",
                      "nontrivial_pixels")
    }
    checks["mean_luminance"] = (
        isinstance(mean, (int, float))
        and not isinstance(mean, bool)
        and math.isfinite(float(mean))
        and math.isclose(
            float(mean),
            float(computed["mean_luminance"]),
            rel_tol=1.0e-9,
            abs_tol=1.0e-12,
        )
    )
    failed = sorted(name for name, passed in checks.items() if not passed)
    if failed:
        raise ArtifactSetError(
            f"Metal N3 {label} report metrics mismatch: {', '.join(failed)}"
        )


def _valid_metal_n3_followup(
    record: object, width: int, height: int
) -> bool:
    if not isinstance(record, dict):
        return False
    mean = record.get("mean_luminance")
    return (
        record.get("width") == width
        and record.get("height") == height
        and record.get("format") == "RGBA16_FLOAT"
        and record.get("bytes") == width * height * 8
        and _is_sha256(record.get("sha256"))
        and _is_positive_int(record.get("nontrivial_pixels"))
        and isinstance(mean, (int, float))
        and not isinstance(mean, bool)
        and math.isfinite(float(mean))
    )


def _verify_metal_n3_pass_semantics(
    report: dict[str, object], payloads: dict[str, bytes]
) -> None:
    metrics = {
        key: _metal_n3_image_metrics(payloads[key])
        for key, _ in METAL_N3_IMAGE_ARTIFACTS
    }
    for key, _ in METAL_N3_IMAGE_ARTIFACTS:
        _verify_metal_n3_reported_metrics(report.get(key), metrics[key], key)

    raster = payloads["raster_only_hdr"]
    contribution = payloads["rt_contribution"]
    hybrid = payloads["hybrid_hdr"]
    applied = 0
    untouched = 0
    for offset in range(0, len(raster), 8):
        raster_values = struct.unpack_from("<4e", raster, offset)
        contribution_values = struct.unpack_from("<4e", contribution, offset)
        hybrid_values = struct.unpack_from("<4e", hybrid, offset)
        contribution_channels = struct.unpack_from("<4H", contribution, offset)
        applies = any((channel & 0x7FFF) != 0 for channel in contribution_channels[:3])
        if contribution_channels[3] != 0:
            raise ArtifactSetError("Metal N3 contribution changed straight alpha")
        if hybrid[offset + 6 : offset + 8] != raster[offset + 6 : offset + 8]:
            raise ArtifactSetError("Metal N3 hybrid changed raster alpha")
        if applies:
            applied += 1
            if hybrid[offset : offset + 6] == raster[offset : offset + 6]:
                raise ArtifactSetError("Metal N3 contribution did not change hybrid RGB")
            for channel in range(3):
                expected = max(
                    -65504.0,
                    min(
                        65504.0,
                        raster_values[channel] + contribution_values[channel],
                    ),
                )
                if not math.isclose(
                    hybrid_values[channel],
                    expected,
                    rel_tol=2.0e-3,
                    abs_tol=5.0e-4,
                ):
                    raise ArtifactSetError(
                        "Metal N3 hybrid RGB is not the attested GPU contribution"
                    )
        else:
            untouched += 1
            if hybrid[offset : offset + 8] != raster[offset : offset + 8]:
                raise ArtifactSetError(
                    "Metal N3 changed a pixel outside its contribution"
                )

    contract = report.get("contract")
    raster_contract = report.get("raster_contract")
    proof = report.get("proof")
    device = report.get("device")
    second = report.get("second_view_contribution")
    resized = report.get("resized_hybrid")
    if (
        not isinstance(contract, dict)
        or not isinstance(raster_contract, dict)
        or not isinstance(proof, dict)
    ):
        raise ArtifactSetError("Metal N3 contract or proof is missing")
    if not isinstance(device, dict):
        raise ArtifactSetError("Metal N3 device proof is missing")
    second_valid = _valid_metal_n3_followup(second, 96, 64)
    resized_valid = _valid_metal_n3_followup(resized, 80, 48)
    second_hash = second.get("sha256") if isinstance(second, dict) else None
    allocations = raster_contract.get("texture_allocations")
    live_allocations = (
        allocations.get("live") if isinstance(allocations, dict) else None
    )
    shutdown_allocations = (
        allocations.get("after_shutdown")
        if isinstance(allocations, dict)
        else None
    )
    checks = {
        "scope": report.get("scope") == METAL_N3_SCOPE,
        "device": isinstance(device.get("name"), str)
        and bool(device["name"])
        and device.get("same_ogre_device") is True
        and device.get("same_ogre_queue") is True
        and device.get("apple_family_9") is True,
        "image_contract": type(contract.get("image_version")) is int
        and contract.get("image_version") == 2
        and _is_positive_int(contract.get("image_generation"))
        and contract.get("usage")
        == "COLOR_ATTACHMENT_SHADER_READ_WRITE_COPY_SOURCE"
        and contract.get("release_state") == "GENERAL_READ_WRITE"
        and contract.get("return_state") == "GENERAL_READ_WRITE",
        "simultaneous_raster_contract": raster_contract.get(
            "raster_feature_tier"
        )
        == "MODERN_PBR_RT4_V1"
        and raster_contract.get("native_feature_tier")
        == "METAL_RAY_TRACING_N3"
        and raster_contract.get("vertex_layout")
        == "POSITION_NORMAL_TANGENT_UV0_FLOAT32_48"
        and type(raster_contract.get("vertex_stride_bytes")) is int
        and raster_contract.get("vertex_stride_bytes") == 48
        and raster_contract.get("authored_tangent_uv0") is True
        and raster_contract.get("base_color_texture") == "RGBA8_UNORM_SRGB"
        and type(raster_contract.get("directional_light_lux")) is int
        and raster_contract.get("directional_light_lux") == 1024
        and raster_contract.get("ray_material_parity_claimed") is False,
        "texture_allocation_contract": isinstance(live_allocations, dict)
        and _json_exact(
            live_allocations,
            {
                "source_textures": 1,
                "sampled_rgba": 1,
                "roughness_r8": 0,
                "metallic_r8": 0,
                "creates": 1,
                "destroys": 0,
                "live": 1,
                "exact_usage": True,
            },
        )
        and isinstance(shutdown_allocations, dict)
        and _json_exact(
            shutdown_allocations,
            {
                "creates": 1,
                "destroys": 1,
                "live": 0,
                "retired_name_lookups": 1,
                "retired_name_rejections": 1,
            },
        ),
        "distinct_nonempty_images": len(
            {metrics[key]["sha256"] for key, _ in METAL_N3_IMAGE_ARTIFACTS}
        )
        == 3
        and all(
            _is_positive_int(metrics[key]["nontrivial_pixels"])
            for key, _ in METAL_N3_IMAGE_ARTIFACTS
        ),
        "contribution_mapping": applied > 0
        and untouched > 0
        and type(proof.get("contribution_pixels")) is int
        and proof.get("contribution_pixels") == applied,
        "required_proofs": all(
            proof.get(field) is True
            for field in METAL_N3_REQUIRED_PROOF_BOOLEANS
        ),
        "far_plane_count": _is_positive_int(
            proof.get("off_axis_far_plane_contribution_pixels")
        ),
        "second_view": second_valid
        and second_hash != metrics["rt_contribution"]["sha256"],
        "resize": resized_valid,
    }
    failed = sorted(name for name, passed in checks.items() if not passed)
    if failed:
        raise ArtifactSetError(
            "Metal N3 pass evidence failed closed: " + ", ".join(failed)
        )


def _verify_metal_n2(
    root: Path,
    manifest: list[dict[str, object]],
    build_contract: dict[str, object],
) -> None:
    report_path = root / METAL_N2_REQUIRED_ARTIFACTS[0]
    attestation_path = root / METAL_N2_REQUIRED_ARTIFACTS[1]
    executable_path = root / METAL_N2_REQUIRED_ARTIFACTS[2]
    probe_path = root / METAL_N2_PROBE_ARTIFACT
    report = _read_json_object(report_path, "Metal N2 report")
    attestation = _read_json_object(attestation_path, "Metal N2 attestation")
    status = report.get("status")
    if report.get("schema") != "ror.ogre_next_metal_rt_n2.v3" or status not in (
        "pass",
        "skip",
    ):
        raise ArtifactSetError("Metal N2 report schema or status is invalid")
    if (
        attestation.get("schema")
        != "ror.ogre_next_metal_rt_n2.attestation.v2"
        or attestation.get("status") != status
    ):
        raise ArtifactSetError("Metal N2 attestation schema or status mismatch")
    provenance = report.get("provenance")
    source = attestation.get("source")
    if not isinstance(provenance, dict) or not isinstance(source, dict):
        raise ArtifactSetError("Metal N2 source provenance is missing")
    expected_source = {
        "ror_commit": provenance.get("ror_commit"),
        "ror_ref": provenance.get("ror_ref"),
        "relevant_source_clean": provenance.get("relevant_source_clean"),
        "relevant_source_manifest_sha256": provenance.get(
            "relevant_source_manifest_sha256"
        ),
    }
    if source != expected_source or source.get("relevant_source_clean") is not True:
        raise ArtifactSetError("Metal N2 source attestation mismatch")
    contract_ror = build_contract["ror_source"]
    contract_ogre = build_contract["provenance"]
    expected_provenance = {
        "ror_repository": contract_ror.get("repository"),
        "ror_ref": contract_ror.get("ref"),
        "ror_commit": contract_ror.get("commit"),
        "relevant_source_manifest_sha256": contract_ror.get(
            "relevant_manifest_sha256"
        ),
        "ogre_next_repository": contract_ogre.get("repository"),
        "ogre_next_commit": contract_ogre.get("commit"),
        "ogre_next_archive_sha256": contract_ogre.get("archive_sha256"),
    }
    if any(
        provenance.get(field) != expected
        for field, expected in expected_provenance.items()
    ):
        raise ArtifactSetError("Metal N2 build-contract provenance mismatch")
    _verify_attested_file(
        attestation.get("report"),
        report_path,
        report_path.name,
        "Metal N2",
        "report",
    )
    _verify_attested_file(
        attestation.get("executable"),
        executable_path,
        executable_path.name,
        "Metal N2",
        "executable",
    )
    if (
        provenance.get("build_artifact") != executable_path.name
        or provenance.get("build_artifact_bytes") != executable_path.stat().st_size
        or provenance.get("build_artifact_sha256") != sha256_file(executable_path)
    ):
        raise ArtifactSetError("Metal N2 executable provenance mismatch")
    if status == "pass":
        if probe_path.is_symlink() or not probe_path.is_file():
            raise ArtifactSetError(f"missing: {METAL_N2_PROBE_ARTIFACT}")
        if probe_path.stat().st_size == 0:
            raise ArtifactSetError(f"empty: {METAL_N2_PROBE_ARTIFACT}")
        _verify_attested_file(
            attestation.get("probe"),
            probe_path,
            probe_path.name,
            "Metal N2",
            "probe",
        )
        manifest.append(
            {
                "path": METAL_N2_PROBE_ARTIFACT,
                "bytes": probe_path.stat().st_size,
                "sha256": sha256_file(probe_path),
            }
        )
    elif attestation.get("probe") is not None or probe_path.exists():
        raise ArtifactSetError("skipped Metal N2 evidence retained a stale probe")


def _verify_metal_n3(
    root: Path,
    manifest: list[dict[str, object]],
    build_contract: dict[str, object],
) -> None:
    report_path = root / METAL_N3_REQUIRED_ARTIFACTS[0]
    attestation_path = root / METAL_N3_REQUIRED_ARTIFACTS[1]
    executable_path = root / METAL_N3_REQUIRED_ARTIFACTS[2]
    report = _read_json_object(report_path, "Metal N3 report")
    attestation = _read_json_object(attestation_path, "Metal N3 attestation")
    status = report.get("status")
    if report.get("schema") != "ror.ogre_next_metal_rt_n3.v2" or status not in (
        "pass",
        "skip",
    ):
        raise ArtifactSetError("Metal N3 report schema or status is invalid")
    if (
        attestation.get("schema")
        != "ror.ogre_next_metal_rt_n3.attestation.v1"
        or attestation.get("status") != status
    ):
        raise ArtifactSetError("Metal N3 attestation schema or status mismatch")
    provenance = report.get("provenance")
    source = attestation.get("source")
    if not isinstance(provenance, dict) or not isinstance(source, dict):
        raise ArtifactSetError("Metal N3 source provenance is missing")
    expected_source = {
        "ror_commit": provenance.get("ror_commit"),
        "ror_ref": provenance.get("ror_ref"),
        "relevant_source_clean": provenance.get("relevant_source_clean"),
        "relevant_source_manifest_sha256": provenance.get(
            "relevant_source_manifest_sha256"
        ),
    }
    if source != expected_source or source.get("relevant_source_clean") is not True:
        raise ArtifactSetError("Metal N3 source attestation mismatch")
    contract_ror = build_contract["ror_source"]
    contract_ogre = build_contract["provenance"]
    expected_provenance = {
        "ror_repository": contract_ror.get("repository"),
        "ror_ref": contract_ror.get("ref"),
        "ror_commit": contract_ror.get("commit"),
        "relevant_source_manifest_sha256": contract_ror.get(
            "relevant_manifest_sha256"
        ),
        "ogre_next_commit": contract_ogre.get("commit"),
    }
    if any(
        provenance.get(field) != expected
        for field, expected in expected_provenance.items()
    ):
        raise ArtifactSetError("Metal N3 build-contract provenance mismatch")
    _verify_attested_file(
        attestation.get("report"),
        report_path,
        report_path.name,
        "Metal N3",
        "report",
    )
    _verify_attested_file(
        attestation.get("executable"),
        executable_path,
        executable_path.name,
        "Metal N3",
        "executable",
    )
    if (
        provenance.get("build_artifact") != executable_path.name
        or provenance.get("build_artifact_bytes") != executable_path.stat().st_size
        or provenance.get("build_artifact_sha256") != sha256_file(executable_path)
    ):
        raise ArtifactSetError("Metal N3 executable provenance mismatch")
    if status == "skip" and (
        not isinstance(report.get("reason"), str) or not report["reason"]
    ):
        raise ArtifactSetError("skipped Metal N3 evidence has no reason")

    payloads: dict[str, bytes] = {}
    for key, name in METAL_N3_IMAGE_ARTIFACTS:
        path = root / name
        attested = attestation.get(key)
        if status == "skip":
            if attested is not None or path.exists():
                raise ArtifactSetError(
                    f"skipped Metal N3 evidence retained a stale {key} image"
                )
            continue
        if path.is_symlink() or not path.is_file():
            raise ArtifactSetError(f"missing: {name}")
        if path.stat().st_size == 0:
            raise ArtifactSetError(f"empty: {name}")
        _verify_attested_file(attested, path, path.name, "Metal N3", key)
        try:
            payloads[key] = path.read_bytes()
        except OSError as error:
            raise ArtifactSetError(f"could not read Metal N3 {key}: {error}") from error
        manifest.append(
            {
                "path": name,
                "bytes": path.stat().st_size,
                "sha256": sha256_file(path),
            }
        )
    if status == "pass":
        _verify_metal_n3_pass_semantics(report, payloads)


def verify_artifact_set(
    build_dir: Path,
    verify_metal_n2_evidence: bool = False,
    verify_metal_n3_evidence: bool = False,
    *,
    expected_ror_repository: str | None = None,
    expected_ror_ref: str | None = None,
    expected_ror_commit: str | None = None,
) -> list[dict[str, object]]:
    root = build_dir.expanduser().resolve()
    failures: list[str] = []
    manifest: list[dict[str, object]] = []
    required = REQUIRED_ARTIFACTS + (
        METAL_N2_REQUIRED_ARTIFACTS if verify_metal_n2_evidence else ()
    ) + (
        METAL_N3_REQUIRED_ARTIFACTS if verify_metal_n3_evidence else ()
    )
    for name in required:
        path = root / name
        if path.is_symlink():
            failures.append(f"symbolic link: {name}")
            continue
        if not path.is_file():
            failures.append(f"missing: {name}")
            continue
        size = path.stat().st_size
        if size == 0:
            failures.append(f"empty: {name}")
            continue
        manifest.append(
            {"path": name, "bytes": size, "sha256": sha256_file(path)}
        )
    if failures:
        raise ArtifactSetError(
            "OGRE-Next artifact set is incomplete: " + ", ".join(failures)
        )
    expected_source = _current_source_identity(
        expected_repository=expected_ror_repository,
        expected_ref=expected_ror_ref,
        expected_commit=expected_ror_commit,
    )
    build_contract = _read_build_contract(root, expected_source)
    _verify_pssm(root, manifest, build_contract)
    _verify_rt4(root, manifest, build_contract)
    if verify_metal_n2_evidence:
        _verify_metal_n2(root, manifest, build_contract)
    if verify_metal_n3_evidence:
        _verify_metal_n3(root, manifest, build_contract)
    return manifest


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument(
        "--expected-ror-repository",
        help="trusted RoR repository identity (defaults to the canonical repo)",
    )
    parser.add_argument(
        "--expected-ror-ref",
        help="trusted RoR ref identity (defaults to CI environment or Git)",
    )
    parser.add_argument(
        "--expected-ror-commit",
        help="trusted RoR commit (defaults to GITHUB_SHA or checked-out Git)",
    )
    parser.add_argument(
        "--verify-metal-n2-evidence",
        action="store_true",
        help=(
            "cross-check attested Apple Metal N2 pass or capability-skip "
            "evidence"
        ),
    )
    parser.add_argument(
        "--verify-metal-n3-evidence",
        action="store_true",
        help=(
            "cross-check attested Apple Metal N3 pass or capability-skip "
            "evidence"
        ),
    )
    args = parser.parse_args(argv)
    try:
        manifest = verify_artifact_set(
            args.build_dir,
            args.verify_metal_n2_evidence,
            args.verify_metal_n3_evidence,
            expected_ror_repository=args.expected_ror_repository,
            expected_ror_ref=args.expected_ror_ref,
            expected_ror_commit=args.expected_ror_commit,
        )
    except (ArtifactSetError, OSError) as error:
        print(str(error), file=sys.stderr)
        return 1
    print(
        json.dumps(
            {"schema_version": 1, "status": "pass", "artifacts": manifest},
            indent=2,
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
