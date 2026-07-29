#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare deterministic storefront runtime outputs from two clean builds."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import sys
from typing import Any, Iterable


DEFAULT_FAMILY = (
    "content-source/cityworld_next/buildings/storefront_family/"
    "rorng_city_storefront_family.v1.json"
)
EXPECTED_COMPILED_ROLES = frozenset(
    {
        "collision-fixture",
        "material-fallback",
        "render-lod0",
        "render-lod1",
        "render-lod2",
        "terrain-object",
    }
)
SHA256_HEX = frozenset("0123456789abcdef")
MAX_FILE_BYTES = 512 * 1024 * 1024


class ComparisonFailure(RuntimeError):
    pass


def reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def load_json(path: Path) -> dict[str, Any]:
    try:
        document = json.loads(
            path.read_text(encoding="utf-8"),
            object_pairs_hook=reject_duplicate_keys,
            parse_constant=lambda token: (_ for _ in ()).throw(
                ValueError(f"non-finite JSON number: {token}")
            ),
        )
    except (OSError, UnicodeDecodeError, json.JSONDecodeError, ValueError) as error:
        raise ComparisonFailure(f"cannot read {path}: {error}") from error
    if not isinstance(document, dict):
        raise ComparisonFailure(f"JSON root must be an object: {path}")
    return document


def is_sha256(value: Any) -> bool:
    return (
        isinstance(value, str)
        and len(value) == 64
        and set(value) <= SHA256_HEX
    )


def resolve_declared(root: Path, value: Any) -> Path:
    if not isinstance(value, str) or not value or "\\" in value:
        raise ComparisonFailure("declared artifact path is not portable")
    relative = PurePosixPath(value)
    if relative.is_absolute() or any(
        part in ("", ".", "..") for part in relative.parts
    ):
        raise ComparisonFailure(f"unsafe declared artifact path: {value}")
    path = (root / value).resolve()
    try:
        path.relative_to(root.resolve())
    except ValueError as error:
        raise ComparisonFailure(f"artifact path escapes build root: {value}") from error
    return path


def sha256_file(path: Path) -> str:
    if not path.is_file() or path.is_symlink():
        raise ComparisonFailure(f"artifact is missing or unsafe: {path}")
    if path.stat().st_size > MAX_FILE_BYTES:
        raise ComparisonFailure(f"artifact exceeds byte limit: {path}")
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _checked_record(
    root: Path,
    record: Any,
    *,
    label: str,
    require_size: bool,
) -> tuple[str, str]:
    if not isinstance(record, dict):
        raise ComparisonFailure(f"{label} record is missing")
    relative = record.get("path")
    expected_hash = record.get("sha256")
    if not is_sha256(expected_hash):
        raise ComparisonFailure(f"{label} has no valid SHA-256")
    path = resolve_declared(root, relative)
    actual_hash = sha256_file(path)
    if actual_hash != expected_hash:
        raise ComparisonFailure(
            f"{label} hash is stale: expected {expected_hash}, got {actual_hash}"
        )
    if require_size and path.stat().st_size != record.get("size"):
        raise ComparisonFailure(f"{label} size is stale")
    return str(relative), actual_hash


def collect_reproducible_outputs(
    root: Path,
    manifest_relatives: Iterable[str],
) -> dict[str, dict[str, str]]:
    """Return checked GLB and compiled runtime output identities."""

    outputs: dict[str, dict[str, str]] = {}
    for manifest_relative in manifest_relatives:
        manifest_path = resolve_declared(root, manifest_relative)
        manifest = load_json(manifest_path)
        asset = manifest.get("asset")
        artifacts = manifest.get("artifacts")
        compiled = manifest.get("compiled")
        if (
            not isinstance(asset, dict)
            or not isinstance(artifacts, dict)
            or not isinstance(compiled, dict)
        ):
            raise ComparisonFailure(
                f"incomplete reproducibility records: {manifest_relative}"
            )
        asset_id = asset.get("id")
        if not isinstance(asset_id, str) or not asset_id:
            raise ComparisonFailure(f"asset ID is missing: {manifest_relative}")

        glb_path, glb_hash = _checked_record(
            root,
            artifacts.get("glb"),
            label=f"{asset_id}:glb",
            require_size=False,
        )
        asset_outputs = {
            "glb": {
                "path": glb_path,
                "sha256": glb_hash,
            }
        }
        compiled_outputs = compiled.get("outputs")
        if not isinstance(compiled_outputs, list):
            raise ComparisonFailure(
                f"compiled outputs are missing: {manifest_relative}"
            )
        roles = {
            record.get("role")
            for record in compiled_outputs
            if isinstance(record, dict)
        }
        if roles != EXPECTED_COMPILED_ROLES:
            raise ComparisonFailure(
                f"compiled output role set is incomplete: {manifest_relative}"
            )
        for record in compiled_outputs:
            role = record["role"]
            path, digest = _checked_record(
                root,
                record,
                label=f"{asset_id}:{role}",
                require_size=True,
            )
            asset_outputs[role] = {
                "path": path,
                "sha256": digest,
            }
        outputs[asset_id] = asset_outputs
    return outputs


def family_manifest_relatives(root: Path, family_relative: str) -> list[str]:
    family = load_json(resolve_declared(root, family_relative))
    variants = family.get("variants")
    if not isinstance(variants, list) or not variants:
        raise ComparisonFailure("storefront family has no variants")
    relatives: list[str] = []
    for index, variant in enumerate(variants):
        if not isinstance(variant, dict) or not isinstance(
            variant.get("manifest"), str
        ):
            raise ComparisonFailure(f"variant {index} has no manifest")
        relatives.append(variant["manifest"])
    if len(set(relatives)) != len(relatives):
        raise ComparisonFailure("storefront family repeats a variant manifest")
    return relatives


def compare_roots(
    left_root: Path,
    right_root: Path,
    manifest_relatives: Iterable[str],
) -> dict[str, Any]:
    relatives = tuple(manifest_relatives)
    left = collect_reproducible_outputs(left_root.resolve(), relatives)
    right = collect_reproducible_outputs(right_root.resolve(), relatives)
    mismatches: list[dict[str, str]] = []
    for asset_id in sorted(set(left) | set(right)):
        left_outputs = left.get(asset_id, {})
        right_outputs = right.get(asset_id, {})
        for role in sorted(set(left_outputs) | set(right_outputs)):
            left_record = left_outputs.get(role)
            right_record = right_outputs.get(role)
            if left_record != right_record:
                mismatches.append(
                    {
                        "asset_id": asset_id,
                        "left": json.dumps(
                            left_record, ensure_ascii=True, sort_keys=True
                        ),
                        "right": json.dumps(
                            right_record, ensure_ascii=True, sort_keys=True
                        ),
                        "role": role,
                    }
                )
    return {
        "format": "ror-cityworld-storefront-reproducibility-v1",
        "mismatches": mismatches,
        "outputs": sum(len(records) for records in left.values()),
        "valid": not mismatches,
        "variants": len(left),
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--left-root", type=Path, required=True)
    parser.add_argument("--right-root", type=Path, required=True)
    parser.add_argument("--family", default=DEFAULT_FAMILY)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        left_manifests = family_manifest_relatives(args.left_root, args.family)
        right_manifests = family_manifest_relatives(args.right_root, args.family)
        if left_manifests != right_manifests:
            raise ComparisonFailure(
                "clean builds do not declare the same variant manifests"
            )
        report = compare_roots(
            args.left_root,
            args.right_root,
            left_manifests,
        )
    except (ComparisonFailure, OSError) as error:
        report = {
            "error": str(error),
            "format": "ror-cityworld-storefront-reproducibility-v1",
            "mismatches": [],
            "valid": False,
        }
    sys.stdout.write(
        json.dumps(report, ensure_ascii=True, sort_keys=True, separators=(",", ":"))
        + "\n"
    )
    return 0 if report["valid"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
