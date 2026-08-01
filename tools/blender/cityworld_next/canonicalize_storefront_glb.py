#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Canonicalize interchangeable storefront vertex identities in a GLB.

This post-pass runs after ``canonicalize_static_glb.py``.  The baseline pass
sorts complete vertex records, but source indices remain as a tie-breaker when
two records have identical position, normal, tangent, and UV values.  Blender
may assign those interchangeable indices differently between exports.  This
pass rewrites only triangle indices using canonical attribute records, while
retaining every vertex and keeping all three indices in each triangle distinct.
"""

from __future__ import annotations

import json
from pathlib import Path
import struct


EXPECTED_ATTRIBUTES = {
    "NORMAL",
    "POSITION",
    "TANGENT",
    "TEXCOORD_0",
}
COMPONENT_FORMATS = {
    5123: ("H", 2),
    5125: ("I", 4),
    5126: ("f", 4),
}
COMPONENT_WIDTHS = {
    "SCALAR": 1,
    "VEC2": 2,
    "VEC3": 3,
    "VEC4": 4,
}


def canonicalize_storefront_indices(path: Path) -> None:
    """Remove Blender source-index identity from canonical storefront GLBs."""

    contents = bytearray(path.read_bytes())
    if len(contents) < 28:
        raise RuntimeError("exported GLB is truncated")
    magic, version, declared_length = struct.unpack_from("<4sII", contents, 0)
    if magic != b"glTF" or version != 2 or declared_length != len(contents):
        raise RuntimeError("exported GLB header is invalid")
    json_length, json_type = struct.unpack_from("<II", contents, 12)
    if json_type != 0x4E4F534A:
        raise RuntimeError("exported GLB has no JSON chunk")
    json_start = 20
    json_end = json_start + json_length
    document = json.loads(
        bytes(contents[json_start:json_end]).rstrip(b" \x00")
    )
    binary_length, binary_type = struct.unpack_from("<II", contents, json_end)
    if binary_type != 0x004E4942:
        raise RuntimeError("exported GLB has no binary chunk")
    binary_start = json_end + 8
    if binary_start + binary_length != len(contents):
        raise RuntimeError("exported GLB binary chunk is truncated")

    def accessor_layout(
        accessor_index: int,
    ) -> tuple[dict[str, object], int, str, int, int]:
        accessor = document["accessors"][accessor_index]
        view = document["bufferViews"][accessor["bufferView"]]
        component_type = accessor["componentType"]
        accessor_type = accessor["type"]
        if (
            component_type not in COMPONENT_FORMATS
            or accessor_type not in COMPONENT_WIDTHS
        ):
            raise RuntimeError("unsupported storefront GLB accessor")
        format_code, component_size = COMPONENT_FORMATS[component_type]
        width = COMPONENT_WIDTHS[accessor_type]
        packed_size = component_size * width
        if view.get("byteStride", packed_size) != packed_size:
            raise RuntimeError("storefront GLB accessors must be packed")
        offset = (
            binary_start
            + view.get("byteOffset", 0)
            + accessor.get("byteOffset", 0)
        )
        if offset + accessor["count"] * packed_size > len(contents):
            raise RuntimeError("storefront GLB accessor escapes its buffer")
        return accessor, offset, format_code, width, packed_size

    def read_accessor(accessor_index: int) -> list[tuple[float | int, ...]]:
        accessor, offset, format_code, width, packed_size = accessor_layout(
            accessor_index
        )
        unpack_format = "<" + format_code * width
        return [
            struct.unpack_from(
                unpack_format,
                contents,
                offset + index * packed_size,
            )
            for index in range(accessor["count"])
        ]

    def write_indices(accessor_index: int, indices: list[int]) -> None:
        accessor, offset, format_code, width, packed_size = accessor_layout(
            accessor_index
        )
        if (
            accessor["componentType"] not in {5123, 5125}
            or accessor["type"] != "SCALAR"
            or width != 1
            or len(indices) != accessor["count"]
        ):
            raise RuntimeError("storefront index accessor profile changed")
        pack_format = "<" + format_code
        try:
            for index, value in enumerate(indices):
                struct.pack_into(
                    pack_format,
                    contents,
                    offset + index * packed_size,
                    value,
                )
        except struct.error as error:
            raise RuntimeError("storefront index exceeds its component type") from error

    for mesh in document["meshes"]:
        for primitive in mesh["primitives"]:
            attributes = primitive["attributes"]
            if set(attributes) != EXPECTED_ATTRIBUTES:
                raise RuntimeError("unexpected storefront GLB attribute profile")
            attribute_names = sorted(attributes)
            values_by_attribute = {
                name: read_accessor(attributes[name])
                for name in attribute_names
            }
            vertex_count = len(values_by_attribute["POSITION"])
            if any(
                len(values) != vertex_count
                for values in values_by_attribute.values()
            ):
                raise RuntimeError("mismatched storefront GLB attribute counts")
            keys = [
                tuple(
                    component
                    for name in attribute_names
                    for component in values_by_attribute[name][index]
                )
                for index in range(vertex_count)
            ]
            if keys != sorted(keys):
                raise RuntimeError(
                    "storefront GLB must be geometry-canonicalized first"
                )

            index_accessor = primitive["indices"]
            raw_indices = [
                int(value[0]) for value in read_accessor(index_accessor)
            ]
            if len(raw_indices) % 3 != 0:
                raise RuntimeError("storefront GLB indices are not triangles")
            if set(raw_indices) != set(range(vertex_count)):
                raise RuntimeError("storefront GLB has unreferenced vertices")

            indices_by_key: dict[tuple[float | int, ...], list[int]] = {}
            for index, key in enumerate(keys):
                indices_by_key.setdefault(key, []).append(index)

            triangles = []
            for offset in range(0, len(raw_indices), 3):
                triangle = tuple(
                    keys[index] for index in raw_indices[offset : offset + 3]
                )
                triangles.append(
                    min(
                        triangle,
                        (triangle[1], triangle[2], triangle[0]),
                        (triangle[2], triangle[0], triangle[1]),
                    )
                )
            triangles.sort()

            next_lane_by_key: dict[tuple[float | int, ...], int] = {}
            canonical_indices: list[int] = []
            for triangle in triangles:
                indexed_triangle = []
                for key in triangle:
                    lanes = indices_by_key[key]
                    lane = next_lane_by_key.get(key, 0)
                    indexed_triangle.append(lanes[lane % len(lanes)])
                    next_lane_by_key[key] = lane + 1
                if len(set(indexed_triangle)) != 3:
                    raise RuntimeError(
                        "storefront triangle lost distinct vertex indices"
                    )
                canonical_indices.extend(indexed_triangle)
            if (
                len(canonical_indices) != len(raw_indices)
                or set(canonical_indices) != set(range(vertex_count))
            ):
                raise RuntimeError("storefront GLB triangle topology changed")
            write_indices(index_accessor, canonical_indices)

    path.write_bytes(contents)
