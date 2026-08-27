#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate v3 forward-native sources with authored distance LOD ladders."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
from typing import Any

import validate_native_render_asset as legacy


SOURCE_FORMAT_V3 = "ror-native-render-source-v3"
MAX_DISTANCE_LOD_LEVELS = 15


class NativeRenderAssetV3Validator(legacy.NativeRenderAssetValidator):
    """Strict v3 extension without changing the immutable v1/v2 validator."""

    def load_manifest(self) -> None:
        super().load_manifest()
        if self.manifest is None:
            return
        if self.manifest.get("format") != SOURCE_FORMAT_V3:
            self.add(
                "FORMAT_UNSUPPORTED",
                "$.format",
                f"expected {SOURCE_FORMAT_V3}",
            )
            return
        self.diagnostics = [
            diagnostic
            for diagnostic in self.diagnostics
            if not (
                diagnostic.code == "FORMAT_UNSUPPORTED"
                and diagnostic.path == "$.format"
            )
        ]
        self.source_format = SOURCE_FORMAT_V3

    def validate_claims(self) -> None:
        assert self.manifest is not None
        claims = self._record(
            self.manifest.get("claims"),
            ("ambient_occlusion", "collision", "lods", "native_terrain", "visual_only"),
            "$.claims",
        )
        if claims is None:
            return
        required = {
            "ambient_occlusion": False,
            "collision": False,
            "lods": True,
            "native_terrain": False,
            "visual_only": True,
        }
        for field, expected in required.items():
            if claims.get(field) is not expected:
                self.add(
                    "V3_SCOPE_CLAIM",
                    f"$.claims.{field}",
                    f"v3 fixture requires {field}={str(expected).lower()}",
                )

    def validate_materials(
        self,
        textures: dict[str, dict[str, Any]],
        samplers: dict[str, dict[str, Any]],
    ) -> dict[str, dict[str, Any]]:
        # V3 retains the complete v2 thin-slab material record unchanged.
        self.source_format = legacy.SOURCE_FORMAT_V2
        try:
            return super().validate_materials(textures, samplers)
        finally:
            self.source_format = SOURCE_FORMAT_V3

    @staticmethod
    def _is_ordered_triangle_subsequence(
        parent: list[int], candidate: list[int]
    ) -> bool:
        if (
            not candidate
            or len(parent) % 3 != 0
            or len(candidate) % 3 != 0
            or len(candidate) >= len(parent)
        ):
            return False
        parent_offset = 0
        for candidate_offset in range(0, len(candidate), 3):
            triangle = candidate[candidate_offset : candidate_offset + 3]
            while (
                parent_offset < len(parent)
                and parent[parent_offset : parent_offset + 3] != triangle
            ):
                parent_offset += 3
            if parent_offset == len(parent):
                return False
            parent_offset += 3
        return True

    def validate_mesh_declarations(
        self,
        glb_meshes: dict[str, dict[str, Any]],
        materials: dict[str, dict[str, Any]],
    ) -> None:
        assert self.manifest is not None
        meshes = self.manifest.get("meshes")
        if not isinstance(meshes, list):
            super().validate_mesh_declarations(glb_meshes, materials)
            return

        stripped_meshes = [
            {
                key: item
                for key, item in value.items()
                if key != "distance_lods"
            }
            if isinstance(value, dict)
            else value
            for value in meshes
        ]
        self.manifest["meshes"] = stripped_meshes
        try:
            super().validate_mesh_declarations(glb_meshes, materials)
        finally:
            self.manifest["meshes"] = meshes

        lod_level_count = 0
        lod_index_count = 0
        for mesh_index, mesh in enumerate(meshes):
            pointer = f"$.meshes[{mesh_index}]"
            if not isinstance(mesh, dict):
                continue
            if "distance_lods" not in mesh:
                self.add(
                    "FIELD_MISSING",
                    f"{pointer}.distance_lods",
                    "required field is missing",
                )
                continue
            levels = mesh.get("distance_lods")
            if not isinstance(levels, list):
                self.add(
                    "FIELD_TYPE",
                    f"{pointer}.distance_lods",
                    "distance LODs must be an array",
                )
                continue
            if len(levels) > MAX_DISTANCE_LOD_LEVELS:
                self.add(
                    "LIMIT_EXCEEDED",
                    f"{pointer}.distance_lods",
                    f"distance LODs exceed {MAX_DISTANCE_LOD_LEVELS} levels",
                )
                continue

            node = mesh.get("node")
            source_mesh = glb_meshes.get(node) if isinstance(node, str) else None
            base_indices = (
                source_mesh.get("index_values")
                if isinstance(source_mesh, dict)
                else None
            )
            previous_indices = base_indices if isinstance(base_indices, list) else []
            vertex_count = (
                len(source_mesh.get("positions", []))
                if isinstance(source_mesh, dict)
                else 0
            )
            previous_distance = 0.0
            for level_index, level_value in enumerate(levels):
                level_pointer = f"{pointer}.distance_lods[{level_index}]"
                level = self._record(
                    level_value,
                    ("activation_distance_meters", "indices"),
                    level_pointer,
                )
                if level is None:
                    continue
                distance = self._number(
                    level.get("activation_distance_meters"),
                    f"{level_pointer}.activation_distance_meters",
                    minimum=0.0,
                )
                if distance is not None and distance <= previous_distance:
                    self.add(
                        "LOD_DISTANCE_ORDER",
                        f"{level_pointer}.activation_distance_meters",
                        "LOD activation distances must be positive and strictly increasing",
                    )
                elif distance is not None:
                    previous_distance = distance

                indices = level.get("indices")
                if (
                    not isinstance(indices, list)
                    or not indices
                    or len(indices) % 3 != 0
                    or any(
                        isinstance(index, bool)
                        or not isinstance(index, int)
                        or index < 0
                        or index > 0xFFFFFFFF
                        for index in indices
                    )
                ):
                    self.add(
                        "LOD_INDICES_INVALID",
                        f"{level_pointer}.indices",
                        "LOD indices must be a non-empty uint32 triangle list",
                    )
                    continue
                if any(index >= vertex_count for index in indices):
                    self.add(
                        "LOD_INDEX_RANGE",
                        f"{level_pointer}.indices",
                        "LOD index exceeds the shared base vertex stream",
                    )
                if not self._is_ordered_triangle_subsequence(previous_indices, indices):
                    self.add(
                        "LOD_TRIANGLE_SUBSEQUENCE",
                        f"{level_pointer}.indices",
                        "LOD triangles must be an ordered strict subset of the preceding level",
                    )
                previous_indices = indices
                lod_level_count += 1
                lod_index_count += len(indices)

        if lod_level_count == 0:
            self.add(
                "LOD_REQUIRED",
                "$.meshes",
                "v3 requires at least one authored distance LOD level",
            )
        self.stats["lod_indices"] = lod_index_count
        self.stats["lod_levels"] = lod_level_count


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--repo-root", type=Path, default=Path.cwd())
    args = parser.parse_args(sys.argv[1:] if argv is None else argv)
    validator = NativeRenderAssetV3Validator(args.repo_root, args.manifest)
    report = validator.validate()
    print(json.dumps(report, ensure_ascii=True, sort_keys=True, separators=(",", ":")))
    return 0 if report["summary"]["valid"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
