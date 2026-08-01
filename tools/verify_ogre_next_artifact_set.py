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


class ArtifactSetError(RuntimeError):
    """Raised when a required artifact is missing, empty, or indirect."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify_artifact_set(build_dir: Path) -> list[dict[str, object]]:
    root = build_dir.expanduser().resolve()
    failures: list[str] = []
    manifest: list[dict[str, object]] = []
    for name in REQUIRED_ARTIFACTS:
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
    return manifest


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        manifest = verify_artifact_set(args.build_dir)
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
