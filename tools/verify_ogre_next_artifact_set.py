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
    entry: object, path: Path, expected_name: str, label: str
) -> None:
    if not isinstance(entry, dict):
        raise ArtifactSetError(f"invalid Metal N2 {label} attestation")
    expected = {
        "path": expected_name,
        "bytes": path.stat().st_size,
        "sha256": sha256_file(path),
    }
    if entry != expected:
        raise ArtifactSetError(f"Metal N2 {label} attestation mismatch")


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
        attestation.get("report"), report_path, report_path.name, "report"
    )
    _verify_attested_file(
        attestation.get("executable"),
        executable_path,
        executable_path.name,
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
            attestation.get("probe"), probe_path, probe_path.name, "probe"
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


def verify_artifact_set(
    build_dir: Path, verify_metal_n2_evidence: bool = False
) -> list[dict[str, object]]:
    root = build_dir.expanduser().resolve()
    failures: list[str] = []
    manifest: list[dict[str, object]] = []
    required = REQUIRED_ARTIFACTS + (
        METAL_N2_REQUIRED_ARTIFACTS if verify_metal_n2_evidence else ()
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
    args = parser.parse_args(argv)
    try:
        manifest = verify_artifact_set(
            args.build_dir, args.verify_metal_n2_evidence
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
