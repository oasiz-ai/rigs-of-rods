#!/usr/bin/env python3
"""Fail unless every required OGRE-Next CI artifact exists and is nonempty."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
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


def _verify_metal_n2(root: Path, manifest: list[dict[str, object]]) -> None:
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


def _verify_metal_n3(root: Path, manifest: list[dict[str, object]]) -> None:
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
        metrics = report.get(key)
        if not isinstance(metrics, dict):
            raise ArtifactSetError(f"Metal N3 {key} report metrics are missing")
        if (
            metrics.get("width") != 96
            or metrics.get("height") != 64
            or metrics.get("format") != "RGBA16_FLOAT"
            or metrics.get("bytes") != path.stat().st_size
            or metrics.get("sha256") != sha256_file(path)
        ):
            raise ArtifactSetError(f"Metal N3 {key} report metrics mismatch")
        manifest.append(
            {
                "path": name,
                "bytes": path.stat().st_size,
                "sha256": sha256_file(path),
            }
        )


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
    if verify_metal_n2_evidence:
        _verify_metal_n2(root, manifest)
    if verify_metal_n3_evidence:
        _verify_metal_n3(root, manifest)
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
