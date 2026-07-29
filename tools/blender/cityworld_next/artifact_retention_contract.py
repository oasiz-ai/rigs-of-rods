#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Authenticate generated artifacts before retaining session-dependent bytes.

Blender project files and rendered PNG previews may contain session or encoder
metadata that is not part of the deterministic runtime asset contract.  A
generator may retain those checked bytes, but it must never silently certify
locally modified bytes by writing their new hashes into a manifest.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path, PurePosixPath
from typing import Any, Mapping


MAX_ARTIFACT_BYTES = 512 * 1024 * 1024
SHA256_HEX = frozenset("0123456789abcdef")


class ArtifactContractError(RuntimeError):
    """A checked artifact cannot be safely retained."""


def reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def is_sha256(value: Any) -> bool:
    return (
        isinstance(value, str)
        and len(value) == 64
        and set(value) <= SHA256_HEX
    )


def sha256_file(path: Path) -> str:
    if not path.is_file() or path.is_symlink():
        raise ArtifactContractError(
            f"required retained artifact is missing or unsafe: {path.name}"
        )
    size = path.stat().st_size
    if size > MAX_ARTIFACT_BYTES:
        raise ArtifactContractError(
            f"retained artifact exceeds {MAX_ARTIFACT_BYTES} bytes: {path.name}"
        )
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_previous_manifest(path: Path) -> dict[str, Any] | None:
    """Load an existing checked manifest without accepting malformed input."""

    if not path.exists():
        return None
    if not path.is_file() or path.is_symlink():
        raise ArtifactContractError(
            f"existing asset manifest is missing or unsafe: {path.name}"
        )
    try:
        document = json.loads(
            path.read_text(encoding="utf-8"),
            object_pairs_hook=reject_duplicate_keys,
            parse_constant=lambda token: (_ for _ in ()).throw(
                ValueError(f"non-finite JSON number: {token}")
            ),
        )
    except (OSError, UnicodeDecodeError, json.JSONDecodeError, ValueError) as error:
        raise ArtifactContractError(
            f"cannot authenticate existing asset manifest {path.name}: {error}"
        ) from error
    if not isinstance(document, dict):
        raise ArtifactContractError(
            f"existing asset manifest root must be an object: {path.name}"
        )
    return document


def generator_identity_matches(
    document: Mapping[str, Any],
    *,
    blender_version: str,
    generator: Mapping[str, Any],
) -> bool:
    """Return whether a manifest belongs to the exact active authoring identity."""

    authoring = document.get("authoring")
    if not isinstance(authoring, dict):
        return False
    previous_generator = authoring.get("generator")
    return (
        authoring.get("blender_version") == blender_version
        and isinstance(previous_generator, dict)
        and previous_generator == dict(generator)
    )


def _portable_relative(root: Path, path: Path) -> str:
    root = root.resolve()
    try:
        relative = path.resolve().relative_to(root).as_posix()
    except ValueError as error:
        raise ArtifactContractError(
            f"retained artifact escapes the repository root: {path}"
        ) from error
    parsed = PurePosixPath(relative)
    if (
        not relative
        or parsed.is_absolute()
        or "\\" in relative
        or any(part in ("", ".", "..") for part in parsed.parts)
    ):
        raise ArtifactContractError(
            f"retained artifact path is not portable: {relative}"
        )
    return relative


def authenticate_retained_artifacts(
    document: Mapping[str, Any],
    *,
    repo_root: Path,
    expected_paths: Mapping[str, Path],
) -> dict[str, str]:
    """Verify exact paths and bytes before any prior artifact may be retained."""

    artifacts = document.get("artifacts")
    if not isinstance(artifacts, dict):
        raise ArtifactContractError(
            "existing asset manifest has no authenticated artifact records"
        )

    authenticated: dict[str, str] = {}
    for role, expected_path in expected_paths.items():
        record = artifacts.get(role)
        if not isinstance(record, dict):
            raise ArtifactContractError(
                f"existing asset manifest has no {role} artifact record"
            )
        expected_relative = _portable_relative(repo_root, expected_path)
        if record.get("path") != expected_relative:
            raise ArtifactContractError(
                f"retained {role} path does not match the checked manifest"
            )
        expected_hash = record.get("sha256")
        if not is_sha256(expected_hash):
            raise ArtifactContractError(
                f"retained {role} artifact has no valid checked SHA-256"
            )
        actual_hash = sha256_file(expected_path)
        if actual_hash != expected_hash:
            raise ArtifactContractError(
                f"retained {role} artifact SHA-256 mismatch: "
                f"expected {expected_hash}, got {actual_hash}"
            )
        authenticated[role] = actual_hash
    return authenticated
