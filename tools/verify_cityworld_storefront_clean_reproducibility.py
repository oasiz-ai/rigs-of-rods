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
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
from typing import Any


PINNED_BLENDER_VERSION = "Blender 5.2.0 LTS"
PINNED_CONVERTER_VERSION = "OgreXMLConverter Tsathoggua (14.5.2)"
EXPECTED_VARIANTS = 5
EXPECTED_OUTPUTS = 35
DEFAULT_GENERATION_TIMEOUT_SECONDS = 600
DEFAULT_GENERATION_WORKERS = 1
DIAGNOSTIC_FORMAT = "ror-cityworld-storefront-clean-failure-v1"
DIAGNOSTIC_GLB_GLOB = (
    "resources/nextgen/cityworld/buildings/storefront_family/*/*.glb"
)
MAX_DIAGNOSTIC_GLB_BYTES = 16 * 1024 * 1024
MAX_DIAGNOSTIC_TOTAL_BYTES = 96 * 1024 * 1024
FAMILY_RELATIVE = Path(
    "content-source/cityworld_next/buildings/storefront_family/"
    "rorng_city_storefront_family.v1.json"
)
AUTHORING_INPUTS = (
    Path("tools/blender/cityworld_next/artifact_retention_contract.py"),
    Path("tools/blender/cityworld_next/canonicalize_static_glb.py"),
    Path("tools/blender/cityworld_next/canonicalize_storefront_glb.py"),
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


def first_byte_difference(left: bytes, right: bytes) -> int | None:
    """Return the first differing byte, including a length-only difference."""

    for offset, (left_byte, right_byte) in enumerate(zip(left, right)):
        if left_byte != right_byte:
            return offset
    if len(left) != len(right):
        return min(len(left), len(right))
    return None


def parse_glb_for_diagnostics(path: Path) -> tuple[dict[str, Any], bytes, int]:
    """Read the JSON document and binary payload from a generated GLB."""

    contents = path.read_bytes()
    if len(contents) < 28:
        raise CleanReproducibilityFailure(f"diagnostic GLB is truncated: {path}")
    magic, version, declared_length = struct.unpack_from("<4sII", contents, 0)
    if magic != b"glTF" or version != 2 or declared_length != len(contents):
        raise CleanReproducibilityFailure(f"diagnostic GLB header is invalid: {path}")
    json_length, json_type = struct.unpack_from("<II", contents, 12)
    if json_type != 0x4E4F534A:
        raise CleanReproducibilityFailure(
            f"diagnostic GLB has no JSON chunk: {path}"
        )
    json_start = 20
    json_end = json_start + json_length
    if json_end + 8 > len(contents):
        raise CleanReproducibilityFailure(
            f"diagnostic GLB JSON chunk is truncated: {path}"
        )
    try:
        document = json.loads(contents[json_start:json_end].rstrip(b" \x00"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise CleanReproducibilityFailure(
            f"diagnostic GLB JSON is unreadable: {path}: {error}"
        ) from error
    if not isinstance(document, dict):
        raise CleanReproducibilityFailure(
            f"diagnostic GLB JSON is not an object: {path}"
        )
    binary_length, binary_type = struct.unpack_from("<II", contents, json_end)
    if binary_type != 0x004E4942:
        raise CleanReproducibilityFailure(
            f"diagnostic GLB has no binary chunk: {path}"
        )
    binary_start = json_end + 8
    binary_end = binary_start + binary_length
    if binary_end != len(contents):
        raise CleanReproducibilityFailure(
            f"diagnostic GLB binary chunk is truncated: {path}"
        )
    return document, contents[binary_start:binary_end], binary_start


def first_document_difference(
    left: Any,
    right: Any,
    *,
    path: str = "$",
) -> dict[str, Any] | None:
    """Return a compact JSON-safe description of the first semantic drift."""

    if type(left) is not type(right):
        return {
            "left": repr(type(left).__name__),
            "path": path,
            "right": repr(type(right).__name__),
        }
    if isinstance(left, dict):
        left_keys = set(left)
        right_keys = set(right)
        if left_keys != right_keys:
            return {
                "left": sorted(str(key) for key in left_keys - right_keys),
                "path": path + ".<keys>",
                "right": sorted(str(key) for key in right_keys - left_keys),
            }
        for key in sorted(left):
            difference = first_document_difference(
                left[key],
                right[key],
                path=f"{path}.{key}",
            )
            if difference is not None:
                return difference
        return None
    if isinstance(left, list):
        if len(left) != len(right):
            return {
                "left": len(left),
                "path": path + ".<length>",
                "right": len(right),
            }
        for index, (left_value, right_value) in enumerate(zip(left, right)):
            difference = first_document_difference(
                left_value,
                right_value,
                path=f"{path}[{index}]",
            )
            if difference is not None:
                return difference
        return None
    if left != right:
        return {"left": left, "path": path, "right": right}
    return None


def validate_diagnostics_destination(
    destination: Path,
    allowed_root: Path,
) -> Path:
    """Admit an empty destination below one real, non-symlink directory."""

    if not destination.is_absolute() or not allowed_root.is_absolute():
        raise CleanReproducibilityFailure(
            "diagnostics paths must be absolute after CLI normalization"
        )
    if ".." in destination.parts:
        raise CleanReproducibilityFailure(
            f"diagnostics destination contains parent traversal: {destination}"
        )
    if not allowed_root.is_dir() or allowed_root.is_symlink():
        raise CleanReproducibilityFailure(
            f"diagnostics root is missing or unsafe: {allowed_root}"
        )
    allowed_resolved = allowed_root.resolve()
    destination_parent_resolved = destination.parent.resolve(strict=False)
    try:
        destination.relative_to(allowed_root)
        destination_parent_resolved.relative_to(allowed_resolved)
    except ValueError as error:
        raise CleanReproducibilityFailure(
            f"diagnostics destination escapes its root: {destination}"
        ) from error
    if destination == allowed_root:
        raise CleanReproducibilityFailure(
            "diagnostics destination must be below its root"
        )

    current = allowed_root
    relative_parent = destination.parent.relative_to(allowed_root)
    for component in relative_parent.parts:
        current /= component
        if current.is_symlink():
            raise CleanReproducibilityFailure(
                f"diagnostics destination has a symlinked parent: {current}"
            )
        if current.exists() and not current.is_dir():
            raise CleanReproducibilityFailure(
                f"diagnostics destination parent is not a directory: {current}"
            )

    if destination.is_symlink():
        raise CleanReproducibilityFailure(
            f"diagnostics destination is unsafe: {destination}"
        )
    if destination.exists():
        if not destination.is_dir():
            raise CleanReproducibilityFailure(
                f"diagnostics destination is unsafe: {destination}"
            )
        if any(destination.iterdir()):
            raise CleanReproducibilityFailure(
                f"diagnostics destination is not empty: {destination}"
            )
    return destination


def collect_failure_diagnostics(
    left_root: Path,
    right_root: Path,
    destination: Path,
    *,
    allowed_root: Path,
    error: BaseException,
) -> dict[str, Any]:
    """Persist only bounded GLB evidence before temporary roots are removed."""

    destination = validate_diagnostics_destination(destination, allowed_root)

    roots = {"left": left_root, "right": right_root}
    source_paths: dict[str, dict[str, Path]] = {}
    aggregate_bytes = 0
    for side, root in roots.items():
        root_resolved = root.resolve()
        candidates = sorted(root.glob(DIAGNOSTIC_GLB_GLOB))
        if len(candidates) != EXPECTED_VARIANTS:
            raise CleanReproducibilityFailure(
                f"diagnostic {side} root has {len(candidates)} GLBs, "
                f"expected {EXPECTED_VARIANTS}"
            )
        source_paths[side] = {}
        for candidate in candidates:
            source = regular_file(candidate, label=f"diagnostic {side} GLB")
            try:
                source.relative_to(root_resolved)
            except ValueError as error:
                raise CleanReproducibilityFailure(
                    f"diagnostic {side} GLB escapes its clean root: {candidate}"
                ) from error
            size = source.stat().st_size
            if size <= 0 or size > MAX_DIAGNOSTIC_GLB_BYTES:
                raise CleanReproducibilityFailure(
                    f"diagnostic {side} GLB size is outside the bounded "
                    f"profile: {candidate}: {size} bytes"
                )
            aggregate_bytes += size
            if aggregate_bytes > MAX_DIAGNOSTIC_TOTAL_BYTES:
                raise CleanReproducibilityFailure(
                    "diagnostic GLB evidence exceeds the aggregate byte limit"
                )
            asset_id = source.stem
            if asset_id in source_paths[side]:
                raise CleanReproducibilityFailure(
                    f"diagnostic {side} root repeats asset {asset_id}"
                )
            source_paths[side][asset_id] = source

    asset_ids = sorted(source_paths["left"])
    if asset_ids != sorted(source_paths["right"]):
        raise CleanReproducibilityFailure(
            "diagnostic roots do not contain the same storefront assets"
        )

    if not destination.exists():
        destination.mkdir(parents=True)

    assets: list[dict[str, Any]] = []
    for asset_id in asset_ids:
        side_records: dict[str, dict[str, Any]] = {}
        documents: dict[str, dict[str, Any]] = {}
        binaries: dict[str, bytes] = {}
        payloads: dict[str, bytes] = {}
        for side in ("left", "right"):
            source = source_paths[side][asset_id]
            payload = source.read_bytes()
            document, binary, binary_start = parse_glb_for_diagnostics(source)
            side_directory = destination / side
            side_directory.mkdir(exist_ok=True)
            glb_destination = side_directory / f"{asset_id}.glb"
            json_destination = side_directory / f"{asset_id}.gltf.json"
            shutil.copyfile(source, glb_destination)
            json_destination.write_text(
                json.dumps(
                    document,
                    ensure_ascii=True,
                    indent=2,
                    sort_keys=True,
                )
                + "\n",
                encoding="utf-8",
            )
            payloads[side] = payload
            documents[side] = document
            binaries[side] = binary
            side_records[side] = {
                "binary_offset": binary_start,
                "bytes": len(payload),
                "glb": glb_destination.relative_to(destination).as_posix(),
                "json": json_destination.relative_to(destination).as_posix(),
                "sha256": hashlib.sha256(payload).hexdigest(),
            }

        assets.append(
            {
                "asset_id": asset_id,
                "binary_first_difference": first_byte_difference(
                    binaries["left"], binaries["right"]
                ),
                "file_first_difference": first_byte_difference(
                    payloads["left"], payloads["right"]
                ),
                "json_first_difference": first_document_difference(
                    documents["left"], documents["right"]
                ),
                "left": side_records["left"],
                "right": side_records["right"],
            }
        )

    report = {
        "assets": assets,
        "error": str(error),
        "format": DIAGNOSTIC_FORMAT,
        "variants": len(assets),
    }
    (destination / "diagnostics.json").write_text(
        json.dumps(report, ensure_ascii=True, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return report


def collect_failure_diagnostics_safely(
    left_root: Path,
    right_root: Path,
    destination: Path,
    *,
    allowed_root: Path,
    error: BaseException,
) -> str | None:
    """Capture evidence without ever replacing the primary comparison error."""

    try:
        collect_failure_diagnostics(
            left_root,
            right_root,
            destination,
            allowed_root=allowed_root,
            error=error,
        )
    # This is deliberately broader than the gate's primary exception handler:
    # best-effort telemetry must never replace the comparison failure that
    # caused it to run.
    except Exception as diagnostic_error:  # noqa: BLE001
        return (
            "storefront failure diagnostics could not be captured: "
            f"{diagnostic_error}"
        )
    return None


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
        "--diagnostics-directory",
        type=Path,
        help=(
            "empty output directory for bounded left/right GLB evidence when "
            "the clean-root comparison fails"
        ),
    )
    parser.add_argument(
        "--generation-timeout",
        type=int,
        default=DEFAULT_GENERATION_TIMEOUT_SECONDS,
        help="maximum seconds allowed for each clean Blender generation",
    )
    parser.add_argument(
        "--generation-workers",
        type=int,
        default=DEFAULT_GENERATION_WORKERS,
        help=(
            "number of independent Blender generations to run concurrently; "
            "use one on shared or software-rendered CI hosts"
        ),
    )
    args = parser.parse_args()
    if args.generation_timeout <= 0:
        parser.error("--generation-timeout must be positive")
    if not 1 <= args.generation_workers <= 2:
        parser.error("--generation-workers must be one or two")
    return args


def main() -> int:
    args = parse_args()
    repo_root = args.repo_root.resolve()
    try:
        diagnostics_directory = None
        diagnostics_root = None
        if args.diagnostics_directory is not None:
            diagnostics_root = repo_root / "artifacts"
            diagnostics_directory = args.diagnostics_directory
            if not diagnostics_directory.is_absolute():
                diagnostics_directory = repo_root / diagnostics_directory
            diagnostics_directory = validate_diagnostics_destination(
                diagnostics_directory,
                diagnostics_root,
            )
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
                max_workers=args.generation_workers,
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
            try:
                report = compare_clean_roots(repo_root, left_root, right_root)
            except CleanReproducibilityFailure as error:
                if (
                    diagnostics_directory is not None
                    and diagnostics_root is not None
                ):
                    diagnostic_error = collect_failure_diagnostics_safely(
                        left_root,
                        right_root,
                        diagnostics_directory,
                        allowed_root=diagnostics_root,
                        error=error,
                    )
                    if diagnostic_error is not None:
                        sys.stderr.write(diagnostic_error + "\n")
                raise
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
