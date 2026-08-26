#!/usr/bin/env python3
"""Build and run the isolated native Ogre-Next NiceMetal readback proof."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
REPOSITORY_ROOT = SCRIPT_DIR.parents[2]
LOCK_PATH = SCRIPT_DIR / "ogre-next-nicemetal-visible-v1.lock.json"
VERIFY_PATH = SCRIPT_DIR / "verify_nicemetal_visible.py"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run(command: list[str], *, capture: bool = False) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=REPOSITORY_ROOT,
        check=True,
        text=True,
        stdout=subprocess.PIPE if capture else None,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ogre-build-root", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--evidence-dir", type=Path, required=True)
    arguments = parser.parse_args()

    build_root = arguments.ogre_build_root.resolve(strict=True)
    build_dir = arguments.build_dir.resolve()
    evidence_dir = arguments.evidence_dir.resolve()
    build_dir.mkdir(parents=True, exist_ok=True)
    evidence_dir.mkdir(parents=True, exist_ok=True)

    run([sys.executable, str(VERIFY_PATH), "--lock", str(LOCK_PATH),
         "--ogre-build-root", str(build_root)])
    run([
        "cmake", "-S", str(SCRIPT_DIR), "-B", str(build_dir), "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_OSX_ARCHITECTURES=arm64",
        f"-DOGRE_NEXT_PROBE_BUILD_ROOT={build_root}",
    ])
    run(["cmake", "--build", str(build_dir), "--target",
         "ror_ogre_next_nicemetal_visible_probe", "-j", "2"])

    binary = build_dir / "ror_ogre_next_nicemetal_visible_probe"
    image = evidence_dir / "nicemetal-visible-readback.ppm"
    report_path = evidence_dir / "nicemetal-visible-receipt.json"
    execution = run([str(binary), "--output", str(image)], capture=True)
    lines = [line for line in execution.stdout.splitlines() if line.strip()]
    if not lines:
        raise RuntimeError("native proof emitted no receipt")
    report = json.loads(lines[-1])
    lock = json.loads(LOCK_PATH.read_text(encoding="utf-8"))
    report["evidence"] = {
        "lock_sha256": sha256(LOCK_PATH),
        "source_closure_sha256": lock["source_closure_sha256"],
        "frame_sha256": sha256(image),
        "binary_sha256": sha256(binary),
        "ogre_next_commit": lock["ogre_next"]["commit"],
        "ogre_next_archive_sha256": lock["ogre_next"]["archive_sha256"],
    }
    report_path.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    run([
        sys.executable, str(VERIFY_PATH), "--lock", str(LOCK_PATH),
        "--ogre-build-root", str(build_root), "--report", str(report_path),
        "--image", str(image), "--binary", str(binary),
    ])
    print(report_path.read_text(encoding="utf-8"), end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
