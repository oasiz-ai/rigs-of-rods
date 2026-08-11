#!/usr/bin/env python3
"""Stage the exact authenticated build-tree media for RoR-Combined."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import tempfile


SCHEMA = "ror.ogre_next_combined_resource_manifest.v1"
COMPLETION_SCHEMA = "ror.ogre_next_combined_resource_stage.v1"
EXPECTED_OUTPUT_NAME = "ror-ogre-next-combined-resources"


def fail(message: str) -> None:
    raise SystemExit(f"combined resource stage rejected: {message}")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while True:
            chunk = stream.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def safe_relative(value: object) -> PurePosixPath:
    if not isinstance(value, str) or not value or "\\" in value:
        fail("manifest path is empty or non-canonical")
    path = PurePosixPath(value)
    if path.is_absolute() or any(part in ("", ".", "..") for part in path.parts):
        fail(f"manifest path escapes the stage: {value!r}")
    if path.parts[0] not in ("ShaderMedia", "Presentation"):
        fail(f"manifest path is outside an admitted media root: {value!r}")
    return path


def regular_direct_file(path: Path, description: str) -> None:
    try:
        status = path.lstat()
    except OSError as error:
        fail(f"{description} is unavailable: {error}")
    if not path.is_file() or path.is_symlink():
        fail(f"{description} is not a direct regular file: {path}")
    if status.st_size < 0:
        fail(f"{description} has an invalid size: {path}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--build-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    regular_direct_file(args.manifest, "resource manifest")
    build_root = args.build_root.resolve(strict=True)
    output = args.output.resolve(strict=False)
    if output.name != EXPECTED_OUTPUT_NAME or output.parent != build_root:
        fail("output must be the exact direct child of the configured build root")

    manifest_bytes = args.manifest.read_bytes()
    try:
        manifest = json.loads(manifest_bytes)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        fail(f"resource manifest is invalid JSON: {error}")
    if not isinstance(manifest, dict) or manifest.get("schema") != SCHEMA:
        fail("resource manifest schema changed")
    files = manifest.get("files")
    if not isinstance(files, list) or not files:
        fail("resource manifest file set is empty")

    validated: list[tuple[PurePosixPath, Path, int, str]] = []
    previous = ""
    for entry in files:
        if not isinstance(entry, dict) or set(entry) != {
            "path",
            "source",
            "size",
            "sha256",
        }:
            fail("resource manifest entry shape changed")
        relative = safe_relative(entry["path"])
        relative_text = relative.as_posix()
        if relative_text <= previous:
            fail("resource manifest paths are duplicate or not strictly sorted")
        previous = relative_text
        source_value = entry["source"]
        expected_size = entry["size"]
        expected_sha256 = entry["sha256"]
        if not isinstance(source_value, str) or not os.path.isabs(source_value):
            fail(f"resource source is not absolute: {source_value!r}")
        if not isinstance(expected_size, int) or isinstance(expected_size, bool) or expected_size < 0:
            fail(f"resource size is invalid: {relative_text}")
        if (
            not isinstance(expected_sha256, str)
            or len(expected_sha256) != 64
            or any(character not in "0123456789abcdef" for character in expected_sha256)
        ):
            fail(f"resource SHA-256 is invalid: {relative_text}")
        source = Path(source_value)
        regular_direct_file(source, f"resource source {relative_text}")
        if source.stat().st_size != expected_size or sha256(source) != expected_sha256:
            fail(f"resource source bytes changed: {relative_text}")
        validated.append((relative, source, expected_size, expected_sha256))

    temporary = Path(tempfile.mkdtemp(prefix=".ror-ogre-next-combined-stage-", dir=build_root))
    backup = build_root / ".ror-ogre-next-combined-resources.previous"
    try:
        for relative, source, expected_size, expected_sha256 in validated:
            destination = temporary.joinpath(*relative.parts)
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, destination)
            regular_direct_file(destination, f"staged resource {relative.as_posix()}")
            if destination.stat().st_size != expected_size or sha256(destination) != expected_sha256:
                fail(f"staged resource verification failed: {relative.as_posix()}")

        completion = {
            "schema": COMPLETION_SCHEMA,
            "manifest_sha256": hashlib.sha256(manifest_bytes).hexdigest(),
            "file_count": len(validated),
            "shader_media_root": str(output / "ShaderMedia"),
            "presentation_media_root": str(output / "Presentation"),
        }
        (temporary / ".complete.json").write_text(
            json.dumps(completion, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        if backup.exists():
            shutil.rmtree(backup)
        if output.exists():
            output.rename(backup)
        temporary.rename(output)
        if backup.exists():
            shutil.rmtree(backup)
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
