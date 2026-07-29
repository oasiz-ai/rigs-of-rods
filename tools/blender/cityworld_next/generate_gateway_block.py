#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the detailed v2 40 m CityWorld gateway streetscape module."""

from __future__ import annotations

import argparse
import importlib.util
import json
import math
from pathlib import Path
import struct
import sys

import bmesh
import bpy
from mathutils import Vector


SCRIPT_DIRECTORY = Path(__file__).resolve().parent
BASE_GENERATOR_PATH = SCRIPT_DIRECTORY / "generate_bridge_kit.py"
BASE_SPEC = importlib.util.spec_from_file_location(
    "rorng_gateway_block_helpers",
    BASE_GENERATOR_PATH,
)
if BASE_SPEC is None or BASE_SPEC.loader is None:
    raise RuntimeError("cannot load the CityWorld authoring helpers")
BASE = importlib.util.module_from_spec(BASE_SPEC)
BASE_SPEC.loader.exec_module(BASE)

ASSET_ID = "rorng_city_gateway_block_40m"
ASSET_VERSION = 2
GENERATOR_ID = "ror-cityworld-gateway-block-generator-v2"
LENGTH_M = 40.0
WIDTH_M = 34.0
ROAD_WIDTH_M = 8.9
ROAD_SURFACE_Z_M = 0.0


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

    canonical_binary = bytearray()
    canonical_views: list[dict[str, object]] = []
    canonical_accessors: list[dict[str, object]] = []

    def append_accessor(
        original_index: int,
        values: list[tuple[float, ...]],
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
        for value in values:
            canonical_binary.extend(struct.pack(pack_format, *value))
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
            if key not in {"bufferView", "byteOffset"}
        }
        accessor["bufferView"] = len(canonical_views) - 1
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
                raise RuntimeError("unexpected static GLB attribute profile")
            raw = {
                name: read_accessor(accessor_index)
                for name, accessor_index in attributes.items()
            }
            count = len(raw["POSITION"])
            if any(len(values) != count for values in raw.values()):
                raise RuntimeError("mismatched static GLB attribute counts")

            canonical: dict[str, list[tuple[float, ...]]] = {
                name: [] for name in sorted(attributes)
            }
            for index in range(count):
                normal = normalized_vector(raw["NORMAL"][index])
                canonical["NORMAL"].append(normal)
                canonical["POSITION"].append(
                    tuple(float(value) for value in raw["POSITION"][index])
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
                        tuple(canonical[name][index] for name in attribute_names),
                    )
                )
            records.sort(key=lambda item: (item[0], item[1]))
            new_index_by_old: dict[int, int] = {}
            sorted_attributes: dict[str, list[tuple[float, ...]]] = {
                name: [] for name in attribute_names
            }
            for new_index, (_key, old_index, values) in enumerate(records):
                new_index_by_old[old_index] = new_index
                for name, value in zip(attribute_names, values):
                    sorted_attributes[name].append(value)

            raw_indices = [
                int(value[0])
                for value in read_accessor(primitive["indices"])
            ]
            if len(raw_indices) % 3 != 0:
                raise RuntimeError("canonical GLB indices are not triangles")
            if set(raw_indices) != set(range(count)):
                raise RuntimeError("canonical GLB has unreferenced vertices")
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
    output.extend(struct.pack("<II", len(json_payload), 0x4E4F534A))
    output.extend(json_payload)
    output.extend(struct.pack("<II", len(binary_payload), 0x004E4942))
    output.extend(binary_payload)
    path.write_bytes(output)


def parse_args() -> argparse.Namespace:
    argv = sys.argv
    script_args = argv[argv.index("--") + 1 :] if "--" in argv else []
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output-root",
        type=Path,
        default=Path(__file__).resolve().parents[3],
    )
    parser.add_argument("--preview-path", type=Path)
    return parser.parse_args(script_args)


def reset_scene_fully() -> None:
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    for collection in list(bpy.data.collections):
        bpy.data.collections.remove(collection)
    for datablocks in (
        bpy.data.meshes,
        bpy.data.curves,
        bpy.data.materials,
        bpy.data.cameras,
        bpy.data.lights,
    ):
        for datablock in list(datablocks):
            datablocks.remove(datablock)
    BASE.reset_scene()


def make_materials() -> dict[str, bpy.types.Material]:
    return {
        "asphalt": BASE.make_material(
            "rorng_gateway_asphalt",
            (0.032, 0.038, 0.045, 1.0),
            metallic=0.0,
            roughness=0.91,
        ),
        "bark": BASE.make_material(
            "rorng_gateway_tree_bark",
            (0.13, 0.065, 0.025, 1.0),
            metallic=0.0,
            roughness=0.96,
        ),
        "brick": BASE.make_material(
            "rorng_gateway_brick",
            (0.29, 0.075, 0.045, 1.0),
            metallic=0.0,
            roughness=0.82,
        ),
        "collision": BASE.make_material(
            "rorng_gateway_collision_debug",
            (0.8, 0.04, 0.03, 1.0),
            metallic=0.0,
            roughness=1.0,
        ),
        "concrete": BASE.make_material(
            "rorng_gateway_architectural_concrete",
            (0.42, 0.43, 0.41, 1.0),
            metallic=0.0,
            roughness=0.72,
        ),
        "emissive": BASE.make_material(
            "rorng_gateway_lamp_emissive",
            (1.0, 0.69, 0.25, 1.0),
            metallic=0.0,
            roughness=0.22,
            emission=(1.0, 0.69, 0.25),
            emission_strength=1.0,
        ),
        "glass": BASE.make_material(
            "rorng_gateway_glass_blue",
            (0.055, 0.16, 0.24, 1.0),
            metallic=0.28,
            roughness=0.16,
        ),
        "lane_white": BASE.make_material(
            "rorng_gateway_lane_white",
            (0.82, 0.84, 0.79, 1.0),
            metallic=0.0,
            roughness=0.66,
        ),
        "lane_yellow": BASE.make_material(
            "rorng_gateway_lane_yellow",
            (0.93, 0.58, 0.035, 1.0),
            metallic=0.0,
            roughness=0.6,
        ),
        "leaf_dark": BASE.make_material(
            "rorng_gateway_leaf_dark",
            (0.035, 0.16, 0.045, 1.0),
            metallic=0.0,
            roughness=0.88,
        ),
        "leaf_light": BASE.make_material(
            "rorng_gateway_leaf_light",
            (0.08, 0.28, 0.075, 1.0),
            metallic=0.0,
            roughness=0.84,
        ),
        "metal": BASE.make_material(
            "rorng_gateway_powdercoat_metal",
            (0.025, 0.035, 0.045, 1.0),
            metallic=0.8,
            roughness=0.27,
        ),
        "preview_ground": BASE.make_material(
            "rorng_preview_ground",
            (0.055, 0.07, 0.075, 1.0),
            metallic=0.0,
            roughness=0.95,
        ),
        "sidewalk": BASE.make_material(
            "rorng_gateway_sidewalk",
            (0.32, 0.34, 0.35, 1.0),
            metallic=0.0,
            roughness=0.86,
        ),
        "stone": BASE.make_material(
            "rorng_gateway_stone",
            (0.27, 0.25, 0.22, 1.0),
            metallic=0.0,
            roughness=0.78,
        ),
    }


def add_box(
    parts: list[bpy.types.Object],
    name: str,
    dimensions: tuple[float, float, float],
    location: tuple[float, float, float],
    material: bpy.types.Material,
    collection: bpy.types.Collection,
    *,
    bevel: float = 0.0,
    bevel_segments: int = 1,
) -> None:
    parts.append(
        BASE.make_box(
            name,
            dimensions=dimensions,
            location=location,
            material=material,
            collection=collection,
            bevel=bevel,
            bevel_segments=bevel_segments,
        )
    )


def add_cylinder(
    parts: list[bpy.types.Object],
    name: str,
    *,
    radius: float,
    depth: float,
    location: tuple[float, float, float],
    vertices: int,
    material: bpy.types.Material,
    collection: bpy.types.Collection,
    rotation: tuple[float, float, float] = (0.0, 0.0, 0.0),
) -> None:
    parts.append(
        BASE.make_cylinder(
            name,
            radius=radius,
            depth=depth,
            location=location,
            rotation=rotation,
            vertices=vertices,
            material=material,
            collection=collection,
            bevel=0.0,
        )
    )


def add_cylinder_between(
    parts: list[bpy.types.Object],
    name: str,
    *,
    start: tuple[float, float, float],
    end: tuple[float, float, float],
    radius: float,
    vertices: int,
    material: bpy.types.Material,
    collection: bpy.types.Collection,
) -> None:
    start_point = Vector(start)
    end_point = Vector(end)
    direction = end_point - start_point
    if direction.length <= 0.0:
        raise RuntimeError(f"zero-length cylinder requested for {name}")
    rotation = direction.to_track_quat("Z", "Y").to_euler()
    add_cylinder(
        parts,
        name,
        radius=radius,
        depth=direction.length,
        location=tuple((start_point + end_point) * 0.5),
        rotation=tuple(rotation),
        vertices=vertices,
        material=material,
        collection=collection,
    )


def add_canopy_lobe(
    parts: list[bpy.types.Object],
    name: str,
    *,
    location: tuple[float, float, float],
    scale: tuple[float, float, float],
    segments: int,
    ring_count: int,
    material: bpy.types.Material,
    collection: bpy.types.Collection,
) -> None:
    bpy.ops.mesh.primitive_uv_sphere_add(
        segments=segments,
        ring_count=ring_count,
        radius=1.0,
        location=location,
    )
    obj = bpy.context.object
    obj.name = name
    obj.scale = scale
    BASE.apply_transform(obj)
    for polygon in obj.data.polygons:
        polygon.use_smooth = True
    BASE.assign_material(obj, material)
    BASE.move_to_collection(obj, collection)
    parts.append(obj)


def join_render_components_deterministically(
    components: list[bpy.types.Object],
    *,
    name: str,
    lod: int,
) -> bpy.types.Object:
    if not components:
        raise RuntimeError(f"no render components supplied for {name}")
    collection = components[0].users_collection[0]
    merged = bmesh.new()
    materials: list[bpy.types.Material] = []
    material_indices: dict[str, int] = {}
    try:
        for component in components:
            source_materials = list(component.data.materials)
            if not source_materials:
                raise RuntimeError(
                    f"render component has no material: {component.name}"
                )
            previous_face_count = len(merged.faces)
            merged.from_mesh(component.data)
            merged.faces.ensure_lookup_table()
            new_faces = list(merged.faces)[previous_face_count:]
            for face in new_faces:
                if face.material_index >= len(source_materials):
                    raise RuntimeError(
                        f"invalid material slot on {component.name}"
                    )
                material = source_materials[face.material_index]
                if material.name not in material_indices:
                    material_indices[material.name] = len(materials)
                    materials.append(material)
                face.material_index = material_indices[material.name]

        mesh = bpy.data.meshes.new(f"{name}_mesh")
        merged.to_mesh(mesh)
    finally:
        merged.free()

    for material in materials:
        mesh.materials.append(material)
    obj = bpy.data.objects.new(name, mesh)
    collection.objects.link(obj)
    for component in components:
        bpy.data.objects.remove(component, do_unlink=True)

    obj["rorng_asset_id"] = ASSET_ID
    obj["rorng_role"] = "render"
    obj["rorng_units"] = "metres"
    obj["rorng_lod"] = lod
    modifier = obj.modifiers.new(name="rorng_triangulate", type="TRIANGULATE")
    modifier.keep_custom_normals = True
    bpy.ops.object.select_all(action="DESELECT")
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    obj.select_set(False)
    return obj


def add_tree(
    parts: list[bpy.types.Object],
    *,
    prefix: str,
    x: float,
    y: float,
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
    detail: int,
    variant: int,
) -> None:
    lean_x, lean_y, height_scale = (
        (-0.12, 0.08, 0.96),
        (0.1, -0.05, 1.04),
        (0.04, 0.13, 1.0),
    )[variant % 3]
    trunk_mid = (
        x + lean_x * 0.45,
        y + lean_y * 0.45,
        2.05 * height_scale,
    )
    trunk_top = (
        x + lean_x,
        y + lean_y,
        3.65 * height_scale,
    )
    if detail == 0:
        add_cylinder(
            parts,
            f"{prefix}_root_flare",
            radius=0.34,
            depth=0.38,
            location=(x, y, 0.19),
            vertices=12,
            material=materials["bark"],
            collection=collection,
        )
    add_cylinder_between(
        parts,
        f"{prefix}_trunk_lower",
        start=(x, y, 0.1),
        end=trunk_mid,
        radius=0.24 if detail == 0 else 0.21,
        vertices=12 if detail == 0 else 8,
        material=materials["bark"],
        collection=collection,
    )
    add_cylinder_between(
        parts,
        f"{prefix}_trunk_upper",
        start=trunk_mid,
        end=trunk_top,
        radius=0.17 if detail == 0 else 0.15,
        vertices=10 if detail == 0 else 7,
        material=materials["bark"],
        collection=collection,
    )

    branch_angles = (
        (18.0, 91.0, 166.0, 238.0, 311.0)
        if detail == 0
        else (38.0, 218.0)
    )
    canopy_centres: list[tuple[float, float, float]] = []
    for index, angle_degrees in enumerate(branch_angles):
        angle = math.radians(angle_degrees + variant * 17.0)
        reach = (
            (1.14, 1.34, 1.08, 1.28, 1.18)[index]
            if detail == 0
            else (0.95, 1.0)[index]
        )
        branch_start_z = 2.35 + (index % 2) * 0.32
        branch_start = (
            x + lean_x * 0.62,
            y + lean_y * 0.62,
            branch_start_z * height_scale,
        )
        branch_end = (
            trunk_top[0] + math.cos(angle) * reach,
            trunk_top[1] + math.sin(angle) * reach,
            (3.95 + (index % 3) * 0.37) * height_scale,
        )
        add_cylinder_between(
            parts,
            f"{prefix}_branch_{index}",
            start=branch_start,
            end=branch_end,
            radius=0.095 if detail == 0 else 0.08,
            vertices=8 if detail == 0 else 6,
            material=materials["bark"],
            collection=collection,
        )
        canopy_centres.append(branch_end)

    central_canopy = (
        trunk_top[0] - lean_x * 0.35,
        trunk_top[1] - lean_y * 0.35,
        4.65 * height_scale,
    )
    lobe_specs = (
        [
            (central_canopy, (1.35, 1.2, 1.42)),
            *[
                (
                    (
                        centre[0],
                        centre[1],
                        centre[2] + 0.36 + 0.08 * (index % 2),
                    ),
                    (
                        1.02 + 0.1 * (index % 2),
                        0.94 + 0.08 * ((index + variant) % 3),
                        1.08 + 0.06 * ((index + 1) % 2),
                    ),
                )
                for index, centre in enumerate(canopy_centres)
            ],
        ]
        if detail == 0
        else [
            (central_canopy, (1.42, 1.28, 1.38)),
            (
                (
                    canopy_centres[0][0],
                    canopy_centres[0][1],
                    canopy_centres[0][2] + 0.32,
                ),
                (1.05, 1.0, 1.12),
            ),
            (
                (
                    canopy_centres[1][0],
                    canopy_centres[1][1],
                    canopy_centres[1][2] + 0.32,
                ),
                (1.02, 1.04, 1.08),
            ),
        ]
    )
    for index, (centre, scale) in enumerate(lobe_specs):
        add_canopy_lobe(
            parts,
            f"{prefix}_canopy_lobe_{index}",
            location=centre,
            scale=scale,
            segments=12 if detail == 0 else 8,
            ring_count=6 if detail == 0 else 4,
            material=(
                materials["leaf_light"]
                if (index + variant) % 3 == 1
                else materials["leaf_dark"]
            ),
            collection=collection,
        )


def add_facade_window_frame(
    parts: list[bpy.types.Object],
    *,
    prefix: str,
    side: float,
    facade_x: float,
    y: float,
    z: float,
    opening_width: float,
    material: bpy.types.Material,
    collection: bpy.types.Collection,
) -> None:
    frame_x = facade_x - side * 0.055
    border = 0.085
    for label, local_y, local_z, width, height in (
        ("left", y - opening_width / 2.0, z, border, 1.48),
        ("right", y + opening_width / 2.0, z, border, 1.48),
        ("top", y, z + 0.7, opening_width, border),
        ("sill", y, z - 0.7, opening_width, 0.11),
    ):
        add_box(
            parts,
            f"{prefix}_frame_{label}",
            (0.1, width, height),
            (frame_x, local_y, local_z),
            material,
            collection,
        )
    if opening_width > 1.05:
        add_box(
            parts,
            f"{prefix}_frame_mullion",
            (0.105, 0.065, 1.36),
            (frame_x - side * 0.004, y, z),
            material,
            collection,
        )


def add_end_window_frame(
    parts: list[bpy.types.Object],
    *,
    prefix: str,
    x: float,
    facade_y: float,
    z: float,
    opening_width: float,
    material: bpy.types.Material,
    collection: bpy.types.Collection,
) -> None:
    frame_y = facade_y - 0.055
    border = 0.085
    for label, local_x, local_z, width, height in (
        ("left", x - opening_width / 2.0, z, border, 1.48),
        ("right", x + opening_width / 2.0, z, border, 1.48),
        ("top", x, z + 0.7, opening_width, border),
        ("sill", x, z - 0.7, opening_width, 0.11),
    ):
        add_box(
            parts,
            f"{prefix}_frame_{label}",
            (width, 0.1, height),
            (local_x, frame_y, local_z),
            material,
            collection,
        )
    if opening_width > 1.05:
        add_box(
            parts,
            f"{prefix}_frame_mullion",
            (0.065, 0.105, 1.36),
            (x, frame_y - 0.004, z),
            material,
            collection,
        )


def add_balcony(
    parts: list[bpy.types.Object],
    *,
    prefix: str,
    side: float,
    facade_x: float,
    y: float,
    z: float,
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
) -> None:
    slab_centre_x = facade_x - side * 0.38
    rail_x = facade_x - side * 0.76
    add_box(
        parts,
        f"{prefix}_slab",
        (0.82, 4.35, 0.16),
        (slab_centre_x, y, z),
        materials["concrete"],
        collection,
        bevel=0.025,
    )
    add_box(
        parts,
        f"{prefix}_front_rail",
        (0.065, 4.25, 0.82),
        (rail_x, y, z + 0.48),
        materials["metal"],
        collection,
    )
    for end_label, end_y in (
        ("near", y - 2.12),
        ("far", y + 2.12),
    ):
        add_box(
            parts,
            f"{prefix}_{end_label}_rail",
            (0.72, 0.065, 0.82),
            (slab_centre_x, end_y, z + 0.48),
            materials["metal"],
            collection,
        )


def add_roof_silhouette(
    parts: list[bpy.types.Object],
    *,
    prefix: str,
    x: float,
    y: float,
    height: float,
    depth_x: float,
    width_y: float,
    style: int,
    lod: int,
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
) -> None:
    add_box(
        parts,
        f"{prefix}_roof_trim",
        (depth_x + 0.18, width_y + 0.18, 0.32),
        (x, y, height + 0.16),
        materials["stone"],
        collection,
        bevel=0.035 if lod == 0 else 0.0,
    )
    penthouse_width = 3.1 + 0.35 * (style % 2)
    penthouse_depth = 3.6 + 0.4 * ((style + 1) % 2)
    penthouse_height = 1.15 if height >= 19.5 else 1.35
    add_box(
        parts,
        f"{prefix}_roof_penthouse",
        (penthouse_depth, penthouse_width, penthouse_height),
        (
            x + (0.45 if style % 2 == 0 else -0.55),
            y + (0.9 if style < 2 else -0.8),
            height + 0.32 + penthouse_height / 2.0,
        ),
        materials["metal"] if style == 2 else materials["concrete"],
        collection,
        bevel=0.045 if lod == 0 else 0.0,
    )
    if lod != 0:
        return
    parapet_height = 0.48
    for label, dimensions, location in (
        (
            "street",
            (0.18, width_y, parapet_height),
            (x - math.copysign(depth_x / 2.0, x), y, height + 0.5),
        ),
        (
            "outer",
            (0.18, width_y, parapet_height),
            (x + math.copysign(depth_x / 2.0, x), y, height + 0.5),
        ),
        (
            "near",
            (depth_x, 0.18, parapet_height),
            (x, y - width_y / 2.0, height + 0.5),
        ),
        (
            "far",
            (depth_x, 0.18, parapet_height),
            (x, y + width_y / 2.0, height + 0.5),
        ),
    ):
        add_box(
            parts,
            f"{prefix}_parapet_{label}",
            dimensions,
            location,
            materials["stone"],
            collection,
        )
    for unit in (-1.0, 1.0):
        add_box(
            parts,
            f"{prefix}_hvac_{'a' if unit < 0 else 'b'}",
            (1.3, 1.05, 0.62),
            (
                x - 0.65,
                y + unit * width_y * 0.22,
                height + 0.63,
            ),
            materials["metal"],
            collection,
            bevel=0.035,
        )


def add_streetlight(
    parts: list[bpy.types.Object],
    *,
    prefix: str,
    x: float,
    y: float,
    inward: float,
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
    detailed: bool,
) -> None:
    add_cylinder(
        parts,
        f"{prefix}_pole",
        radius=0.075,
        depth=5.4,
        location=(x, y, 2.7),
        vertices=16 if detailed else 8,
        material=materials["metal"],
        collection=collection,
    )
    add_box(
        parts,
        f"{prefix}_arm",
        (1.1, 0.09, 0.09),
        (x + inward * 0.5, y, 5.28),
        materials["metal"],
        collection,
    )
    if detailed:
        add_box(
            parts,
            f"{prefix}_luminaire",
            (0.46, 0.24, 0.12),
            (x + inward * 1.0, y, 5.2),
            materials["emissive"],
            collection,
            bevel=0.018,
            bevel_segments=1,
        )


def add_building(
    parts: list[bpy.types.Object],
    *,
    prefix: str,
    side: float,
    y: float,
    height: float,
    width_y: float,
    body_material: bpy.types.Material,
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
    lod: int,
    style: int,
) -> None:
    x = side * 11.2
    depth_x = 8.6
    add_box(
        parts,
        f"{prefix}_body",
        (depth_x, width_y, height),
        (x, y, height / 2.0),
        body_material,
        collection,
        bevel=0.1 if lod == 0 else 0.035 if lod == 1 else 0.0,
        bevel_segments=2 if lod == 0 else 1,
    )
    if lod == 2:
        add_roof_silhouette(
            parts,
            prefix=prefix,
            x=x,
            y=y,
            height=height,
            depth_x=depth_x,
            width_y=width_y,
            style=style,
            lod=lod,
            collection=collection,
            materials=materials,
        )
        return

    facade_x = x - side * (depth_x / 2.0 + 0.025)
    frame_material = (
        materials["stone"] if style in (0, 3) else materials["metal"]
    )
    floor_count = max(2, int((height - 2.6) // 2.7))
    window_columns = max(2, int(width_y // 2.1))
    end_columns = max(2, int(depth_x // 2.2))
    if lod == 1:
        floor_count = min(floor_count, 3)
        window_columns = min(window_columns, 4)
        end_columns = min(end_columns, 3)
    for floor in range(floor_count):
        z = 3.2 + floor * 2.55
        if z + 0.7 >= height:
            break
        for column in range(window_columns):
            local_y = (
                y - width_y / 2.0
                + (column + 0.5) * width_y / window_columns
            )
            add_box(
                parts,
                f"{prefix}_window_{floor}_{column}",
                (0.055, width_y / window_columns * 0.62, 1.25),
                (facade_x, local_y, z),
                materials["glass"],
                collection,
                bevel=0.018 if lod == 0 else 0.0,
            )
            if lod == 0:
                add_facade_window_frame(
                    parts,
                    prefix=f"{prefix}_window_{floor}_{column}",
                    side=side,
                    facade_x=facade_x,
                    y=local_y,
                    z=z,
                    opening_width=width_y / window_columns * 0.62,
                    material=frame_material,
                    collection=collection,
                )
        front_y = y - width_y / 2.0 - 0.025
        for column in range(end_columns):
            local_x = (
                x - depth_x / 2.0
                + (column + 0.5) * depth_x / end_columns
            )
            add_box(
                parts,
                f"{prefix}_front_window_{floor}_{column}",
                (depth_x / end_columns * 0.62, 0.055, 1.25),
                (local_x, front_y, z),
                materials["glass"],
                collection,
                bevel=0.018 if lod == 0 else 0.0,
            )
            if lod == 0:
                add_end_window_frame(
                    parts,
                    prefix=f"{prefix}_front_window_{floor}_{column}",
                    x=local_x,
                    facade_y=front_y,
                    z=z,
                    opening_width=depth_x / end_columns * 0.62,
                    material=frame_material,
                    collection=collection,
                )

    add_roof_silhouette(
        parts,
        prefix=prefix,
        x=x,
        y=y,
        height=height,
        depth_x=depth_x,
        width_y=width_y,
        style=style,
        lod=lod,
        collection=collection,
        materials=materials,
    )
    if lod == 1:
        return

    add_box(
        parts,
        f"{prefix}_ground_glass",
        (0.065, width_y * 0.52, 1.9),
        (facade_x, y, 1.3),
        materials["glass"],
        collection,
        bevel=0.025,
    )
    storefront_width = width_y * 0.52
    for index in range(5):
        mullion_y = y - storefront_width / 2.0 + index * storefront_width / 4.0
        add_box(
            parts,
            f"{prefix}_storefront_mullion_{index}",
            (0.11, 0.065, 2.05),
            (facade_x - side * 0.06, mullion_y, 1.32),
            materials["metal"],
            collection,
        )
    entrance_y = y + (0.85 if style % 2 == 0 else -0.85)
    add_box(
        parts,
        f"{prefix}_entrance_door",
        (0.115, 1.0, 2.15),
        (facade_x - side * 0.075, entrance_y, 1.18),
        materials["glass"],
        collection,
        bevel=0.018,
    )
    for label, door_y, door_z, width, height in (
        ("left", entrance_y - 0.55, 1.18, 0.08, 2.28),
        ("right", entrance_y + 0.55, 1.18, 0.08, 2.28),
        ("header", entrance_y, 2.28, 1.18, 0.1),
    ):
        add_box(
            parts,
            f"{prefix}_entrance_frame_{label}",
            (0.125, width, height),
            (facade_x - side * 0.09, door_y, door_z),
            materials["metal"],
            collection,
        )
    add_box(
        parts,
        f"{prefix}_entrance_handle",
        (0.145, 0.035, 0.5),
        (
            facade_x - side * 0.16,
            entrance_y - 0.29,
            1.18,
        ),
        materials["metal"],
        collection,
        bevel=0.008,
    )
    add_box(
        parts,
        f"{prefix}_front_ground_glass",
        (depth_x * 0.46, 0.065, 1.9),
        (x, y - width_y / 2.0 - 0.025, 1.3),
        materials["glass"],
        collection,
        bevel=0.025,
    )
    front_storefront_width = depth_x * 0.46
    front_storefront_y = y - width_y / 2.0 - 0.085
    for index in range(5):
        mullion_x = (
            x - front_storefront_width / 2.0
            + index * front_storefront_width / 4.0
        )
        add_box(
            parts,
            f"{prefix}_front_storefront_mullion_{index}",
            (0.065, 0.11, 2.05),
            (mullion_x, front_storefront_y, 1.32),
            materials["metal"],
            collection,
        )
    front_door_x = x + (0.7 if style % 2 == 0 else -0.7)
    add_box(
        parts,
        f"{prefix}_front_entrance_door",
        (0.92, 0.12, 2.12),
        (front_door_x, front_storefront_y - 0.015, 1.17),
        materials["glass"],
        collection,
        bevel=0.018,
    )
    for label, door_x, door_z, width, height in (
        ("left", front_door_x - 0.5, 1.17, 0.08, 2.25),
        ("right", front_door_x + 0.5, 1.17, 0.08, 2.25),
        ("header", front_door_x, 2.27, 1.08, 0.1),
    ):
        add_box(
            parts,
            f"{prefix}_front_entrance_frame_{label}",
            (width, 0.13, height),
            (door_x, front_storefront_y - 0.025, door_z),
            materials["metal"],
            collection,
        )
    add_box(
        parts,
        f"{prefix}_awning",
        (1.35, width_y * 0.64, 0.16),
        (x - side * (depth_x / 2.0 + 0.58), y, 2.55),
        materials["metal"],
        collection,
        bevel=0.025,
    )
    for pilaster_y in (y - width_y * 0.42, y + width_y * 0.42):
        add_box(
            parts,
            f"{prefix}_facade_pilaster_{'near' if pilaster_y < y else 'far'}",
            (0.14, 0.24, height - 0.75),
            (
                facade_x - side * 0.055,
                pilaster_y,
                height / 2.0 + 0.1,
            ),
            frame_material,
            collection,
        )
    if style in (1, 2):
        for balcony_index, balcony_z in enumerate((5.75, 8.3)):
            if balcony_z + 0.9 < height:
                add_balcony(
                    parts,
                    prefix=f"{prefix}_balcony_{balcony_index}",
                    side=side,
                    facade_x=facade_x,
                    y=y + (1.3 if balcony_index == 0 else -1.2),
                    z=balcony_z,
                    collection=collection,
                    materials=materials,
                )
    else:
        for band_index, band_z in enumerate((2.72, height - 0.75)):
            add_box(
                parts,
                f"{prefix}_facade_band_{band_index}",
                (0.15, width_y + 0.08, 0.18),
                (facade_x - side * 0.055, y, band_z),
                frame_material,
                collection,
            )


def build_render_lod(
    lod: int,
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
) -> bpy.types.Object:
    prefix = f"lod{lod}"
    parts: list[bpy.types.Object] = []
    add_box(
        parts,
        f"{prefix}_road",
        (9.2, LENGTH_M, 0.12),
        (0.0, 0.0, -0.06),
        materials["asphalt"],
        collection,
    )
    for side in (-1.0, 1.0):
        label = "left" if side < 0.0 else "right"
        add_box(
            parts,
            f"{prefix}_sidewalk_{label}",
            (3.2, LENGTH_M, 0.22),
            (side * 6.2, 0.0, 0.09),
            materials["sidewalk"],
            collection,
            bevel=0.035 if lod == 0 else 0.0,
        )
        add_box(
            parts,
            f"{prefix}_curb_{label}",
            (0.22, LENGTH_M, 0.28),
            (side * 4.7, 0.0, 0.12),
            materials["concrete"],
            collection,
            bevel=0.025 if lod == 0 else 0.0,
        )

    if lod < 2:
        for name, x, width, material in (
            ("edge_left", -3.95, 0.12, materials["lane_white"]),
            ("centre_left", -0.09, 0.1, materials["lane_yellow"]),
            ("centre_right", 0.09, 0.1, materials["lane_yellow"]),
            ("edge_right", 3.95, 0.12, materials["lane_white"]),
        ):
            add_box(
                parts,
                f"{prefix}_lane_{name}",
                (width, LENGTH_M - 0.4, 0.014),
                (x, 0.0, 0.011),
                material,
                collection,
            )

    building_specs = (
        (-1.0, -10.0, 15.5, 16.5, materials["brick"]),
        (-1.0, 10.3, 20.0, 15.5, materials["stone"]),
        (1.0, -10.5, 18.0, 15.5, materials["concrete"]),
        (1.0, 10.2, 14.5, 16.5, materials["brick"]),
    )
    for index, (side, y, height, width_y, material) in enumerate(
        building_specs
    ):
        add_building(
            parts,
            prefix=f"{prefix}_building_{index}",
            side=side,
            y=y,
            height=height,
            width_y=width_y,
            body_material=material,
            collection=collection,
            materials=materials,
            lod=lod,
            style=index,
        )

    if lod < 2:
        tree_stations = (-15.0, -5.0, 5.0, 15.0)
        for side in (-1.0, 1.0):
            label = "left" if side < 0.0 else "right"
            for index, y in enumerate(tree_stations):
                add_tree(
                    parts,
                    prefix=f"{prefix}_tree_{label}_{index}",
                    x=side * 6.3,
                    y=y,
                    collection=collection,
                    materials=materials,
                    detail=lod,
                    variant=(index + (0 if side < 0.0 else 2)) % 3,
                )
                add_streetlight(
                    parts,
                    prefix=f"{prefix}_lamp_{label}_{index}",
                    x=side * 4.95,
                    y=y + 2.4,
                    inward=-side,
                    collection=collection,
                    materials=materials,
                    detailed=lod == 0,
                )
    else:
        for side in (-1.0, 1.0):
            for index, y in enumerate((-12.0, 0.0, 12.0)):
                add_box(
                    parts,
                    f"{prefix}_tree_proxy_{side}_{index}",
                    (2.4, 2.4, 5.0),
                    (side * 6.3, y, 2.5),
                    materials["leaf_dark"],
                    collection,
                )

    return join_render_components_deterministically(
        parts,
        name=f"{ASSET_ID}_lod{lod}",
        lod=lod,
    )


def build_collision(
    collection: bpy.types.Collection,
    material: bpy.types.Material,
) -> list[bpy.types.Object]:
    road = BASE.make_box(
        "collision_road_component",
        dimensions=(ROAD_WIDTH_M, LENGTH_M, 0.12),
        location=(0.0, 0.0, -0.06),
        material=material,
        collection=collection,
    )
    road = BASE.join_components(
        [road],
        name=f"{ASSET_ID}_collision_road",
        role="collision-road",
        lod=None,
    )
    barriers: list[bpy.types.Object] = []
    for side in (-1.0, 1.0):
        label = "left" if side < 0.0 else "right"
        barrier = BASE.make_box(
            f"collision_building_proxy_{label}",
            dimensions=(12.35, LENGTH_M, 22.0),
            location=(side * 10.725, 0.0, 11.0),
            material=material,
            collection=collection,
        )
        barriers.append(
            BASE.join_components(
                [barrier],
                name=f"{ASSET_ID}_collision_barrier_{label}",
                role="collision-barrier",
                lod=None,
            )
        )
    return [road, *barriers]


def add_preview_scene(
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
    preview_path: Path,
) -> None:
    ground = BASE.make_box(
        "preview_ground",
        dimensions=(58.0, 58.0, 0.25),
        location=(0.0, 0.0, -0.22),
        material=materials["preview_ground"],
        collection=collection,
        bevel=0.08,
        bevel_segments=2,
    )
    ground["rorng_role"] = "preview-only"

    bpy.ops.object.camera_add(location=(0.0, -62.0, 13.5))
    camera = bpy.context.object
    camera.name = "preview_camera"
    camera.data.lens = 46.0
    BASE.point_camera(camera, (0.0, 0.0, 6.8))
    BASE.move_to_collection(camera, collection)
    bpy.context.scene.camera = camera

    bpy.ops.object.light_add(type="SUN", location=(0.0, 0.0, 28.0))
    sun = bpy.context.object
    sun.name = "preview_sun"
    sun.rotation_euler = (
        math.radians(28.0),
        math.radians(-20.0),
        math.radians(-38.0),
    )
    sun.data.energy = 3.5
    sun.data.angle = math.radians(2.5)
    BASE.move_to_collection(sun, collection)

    bpy.ops.object.light_add(type="AREA", location=(-16.0, -10.0, 19.0))
    fill = bpy.context.object
    fill.name = "preview_fill"
    fill.data.energy = 2_300.0
    fill.data.shape = "DISK"
    fill.data.size = 10.0
    BASE.point_camera(fill, (0.0, 0.0, 6.0))
    BASE.move_to_collection(fill, collection)

    bpy.ops.object.light_add(type="AREA", location=(13.0, -26.0, 14.0))
    canyon_fill = bpy.context.object
    canyon_fill.name = "preview_canyon_fill"
    canyon_fill.data.energy = 2_600.0
    canyon_fill.data.shape = "DISK"
    canyon_fill.data.size = 11.0
    BASE.point_camera(canyon_fill, (0.0, -1.0, 5.3))
    BASE.move_to_collection(canyon_fill, collection)

    world = bpy.context.scene.world
    world.use_nodes = True
    background = world.node_tree.nodes.get("Background")
    background.inputs["Color"].default_value = (0.055, 0.08, 0.13, 1.0)
    background.inputs["Strength"].default_value = 0.62
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x = 1280
    scene.render.resolution_y = 720
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.image_settings.color_depth = "8"
    scene.render.filepath = str(preview_path)
    scene.view_settings.exposure = 0.45
    scene.view_settings.look = "AgX - Medium High Contrast"


def main() -> int:
    args = parse_args()
    root = args.output_root.resolve()
    if not (root / ".git").exists():
        raise RuntimeError(f"--output-root is not a repository root: {root}")
    generator_path = Path(__file__).resolve()
    asset_root = (
        root / "resources/nextgen/cityworld/streetscape/gateway_block_40m"
    )
    source_root = (
        root / "content-source/cityworld_next/streetscape/gateway_block_40m"
    )
    blend_path = source_root / f"{ASSET_ID}.blend"
    glb_path = asset_root / f"{ASSET_ID}.glb"
    manifest_path = asset_root / f"{ASSET_ID}.asset.json"
    preview_path = (
        args.preview_path.resolve()
        if args.preview_path
        else source_root / f"{ASSET_ID}_preview.png"
    )
    for path in (blend_path, glb_path, manifest_path, preview_path):
        path.parent.mkdir(parents=True, exist_ok=True)

    BASE.ASSET_ID = ASSET_ID
    BASE.ASSET_VERSION = ASSET_VERSION
    BASE.GENERATOR_ID = GENERATOR_ID
    BASE.BRIDGE_LENGTH_M = LENGTH_M
    BASE.BRIDGE_WIDTH_M = WIDTH_M
    BASE.ROAD_WIDTH_M = ROAD_WIDTH_M
    reset_scene_fully()
    scene = bpy.context.scene
    scene[GENERATOR_ID] = ASSET_VERSION
    render_collection = BASE.make_collection("rorng_gateway_block_render")
    collision_collection = BASE.make_collection(
        "rorng_gateway_block_collision"
    )
    preview_collection = BASE.make_collection("rorng_gateway_block_preview")
    materials = make_materials()
    lod_objects = [
        build_render_lod(lod, render_collection, materials)
        for lod in range(3)
    ]
    collision_objects = build_collision(
        collision_collection,
        materials["collision"],
    )
    asset_objects = [*lod_objects, *collision_objects]
    bpy.ops.object.select_all(action="DESELECT")
    for obj in asset_objects:
        obj.hide_set(False)
        obj.select_set(True)
    bpy.context.view_layer.objects.active = lod_objects[0]
    bpy.ops.export_scene.gltf(
        filepath=str(glb_path),
        check_existing=False,
        export_animations=False,
        export_apply=True,
        export_cameras=False,
        export_extras=True,
        export_format="GLB",
        export_lights=False,
        export_materials="EXPORT",
        export_normals=True,
        export_tangents=True,
        export_texcoords=True,
        export_yup=True,
        use_renderable=False,
        use_selection=True,
        use_visible=False,
    )
    canonicalize_glb_geometry(glb_path)
    for obj in [lod_objects[1], lod_objects[2], *collision_objects]:
        obj.hide_render = True
        obj.hide_set(True)
    lod_objects[0].hide_render = False
    lod_objects[0].hide_set(False)
    add_preview_scene(preview_collection, materials, preview_path)
    bpy.ops.wm.save_as_mainfile(filepath=str(blend_path), compress=False)
    bpy.ops.render.render(write_still=True)

    manifest = BASE.make_manifest(
        root=root,
        generator_path=generator_path,
        blend_path=blend_path,
        glb_path=glb_path,
        preview_path=preview_path,
        lod_objects=lod_objects,
        collision_objects=collision_objects,
        materials=materials,
    )
    manifest["authoring"]["generator"]["format"] = GENERATOR_ID
    manifest["authoring"]["generator"]["dependencies"] = [
        {
            "path": BASE_GENERATOR_PATH.relative_to(root).as_posix(),
            "sha256": BASE.sha256_file(BASE_GENERATOR_PATH),
        },
    ]
    manifest["authoring"]["procedural_provenance"] = {
        "external_geometry": False,
        "external_textures": False,
        "method": "deterministic-project-authored-blender-python",
        "rights_basis": "GPL-3.0-or-later project-authored source",
    }
    manifest["connectors"] = [
        {
            "forward": [0.0, -1.0, 0.0],
            "id": "start",
            "lane_centres_x_m": [-1.75, 1.75],
            "position_blender_z_up_m": [0.0, -20.0, 0.0],
            "road_width_m": ROAD_WIDTH_M,
        },
        {
            "forward": [0.0, 1.0, 0.0],
            "id": "end",
            "lane_centres_x_m": [-1.75, 1.75],
            "position_blender_z_up_m": [0.0, 20.0, 0.0],
            "road_width_m": ROAD_WIDTH_M,
        },
    ]
    manifest["geometry"]["asset_family"] = "city-gateway-streetscape"
    manifest["geometry"]["detail_profile"] = {
        "building_facades": (
            "recessed-glazing-frames-doors-balconies-stepped-roofs"
        ),
        "collision_revision": 1,
        "lod_policy": "authored-three-level-silhouette-preserving",
        "tree_canopies": "branched-varied-six-lobe-close-three-lobe-medium",
    }
    manifest["runtime_lights"] = {
        "lights": [
            {
                "color_linear": [1.0, 0.62, 0.22],
                "id": (
                    f"rorng_gateway_lamp_"
                    f"{'left' if side < 0.0 else 'right'}_{index}"
                ),
                "position_blender_z_up_m": [
                    side * 3.95,
                    station + 2.4,
                    5.2,
                ],
                "range_m": 13.0,
                "type": "point",
            }
            for side in (-1.0, 1.0)
            for index, station in enumerate((-15.0, -5.0, 5.0, 15.0))
        ],
        "profile": "ror-cityworld-local-lights-v1",
    }
    for material in manifest["materials"]:
        if material["name"] == "rorng_gateway_lamp_emissive":
            material["emissive_factor_linear"] = [1.0, 0.69, 0.25]
    manifest_path.write_text(
        json.dumps(manifest, indent=2, ensure_ascii=True, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(
        json.dumps(
            {
                "asset_id": ASSET_ID,
                "blend": str(blend_path),
                "glb": str(glb_path),
                "lod_triangles": [
                    BASE.triangle_count(obj) for obj in lod_objects
                ],
                "manifest": str(manifest_path),
                "preview": str(preview_path),
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
