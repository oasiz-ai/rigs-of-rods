#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate a project-authored 20 m / 15 degree CityWorld bridge curve.

Run with Blender 5.2 LTS:

    blender --background --factory-startup --python \
      tools/blender/cityworld_next/generate_curved_bridge.py -- \
      --output-root /path/to/rigs-of-rods

The span has exact tangent-aware connectors, three authored render LODs,
separate watertight road/barrier collision volumes, four lighting fixtures,
and an integrated reinforced-concrete support pier. Geometry is authored in
metres with Blender Z-up and exported as applied-transform glTF Y-up.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import math
from pathlib import Path
import sys
from typing import Any

import bpy


SCRIPT_DIRECTORY = Path(__file__).resolve().parent
BASE_GENERATOR_PATH = SCRIPT_DIRECTORY / "generate_bridge_kit.py"
BASE_SPEC = importlib.util.spec_from_file_location(
    "rorng_tangent_bridge_generator",
    BASE_GENERATOR_PATH,
)
if BASE_SPEC is None or BASE_SPEC.loader is None:
    raise RuntimeError("cannot load the tangent bridge authoring helpers")
BASE = importlib.util.module_from_spec(BASE_SPEC)
BASE_SPEC.loader.exec_module(BASE)

ASSET_ID = "rorng_city_bridge_curve_left_15deg_20m"
ASSET_VERSION = 1
GENERATOR_ID = "ror-cityworld-curved-bridge-generator-v1"
ARC_LENGTH_M = 20.0
TURN_ANGLE_DEGREES = 15.0
TURN_ANGLE_RADIANS = math.radians(TURN_ANGLE_DEGREES)
HALF_ANGLE_RADIANS = TURN_ANGLE_RADIANS / 2.0
CURVE_RADIUS_M = ARC_LENGTH_M / TURN_ANGLE_RADIANS
CONNECTOR_CHORD_M = 2.0 * CURVE_RADIUS_M * math.sin(HALF_ANGLE_RADIANS)
BRIDGE_WIDTH_M = 10.0
ROAD_WIDTH_M = 8.9
ROAD_SURFACE_Z_M = 0.0
LANE_WIDTH_M = 3.5


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
    """Remove hidden source objects as well as visible scene contents.

    Blender's select-all operator intentionally skips hidden objects. The
    straight bridge source keeps non-preview LODs and collision meshes hidden,
    so a generator launched from that open file must remove datablocks
    directly to retain stable object and material names.
    """

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


def curve_frame(distance_m: float) -> tuple[float, float, float]:
    theta = distance_m / CURVE_RADIUS_M
    return (
        CURVE_RADIUS_M * (1.0 - math.cos(theta)),
        CURVE_RADIUS_M * math.sin(theta),
        theta,
    )


def make_swept_box(
    name: str,
    *,
    width: float,
    bottom_z: float,
    top_z: float,
    lateral_offset: float,
    segments: int,
    material: bpy.types.Material,
    collection: bpy.types.Collection,
) -> bpy.types.Object:
    if (
        width <= 0.0
        or top_z <= bottom_z
        or segments < 1
        or abs(lateral_offset) + width / 2.0 > BRIDGE_WIDTH_M / 2.0
    ):
        raise RuntimeError(f"invalid swept-box dimensions for {name}")

    vertices: list[tuple[float, float, float]] = []
    for index in range(segments + 1):
        distance = -ARC_LENGTH_M / 2.0 + ARC_LENGTH_M * index / segments
        center_x, center_y, theta = curve_frame(distance)
        right_x = math.cos(theta)
        right_y = -math.sin(theta)
        for local_x, z in (
            (lateral_offset - width / 2.0, bottom_z),
            (lateral_offset + width / 2.0, bottom_z),
            (lateral_offset + width / 2.0, top_z),
            (lateral_offset - width / 2.0, top_z),
        ):
            vertices.append(
                (
                    center_x + right_x * local_x,
                    center_y + right_y * local_x,
                    z,
                )
            )

    faces: list[tuple[int, int, int, int]] = [(0, 1, 2, 3)]
    for index in range(segments):
        current = index * 4
        following = (index + 1) * 4
        faces.extend(
            (
                (current, following, following + 1, current + 1),
                (current + 1, following + 1, following + 2, current + 2),
                (current + 2, following + 2, following + 3, current + 3),
                (current + 3, following + 3, following, current),
            )
        )
    final = segments * 4
    faces.append((final + 3, final + 2, final + 1, final))

    mesh = bpy.data.meshes.new(f"{name}_mesh")
    mesh.from_pydata(vertices, [], faces)
    mesh.validate(verbose=True, clean_customdata=False)
    mesh.update(calc_edges=True)
    uv_layer = mesh.uv_layers.new(name="UVMap")
    square_uvs = ((0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0))
    for polygon in mesh.polygons:
        for corner, loop_index in enumerate(polygon.loop_indices):
            uv_layer.data[loop_index].uv = square_uvs[corner]

    obj = bpy.data.objects.new(name, mesh)
    collection.objects.link(obj)
    BASE.assign_material(obj, material)
    return obj


def make_oriented_box(
    name: str,
    *,
    dimensions: tuple[float, float, float],
    location: tuple[float, float, float],
    yaw_radians: float,
    material: bpy.types.Material,
    collection: bpy.types.Collection,
    bevel: float = 0.0,
) -> bpy.types.Object:
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=location)
    obj = bpy.context.object
    obj.name = name
    obj.dimensions = dimensions
    obj.rotation_euler.z = yaw_radians
    BASE.apply_transform(obj)
    BASE.add_bevel(obj, bevel, 2)
    BASE.assign_material(obj, material)
    BASE.move_to_collection(obj, collection)
    return obj


def curve_point_with_offset(
    distance_m: float,
    lateral_offset: float,
    z: float,
) -> tuple[float, float, float, float]:
    center_x, center_y, theta = curve_frame(distance_m)
    return (
        center_x + math.cos(theta) * lateral_offset,
        center_y - math.sin(theta) * lateral_offset,
        z,
        theta,
    )


def add_support(
    parts: list[bpy.types.Object],
    *,
    prefix: str,
    material: bpy.types.Material,
    dark_material: bpy.types.Material,
    collection: bpy.types.Collection,
    detailed: bool,
) -> None:
    parts.append(
        make_oriented_box(
            f"{prefix}_pier_column",
            dimensions=(2.2, 1.8, 7.4),
            location=(0.0, 0.0, -5.35),
            yaw_radians=0.0,
            material=material,
            collection=collection,
            bevel=0.12 if detailed else 0.04,
        )
    )
    parts.append(
        make_oriented_box(
            f"{prefix}_pier_hammerhead",
            dimensions=(8.8, 1.35, 0.82),
            location=(0.0, 0.0, -1.78),
            yaw_radians=0.0,
            material=material,
            collection=collection,
            bevel=0.1 if detailed else 0.03,
        )
    )
    if detailed:
        for index, x_position in enumerate((-3.3, -1.1, 1.1, 3.3)):
            parts.append(
                make_oriented_box(
                    f"{prefix}_bearing_{index}",
                    dimensions=(0.48, 0.54, 0.18),
                    location=(x_position, 0.0, -1.27),
                    yaw_radians=0.0,
                    material=dark_material,
                    collection=collection,
                    bevel=0.025,
                )
            )


def add_lighting_fixtures(
    parts: list[bpy.types.Object],
    *,
    collection: bpy.types.Collection,
    steel: bpy.types.Material,
    dark_steel: bpy.types.Material,
    emissive: bpy.types.Material,
) -> None:
    for station_index, distance in enumerate((-6.2, 6.2)):
        for side in (-1.0, 1.0):
            side_name = "left" if side < 0.0 else "right"
            x, y, _, theta = curve_point_with_offset(distance, side * 4.3, 0.0)
            parts.append(
                BASE.make_cylinder(
                    f"lod0_lamp_pole_{side_name}_{station_index}",
                    radius=0.075,
                    depth=4.2,
                    location=(x, y, 2.1),
                    rotation=(0.0, 0.0, 0.0),
                    vertices=20,
                    material=steel,
                    collection=collection,
                    bevel=0.012,
                )
            )
            inward = -side
            arm_center = curve_point_with_offset(
                distance, side * 4.3 + inward * 0.5, 4.15
            )
            parts.append(
                make_oriented_box(
                    f"lod0_lamp_arm_{side_name}_{station_index}",
                    dimensions=(1.05, 0.1, 0.1),
                    location=arm_center[:3],
                    yaw_radians=-theta,
                    material=dark_steel,
                    collection=collection,
                    bevel=0.025,
                )
            )
            head_center = curve_point_with_offset(
                distance, side * 4.3 + inward * 0.98, 4.08
            )
            parts.append(
                make_oriented_box(
                    f"lod0_lamp_head_{side_name}_{station_index}",
                    dimensions=(0.34, 0.64, 0.12),
                    location=head_center[:3],
                    yaw_radians=-theta,
                    material=emissive,
                    collection=collection,
                    bevel=0.035,
                )
            )


def build_render_lod(
    lod: int,
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
) -> bpy.types.Object:
    segments = (32, 12, 4)[lod]
    prefix = f"lod{lod}"
    parts = [
        make_swept_box(
            f"{prefix}_deck",
            width=BRIDGE_WIDTH_M,
            bottom_z=-0.72,
            top_z=-0.04,
            lateral_offset=0.0,
            segments=segments,
            material=materials["concrete"],
            collection=collection,
        ),
        make_swept_box(
            f"{prefix}_asphalt",
            width=9.2,
            bottom_z=-0.08,
            top_z=0.0,
            lateral_offset=0.0,
            segments=segments,
            material=materials["asphalt"],
            collection=collection,
        ),
    ]
    for side in (-1.0, 1.0):
        side_name = "left" if side < 0.0 else "right"
        parts.append(
            make_swept_box(
                f"{prefix}_barrier_{side_name}",
                width=0.36 if lod < 2 else 0.34,
                bottom_z=-0.02,
                top_z=1.04 if lod == 0 else 0.92,
                lateral_offset=side * 4.72,
                segments=segments,
                material=materials["concrete"],
                collection=collection,
            )
        )
        if lod == 0:
            parts.append(
                make_swept_box(
                    f"{prefix}_barrier_rail_{side_name}",
                    width=0.13,
                    bottom_z=0.96,
                    top_z=1.09,
                    lateral_offset=side * 4.53,
                    segments=segments,
                    material=materials["steel"],
                    collection=collection,
                )
            )

    if lod < 2:
        girder_positions = (-3.25, -1.1, 1.1, 3.25) if lod == 0 else (-2.8, 2.8)
        for index, lateral in enumerate(girder_positions):
            parts.append(
                make_swept_box(
                    f"{prefix}_girder_{index}",
                    width=0.34 if lod == 0 else 0.4,
                    bottom_z=-1.48 if lod == 0 else -1.35,
                    top_z=-0.7,
                    lateral_offset=lateral,
                    segments=segments,
                    material=materials["steel"],
                    collection=collection,
                )
            )
        lane_specs = (
            ("edge_left", -3.95, 0.12, materials["lane_white"]),
            ("centre_left", -0.09, 0.1, materials["lane_yellow"]),
            ("centre_right", 0.09, 0.1, materials["lane_yellow"]),
            ("edge_right", 3.95, 0.12, materials["lane_white"]),
        )
        for name, lateral, width, material in lane_specs:
            parts.append(
                make_swept_box(
                    f"{prefix}_lane_{name}",
                    width=width,
                    bottom_z=0.004,
                    top_z=0.018,
                    lateral_offset=lateral,
                    segments=segments,
                    material=material,
                    collection=collection,
                )
            )

    add_support(
        parts,
        prefix=prefix,
        material=materials["concrete"],
        dark_material=materials["dark_steel"],
        collection=collection,
        detailed=lod == 0,
    )
    if lod == 0:
        for end_index, distance in enumerate((-ARC_LENGTH_M / 2.0, ARC_LENGTH_M / 2.0)):
            x, y, _, theta = curve_point_with_offset(distance, 0.0, 0.016)
            parts.append(
                make_oriented_box(
                    f"{prefix}_expansion_joint_{end_index}",
                    dimensions=(9.15, 0.16, 0.032),
                    location=(x, y, 0.016),
                    yaw_radians=-theta,
                    material=materials["dark_steel"],
                    collection=collection,
                    bevel=0.0,
                )
            )
        add_lighting_fixtures(
            parts,
            collection=collection,
            steel=materials["steel"],
            dark_steel=materials["dark_steel"],
            emissive=materials["lamp_emissive"],
        )

    return BASE.join_components(
        parts,
        name=f"{ASSET_ID}_lod{lod}",
        role="render",
        lod=lod,
    )


def build_collision(
    collection: bpy.types.Collection,
    material: bpy.types.Material,
) -> list[bpy.types.Object]:
    road = make_swept_box(
        "collision_road_component",
        width=ROAD_WIDTH_M,
        bottom_z=-0.12,
        top_z=0.0,
        lateral_offset=0.0,
        segments=32,
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
        side_name = "left" if side < 0.0 else "right"
        barrier = make_swept_box(
            f"collision_barrier_{side_name}_component",
            width=0.3,
            bottom_z=0.02,
            top_z=1.06,
            lateral_offset=side * 4.64,
            segments=32,
            material=material,
            collection=collection,
        )
        barriers.append(
            BASE.join_components(
                [barrier],
                name=f"{ASSET_ID}_collision_barrier_{side_name}",
                role="collision-barrier",
                lod=None,
            )
        )
    return [road, *barriers]


def make_materials() -> dict[str, bpy.types.Material]:
    return {
        "asphalt": BASE.make_material(
            "rorng_city_asphalt",
            (0.032, 0.038, 0.045, 1.0),
            metallic=0.0,
            roughness=0.9,
        ),
        "collision": BASE.make_material(
            "rorng_collision_debug",
            (0.8, 0.04, 0.03, 1.0),
            metallic=0.0,
            roughness=1.0,
        ),
        "concrete": BASE.make_material(
            "rorng_city_concrete",
            (0.31, 0.34, 0.37, 1.0),
            metallic=0.0,
            roughness=0.78,
        ),
        "dark_steel": BASE.make_material(
            "rorng_city_dark_steel",
            (0.035, 0.047, 0.058, 1.0),
            metallic=0.78,
            roughness=0.29,
        ),
        "lamp_emissive": BASE.make_material(
            "rorng_city_lamp_emissive",
            (0.95, 0.74, 0.31, 1.0),
            metallic=0.0,
            roughness=0.28,
            emission=(1.0, 0.72, 0.28),
            emission_strength=1.0,
        ),
        "lane_white": BASE.make_material(
            "rorng_city_lane_white",
            (0.82, 0.84, 0.79, 1.0),
            metallic=0.0,
            roughness=0.66,
        ),
        "lane_yellow": BASE.make_material(
            "rorng_city_lane_yellow",
            (0.93, 0.58, 0.035, 1.0),
            metallic=0.0,
            roughness=0.6,
        ),
        "preview_ground": BASE.make_material(
            "rorng_preview_ground",
            (0.055, 0.07, 0.075, 1.0),
            metallic=0.0,
            roughness=0.95,
        ),
        "steel": BASE.make_material(
            "rorng_city_galvanized_steel",
            (0.23, 0.29, 0.34, 1.0),
            metallic=0.72,
            roughness=0.34,
        ),
    }


def add_preview_scene(
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
    preview_path: Path,
) -> None:
    BASE.make_box(
        "preview_ground",
        dimensions=(45.0, 52.0, 0.3),
        location=(0.0, 0.0, -9.2),
        material=materials["preview_ground"],
        collection=collection,
        bevel=0.08,
        bevel_segments=2,
    )["rorng_role"] = "preview-only"

    bpy.ops.object.camera_add(location=(21.0, -24.0, 11.5))
    camera = bpy.context.object
    camera.name = "preview_camera"
    camera.data.lens = 52.0
    BASE.point_camera(camera, (0.2, 0.0, -2.4))
    BASE.move_to_collection(camera, collection)
    bpy.context.scene.camera = camera

    bpy.ops.object.light_add(type="SUN", location=(0.0, 0.0, 15.0))
    sun = bpy.context.object
    sun.name = "preview_sun"
    sun.rotation_euler = (
        math.radians(28.0),
        math.radians(-24.0),
        math.radians(-38.0),
    )
    sun.data.energy = 3.2
    sun.data.angle = math.radians(3.0)
    BASE.move_to_collection(sun, collection)

    bpy.ops.object.light_add(type="AREA", location=(-9.0, -8.0, 10.0))
    area = bpy.context.object
    area.name = "preview_fill"
    area.data.energy = 1_600.0
    area.data.shape = "DISK"
    area.data.size = 8.0
    BASE.point_camera(area, (0.0, 0.0, -2.0))
    BASE.move_to_collection(area, collection)

    world = bpy.context.scene.world
    world.use_nodes = True
    background = world.node_tree.nodes.get("Background")
    background.inputs["Color"].default_value = (0.035, 0.05, 0.08, 1.0)
    background.inputs["Strength"].default_value = 0.28

    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x = 1280
    scene.render.resolution_y = 720
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.image_settings.color_depth = "8"
    scene.render.filepath = str(preview_path)
    scene.render.film_transparent = False
    scene.view_settings.look = "AgX - Medium High Contrast"


def connector_record(identifier: str, distance_m: float, outward: bool) -> dict[str, Any]:
    x, y, theta = curve_frame(distance_m)
    tangent = (math.sin(theta), math.cos(theta), 0.0)
    direction = tangent if outward else tuple(-value for value in tangent)
    return {
        "forward": [round(value, 9) for value in direction],
        "id": identifier,
        "lane_centres_x_m": [-1.75, 1.75],
        "position_blender_z_up_m": [
            round(x, 9),
            round(y, 9),
            ROAD_SURFACE_Z_M,
        ],
        "road_width_m": ROAD_WIDTH_M,
    }


def main() -> int:
    args = parse_args()
    root = args.output_root.resolve()
    generator_path = Path(__file__).resolve()
    if not (root / ".git").exists():
        raise RuntimeError(f"--output-root is not a repository root: {root}")

    asset_root = (
        root
        / "resources"
        / "nextgen"
        / "cityworld"
        / "bridge"
        / "curve_left_15deg"
    )
    source_root = (
        root
        / "content-source"
        / "cityworld_next"
        / "bridge"
        / "curve_left_15deg"
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
    reset_scene_fully()
    scene = bpy.context.scene
    scene[GENERATOR_ID] = ASSET_VERSION
    scene["rorng_curve_radius_m"] = CURVE_RADIUS_M
    scene["rorng_turn_angle_degrees"] = TURN_ANGLE_DEGREES

    render_collection = BASE.make_collection("rorng_city_bridge_curve_render")
    collision_collection = BASE.make_collection("rorng_city_bridge_curve_collision")
    preview_collection = BASE.make_collection("rorng_city_bridge_curve_preview")
    materials = make_materials()
    lod_objects = [
        build_render_lod(lod, render_collection, materials)
        for lod in range(3)
    ]
    collision_objects = build_collision(
        collision_collection, materials["collision"]
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
    manifest["connectors"] = [
        connector_record("start", -ARC_LENGTH_M / 2.0, False),
        connector_record("end", ARC_LENGTH_M / 2.0, True),
    ]
    for material in manifest["materials"]:
        if material["name"] == "rorng_city_lamp_emissive":
            material["emissive_factor_linear"] = [1.0, 0.72, 0.28]
    manifest["geometry"].update(
        {
            "bridge_length_m": round(CONNECTOR_CHORD_M, 9),
            "centerline_length_m": ARC_LENGTH_M,
            "curve_radius_m": round(CURVE_RADIUS_M, 9),
            "turn_angle_degrees": TURN_ANGLE_DEGREES,
        }
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
                "connector_chord_m": round(CONNECTOR_CHORD_M, 9),
                "curve_radius_m": round(CURVE_RADIUS_M, 9),
                "glb": str(glb_path),
                "lod_triangles": [
                    BASE.triangle_count(obj) for obj in lod_objects
                ],
                "manifest": str(manifest_path),
                "preview": str(preview_path),
                "turn_angle_degrees": TURN_ANGLE_DEGREES,
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
