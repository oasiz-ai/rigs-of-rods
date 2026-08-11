#!/usr/bin/env python3
"""Validate one non-admitted RoR-OgreNext child execution receipt."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import sys
from typing import Any


BUILD_CONTRACT_NAME = "ogre-next-build-contract.json"
RECEIPT_NAME = "ror-ogre-next-child-runtime-execution-receipt.json"
STDOUT_LOG_NAME = "ror-ogre-next-child-runtime.stdout.log"
STDERR_LOG_NAME = "ror-ogre-next-child-runtime.stderr.log"
RECEIPT_SCHEMA = "ror.ogre_next_child_runtime_execution_receipt.v1"
INTENT_SCHEMA = "ror.renderer_ogre_next_child_intent_argv.v1"
SUCCESS_LINE = b"RoR Ogre-Next child: completed-headless-bootstrap"
SKIP_LINE = (
    b"RoR Ogre-Next child: skipped-exact-pssm-capability-unsupported"
)
INTENT_ARGUMENTS = (
    "--ror-renderer-child-intent-version=1",
    "--ror-renderer-child-frontend=ogre-next-require",
    "--ror-renderer-child-directional-shadows=pssm",
    "--ror-renderer-child-native-backend=none",
)
PLATFORM_BACKENDS = {
    "macos-arm64-metal": ("RenderSystem_Metal", "metal", "RoR-OgreNext"),
    "windows-x64-d3d11": (
        "RenderSystem_Direct3D11",
        "direct3d11",
        "RoR-OgreNext.exe",
    ),
    "linux-x86_64-vulkan": (
        "RenderSystem_Vulkan",
        "vulkan",
        "RoR-OgreNext",
    ),
}
RECEIPT_PROCESS_MODEL = "single-process-reviewed-source-closure-v1"
NONCE_POLICY = "os-csprng-256-bit-v1"
TIMESTAMP_POLICY = "omitted-no-wall-clock-v1"


class ReceiptValidationError(RuntimeError):
    """Raised when child execution evidence does not match exact artifacts."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _read_json_object(path: Path, label: str) -> dict[str, Any]:
    def reject_duplicates(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in pairs:
            if key in result:
                raise ReceiptValidationError(
                    f"invalid {label}: duplicate JSON key {key!r}"
                )
            result[key] = value
        return result

    try:
        value = json.loads(
            path.read_text(encoding="utf-8"),
            object_pairs_hook=reject_duplicates,
        )
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ReceiptValidationError(f"invalid {label}: {error}") from error
    if not isinstance(value, dict):
        raise ReceiptValidationError(f"invalid {label}: root is not an object")
    return value


def _require_exact_keys(
    value: object, expected: set[str], label: str
) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != expected:
        raise ReceiptValidationError(f"{label} keys are not exact")
    return value


def _is_sha256(value: object) -> bool:
    return isinstance(value, str) and re.fullmatch(r"[0-9a-f]{64}", value) is not None


def _is_positive_int(value: object) -> bool:
    return type(value) is int and value > 0


def _exact(actual: object, expected: object) -> bool:
    if type(actual) is not type(expected):
        return False
    if isinstance(expected, dict):
        return set(actual) == set(expected) and all(
            _exact(actual[key], item) for key, item in expected.items()
        )
    if isinstance(expected, list):
        return len(actual) == len(expected) and all(
            _exact(left, right) for left, right in zip(actual, expected)
        )
    return actual == expected


def expected_receipt_scope(build_contract: dict[str, Any]) -> dict[str, Any]:
    components = build_contract.get("components")
    if not isinstance(components, dict):
        raise ReceiptValidationError("build contract child receipt policy is invalid")
    packaged = components.get("headless_child_packaged")
    admitted = components.get("headless_child_production_admitted")
    if type(packaged) is not bool or admitted is not False:
        raise ReceiptValidationError("build contract child receipt policy is invalid")
    return {
        "probe_only": not packaged,
        "packaged": packaged,
        "production_admitted": False,
        "process_model": RECEIPT_PROCESS_MODEL,
    }


def _regular_file(root: Path, relative: str, label: str) -> Path:
    pure = PurePosixPath(relative)
    if (
        not relative
        or pure.is_absolute()
        or "\\" in relative
        or any(part in ("", ".", "..") for part in pure.parts)
    ):
        raise ReceiptValidationError(f"{label} path is not canonical")
    path = root.joinpath(*pure.parts)
    if path.is_symlink() or not path.is_file():
        raise ReceiptValidationError(f"{label} is missing or indirect")
    try:
        resolved = path.resolve(strict=True)
        resolved.relative_to(root)
    except (OSError, ValueError) as error:
        raise ReceiptValidationError(f"{label} escapes the build root") from error
    if resolved != path:
        raise ReceiptValidationError(f"{label} contains an indirect parent")
    return path


def _artifact_record(
    root: Path, relative: str, label: str, *, allow_empty: bool = False
) -> dict[str, Any]:
    path = _regular_file(root, relative, label)
    size = path.stat().st_size
    if size < 0 or (size == 0 and not allow_empty):
        raise ReceiptValidationError(f"{label} is empty")
    return {"path": relative, "size_bytes": size, "sha256": sha256_file(path)}


def expected_child_relative(build_contract: dict[str, Any]) -> str:
    platform = build_contract.get("platform")
    if not isinstance(platform, dict):
        raise ReceiptValidationError("build contract platform is missing")
    policy = platform.get("policy")
    backend = PLATFORM_BACKENDS.get(policy)
    if backend is None:
        raise ReceiptValidationError("build contract platform policy is unsupported")
    return f"renderer-ogre-next-child-runtime/Release/{backend[2]}"


def _expected_provenance(
    root: Path, build_contract: dict[str, Any]
) -> dict[str, Any]:
    source = build_contract.get("ror_source")
    ogre = build_contract.get("provenance")
    if not isinstance(source, dict) or not isinstance(ogre, dict):
        raise ReceiptValidationError("build contract provenance is missing")
    ror = {
        "repository": source.get("repository"),
        "ref": source.get("ref"),
        "commit": source.get("commit"),
        "relevant_manifest_sha256": source.get("relevant_manifest_sha256"),
        "relevant_manifest_file_count": source.get(
            "relevant_manifest_file_count"
        ),
    }
    ogre_next = {
        "repository": ogre.get("repository"),
        "branch": ogre.get("branch"),
        "commit": ogre.get("commit"),
        "archive_sha256": ogre.get("archive_sha256"),
    }
    if (
        not isinstance(ror["repository"], str)
        or not isinstance(ror["ref"], str)
        or not isinstance(ror["commit"], str)
        or re.fullmatch(r"[0-9a-f]{40}", ror["commit"]) is None
        or not _is_sha256(ror["relevant_manifest_sha256"])
        or not _is_positive_int(ror["relevant_manifest_file_count"])
        or not isinstance(ogre_next["repository"], str)
        or not isinstance(ogre_next["branch"], str)
        or not isinstance(ogre_next["commit"], str)
        or re.fullmatch(r"[0-9a-f]{40}", ogre_next["commit"]) is None
        or not _is_sha256(ogre_next["archive_sha256"])
    ):
        raise ReceiptValidationError("build contract provenance is invalid")
    return {
        "ror": ror,
        "ogre_next": ogre_next,
        "build_contract": _artifact_record(
            root, BUILD_CONTRACT_NAME, "build contract"
        ),
    }


def _expected_platform(build_contract: dict[str, Any]) -> dict[str, Any]:
    platform = build_contract.get("platform")
    if not isinstance(platform, dict):
        raise ReceiptValidationError("build contract platform is missing")
    policy = platform.get("policy")
    backend = PLATFORM_BACKENDS.get(policy)
    if backend is None or platform.get("renderer_target") != backend[0]:
        raise ReceiptValidationError("build contract renderer backend is invalid")
    expected = {
        "policy": policy,
        "system": platform.get("system"),
        "processor": platform.get("processor"),
        "renderer_target": backend[0],
        "runtime_backend": backend[1],
    }
    if not isinstance(expected["system"], str) or not expected["system"]:
        raise ReceiptValidationError("build contract system is invalid")
    if not isinstance(expected["processor"], str) or not expected["processor"]:
        raise ReceiptValidationError("build contract processor is invalid")
    return expected


def classify_observation(
    platform_policy: str,
    launch_status: str,
    exit_code: int | None,
    stdout: bytes,
    stderr: bytes,
) -> tuple[str, str, int]:
    if platform_policy not in PLATFORM_BACKENDS:
        return "failure", "invalid-platform-observation", 78
    line_ending = (
        b"\r\n" if platform_policy == "windows-x64-d3d11" else b"\n"
    )
    success_marker = SUCCESS_LINE + line_ending
    skip_marker = SKIP_LINE + line_ending
    success_exact = stdout.count(success_marker) == 1 and stdout.endswith(
        success_marker
    )
    skip_exact = stderr.count(skip_marker) == 1 and stderr.endswith(skip_marker)
    if (
        launch_status == "exited"
        and exit_code == 0
        and success_exact
        and SKIP_LINE not in stderr
    ):
        return "pass", "completed-headless-bootstrap", 0
    if (
        launch_status == "exited"
        and exit_code == 77
        and skip_exact
        and SUCCESS_LINE not in stdout
    ):
        return "skip", "exact-pssm-capability-unsupported", 77
    if launch_status == "timeout":
        return "failure", "execution-timeout", 78
    if launch_status == "launch-error":
        return "failure", "execution-launch-error", 78
    if exit_code == 0:
        return "failure", "invalid-pass-observation", 78
    if exit_code == 77:
        return "failure", "invalid-skip-observation", 78
    return "failure", "child-nonzero-failure", 78


def validate_receipt(
    build_dir: Path, *, require_pass_or_skip: bool = False
) -> dict[str, Any]:
    root = build_dir.expanduser().resolve(strict=True)
    if not root.is_dir():
        raise ReceiptValidationError("build root is not a directory")
    contract_path = _regular_file(
        root, BUILD_CONTRACT_NAME, "build contract"
    )
    receipt_path = _regular_file(root, RECEIPT_NAME, "child receipt")
    build_contract = _read_json_object(contract_path, "build contract")
    receipt = _read_json_object(receipt_path, "child receipt")
    if build_contract.get("schema_version") != 6:
        raise ReceiptValidationError("child receipt requires build contract schema 6")
    components = build_contract.get("components")
    expected_scope = expected_receipt_scope(build_contract)
    if not isinstance(components, dict) or not (
        components.get("headless_child_bootstrap") is True
        and components.get("headless_child_output_name") == "RoR-OgreNext"
        and components.get("headless_child_execution_receipt_schema")
        == RECEIPT_SCHEMA
        and components.get("headless_child_execution_receipt_required") is True
        and components.get("headless_child_binary_retained") is True
        and components.get("headless_child_logs_retained") is True
        and components.get("headless_child_process_model")
        == RECEIPT_PROCESS_MODEL
    ):
        raise ReceiptValidationError("build contract child receipt policy is invalid")

    _require_exact_keys(
        receipt,
        {
            "schema",
            "schema_version",
            "scope",
            "outcome",
            "reason",
            "process",
            "provenance",
            "platform",
            "child_binary",
            "intent_contract",
            "execution",
        },
        "child receipt",
    )
    schema_version = receipt.get("schema_version")
    if (
        receipt.get("schema") != RECEIPT_SCHEMA
        or type(schema_version) is not int
        or schema_version != 1
    ):
        raise ReceiptValidationError("child receipt schema is invalid")
    if not _exact(receipt.get("scope"), expected_scope):
        raise ReceiptValidationError("child receipt scope is invalid")
    expected_provenance = _expected_provenance(root, build_contract)
    if not _exact(receipt.get("provenance"), expected_provenance):
        raise ReceiptValidationError("child receipt provenance mismatch")
    expected_platform = _expected_platform(build_contract)
    if not _exact(receipt.get("platform"), expected_platform):
        raise ReceiptValidationError("child receipt platform mismatch")

    child_relative = expected_child_relative(build_contract)
    final_child = _artifact_record(root, child_relative, "child binary")
    child_record = _require_exact_keys(
        receipt.get("child_binary"),
        {
            "path",
            "size_bytes",
            "sha256",
            "pre_execution_size_bytes",
            "pre_execution_sha256",
            "unchanged_during_execution",
        },
        "child binary record",
    )
    if not _exact(
        {key: child_record.get(key) for key in ("path", "size_bytes", "sha256")},
        final_child,
    ):
        raise ReceiptValidationError("child binary artifact binding mismatch")
    if not (
        _is_positive_int(child_record.get("pre_execution_size_bytes"))
        and _is_sha256(child_record.get("pre_execution_sha256"))
        and type(child_record.get("unchanged_during_execution")) is bool
    ):
        raise ReceiptValidationError("child binary pre-execution binding is invalid")
    unchanged = (
        child_record["pre_execution_size_bytes"] == final_child["size_bytes"]
        and child_record["pre_execution_sha256"] == final_child["sha256"]
    )
    if child_record["unchanged_during_execution"] is not unchanged:
        raise ReceiptValidationError("child binary mutation flag is inconsistent")

    intent = {
        "schema": INTENT_SCHEMA,
        "version": 1,
        "ordered_arguments": list(INTENT_ARGUMENTS),
    }
    if not _exact(receipt.get("intent_contract"), intent):
        raise ReceiptValidationError("child intent contract mismatch")
    process = _require_exact_keys(
        receipt.get("process"),
        {"launch_status", "exit_code", "wrapper_return_code"},
        "child process record",
    )
    launch_status = process.get("launch_status")
    exit_code = process.get("exit_code")
    if launch_status not in {"exited", "timeout", "launch-error"}:
        raise ReceiptValidationError("child launch status is invalid")
    if not (
        (launch_status == "exited" and type(exit_code) is int)
        or (launch_status in {"timeout", "launch-error"} and exit_code is None)
    ):
        raise ReceiptValidationError(
            "child launch status and exit code are contradictory"
        )
    if type(process.get("wrapper_return_code")) is not int:
        raise ReceiptValidationError("wrapper return code is invalid")

    execution = _require_exact_keys(
        receipt.get("execution"),
        {"execution_nonce", "nonce_policy", "timestamp_policy", "stdout", "stderr"},
        "child execution record",
    )
    if (
        not isinstance(execution.get("execution_nonce"), str)
        or re.fullmatch(r"[0-9a-f]{64}", execution["execution_nonce"]) is None
        or execution.get("nonce_policy") != NONCE_POLICY
        or execution.get("timestamp_policy") != TIMESTAMP_POLICY
    ):
        raise ReceiptValidationError("child nonce/timestamp policy is invalid")
    stdout_record = _artifact_record(
        root, STDOUT_LOG_NAME, "child stdout log", allow_empty=True
    )
    stderr_path = _regular_file(root, STDERR_LOG_NAME, "child stderr log")
    stderr_record = {
        "path": STDERR_LOG_NAME,
        "size_bytes": stderr_path.stat().st_size,
        "sha256": sha256_file(stderr_path),
    }
    # A successful run may have an empty stderr log; stdout is always nonempty.
    if not _exact(execution.get("stdout"), stdout_record) or not _exact(
        execution.get("stderr"), stderr_record
    ):
        raise ReceiptValidationError("child execution log binding mismatch")
    stdout = (root / STDOUT_LOG_NAME).read_bytes()
    stderr = stderr_path.read_bytes()
    expected_outcome, expected_reason, expected_wrapper_exit = classify_observation(
        expected_platform["policy"],
        launch_status,
        exit_code,
        stdout,
        stderr,
    )
    if not unchanged:
        expected_outcome = "failure"
        expected_reason = "binary-changed-during-execution"
        expected_wrapper_exit = 78
    if not (
        receipt.get("outcome") == expected_outcome
        and receipt.get("reason") == expected_reason
        and process.get("wrapper_return_code") == expected_wrapper_exit
    ):
        raise ReceiptValidationError("child receipt outcome classification mismatch")
    if require_pass_or_skip and expected_outcome not in {"pass", "skip"}:
        raise ReceiptValidationError("child execution receipt records failure")
    return receipt


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--require-pass-or-skip", action="store_true")
    args = parser.parse_args(argv)
    try:
        receipt = validate_receipt(
            args.build_dir, require_pass_or_skip=args.require_pass_or_skip
        )
    except (OSError, ReceiptValidationError) as error:
        print(str(error), file=sys.stderr)
        return 1
    print(
        json.dumps(
            {
                "schema": RECEIPT_SCHEMA,
                "status": "verified",
                "outcome": receipt["outcome"],
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
