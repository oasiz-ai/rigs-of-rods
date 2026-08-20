#!/usr/bin/env python3
"""Fail closed unless every aerial-haze media file matches its reviewed lock.

The runtime media manifest already refuses to initialize on a byte change, but
that gate lives downstream of configure. This script is the configure-time
sibling: it authenticates the RoR-owned haze material and its three shader
siblings before CMake ever embeds their digests, so a local edit cannot slip
into a generated manifest unreviewed.
"""

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
    if lock.get("schema") != "ror.ogre_next_aerial_haze_media_lock.v1":
        raise SystemExit("unsupported aerial-haze media-lock schema")
    files = lock.get("files")
    if not isinstance(files, list) or not files:
        raise SystemExit("aerial-haze media lock is empty")
    for entry in files:
        if not isinstance(entry, dict):
            raise SystemExit("aerial-haze media lock entry is malformed")
        relative = entry.get("path")
        expected = entry.get("sha256")
        if not isinstance(relative, str) or not isinstance(expected, str):
            raise SystemExit("aerial-haze media lock entry is incomplete")
        shader = (root / relative).resolve(strict=True)
        if root not in shader.parents:
            raise SystemExit(
                f"aerial-haze media file escaped the probe root: {relative}"
            )
        observed = hashlib.sha256(shader.read_bytes()).hexdigest()
        if observed != expected:
            raise SystemExit(
                f"aerial-haze media digest mismatch for {relative}: "
                f"expected {expected}, got {observed}"
            )
    print(f"aerial-haze media verified: {len(files)} files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
