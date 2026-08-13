#!/usr/bin/env python3
"""Fail closed unless the V2 Metal shader matches its reviewed lock."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--lock", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve(strict=True)
    lock_path = args.lock.resolve(strict=True)
    lock = json.loads(lock_path.read_text(encoding="utf-8"))
    if lock.get("schema") != (
        "ror.ogre_next_metal_sun_visibility_v2_shader_lock.v1"
    ):
        raise SystemExit("unsupported V2 shader-lock schema")
    relative = lock.get("shader")
    expected = lock.get("sha256")
    if not isinstance(relative, str) or not isinstance(expected, str):
        raise SystemExit("V2 shader lock is incomplete")
    shader = (root / relative).resolve(strict=True)
    if root not in shader.parents:
        raise SystemExit("V2 shader escaped the probe root")
    observed = hashlib.sha256(shader.read_bytes()).hexdigest()
    if observed != expected:
        raise SystemExit(
            f"V2 Metal shader digest mismatch: expected {expected}, got {observed}"
        )
    print(f"V2 Metal shader verified: {observed}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
