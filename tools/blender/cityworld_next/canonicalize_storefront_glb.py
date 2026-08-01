#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Canonicalize storefront structure and interchangeable vertex identities.

``canonicalize_storefront_structure`` runs before ``canonicalize_static_glb``.
It removes Blender selection/join iteration from material, primitive, mesh, and
node identity.  ``canonicalize_storefront_indices`` then runs after the static
geometry pass.  It removes source-index identity when two complete vertex
records are interchangeable.  Both passes are fail-closed for the deliberately
narrow, texture-free storefront GLB profile.
"""

from __future__ import annotations

import json
from pathlib import Path
import struct
from typing import Callable


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
MATERIAL_SUFFIX_ORDER = (
    "collision_debug",
    "grounded_concrete",
    "facade",
    "glass",
    "powdercoat_metal",
    "glass_warm_interior",
    "signage",
    "architectural_trim",
    "roof",
    "facade_accent",
)
MESH_SUFFIX_ORDER = (
    "collision_fixture_mesh",
    "lod0_mesh",
    "lod1_mesh",
    "lod2_mesh",
)
NODE_SUFFIX_ORDER = (
    "collision_fixture",
    "lod0",
    "lod1",
    "lod2",
)
LOD2_PRIMITIVE_SUFFIX_ORDER = (
    "grounded_concrete",
    "facade",
    "glass",
    "signage",
    "roof",
    "architectural_trim",
    "powdercoat_metal",
    "glass_warm_interior",
    "facade_accent",
    "collision_debug",
)


def _load_glb(path: Path) -> tuple[dict[str, object], bytes]:
    contents = path.read_bytes()
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
    if json_end + 8 > len(contents):
        raise RuntimeError("exported GLB JSON chunk is truncated")
    document = json.loads(contents[json_start:json_end].rstrip(b" \x00"))
    if not isinstance(document, dict):
        raise RuntimeError("exported GLB document is not an object")
    binary_length, binary_type = struct.unpack_from("<II", contents, json_end)
    if binary_type != 0x004E4942:
        raise RuntimeError("exported GLB has no binary chunk")
    binary_start = json_end + 8
    if binary_start + binary_length != len(contents):
        raise RuntimeError("exported GLB binary chunk is truncated")
    return document, contents[binary_start : binary_start + binary_length]


def _write_glb(path: Path, document: dict[str, object], binary: bytes) -> None:
    json_payload = json.dumps(
        document,
        ensure_ascii=True,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")
    json_payload += b" " * (-len(json_payload) % 4)
    binary_payload = binary + b"\x00" * (-len(binary) % 4)
    total_length = 12 + 8 + len(json_payload) + 8 + len(binary_payload)
    output = bytearray(struct.pack("<4sII", b"glTF", 2, total_length))
    output.extend(struct.pack("<II", len(json_payload), 0x4E4F534A))
    output.extend(json_payload)
    output.extend(struct.pack("<II", len(binary_payload), 0x004E4942))
    output.extend(binary_payload)
    path.write_bytes(output)


def _sorted_named_records(
    records: object,
    label: str,
    sort_key: Callable[[str], object] = lambda name: name,
) -> tuple[list[dict[str, object]], dict[int, int]]:
    if not isinstance(records, list) or not records:
        raise RuntimeError(f"storefront GLB {label} must be a non-empty array")
    indexed: list[tuple[str, int, dict[str, object]]] = []
    names: set[str] = set()
    for index, record in enumerate(records):
        if not isinstance(record, dict):
            raise RuntimeError(f"storefront GLB {label} entry is not an object")
        name = record.get("name")
        if not isinstance(name, str) or not name or name in names:
            raise RuntimeError(
                f"storefront GLB {label} names must be unique and non-empty"
            )
        names.add(name)
        indexed.append((name, index, record))
    indexed.sort(key=lambda item: sort_key(item[0]))
    return (
        [record for _name, _old_index, record in indexed],
        {
            old_index: new_index
            for new_index, (_name, old_index, _record) in enumerate(indexed)
        },
    )


def _suffix_order_key(
    name: str,
    suffixes: tuple[str, ...],
    label: str,
) -> int:
    matches = [
        index
        for index, suffix in enumerate(suffixes)
        if name.endswith("_" + suffix)
    ]
    if len(matches) != 1:
        raise RuntimeError(
            f"storefront GLB {label} name has an unknown semantic suffix"
        )
    return matches[0]


def canonicalize_storefront_structure(path: Path) -> None:
    """Canonicalize all named/order-sensitive storefront GLB structure."""

    document, binary = _load_glb(path)
    for unsupported in ("animations", "cameras", "skins"):
        if document.get(unsupported):
            raise RuntimeError(
                f"storefront GLB unexpectedly contains {unsupported}"
            )

    materials, material_index = _sorted_named_records(
        document.get("materials"),
        "materials",
        lambda name: _suffix_order_key(
            name,
            MATERIAL_SUFFIX_ORDER,
            "material",
        ),
    )
    raw_meshes = document.get("meshes")
    if not isinstance(raw_meshes, list):
        raise RuntimeError("storefront GLB meshes must be an array")
    for mesh in raw_meshes:
        if not isinstance(mesh, dict):
            raise RuntimeError("storefront GLB mesh is not an object")
        primitives = mesh.get("primitives")
        if not isinstance(primitives, list) or not primitives:
            raise RuntimeError("storefront GLB mesh has no primitives")
        remapped: list[dict[str, object]] = []
        seen_materials: set[int] = set()
        for primitive in primitives:
            if not isinstance(primitive, dict):
                raise RuntimeError("storefront GLB primitive is not an object")
            old_material = primitive.get("material")
            if (
                not isinstance(old_material, int)
                or isinstance(old_material, bool)
                or old_material not in material_index
            ):
                raise RuntimeError(
                    "storefront GLB primitive has an invalid material"
                )
            new_material = material_index[old_material]
            if new_material in seen_materials:
                raise RuntimeError(
                    "storefront GLB mesh repeats a material primitive"
                )
            seen_materials.add(new_material)
            primitive["material"] = new_material
            remapped.append(primitive)
        mesh_name = mesh.get("name")
        if not isinstance(mesh_name, str):
            raise RuntimeError("storefront GLB mesh name is invalid")
        primitive_suffix_order = (
            LOD2_PRIMITIVE_SUFFIX_ORDER
            if mesh_name.endswith("_lod2_mesh")
            else MATERIAL_SUFFIX_ORDER
        )
        remapped.sort(
            key=lambda primitive: _suffix_order_key(
                str(materials[int(primitive["material"])]["name"]),
                primitive_suffix_order,
                "primitive material",
            )
        )
        mesh["primitives"] = remapped

    meshes, mesh_index = _sorted_named_records(
        raw_meshes,
        "meshes",
        lambda name: _suffix_order_key(name, MESH_SUFFIX_ORDER, "mesh"),
    )
    raw_nodes = document.get("nodes")
    if not isinstance(raw_nodes, list):
        raise RuntimeError("storefront GLB nodes must be an array")
    for node in raw_nodes:
        if not isinstance(node, dict):
            raise RuntimeError("storefront GLB node is not an object")
        old_mesh = node.get("mesh")
        if (
            not isinstance(old_mesh, int)
            or isinstance(old_mesh, bool)
            or old_mesh not in mesh_index
        ):
            raise RuntimeError("storefront GLB node has an invalid mesh")
        node["mesh"] = mesh_index[old_mesh]

    nodes, node_index = _sorted_named_records(
        raw_nodes,
        "nodes",
        lambda name: _suffix_order_key(name, NODE_SUFFIX_ORDER, "node"),
    )
    for node in nodes:
        children = node.get("children")
        if children is None:
            continue
        if (
            not isinstance(children, list)
            or any(
                not isinstance(child, int)
                or isinstance(child, bool)
                or child not in node_index
                for child in children
            )
        ):
            raise RuntimeError("storefront GLB node children are invalid")
        node["children"] = sorted(node_index[child] for child in children)

    raw_scenes = document.get("scenes")
    if not isinstance(raw_scenes, list):
        raise RuntimeError("storefront GLB scenes must be an array")
    for scene in raw_scenes:
        if not isinstance(scene, dict):
            raise RuntimeError("storefront GLB scene is not an object")
        scene_nodes = scene.get("nodes")
        if (
            not isinstance(scene_nodes, list)
            or any(
                not isinstance(node, int)
                or isinstance(node, bool)
                or node not in node_index
                for node in scene_nodes
            )
        ):
            raise RuntimeError("storefront GLB scene nodes are invalid")
        scene["nodes"] = sorted(node_index[node] for node in scene_nodes)
    scenes, scene_index = _sorted_named_records(raw_scenes, "scenes")
    active_scene = document.get("scene")
    if (
        not isinstance(active_scene, int)
        or isinstance(active_scene, bool)
        or active_scene not in scene_index
    ):
        raise RuntimeError("storefront GLB active scene is invalid")

    document["materials"] = materials
    document["meshes"] = meshes
    document["nodes"] = nodes
    document["scenes"] = scenes
    document["scene"] = scene_index[active_scene]
    _write_glb(path, document, binary)


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
