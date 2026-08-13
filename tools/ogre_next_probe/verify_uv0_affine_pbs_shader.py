#!/usr/bin/env python3
"""Fail closed unless the project-owned UV0 affine PBS piece matches its lock."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re


LOCK_SCHEMA = "ror.ogre_next_uv0_affine_pbs_shader_lock.v1"
SHA256_PATTERN = re.compile(r"[0-9a-f]{64}")


def verify_shader(root: Path, lock_path: Path) -> str:
    resolved_root = root.resolve(strict=True)
    resolved_lock = lock_path.resolve(strict=True)
    if lock_path.is_symlink() or resolved_root not in resolved_lock.parents:
        raise ValueError("UV0 affine PBS shader lock is indirect or escaped the probe root")
    raw_lock = resolved_lock.read_text(encoding="ascii")
    lock = json.loads(raw_lock)
    canonical = json.dumps(lock, ensure_ascii=True, indent=2, sort_keys=True) + "\n"
    if raw_lock != canonical:
        raise ValueError("UV0 affine PBS shader lock is not canonical JSON")
    if lock.get("schema") != LOCK_SCHEMA:
        raise ValueError("unsupported UV0 affine PBS shader-lock schema")
    relative = lock.get("shader")
    expected = lock.get("sha256")
    if (
        not isinstance(relative, str)
        or not isinstance(expected, str)
        or SHA256_PATTERN.fullmatch(expected) is None
    ):
        raise ValueError("UV0 affine PBS shader lock is incomplete")
    shader_path = resolved_root / relative
    if shader_path.is_symlink():
        raise ValueError("UV0 affine PBS shader is indirect")
    shader = shader_path.resolve(strict=True)
    if resolved_root not in shader.parents:
        raise ValueError("UV0 affine PBS shader escaped the probe root")
    observed = hashlib.sha256(shader.read_bytes()).hexdigest()
    if observed != expected:
        raise ValueError(
            f"UV0 affine PBS shader digest mismatch: expected {expected}, got {observed}"
        )
    return observed


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--lock", type=Path, required=True)
    args = parser.parse_args()
    try:
        observed = verify_shader(args.root, args.lock)
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError) as error:
        raise SystemExit(str(error)) from error
    print(f"UV0 affine PBS shader verified: {observed}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
