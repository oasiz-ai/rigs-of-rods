#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build deterministic provenance inputs for CityWorld Next content.

The generated provenance manifest and distributable inventory intentionally
live outside ``resources/nextgen/cityworld`` so the package has no
self-referential checksums.  Every package file must be owned by exactly one
validated asset manifest; unknown files fail closed.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import sys
from typing import Any


PROVENANCE_FORMAT = "ror-content-provenance-v1"
INVENTORY_FORMAT = "ror-distributable-inventory-v1"
COMPILED_FORMAT = "ror-cityworld-compiled-asset-v1"
COMPILE_REPORT_FORMAT = "ror-cityworld-scene-compile-report-v1"
COMPILER_FORMAT = "ror-cityworld-scene-compiler-v1"
COMPILER_PATH = "tools/compile_cityworld_asset.py"
# This pinned compiler revision is byte-compatible for manifests that do not
# opt into inherited runtime materials. The current compiler revalidates that
# restriction before accepting the same checked outputs.
BYTE_COMPATIBLE_COMPILER_SHA256_WITHOUT_RUNTIME_PARENT = frozenset(
    {
        "e073ac1015198aecb609e8bf3c7b70d9013bc9d77b68d8d06d6ddfed470a4059",
    }
)
# This prior revision differs only in where the read-only ``-v`` converter
# probe writes its diagnostic log. Mesh lowering and every emitted byte are
# unchanged, including assets that opt into ``runtime_parent_material``.
BYTE_COMPATIBLE_COMPILER_SHA256 = frozenset(
    {
        "9e65172d4895cac5b033c23485cff4d3744557c76db62cd01ec083f01971ce47",
    }
)
SPDX_LIST_VERSION = "3.28.0"
MAX_FILE_BYTES = 512 * 1024 * 1024


class BuildFailure(RuntimeError):
    pass


def sha256_file(path: Path) -> str:
    if not path.is_file() or path.is_symlink():
        raise BuildFailure(f"required regular file is missing: {path.name}")
    size = path.stat().st_size
    if size > MAX_FILE_BYTES:
        raise BuildFailure(f"file exceeds {MAX_FILE_BYTES} byte limit: {path.name}")
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def load_json(path: Path) -> dict[str, Any]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise BuildFailure(f"cannot read {path.name}: {error}") from error
    if not isinstance(document, dict):
        raise BuildFailure(f"{path.name} root must be an object")
    return document


def relative_path(root: Path, path: Path) -> str:
    try:
        relative = path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError as error:
        raise BuildFailure(f"path escapes repository root: {path}") from error
    parsed = PurePosixPath(relative)
    if (
        not relative
        or parsed.is_absolute()
        or any(part in ("", ".", "..") for part in parsed.parts)
    ):
        raise BuildFailure(f"non-portable repository path: {relative}")
    return relative


def resolve_declared(root: Path, value: Any) -> Path:
    if not isinstance(value, str) or not value or "\\" in value:
        raise BuildFailure("declared path is not a portable relative path")
    parsed = PurePosixPath(value)
    if parsed.is_absolute() or any(part in ("", ".", "..") for part in parsed.parts):
        raise BuildFailure(f"unsafe declared path: {value}")
    path = (root / value).resolve()
    try:
        path.relative_to(root.resolve())
    except ValueError as error:
        raise BuildFailure(f"declared path escapes repository: {value}") from error
    return path


def canonical_pretty(document: dict[str, Any]) -> str:
    return json.dumps(document, indent=2, ensure_ascii=True, sort_keys=True) + "\n"


def canonical_json(document: dict[str, Any]) -> str:
    return json.dumps(
        document,
        ensure_ascii=True,
        separators=(",", ":"),
        sort_keys=True,
    )


def uses_runtime_parent_material(manifest: dict[str, Any]) -> bool:
    materials = manifest.get("materials")
    return isinstance(materials, list) and any(
        isinstance(material, dict)
        and "runtime_parent_material" in material
        for material in materials
    )


def build_documents(repo_root: Path) -> tuple[dict[str, Any], dict[str, Any]]:
    repo_root = repo_root.resolve()
    package_root = repo_root / "resources/nextgen/cityworld"
    if not package_root.is_dir() or package_root.is_symlink():
        raise BuildFailure("CityWorld Next package root is missing or unsafe")

    package_files = sorted(
        (
            path
            for path in package_root.rglob("*")
            if path.is_file() and not path.is_symlink()
        ),
        key=lambda path: path.relative_to(package_root).as_posix(),
    )
    if not package_files:
        raise BuildFailure("CityWorld Next package is empty")
    asset_manifest_paths = [
        path for path in package_files if path.name.endswith(".asset.json")
    ]
    if not asset_manifest_paths:
        raise BuildFailure("CityWorld Next package has no asset manifests")

    owners: dict[Path, tuple[dict[str, Any], Path]] = {}
    generated_by: dict[Path, dict[str, str]] = {}
    for asset_manifest_path in asset_manifest_paths:
        asset_manifest = load_json(asset_manifest_path)
        if asset_manifest.get("format") != "ror-cityworld-asset-v1":
            raise BuildFailure(f"unsupported asset format: {asset_manifest_path.name}")
        asset = asset_manifest.get("asset")
        authoring = asset_manifest.get("authoring")
        artifacts = asset_manifest.get("artifacts")
        if not isinstance(asset, dict) or not isinstance(authoring, dict) or not isinstance(artifacts, dict):
            raise BuildFailure(f"incomplete asset manifest: {asset_manifest_path.name}")
        generator = authoring.get("generator")
        blend = artifacts.get("blend")
        glb = artifacts.get("glb")
        if not isinstance(generator, dict) or not isinstance(blend, dict) or not isinstance(glb, dict):
            raise BuildFailure(f"incomplete asset sources: {asset_manifest_path.name}")

        generator_path = resolve_declared(repo_root, generator.get("path"))
        blend_path = resolve_declared(repo_root, blend.get("path"))
        glb_path = resolve_declared(repo_root, glb.get("path"))
        if sha256_file(generator_path) != generator.get("sha256"):
            raise BuildFailure(f"stale generator hash: {asset_manifest_path.name}")
        dependencies = generator.get("dependencies", [])
        if not isinstance(dependencies, list):
            raise BuildFailure(
                f"invalid generator dependencies: {asset_manifest_path.name}"
            )
        dependency_paths = {generator_path}
        for index, dependency in enumerate(dependencies):
            if not isinstance(dependency, dict):
                raise BuildFailure(
                    f"invalid generator dependency {index}: "
                    f"{asset_manifest_path.name}"
                )
            dependency_path = resolve_declared(
                repo_root,
                dependency.get("path"),
            )
            if dependency_path in dependency_paths:
                raise BuildFailure(
                    f"duplicate generator dependency {index}: "
                    f"{asset_manifest_path.name}"
                )
            dependency_paths.add(dependency_path)
            if sha256_file(dependency_path) != dependency.get("sha256"):
                raise BuildFailure(
                    f"stale generator dependency {index}: "
                    f"{asset_manifest_path.name}"
                )
        if sha256_file(blend_path) != blend.get("sha256"):
            raise BuildFailure(f"stale Blender source hash: {asset_manifest_path.name}")
        if sha256_file(glb_path) != glb.get("sha256"):
            raise BuildFailure(f"stale GLB hash: {asset_manifest_path.name}")
        try:
            glb_path.relative_to(package_root)
        except ValueError as error:
            raise BuildFailure(f"GLB is outside the package: {asset_manifest_path.name}") from error

        compiled = asset_manifest.get("compiled")
        if (
            not isinstance(compiled, dict)
            or compiled.get("format") != COMPILED_FORMAT
        ):
            raise BuildFailure(
                f"missing compiled asset record: {asset_manifest_path.name}"
            )
        report_record = compiled.get("report")
        compiled_outputs = compiled.get("outputs")
        if not isinstance(report_record, dict) or not isinstance(compiled_outputs, list):
            raise BuildFailure(
                f"incomplete compiled asset record: {asset_manifest_path.name}"
            )
        report_path = resolve_declared(repo_root, report_record.get("path"))
        if sha256_file(report_path) != report_record.get("sha256"):
            raise BuildFailure(f"stale compile report: {asset_manifest_path.name}")
        report = load_json(report_path)
        if report.get("format") != COMPILE_REPORT_FORMAT:
            raise BuildFailure(
                f"unsupported compile report: {asset_manifest_path.name}"
            )
        compiler = report.get("compiler")
        if (
            not isinstance(compiler, dict)
            or compiler.get("format") != COMPILER_FORMAT
            or compiler.get("path") != COMPILER_PATH
        ):
            raise BuildFailure(
                f"invalid scene compiler identity: {asset_manifest_path.name}"
            )
        compiler_path = resolve_declared(repo_root, compiler.get("path"))
        active_compiler_sha256 = sha256_file(compiler_path)
        current_compiler_sha256 = sha256_file(
            Path(__file__).resolve().with_name("compile_cityworld_asset.py")
        )
        checked_compiler_sha256 = compiler.get("sha256")
        compiler_is_current = (
            active_compiler_sha256 == current_compiler_sha256
            and checked_compiler_sha256 == current_compiler_sha256
        )
        compiler_is_byte_compatible = (
            active_compiler_sha256 == current_compiler_sha256
            and (
                checked_compiler_sha256
                in BYTE_COMPATIBLE_COMPILER_SHA256
                or (
                    not uses_runtime_parent_material(asset_manifest)
                    and checked_compiler_sha256
                    in BYTE_COMPATIBLE_COMPILER_SHA256_WITHOUT_RUNTIME_PARENT
                )
            )
        )
        if not (compiler_is_current or compiler_is_byte_compatible):
            raise BuildFailure(
                f"stale scene compiler identity: {asset_manifest_path.name}"
            )
        expected_inputs = {
            "asset_contract_sha256": sha256_bytes(
                canonical_json(
                    {
                        key: value
                        for key, value in asset_manifest.items()
                        if key != "compiled"
                    }
                ).encode("utf-8")
            ),
            "glb": {
                "path": relative_path(repo_root, glb_path),
                "sha256": sha256_file(glb_path),
            },
        }
        if report.get("inputs") != expected_inputs:
            raise BuildFailure(
                f"stale compile source identity: "
                f"{asset_manifest_path.name}"
            )
        report_outputs = report.get("outputs")
        if report_outputs != compiled_outputs:
            raise BuildFailure(
                f"compile outputs disagree with asset manifest: {asset_manifest_path.name}"
            )

        compiled_paths: list[Path] = []
        for index, output in enumerate(compiled_outputs):
            if not isinstance(output, dict):
                raise BuildFailure(
                    f"invalid compiled output {index}: {asset_manifest_path.name}"
                )
            output_path = resolve_declared(repo_root, output.get("path"))
            if (
                sha256_file(output_path) != output.get("sha256")
                or output_path.stat().st_size != output.get("size")
            ):
                raise BuildFailure(
                    f"stale compiled output {index}: {asset_manifest_path.name}"
                )
            if not isinstance(output.get("role"), str) or not output["role"]:
                raise BuildFailure(
                    f"compiled output has no role: {asset_manifest_path.name}"
                )
            compiled_paths.append(output_path)

        owned_paths = (
            asset_manifest_path.resolve(),
            glb_path,
            report_path,
            *compiled_paths,
        )
        for owned_path in owned_paths:
            try:
                owned_path.relative_to(package_root)
            except ValueError as error:
                raise BuildFailure(
                    f"package output is outside package: {owned_path.name}"
                ) from error
            if owned_path in owners:
                raise BuildFailure(f"package file has multiple owners: {owned_path.name}")
            owners[owned_path] = (asset_manifest, asset_manifest_path)
        generator_source = {
            "kind": "generator",
            "sha256": generator["sha256"],
            "uri": asset.get("source_uri"),
        }
        compiler_source = {
            "kind": "generator",
            "sha256": compiler["sha256"],
            "uri": asset.get("source_uri"),
        }
        generated_by[asset_manifest_path.resolve()] = generator_source
        generated_by[glb_path] = generator_source
        generated_by[report_path] = compiler_source
        for compiled_path in compiled_paths:
            generated_by[compiled_path] = compiler_source

    unknown = [path for path in package_files if path.resolve() not in owners]
    if unknown:
        names = ", ".join(path.relative_to(package_root).as_posix() for path in unknown)
        raise BuildFailure(f"package contains unowned files: {names}")

    inventory_files: list[dict[str, Any]] = []
    provenance_assets: list[dict[str, Any]] = []
    for package_path in package_files:
        asset_manifest, asset_manifest_path = owners[package_path.resolve()]
        asset = asset_manifest["asset"]
        generator = asset_manifest["authoring"]["generator"]
        blend = asset_manifest["artifacts"]["blend"]
        package_relative = package_path.relative_to(package_root).as_posix()
        digest = sha256_file(package_path)
        inventory_files.append(
            {
                "path": package_relative,
                "sha256": digest,
                "size": package_path.stat().st_size,
                "type": "file",
            }
        )
        if package_path.resolve() == asset_manifest_path.resolve():
            editable = {
                "path": generator["path"],
                "sha256": generator["sha256"],
            }
        else:
            editable = {
                "path": blend["path"],
                "sha256": blend["sha256"],
            }
        source_uri = asset.get("source_uri")
        if (
            not isinstance(source_uri, str)
            or not source_uri.startswith("https://")
            or source_uri != source_uri.lower()
        ):
            raise BuildFailure(f"asset source URI is not canonical: {asset_manifest_path.name}")
        provenance_assets.append(
            {
                "author": asset.get("author"),
                "classification": "generated",
                "editable_source": editable,
                "license": asset.get("license"),
                "modified": True,
                "path": package_relative,
                "redistribution": {
                    "allowed": True,
                    "evidence": source_uri,
                },
                "sha256": digest,
                "source": generated_by[package_path.resolve()],
            }
        )

    provenance_assets.sort(key=lambda item: item["path"])
    inventory_files.sort(key=lambda item: item["path"])
    return (
        {
            "assets": provenance_assets,
            "format": PROVENANCE_FORMAT,
            "spdx_list_version": SPDX_LIST_VERSION,
        },
        {
            "files": inventory_files,
            "format": INVENTORY_FORMAT,
        },
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=Path.cwd(),
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="fail if checked-in generated documents are absent or stale",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = args.repo_root.resolve()
    manifest_path = (
        root
        / "content-source/cityworld_next/provenance/cityworld_next.manifest.json"
    )
    inventory_path = (
        root
        / "content-source/cityworld_next/provenance/cityworld_next.inventory.json"
    )
    try:
        manifest, inventory = build_documents(root)
        outputs = {
            manifest_path: canonical_pretty(manifest),
            inventory_path: canonical_pretty(inventory),
        }
        if args.check:
            stale = [
                relative_path(root, path)
                for path, expected in outputs.items()
                if not path.is_file()
                or path.read_text(encoding="utf-8") != expected
            ]
            if stale:
                raise BuildFailure(
                    "generated provenance is stale: " + ", ".join(stale)
                )
        else:
            for path, payload in outputs.items():
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(payload, encoding="utf-8")
    except (BuildFailure, OSError, UnicodeDecodeError) as error:
        sys.stderr.write(f"cityworld provenance build failed: {error}\n")
        return 1

    sys.stdout.write(
        json.dumps(
            {
                "assets": len(manifest["assets"]),
                "format": "ror-cityworld-provenance-build-v1",
                "inventory_files": len(inventory["files"]),
                "mode": "check" if args.check else "write",
            },
            ensure_ascii=True,
            sort_keys=True,
            separators=(",", ":"),
        )
        + "\n"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
