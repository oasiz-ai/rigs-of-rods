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
)
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
    if (
        contract.get("schema_version") != 2
        or not isinstance(ror_source, dict)
        or not isinstance(ogre_source, dict)
        or not isinstance(ror_source.get("repository"), str)
        or not ror_source["repository"]
        or not isinstance(ror_source.get("ref"), str)
        or not ror_source["ref"]
        or re.fullmatch(r"[0-9a-f]{40}", str(ror_source.get("commit"))) is None
        or not _is_sha256(ror_source.get("relevant_manifest_sha256"))
        or not isinstance(ogre_source.get("repository"), str)
        or not ogre_source["repository"]
        or re.fullmatch(r"[0-9a-f]{40}", str(ogre_source.get("commit"))) is None
        or not _is_sha256(ogre_source.get("archive_sha256"))
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
        "image_contract": contract.get("image_version") == 2
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
    build_contract = (
        _read_build_contract(root)
        if verify_metal_n2_evidence or verify_metal_n3_evidence
        else {}
    )
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
