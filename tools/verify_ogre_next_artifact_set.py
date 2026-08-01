#!/usr/bin/env python3
"""Fail unless every required OGRE-Next CI artifact exists and is nonempty."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
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
    "ror-ogre-next-frontend-rt4-pbr-v1-attestation.json",
)
RT4_REPORT_ARTIFACT = "ror-ogre-next-frontend-rt4-pbr-v1-report.json"
RT4_PPM_ARTIFACT = "ror-ogre-next-frontend-rt4-pbr-v1.ppm"
RT4_ISOLATION_ARTIFACT = "ror-ogre-next-frontend-rt4-pbr-v1-isolation.bin"
RT4_ATTESTATION_ARTIFACT = (
    "ror-ogre-next-frontend-rt4-pbr-v1-attestation.json"
)
RT4_PACKAGE_EXECUTABLE_STEM = "ror_ogre_next_frontend_n1_smoke"
RT4_EXPECTED_VARIANTS = (
    ("baseline", "none"),
    ("base_color", "base_color_rgb"),
    ("roughness_g", "packed_green_roughness"),
    ("metallic_b", "packed_blue_metallic"),
    ("emissive", "emissive_rgb"),
    ("sampler_uv", "sampler_address_over_uv0"),
)
RT4_EXPECTED_RETIREMENT = {
    "schema": "ror.ogre_next_rt4_texture_retirement.v1",
    "isolated_from_visual_variants": True,
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
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
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
    if entry != expected:
        raise ArtifactSetError(f"{checkpoint} {label} attestation mismatch")


def _read_build_contract(root: Path) -> dict[str, object]:
    contract = _read_json_object(
        root / REQUIRED_ARTIFACTS[0], "OGRE-Next build contract"
    )
    ror_source = contract.get("ror_source")
    ogre_source = contract.get("provenance")
    shader_media = contract.get("shader_media")
    platform = contract.get("platform")
    notice = (
        shader_media.get("third_party_notice")
        if isinstance(shader_media, dict)
        else None
    )
    if (
        type(contract.get("schema_version")) is not int
        or contract.get("schema_version") != 2
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
        or not isinstance(platform, dict)
        or platform.get("policy")
        not in (
            "macos-arm64-metal",
            "windows-x64-d3d11",
            "linux-x86_64-vulkan",
        )
    ):
        raise ArtifactSetError("OGRE-Next build contract source identity is invalid")
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


def _verify_rt4_semantics(
    report: dict[str, object], ppm_path: Path, isolation_path: Path
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

    if (
        report.get("schema")
        != "ror.ogre_next_frontend_rt4_pbr_v1_smoke.v1"
        or report.get("status") != "pass"
    ):
        raise ArtifactSetError("RT4 report schema or status is invalid")
    hdr = report.get("hdr")
    sdr = report.get("sdr")
    isolation = report.get("texture_isolation")
    if not isinstance(hdr, dict) or not isinstance(sdr, dict):
        raise ArtifactSetError("RT4 HDR/SDR report metrics are missing")
    if not isinstance(isolation, dict):
        raise ArtifactSetError("RT4 isolation report is missing")
    variants = isolation.get("variants")
    controls = {
        "schema": isolation.get("schema")
        == "ror.ogre_next_rt4_texture_isolation.v1",
        "file": isolation.get("evidence_file") == isolation_path.name,
        "extent": isolation.get("width") == width
        and isolation.get("height") == height,
        "bytes": isolation.get("evidence_bytes") == len(isolation_payload),
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
        if (
            entry.get("name") != expected[0]
            or entry.get("changed_input") != expected[1]
            or entry.get("asset_sequence") != index + 1
        ):
            raise ArtifactSetError("RT4 isolation variant identity mismatch")
        for attachment, bytes_per_pixel in (("hdr", 8), ("sdr", 4)):
            reported = entry.get(attachment)
            expected_bytes = width * height * bytes_per_pixel
            if not isinstance(reported, dict):
                raise ArtifactSetError(
                    f"RT4 {expected[0]} {attachment} metadata is missing"
                )
            if (
                reported.get("offset") != offset
                or reported.get("bytes") != expected_bytes
                or offset + expected_bytes > len(isolation_payload)
            ):
                raise ArtifactSetError(
                    f"RT4 {expected[0]} {attachment} slice layout mismatch"
                )
            payload = isolation_payload[offset : offset + expected_bytes]
            if attachment == "hdr":
                for channels in struct.iter_unpack("<4e", payload):
                    if not all(math.isfinite(channel) for channel in channels):
                        raise ArtifactSetError("RT4 HDR isolation contains non-finite data")
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
                reported.get("changed_pixels_from_baseline") != changed
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

    if offset != len(isolation_payload):
        raise ArtifactSetError("RT4 isolation evidence has trailing bytes")
    if any(
        len(hashes) != len(RT4_EXPECTED_VARIANTS)
        for hashes in exact_hashes.values()
    ):
        raise ArtifactSetError("RT4 isolation variants are not all distinct")
    baseline_sdr_rgb = bytes(
        channel
        for pixel_offset in range(0, len(baseline["sdr"]), 4)
        for channel in baseline["sdr"][pixel_offset : pixel_offset + 3]
    )
    report_checks = {
        "ppm_baseline": ppm_pixels == baseline_sdr_rgb,
        "retirement": report.get("texture_retirement")
        == RT4_EXPECTED_RETIREMENT,
        "hdr_format": hdr.get("format") == "RGBA16_FLOAT",
        "hdr_extent": hdr.get("width") == width and hdr.get("height") == height,
        "hdr_exact": hdr.get("exact_attachment_fnv1a64")
        == _fnv1a64(baseline["hdr"]),
        "sdr_format": sdr.get("format") == "RGBA8_SRGB",
        "sdr_extent": sdr.get("width") == width and sdr.get("height") == height,
        "sdr_exact": sdr.get("exact_attachment_fnv1a64")
        == _fnv1a64(baseline["sdr"]),
        "sdr_ppm_hash": sdr.get("rgb8_fnv1a64") == _fnv1a64(ppm_pixels),
        "sdr_distinct": sdr.get("distinct_rgb8_values") == len(colour_counts),
        "sdr_geometry": sdr.get("non_background_pixels") == ppm_non_background
        and ppm_non_background >= 512,
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
    report_path = root / RT4_REPORT_ARTIFACT
    ppm_path = root / RT4_PPM_ARTIFACT
    isolation_path = root / RT4_ISOLATION_ARTIFACT
    attestation_path = root / RT4_ATTESTATION_ARTIFACT
    report = _read_json_object(report_path, "RT4 report")
    attestation = _read_json_object(attestation_path, "RT4 attestation")
    if (
        attestation.get("schema")
        != "ror.ogre_next_frontend_rt4_pbr_v1.attestation.v1"
        or attestation.get("status") != "pass"
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

    files = attestation.get("files")
    if not isinstance(files, dict) or set(files) != {
        "build_contract",
        "report",
        "ppm",
        "isolation",
        "executable",
    }:
        raise ArtifactSetError("RT4 attested file set is invalid")
    for key, path, relative in (
        ("build_contract", root / REQUIRED_ARTIFACTS[0], REQUIRED_ARTIFACTS[0]),
        ("report", report_path, RT4_REPORT_ARTIFACT),
        ("ppm", ppm_path, RT4_PPM_ARTIFACT),
        ("isolation", isolation_path, RT4_ISOLATION_ARTIFACT),
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
    source_report_checks = {
        "repository": provenance.get("ror_repository") == expected_source["repository"],
        "ref": provenance.get("ror_ref") == expected_source["ref"],
        "commit": provenance.get("ror_commit") == expected_source["commit"],
        "manifest": provenance.get("ror_relevant_source_manifest_sha256")
        == expected_source["relevant_manifest_sha256"],
        "manifest_count": provenance.get("ror_relevant_source_manifest_file_count")
        == expected_source["relevant_manifest_file_count"],
        "ogre_commit": provenance.get("ogre_next_commit")
        == expected_ogre["commit"],
        "ogre_archive": provenance.get("ogre_next_archive_sha256")
        == expected_ogre["archive_sha256"],
        "shader_root": provenance.get("shader_media_root")
        == expected_shader["root"],
        "shader_license": provenance.get("shader_media_license_expression")
        == expected_shader["license_expression"],
        "shader_notice": provenance.get("shader_media_notice_sha256")
        == expected_shader["notice_sha256"],
        "media_manifest": _is_sha256(expected_shader["manifest_sha256"]),
        "media_count": _is_positive_int(expected_shader["manifest_file_count"]),
    }
    if attestation.get("source") != expected_source:
        raise ArtifactSetError("RT4 source attestation mismatch")
    if attestation.get("ogre_next") != expected_ogre:
        raise ArtifactSetError("RT4 Ogre attestation mismatch")
    if attestation.get("shader_media") != expected_shader:
        raise ArtifactSetError("RT4 shader-media attestation mismatch")
    failed_provenance = sorted(
        name for name, passed in source_report_checks.items() if not passed
    )
    if failed_provenance:
        raise ArtifactSetError(
            "RT4 build-contract provenance mismatch: "
            + ", ".join(failed_provenance)
        )

    computed_slices = _verify_rt4_semantics(report, ppm_path, isolation_path)
    if attestation.get("isolation_slices") != computed_slices:
        raise ArtifactSetError("RT4 SHA-256 slice attestation mismatch")
    manifest.append(
        {
            "path": executable_relative,
            "bytes": executable_path.stat().st_size,
            "sha256": sha256_file(executable_path),
        }
    )


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
    proof = report.get("proof")
    device = report.get("device")
    second = report.get("second_view_contribution")
    resized = report.get("resized_hybrid")
    if not isinstance(contract, dict) or not isinstance(proof, dict):
        raise ArtifactSetError("Metal N3 contract or proof is missing")
    if not isinstance(device, dict):
        raise ArtifactSetError("Metal N3 device proof is missing")
    second_valid = _valid_metal_n3_followup(second, 96, 64)
    resized_valid = _valid_metal_n3_followup(resized, 80, 48)
    second_hash = second.get("sha256") if isinstance(second, dict) else None
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
    build_contract = _read_build_contract(root)
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
