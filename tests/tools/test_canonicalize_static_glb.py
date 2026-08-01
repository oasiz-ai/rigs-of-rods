#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
from typing import Any


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = (
    REPOSITORY_ROOT
    / "tools/blender/cityworld_next/canonicalize_static_glb.py"
)
SPEC = importlib.util.spec_from_file_location(
    "canonicalize_static_glb",
    TOOL_PATH,
)
assert SPEC is not None and SPEC.loader is not None
CANONICALIZER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CANONICALIZER)
STOREFRONT_TOOL_PATH = (
    REPOSITORY_ROOT
    / "tools/blender/cityworld_next/canonicalize_storefront_glb.py"
)
STOREFRONT_SPEC = importlib.util.spec_from_file_location(
    "canonicalize_storefront_glb",
    STOREFRONT_TOOL_PATH,
)
assert STOREFRONT_SPEC is not None and STOREFRONT_SPEC.loader is not None
STOREFRONT_CANONICALIZER = importlib.util.module_from_spec(STOREFRONT_SPEC)
STOREFRONT_SPEC.loader.exec_module(STOREFRONT_CANONICALIZER)

JSON_CHUNK = 0x4E4F534A
BIN_CHUNK = 0x004E4942
COMPONENT_FORMATS = {
    5123: ("H", 2),
    5126: ("f", 4),
}
ACCESSOR_WIDTHS = {
    "SCALAR": 1,
    "VEC2": 2,
    "VEC3": 3,
    "VEC4": 4,
}


def padded(payload: bytes, fill: bytes) -> bytes:
    return payload + fill * (-len(payload) % 4)


def build_test_glb(
    *,
    positions_override: list[tuple[float, float, float]] | None = None,
    texcoords_override: list[tuple[float, float]] | None = None,
    indices_override: list[tuple[int]] | None = None,
) -> bytes:
    binary = bytearray()
    views: list[dict[str, Any]] = []
    accessors: list[dict[str, Any]] = []

    def append_accessor(
        values: list[tuple[int | float, ...]],
        component_type: int,
        accessor_type: str,
    ) -> int:
        format_code, component_size = COMPONENT_FORMATS[component_type]
        width = ACCESSOR_WIDTHS[accessor_type]
        if any(len(value) != width for value in values):
            raise AssertionError("test accessor width mismatch")
        while len(binary) % 4:
            binary.append(0)
        byte_offset = len(binary)
        pack_format = "<" + format_code * width
        packed_values = []
        for value in values:
            packed = struct.pack(pack_format, *value)
            binary.extend(packed)
            packed_values.append(struct.unpack(pack_format, packed))
        views.append(
            {
                "buffer": 0,
                "byteLength": len(values) * component_size * width,
                "byteOffset": byte_offset,
                "target": (
                    34963
                    if accessor_type == "SCALAR"
                    else 34962
                ),
            }
        )
        accessors.append(
            {
                "bufferView": len(views) - 1,
                "componentType": component_type,
                "count": len(values),
                "max": [
                    max(value[axis] for value in packed_values)
                    for axis in range(width)
                ],
                "min": [
                    min(value[axis] for value in packed_values)
                    for axis in range(width)
                ],
                "type": accessor_type,
            }
        )
        return len(accessors) - 1

    position_values = positions_override or [
        (0.1900000125, -2.3456788, 4.0000004),
        (1.0000004, 0.0, -0.0000004),
        (-3.0000004, 2.0000004, 1.0),
    ]
    positions = append_accessor(
        position_values,
        5126,
        "VEC3",
    )
    normals = append_accessor(
        [(0.0, 0.0, 1.0)] * len(position_values),
        5126,
        "VEC3",
    )
    tangents = append_accessor(
        [(1.0, 0.0, 0.0, 1.0)] * len(position_values),
        5126,
        "VEC4",
    )
    texcoord_values = texcoords_override or [
        (0.12345678, 0.0),
        (1.0, 0.0),
        (0.0, 1.0),
    ]
    if len(texcoord_values) != len(position_values):
        raise AssertionError("test texcoord count mismatch")
    texcoords = append_accessor(
        texcoord_values,
        5126,
        "VEC2",
    )
    indices = append_accessor(
        indices_override or [(0,), (1,), (2,)],
        5123,
        "SCALAR",
    )
    document = {
        "accessors": accessors,
        "asset": {"generator": "canonicalizer-test", "version": "2.0"},
        "bufferViews": views,
        "buffers": [{"byteLength": len(binary)}],
        "meshes": [
            {
                "name": "test_triangle",
                "primitives": [
                    {
                        "attributes": {
                            "NORMAL": normals,
                            "POSITION": positions,
                            "TANGENT": tangents,
                            "TEXCOORD_0": texcoords,
                        },
                        "indices": indices,
                        "mode": 4,
                    }
                ],
            }
        ],
        "nodes": [{"mesh": 0, "name": "test_triangle"}],
        "scene": 0,
        "scenes": [{"nodes": [0]}],
    }
    json_payload = padded(
        json.dumps(
            document,
            ensure_ascii=True,
            separators=(",", ":"),
            sort_keys=True,
        ).encode("utf-8"),
        b" ",
    )
    binary_payload = padded(bytes(binary), b"\x00")
    length = 12 + 8 + len(json_payload) + 8 + len(binary_payload)
    return b"".join(
        (
            struct.pack("<4sII", b"glTF", 2, length),
            struct.pack("<II", len(json_payload), JSON_CHUNK),
            json_payload,
            struct.pack("<II", len(binary_payload), BIN_CHUNK),
            binary_payload,
        )
    )


def read_glb(path: Path) -> tuple[dict[str, Any], bytes]:
    contents = path.read_bytes()
    _magic, _version, length = struct.unpack_from("<4sII", contents, 0)
    if length != len(contents):
        raise AssertionError("test GLB length mismatch")
    json_length, json_type = struct.unpack_from("<II", contents, 12)
    if json_type != JSON_CHUNK:
        raise AssertionError("test GLB JSON chunk mismatch")
    json_start = 20
    json_end = json_start + json_length
    document = json.loads(
        contents[json_start:json_end].rstrip(b" \x00")
    )
    binary_length, binary_type = struct.unpack_from(
        "<II",
        contents,
        json_end,
    )
    if binary_type != BIN_CHUNK:
        raise AssertionError("test GLB binary chunk mismatch")
    binary_start = json_end + 8
    return document, contents[binary_start : binary_start + binary_length]


def read_accessor(
    document: dict[str, Any],
    binary: bytes,
    accessor_index: int,
) -> list[tuple[int | float, ...]]:
    accessor = document["accessors"][accessor_index]
    view = document["bufferViews"][accessor["bufferView"]]
    format_code, component_size = COMPONENT_FORMATS[
        accessor["componentType"]
    ]
    width = ACCESSOR_WIDTHS[accessor["type"]]
    packed_size = component_size * width
    offset = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    unpack_format = "<" + format_code * width
    return [
        struct.unpack_from(
            unpack_format,
            binary,
            offset + index * packed_size,
        )
        for index in range(accessor["count"])
    ]


class StaticGlbCanonicalizerTests(unittest.TestCase):
    def test_duplicate_vertex_identity_does_not_change_output(self) -> None:
        positions = [
            (0.0, 0.0, 0.0),
            (0.0, 0.0, 0.0),
            (1.0, 0.0, 0.0),
            (0.0, 1.0, 0.0),
        ]
        texcoords = [
            (0.0, 0.0),
            (0.0, 0.0),
            (1.0, 0.0),
            (0.0, 1.0),
        ]
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            left = root / "left.glb"
            right = root / "right.glb"
            left.write_bytes(
                build_test_glb(
                    positions_override=positions,
                    texcoords_override=texcoords,
                    indices_override=[
                        (0,), (2,), (3,),
                        (1,), (3,), (2,),
                    ],
                )
            )
            right.write_bytes(
                build_test_glb(
                    positions_override=positions,
                    texcoords_override=texcoords,
                    indices_override=[
                        (1,), (2,), (3,),
                        (0,), (3,), (2,),
                    ],
                )
            )

            CANONICALIZER.canonicalize_glb_geometry(left)
            CANONICALIZER.canonicalize_glb_geometry(right)
            self.assertNotEqual(left.read_bytes(), right.read_bytes())

            STOREFRONT_CANONICALIZER.canonicalize_storefront_indices(left)
            STOREFRONT_CANONICALIZER.canonicalize_storefront_indices(right)

            self.assertEqual(left.read_bytes(), right.read_bytes())
            canonical = left.read_bytes()
            STOREFRONT_CANONICALIZER.canonicalize_storefront_indices(left)
            self.assertEqual(left.read_bytes(), canonical)
            document, binary = read_glb(left)
            primitive = document["meshes"][0]["primitives"][0]
            for accessor_index in primitive["attributes"].values():
                self.assertEqual(
                    document["accessors"][accessor_index]["count"],
                    4,
                )
            indices = read_accessor(
                document,
                binary,
                primitive["indices"],
            )
            self.assertEqual(len(indices), 6)
            self.assertEqual({value[0] for value in indices}, {0, 1, 2, 3})

    def test_bounds_match_packed_positions_and_output_is_idempotent(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "fixture.glb"
            path.write_bytes(build_test_glb())
            before, _binary = read_glb(path)
            before_position = before["accessors"][0]

            CANONICALIZER.canonicalize_glb_geometry(path)
            first = path.read_bytes()
            document, binary = read_glb(path)
            primitive = document["meshes"][0]["primitives"][0]
            position_index = primitive["attributes"]["POSITION"]
            position = document["accessors"][position_index]
            values = read_accessor(document, binary, position_index)
            expected_min = [
                min(value[axis] for value in values)
                for axis in range(3)
            ]
            expected_max = [
                max(value[axis] for value in values)
                for axis in range(3)
            ]

            self.assertEqual(position["min"], expected_min)
            self.assertEqual(position["max"], expected_max)
            self.assertNotEqual(position["min"], before_position["min"])
            self.assertNotEqual(position["max"], before_position["max"])

            for semantic, accessor_index in primitive[
                "attributes"
            ].items():
                accessor = document["accessors"][accessor_index]
                if semantic != "POSITION":
                    self.assertNotIn("min", accessor)
                    self.assertNotIn("max", accessor)

            index = document["accessors"][primitive["indices"]]
            index_values = read_accessor(
                document,
                binary,
                primitive["indices"],
            )
            self.assertEqual(index["componentType"], 5123)
            self.assertEqual(index["type"], "SCALAR")
            self.assertNotIn("min", index)
            self.assertNotIn("max", index)
            self.assertEqual(
                {value[0] for value in index_values},
                {0, 1, 2},
            )
            self.assertTrue(
                all(
                    isinstance(value[0], int)
                    for value in index_values
                )
            )

            CANONICALIZER.canonicalize_glb_geometry(path)
            self.assertEqual(path.read_bytes(), first)

    def test_hostile_truncation_fails_without_rewriting_input(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "truncated.glb"
            payload = build_test_glb()[:-3]
            path.write_bytes(payload)

            with self.assertRaises(RuntimeError):
                CANONICALIZER.canonicalize_glb_geometry(path)

            self.assertEqual(path.read_bytes(), payload)


if __name__ == "__main__":
    unittest.main()
