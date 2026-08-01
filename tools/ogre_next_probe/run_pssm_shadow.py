#!/usr/bin/env python3
"""Run the PSSM smoke as a challenged child and atomically attest its outputs."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import subprocess
import sys
import tempfile
from typing import Any


REPORT_SCHEMA = "ror.ogre_next_pssm_shadow_smoke.v3"
EXECUTION_SCHEMA = "ror.ogre_next_pssm_shadow_execution_challenge.v1"
RECEIPT_SCHEMA = "ror.ogre_next_pssm_shadow_execution_receipt.v1"
ATTESTATION_SCHEMA = "ror.ogre_next_pssm_shadow_attestation.v1"
MANIFEST_SCHEMA = "ror.ogre_next_pssm_shadow_artifact_manifest.v1"
REPORT_NAME = "ror-ogre-next-pssm-shadow-report.json"
EVIDENCE_NAME = "ror-ogre-next-pssm-shadow-isolation.bin"
RECEIPT_NAME = "ror-ogre-next-pssm-shadow-execution-receipt.json"
ATTESTATION_NAME = "ror-ogre-next-pssm-shadow-attestation.json"
MANIFEST_NAME = "ror-ogre-next-pssm-shadow-artifact-manifest.json"
TRUSTED_REPOSITORY = "oasiz-ai/rigs-of-rods"
TRUSTED_REPOSITORY_URL = "https://github.com/oasiz-ai/rigs-of-rods"
UNSUPPORTED_EXIT_CODE = 77
OFFLINE_EXECUTION_LIMITATION = (
    "hashes, binary structure, report semantics, and a fresh challenge can be "
    "verified offline, but the receipt is not a cryptographic proof that its "
    "executable ran; require the GitHub artifact attestation for that receipt"
)


class PssmRunError(RuntimeError):
    """Raised when execution or evidence publication fails closed."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _reject_duplicate_key(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    value: dict[str, Any] = {}
    for key, item in pairs:
        if key in value:
            raise PssmRunError(f"duplicate JSON object key: {key}")
        value[key] = item
    return value


def read_json(path: Path, label: str) -> dict[str, Any]:
    try:
        value = json.loads(
            path.read_text(encoding="utf-8"), object_pairs_hook=_reject_duplicate_key
        )
    except (OSError, json.JSONDecodeError) as error:
        raise PssmRunError(f"could not read {label}: {error}") from error
    if not isinstance(value, dict):
        raise PssmRunError(f"{label} root is not an object")
    return value


def fsync_parent(path: Path) -> None:
    if os.name == "nt":
        return
    descriptor = os.open(
        path.parent, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0)
    )
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def write_json_atomically(path: Path, value: dict[str, Any]) -> None:
    temporary_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            dir=path.parent,
            prefix=f".{path.name}.",
            suffix=".tmp",
            delete=False,
        ) as temporary:
            temporary_path = Path(temporary.name)
            json.dump(value, temporary, indent=2, sort_keys=True)
            temporary.write("\n")
            temporary.flush()
            os.fsync(temporary.fileno())
        os.replace(temporary_path, path)
        temporary_path = None
        fsync_parent(path)
    except OSError as error:
        raise PssmRunError(f"could not atomically publish {path.name}: {error}") from error
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)


def durable_unlink(path: Path) -> None:
    existed = path.exists() or path.is_symlink()
    try:
        path.unlink(missing_ok=True)
        if existed:
            fsync_parent(path)
    except OSError as error:
        raise PssmRunError(f"could not invalidate stale {path.name}: {error}") from error


def direct_file(path: Path, root: Path, label: str) -> Path:
    try:
        resolved_root = root.resolve(strict=True)
        resolved = path.resolve(strict=True)
        resolved.relative_to(resolved_root)
    except (OSError, ValueError) as error:
        raise PssmRunError(f"{label} escaped the build root or is missing: {error}") from error
    if path.is_symlink() or not path.is_file() or path.stat().st_size <= 0:
        raise PssmRunError(f"{label} is empty, indirect, or irregular")
    return resolved


def artifact_record(path: Path, root: Path) -> dict[str, Any]:
    resolved = direct_file(path, root, path.name)
    return {
        "path": resolved.relative_to(root.resolve(strict=True)).as_posix(),
        "bytes": resolved.stat().st_size,
        "sha256": sha256_file(resolved),
    }


def workflow_identity(source: dict[str, Any]) -> dict[str, Any]:
    if os.environ.get("GITHUB_ACTIONS") != "true":
        return {
            "provider": "local",
            "repository": "",
            "workflow_ref": "",
            "run_id": "",
            "run_attempt": "",
            "sha": "",
            "ref": "",
            "job": "",
            "external_dsse_required": False,
        }
    names = (
        "GITHUB_REPOSITORY",
        "GITHUB_WORKFLOW_REF",
        "GITHUB_RUN_ID",
        "GITHUB_RUN_ATTEMPT",
        "GITHUB_SHA",
        "GITHUB_REF",
        "GITHUB_JOB",
    )
    values = {name: os.environ.get(name, "") for name in names}
    if any(not value for value in values.values()):
        raise PssmRunError("GitHub Actions workflow identity is incomplete")
    if (
        values["GITHUB_REPOSITORY"] != TRUSTED_REPOSITORY
        or values["GITHUB_SHA"] != source.get("commit")
        or not values["GITHUB_WORKFLOW_REF"].startswith(
            TRUSTED_REPOSITORY + "/.github/workflows/ogre-next-probe.yml@"
        )
    ):
        raise PssmRunError("GitHub Actions workflow identity is not trusted")
    return {
        "provider": "github-actions",
        "repository": values["GITHUB_REPOSITORY"],
        "workflow_ref": values["GITHUB_WORKFLOW_REF"],
        "run_id": values["GITHUB_RUN_ID"],
        "run_attempt": values["GITHUB_RUN_ATTEMPT"],
        "sha": values["GITHUB_SHA"],
        "ref": values["GITHUB_REF"],
        "job": values["GITHUB_JOB"],
        "external_dsse_required": True,
    }


def require_source(contract: dict[str, Any], report: dict[str, Any]) -> dict[str, Any]:
    source = contract.get("ror_source")
    provenance = report.get("provenance")
    if not isinstance(source, dict) or not isinstance(provenance, dict):
        raise PssmRunError("PSSM source provenance is missing")
    expected = {
        "repository": provenance.get("ror_repository"),
        "ref": provenance.get("ror_ref"),
        "commit": provenance.get("ror_commit"),
        "relevant_manifest_sha256": provenance.get(
            "ror_relevant_source_manifest_sha256"
        ),
        "relevant_manifest_file_count": source.get(
            "relevant_manifest_file_count"
        ),
    }
    if source != expected or source.get("repository") != TRUSTED_REPOSITORY_URL:
        raise PssmRunError("PSSM report differs from the configured RoR source")
    return source


def run_challenged(args: argparse.Namespace) -> dict[str, Any]:
    root = args.build_dir.expanduser().resolve(strict=True)
    executable = direct_file(args.executable, root, "PSSM executable")
    build_contract_path = direct_file(
        args.build_contract, root, "PSSM build contract"
    )
    if not args.media_root.resolve(strict=True).is_dir():
        raise PssmRunError("PSSM media root is unavailable")

    output_paths = (
        args.report,
        args.evidence,
        args.execution_receipt,
        args.attestation,
        args.artifact_manifest,
    )
    for path in output_paths:
        if path.parent.resolve(strict=True) != root:
            raise PssmRunError(f"PSSM output must be a direct build-root file: {path}")
        durable_unlink(path)

    challenge = secrets.token_hex(32)
    command = [
        str(executable),
        "--media-root",
        str(args.media_root.resolve(strict=True)),
        "--report",
        str(args.report),
        "--evidence",
        str(args.evidence),
        "--execution-challenge",
        challenge,
    ]
    try:
        completed = subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
            timeout=args.timeout_seconds,
        )
    except (OSError, subprocess.SubprocessError) as error:
        raise PssmRunError(f"could not execute PSSM child process: {error}") from error
    if completed.returncode not in (0, UNSUPPORTED_EXIT_CODE):
        detail = completed.stderr.strip() or completed.stdout.strip() or "no detail"
        raise PssmRunError(
            f"PSSM child failed with exit {completed.returncode}: {detail}"
        )

    report = read_json(args.report, "PSSM report")
    expected_status = "pass" if completed.returncode == 0 else "unsupported"
    execution = report.get("execution")
    if (
        report.get("schema") != REPORT_SCHEMA
        or report.get("status") != expected_status
        or not isinstance(execution, dict)
        or execution
        != {"schema": EXECUTION_SCHEMA, "challenge_nonce": challenge}
    ):
        raise PssmRunError("PSSM child did not return the exact fresh challenge")
    if expected_status == "pass":
        direct_file(args.evidence, root, "PSSM evidence")
    elif args.evidence.exists() or args.evidence.is_symlink():
        raise PssmRunError("unsupported PSSM execution retained stale evidence")

    contract = read_json(build_contract_path, "PSSM build contract")
    source = require_source(contract, report)
    workflow = workflow_identity(source)
    provenance = report["provenance"]
    build_identity = provenance.get("executable_build_identity")
    if not isinstance(build_identity, str) or not build_identity:
        raise PssmRunError("PSSM executable build identity is missing")

    evidence_record = (
        artifact_record(args.evidence, root) if expected_status == "pass" else None
    )
    subjects = {
        "build_contract": artifact_record(build_contract_path, root),
        "executable": artifact_record(executable, root),
        "report": artifact_record(args.report, root),
        "evidence": evidence_record,
    }
    receipt = {
        "schema": RECEIPT_SCHEMA,
        "status": expected_status,
        "observation": {
            "mode": "fresh_child_process_challenge",
            "challenge_nonce": challenge,
            "observed_process_exit_code": completed.returncode,
            "offline_cryptographic_execution_proof": False,
            "limitation": OFFLINE_EXECUTION_LIMITATION,
        },
        "subjects": subjects,
        "build_identity": build_identity,
        "source": source,
        "workflow": workflow,
        "complete": True,
    }
    write_json_atomically(args.execution_receipt, receipt)

    attestation = {
        "schema": ATTESTATION_SCHEMA,
        "status": expected_status,
        "integrity_model": (
            "atomic-self-contained-sha256-plus-challenged-execution-receipt; "
            "external-github-dsse-required-in-ci"
        ),
        "source": source,
        "workflow": workflow,
        "build_identity": build_identity,
        "files": {
            **subjects,
            "execution_receipt": artifact_record(args.execution_receipt, root),
        },
        "complete": True,
    }
    write_json_atomically(args.attestation, attestation)

    records = [record for record in subjects.values() if record is not None]
    records.extend(
        (
            artifact_record(args.execution_receipt, root),
            artifact_record(args.attestation, root),
        )
    )
    manifest = {
        "schema": MANIFEST_SCHEMA,
        "status": expected_status,
        "source": source,
        "workflow": workflow,
        "build_identity": build_identity,
        "artifacts": sorted(records, key=lambda record: record["path"]),
        "complete": True,
    }
    write_json_atomically(args.artifact_manifest, manifest)
    return manifest


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--build-dir", type=Path, required=True)
    result.add_argument("--executable", type=Path, required=True)
    result.add_argument("--media-root", type=Path, required=True)
    result.add_argument("--build-contract", type=Path, required=True)
    result.add_argument("--report", type=Path, required=True)
    result.add_argument("--evidence", type=Path, required=True)
    result.add_argument("--execution-receipt", type=Path, required=True)
    result.add_argument("--attestation", type=Path, required=True)
    result.add_argument("--artifact-manifest", type=Path, required=True)
    result.add_argument("--timeout-seconds", type=int, default=180)
    return result


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    if args.timeout_seconds <= 0:
        print("PSSM timeout must be positive", file=sys.stderr)
        return 1
    try:
        manifest = run_challenged(args)
    except (PssmRunError, OSError) as error:
        print(str(error), file=sys.stderr)
        return 1
    print(json.dumps(manifest, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
