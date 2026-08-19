#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the project-authored CityWorld regional infill asset family.

Run with the pinned Blender 5.2 LTS authoring version:

    blender --background --factory-startup --python \
      tools/blender/cityworld_next/generate_cityworld_infill_family.py -- \
      --output-root /path/to/rigs-of-rods

The generator emits texture-free, factor-lit static assets with three manual
LODs and one conservative watertight collision proxy per asset. No geometry,
materials, textures, branding, or signage are copied from CityWorld.zip.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import math
import os
from pathlib import Path
import struct
import sys
from typing import Any

import bpy


SCRIPT_DIRECTORY = Path(__file__).resolve().parent
BASE_GENERATOR_PATH = SCRIPT_DIRECTORY / "generate_bridge_kit.py"
CANONICALIZER_PATH = SCRIPT_DIRECTORY / "canonicalize_static_glb.py"
BASE_SPEC = importlib.util.spec_from_file_location(
    "rorng_infill_helpers",
    BASE_GENERATOR_PATH,
)
if BASE_SPEC is None or BASE_SPEC.loader is None:
    raise RuntimeError("cannot load the CityWorld authoring helpers")
BASE = importlib.util.module_from_spec(BASE_SPEC)
BASE_SPEC.loader.exec_module(BASE)
CANONICALIZER_SPEC = importlib.util.spec_from_file_location(
    "rorng_infill_glb_canonicalizer",
    CANONICALIZER_PATH,
)
if CANONICALIZER_SPEC is None or CANONICALIZER_SPEC.loader is None:
    raise RuntimeError("cannot load the static GLB canonicalizer")
CANONICALIZER = importlib.util.module_from_spec(CANONICALIZER_SPEC)
CANONICALIZER_SPEC.loader.exec_module(CANONICALIZER)


ASSET_VERSION = 1
FAMILY_ID = "rorng_city_regional_infill_family"
GENERATOR_ID = "ror-cityworld-regional-infill-generator-v1"
COLLISION_COMPONENTS_FORMAT = "ror-cityworld-collision-components-v1"
AUTHORING_DEPENDENCY_PATHS = (
    BASE_GENERATOR_PATH,
    CANONICALIZER_PATH,
)
PINNED_BLENDER_VERSION = "5.2.0 LTS"

VARIANTS: tuple[dict[str, Any], ...] = (
    {
        "asset_id": "rorng_city_infill_farmstead_98x86",
        "profile": "static-building-v1",
        "category": "farmland",
        "label": "Golden Horizon Farmstead",
        "footprint_m": (98.0, 86.0),
        "height_m": 18.0,
        "runtime_parent_materials": {
            "asphalt": "RoR/CityWorldNext/ParcelAsphalt",
        },
    },
    {
        "asset_id": "rorng_city_infill_suburb_block_96x88",
        "profile": "static-building-v1",
        "category": "suburb",
        "label": "Sunridge Courtyard Homes",
        "footprint_m": (96.0, 88.0),
        "height_m": 15.0,
        "runtime_parent_materials": {
            "asphalt": "RoR/CityWorldNext/ParcelAsphalt",
        },
    },
    {
        "asset_id": "rorng_city_infill_service_station_90x65",
        "profile": "static-building-v1",
        "category": "service-station",
        "label": "Horizon Energy and Market",
        "footprint_m": (90.0, 65.0),
        "height_m": 15.0,
    },
    {
        "asset_id": "rorng_city_infill_red_mesa_19m",
        "profile": "static-fixture-v1",
        "category": "natural-landmark",
        "label": "Red Mesa Natural Monument",
        "footprint_m": (19.0, 19.0),
        "height_m": 16.0,
    },
    {
        "asset_id": "rorng_city_infill_arroyo_oasis_19m",
        "profile": "static-fixture-v1",
        "category": "natural-landmark",
        "label": "Coyote Arroyo Oasis",
        "footprint_m": (19.0, 19.0),
        "height_m": 13.0,
    },
)


def parse_args() -> argparse.Namespace:
    argv = sys.argv
    script_args = argv[argv.index("--") + 1 :] if "--" in argv else []
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output-root",
        type=Path,
        default=Path(__file__).resolve().parents[3],
    )
    return parser.parse_args(script_args)


def canonicalize_textureless_texcoords(path: Path) -> None:
    """Zero UV accessors for the explicitly texture-free runtime profile."""

    contents = bytearray(path.read_bytes())
    if len(contents) < 28:
        raise RuntimeError("exported GLB is truncated")
    _magic, _version, declared_length = struct.unpack_from("<4sII", contents, 0)
    if declared_length != len(contents):
        raise RuntimeError("exported GLB length is invalid")
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
    accessor_indices = {
        primitive["attributes"]["TEXCOORD_0"]
        for mesh in document["meshes"]
        for primitive in mesh["primitives"]
    }
    for accessor_index in sorted(accessor_indices):
        accessor = document["accessors"][accessor_index]
        view = document["bufferViews"][accessor["bufferView"]]
        if (
            accessor.get("componentType") != 5126
            or accessor.get("type") != "VEC2"
            or accessor.get("normalized", False) is not False
        ):
            raise RuntimeError("textureless UV accessor is not float VEC2")
        stride = view.get("byteStride", 8)
        if stride != 8:
            raise RuntimeError("textureless UV accessor is not tightly packed")
        offset = (
            binary_start
            + view.get("byteOffset", 0)
            + accessor.get("byteOffset", 0)
        )
        count = accessor.get("count")
        if not isinstance(count, int) or count < 0:
            raise RuntimeError("textureless UV accessor count is invalid")
        if offset + count * stride > len(contents):
            raise RuntimeError("textureless UV accessor escapes GLB bounds")
        for index in range(count):
            struct.pack_into("<ff", contents, offset + index * stride, 0.0, 0.0)
    path.write_bytes(contents)


def orthogonalize_glb_tangents(path: Path) -> None:
    """Repair Blender tangents after non-uniform procedural mesh scaling."""

    contents = bytearray(path.read_bytes())
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

    def layout(accessor_index: int, width: int) -> tuple[int, int, int]:
        accessor = document["accessors"][accessor_index]
        view = document["bufferViews"][accessor["bufferView"]]
        if (
            accessor.get("componentType") != 5126
            or accessor.get("type") != ("VEC3" if width == 3 else "VEC4")
        ):
            raise RuntimeError("normal/tangent accessor is not float vector")
        stride = view.get("byteStride", width * 4)
        if stride < width * 4:
            raise RuntimeError("normal/tangent accessor stride is invalid")
        offset = (
            binary_start
            + view.get("byteOffset", 0)
            + accessor.get("byteOffset", 0)
        )
        count = accessor.get("count")
        if not isinstance(count, int) or count < 0:
            raise RuntimeError("normal/tangent accessor count is invalid")
        return offset, stride, count

    processed: set[tuple[int, int]] = set()
    for mesh in document["meshes"]:
        for primitive in mesh["primitives"]:
            attributes = primitive["attributes"]
            normal_index = attributes["NORMAL"]
            tangent_index = attributes["TANGENT"]
            key = (normal_index, tangent_index)
            if key in processed:
                continue
            processed.add(key)
            normal_offset, normal_stride, normal_count = layout(normal_index, 3)
            tangent_offset, tangent_stride, tangent_count = layout(tangent_index, 4)
            if normal_count != tangent_count:
                raise RuntimeError("normal/tangent accessor counts differ")
            for index in range(normal_count):
                normal = struct.unpack_from(
                    "<fff",
                    contents,
                    normal_offset + index * normal_stride,
                )
                tangent = struct.unpack_from(
                    "<ffff",
                    contents,
                    tangent_offset + index * tangent_stride,
                )
                normal_length = math.sqrt(sum(value * value for value in normal))
                tangent_length = math.sqrt(
                    sum(value * value for value in tangent[:3])
                )
                if normal_length <= 1e-12:
                    raise RuntimeError("normal vector has zero length")
                n = tuple(value / normal_length for value in normal)
                if tangent_length <= 1e-12:
                    t = (
                        (1.0, 0.0, 0.0)
                        if abs(n[0]) < 0.8
                        else (0.0, 1.0, 0.0)
                    )
                else:
                    t = tuple(value / tangent_length for value in tangent[:3])
                projection = sum(t[axis] * n[axis] for axis in range(3))
                orthogonal = tuple(
                    t[axis] - projection * n[axis]
                    for axis in range(3)
                )
                orthogonal_length = math.sqrt(
                    sum(value * value for value in orthogonal)
                )
                if orthogonal_length <= 1e-9:
                    reference = (
                        (1.0, 0.0, 0.0)
                        if abs(n[0]) < 0.8
                        else (0.0, 1.0, 0.0)
                    )
                    orthogonal = (
                        n[1] * reference[2] - n[2] * reference[1],
                        n[2] * reference[0] - n[0] * reference[2],
                        n[0] * reference[1] - n[1] * reference[0],
                    )
                    orthogonal_length = math.sqrt(
                        sum(value * value for value in orthogonal)
                    )
                resolved = tuple(
                    value / orthogonal_length for value in orthogonal
                )
                struct.pack_into(
                    "<ffff",
                    contents,
                    tangent_offset + index * tangent_stride,
                    *resolved,
                    -1.0 if tangent[3] < 0.0 else 1.0,
                )
    path.write_bytes(contents)


def reset_scene_fully(asset_id: str, profile: str) -> None:
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
    BASE.ASSET_ID = asset_id
    BASE.ASSET_VERSION = ASSET_VERSION
    BASE.GENERATOR_ID = GENERATOR_ID
    BASE.reset_scene()
    bpy.context.scene["rorng_asset_profile"] = profile


def material(
    asset_id: str,
    suffix: str,
    color: tuple[float, float, float, float],
    *,
    metallic: float = 0.0,
    roughness: float = 0.75,
) -> bpy.types.Material:
    return BASE.make_material(
        f"{asset_id}_{suffix}",
        color,
        metallic=metallic,
        roughness=roughness,
    )


def make_materials(asset_id: str) -> dict[str, bpy.types.Material]:
    return {
        "asphalt": material(
            asset_id, "asphalt", (0.035, 0.042, 0.048, 1.0), roughness=0.91
        ),
        "clay": material(
            asset_id, "clay_tile", (0.42, 0.095, 0.035, 1.0), roughness=0.82
        ),
        "collision": material(
            asset_id, "collision_debug", (0.82, 0.025, 0.02, 1.0), roughness=1.0
        ),
        "concrete": material(
            asset_id, "concrete", (0.36, 0.37, 0.34, 1.0), roughness=0.88
        ),
        "crop_gold": material(
            asset_id, "crop_gold", (0.52, 0.34, 0.055, 1.0), roughness=0.94
        ),
        "crop_green": material(
            asset_id, "crop_green", (0.085, 0.245, 0.055, 1.0), roughness=0.96
        ),
        "glass": material(
            asset_id,
            "glass",
            (0.035, 0.12, 0.17, 1.0),
            metallic=0.18,
            roughness=0.16,
        ),
        "leaf": material(
            asset_id, "leaf", (0.045, 0.22, 0.07, 1.0), roughness=0.92
        ),
        "light": material(
            asset_id,
            "light_lens",
            (0.96, 0.72, 0.31, 1.0),
            metallic=0.0,
            roughness=0.24,
        ),
        "metal": material(
            asset_id,
            "powdercoat_metal",
            (0.055, 0.065, 0.075, 1.0),
            metallic=0.7,
            roughness=0.31,
        ),
        "paint_blue": material(
            asset_id, "paint_blue", (0.025, 0.19, 0.32, 1.0), roughness=0.5
        ),
        "paint_cream": material(
            asset_id, "paint_cream", (0.68, 0.54, 0.35, 1.0), roughness=0.76
        ),
        "paint_white": material(
            asset_id, "paint_white", (0.72, 0.71, 0.64, 1.0), roughness=0.72
        ),
        "rock_dark": material(
            asset_id, "rock_dark", (0.21, 0.075, 0.035, 1.0), roughness=0.98
        ),
        "rock_light": material(
            asset_id, "rock_light", (0.48, 0.18, 0.065, 1.0), roughness=0.96
        ),
        "sand": material(
            asset_id, "sand", (0.36, 0.24, 0.105, 1.0), roughness=0.98
        ),
        "sign": material(
            asset_id,
            "fictional_signage",
            (0.02, 0.31, 0.34, 1.0),
            metallic=0.08,
            roughness=0.42,
        ),
        "soil": material(
            asset_id, "cultivated_soil", (0.18, 0.075, 0.025, 1.0), roughness=1.0
        ),
        "stucco": material(
            asset_id, "stucco", (0.58, 0.41, 0.235, 1.0), roughness=0.87
        ),
        "trunk": material(
            asset_id, "trunk", (0.18, 0.075, 0.025, 1.0), roughness=0.98
        ),
        "water": material(
            asset_id,
            "water",
            (0.025, 0.19, 0.255, 1.0),
            metallic=0.12,
            roughness=0.18,
        ),
        "preview_ground": BASE.make_material(
            "rorng_preview_infill_ground",
            (0.065, 0.07, 0.065, 1.0),
            metallic=0.0,
            roughness=0.96,
        ),
    }


def add_box(
    parts: list[bpy.types.Object],
    name: str,
    dimensions: tuple[float, float, float],
    location: tuple[float, float, float],
    mat: bpy.types.Material,
    collection: bpy.types.Collection,
    *,
    rotation: tuple[float, float, float] = (0.0, 0.0, 0.0),
    bevel: float = 0.0,
    segments: int = 1,
) -> bpy.types.Object:
    initial_location = (
        (0.0, 0.0, 0.0)
        if rotation != (0.0, 0.0, 0.0)
        else location
    )
    obj = BASE.make_box(
        name,
        dimensions=dimensions,
        location=initial_location,
        material=mat,
        collection=collection,
        bevel=0.0,
    )
    if rotation != (0.0, 0.0, 0.0):
        obj.rotation_euler = rotation
        BASE.apply_transform(obj)
        obj.location = location
        BASE.apply_transform(obj)
    if bevel > 0.0:
        BASE.add_bevel(obj, bevel, segments)
    parts.append(obj)
    return obj


def add_cylinder(
    parts: list[bpy.types.Object],
    name: str,
    *,
    radius: float,
    depth: float,
    location: tuple[float, float, float],
    mat: bpy.types.Material,
    collection: bpy.types.Collection,
    vertices: int,
    rotation: tuple[float, float, float] = (0.0, 0.0, 0.0),
    bevel: float = 0.0,
) -> bpy.types.Object:
    obj = BASE.make_cylinder(
        name,
        radius=radius,
        depth=depth,
        location=location,
        rotation=rotation,
        vertices=vertices,
        material=mat,
        collection=collection,
        bevel=bevel,
    )
    parts.append(obj)
    return obj


def add_ico(
    parts: list[bpy.types.Object],
    name: str,
    *,
    location: tuple[float, float, float],
    scale: tuple[float, float, float],
    mat: bpy.types.Material,
    collection: bpy.types.Collection,
    subdivisions: int,
    rotation_z: float = 0.0,
) -> bpy.types.Object:
    segments = {1: 12, 2: 20, 3: 32}[subdivisions]
    rings = {1: 6, 2: 10, 3: 16}[subdivisions]
    bpy.ops.mesh.primitive_uv_sphere_add(
        segments=segments,
        ring_count=rings,
        radius=1.0,
        location=location,
        rotation=(0.0, 0.0, rotation_z),
    )
    obj = bpy.context.object
    obj.name = name
    obj.scale = scale
    BASE.apply_transform(obj)
    for polygon in obj.data.polygons:
        polygon.use_smooth = True
    BASE.assign_material(obj, mat)
    BASE.move_to_collection(obj, collection)
    parts.append(obj)
    return obj


def add_gabled_roof(
    parts: list[bpy.types.Object],
    *,
    prefix: str,
    width: float,
    depth: float,
    eave_z: float,
    ridge_z: float,
    centre: tuple[float, float],
    mat: bpy.types.Material,
    collection: bpy.types.Collection,
    detail: bool,
    trim_mat: bpy.types.Material | None = None,
    accent_mat: bpy.types.Material | None = None,
    tile_courses: int = 0,
    tile_segments: int = 0,
    ridge_cap: bool = False,
    wall_width: float = 0.0,
) -> None:
    half_width = width * 0.5
    rise = ridge_z - eave_z
    panel_length = math.hypot(half_width, rise)
    slope = math.atan2(rise, half_width)
    for side in (-1.0, 1.0):
        add_box(
            parts,
            f"{prefix}_{'left' if side < 0 else 'right'}",
            (panel_length, depth, 0.26 if detail else 0.2),
            (
                centre[0] + side * width * 0.25,
                centre[1],
                eave_z + rise * 0.5,
            ),
            mat,
            collection,
            rotation=(0.0, side * slope, 0.0),
            bevel=0.045 if detail else 0.0,
            segments=2,
        )
    if trim_mat is not None:
        for side in (-1.0, 1.0):
            label = "left" if side < 0 else "right"
            add_box(
                parts,
                f"{prefix}_fascia_{label}",
                (0.15, depth - 0.12, 0.36),
                (
                    centre[0] + side * (half_width - 0.09),
                    centre[1],
                    eave_z - 0.05,
                ),
                trim_mat,
                collection,
                bevel=0.03 if detail else 0.0,
                segments=1,
            )
            if detail and wall_width > 0.0 and half_width > wall_width * 0.5:
                soffit_span = half_width - wall_width * 0.5
                add_box(
                    parts,
                    f"{prefix}_soffit_{label}",
                    (soffit_span, depth - 0.12, 0.06),
                    (
                        centre[0] + side * (wall_width * 0.5 + soffit_span * 0.5),
                        centre[1],
                        eave_z - 0.19,
                    ),
                    trim_mat,
                    collection,
                )
        if detail:
            for side in (-1.0, 1.0):
                for end in (-1.0, 1.0):
                    add_box(
                        parts,
                        f"{prefix}_rake_{'l' if side < 0 else 'r'}"
                        f"_{'s' if end < 0 else 'n'}",
                        (panel_length * 0.97, 0.12, 0.3),
                        (
                            centre[0] + side * width * 0.25,
                            centre[1] + end * (depth * 0.5 + 0.03),
                            eave_z + rise * 0.5 + 0.06,
                        ),
                        trim_mat,
                        collection,
                        rotation=(0.0, side * slope, 0.0),
                    )
    if ridge_cap:
        add_box(
            parts,
            f"{prefix}_ridge_cap",
            (0.55, depth + 0.06, 0.16),
            (centre[0], centre[1], ridge_z + 0.05),
            mat,
            collection,
            bevel=0.035 if detail else 0.0,
            segments=1,
        )
        if detail and accent_mat is not None:
            crest_count = max(4, int(depth // 2.2))
            crest_span = depth - 0.4
            segment_length = crest_span / crest_count
            for index in range(crest_count):
                add_box(
                    parts,
                    f"{prefix}_crest_{index}",
                    (0.5, segment_length * 0.62, 0.1),
                    (
                        centre[0] + (0.024 if index % 2 else -0.024),
                        centre[1]
                        - crest_span * 0.5
                        + (index + 0.5) * segment_length,
                        ridge_z + 0.16,
                    ),
                    accent_mat,
                    collection,
                )
    if detail and tile_courses > 0 and tile_segments > 0 and accent_mat is not None:
        tile_span = depth - 0.5
        segment_depth = tile_span / tile_segments
        for side in (-1.0, 1.0):
            normal_x = math.sin(slope) * side
            normal_z = math.cos(slope)
            for row in range(tile_courses):
                t = (row + 0.55) / tile_courses
                surface_x = centre[0] + side * half_width * t
                surface_z = eave_z + rise * (1.0 - t)
                for segment in range(tile_segments):
                    lift = 0.155 + ((row + segment) % 2) * 0.014
                    add_box(
                        parts,
                        f"{prefix}_tile_{'l' if side < 0 else 'r'}"
                        f"_{row}_{segment}",
                        (0.11, segment_depth - 0.07, 0.055),
                        (
                            surface_x + normal_x * lift,
                            centre[1]
                            - tile_span * 0.5
                            + (segment + 0.5) * segment_depth,
                            surface_z + normal_z * lift,
                        ),
                        accent_mat,
                        collection,
                        rotation=(0.0, side * slope, 0.0),
                    )


def add_gable_prism(
    parts: list[bpy.types.Object],
    name: str,
    *,
    width: float,
    depth: float,
    base_z: float,
    apex_z: float,
    centre: tuple[float, float],
    mat: bpy.types.Material,
    collection: bpy.types.Collection,
) -> bpy.types.Object:
    """Close the open gable wedge with a watertight triangular attic prism."""

    half_width = width * 0.5
    half_depth = depth * 0.5
    rise = apex_z - base_z
    vertices = [
        (-half_width, -half_depth, 0.0),
        (half_width, -half_depth, 0.0),
        (0.0, -half_depth, rise),
        (-half_width, half_depth, 0.0),
        (half_width, half_depth, 0.0),
        (0.0, half_depth, rise),
    ]
    faces = [
        (0, 1, 2),
        (3, 5, 4),
        (0, 3, 4, 1),
        (1, 4, 5, 2),
        (0, 2, 5, 3),
    ]
    mesh = bpy.data.meshes.new(f"{name}_mesh")
    mesh.from_pydata(vertices, [], faces)
    mesh.validate()
    mesh.update()
    uv_layer = mesh.uv_layers.new(name="UVMap")
    for polygon in mesh.polygons:
        normal = polygon.normal
        for loop_index in polygon.loop_indices:
            position = mesh.vertices[mesh.loops[loop_index].vertex_index].co
            if abs(normal.z) >= max(abs(normal.x), abs(normal.y)):
                uv = (position.x, position.y)
            elif abs(normal.y) >= abs(normal.x):
                uv = (position.x, position.z)
            else:
                uv = (position.y, position.z)
            uv_layer.data[loop_index].uv = (uv[0] * 0.25, uv[1] * 0.25)
    obj = bpy.data.objects.new(name, mesh)
    collection.objects.link(obj)
    obj.location = (centre[0], centre[1], base_z)
    BASE.apply_transform(obj)
    BASE.assign_material(obj, mat)
    parts.append(obj)
    return obj


def add_plinth(
    parts: list[bpy.types.Object],
    name: str,
    *,
    width: float,
    depth: float,
    centre: tuple[float, float],
    mat: bpy.types.Material,
    collection: bpy.types.Collection,
    detail: bool,
) -> None:
    add_box(
        parts,
        name,
        (width + 0.7, depth + 0.7, 0.42),
        (centre[0], centre[1], 0.13),
        mat,
        collection,
        bevel=0.05 if detail else 0.0,
        segments=2,
    )


def add_inset_window(
    parts: list[bpy.types.Object],
    *,
    prefix: str,
    x: float,
    wall_y: float,
    facing: float,
    centre_z: float,
    width: float,
    height: float,
    frame_mat: bpy.types.Material,
    glass_mat: bpy.types.Material,
    collection: bpy.types.Collection,
    shutter_mat: bpy.types.Material | None = None,
    axis: str = "y",
) -> None:
    """Recess a glazed opening behind protruding casing for real relief.

    ``axis`` names the wall normal: ``"y"`` places the opening on a wall
    whose outward face is along Y at ``wall_y`` (spanning X around ``x``);
    ``"x"`` mirrors the roles for gable-end walls.
    """

    def emit(
        suffix: str,
        along: float,
        out: float,
        z: float,
        span: float,
        thickness: float,
        rise: float,
        mat: bpy.types.Material,
        *,
        bevel: float = 0.0,
    ) -> None:
        if axis == "y":
            dimensions = (span, thickness, rise)
            location = (x + along, wall_y + facing * out, z)
        else:
            dimensions = (thickness, span, rise)
            location = (wall_y + facing * out, x + along, z)
        add_box(
            parts,
            f"{prefix}_{suffix}",
            dimensions,
            location,
            mat,
            collection,
            bevel=bevel,
            segments=1,
        )

    half_width = width * 0.5
    half_height = height * 0.5
    for side in (-1.0, 1.0):
        emit(
            f"jamb_{'l' if side < 0 else 'r'}",
            side * (half_width + 0.05),
            0.045,
            centre_z,
            0.1,
            0.34,
            height + 0.32,
            frame_mat,
        )
    emit(
        "lintel",
        0.0,
        0.045,
        centre_z + half_height + 0.11,
        width + 0.44,
        0.34,
        0.11,
        frame_mat,
    )
    emit(
        "sill",
        0.0,
        0.07,
        centre_z - half_height - 0.085,
        width + 0.5,
        0.46,
        0.09,
        frame_mat,
        bevel=0.02,
    )
    emit("pane", 0.0, 0.02, centre_z, width, 0.07, height, glass_mat)
    emit("mullion_v", 0.0, 0.06, centre_z, 0.055, 0.05, height, frame_mat)
    emit("mullion_h", 0.0, 0.06, centre_z, width, 0.05, 0.055, frame_mat)
    if shutter_mat is not None:
        for side in (-1.0, 1.0):
            emit(
                f"shutter_{'l' if side < 0 else 'r'}",
                side * (half_width + 0.42),
                0.045,
                centre_z,
                0.5,
                0.08,
                height + 0.2,
                shutter_mat,
            )


def add_inset_door(
    parts: list[bpy.types.Object],
    *,
    prefix: str,
    x: float,
    wall_y: float,
    facing: float,
    width: float,
    height: float,
    door_mat: bpy.types.Material,
    frame_mat: bpy.types.Material,
    step_mat: bpy.types.Material,
    canopy_mat: bpy.types.Material,
    collection: bpy.types.Collection,
) -> None:
    """Recess an entry door behind casing with a stoop and shed canopy."""

    half_width = width * 0.5
    centre_z = 0.12 + height * 0.5
    top_z = 0.12 + height
    for side in (-1.0, 1.0):
        add_box(
            parts,
            f"{prefix}_jamb_{'l' if side < 0 else 'r'}",
            (0.13, 0.42, height + 0.2),
            (x + side * (half_width + 0.065), wall_y + facing * 0.06, centre_z),
            frame_mat,
            collection,
        )
    add_box(
        parts,
        f"{prefix}_lintel",
        (width + 0.56, 0.42, 0.13),
        (x, wall_y + facing * 0.06, top_z + 0.09),
        frame_mat,
        collection,
    )
    add_box(
        parts,
        f"{prefix}_slab",
        (width, 0.09, height),
        (x, wall_y + facing * 0.025, centre_z),
        door_mat,
        collection,
    )
    add_box(
        parts,
        f"{prefix}_threshold",
        (width + 0.5, 0.55, 0.13),
        (x, wall_y + facing * 0.24, 0.065),
        step_mat,
        collection,
    )
    add_box(
        parts,
        f"{prefix}_stoop",
        (width + 1.2, 1.4, 0.16),
        (x, wall_y + facing * 0.95, 0.08),
        step_mat,
        collection,
        bevel=0.03,
        segments=1,
    )
    add_box(
        parts,
        f"{prefix}_canopy",
        (width + 1.3, 1.25, 0.09),
        (x, wall_y + facing * 0.72, top_z + 0.46),
        canopy_mat,
        collection,
        rotation=(facing * math.radians(12.0), 0.0, 0.0),
        bevel=0.02,
        segments=1,
    )
    post_height = top_z + 0.3
    for side in (-1.0, 1.0):
        add_cylinder(
            parts,
            f"{prefix}_post_{'l' if side < 0 else 'r'}",
            radius=0.07,
            depth=post_height,
            location=(
                x + side * (half_width + 0.45),
                wall_y + facing * 1.2,
                post_height * 0.5 + 0.08,
            ),
            mat=frame_mat,
            collection=collection,
            vertices=8,
        )


def add_garage_trim(
    parts: list[bpy.types.Object],
    *,
    prefix: str,
    x: float,
    wall_y: float,
    facing: float,
    width: float,
    height: float,
    frame_mat: bpy.types.Material,
    slat_mat: bpy.types.Material,
    collection: bpy.types.Collection,
) -> None:
    half_width = width * 0.5
    centre_z = 0.12 + height * 0.5
    for side in (-1.0, 1.0):
        add_box(
            parts,
            f"{prefix}_jamb_{'l' if side < 0 else 'r'}",
            (0.16, 0.3, height + 0.25),
            (x + side * (half_width + 0.11), wall_y + facing * 0.04, centre_z),
            frame_mat,
            collection,
        )
    add_box(
        parts,
        f"{prefix}_lintel",
        (width + 0.66, 0.3, 0.17),
        (x, wall_y + facing * 0.04, 0.12 + height + 0.11),
        frame_mat,
        collection,
    )
    for row in range(4):
        add_box(
            parts,
            f"{prefix}_slat_{row}",
            (width - 0.3, 0.05, 0.15),
            (
                x,
                wall_y + facing * 0.1,
                0.42 + row * (height - 0.6) / 3.0,
            ),
            slat_mat,
            collection,
        )


def add_chimney(
    parts: list[bpy.types.Object],
    *,
    prefix: str,
    x: float,
    y: float,
    base_z: float,
    height: float,
    mat: bpy.types.Material,
    cap_mat: bpy.types.Material,
    collection: bpy.types.Collection,
    detail: bool,
) -> None:
    add_box(
        parts,
        f"{prefix}_stack",
        (0.85, 0.65, height),
        (x, y, base_z + height * 0.5),
        mat,
        collection,
        bevel=0.04 if detail else 0.0,
        segments=1,
    )
    if detail:
        add_box(
            parts,
            f"{prefix}_cap",
            (1.05, 0.85, 0.12),
            (x, y, base_z + height + 0.06),
            cap_mat,
            collection,
        )


def add_orchard_tree(
    parts: list[bpy.types.Object],
    *,
    prefix: str,
    x: float,
    y: float,
    index: int,
    lod: int,
    collection: bpy.types.Collection,
    mats: dict[str, bpy.types.Material],
) -> None:
    add_cylinder(
        parts,
        f"{prefix}_trunk",
        radius=0.26 if lod == 0 else 0.34,
        depth=2.8,
        location=(x, y, 1.4),
        mat=mats["trunk"],
        collection=collection,
        vertices=10 if lod == 0 else 7,
    )
    add_ico(
        parts,
        f"{prefix}_canopy",
        location=(x, y, 3.9),
        scale=(
            (2.1, 2.1, 2.2 + (index % 3) * 0.12)
            if lod == 0
            else (2.3, 2.3, 2.3)
        ),
        mat=mats["leaf"],
        collection=collection,
        subdivisions=2 if lod == 0 else 1,
        rotation_z=math.radians(23.0 * index),
    )


def add_palm(
    parts: list[bpy.types.Object],
    *,
    prefix: str,
    x: float,
    y: float,
    height: float,
    lod: int,
    collection: bpy.types.Collection,
    mats: dict[str, bpy.types.Material],
) -> None:
    vertices = 12 if lod == 0 else 8
    add_cylinder(
        parts,
        f"{prefix}_trunk",
        radius=0.34 if lod == 0 else 0.42,
        depth=height,
        location=(x, y, height * 0.5),
        mat=mats["trunk"],
        collection=collection,
        vertices=vertices,
        bevel=0.035 if lod == 0 else 0.0,
    )
    fronds = 10 if lod == 0 else 5 if lod == 1 else 2
    for index in range(fronds):
        angle = math.tau * index / fronds
        length = 4.2 if lod == 0 else 3.5
        add_box(
            parts,
            f"{prefix}_frond_{index}",
            (0.42 if lod == 0 else 0.62, length, 0.12),
            (
                x + math.sin(angle) * length * 0.28,
                y + math.cos(angle) * length * 0.28,
                height + 0.15 - (index % 2) * 0.22,
            ),
            mats["leaf"],
            collection,
            rotation=(math.radians(11.0 + (index % 2) * 7.0), 0.0, -angle),
            bevel=0.025 if lod == 0 else 0.0,
            segments=1,
        )


def add_house(
    *,
    prefix: str,
    x: float,
    y: float,
    yaw: float,
    lod: int,
    collection: bpy.types.Collection,
    mats: dict[str, bpy.types.Material],
) -> list[bpy.types.Object]:
    detail = lod == 0
    width = 24.0
    depth = 17.0
    height = 6.5
    ridge_z = height + 3.0
    front_y = -depth * 0.5
    body_mat = mats["stucco" if int(abs(x) + abs(y)) % 2 else "paint_cream"]
    local: list[bpy.types.Object] = []
    add_box(
        local,
        f"{prefix}_body",
        (width, depth, height),
        (0.0, 0.0, height * 0.5),
        body_mat,
        collection,
        bevel=0.12 if detail else 0.05 if lod == 1 else 0.0,
        segments=3 if detail else 1,
    )
    if lod < 2:
        add_plinth(
            local,
            f"{prefix}_plinth",
            width=width,
            depth=depth,
            centre=(0.0, 0.0),
            mat=mats["concrete"],
            collection=collection,
            detail=detail,
        )
        add_gable_prism(
            local,
            f"{prefix}_attic",
            width=width,
            depth=depth,
            base_z=height - 0.05,
            apex_z=ridge_z - 0.12,
            centre=(0.0, 0.0),
            mat=body_mat,
            collection=collection,
        )
    add_gabled_roof(
        local,
        prefix=f"{prefix}_roof",
        width=width + 2.0,
        depth=depth + 1.6,
        eave_z=height,
        ridge_z=ridge_z,
        centre=(0.0, 0.0),
        mat=mats["clay"],
        collection=collection,
        detail=detail,
        trim_mat=mats["paint_white"] if lod < 2 else None,
        accent_mat=mats["rock_dark"],
        tile_courses=9 if detail else 0,
        tile_segments=8 if detail else 0,
        ridge_cap=lod < 2,
        wall_width=width,
    )
    if lod < 2:
        add_box(
            local,
            f"{prefix}_garage",
            (7.2, 0.16, 2.7),
            (-5.6, front_y - 0.03, 1.5),
            mats["paint_white"],
            collection,
            bevel=0.04 if detail else 0.0,
            segments=2,
        )
        add_chimney(
            local,
            prefix=f"{prefix}_chimney",
            x=4.0,
            y=3.0,
            base_z=7.6,
            height=2.8,
            mat=mats["rock_dark"],
            cap_mat=mats["concrete"],
            collection=collection,
            detail=detail,
        )
    if detail:
        add_garage_trim(
            local,
            prefix=f"{prefix}_garage_trim",
            x=-5.6,
            wall_y=front_y - 0.03,
            facing=-1.0,
            width=7.2,
            height=2.7,
            frame_mat=mats["paint_white"],
            slat_mat=mats["paint_cream"],
            collection=collection,
        )
        add_inset_door(
            local,
            prefix=f"{prefix}_entry",
            x=4.2,
            wall_y=front_y,
            facing=-1.0,
            width=1.5,
            height=2.55,
            door_mat=mats["paint_blue"],
            frame_mat=mats["paint_white"],
            step_mat=mats["concrete"],
            canopy_mat=mats["clay"],
            collection=collection,
        )
        for wx in (-8.0, 0.5, 7.4):
            add_inset_window(
                local,
                prefix=f"{prefix}_window_{wx:+.1f}",
                x=wx,
                wall_y=front_y,
                facing=-1.0,
                centre_z=4.2,
                width=2.5,
                height=1.55,
                frame_mat=mats["paint_white"],
                glass_mat=mats["glass"],
                collection=collection,
                shutter_mat=mats["paint_blue"],
            )
        for wx in (-6.0, 6.0):
            add_inset_window(
                local,
                prefix=f"{prefix}_rear_window_{wx:+.1f}",
                x=wx,
                wall_y=depth * 0.5,
                facing=1.0,
                centre_z=4.2,
                width=2.5,
                height=1.55,
                frame_mat=mats["paint_white"],
                glass_mat=mats["glass"],
                collection=collection,
            )
        for side in (-1.0, 1.0):
            add_inset_window(
                local,
                prefix=f"{prefix}_side_window_{'w' if side < 0 else 'e'}",
                x=-2.5,
                wall_y=side * width * 0.5,
                facing=side,
                centre_z=4.2,
                width=2.2,
                height=1.5,
                frame_mat=mats["paint_white"],
                glass_mat=mats["glass"],
                collection=collection,
                axis="x",
            )
    elif lod == 1:
        add_box(
            local,
            f"{prefix}_entry",
            (1.5, 0.18, 2.55),
            (4.2, front_y - 0.04, 1.35),
            mats["paint_blue"],
            collection,
        )
        for wx in (-8.0, 0.5, 7.4):
            add_box(
                local,
                f"{prefix}_window_{wx:+.1f}",
                (2.5, 0.12, 1.55),
                (wx, front_y - 0.07, 4.2),
                mats["glass"],
                collection,
            )
    for obj in local:
        obj.location.x += x
        obj.location.y += y
        obj.rotation_euler.rotate_axis("Z", yaw)
        BASE.apply_transform(obj)
    return local


def farmstead_lod(
    spec: dict[str, Any],
    lod: int,
    collection: bpy.types.Collection,
    mats: dict[str, bpy.types.Material],
) -> list[bpy.types.Object]:
    parts: list[bpy.types.Object] = []
    driveway_min_x = -36.0
    driveway_max_x = -28.0
    driveway_min_y = -43.0
    driveway_max_y = 17.5
    driveway_branch_min_y = -39.0
    driveway_branch_max_y = -31.0
    add_box(
        parts,
        f"lod{lod}_field_base_west_front",
        (13.0, driveway_branch_min_y - driveway_min_y, 0.12),
        (
            -42.5,
            (driveway_min_y + driveway_branch_min_y) * 0.5,
            -0.06,
        ),
        mats["soil"],
        collection,
    )
    add_box(
        parts,
        f"lod{lod}_field_base_west_rear",
        (13.0, 43.0 - driveway_branch_max_y, 0.12),
        (
            -42.5,
            (driveway_branch_max_y + 43.0) * 0.5,
            -0.06,
        ),
        mats["soil"],
        collection,
    )
    add_box(
        parts,
        f"lod{lod}_field_base_east",
        (77.0, 86.0, 0.12),
        (10.5, 0.0, -0.06),
        mats["soil"],
        collection,
    )
    add_box(
        parts,
        f"lod{lod}_field_base_barn_apron",
        (
            driveway_max_x - driveway_min_x,
            43.0 - driveway_max_y,
            0.12,
        ),
        (
            (driveway_min_x + driveway_max_x) * 0.5,
            (driveway_max_y + 43.0) * 0.5,
            -0.06,
        ),
        mats["soil"],
        collection,
    )
    add_box(
        parts,
        f"lod{lod}_driveway",
        (
            driveway_max_x - driveway_min_x,
            driveway_max_y - driveway_min_y,
            0.04,
        ),
        (
            (driveway_min_x + driveway_max_x) * 0.5,
            (driveway_min_y + driveway_max_y) * 0.5,
            -0.02,
        ),
        mats["asphalt"],
        collection,
    )
    add_box(
        parts,
        f"lod{lod}_driveway_west_branch",
        (
            driveway_min_x - (-49.0),
            driveway_branch_max_y - driveway_branch_min_y,
            0.04,
        ),
        (
            (-49.0 + driveway_min_x) * 0.5,
            (driveway_branch_min_y + driveway_branch_max_y) * 0.5,
            -0.02,
        ),
        mats["asphalt"],
        collection,
    )
    row_count = 22 if lod == 0 else 9 if lod == 1 else 3
    for index in range(row_count):
        x = -45.0 + 90.0 * (index + 0.5) / row_count
        row_width = 2.5 if lod == 0 else 5.0
        if (
            x + row_width * 0.5 > driveway_min_x
            and x - row_width * 0.5 < driveway_max_x
        ):
            continue
        row_depth = 72.0
        row_y = -3.0
        if x + row_width * 0.5 <= driveway_min_x:
            row_depth = 64.0
            row_y = 1.0
        add_box(
            parts,
            f"lod{lod}_crop_row_{index}",
            (row_width, row_depth, 0.3),
            (x, row_y, 0.15),
            mats["crop_green" if index % 3 else "crop_gold"],
            collection,
            bevel=0.08 if lod == 0 else 0.0,
            segments=2,
        )
        if lod == 0:
            clump_count = 13
            clump_depth = row_depth / clump_count
            for clump in range(clump_count):
                phase = (index + clump) % 3
                add_box(
                    parts,
                    f"lod0_crop_clump_{index}_{clump}",
                    (
                        row_width * 0.92,
                        clump_depth * 0.66,
                        0.42 + phase * 0.09,
                    ),
                    (
                        x + (0.16 if (index + clump) % 2 else -0.16),
                        row_y - row_depth * 0.5 + (clump + 0.5) * clump_depth,
                        0.36 + phase * 0.045,
                    ),
                    mats["crop_green" if index % 3 else "crop_gold"],
                    collection,
                    rotation=(
                        0.0,
                        0.0,
                        math.radians(float((index * 7 + clump * 11) % 10) - 5.0),
                    ),
                )
    barn_x, barn_y = -32.0, 26.0
    barn_height = 8.2
    barn_ridge = 12.2
    add_box(
        parts,
        f"lod{lod}_barn_body",
        (24.0, 17.0, barn_height),
        (barn_x, barn_y, barn_height * 0.5),
        mats["paint_cream"],
        collection,
        bevel=0.1 if lod == 0 else 0.05 if lod == 1 else 0.0,
        segments=3 if lod == 0 else 1,
    )
    if lod < 2:
        add_plinth(
            parts,
            f"lod{lod}_barn_plinth",
            width=24.0,
            depth=17.0,
            centre=(barn_x, barn_y),
            mat=mats["concrete"],
            collection=collection,
            detail=lod == 0,
        )
        add_gable_prism(
            parts,
            f"lod{lod}_barn_attic",
            width=24.0,
            depth=17.0,
            base_z=barn_height - 0.05,
            apex_z=barn_ridge - 0.12,
            centre=(barn_x, barn_y),
            mat=mats["paint_cream"],
            collection=collection,
        )
    add_gabled_roof(
        parts,
        prefix=f"lod{lod}_barn_roof",
        width=26.0,
        depth=18.8,
        eave_z=barn_height,
        ridge_z=barn_ridge,
        centre=(barn_x, barn_y),
        mat=mats["clay"],
        collection=collection,
        detail=lod == 0,
        trim_mat=mats["paint_white"] if lod < 2 else None,
        accent_mat=mats["rock_dark"],
        tile_courses=10 if lod == 0 else 0,
        tile_segments=8 if lod == 0 else 0,
        ridge_cap=lod < 2,
        wall_width=24.0,
    )
    if lod < 2:
        add_box(
            parts,
            f"lod{lod}_barn_door",
            (5.4, 0.18, 5.8),
            (barn_x, barn_y - 8.57, 3.0),
            mats["paint_blue"],
            collection,
            bevel=0.05 if lod == 0 else 0.0,
            segments=2,
        )
        if lod == 0:
            add_box(
                parts,
                f"lod0_barn_door_track",
                (6.6, 0.3, 0.3),
                (barn_x, barn_y - 8.62, 6.18),
                mats["paint_white"],
                collection,
            )
            for side in (-1.0, 1.0):
                add_box(
                    parts,
                    f"lod0_barn_door_trim_{'l' if side < 0 else 'r'}",
                    (0.3, 0.3, 6.1),
                    (barn_x + side * 3.0, barn_y - 8.62, 3.05),
                    mats["paint_white"],
                    collection,
                )
                add_box(
                    parts,
                    f"lod0_barn_door_brace_{'l' if side < 0 else 'r'}",
                    (0.24, 0.1, 6.6),
                    (barn_x + side * 1.32, barn_y - 8.7, 3.0),
                    mats["paint_white"],
                    collection,
                    rotation=(0.0, side * math.radians(24.0), 0.0),
                )
            for wx in (-8.0, 8.0):
                add_inset_window(
                    parts,
                    prefix=f"lod0_barn_window_{wx:+.1f}",
                    x=barn_x + wx,
                    wall_y=barn_y - 8.5,
                    facing=-1.0,
                    centre_z=4.6,
                    width=1.6,
                    height=1.3,
                    frame_mat=mats["paint_white"],
                    glass_mat=mats["glass"],
                    collection=collection,
                )
            add_inset_window(
                parts,
                prefix="lod0_barn_loft_window",
                x=barn_x,
                wall_y=barn_y - 8.5,
                facing=-1.0,
                centre_z=9.3,
                width=1.5,
                height=1.5,
                frame_mat=mats["paint_white"],
                glass_mat=mats["glass"],
                collection=collection,
            )
            for wx in (-6.0, 6.0):
                add_inset_window(
                    parts,
                    prefix=f"lod0_barn_rear_window_{wx:+.1f}",
                    x=barn_x + wx,
                    wall_y=barn_y + 8.5,
                    facing=1.0,
                    centre_z=4.6,
                    width=1.6,
                    height=1.3,
                    frame_mat=mats["paint_white"],
                    glass_mat=mats["glass"],
                    collection=collection,
                )
            add_box(
                parts,
                f"lod0_barn_cupola_base",
                (2.3, 2.6, 1.1),
                (barn_x, barn_y, barn_ridge + 0.52),
                mats["paint_cream"],
                collection,
                bevel=0.05,
                segments=2,
            )
            for side in (-1.0, 1.0):
                add_box(
                    parts,
                    f"lod0_barn_cupola_vent_{'s' if side < 0 else 'n'}",
                    (1.5, 0.08, 0.7),
                    (barn_x, barn_y + side * 1.34, barn_ridge + 0.55),
                    mats["metal"],
                    collection,
                )
            add_gabled_roof(
                parts,
                prefix="lod0_barn_cupola_roof",
                width=2.9,
                depth=3.1,
                eave_z=barn_ridge + 1.07,
                ridge_z=barn_ridge + 1.75,
                centre=(barn_x, barn_y),
                mat=mats["clay"],
                collection=collection,
                detail=False,
                ridge_cap=True,
            )
            add_cylinder(
                parts,
                "lod0_barn_cupola_finial",
                radius=0.07,
                depth=0.7,
                location=(barn_x, barn_y, barn_ridge + 2.05),
                mat=mats["metal"],
                collection=collection,
                vertices=8,
            )
        elif lod == 1:
            add_box(
                parts,
                f"lod1_barn_cupola",
                (2.3, 2.6, 1.6),
                (barn_x, barn_y, barn_ridge + 0.75),
                mats["paint_cream"],
                collection,
            )
        silo_count = 2 if lod == 0 else 1
        for index in range(silo_count):
            sx = -7.0 + index * 8.0
            add_cylinder(
                parts,
                f"lod{lod}_silo_{index}",
                radius=3.2,
                depth=10.5,
                location=(sx, 31.0, 5.25),
                mat=mats["metal"],
                collection=collection,
                vertices=24 if lod == 0 else 10,
                bevel=0.06 if lod == 0 else 0.0,
            )
            add_ico(
                parts,
                f"lod{lod}_silo_cap_{index}",
                location=(sx, 31.0, 10.5),
                scale=(3.22, 3.22, 1.6),
                mat=mats["metal"],
                collection=collection,
                subdivisions=2 if lod == 0 else 1,
            )
            ring_count = 3 if lod == 0 else 1
            for ring in range(ring_count):
                add_cylinder(
                    parts,
                    f"lod{lod}_silo_ring_{index}_{ring}",
                    radius=3.3,
                    depth=0.1,
                    location=(sx, 31.0, 2.4 + ring * 2.8),
                    mat=mats["metal"],
                    collection=collection,
                    vertices=24 if lod == 0 else 10,
                )
            if lod == 0:
                add_cylinder(
                    parts,
                    f"lod0_silo_vent_{index}",
                    radius=0.34,
                    depth=0.8,
                    location=(sx, 31.0, 12.0),
                    mat=mats["metal"],
                    collection=collection,
                    vertices=10,
                )
    orchard_positions = (
        (-16.0, 38.4),
        (-8.0, 38.9),
        (0.0, 38.3),
        (8.0, 38.8),
        (16.0, 38.4),
        (24.0, 38.9),
        (32.0, 38.3),
        (40.0, 38.8),
    )
    orchard_count = 8 if lod == 0 else 2 if lod == 1 else 0
    for index, (tree_x, tree_y) in enumerate(orchard_positions[:orchard_count]):
        add_orchard_tree(
            parts,
            prefix=f"lod{lod}_orchard_{index}",
            x=tree_x,
            y=tree_y,
            index=index,
            lod=lod,
            collection=collection,
            mats=mats,
        )
    if lod == 0:
        for index in range(24):
            x = -46.0 + index * 4.0
            if driveway_min_x < x < driveway_max_x:
                continue
            add_cylinder(
                parts,
                f"lod0_fence_post_{index}",
                radius=0.09,
                depth=1.5,
                location=(x, -42.0, 0.75),
                mat=mats["trunk"],
                collection=collection,
                vertices=8,
            )
        for rail_z in (0.55, 1.12):
            for side, start_x, end_x in (
                ("west", -47.0, driveway_min_x),
                ("east", driveway_max_x, 47.0),
            ):
                add_box(
                    parts,
                    f"lod0_fence_rail_{side}_{rail_z}",
                    (end_x - start_x, 0.11, 0.11),
                    ((start_x + end_x) * 0.5, -42.0, rail_z),
                    mats["trunk"],
                    collection,
                )
    return parts


def suburb_lod(
    spec: dict[str, Any],
    lod: int,
    collection: bpy.types.Collection,
    mats: dict[str, bpy.types.Material],
) -> list[bpy.types.Object]:
    parts: list[bpy.types.Object] = []
    add_box(
        parts,
        f"lod{lod}_landscape",
        (96.0, 88.0, 0.11),
        (0.0, 0.0, -0.065),
        mats["sand"],
        collection,
    )
    add_box(
        parts,
        f"lod{lod}_shared_lane",
        (10.0, 84.0, 0.04),
        (0.0, 0.0, -0.02),
        mats["asphalt"],
        collection,
    )
    positions = (
        (-27.0, -24.0, 0.0),
        (27.0, -24.0, 0.0),
        (-27.0, 0.0, 0.0),
        (27.0, 0.0, 0.0),
        (-27.0, 24.0, 0.0),
        (27.0, 24.0, 0.0),
    )
    for index, (x, y, yaw) in enumerate(positions):
        parts.extend(add_house(
            prefix=f"lod{lod}_house_{index}",
            x=x,
            y=y,
            yaw=yaw,
            lod=lod,
            collection=collection,
            mats=mats,
        ))
    palm_positions = (
        (-43.5, -38.0),
        (43.5, -15.0),
        (-43.5, 12.0),
        (43.5, 38.0),
    )
    for index, (x, y) in enumerate(palm_positions[: 4 if lod == 0 else 2 if lod == 1 else 1]):
        add_palm(
            parts,
            prefix=f"lod{lod}_palm_{index}",
            x=x,
            y=y,
            height=9.0 + index * 0.7,
            lod=lod,
            collection=collection,
            mats=mats,
        )
    if lod == 0:
        for side in (-1.0, 1.0):
            add_box(
                parts,
                f"lod0_perimeter_wall_{side:+.0f}",
                (1.5, 84.0, 1.8),
                (side * 47.0, 0.0, 0.9),
                mats["stucco"],
                collection,
                bevel=0.08,
                segments=2,
            )
            add_box(
                parts,
                f"lod0_perimeter_wall_cap_{side:+.0f}",
                (1.7, 84.0, 0.16),
                (side * 47.0, 0.0, 1.86),
                mats["concrete"],
                collection,
                bevel=0.03,
                segments=1,
            )
            for pier in range(7):
                add_box(
                    parts,
                    f"lod0_perimeter_pier_{side:+.0f}_{pier}",
                    (1.9, 1.1, 2.04),
                    (side * 47.0, -36.0 + pier * 12.0, 1.02),
                    mats["stucco"],
                    collection,
                    bevel=0.06,
                    segments=2,
                )
    return parts


def station_lod(
    spec: dict[str, Any],
    lod: int,
    collection: bpy.types.Collection,
    mats: dict[str, bpy.types.Material],
) -> list[bpy.types.Object]:
    parts: list[bpy.types.Object] = []
    add_box(
        parts,
        f"lod{lod}_forecourt",
        (90.0, 65.0, 0.12),
        (0.0, 0.0, -0.06),
        mats["concrete"],
        collection,
    )
    store_y = 22.0
    add_box(
        parts,
        f"lod{lod}_market",
        (31.0, 17.0, 6.8),
        (0.0, store_y, 3.4),
        mats["paint_cream"],
        collection,
        bevel=0.12 if lod == 0 else 0.0,
        segments=3,
    )
    add_box(
        parts,
        f"lod{lod}_market_cap",
        (32.5, 18.2, 0.55),
        (0.0, store_y, 7.0),
        mats["sign"],
        collection,
        bevel=0.08 if lod == 0 else 0.0,
        segments=2,
    )
    if lod < 2:
        add_box(
            parts,
            f"lod{lod}_market_glass",
            (24.0, 0.15, 3.3),
            (0.0, store_y - 8.57, 2.15),
            mats["glass"],
            collection,
            bevel=0.035 if lod == 0 else 0.0,
            segments=2,
        )
    canopy_y = -4.0
    add_box(
        parts,
        f"lod{lod}_canopy",
        (44.0, 23.0, 0.72),
        (3.0, canopy_y, 5.6),
        mats["paint_white"],
        collection,
        bevel=0.16 if lod == 0 else 0.0,
        segments=3,
    )
    add_box(
        parts,
        f"lod{lod}_canopy_band",
        (44.3, 23.3, 0.55),
        (3.0, canopy_y, 5.82),
        mats["sign"],
        collection,
        bevel=0.1 if lod == 0 else 0.0,
        segments=2,
    )
    column_positions = ((-15.0, -11.0), (21.0, -11.0), (-15.0, 3.0), (21.0, 3.0))
    for index, (x, y) in enumerate(column_positions[: 4 if lod < 2 else 2]):
        add_cylinder(
            parts,
            f"lod{lod}_canopy_column_{index}",
            radius=0.42 if lod == 0 else 0.58,
            depth=5.25,
            location=(x, y, 2.625),
            mat=mats["metal"],
            collection=collection,
            vertices=16 if lod == 0 else 8,
            bevel=0.04 if lod == 0 else 0.0,
        )
    pump_count = 6 if lod == 0 else 3 if lod == 1 else 1
    for index in range(pump_count):
        x = -12.0 + (index % 3) * 12.0
        y = -8.0 + (index // 3) * 8.0
        add_box(
            parts,
            f"lod{lod}_pump_{index}",
            (1.35, 0.9, 2.2),
            (x, y, 1.1),
            mats["paint_blue"],
            collection,
            bevel=0.12 if lod == 0 else 0.0,
            segments=3,
        )
        if lod == 0:
            add_box(
                parts,
                f"lod0_pump_screen_{index}",
                (0.78, 0.08, 0.45),
                (x, y - 0.49, 1.45),
                mats["light"],
                collection,
                bevel=0.025,
                segments=1,
            )
    add_box(
        parts,
        f"lod{lod}_price_pylon",
        (3.2, 1.1, 10.5),
        (-38.0, -24.0, 5.25),
        mats["sign"],
        collection,
        bevel=0.14 if lod == 0 else 0.0,
        segments=3,
    )
    if lod == 0:
        for row in range(3):
            add_box(
                parts,
                f"lod0_pylon_glyph_{row}",
                (2.3, 0.1, 0.72),
                (-38.0, -24.61, 4.5 + row * 1.25),
                mats["light"],
                collection,
                bevel=0.035,
                segments=2,
            )
        for index in range(4):
            add_box(
                parts,
                f"lod0_ev_charger_{index}",
                (0.72, 0.7, 1.7),
                (28.0 + index * 3.2, 24.0, 0.85),
                mats["paint_blue"],
                collection,
                bevel=0.11,
                segments=3,
            )
    return parts


def mesa_lod(
    spec: dict[str, Any],
    lod: int,
    collection: bpy.types.Collection,
    mats: dict[str, bpy.types.Material],
) -> list[bpy.types.Object]:
    parts: list[bpy.types.Object] = []
    add_box(
        parts,
        f"lod{lod}_desert_base",
        (19.0, 19.0, 0.14),
        (0.0, 0.0, -0.07),
        mats["sand"],
        collection,
    )
    formations = (
        (-2.4, 0.8, 6.8, 5.1, 14.0),
        (3.6, 2.2, 4.7, 3.8, 9.5),
        (4.4, -3.9, 3.2, 2.8, 6.2),
        (-5.8, -4.2, 2.7, 2.2, 5.0),
    )
    count = 4 if lod == 0 else 3 if lod == 1 else 2
    subdivisions = 3 if lod == 0 else 2 if lod == 1 else 1
    for index, (x, y, sx, sy, sz) in enumerate(formations[:count]):
        add_box(
            parts,
            f"lod{lod}_formation_{index}",
            (sx * 1.55, sy * 1.5, sz),
            (x, y, sz * 0.5),
            mats["rock_light" if index % 2 == 0 else "rock_dark"],
            collection=collection,
            rotation=(0.0, 0.0, math.radians(11.0 * index)),
            bevel=(
                min(sx, sy) * 0.28
                if lod == 0
                else min(sx, sy) * 0.12
                if lod == 1
                else 0.0
            ),
            segments=4 if lod == 0 else 2,
        )
        if lod == 0:
            for layer in range(4):
                add_box(
                    parts,
                    f"lod0_strata_{index}_{layer}",
                    (
                        max(1.0, sx * 1.45 - layer * 0.7),
                        max(1.0, sy * 1.32 - layer * 0.5),
                        0.32,
                    ),
                    (x, y, 3.6 + layer * sz * 0.19),
                    mats["rock_dark" if layer % 2 else "rock_light"],
                    collection,
                    rotation=(0.0, 0.0, math.radians(index * 9.0)),
                    bevel=0.18,
                    segments=2,
                )
    if lod < 2:
        add_box(
            parts,
            f"lod{lod}_overlook_pad",
            (6.0, 3.2, 0.22),
            (5.4, 6.4, 0.11),
            mats["concrete"],
            collection,
            bevel=0.12 if lod == 0 else 0.0,
            segments=2,
        )
    if lod == 0:
        for index in range(8):
            angle = math.tau * index / 8.0
            add_box(
                parts,
                f"lod0_boulder_{index}",
                (
                    1.8 + index % 2 * 0.4,
                    1.4 + index % 3 * 0.2,
                    1.6,
                ),
                (
                    math.cos(angle) * (7.1 + index % 2 * 0.5),
                    math.sin(angle) * (6.5 + index % 3 * 0.35),
                    0.8,
                ),
                mats["rock_dark"],
                collection=collection,
                rotation=(0.0, 0.0, angle),
                bevel=0.38,
                segments=3,
            )
    return parts


def oasis_lod(
    spec: dict[str, Any],
    lod: int,
    collection: bpy.types.Collection,
    mats: dict[str, bpy.types.Material],
) -> list[bpy.types.Object]:
    parts: list[bpy.types.Object] = []
    add_box(
        parts,
        f"lod{lod}_arroyo_base",
        (19.0, 19.0, 0.14),
        (0.0, 0.0, -0.07),
        mats["sand"],
        collection,
    )
    add_box(
        parts,
        f"lod{lod}_water_basin",
        (10.4, 6.8, 0.18),
        (0.8, 0.3, 0.19),
        mats["water"],
        collection=collection,
        bevel=2.6 if lod == 0 else 1.4 if lod == 1 else 0.0,
        segments=6 if lod == 0 else 3,
    )
    bank_count = 18 if lod == 0 else 7 if lod == 1 else 3
    for index in range(bank_count):
        angle = math.tau * index / bank_count
        add_box(
            parts,
            f"lod{lod}_bank_rock_{index}",
            (
                1.3 + index % 3 * 0.32,
                1.04 + index % 2 * 0.26,
                1.1 + index % 2 * 0.2,
            ),
            (
                0.8 + math.cos(angle) * 6.2,
                0.3 + math.sin(angle) * 4.5,
                0.55 + (index % 3) * 0.12,
            ),
            mats["rock_light" if index % 2 else "rock_dark"],
            collection=collection,
            rotation=(0.0, 0.0, angle),
            bevel=0.28 if lod == 0 else 0.0,
            segments=3,
        )
    palm_positions = (
        (-4.6, -3.2),
        (-3.4, 4.3),
        (3.1, -4.4),
        (4.7, 3.7),
        (5.0, -0.8),
        (-5.0, 1.4),
    )
    palm_count = 6 if lod == 0 else 3 if lod == 1 else 1
    for index, (x, y) in enumerate(palm_positions[:palm_count]):
        add_palm(
            parts,
            prefix=f"lod{lod}_oasis_palm_{index}",
            x=x,
            y=y,
            height=7.2 + (index % 3) * 0.7,
            lod=lod,
            collection=collection,
            mats=mats,
        )
    if lod < 2:
        add_box(
            parts,
            f"lod{lod}_trail",
            (14.0, 1.4, 0.12),
            (-0.8, 7.2, 0.13),
            mats["soil"],
            collection,
            rotation=(0.0, 0.0, math.radians(-7.0)),
        )
    return parts


BUILDERS = {
    "farmland": farmstead_lod,
    "suburb": suburb_lod,
    "service-station": station_lod,
    "natural-landmark:red": mesa_lod,
    "natural-landmark:oasis": oasis_lod,
}


def build_render_lod(
    spec: dict[str, Any],
    lod: int,
    collection: bpy.types.Collection,
    mats: dict[str, bpy.types.Material],
) -> bpy.types.Object:
    category = str(spec["category"])
    if spec["asset_id"].endswith("red_mesa_19m"):
        builder = BUILDERS["natural-landmark:red"]
    elif spec["asset_id"].endswith("arroyo_oasis_19m"):
        builder = BUILDERS["natural-landmark:oasis"]
    else:
        builder = BUILDERS[category]
    parts = builder(spec, lod, collection, mats)
    obj = BASE.join_components(
        parts,
        name=f"{spec['asset_id']}_lod{lod}",
        role="render",
        lod=lod,
    )
    obj["rorng_family_id"] = FAMILY_ID
    obj["rorng_infill_category"] = category
    return obj


def build_collision(
    spec: dict[str, Any],
    collection: bpy.types.Collection,
    mat: bpy.types.Material,
) -> tuple[list[bpy.types.Object], list[dict[str, Any]]]:
    asset_id = str(spec["asset_id"])
    category = str(spec["category"])
    components: list[bpy.types.Object] = []
    if category == "farmland":
        components.append(
            BASE.make_box(
                "collision_farmhouse",
                dimensions=(23.2, 16.2, 8.2),
                location=(-32.0, 26.0, 4.1),
                material=mat,
                collection=collection,
            )
        )
    elif category == "suburb":
        for index, (x, y) in enumerate(
            (
                (-27.0, -24.0),
                (27.0, -24.0),
                (-27.0, 0.0),
                (27.0, 0.0),
                (-27.0, 24.0),
                (27.0, 24.0),
            )
        ):
            components.append(
                BASE.make_box(
                    f"collision_house_{index:02d}",
                    dimensions=(24.0, 17.0, 9.5),
                    location=(x, y, 4.75),
                    material=mat,
                    collection=collection,
                )
            )
        for side, x in (("west", -47.0), ("east", 47.0)):
            components.append(
                BASE.make_box(
                    f"collision_{side}_perimeter_wall",
                    dimensions=(1.5, 84.0, 1.8),
                    location=(x, 0.0, 0.9),
                    material=mat,
                    collection=collection,
                )
            )
    elif category == "service-station":
        components.extend(
            (
                BASE.make_box(
                    "collision_market",
                    dimensions=(32.5, 18.2, 7.275),
                    location=(0.0, 22.0, 3.6375),
                    material=mat,
                    collection=collection,
                ),
                BASE.make_box(
                    "collision_canopy",
                    dimensions=(44.3, 23.3, 0.55),
                    location=(3.0, -4.0, 5.82),
                    material=mat,
                    collection=collection,
                ),
            )
        )
        for index, (x, y) in enumerate(
            ((-15.0, -11.0), (21.0, -11.0), (-15.0, 3.0), (21.0, 3.0))
        ):
            components.append(
                BASE.make_box(
                    f"collision_canopy_column_{index:02d}",
                    dimensions=(0.84, 0.84, 5.25),
                    location=(x, y, 2.625),
                    material=mat,
                    collection=collection,
                )
            )
        for index in range(6):
            x = -12.0 + (index % 3) * 12.0
            y = -8.0 + (index // 3) * 8.0
            components.append(
                BASE.make_box(
                    f"collision_fuel_pump_{index:02d}",
                    dimensions=(1.35, 0.9, 2.2),
                    location=(x, y, 1.1),
                    material=mat,
                    collection=collection,
                )
            )
        components.append(
            BASE.make_box(
                "collision_price_pylon",
                dimensions=(3.2, 1.1, 10.5),
                location=(-38.0, -24.0, 5.25),
                material=mat,
                collection=collection,
            )
        )
        for index in range(4):
            components.append(
                BASE.make_box(
                    f"collision_ev_charger_{index:02d}",
                    dimensions=(0.72, 0.7, 1.7),
                    location=(28.0 + index * 3.2, 24.0, 0.85),
                    material=mat,
                    collection=collection,
                )
            )
    elif asset_id.endswith("red_mesa_19m"):
        components.append(
            BASE.make_box(
                "collision_mesa",
                dimensions=(11.5, 9.0, 14.0),
                location=(-2.4, 0.8, 7.0),
                material=mat,
                collection=collection,
            )
        )
    else:
        components.append(
            BASE.make_box(
                "collision_palm_trunk",
                dimensions=(1.8, 1.8, 7.2),
                location=(-4.6, -3.2, 3.6),
                material=mat,
                collection=collection,
            )
        )
    component_records = []
    component_ids = []
    for component in components:
        if not component.name.startswith("collision_"):
            raise RuntimeError(
                f"collision component name is not canonical: {component.name}"
            )
        component_id = component.name[len("collision_"):].replace("_", "-")
        if (
            not component_id
            or any(
                character not in "abcdefghijklmnopqrstuvwxyz0123456789-"
                for character in component_id
            )
        ):
            raise RuntimeError(
                f"collision component ID is invalid: {component_id}"
            )
        component_ids.append(component_id)
        component_records.append(
            {
                "bounds_blender_z_up": BASE.object_bounds(component),
                "component_id": component_id,
                "triangles": BASE.triangle_count(component),
            }
        )
    if len(component_ids) != len(set(component_ids)):
        raise RuntimeError(f"{asset_id} collision component IDs are duplicated")
    joined = BASE.join_components(
        components,
        name=f"{asset_id}_collision_fixture",
        role="collision-fixture",
        lod=None,
    )
    joined["rorng_collision_component_ids"] = json.dumps(
        component_ids,
        ensure_ascii=True,
        separators=(",", ":"),
    )
    return [joined], component_records


def add_preview_scene(
    spec: dict[str, Any],
    collection: bpy.types.Collection,
    mats: dict[str, bpy.types.Material],
    preview_path: Path,
) -> None:
    width, depth = spec["footprint_m"]
    height = float(spec["height_m"])
    BASE.make_box(
        "preview_ground",
        dimensions=(width + 36.0, depth + 36.0, 0.18),
        location=(0.0, 0.0, -0.09),
        material=mats["preview_ground"],
        collection=collection,
        bevel=0.06,
        bevel_segments=2,
    )
    bpy.ops.object.camera_add(
        location=(width * 0.72 + 32.0, -depth * 0.75 - 35.0, height * 1.2 + 18.0)
    )
    camera = bpy.context.object
    camera.name = "preview_camera"
    camera.data.lens = 52.0
    BASE.point_camera(camera, (0.0, 0.0, height * 0.28))
    BASE.move_to_collection(camera, collection)
    bpy.context.scene.camera = camera

    bpy.ops.object.light_add(type="SUN", location=(-30.0, -35.0, 70.0))
    sun = bpy.context.object
    sun.name = "preview_sun"
    sun.rotation_euler = (
        math.radians(28.0),
        math.radians(-18.0),
        math.radians(-35.0),
    )
    sun.data.energy = 3.4
    sun.data.angle = math.radians(3.0)
    BASE.move_to_collection(sun, collection)

    bpy.ops.object.light_add(
        type="AREA",
        location=(-width * 0.55, -depth * 0.55, height + 24.0),
    )
    fill = bpy.context.object
    fill.name = "preview_fill"
    fill.data.energy = 2_400.0
    fill.data.shape = "DISK"
    fill.data.size = 16.0
    BASE.point_camera(fill, (0.0, 0.0, height * 0.25))
    BASE.move_to_collection(fill, collection)

    world = bpy.context.scene.world
    world.use_nodes = True
    background = world.node_tree.nodes.get("Background")
    background.inputs["Color"].default_value = (0.085, 0.12, 0.18, 1.0)
    background.inputs["Strength"].default_value = 0.45
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x = 1280
    scene.render.resolution_y = 720
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.image_settings.color_depth = "8"
    scene.render.filepath = str(preview_path)
    scene.view_settings.look = "AgX - Medium High Contrast"
    scene.view_settings.exposure = 0.18


def asset_paths(root: Path, asset_id: str) -> dict[str, Path]:
    relative = Path("regional_infill") / asset_id
    source_root = root / "content-source/cityworld_next" / relative
    runtime_root = root / "resources/nextgen/cityworld" / relative
    return {
        "blend": source_root / f"{asset_id}.blend",
        "glb": runtime_root / f"{asset_id}.glb",
        "manifest": runtime_root / f"{asset_id}.asset.json",
        "preview": source_root / f"{asset_id}_preview.png",
    }


def runtime_lights(spec: dict[str, Any]) -> dict[str, Any] | None:
    if spec["category"] != "service-station":
        return None
    positions = (
        (-12.0, -8.0, 5.16),
        (0.0, -8.0, 5.16),
        (12.0, -8.0, 5.16),
        (-12.0, 0.0, 5.16),
        (0.0, 0.0, 5.16),
        (12.0, 0.0, 5.16),
    )
    return {
        "profile": "ror-cityworld-local-lights-v1",
        "lights": [
            {
                "color_linear": [1.0, 0.66, 0.31],
                "id": f"rorng_infill_station_canopy_{index:02d}",
                "position_blender_z_up_m": list(position),
                "range_m": 24.0,
                "type": "point",
            }
            for index, position in enumerate(positions)
        ],
    }


def generate_variant(root: Path, spec: dict[str, Any]) -> dict[str, Any]:
    asset_id = str(spec["asset_id"])
    paths = asset_paths(root, asset_id)
    for path in paths.values():
        path.parent.mkdir(parents=True, exist_ok=True)
    generator_path = Path(__file__).resolve()
    candidate_blend = paths["blend"].with_name(f".{asset_id}.candidate.blend")
    candidate_glb = paths["glb"].with_name(f".{asset_id}.candidate.glb")
    candidate_preview = paths["preview"].with_name(f".{asset_id}.candidate.png")
    for candidate in (candidate_blend, candidate_glb, candidate_preview):
        candidate.unlink(missing_ok=True)

    reset_scene_fully(asset_id, str(spec["profile"]))
    render_collection = BASE.make_collection(f"{asset_id}_render")
    collision_collection = BASE.make_collection(f"{asset_id}_collision")
    preview_collection = BASE.make_collection(f"{asset_id}_preview")
    mats = make_materials(asset_id)
    lod_objects = [
        build_render_lod(spec, lod, render_collection, mats)
        for lod in range(3)
    ]
    collision_objects, collision_components = build_collision(
        spec,
        collision_collection,
        mats["collision"],
    )
    asset_objects = [*lod_objects, *collision_objects]
    bpy.ops.object.select_all(action="DESELECT")
    for obj in asset_objects:
        obj.hide_set(False)
        obj.select_set(True)
    bpy.context.view_layer.objects.active = lod_objects[0]
    bpy.ops.export_scene.gltf(
        filepath=str(candidate_glb),
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
    orthogonalize_glb_tangents(candidate_glb)
    CANONICALIZER.canonicalize_glb_geometry(candidate_glb)
    canonicalize_textureless_texcoords(candidate_glb)

    for obj in [lod_objects[1], lod_objects[2], *collision_objects]:
        obj.hide_render = True
        obj.hide_set(True)
    lod_objects[0].hide_render = False
    lod_objects[0].hide_set(False)
    add_preview_scene(spec, preview_collection, mats, candidate_preview)
    bpy.ops.wm.save_as_mainfile(filepath=str(candidate_blend), compress=False)
    bpy.ops.render.render(write_still=True)
    os.replace(candidate_blend, paths["blend"])
    os.replace(candidate_glb, paths["glb"])
    os.replace(candidate_preview, paths["preview"])

    used_material_names = {
        assigned.name
        for obj in asset_objects
        for assigned in obj.data.materials
        if assigned is not None
    }
    asset_materials = {
        key: value
        for key, value in mats.items()
        if value.name in used_material_names
    }
    manifest = BASE.make_manifest(
        root=root,
        generator_path=generator_path,
        blend_path=paths["blend"],
        glb_path=paths["glb"],
        preview_path=paths["preview"],
        lod_objects=lod_objects,
        collision_objects=collision_objects,
        materials=asset_materials,
    )
    runtime_parents = {
        f"{asset_id}_{suffix}": parent
        for suffix, parent in dict(
            spec.get("runtime_parent_materials", {})
        ).items()
    }
    for declaration in manifest["materials"]:
        parent = runtime_parents.pop(declaration["name"], None)
        if parent is not None:
            declaration["runtime_parent_material"] = parent
    if runtime_parents:
        raise RuntimeError(
            f"{asset_id} declares runtime parents for unused materials: "
            + ", ".join(sorted(runtime_parents))
        )
    generator_record = {
        "dependencies": [
            {
                "path": dependency.relative_to(root).as_posix(),
                "sha256": BASE.sha256_file(dependency),
            }
            for dependency in AUTHORING_DEPENDENCY_PATHS
        ],
        "format": GENERATOR_ID,
        "path": generator_path.relative_to(root).as_posix(),
        "sha256": BASE.sha256_file(generator_path),
    }
    manifest["asset"]["profile"] = spec["profile"]
    manifest["authoring"]["artifact_reproducibility"] = {
        "blend": "authenticated-session-metadata-bearing",
        "glb": "byte-deterministic-pinned-toolchain",
        "preview": "authenticated-render-metadata-bearing",
    }
    manifest["authoring"]["generator"] = generator_record
    manifest["authoring"]["procedural_provenance"] = {
        "external_geometry": False,
        "external_materials": False,
        "external_textures": False,
        "method": "deterministic-project-authored-blender-python",
        "rights_basis": "GPL-3.0-or-later project-authored source",
    }
    manifest["collision"].pop("road_surface_z_m", None)
    collision_component_count = len(collision_components)
    manifest["collision"]["profile"] = (
        "compound-watertight-proxy-v1"
        if spec["category"] in {"suburb", "service-station"}
        else "single-watertight-proxy-v1"
    )
    manifest["collision"]["components_format"] = (
        COLLISION_COMPONENTS_FORMAT
    )
    manifest["collision"]["components"] = collision_components
    manifest["collision"]["objects"][0]["topology"][
        "connected_components"
    ] = collision_component_count
    manifest["collision"]["purpose"] = "bounded-landmark-or-building-envelope"
    manifest["connectors"] = []
    lod_entries = manifest["geometry"]["lods"]
    lod0_triangles = int(lod_entries[0]["triangles"])
    geometry: dict[str, Any] = {
        "asset_family": FAMILY_ID,
        "detail_profile": {
            "architecture": "real-scale-recessed-openings-roofs-fixtures",
            "landscape": "modeled-crop-rows-palms-strata-boulders-water-basin",
            "runtime_materials": "portable-factor-lit-rtshader-compatible",
        },
        "lod0_triangle_ceiling": max(80_000, lod0_triangles),
        "lod1_max_ratio": 0.35,
        "lod2_max_ratio": 0.12,
        "lods": lod_entries,
        "texcoord_policy": "canonical-zero-textureless-v1",
    }
    if spec["profile"] == "static-building-v1":
        geometry.update(
            {
                "footprint_m": [
                    float(value) for value in spec["footprint_m"]
                ],
                "foundation_recess_m": 0.12,
                "ground_plane_z_m": 0.0,
                "height_limit_m": float(spec["height_m"]),
            }
        )
    else:
        geometry.update(
            {
                "fixture_height_m": float(spec["height_m"]),
                "footprint_diameter_m": max(
                    float(value) for value in spec["footprint_m"]
                ),
            }
        )
    manifest["geometry"] = geometry
    manifest["infill"] = {
        "category": spec["category"],
        "family": FAMILY_ID,
        "fictional_branding": True,
        "label": spec["label"],
        "placement_integration": "cityworld-local-overlay-v7",
        "runtime_texture_dependencies": [],
    }
    lights = runtime_lights(spec)
    if lights is not None:
        manifest["runtime_lights"] = lights
    paths["manifest"].write_text(
        json.dumps(manifest, indent=2, ensure_ascii=True, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return {
        "asset_id": asset_id,
        "category": spec["category"],
        "lod_triangles": [BASE.triangle_count(obj) for obj in lod_objects],
        "manifest": paths["manifest"].relative_to(root).as_posix(),
    }


def write_family_contract(root: Path, generated: list[dict[str, Any]]) -> Path:
    path = (
        root
        / "content-source/cityworld_next/regional_infill/"
        "rorng_city_regional_infill_family.v1.json"
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    generator_path = Path(__file__).resolve()
    contract = {
        "asset": {
            "author": BASE.AUTHOR,
            "id": FAMILY_ID,
            "license": BASE.LICENSE,
            "source_uri": BASE.SOURCE_URI,
            "version": 1,
        },
        "authoring": {
            "blender_version": bpy.app.version_string,
            "generator": {
                "dependencies": [
                    {
                        "path": dependency.relative_to(root).as_posix(),
                        "sha256": BASE.sha256_file(dependency),
                    }
                    for dependency in AUTHORING_DEPENDENCY_PATHS
                ],
                "format": GENERATOR_ID,
                "path": generator_path.relative_to(root).as_posix(),
                "sha256": BASE.sha256_file(generator_path),
            },
            "procedural_provenance": {
                "external_geometry": False,
                "external_materials": False,
                "external_textures": False,
                "method": "deterministic-project-authored-blender-python",
                "rights_basis": "GPL-3.0-or-later project-authored source",
            },
        },
        "format": "ror-cityworld-regional-infill-family-v1",
        "placement_target": {
            "integration_status": "asset-ready-overlay-v7",
            "source_archive_sha256":
                "ebeac2f0204f25ca1955f29ca1583b2afa4517a3a848feb1db203814acac2ef3",
        },
        "variants": generated,
    }
    path.write_text(
        json.dumps(contract, indent=2, ensure_ascii=True, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return path


def main() -> int:
    args = parse_args()
    root = args.output_root.resolve()
    if not (root / ".git").exists():
        raise RuntimeError(f"--output-root is not a repository root: {root}")
    if bpy.app.version_string != PINNED_BLENDER_VERSION:
        raise RuntimeError(
            "regional infill generation requires Blender "
            f"{PINNED_BLENDER_VERSION}; found {bpy.app.version_string}"
        )
    generated = [generate_variant(root, spec) for spec in VARIANTS]
    family_path = write_family_contract(root, generated)
    print(
        json.dumps(
            {
                "assets": generated,
                "blender_version": bpy.app.version_string,
                "family": family_path.relative_to(root).as_posix(),
                "format": GENERATOR_ID,
            },
            ensure_ascii=True,
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
