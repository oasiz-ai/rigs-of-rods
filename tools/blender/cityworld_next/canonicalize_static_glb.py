#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Canonicalize project-authored static GLB geometry byte-for-byte."""

from __future__ import annotations

import json
import math
from pathlib import Path
import struct


def rounded_component(value: float, digits: int) -> float:
    rounded = round(float(value), digits)
    return 0.0 if rounded == 0.0 else rounded


def normalized_vector(
    values: tuple[float, float, float],
) -> tuple[float, float, float]:
    canonical = tuple(rounded_component(value, 3) for value in values)
    length = math.sqrt(sum(value * value for value in canonical))
    if abs(length - 1.0) > 2e-3:
        raise RuntimeError("canonical direction is not unit length")
    return canonical


def canonical_tangent(
    values: tuple[float, float, float, float],
    normal: tuple[float, float, float],
) -> tuple[float, float, float, float]:
    tangent = normalized_vector(values[:3])
    projection = abs(
        sum(tangent[axis] * normal[axis] for axis in range(3))
    )
    if projection > 2e-3:
        raise RuntimeError("canonical tangent is not orthogonal")
    return (*tangent, -1.0 if values[3] < 0.0 else 1.0)


def canonicalize_glb_geometry(path: Path) -> None:
    """Canonicalize exported static geometry without changing its topology."""

    contents = bytearray(path.read_bytes())
    if len(contents) < 28:
        raise RuntimeError("exported GLB is truncated")
    magic, version, declared_length = struct.unpack_from(
        "<4sII",
        contents,
        0,
    )
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
    binary_header = json_end
    binary_length, binary_type = struct.unpack_from(
        "<II",
        contents,
        binary_header,
    )
    if binary_type != 0x004E4942:
        raise RuntimeError("exported GLB has no binary chunk")
    binary_start = binary_header + 8
    if binary_start + binary_length != len(contents):
        raise RuntimeError("exported GLB binary chunk is truncated")

    component_formats = {
        5123: ("H", 2),
        5125: ("I", 4),
        5126: ("f", 4),
    }
    component_widths = {
        "SCALAR": 1,
        "VEC2": 2,
        "VEC3": 3,
        "VEC4": 4,
    }

    def accessor_layout(
        accessor_index: int,
    ) -> tuple[dict[str, object], int, str, int, int]:
        accessor = document["accessors"][accessor_index]
        view = document["bufferViews"][accessor["bufferView"]]
        component_type = accessor["componentType"]
        accessor_type = accessor["type"]
        if (
            component_type not in component_formats
            or accessor_type not in component_widths
        ):
            raise RuntimeError("unsupported canonical GLB accessor")
        format_code, component_size = component_formats[component_type]
        width = component_widths[accessor_type]
        packed_size = component_size * width
        stride = view.get("byteStride", packed_size)
        if stride != packed_size:
            raise RuntimeError("canonical GLB accessors must be packed")
        offset = (
            binary_start
            + view.get("byteOffset", 0)
            + accessor.get("byteOffset", 0)
        )
        return accessor, offset, format_code, width, packed_size

    def read_accessor(accessor_index: int) -> list[tuple[float, ...]]:
        accessor, offset, format_code, width, packed_size = (
            accessor_layout(accessor_index)
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

    canonical_binary = bytearray()
    canonical_views: list[dict[str, object]] = []
    canonical_accessors: list[dict[str, object]] = []

    def append_accessor(
        original_index: int,
        values: list[tuple[float, ...]],
        *,
        position_bounds: bool = False,
    ) -> int:
        original = document["accessors"][original_index]
        original_view = document["bufferViews"][original["bufferView"]]
        _accessor, _offset, format_code, width, packed_size = (
            accessor_layout(original_index)
        )
        if len(values) != original["count"]:
            raise RuntimeError("canonical accessor count changed")
        while len(canonical_binary) % 4:
            canonical_binary.append(0)
        byte_offset = len(canonical_binary)
        pack_format = "<" + format_code * width
        packed_values: list[tuple[float, ...]] = []
        for value in values:
            packed = struct.pack(pack_format, *value)
            canonical_binary.extend(packed)
            packed_values.append(struct.unpack(pack_format, packed))
        byte_length = len(values) * packed_size
        view = {
            "buffer": 0,
            "byteLength": byte_length,
            "byteOffset": byte_offset,
        }
        if "target" in original_view:
            view["target"] = original_view["target"]
        canonical_views.append(view)
        accessor = {
            key: value
            for key, value in original.items()
            if key not in {
                "bufferView",
                "byteOffset",
                "max",
                "min",
            }
        }
        accessor["bufferView"] = len(canonical_views) - 1
        if position_bounds:
            if format_code != "f" or width != 3 or not packed_values:
                raise RuntimeError(
                    "POSITION bounds require a non-empty float VEC3 accessor"
                )
            accessor["min"] = [
                min(value[axis] for value in packed_values)
                for axis in range(3)
            ]
            accessor["max"] = [
                max(value[axis] for value in packed_values)
                for axis in range(3)
            ]
        canonical_accessors.append(accessor)
        return len(canonical_accessors) - 1

    for mesh in document["meshes"]:
        for primitive in mesh["primitives"]:
            attributes = primitive["attributes"]
            if set(attributes) != {
                "NORMAL",
                "POSITION",
                "TANGENT",
                "TEXCOORD_0",
            }:
                raise RuntimeError(
                    "unexpected static GLB attribute profile"
                )
            raw = {
                name: read_accessor(accessor_index)
                for name, accessor_index in attributes.items()
            }
            count = len(raw["POSITION"])
            if any(len(values) != count for values in raw.values()):
                raise RuntimeError(
                    "mismatched static GLB attribute counts"
                )

            canonical: dict[str, list[tuple[float, ...]]] = {
                name: [] for name in sorted(attributes)
            }
            for index in range(count):
                normal = normalized_vector(raw["NORMAL"][index])
                canonical["NORMAL"].append(normal)
                canonical["POSITION"].append(
                    tuple(
                        rounded_component(value, 6)
                        for value in raw["POSITION"][index]
                    )
                )
                canonical["TANGENT"].append(
                    canonical_tangent(raw["TANGENT"][index], normal)
                )
                canonical["TEXCOORD_0"].append(
                    tuple(
                        rounded_component(value, 6)
                        for value in raw["TEXCOORD_0"][index]
                    )
                )

            attribute_names = sorted(canonical)
            records = []
            for index in range(count):
                key = tuple(
                    component
                    for name in attribute_names
                    for component in canonical[name][index]
                )
                records.append(
                    (
                        key,
                        index,
                        tuple(
                            canonical[name][index]
                            for name in attribute_names
                        ),
                    )
                )
            records.sort(key=lambda item: (item[0], item[1]))
            new_index_by_old: dict[int, int] = {}
            sorted_attributes: dict[str, list[tuple[float, ...]]] = {
                name: [] for name in attribute_names
            }
            for new_index, (
                _key,
                old_index,
                values,
            ) in enumerate(records):
                new_index_by_old[old_index] = new_index
                for name, value in zip(attribute_names, values):
                    sorted_attributes[name].append(value)

            raw_indices = [
                int(value[0])
                for value in read_accessor(primitive["indices"])
            ]
            if len(raw_indices) % 3 != 0:
                raise RuntimeError(
                    "canonical GLB indices are not triangles"
                )
            if set(raw_indices) != set(range(count)):
                raise RuntimeError(
                    "canonical GLB has unreferenced vertices"
                )
            triangles = []
            for offset in range(0, len(raw_indices), 3):
                triangle = tuple(
                    new_index_by_old[index]
                    for index in raw_indices[offset : offset + 3]
                )
                triangles.append(
                    min(
                        triangle,
                        (triangle[1], triangle[2], triangle[0]),
                        (triangle[2], triangle[0], triangle[1]),
                    )
                )
            triangles.sort()
            canonical_indices = [
                (index,)
                for triangle in triangles
                for index in triangle
            ]
            if (
                len(canonical_indices) != len(raw_indices)
                or set(index[0] for index in canonical_indices)
                != set(range(count))
            ):
                raise RuntimeError("canonical GLB topology changed")
            primitive["attributes"] = {
                name: append_accessor(
                    attributes[name],
                    sorted_attributes[name],
                    position_bounds=name == "POSITION",
                )
                for name in attribute_names
            }
            primitive["indices"] = append_accessor(
                primitive["indices"],
                canonical_indices,
            )

    document["accessors"] = canonical_accessors
    document["bufferViews"] = canonical_views
    document["buffers"] = [{"byteLength": len(canonical_binary)}]
    json_payload = json.dumps(
        document,
        ensure_ascii=True,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")
    json_payload += b" " * (-len(json_payload) % 4)
    binary_payload = bytes(canonical_binary)
    binary_payload += b"\x00" * (-len(binary_payload) % 4)
    total_length = (
        12
        + 8
        + len(json_payload)
        + 8
        + len(binary_payload)
    )
    output = bytearray(struct.pack("<4sII", b"glTF", 2, total_length))
    output.extend(
        struct.pack("<II", len(json_payload), 0x4E4F534A)
    )
    output.extend(json_payload)
    output.extend(
        struct.pack("<II", len(binary_payload), 0x004E4942)
    )
    output.extend(binary_payload)
    path.write_bytes(output)
