#!/usr/bin/env python3
"""Verify the pinned Ogre-Next HDR numerical-reference source closure."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path, PurePosixPath
from typing import Any


_SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
_COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")


class VerificationError(RuntimeError):
    pass


def _load_object(path: Path, label: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise VerificationError(f"cannot read {label} {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise VerificationError(f"{label} root must be a JSON object")
    return value


def _exact_int(value: Any, field: str) -> int:
    if type(value) is not int:
        raise VerificationError(f"{field} must be an integer")
    return value


def _exact_string(value: Any, field: str) -> str:
    if type(value) is not str or not value:
        raise VerificationError(f"{field} must be a nonempty string")
    return value


def _safe_relative_path(value: Any, field: str) -> PurePosixPath:
    text = _exact_string(value, field)
    path = PurePosixPath(text)
    if path.is_absolute() or ".." in path.parts or "." in path.parts:
        raise VerificationError(f"{field} must be a normalized relative path")
    if path.as_posix() != text:
        raise VerificationError(f"{field} must use canonical POSIX separators")
    return path


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
    except OSError as exc:
        raise VerificationError(f"cannot hash {path}: {exc}") from exc
    return digest.hexdigest()


def verify(manifest_path: Path, ogre_source_root: Path | None = None) -> int:
    manifest_path = manifest_path.resolve(strict=True)
    manifest = _load_object(manifest_path, "HDR reference lock")
    if _exact_int(manifest.get("schema_version"), "schema_version") != 1:
        raise VerificationError("unsupported HDR reference lock schema_version")
    _exact_string(manifest.get("name"), "name")
    if _exact_int(
        manifest.get("analytic_behavior_version"), "analytic_behavior_version"
    ) != 1:
        raise VerificationError("unsupported analytic_behavior_version")
    if _exact_int(
        manifest.get("shader_behavior_version"), "shader_behavior_version"
    ) != 1:
        raise VerificationError("unsupported shader_behavior_version")

    canonical_name = _safe_relative_path(
        manifest.get("canonical_ogre_next_lock"), "canonical_ogre_next_lock"
    )
    if len(canonical_name.parts) != 1:
        raise VerificationError("canonical_ogre_next_lock must be a sibling file")
    canonical_path = manifest_path.parent / canonical_name.as_posix()
    canonical = _load_object(canonical_path, "canonical Ogre-Next lock")
    canonical_commit = _exact_string(canonical.get("commit"), "canonical commit")
    manifest_commit = _exact_string(
        manifest.get("ogre_next_commit"), "ogre_next_commit"
    )
    if not _COMMIT_RE.fullmatch(canonical_commit):
        raise VerificationError("canonical commit is not lowercase 40-hex")
    if manifest_commit != canonical_commit:
        raise VerificationError(
            "HDR reference commit does not match canonical ogre-next.lock.json"
        )

    files = manifest.get("files")
    if not isinstance(files, list) or not files:
        raise VerificationError("files must be a nonempty array")
    seen_roles: set[str] = set()
    seen_paths: set[str] = set()
    parsed_files: list[tuple[str, str, str]] = []
    for index, entry in enumerate(files):
        field = f"files[{index}]"
        if not isinstance(entry, dict):
            raise VerificationError(f"{field} must be an object")
        if set(entry) != {"role", "path", "sha256"}:
            raise VerificationError(f"{field} has unexpected or missing fields")
        role = _exact_string(entry.get("role"), f"{field}.role")
        source_path = _safe_relative_path(entry.get("path"), f"{field}.path")
        digest = _exact_string(entry.get("sha256"), f"{field}.sha256")
        if not _SHA256_RE.fullmatch(digest):
            raise VerificationError(f"{field}.sha256 must be lowercase 64-hex")
        source_text = source_path.as_posix()
        if role in seen_roles:
            raise VerificationError(f"duplicate source role: {role}")
        if source_text in seen_paths:
            raise VerificationError(f"duplicate source path: {source_text}")
        seen_roles.add(role)
        seen_paths.add(source_text)
        parsed_files.append((role, source_text, digest))

    if ogre_source_root is not None:
        source_root = ogre_source_root.resolve(strict=True)
        if not source_root.is_dir():
            raise VerificationError("Ogre-Next source root must be a directory")
        for _, source_text, expected_digest in parsed_files:
            candidate = (source_root / source_text).resolve(strict=True)
            if source_root not in candidate.parents:
                raise VerificationError(f"source path escapes root: {source_text}")
            if not candidate.is_file():
                raise VerificationError(f"source path is not a file: {source_text}")
            actual_digest = _sha256(candidate)
            if actual_digest != expected_digest:
                raise VerificationError(
                    f"source hash mismatch for {source_text}: "
                    f"expected {expected_digest}, got {actual_digest}"
                )

    return len(parsed_files)


def _parse_args(argv: list[str]) -> argparse.Namespace:
    repository_root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--manifest",
        type=Path,
        default=repository_root
        / "tools"
        / "ogre_next_probe"
        / "ogre-next-hdr-reference.lock.json",
    )
    parser.add_argument("--ogre-source-root", type=Path)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = _parse_args(sys.argv[1:] if argv is None else argv)
    try:
        count = verify(args.manifest, args.ogre_source_root)
    except (OSError, VerificationError) as exc:
        print(f"HDR reference source verification failed: {exc}", file=sys.stderr)
        return 1
    mode = "source hashes verified" if args.ogre_source_root else "metadata verified"
    print(f"HDR reference {mode}: {count} files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
