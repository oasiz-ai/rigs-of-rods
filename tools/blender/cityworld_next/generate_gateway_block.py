#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate a detailed 40 m CityWorld gateway streetscape module."""

from __future__ import annotations

import argparse
import importlib.util
import json
import math
from pathlib import Path
import sys

import bpy


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
ASSET_VERSION = 1
GENERATOR_ID = "ror-cityworld-gateway-block-generator-v1"
LENGTH_M = 40.0
WIDTH_M = 34.0
ROAD_WIDTH_M = 8.9
ROAD_SURFACE_Z_M = 0.0


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
) -> None:
    parts.append(
        BASE.make_cylinder(
            name,
            radius=radius,
            depth=depth,
            location=location,
            rotation=(0.0, 0.0, 0.0),
            vertices=vertices,
            material=material,
            collection=collection,
            bevel=0.0,
        )
    )


def add_tree(
    parts: list[bpy.types.Object],
    *,
    prefix: str,
    x: float,
    y: float,
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
    detail: int,
) -> None:
    add_cylinder(
        parts,
        f"{prefix}_trunk",
        radius=0.22 if detail == 0 else 0.2,
        depth=3.4 if detail == 0 else 3.0,
        location=(x, y, 1.7 if detail == 0 else 1.5),
        vertices=12 if detail == 0 else 8,
        material=materials["bark"],
        collection=collection,
    )
    crown_layers = (
        ((1.5, 1.8, 3.4), (1.85, 1.65, 4.65), (1.35, 1.4, 5.75))
        if detail == 0
        else ((1.45, 1.7, 3.5), (1.25, 1.45, 4.75))
    )
    for index, (radius, depth, z) in enumerate(crown_layers):
        add_cylinder(
            parts,
            f"{prefix}_crown_{index}",
            radius=radius,
            depth=depth,
            location=(x, y, z),
            vertices=12 if detail == 0 else 8,
            material=(
                materials["leaf_dark"]
                if index % 2 == 0
                else materials["leaf_light"]
            ),
            collection=collection,
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
        return

    facade_x = x - side * (depth_x / 2.0 + 0.025)
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
    add_box(
        parts,
        f"{prefix}_front_ground_glass",
        (depth_x * 0.46, 0.065, 1.9),
        (x, y - width_y / 2.0 - 0.025, 1.3),
        materials["glass"],
        collection,
        bevel=0.025,
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
    add_box(
        parts,
        f"{prefix}_roof_trim",
        (depth_x + 0.18, width_y + 0.18, 0.32),
        (x, y, height + 0.16),
        materials["stone"],
        collection,
        bevel=0.035,
    )
    for unit in (-1.0, 1.0):
        add_box(
            parts,
            f"{prefix}_hvac_{'a' if unit < 0 else 'b'}",
            (1.6, 1.35, 0.8),
            (x, y + unit * width_y * 0.22, height + 0.72),
            materials["metal"],
            collection,
            bevel=0.04,
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

    bpy.ops.object.camera_add(location=(0.0, -65.0, 18.0))
    camera = bpy.context.object
    camera.name = "preview_camera"
    camera.data.lens = 50.0
    BASE.point_camera(camera, (0.0, 0.0, 7.0))
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

    world = bpy.context.scene.world
    world.use_nodes = True
    background = world.node_tree.nodes.get("Background")
    background.inputs["Color"].default_value = (0.055, 0.08, 0.13, 1.0)
    background.inputs["Strength"].default_value = 0.42
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
