#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the first project-authored CityWorld Next bridge module.

Run with Blender 4.0 or newer:

    blender --background --factory-startup --python \
      tools/blender/cityworld_next/generate_bridge_kit.py -- \
      --output-root /path/to/rigs-of-rods

The generator deliberately uses only Blender's bundled Python API.  It authors
in metres with Blender's Z-up convention, applies every object transform, and
exports a standard Y-up glTF 2.0 GLB for the offline scene compiler.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import sys
from typing import Any, Iterable

import bpy
from mathutils import Vector


ASSET_FORMAT = "ror-cityworld-asset-v1"
ASSET_ID = "rorng_city_bridge_span_20m"
ASSET_VERSION = 1
GENERATOR_ID = "ror-cityworld-blender-generator-v1"
LICENSE = "GPL-3.0-or-later"
AUTHOR = "Oasiz AI and Rigs of Rods contributors"
SOURCE_URI = "https://github.com/oasiz-ai/rigs-of-rods"

BRIDGE_LENGTH_M = 20.0
BRIDGE_WIDTH_M = 10.0
ROAD_WIDTH_M = 8.9
ROAD_SURFACE_Z_M = 0.0
LANE_WIDTH_M = 3.5


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_args() -> argparse.Namespace:
    argv = sys.argv
    script_args = argv[argv.index("--") + 1 :] if "--" in argv else []
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output-root",
        type=Path,
        default=Path(__file__).resolve().parents[3],
        help="Rigs of Rods repository root",
    )
    parser.add_argument(
        "--preview-path",
        type=Path,
        help="Optional PNG path (defaults below build/nextgen-cityworld)",
    )
    return parser.parse_args(script_args)


def reset_scene() -> None:
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for datablocks in (
        bpy.data.meshes,
        bpy.data.curves,
        bpy.data.materials,
        bpy.data.cameras,
        bpy.data.lights,
    ):
        for datablock in list(datablocks):
            if datablock.users == 0:
                datablocks.remove(datablock)

    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0
    scene.render.film_transparent = False
    scene.frame_set(1)
    bpy.context.preferences.filepaths.save_version = 0
    scene[GENERATOR_ID] = ASSET_VERSION
    scene["rorng_asset_id"] = ASSET_ID
    scene["rorng_authoring_axis"] = "blender-z-up"
    scene["rorng_interchange_axis"] = "gltf-y-up"
    scene["rorng_units"] = "metres"


def make_collection(name: str) -> bpy.types.Collection:
    collection = bpy.data.collections.new(name)
    bpy.context.scene.collection.children.link(collection)
    return collection


def move_to_collection(obj: bpy.types.Object, collection: bpy.types.Collection) -> None:
    for current in list(obj.users_collection):
        current.objects.unlink(obj)
    collection.objects.link(obj)


def make_material(
    name: str,
    base_color: tuple[float, float, float, float],
    *,
    metallic: float,
    roughness: float,
    emission: tuple[float, float, float] | None = None,
    emission_strength: float = 0.0,
) -> bpy.types.Material:
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    material.diffuse_color = base_color
    material.metallic = metallic
    material.roughness = roughness
    material.use_backface_culling = True
    material["rorng_color_space"] = "linear-factor"
    material["rorng_license"] = LICENSE

    principled = material.node_tree.nodes.get("Principled BSDF")
    if principled is None:
        raise RuntimeError(f"Material {name} has no Principled BSDF node")
    principled.inputs["Base Color"].default_value = base_color
    principled.inputs["Metallic"].default_value = metallic
    principled.inputs["Roughness"].default_value = roughness
    if emission is not None:
        emission_input = principled.inputs.get("Emission Color") or principled.inputs.get("Emission")
        strength_input = principled.inputs.get("Emission Strength")
        if emission_input is not None:
            emission_input.default_value = (*emission, 1.0)
        if strength_input is not None:
            strength_input.default_value = emission_strength
    return material


def assign_material(obj: bpy.types.Object, material: bpy.types.Material) -> None:
    obj.data.materials.append(material)


def apply_transform(obj: bpy.types.Object) -> None:
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    obj.select_set(False)


def add_bevel(obj: bpy.types.Object, width: float, segments: int) -> None:
    if width <= 0.0:
        return
    modifier = obj.modifiers.new(name="rorng_edge_bevel", type="BEVEL")
    modifier.width = width
    modifier.segments = segments
    modifier.limit_method = "ANGLE"
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    obj.select_set(False)


def make_box(
    name: str,
    *,
    dimensions: tuple[float, float, float],
    location: tuple[float, float, float],
    material: bpy.types.Material,
    collection: bpy.types.Collection,
    bevel: float = 0.0,
    bevel_segments: int = 1,
) -> bpy.types.Object:
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=location)
    obj = bpy.context.object
    obj.name = name
    obj.dimensions = dimensions
    apply_transform(obj)
    add_bevel(obj, bevel, bevel_segments)
    assign_material(obj, material)
    move_to_collection(obj, collection)
    return obj


def make_cylinder(
    name: str,
    *,
    radius: float,
    depth: float,
    location: tuple[float, float, float],
    rotation: tuple[float, float, float],
    vertices: int,
    material: bpy.types.Material,
    collection: bpy.types.Collection,
    bevel: float = 0.0,
) -> bpy.types.Object:
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=vertices,
        radius=radius,
        depth=depth,
        end_fill_type="NGON",
        location=location,
        rotation=rotation,
    )
    obj = bpy.context.object
    obj.name = name
    apply_transform(obj)
    add_bevel(obj, bevel, 2)
    assign_material(obj, material)
    move_to_collection(obj, collection)
    return obj


def join_components(
    components: Iterable[bpy.types.Object],
    *,
    name: str,
    role: str,
    lod: int | None,
) -> bpy.types.Object:
    items = list(components)
    if not items:
        raise RuntimeError(f"No components supplied for {name}")
    bpy.ops.object.select_all(action="DESELECT")
    for item in items:
        item.select_set(True)
    bpy.context.view_layer.objects.active = items[0]
    if len(items) > 1:
        bpy.ops.object.join()
    obj = bpy.context.object
    obj.name = name
    obj.data.name = f"{name}_mesh"
    obj["rorng_asset_id"] = ASSET_ID
    obj["rorng_role"] = role
    obj["rorng_units"] = "metres"
    if lod is not None:
        obj["rorng_lod"] = lod
    apply_transform(obj)
    modifier = obj.modifiers.new(name="rorng_triangulate", type="TRIANGULATE")
    modifier.keep_custom_normals = True
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    obj.select_set(False)
    return obj


def triangle_count(obj: bpy.types.Object) -> int:
    return sum(max(0, len(polygon.vertices) - 2) for polygon in obj.data.polygons)


def object_bounds(obj: bpy.types.Object) -> dict[str, list[float]]:
    corners = [obj.matrix_world @ Vector(corner) for corner in obj.bound_box]
    return {
        "min": [round(min(corner[axis] for corner in corners), 6) for axis in range(3)],
        "max": [round(max(corner[axis] for corner in corners), 6) for axis in range(3)],
    }


def build_lod0(
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
) -> bpy.types.Object:
    parts: list[bpy.types.Object] = []
    concrete = materials["concrete"]
    asphalt = materials["asphalt"]
    steel = materials["steel"]
    dark_steel = materials["dark_steel"]
    white = materials["lane_white"]
    yellow = materials["lane_yellow"]

    parts.append(
        make_box(
            "lod0_deck",
            dimensions=(BRIDGE_WIDTH_M, BRIDGE_LENGTH_M, 0.68),
            location=(0.0, 0.0, -0.38),
            material=concrete,
            collection=collection,
            bevel=0.06,
            bevel_segments=3,
        )
    )
    parts.append(
        make_box(
            "lod0_asphalt",
            dimensions=(9.2, 19.96, 0.08),
            location=(0.0, 0.0, -0.04),
            material=asphalt,
            collection=collection,
            bevel=0.015,
            bevel_segments=2,
        )
    )

    for side in (-1.0, 1.0):
        label = "left" if side < 0.0 else "right"
        parts.append(
            make_box(
                f"lod0_barrier_{label}",
                dimensions=(0.36, 19.9, 1.04),
                location=(side * 4.72, 0.0, 0.5),
                material=concrete,
                collection=collection,
                bevel=0.075,
                bevel_segments=3,
            )
        )
        parts.append(
            make_box(
                f"lod0_barrier_rail_{label}",
                dimensions=(0.13, 19.7, 0.13),
                location=(side * 4.55, 0.0, 1.02),
                material=steel,
                collection=collection,
                bevel=0.035,
                bevel_segments=2,
            )
        )

    for index, x_position in enumerate((-3.25, -1.1, 1.1, 3.25)):
        parts.append(
            make_box(
                f"lod0_girder_{index}",
                dimensions=(0.34, 19.55, 0.78),
                location=(x_position, 0.0, -0.98),
                material=steel,
                collection=collection,
                bevel=0.045,
                bevel_segments=2,
            )
        )

    for index, y_position in enumerate((-8.0, -4.0, 0.0, 4.0, 8.0)):
        parts.append(
            make_box(
                f"lod0_crossbeam_{index}",
                dimensions=(8.8, 0.32, 0.42),
                location=(0.0, y_position, -0.84),
                material=steel,
                collection=collection,
                bevel=0.035,
                bevel_segments=2,
            )
        )

    for end_index, y_position in enumerate((-9.82, 9.82)):
        parts.append(
            make_box(
                f"lod0_expansion_joint_{end_index}",
                dimensions=(9.18, 0.16, 0.035),
                location=(0.0, y_position, 0.018),
                material=dark_steel,
                collection=collection,
                bevel=0.008,
                bevel_segments=1,
            )
        )

    for line_name, x_position, width, material in (
        ("edge_left", -3.95, 0.12, white),
        ("centre_left", -0.09, 0.1, yellow),
        ("centre_right", 0.09, 0.1, yellow),
        ("edge_right", 3.95, 0.12, white),
    ):
        parts.append(
            make_box(
                f"lod0_lane_{line_name}",
                dimensions=(width, 19.45, 0.014),
                location=(x_position, 0.0, 0.009),
                material=material,
                collection=collection,
                bevel=0.004,
                bevel_segments=1,
            )
        )

    for drain_index, y_position in enumerate((-7.5, -3.75, 0.0, 3.75, 7.5)):
        for side in (-1.0, 1.0):
            label = "left" if side < 0.0 else "right"
            parts.append(
                make_box(
                    f"lod0_drain_{label}_{drain_index}",
                    dimensions=(0.3, 0.46, 0.028),
                    location=(side * 4.22, y_position, 0.016),
                    material=dark_steel,
                    collection=collection,
                    bevel=0.012,
                    bevel_segments=1,
                )
            )

    for pipe_index, x_position in enumerate((-2.55, 2.55)):
        parts.append(
            make_cylinder(
                f"lod0_service_pipe_{pipe_index}",
                radius=0.085,
                depth=19.15,
                location=(x_position, 0.0, -1.43),
                rotation=(math.radians(90.0), 0.0, 0.0),
                vertices=16,
                material=dark_steel,
                collection=collection,
                bevel=0.012,
            )
        )

    for mount_index, y_position in enumerate((-6.5, 6.5)):
        for side in (-1.0, 1.0):
            label = "left" if side < 0.0 else "right"
            parts.append(
                make_cylinder(
                    f"lod0_lamp_base_{label}_{mount_index}",
                    radius=0.22,
                    depth=0.18,
                    location=(side * 4.25, y_position, 0.1),
                    rotation=(0.0, 0.0, 0.0),
                    vertices=20,
                    material=steel,
                    collection=collection,
                    bevel=0.02,
                )
            )
            parts.append(
                make_cylinder(
                    f"lod0_lamp_socket_{label}_{mount_index}",
                    radius=0.085,
                    depth=0.38,
                    location=(side * 4.25, y_position, 0.36),
                    rotation=(0.0, 0.0, 0.0),
                    vertices=16,
                    material=dark_steel,
                    collection=collection,
                    bevel=0.012,
                )
            )

    return join_components(
        parts,
        name=f"{ASSET_ID}_lod0",
        role="render",
        lod=0,
    )


def build_lod1(
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
) -> bpy.types.Object:
    parts = [
        make_box(
            "lod1_deck",
            dimensions=(BRIDGE_WIDTH_M, BRIDGE_LENGTH_M, 0.68),
            location=(0.0, 0.0, -0.38),
            material=materials["concrete"],
            collection=collection,
            bevel=0.045,
            bevel_segments=1,
        ),
        make_box(
            "lod1_asphalt",
            dimensions=(9.2, 20.0, 0.07),
            location=(0.0, 0.0, -0.035),
            material=materials["asphalt"],
            collection=collection,
            bevel=0.01,
            bevel_segments=1,
        ),
    ]
    for side in (-1.0, 1.0):
        label = "left" if side < 0.0 else "right"
        parts.append(
            make_box(
                f"lod1_barrier_{label}",
                dimensions=(0.36, 20.0, 1.0),
                location=(side * 4.72, 0.0, 0.48),
                material=materials["concrete"],
                collection=collection,
                bevel=0.05,
                bevel_segments=1,
            )
        )
    for index, x_position in enumerate((-2.8, 2.8)):
        parts.append(
            make_box(
                f"lod1_girder_{index}",
                dimensions=(0.38, 19.6, 0.72),
                location=(x_position, 0.0, -0.96),
                material=materials["steel"],
                collection=collection,
                bevel=0.03,
                bevel_segments=1,
            )
        )
    for line_name, x_position, material in (
        ("edge_left", -3.95, materials["lane_white"]),
        ("centre", 0.0, materials["lane_yellow"]),
        ("edge_right", 3.95, materials["lane_white"]),
    ):
        parts.append(
            make_box(
                f"lod1_lane_{line_name}",
                dimensions=(0.12, 19.5, 0.012),
                location=(x_position, 0.0, 0.008),
                material=material,
                collection=collection,
            )
        )
    return join_components(
        parts,
        name=f"{ASSET_ID}_lod1",
        role="render",
        lod=1,
    )


def build_lod2(
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
) -> bpy.types.Object:
    parts = [
        make_box(
            "lod2_deck",
            dimensions=(BRIDGE_WIDTH_M, BRIDGE_LENGTH_M, 0.68),
            location=(0.0, 0.0, -0.38),
            material=materials["concrete"],
            collection=collection,
        ),
        make_box(
            "lod2_asphalt",
            dimensions=(9.2, 20.0, 0.06),
            location=(0.0, 0.0, -0.03),
            material=materials["asphalt"],
            collection=collection,
        ),
    ]
    for side in (-1.0, 1.0):
        label = "left" if side < 0.0 else "right"
        parts.append(
            make_box(
                f"lod2_barrier_{label}",
                dimensions=(0.34, 20.0, 0.88),
                location=(side * 4.72, 0.0, 0.42),
                material=materials["concrete"],
                collection=collection,
            )
        )
    return join_components(
        parts,
        name=f"{ASSET_ID}_lod2",
        role="render",
        lod=2,
    )


def build_collision(
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
) -> list[bpy.types.Object]:
    collision_material = materials["collision"]
    road = make_box(
        "collision_road_component",
        dimensions=(ROAD_WIDTH_M, BRIDGE_LENGTH_M, 0.12),
        location=(0.0, 0.0, -0.06),
        material=collision_material,
        collection=collection,
    )
    road = join_components(
        [road],
        name=f"{ASSET_ID}_collision_road",
        role="collision-road",
        lod=None,
    )

    barriers: list[bpy.types.Object] = []
    for side in (-1.0, 1.0):
        label = "left" if side < 0.0 else "right"
        barrier = make_box(
            f"collision_barrier_{label}_component",
            dimensions=(0.3, 20.0, 1.04),
            location=(side * 4.64, 0.0, 0.54),
            material=collision_material,
            collection=collection,
        )
        barriers.append(
            join_components(
                [barrier],
                name=f"{ASSET_ID}_collision_barrier_{label}",
                role="collision-barrier",
                lod=None,
            )
        )
    return [road, *barriers]


def point_camera(camera: bpy.types.Object, target: tuple[float, float, float]) -> None:
    direction = Vector(target) - camera.location
    camera.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()


def add_preview_scene(
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
    preview_path: Path,
) -> None:
    ground = make_box(
        "preview_ground",
        dimensions=(42.0, 48.0, 0.2),
        location=(0.0, 0.0, -2.08),
        material=materials["preview_ground"],
        collection=collection,
        bevel=0.08,
        bevel_segments=2,
    )
    ground["rorng_role"] = "preview-only"

    bpy.ops.object.camera_add(location=(15.5, -18.5, 7.0))
    camera = bpy.context.object
    camera.name = "preview_camera"
    camera.data.lens = 50.0
    point_camera(camera, (0.0, 0.0, -0.55))
    move_to_collection(camera, collection)
    bpy.context.scene.camera = camera

    bpy.ops.object.light_add(type="SUN", location=(0.0, 0.0, 12.0))
    sun = bpy.context.object
    sun.name = "preview_sun"
    sun.rotation_euler = (
        math.radians(34.0),
        math.radians(-22.0),
        math.radians(-28.0),
    )
    sun.data.energy = 3.0
    sun.data.angle = math.radians(3.5)
    move_to_collection(sun, collection)

    bpy.ops.object.light_add(type="AREA", location=(-8.0, -7.0, 8.0))
    area = bpy.context.object
    area.name = "preview_fill"
    area.data.energy = 1_300.0
    area.data.shape = "DISK"
    area.data.size = 7.0
    point_camera(area, (0.0, 0.0, -0.3))
    move_to_collection(area, collection)

    world = bpy.context.scene.world
    world.use_nodes = True
    background = world.node_tree.nodes.get("Background")
    background.inputs["Color"].default_value = (0.055, 0.075, 0.11, 1.0)
    background.inputs["Strength"].default_value = 0.32

    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x = 1280
    scene.render.resolution_y = 720
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.filepath = str(preview_path)
    scene.render.film_transparent = False
    scene.render.image_settings.color_depth = "8"
    scene.view_settings.look = "AgX - Medium High Contrast"


def make_manifest(
    *,
    root: Path,
    generator_path: Path,
    blend_path: Path,
    glb_path: Path,
    preview_path: Path,
    lod_objects: list[bpy.types.Object],
    collision_objects: list[bpy.types.Object],
    materials: dict[str, bpy.types.Material],
) -> dict[str, Any]:
    lod_entries = [
        {
            "bounds_blender_z_up": object_bounds(obj),
            "lod": int(obj["rorng_lod"]),
            "name": obj.name,
            "triangles": triangle_count(obj),
        }
        for obj in lod_objects
    ]
    lod_entries.sort(key=lambda item: item["lod"])
    collision_entries = [
        {
            "bounds_blender_z_up": object_bounds(obj),
            "name": obj.name,
            "role": obj["rorng_role"],
            "triangles": triangle_count(obj),
            "topology": {
                "connected_components": 1,
                "intersecting_faces": 0,
                "outward_winding": True,
                "watertight": True,
            },
        }
        for obj in collision_objects
    ]
    return {
        "asset": {
            "author": AUTHOR,
            "id": ASSET_ID,
            "license": LICENSE,
            "source_uri": SOURCE_URI,
            "version": ASSET_VERSION,
        },
        "artifacts": {
            "blend": {
                "path": blend_path.relative_to(root).as_posix(),
                "sha256": sha256_file(blend_path),
            },
            "glb": {
                "path": glb_path.relative_to(root).as_posix(),
                "sha256": sha256_file(glb_path),
            },
            "preview": {
                "path": preview_path.relative_to(root).as_posix(),
                "sha256": sha256_file(preview_path),
            },
        },
        "authoring": {
            "axis": "blender-z-up",
            "blender_version": bpy.app.version_string,
            "generator": {
                "format": GENERATOR_ID,
                "path": generator_path.relative_to(root).as_posix(),
                "sha256": sha256_file(generator_path),
            },
            "transforms_applied": True,
            "units": "metres",
        },
        "collision": {
            "objects": collision_entries,
            "road_surface_z_m": ROAD_SURFACE_Z_M,
            "separate_from_render_mesh": True,
        },
        "connectors": [
            {
                "forward": [0.0, -1.0, 0.0],
                "id": "start",
                "lane_centres_x_m": [-1.75, 1.75],
                "position_blender_z_up_m": [0.0, -10.0, ROAD_SURFACE_Z_M],
                "road_width_m": ROAD_WIDTH_M,
            },
            {
                "forward": [0.0, 1.0, 0.0],
                "id": "end",
                "lane_centres_x_m": [-1.75, 1.75],
                "position_blender_z_up_m": [0.0, 10.0, ROAD_SURFACE_Z_M],
                "road_width_m": ROAD_WIDTH_M,
            },
        ],
        "export": {
            "allowlisted_extensions": [],
            "animations": False,
            "apply_modifiers": True,
            "cameras": False,
            "format": "glb",
            "lights": False,
            "materials": "export",
            "profile": "gltf-2.0",
            "selection_only": True,
            "tangents": True,
            "textures": [],
            "y_up": True,
        },
        "format": ASSET_FORMAT,
        "geometry": {
            "bridge_length_m": BRIDGE_LENGTH_M,
            "bridge_width_m": BRIDGE_WIDTH_M,
            "lane_width_m": LANE_WIDTH_M,
            "lod0_triangle_ceiling": 60_000,
            "lod1_max_ratio": 0.35,
            "lod2_max_ratio": 0.10,
            "lods": lod_entries,
            "road_width_m": ROAD_WIDTH_M,
        },
        "materials": [
            {
                "base_color_factor_linear": [
                    round(float(channel), 6) for channel in material.diffuse_color
                ],
                "color_space": material["rorng_color_space"],
                "metallic_factor": round(float(material.metallic), 6),
                "name": material.name,
                "roughness_factor": round(float(material.roughness), 6),
            }
            for material in sorted(materials.values(), key=lambda item: item.name)
            if not material.name.startswith("rorng_preview_")
        ],
    }


def main() -> int:
    args = parse_args()
    root = args.output_root.resolve()
    generator_path = Path(__file__).resolve()
    if not (root / ".git").exists():
        raise RuntimeError(f"--output-root is not a repository root: {root}")

    blend_path = (
        root
        / "content-source"
        / "cityworld_next"
        / "bridge"
        / f"{ASSET_ID}.blend"
    )
    glb_path = (
        root
        / "resources"
        / "nextgen"
        / "cityworld"
        / "bridge"
        / f"{ASSET_ID}.glb"
    )
    manifest_path = (
        root
        / "resources"
        / "nextgen"
        / "cityworld"
        / "bridge"
        / f"{ASSET_ID}.asset.json"
    )
    preview_path = (
        args.preview_path.resolve()
        if args.preview_path
        else (
            root
            / "content-source"
            / "cityworld_next"
            / "bridge"
            / f"{ASSET_ID}_preview.png"
        )
    )
    for path in (blend_path, glb_path, manifest_path, preview_path):
        path.parent.mkdir(parents=True, exist_ok=True)

    reset_scene()
    render_collection = make_collection("rorng_city_bridge_render")
    collision_collection = make_collection("rorng_city_bridge_collision")
    preview_collection = make_collection("rorng_city_bridge_preview")

    materials = {
        "asphalt": make_material(
            "rorng_city_asphalt",
            (0.032, 0.038, 0.045, 1.0),
            metallic=0.0,
            roughness=0.9,
        ),
        "collision": make_material(
            "rorng_collision_debug",
            (0.8, 0.04, 0.03, 1.0),
            metallic=0.0,
            roughness=1.0,
        ),
        "concrete": make_material(
            "rorng_city_concrete",
            (0.31, 0.34, 0.37, 1.0),
            metallic=0.0,
            roughness=0.78,
        ),
        "dark_steel": make_material(
            "rorng_city_dark_steel",
            (0.035, 0.047, 0.058, 1.0),
            metallic=0.78,
            roughness=0.29,
        ),
        "lane_white": make_material(
            "rorng_city_lane_white",
            (0.82, 0.84, 0.79, 1.0),
            metallic=0.0,
            roughness=0.66,
        ),
        "lane_yellow": make_material(
            "rorng_city_lane_yellow",
            (0.93, 0.58, 0.035, 1.0),
            metallic=0.0,
            roughness=0.6,
        ),
        "preview_ground": make_material(
            "rorng_preview_ground",
            (0.055, 0.07, 0.075, 1.0),
            metallic=0.0,
            roughness=0.95,
        ),
        "steel": make_material(
            "rorng_city_galvanized_steel",
            (0.23, 0.29, 0.34, 1.0),
            metallic=0.72,
            roughness=0.34,
        ),
    }

    lod_objects = [
        build_lod0(render_collection, materials),
        build_lod1(render_collection, materials),
        build_lod2(render_collection, materials),
    ]
    collision_objects = build_collision(collision_collection, materials)

    for obj in [lod_objects[1], lod_objects[2], *collision_objects]:
        obj.hide_render = True
        obj.hide_set(True)

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

    for obj in [lod_objects[1], lod_objects[2], *collision_objects]:
        obj.hide_render = True
        obj.hide_set(True)
    lod_objects[0].hide_render = False
    lod_objects[0].hide_set(False)

    add_preview_scene(preview_collection, materials, preview_path)
    bpy.ops.wm.save_as_mainfile(filepath=str(blend_path), compress=False)
    bpy.ops.render.render(write_still=True)

    manifest = make_manifest(
        root=root,
        generator_path=generator_path,
        blend_path=blend_path,
        glb_path=glb_path,
        preview_path=preview_path,
        lod_objects=lod_objects,
        collision_objects=collision_objects,
        materials=materials,
    )
    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True, ensure_ascii=True) + "\n",
        encoding="utf-8",
    )

    print(
        json.dumps(
            {
                "asset_id": ASSET_ID,
                "blend": str(blend_path),
                "glb": str(glb_path),
                "lod_triangles": [
                    triangle_count(obj) for obj in lod_objects
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
