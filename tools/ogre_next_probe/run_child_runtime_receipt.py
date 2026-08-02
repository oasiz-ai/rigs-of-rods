#!/usr/bin/env python3
"""Run the probe-only RoR-OgreNext child and atomically record evidence."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import secrets
import signal
import subprocess
import sys
import tempfile
from typing import Any


PROBE_ROOT = Path(__file__).resolve().parent
if str(PROBE_ROOT) not in sys.path:
    sys.path.insert(0, str(PROBE_ROOT))

from validate_child_runtime_receipt import (  # noqa: E402
    BUILD_CONTRACT_NAME,
    INTENT_ARGUMENTS,
    INTENT_SCHEMA,
    NONCE_POLICY,
    RECEIPT_NAME,
    RECEIPT_SCHEMA,
    RECEIPT_SCOPE,
    STDERR_LOG_NAME,
    STDOUT_LOG_NAME,
    TIMESTAMP_POLICY,
    ReceiptValidationError,
    _artifact_record,
    _expected_platform,
    _expected_provenance,
    _read_json_object,
    classify_observation,
    expected_child_relative,
    sha256_file,
    validate_receipt,
)


WRAPPER_FAILURE_EXIT_CODE = 78
DEFAULT_TIMEOUT_SECONDS = 110
POSIX_PROCESS_GROUPS = os.name == "posix"


class ChildReceiptRunnerError(RuntimeError):
    """Raised when the wrapper cannot produce independently valid evidence."""


def _remove_stale(path: Path) -> None:
    if path.is_symlink():
        raise ChildReceiptRunnerError(f"refusing stale symbolic output: {path.name}")
    if path.exists():
        if not path.is_file():
            raise ChildReceiptRunnerError(
                f"refusing stale non-file output: {path.name}"
            )
        path.unlink()


def _atomic_write(path: Path, payload: bytes) -> None:
    if path.is_symlink() or (path.exists() and not path.is_file()):
        raise ChildReceiptRunnerError(f"refusing indirect output: {path.name}")
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as output:
            output.write(payload)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
    except BaseException:
        try:
            temporary.unlink(missing_ok=True)
        except OSError:
            pass
        raise


def _captured_bytes(value: bytes | str | None) -> bytes:
    if value is None:
        return b""
    if isinstance(value, bytes):
        return value
    return value.encode("utf-8", errors="replace")


def _emit(stdout: bytes, stderr: bytes) -> None:
    if stdout:
        sys.stdout.buffer.write(stdout)
        sys.stdout.buffer.flush()
    if stderr:
        sys.stderr.buffer.write(stderr)
        sys.stderr.buffer.flush()


def _execute_child(
    command: list[str], timeout_seconds: int
) -> subprocess.CompletedProcess[bytes]:
    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        start_new_session=POSIX_PROCESS_GROUPS,
    )
    try:
        stdout, stderr = process.communicate(timeout=timeout_seconds)
    except subprocess.TimeoutExpired as error:
        if POSIX_PROCESS_GROUPS:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        else:
            process.kill()
        stdout, stderr = process.communicate()
        raise subprocess.TimeoutExpired(
            command,
            timeout_seconds,
            output=stdout if stdout is not None else error.stdout,
            stderr=stderr if stderr is not None else error.stderr,
        ) from error
    return subprocess.CompletedProcess(
        command,
        process.returncode,
        stdout=stdout,
        stderr=stderr,
    )


def run_child(
    build_dir: Path,
    child: Path,
    *,
    timeout_seconds: int = DEFAULT_TIMEOUT_SECONDS,
    emit_output: bool = True,
) -> int:
    root = build_dir.expanduser().resolve(strict=True)
    if not root.is_dir():
        raise ChildReceiptRunnerError("build root is not a directory")
    contract_path = root / BUILD_CONTRACT_NAME
    if contract_path.is_symlink() or not contract_path.is_file():
        raise ChildReceiptRunnerError("build contract is missing or indirect")
    build_contract = _read_json_object(contract_path, "build contract")
    expected_relative = expected_child_relative(build_contract)
    expected_child = root.joinpath(*expected_relative.split("/"))
    try:
        resolved_child = child.expanduser().resolve(strict=True)
    except OSError as error:
        raise ChildReceiptRunnerError(
            f"child binary is unavailable: {error}"
        ) from error
    if child.is_symlink() or resolved_child != expected_child:
        raise ChildReceiptRunnerError("child binary path does not match build policy")
    if not resolved_child.is_file() or resolved_child.stat().st_size <= 0:
        raise ChildReceiptRunnerError("child binary is missing or empty")
    if type(timeout_seconds) is not int or not 1 <= timeout_seconds <= 120:
        raise ChildReceiptRunnerError("child timeout is outside the reviewed bound")

    receipt_path = root / RECEIPT_NAME
    stdout_path = root / STDOUT_LOG_NAME
    stderr_path = root / STDERR_LOG_NAME
    for output in (receipt_path, stdout_path, stderr_path):
        _remove_stale(output)

    contract_size_before = contract_path.stat().st_size
    contract_sha_before = sha256_file(contract_path)
    pre_size = resolved_child.stat().st_size
    pre_sha256 = sha256_file(resolved_child)
    nonce = secrets.token_hex(32)
    if len(nonce) != 64:
        raise ChildReceiptRunnerError("CSPRNG did not return a 256-bit nonce")

    command = [str(resolved_child), *INTENT_ARGUMENTS]
    launch_status = "exited"
    exit_code: int | None = None
    try:
        completed = _execute_child(command, timeout_seconds)
        exit_code = completed.returncode
        stdout = _captured_bytes(completed.stdout)
        stderr = _captured_bytes(completed.stderr)
    except subprocess.TimeoutExpired as error:
        launch_status = "timeout"
        stdout = _captured_bytes(error.stdout)
        stderr = _captured_bytes(error.stderr)
        stderr += b"RoR Ogre-Next child wrapper: execution-timeout\n"
    except OSError as error:
        launch_status = "launch-error"
        stdout = b""
        stderr = (
            "RoR Ogre-Next child wrapper: execution-launch-error:"
            f"{type(error).__name__}\n"
        ).encode("ascii", errors="replace")

    if emit_output:
        _emit(stdout, stderr)
    if not resolved_child.is_file() or resolved_child.is_symlink():
        raise ChildReceiptRunnerError("child binary disappeared during execution")
    final_size = resolved_child.stat().st_size
    final_sha256 = sha256_file(resolved_child)
    if (
        contract_path.stat().st_size != contract_size_before
        or sha256_file(contract_path) != contract_sha_before
    ):
        raise ChildReceiptRunnerError("build contract changed during execution")

    _atomic_write(stdout_path, stdout)
    _atomic_write(stderr_path, stderr)
    outcome, reason, wrapper_return_code = classify_observation(
        build_contract["platform"]["policy"],
        launch_status,
        exit_code,
        stdout,
        stderr,
    )
    unchanged = final_size == pre_size and final_sha256 == pre_sha256
    if not unchanged:
        outcome = "failure"
        reason = "binary-changed-during-execution"
        wrapper_return_code = WRAPPER_FAILURE_EXIT_CODE

    receipt: dict[str, Any] = {
        "schema": RECEIPT_SCHEMA,
        "schema_version": 1,
        "scope": RECEIPT_SCOPE,
        "outcome": outcome,
        "reason": reason,
        "process": {
            "launch_status": launch_status,
            "exit_code": exit_code,
            "wrapper_return_code": wrapper_return_code,
        },
        "provenance": _expected_provenance(root, build_contract),
        "platform": _expected_platform(build_contract),
        "child_binary": {
            "path": expected_relative,
            "size_bytes": final_size,
            "sha256": final_sha256,
            "pre_execution_size_bytes": pre_size,
            "pre_execution_sha256": pre_sha256,
            "unchanged_during_execution": unchanged,
        },
        "intent_contract": {
            "schema": INTENT_SCHEMA,
            "version": 1,
            "ordered_arguments": list(INTENT_ARGUMENTS),
        },
        "execution": {
            "execution_nonce": nonce,
            "nonce_policy": NONCE_POLICY,
            "timestamp_policy": TIMESTAMP_POLICY,
            "stdout": _artifact_record(
                root, STDOUT_LOG_NAME, "child stdout log", allow_empty=True
            ),
            "stderr": {
                "path": STDERR_LOG_NAME,
                "size_bytes": stderr_path.stat().st_size,
                "sha256": sha256_file(stderr_path),
            },
        },
    }
    serialized = (
        json.dumps(receipt, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")
    _atomic_write(receipt_path, serialized)
    try:
        validate_receipt(root, require_pass_or_skip=False)
    except ReceiptValidationError as error:
        raise ChildReceiptRunnerError(
            f"new child receipt failed independent validation: {error}"
        ) from error
    return wrapper_return_code


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--child", type=Path, required=True)
    parser.add_argument(
        "--timeout-seconds",
        type=int,
        default=DEFAULT_TIMEOUT_SECONDS,
    )
    args = parser.parse_args(argv)
    try:
        return run_child(
            args.build_dir,
            args.child,
            timeout_seconds=args.timeout_seconds,
        )
    except (OSError, ChildReceiptRunnerError, ReceiptValidationError) as error:
        print(f"RoR Ogre-Next child receipt failure: {error}", file=sys.stderr)
        return WRAPPER_FAILURE_EXIT_CODE


if __name__ == "__main__":
    raise SystemExit(main())
