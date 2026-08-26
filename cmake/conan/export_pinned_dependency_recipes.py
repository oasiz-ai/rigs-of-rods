#!/usr/bin/env python3
"""Export the exact public RoR Conan recipes needed by clean native CI."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import tarfile
import urllib.request


UPSTREAM_COMMIT = "d3568327ff541d62052fa8a97cc71a4e3f126d89"
ARCHIVE_ROOT = f"ror-conan-recipes-{UPSTREAM_COMMIT}"
ARCHIVE_URL = (
    "https://codeload.github.com/AnotherFoxGuy/ror-conan-recipes/"
    f"tar.gz/{UPSTREAM_COMMIT}"
)
ARCHIVE_SHA256 = (
    "9489b03dad4a8d23ba8870c523874c6a16706fdd87cae8ea52ce768ac53f0932"
)
ARCHIVE_BYTES = 40_157
MAXIMUM_ARCHIVE_MEMBERS = 512
MAXIMUM_EXTRACTED_BYTES = 2 * 1024 * 1024

RECIPES = (
    (
        "discord-rpc/all",
        "3.4.0",
        "discord-rpc/3.4.0@anotherfoxguy/stable"
        "#a2905f22ab84faeceebe54e488ff9195",
    ),
    (
        "socketw/all",
        "3.11.0",
        "socketw/3.11.0@anotherfoxguy/stable"
        "#6630840d3f73fb6d6e60f6f88132d40a",
    ),
    (
        "freeimage/all",
        "3.18.0",
        "freeimage/3.18.0@anotherfoxguy/stable"
        "#8b69961fa00ad36b37d77dd40502fcbf",
    ),
    (
        "mygui/all",
        "3.4.0",
        "mygui/3.4.0@anotherfoxguy/stable"
        "#d544e344e389c9b287124fea8b567d01",
    ),
)


class RecipeExportError(RuntimeError):
    """The pinned public recipe transaction could not be completed."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_archive(path: Path) -> list[tarfile.TarInfo]:
    if not path.is_file():
        raise RecipeExportError(f"recipe archive is absent: {path}")
    if path.stat().st_size != ARCHIVE_BYTES:
        raise RecipeExportError(
            "recipe archive byte count changed: "
            f"expected {ARCHIVE_BYTES}, got {path.stat().st_size}"
        )
    observed_sha256 = sha256_file(path)
    if observed_sha256 != ARCHIVE_SHA256:
        raise RecipeExportError(
            "recipe archive SHA-256 changed: "
            f"expected {ARCHIVE_SHA256}, got {observed_sha256}"
        )

    with tarfile.open(path, mode="r:gz") as archive:
        members = archive.getmembers()
    if not members or len(members) > MAXIMUM_ARCHIVE_MEMBERS:
        raise RecipeExportError("recipe archive member count is invalid")

    names: set[str] = set()
    extracted_bytes = 0
    for member in members:
        name = member.name
        relative = PurePosixPath(name)
        if (
            not name
            or "\\" in name
            or relative.is_absolute()
            or ".." in relative.parts
            or not relative.parts
            or relative.parts[0] != ARCHIVE_ROOT
        ):
            raise RecipeExportError(
                f"recipe archive contains an unsafe path: {name!r}"
            )
        if name in names:
            raise RecipeExportError(
                f"recipe archive contains a duplicate path: {name!r}"
            )
        names.add(name)
        if not (member.isdir() or member.isfile()):
            raise RecipeExportError(
                f"recipe archive contains a non-file member: {name!r}"
            )
        if member.size < 0:
            raise RecipeExportError(
                f"recipe archive contains a negative size: {name!r}"
            )
        extracted_bytes += member.size
        if extracted_bytes > MAXIMUM_EXTRACTED_BYTES:
            raise RecipeExportError("recipe archive exceeds the extraction cap")
    return members


def acquire_archive(path: Path) -> None:
    if path.exists():
        validate_archive(path)
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.download")
    if temporary.exists():
        temporary.unlink()
    request = urllib.request.Request(
        ARCHIVE_URL,
        headers={"User-Agent": "Rigs-of-Rods-pinned-Conan-recipe-export"},
    )
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            with temporary.open("xb") as output:
                shutil.copyfileobj(response, output, length=1024 * 1024)
        validate_archive(temporary)
        os.replace(temporary, path)
    except Exception:
        if temporary.exists():
            temporary.unlink()
        raise


def extract_archive(path: Path, destination: Path) -> Path:
    members = validate_archive(path)
    destination.mkdir(parents=True, exist_ok=True)
    extracted_root = destination / ARCHIVE_ROOT
    if extracted_root.exists():
        shutil.rmtree(extracted_root)
    with tarfile.open(path, mode="r:gz") as archive:
        archive.extractall(destination, members=members, filter="data")
    if not extracted_root.is_dir():
        raise RecipeExportError("recipe archive did not publish its exact root")
    return extracted_root


def export_recipe(
    conan: str, recipe_root: Path, relative_path: str, version: str
) -> str:
    recipe_path = recipe_root / relative_path
    if not (recipe_path / "conanfile.py").is_file() or not (
        recipe_path / "conandata.yml"
    ).is_file():
        raise RecipeExportError(
            f"pinned recipe is incomplete: {relative_path}"
        )
    result = subprocess.run(
        [
            conan,
            "export",
            str(recipe_path),
            "--version",
            version,
            "--user",
            "anotherfoxguy",
            "--channel",
            "stable",
            "--format=json",
        ],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if result.returncode != 0:
        raise RecipeExportError(
            f"Conan export failed for {relative_path}: {result.stderr}"
        )
    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise RecipeExportError(
            f"Conan export emitted invalid JSON for {relative_path}"
        ) from error
    reference = payload.get("reference")
    if not isinstance(reference, str):
        raise RecipeExportError(
            f"Conan export omitted the reference for {relative_path}"
        )
    return reference


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--archive", required=True, type=Path)
    parser.add_argument("--extract-root", required=True, type=Path)
    parser.add_argument("--conan", default="conan")
    arguments = parser.parse_args()

    try:
        archive = arguments.archive.resolve()
        extract_root = arguments.extract_root.resolve()
        acquire_archive(archive)
        recipe_root = extract_archive(archive, extract_root)
        exported: list[str] = []
        for relative_path, version, expected_reference in RECIPES:
            reference = export_recipe(
                arguments.conan, recipe_root, relative_path, version
            )
            if reference != expected_reference:
                raise RecipeExportError(
                    f"recipe revision changed for {relative_path}: "
                    f"expected {expected_reference}, got {reference}"
                )
            exported.append(reference)
    except (OSError, RecipeExportError, tarfile.TarError) as error:
        print(f"pinned Conan recipe export failed: {error}", file=sys.stderr)
        return 1

    print(
        json.dumps(
            {
                "archive_bytes": ARCHIVE_BYTES,
                "archive_sha256": ARCHIVE_SHA256,
                "exported_references": exported,
                "source_commit": UPSTREAM_COMMIT,
                "status": "ok",
            },
            indent=2,
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
