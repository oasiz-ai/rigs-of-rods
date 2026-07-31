#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build and compare storefront outputs from two artifact-free roots.

This gate copies only the project-authored generator/compiler sources needed to
create the storefront family.  It never seeds either root with checked
``.blend``, preview, GLB, manifest, compile-report, material, ODEF, or mesh
artifacts.
"""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from typing import Any


PINNED_BLENDER_VERSION = "Blender 5.2.0 LTS"
PINNED_CONVERTER_VERSION = "OgreXMLConverter Tsathoggua (14.5.2)"
EXPECTED_VARIANTS = 5
EXPECTED_OUTPUTS = 35
DEFAULT_GENERATION_TIMEOUT_SECONDS = 600
FAMILY_RELATIVE = Path(
    "content-source/cityworld_next/buildings/storefront_family/"
    "rorng_city_storefront_family.v1.json"
)
AUTHORING_INPUTS = (
    Path("tools/blender/cityworld_next/artifact_retention_contract.py"),
    Path("tools/blender/cityworld_next/canonicalize_static_glb.py"),
    Path("tools/blender/cityworld_next/generate_bridge_kit.py"),
    Path("tools/blender/cityworld_next/generate_cityworld_storefront_family.py"),
    Path("tools/compile_cityworld_asset.py"),
    Path("tools/validate_cityworld_asset.py"),
)
GENERATOR_RELATIVE = Path(
    "tools/blender/cityworld_next/generate_cityworld_storefront_family.py"
)
COMPILER_RELATIVE = Path("tools/compile_cityworld_asset.py")
COMPARATOR_RELATIVE = Path(
    "tools/compare_cityworld_storefront_reproducibility.py"
)


class CleanReproducibilityFailure(RuntimeError):
    pass


def regular_file(path: Path, *, label: str) -> Path:
    if not path.is_file() or path.is_symlink():
        raise CleanReproducibilityFailure(
            f"{label} is missing or unsafe: {path}"
        )
    return path.resolve()


def run_checked(
    command: list[str],
    *,
    cwd: Path,
    label: str,
    timeout: int,
) -> subprocess.CompletedProcess[str]:
    environment = os.environ.copy()
    environment["PYTHONDONTWRITEBYTECODE"] = "1"
    try:
        result = subprocess.run(
            command,
            cwd=cwd,
            env=environment,
            check=False,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
    except (OSError, subprocess.SubprocessError) as error:
        raise CleanReproducibilityFailure(f"{label} could not run: {error}") from error
    if result.returncode != 0:
        output = (result.stdout + "\n" + result.stderr).strip()
        raise CleanReproducibilityFailure(
            f"{label} failed with exit {result.returncode}: {output[-8000:]}"
        )
    return result


def verify_toolchain(blender: Path, converter: Path, repo_root: Path) -> None:
    blender_result = run_checked(
        [str(blender), "--version"],
        cwd=repo_root,
        label="Blender version probe",
        timeout=30,
    )
    blender_lines = blender_result.stdout.splitlines()
    if not blender_lines or blender_lines[0].strip() != PINNED_BLENDER_VERSION:
        received = blender_lines[0].strip() if blender_lines else ""
        raise CleanReproducibilityFailure(
            f"Blender version is not pinned: expected "
            f"{PINNED_BLENDER_VERSION!r}, received {received!r}"
        )

    converter_result = run_checked(
        [str(converter), "-v"],
        cwd=repo_root,
        label="OgreXMLConverter version probe",
        timeout=30,
    )
    received_converter = converter_result.stdout.strip()
    if received_converter != PINNED_CONVERTER_VERSION:
        raise CleanReproducibilityFailure(
            f"OgreXMLConverter version is not pinned: expected "
            f"{PINNED_CONVERTER_VERSION!r}, received {received_converter!r}"
        )


def prepare_artifact_free_root(repo_root: Path, target: Path) -> None:
    """Populate an empty root with authored toolchain inputs and nothing else."""

    if not target.is_dir() or target.is_symlink():
        raise CleanReproducibilityFailure(
            f"clean build root is missing or unsafe: {target}"
        )
    if any(target.iterdir()):
        raise CleanReproducibilityFailure(
            f"clean build root is not artifact-free: {target}"
        )

    (target / ".git").mkdir()
    for relative in AUTHORING_INPUTS:
        source = regular_file(repo_root / relative, label="authored input")
        destination = target / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)

    for forbidden in ("content-source", "resources"):
        if (target / forbidden).exists():
            raise CleanReproducibilityFailure(
                f"generated artifact tree was copied into the clean root: {forbidden}"
            )


def load_family_manifests(build_root: Path) -> list[Path]:
    family_path = build_root / FAMILY_RELATIVE
    if not family_path.is_file() or family_path.is_symlink():
        raise CleanReproducibilityFailure(
            f"generator did not emit the family contract: {family_path}"
        )
    try:
        family = json.loads(family_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise CleanReproducibilityFailure(
            f"generated family contract is unreadable: {error}"
        ) from error
    variants = family.get("variants") if isinstance(family, dict) else None
    if not isinstance(variants, list) or len(variants) != EXPECTED_VARIANTS:
        raise CleanReproducibilityFailure(
            f"generator emitted an unexpected variant count: "
            f"{len(variants) if isinstance(variants, list) else 'invalid'}"
        )

    manifests: list[Path] = []
    for index, variant in enumerate(variants):
        relative = variant.get("manifest") if isinstance(variant, dict) else None
        if not isinstance(relative, str) or not relative:
            raise CleanReproducibilityFailure(
                f"generated variant {index} has no manifest"
            )
        manifest_candidate = build_root / relative
        if not manifest_candidate.is_file() or manifest_candidate.is_symlink():
            raise CleanReproducibilityFailure(
                f"generated variant {index} manifest is missing or unsafe"
            )
        manifest = manifest_candidate.resolve()
        try:
            manifest.relative_to(build_root.resolve())
        except ValueError as error:
            raise CleanReproducibilityFailure(
                f"generated variant {index} manifest escapes the clean root"
            ) from error
        manifests.append(manifest)
    if len(set(manifests)) != EXPECTED_VARIANTS:
        raise CleanReproducibilityFailure(
            "generator repeated a storefront variant manifest"
        )
    return manifests


def build_clean_root(
    build_root: Path,
    *,
    blender: Path,
    converter: Path,
    generation_timeout: int,
) -> dict[str, Any]:
    run_checked(
        [
            str(blender),
            "--background",
            "--factory-startup",
            "--python-exit-code",
            "1",
            "--python",
            str(build_root / GENERATOR_RELATIVE),
            "--",
            "--output-root",
            str(build_root),
        ],
        cwd=build_root,
        label=f"Blender generation in {build_root.name}",
        timeout=generation_timeout,
    )
    manifests = load_family_manifests(build_root)
    for manifest in manifests:
        run_checked(
            [
                sys.executable,
                str(build_root / COMPILER_RELATIVE),
                str(manifest),
                "--repo-root",
                str(build_root),
                "--converter",
                str(converter),
            ],
            cwd=build_root,
            label=f"OGRE compilation of {manifest.name}",
            timeout=180,
        )
    return {
        "manifests": len(manifests),
        "root": build_root.name,
    }


def compare_clean_roots(
    repo_root: Path,
    left_root: Path,
    right_root: Path,
) -> dict[str, Any]:
    comparator = regular_file(
        repo_root / COMPARATOR_RELATIVE,
        label="storefront comparator",
    )
    result = run_checked(
        [
            sys.executable,
            str(comparator),
            "--left-root",
            str(left_root),
            "--right-root",
            str(right_root),
        ],
        cwd=repo_root,
        label="clean-root storefront comparison",
        timeout=120,
    )
    try:
        report = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise CleanReproducibilityFailure(
            f"storefront comparator emitted invalid JSON: {error}"
        ) from error
    if (
        not isinstance(report, dict)
        or report.get("valid") is not True
        or report.get("variants") != EXPECTED_VARIANTS
        or report.get("outputs") != EXPECTED_OUTPUTS
        or report.get("mismatches") != []
    ):
        raise CleanReproducibilityFailure(
            "storefront comparator did not prove 5 variants and 35 matching "
            f"outputs: {json.dumps(report, ensure_ascii=True, sort_keys=True)}"
        )
    return report


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=Path.cwd(),
    )
    parser.add_argument("--blender", type=Path, required=True)
    parser.add_argument("--converter", type=Path, required=True)
    parser.add_argument(
        "--generation-timeout",
        type=int,
        default=DEFAULT_GENERATION_TIMEOUT_SECONDS,
        help="maximum seconds allowed for each clean Blender generation",
    )
    args = parser.parse_args()
    if args.generation_timeout <= 0:
        parser.error("--generation-timeout must be positive")
    return args


def main() -> int:
    args = parse_args()
    repo_root = args.repo_root.resolve()
    try:
        blender = regular_file(args.blender, label="Blender executable")
        converter = regular_file(
            args.converter,
            label="OgreXMLConverter executable",
        )
        regular_file(
            repo_root / GENERATOR_RELATIVE,
            label="storefront generator",
        )
        regular_file(
            repo_root / COMPILER_RELATIVE,
            label="CityWorld compiler",
        )
        verify_toolchain(blender, converter, repo_root)
        with (
            tempfile.TemporaryDirectory(
                prefix="ror-storefront-clean-a-"
            ) as left_directory,
            tempfile.TemporaryDirectory(
                prefix="ror-storefront-clean-b-"
            ) as right_directory,
        ):
            left_root = Path(left_directory)
            right_root = Path(right_directory)
            prepare_artifact_free_root(repo_root, left_root)
            prepare_artifact_free_root(repo_root, right_root)
            with ThreadPoolExecutor(
                max_workers=2,
                thread_name_prefix="storefront-clean-build",
            ) as executor:
                builds = [
                    executor.submit(
                        build_clean_root,
                        build_root,
                        blender=blender,
                        converter=converter,
                        generation_timeout=args.generation_timeout,
                    )
                    for build_root in (left_root, right_root)
                ]
                for build in builds:
                    build.result()
            report = compare_clean_roots(repo_root, left_root, right_root)
    except (CleanReproducibilityFailure, OSError) as error:
        sys.stderr.write(f"storefront clean reproducibility failed: {error}\n")
        return 1

    sys.stdout.write(
        json.dumps(
            report,
            ensure_ascii=True,
            sort_keys=True,
            separators=(",", ":"),
        )
        + "\n"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
