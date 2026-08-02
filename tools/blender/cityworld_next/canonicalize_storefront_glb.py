#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Canonicalize storefront structure and interchangeable vertex identities.

``canonicalize_storefront_structure`` runs before ``canonicalize_static_glb``.
It removes Blender selection/join iteration from material, primitive, mesh, and
node identity.  ``canonicalize_storefront_indices`` then runs after the static
geometry and textureless-UV passes.  It replaces Blender's undefined
textureless tangents with a normal-derived basis, then removes surplus
source-index lanes while retaining the minimum multiplicity needed for each
triangle.  Both passes are fail-closed for the deliberately narrow,
texture-free storefront GLB profile.
"""

from __future__ import annotations

import json
import math
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
    """Remove exporter tangent and duplicate-vertex identity deterministically.

    Storefronts are explicitly texture-free, so Blender's generated tangent
    lanes carry no authored signal.  Blender may split otherwise identical
    position/normal corners into different tangent lanes depending on worker
    scheduling.  Rebuild tangents solely from the canonical normal, merge
    complete interchangeable vertex records, and sort triangle records without
    changing position/normal triangle semantics.
    """

    document, binary = _load_glb(path)
    accessors = document.get("accessors")
    views = document.get("bufferViews")
    buffers = document.get("buffers")
    if (
        not isinstance(accessors, list)
        or not isinstance(views, list)
        or not isinstance(buffers, list)
        or len(buffers) != 1
        or not isinstance(buffers[0], dict)
    ):
        raise RuntimeError("storefront GLB buffer profile changed")
    declared_buffer_length = buffers[0].get("byteLength")
    if (
        not isinstance(declared_buffer_length, int)
        or isinstance(declared_buffer_length, bool)
        or declared_buffer_length < 0
        or not 0 <= len(binary) - declared_buffer_length <= 3
        or any(binary[declared_buffer_length:])
    ):
        raise RuntimeError("storefront GLB buffer profile changed")
    binary = binary[:declared_buffer_length]

    def accessor_layout(
        accessor_index: int,
    ) -> tuple[dict[str, object], int, str, int, int]:
        if (
            not isinstance(accessor_index, int)
            or isinstance(accessor_index, bool)
            or not 0 <= accessor_index < len(accessors)
        ):
            raise RuntimeError("storefront GLB accessor index is invalid")
        accessor = accessors[accessor_index]
        if not isinstance(accessor, dict) or "sparse" in accessor:
            raise RuntimeError("unsupported storefront GLB accessor")
        view_index = accessor.get("bufferView")
        if (
            not isinstance(view_index, int)
            or isinstance(view_index, bool)
            or not 0 <= view_index < len(views)
        ):
            raise RuntimeError("storefront GLB buffer view is invalid")
        view = views[view_index]
        if not isinstance(view, dict) or view.get("buffer") != 0:
            raise RuntimeError("storefront GLB buffer view profile changed")
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
        count = accessor.get("count")
        if (
            not isinstance(count, int)
            or isinstance(count, bool)
            or count <= 0
        ):
            raise RuntimeError("storefront GLB accessor count is invalid")
        offset = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
        if (
            not isinstance(offset, int)
            or isinstance(offset, bool)
            or offset < 0
            or offset + count * packed_size > len(binary)
        ):
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
                binary,
                offset + index * packed_size,
            )
            for index in range(accessor["count"])
        ]

    canonical_binary = bytearray()
    canonical_views: list[dict[str, object]] = []
    canonical_accessors: list[dict[str, object]] = []

    def append_accessor(
        original_index: int,
        values: list[tuple[float | int, ...]],
        *,
        position_bounds: bool = False,
    ) -> int:
        original, _offset, format_code, width, packed_size = accessor_layout(
            original_index
        )
        original_view = views[original["bufferView"]]
        if not values or any(len(value) != width for value in values):
            raise RuntimeError("canonical storefront accessor is invalid")
        while len(canonical_binary) % 4:
            canonical_binary.append(0)
        byte_offset = len(canonical_binary)
        pack_format = "<" + format_code * width
        packed_values: list[tuple[float | int, ...]] = []
        try:
            for value in values:
                packed = struct.pack(pack_format, *value)
                canonical_binary.extend(packed)
                packed_values.append(struct.unpack(pack_format, packed))
        except struct.error as error:
            raise RuntimeError(
                "canonical storefront value exceeds its component type"
            ) from error
        view: dict[str, object] = {
            "buffer": 0,
            "byteLength": len(values) * packed_size,
            "byteOffset": byte_offset,
        }
        if "target" in original_view:
            view["target"] = original_view["target"]
        canonical_views.append(view)
        accessor = {
            key: value
            for key, value in original.items()
            if key
            not in {
                "bufferView",
                "byteOffset",
                "count",
                "max",
                "min",
            }
        }
        accessor["bufferView"] = len(canonical_views) - 1
        accessor["count"] = len(values)
        if position_bounds:
            if format_code != "f" or width != 3:
                raise RuntimeError(
                    "POSITION bounds require float VEC3 values"
                )
            accessor["min"] = [
                min(float(value[axis]) for value in packed_values)
                for axis in range(3)
            ]
            accessor["max"] = [
                max(float(value[axis]) for value in packed_values)
                for axis in range(3)
            ]
        canonical_accessors.append(accessor)
        return len(canonical_accessors) - 1

    def deterministic_textureless_tangent(
        normal: tuple[float | int, ...],
        exported_tangent: tuple[float | int, ...],
    ) -> tuple[float, float, float, float]:
        if len(normal) != 3 or len(exported_tangent) != 4:
            raise RuntimeError("storefront tangent inputs are invalid")
        vector = tuple(float(component) for component in normal)
        exported_handedness = float(exported_tangent[3])
        if (
            not all(math.isfinite(component) for component in vector)
            or not math.isfinite(exported_handedness)
            or exported_handedness not in {-1.0, 1.0}
        ):
            raise RuntimeError("storefront tangent inputs are not canonical")
        normal_length = math.sqrt(sum(component * component for component in vector))
        if abs(normal_length - 1.0) > 2e-3:
            raise RuntimeError("canonical storefront normal is not unit length")
        reference_index = min(
            range(3),
            key=lambda index: (abs(vector[index]), index),
        )
        reference = [0.0, 0.0, 0.0]
        reference[reference_index] = 1.0
        tangent = (
            reference[1] * vector[2] - reference[2] * vector[1],
            reference[2] * vector[0] - reference[0] * vector[2],
            reference[0] * vector[1] - reference[1] * vector[0],
        )
        tangent_length = math.sqrt(
            sum(component * component for component in tangent)
        )
        if tangent_length <= 1e-12:
            raise RuntimeError("cannot derive canonical storefront tangent")
        canonical_xyz = tuple(
            0.0
            if round(component / tangent_length, 3) == 0.0
            else round(component / tangent_length, 3)
            for component in tangent
        )
        canonical_length = math.sqrt(
            sum(component * component for component in canonical_xyz)
        )
        projection = abs(
            sum(canonical_xyz[index] * vector[index] for index in range(3))
        )
        if abs(canonical_length - 1.0) > 2e-3 or projection > 2e-3:
            raise RuntimeError("derived storefront tangent is invalid")
        return (*canonical_xyz, exported_handedness)

    meshes = document.get("meshes")
    if not isinstance(meshes, list) or not meshes:
        raise RuntimeError("storefront GLB meshes must be a non-empty array")
    for mesh in meshes:
        if not isinstance(mesh, dict):
            raise RuntimeError("storefront GLB mesh is not an object")
        primitives = mesh.get("primitives")
        if not isinstance(primitives, list) or not primitives:
            raise RuntimeError("storefront GLB mesh has no primitives")
        for primitive in mesh["primitives"]:
            if not isinstance(primitive, dict):
                raise RuntimeError("storefront GLB primitive is not an object")
            if primitive.get("mode", 4) != 4 or "targets" in primitive:
                raise RuntimeError(
                    "storefront GLB primitive must be an unskinned triangle list"
                )
            attributes = primitive.get("attributes")
            if not isinstance(attributes, dict):
                raise RuntimeError("storefront GLB attributes are invalid")
            if set(attributes) != EXPECTED_ATTRIBUTES:
                raise RuntimeError("unexpected storefront GLB attribute profile")
            expected_attribute_profiles = {
                "NORMAL": (5126, "VEC3"),
                "POSITION": (5126, "VEC3"),
                "TANGENT": (5126, "VEC4"),
                "TEXCOORD_0": (5126, "VEC2"),
            }
            for name, (component_type, accessor_type) in (
                expected_attribute_profiles.items()
            ):
                accessor, _offset, _format, _width, _packed = accessor_layout(
                    attributes[name]
                )
                if (
                    accessor.get("componentType") != component_type
                    or accessor.get("type") != accessor_type
                    or accessor.get("normalized", False) is not False
                ):
                    raise RuntimeError(
                        "storefront GLB attribute accessor profile changed"
                    )
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
            if any(
                not math.isfinite(float(component))
                for values in values_by_attribute.values()
                for value in values
                for component in value
            ):
                raise RuntimeError(
                    "storefront GLB attributes contain non-finite values"
                )
            if any(
                component != 0.0
                for value in values_by_attribute["TEXCOORD_0"]
                for component in value
            ):
                raise RuntimeError("storefront GLB texcoords are not canonical zero")

            keys: list[tuple[float | int, ...]] = []
            records_by_key: dict[
                tuple[float | int, ...],
                dict[str, tuple[float | int, ...]],
            ] = {}
            for index in range(vertex_count):
                record = {
                    "NORMAL": values_by_attribute["NORMAL"][index],
                    "POSITION": values_by_attribute["POSITION"][index],
                    "TANGENT": deterministic_textureless_tangent(
                        values_by_attribute["NORMAL"][index],
                        values_by_attribute["TANGENT"][index],
                    ),
                    "TEXCOORD_0": (0.0, 0.0),
                }
                key = tuple(
                    component
                    for name in attribute_names
                    for component in record[name]
                )
                keys.append(key)
                records_by_key.setdefault(key, record)

            index_accessor = primitive.get("indices")
            (
                index_record,
                _index_offset,
                _index_format,
                _index_width,
                _index_packed,
            ) = accessor_layout(index_accessor)
            if (
                index_record.get("componentType") not in {5123, 5125}
                or index_record.get("type") != "SCALAR"
                or index_record.get("normalized", False) is not False
            ):
                raise RuntimeError(
                    "storefront GLB index accessor profile changed"
                )
            raw_indices = [
                int(value[0]) for value in read_accessor(index_accessor)
            ]
            if len(raw_indices) % 3 != 0:
                raise RuntimeError("storefront GLB indices are not triangles")
            if (
                any(index < 0 or index >= vertex_count for index in raw_indices)
                or set(raw_indices) != set(range(vertex_count))
            ):
                raise RuntimeError("storefront GLB has unreferenced vertices")

            triangle_keys: list[
                tuple[
                    tuple[float | int, ...],
                    tuple[float | int, ...],
                    tuple[float | int, ...],
                ]
            ] = []
            maximum_lanes_by_key: dict[tuple[float | int, ...], int] = {}
            for offset in range(0, len(raw_indices), 3):
                triangle = tuple(
                    keys[index]
                    for index in raw_indices[offset : offset + 3]
                )
                canonical_triangle = min(
                    triangle,
                    (triangle[1], triangle[2], triangle[0]),
                    (triangle[2], triangle[0], triangle[1]),
                )
                triangle_keys.append(canonical_triangle)
                for key in set(canonical_triangle):
                    occurrences = canonical_triangle.count(key)
                    maximum_lanes_by_key[key] = max(
                        maximum_lanes_by_key.get(key, 0),
                        occurrences,
                    )
            triangle_keys.sort()

            sorted_keys = sorted(records_by_key)
            lane_indices_by_key: dict[
                tuple[float | int, ...],
                tuple[int, ...],
            ] = {}
            canonical_record_keys: list[tuple[float | int, ...]] = []
            for key in sorted_keys:
                lane_count = maximum_lanes_by_key.get(key)
                if lane_count not in {1, 2, 3}:
                    raise RuntimeError(
                        "storefront vertex lane multiplicity is invalid"
                    )
                first_index = len(canonical_record_keys)
                canonical_record_keys.extend([key] * lane_count)
                lane_indices_by_key[key] = tuple(
                    range(first_index, first_index + lane_count)
                )

            triangles: list[tuple[int, int, int]] = []
            for triangle in triangle_keys:
                next_lane_by_key: dict[tuple[float | int, ...], int] = {}
                indexed_triangle: list[int] = []
                for key in triangle:
                    lane = next_lane_by_key.get(key, 0)
                    indexed_triangle.append(lane_indices_by_key[key][lane])
                    next_lane_by_key[key] = lane + 1
                if len(set(indexed_triangle)) != 3:
                    raise RuntimeError(
                        "storefront triangle lost distinct vertex lanes"
                    )
                triangles.append(
                    min(
                        tuple(indexed_triangle),
                        (
                            indexed_triangle[1],
                            indexed_triangle[2],
                            indexed_triangle[0],
                        ),
                        (
                            indexed_triangle[2],
                            indexed_triangle[0],
                            indexed_triangle[1],
                        ),
                    )
                )
            triangles.sort()
            canonical_indices = [
                (index,)
                for triangle in triangles
                for index in triangle
            ]
            if len(canonical_indices) != len(raw_indices):
                raise RuntimeError("storefront GLB triangle topology changed")
            primitive["attributes"] = {
                name: append_accessor(
                    attributes[name],
                    [
                        records_by_key[key][name]
                        for key in canonical_record_keys
                    ],
                    position_bounds=name == "POSITION",
                )
                for name in attribute_names
            }
            primitive["indices"] = append_accessor(
                index_accessor,
                canonical_indices,
            )

    document["accessors"] = canonical_accessors
    document["bufferViews"] = canonical_views
    document["buffers"] = [{"byteLength": len(canonical_binary)}]
    _write_glb(path, document, bytes(canonical_binary))
