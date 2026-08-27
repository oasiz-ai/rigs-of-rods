#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile validated v3 forward-native sources with distance LOD records."""

from __future__ import annotations

import json
from pathlib import Path
import struct
import sys
from typing import Any

import compile_native_render_asset as legacy
from validate_native_render_asset_v3 import (
    NativeRenderAssetV3Validator,
    SOURCE_FORMAT_V3,
)


COMPILER_FORMAT_V3 = "ror-native-render-compiler-v3"
PACKAGE_MANIFEST_FORMAT_V3 = "ror-native-render-package-manifest-v3"
PACKAGE_MAGIC_V3 = b"RORNAT3\x00"
PACKAGE_VERSION_V3 = 3


class NativeRenderAssetV3Compiler(legacy.NativeRenderAssetCompiler):
    """V3 compiler extension that leaves v1/v2 tool bytes untouched."""

    def __init__(self, repo_root: Path, manifest_path: Path):
        super().__init__(repo_root, manifest_path)
        self.validator = NativeRenderAssetV3Validator(repo_root, manifest_path)
        self.repo_root = self.validator.repo_root
        self.manifest_path = self.validator.manifest_path

    @staticmethod
    def _tool_dependencies() -> list[dict[str, str]]:
        paths = (
            "tools/validate_cityworld_asset.py",
            "tools/validate_native_render_asset.py",
            "tools/validate_native_render_asset_v3.py",
            "tools/compile_native_render_asset.py",
        )
        return [
            {
                "path": path,
                "sha256": legacy.sha256_file(
                    legacy.SCRIPT_DIR.parent / path,
                    max_bytes=legacy.MAX_SOURCE_BYTES,
                ),
            }
            for path in paths
        ]

    @staticmethod
    def _compiler_record() -> dict[str, Any]:
        return {
            "dependencies": NativeRenderAssetV3Compiler._tool_dependencies(),
            "format": COMPILER_FORMAT_V3,
            "path": "tools/compile_native_render_asset_v3.py",
            "sha256": legacy.sha256_file(
                Path(__file__).resolve(),
                max_bytes=legacy.MAX_SOURCE_BYTES,
            ),
        }

    def _mesh_payload(self, entry: dict[str, Any]) -> bytes:
        source = self.validator.mesh_sources[entry["id"]]["glb"]
        if not isinstance(source, dict):
            raise legacy.CompileFailure("validated GLB mesh source disappeared")
        positions = source["positions"]
        normals = source["normals"]
        tangents = source["tangents"]
        texcoords = source["texcoords"]
        indices = source["index_values"]
        index_format = 0 if len(positions) <= 65535 and max(indices) <= 65535 else 1
        counts = (
            len(positions),
            len(normals),
            len(tangents),
            0,
            len(texcoords),
            0,
            0,
            len(indices),
        )
        payload = bytearray()
        payload.extend(struct.pack("<I", 2))
        payload.extend(legacy.encode_string(entry["id"]))
        payload.extend(struct.pack("<BBBBQ", 0, index_format, 0, 0, 1))
        payload.extend(
            struct.pack(
                "<6f",
                *(legacy.canonical_float(value) for value in source["bounds_min"]),
                *(legacy.canonical_float(value) for value in source["bounds_max"]),
            )
        )
        payload.extend(struct.pack("<8I", *counts))
        payload.extend(legacy.pack_float_vectors(positions, 3))
        payload.extend(legacy.pack_float_vectors(normals, 3))
        payload.extend(legacy.pack_float_vectors(tangents, 4))
        payload.extend(legacy.pack_float_vectors(texcoords, 2))
        payload.extend(legacy.pack_uint32_values(indices))
        levels = entry["distance_lods"]
        payload.extend(struct.pack("<I", len(levels)))
        for level in levels:
            lod_indices = level["indices"]
            payload.extend(
                struct.pack(
                    "<fI",
                    legacy.canonical_float(level["activation_distance_meters"]),
                    len(lod_indices),
                )
            )
            payload.extend(legacy.pack_uint32_values(lod_indices))
        return bytes(payload)

    def _material_payload(self, entry: dict[str, Any]) -> bytes:
        assert self.manifest is not None
        source_format = self.manifest["format"]
        self.manifest["format"] = legacy.SOURCE_FORMAT_V2
        try:
            return super()._material_payload(entry)
        finally:
            self.manifest["format"] = source_format

    def embedded_manifest(self) -> bytes:
        assert self.manifest is not None
        source_format = self.manifest["format"]
        self.manifest["format"] = legacy.SOURCE_FORMAT_V2
        try:
            value = json.loads(super().embedded_manifest())
        finally:
            self.manifest["format"] = source_format
        value["format"] = PACKAGE_MANIFEST_FORMAT_V3
        value["compiler"] = self._compiler_record()
        encoded = legacy.canonical_json(value).encode("ascii")
        if len(encoded) > 1024 * 1024:
            raise legacy.CompileFailure("embedded package manifest exceeds v3 limit")
        return encoded

    def build_package(self) -> tuple[bytes, dict[str, Any]]:
        if self.manifest is None:
            self.prepare()
        assert self.manifest is not None
        if self.manifest["format"] != SOURCE_FORMAT_V3:
            raise legacy.CompileFailure(f"v3 compiler requires {SOURCE_FORMAT_V3}")
        source_format = self.manifest["format"]
        self.manifest["format"] = legacy.SOURCE_FORMAT_V2
        try:
            package_bytes, report = super().build_package()
        finally:
            self.manifest["format"] = source_format
        package = bytearray(package_bytes)
        package[:8] = PACKAGE_MAGIC_V3
        struct.pack_into("<I", package, 8, PACKAGE_VERSION_V3)
        package_bytes = bytes(package)
        report["compiler"] = self._compiler_record()
        report["output"]["sha256"] = legacy.sha256_bytes(package_bytes)
        return package_bytes, report


def main(argv: list[str] | None = None) -> int:
    args = legacy._arguments(sys.argv[1:] if argv is None else argv)
    try:
        compiler = NativeRenderAssetV3Compiler(args.repo_root, args.manifest)
        compiler.prepare()
        package_bytes, report = compiler.build_package()
        assert compiler.manifest is not None
        output_path = (
            legacy.checked_output_path(
                compiler.repo_root,
                args.output,
                ".rornative",
                lexical_root=compiler.repo_root_lexical,
            )
            if args.output is not None
            else legacy.declared_output(
                compiler.repo_root,
                compiler.manifest["outputs"]["package_path"],
                ".rornative",
            )
        )
        report_path = (
            legacy.checked_output_path(
                compiler.repo_root,
                args.report,
                ".compile.json",
                lexical_root=compiler.repo_root_lexical,
            )
            if args.report is not None
            else legacy.declared_output(
                compiler.repo_root,
                compiler.manifest["outputs"]["report_path"],
                ".compile.json",
            )
        )
        report_bytes = legacy.canonical_pretty(report)
        if args.validate_checked:
            legacy.validate_checked(output_path, package_bytes, "package")
            legacy.validate_checked(report_path, report_bytes, "report")
        else:
            legacy.atomic_write(output_path, package_bytes)
            legacy.atomic_write(report_path, report_bytes)
        print(legacy.canonical_json(report))
        return 0
    except MemoryError:
        print(
            "native render asset v3 compile failed: bounded allocation could not be satisfied",
            file=sys.stderr,
        )
        return 1
    except (OverflowError, struct.error):
        print(
            "native render asset v3 compile failed: binary packing rejected a bounded field",
            file=sys.stderr,
        )
        return 1
    except (legacy.CompileFailure, OSError, ValueError) as error:
        print(f"native render asset v3 compile failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
