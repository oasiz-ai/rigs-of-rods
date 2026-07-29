#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate a 12 m CityWorld road-to-bridge transition and abutment."""

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
    "rorng_transition_bridge_helpers",
    BASE_GENERATOR_PATH,
)
if BASE_SPEC is None or BASE_SPEC.loader is None:
    raise RuntimeError("cannot load the bridge authoring helpers")
BASE = importlib.util.module_from_spec(BASE_SPEC)
BASE_SPEC.loader.exec_module(BASE)

ASSET_ID = "rorng_city_bridge_transition_12m"
ASSET_VERSION = 2
GENERATOR_ID = "ror-cityworld-bridge-transition-generator-v2"
LENGTH_M = 12.0
WIDTH_M = 10.0
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
    BASE.GENERATOR_ID = GENERATOR_ID
    BASE.reset_scene()


def make_materials() -> dict[str, bpy.types.Material]:
    return {
        "asphalt": BASE.make_material(
            "rorng_transition_asphalt",
            (0.034, 0.04, 0.047, 1.0),
            metallic=0.0,
            roughness=0.9,
        ),
        "collision": BASE.make_material(
            "rorng_transition_collision_debug",
            (0.8, 0.04, 0.03, 1.0),
            metallic=0.0,
            roughness=1.0,
        ),
        "concrete": BASE.make_material(
            "rorng_transition_concrete",
            (0.3, 0.33, 0.36, 1.0),
            metallic=0.0,
            roughness=0.8,
        ),
        "concrete_edge": BASE.make_material(
            "rorng_transition_concrete_edge",
            (0.39, 0.42, 0.44, 1.0),
            metallic=0.0,
            roughness=0.76,
        ),
        "concrete_weathered": BASE.make_material(
            "rorng_transition_concrete_weathered",
            (0.16, 0.185, 0.2, 1.0),
            metallic=0.0,
            roughness=0.95,
        ),
        "dark_steel": BASE.make_material(
            "rorng_transition_dark_steel",
            (0.035, 0.047, 0.058, 1.0),
            metallic=0.78,
            roughness=0.29,
        ),
        "lane_white": BASE.make_material(
            "rorng_transition_lane_white",
            (0.82, 0.84, 0.79, 1.0),
            metallic=0.0,
            roughness=0.66,
        ),
        "lane_yellow": BASE.make_material(
            "rorng_transition_lane_yellow",
            (0.93, 0.58, 0.035, 1.0),
            metallic=0.0,
            roughness=0.6,
        ),
        "reflector": BASE.make_material(
            "rorng_transition_reflector_amber",
            (1.0, 0.34, 0.025, 1.0),
            metallic=0.0,
            roughness=0.34,
        ),
        "rubber": BASE.make_material(
            "rorng_transition_joint_rubber",
            (0.008, 0.011, 0.014, 1.0),
            metallic=0.0,
            roughness=0.82,
        ),
        "preview_ground": BASE.make_material(
            "rorng_preview_ground",
            (0.055, 0.07, 0.075, 1.0),
            metallic=0.0,
            roughness=0.95,
        ),
        "steel": BASE.make_material(
            "rorng_transition_galvanized_steel",
            (0.23, 0.29, 0.34, 1.0),
            metallic=0.72,
            roughness=0.34,
        ),
    }


def box(
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


def rotated_box(
    parts: list[bpy.types.Object],
    name: str,
    dimensions: tuple[float, float, float],
    location: tuple[float, float, float],
    rotation_z: float,
    material: bpy.types.Material,
    collection: bpy.types.Collection,
    *,
    bevel: float = 0.0,
    bevel_segments: int = 1,
) -> None:
    obj = BASE.make_box(
        name,
        dimensions=dimensions,
        location=location,
        material=material,
        collection=collection,
        bevel=bevel,
        bevel_segments=bevel_segments,
    )
    obj.rotation_euler[2] = rotation_z
    BASE.apply_transform(obj)
    parts.append(obj)


def cylinder(
    parts: list[bpy.types.Object],
    name: str,
    radius: float,
    depth: float,
    location: tuple[float, float, float],
    rotation: tuple[float, float, float],
    vertices: int,
    material: bpy.types.Material,
    collection: bpy.types.Collection,
    *,
    bevel: float = 0.0,
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
            bevel=bevel,
        )
    )


def build_render_lod(
    lod: int,
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
) -> bpy.types.Object:
    prefix = f"lod{lod}"
    parts: list[bpy.types.Object] = []
    bevel = 0.04 if lod == 0 else 0.0
    box(
        parts,
        f"{prefix}_deck",
        (WIDTH_M, LENGTH_M, 0.68),
        (0.0, 0.0, -0.38),
        materials["concrete"],
        collection,
        bevel=bevel,
        bevel_segments=2 if lod == 0 else 1,
    )
    box(
        parts,
        f"{prefix}_asphalt",
        (9.2, LENGTH_M, 0.08),
        (0.0, 0.0, -0.04),
        materials["asphalt"],
        collection,
    )
    for side in (-1.0, 1.0):
        label = "left" if side < 0.0 else "right"
        box(
            parts,
            f"{prefix}_barrier_{label}",
            (0.36, LENGTH_M, 1.02 if lod == 0 else 0.9),
            (side * 4.72, 0.0, 0.49 if lod == 0 else 0.43),
            materials["concrete"],
            collection,
            bevel=0.035 if lod == 0 else 0.0,
            bevel_segments=2,
        )
        box(
            parts,
            f"{prefix}_barrier_cap_{label}",
            (0.47, LENGTH_M - 0.12, 0.1),
            (side * 4.72, 0.0, 0.96 if lod == 0 else 0.84),
            materials["concrete_edge"],
            collection,
            bevel=0.025 if lod == 0 else 0.0,
            bevel_segments=2,
        )
        if lod < 2:
            box(
                parts,
                f"{prefix}_guardrail_{label}",
                (0.1, LENGTH_M - 0.3, 0.14),
                (side * 4.5, 0.0, 0.76 if lod == 0 else 0.69),
                materials["steel"],
                collection,
                bevel=0.025 if lod == 0 else 0.0,
                bevel_segments=2,
            )

    if lod < 2:
        for side in (-1.0, 1.0):
            label = "left" if side < 0.0 else "right"
            box(
                parts,
                f"{prefix}_deck_fascia_{label}",
                (0.1, LENGTH_M - 0.18, 0.24),
                (side * 4.96, 0.0, -0.5),
                materials["concrete_edge"],
                collection,
                bevel=0.018 if lod == 0 else 0.0,
            )

    # The terrain-facing end is a complete abutment: backwall, bearing shelf,
    # flared wing walls, and retaining toes. These stay below road level and
    # do not interrupt the continuous collision slab.
    box(
        parts,
        f"{prefix}_backwall",
        (10.8, 0.7, 4.6),
        (0.0, -5.45, -2.58),
        materials["concrete"],
        collection,
        bevel=0.08 if lod == 0 else 0.0,
        bevel_segments=2,
    )
    box(
        parts,
        f"{prefix}_bearing_shelf",
        (9.4, 1.25, 0.42),
        (0.0, -4.95, -0.92),
        materials["concrete"],
        collection,
        bevel=0.06 if lod == 0 else 0.0,
        bevel_segments=2,
    )
    for side in (-1.0, 1.0):
        label = "left" if side < 0.0 else "right"
        wing_rotation = math.radians(side * 5.5)
        rotated_box(
            parts,
            f"{prefix}_wingwall_{label}",
            (0.62, 5.2, 3.4),
            (side * 5.18, -3.55, -2.0),
            wing_rotation,
            materials["concrete"],
            collection,
            bevel=0.07 if lod == 0 else 0.0,
            bevel_segments=2,
        )
        if lod < 2:
            rotated_box(
                parts,
                f"{prefix}_retaining_toe_{label}",
                (1.25, 4.8, 0.36),
                (side * 5.18, -3.5, -3.82),
                wing_rotation,
                materials["concrete"],
                collection,
                bevel=0.04 if lod == 0 else 0.0,
            )
        rotated_box(
            parts,
            f"{prefix}_wingwall_cap_{label}",
            (0.74, 5.12, 0.16),
            (side * 5.18, -3.55, -0.27),
            wing_rotation,
            materials["concrete_edge"],
            collection,
            bevel=0.035 if lod == 0 else 0.0,
        )

    box(
        parts,
        f"{prefix}_backwall_coping",
        (11.05, 0.82, 0.22),
        (0.0, -5.45, -0.32),
        materials["concrete_edge"],
        collection,
        bevel=0.045 if lod == 0 else 0.0,
        bevel_segments=2,
    )

    if lod < 2:
        for index, lateral in enumerate((-3.25, -1.1, 1.1, 3.25)):
            box(
                parts,
                f"{prefix}_bearing_{index}",
                (0.5, 0.62, 0.18),
                (lateral, -4.62, -0.72),
                materials["dark_steel"],
                collection,
                bevel=0.025 if lod == 0 else 0.0,
                bevel_segments=2,
            )
        for name, lateral, width, material in (
            ("edge_left", -3.95, 0.12, materials["lane_white"]),
            ("centre_left", -0.09, 0.1, materials["lane_yellow"]),
            ("centre_right", 0.09, 0.1, materials["lane_yellow"]),
            ("edge_right", 3.95, 0.12, materials["lane_white"]),
        ):
            box(
                parts,
                f"{prefix}_lane_{name}",
                (width, LENGTH_M - 0.4, 0.014),
                (lateral, -0.12, 0.011),
                material,
                collection,
            )
    if lod == 0:
        # The modular joint uses two anchored steel edge rails around a narrow
        # elastomer seal. Lane paint stops short so the joint reads cleanly.
        box(
            parts,
            f"{prefix}_expansion_joint_rubber",
            (9.15, 0.065, 0.026),
            (0.0, 5.81, 0.013),
            materials["rubber"],
            collection,
        )
        for rail_index, y_position in enumerate((5.735, 5.885)):
            box(
                parts,
                f"{prefix}_expansion_joint_rail_{rail_index}",
                (9.18, 0.075, 0.035),
                (0.0, y_position, 0.018),
                materials["dark_steel"],
                collection,
                bevel=0.008,
            )
            for bolt_index, lateral in enumerate(
                (-4.0, -3.0, -2.0, -1.0, 0.0, 1.0, 2.0, 3.0, 4.0)
            ):
                cylinder(
                    parts,
                    (
                        f"{prefix}_expansion_joint_bolt_"
                        f"{rail_index}_{bolt_index}"
                    ),
                    radius=0.028,
                    depth=0.024,
                    location=(lateral, y_position, 0.04),
                    rotation=(0.0, 0.0, 0.0),
                    vertices=8,
                    material=materials["steel"],
                    collection=collection,
                    bevel=0.004,
                )

        # Galvanized guardrail posts, base plates, fasteners, and reflectors
        # expose the anchoring rather than leaving a monolithic parapet.
        for side in (-1.0, 1.0):
            label = "left" if side < 0.0 else "right"
            for post_index, y_position in enumerate(
                (-5.0, -3.0, -1.0, 1.0, 3.0, 5.0)
            ):
                box(
                    parts,
                    f"{prefix}_guardrail_post_{label}_{post_index}",
                    (0.12, 0.16, 0.68),
                    (side * 4.5, y_position, 0.43),
                    materials["steel"],
                    collection,
                    bevel=0.018,
                    bevel_segments=2,
                )
                box(
                    parts,
                    f"{prefix}_guardrail_base_{label}_{post_index}",
                    (0.3, 0.34, 0.035),
                    (side * 4.49, y_position, 0.045),
                    materials["dark_steel"],
                    collection,
                    bevel=0.008,
                )
                for bolt_index, longitudinal in enumerate((-0.105, 0.105)):
                    cylinder(
                        parts,
                        (
                            f"{prefix}_guardrail_anchor_"
                            f"{label}_{post_index}_{bolt_index}"
                        ),
                        radius=0.026,
                        depth=0.035,
                        location=(
                            side * 4.37,
                            y_position + longitudinal,
                            0.074,
                        ),
                        rotation=(0.0, 0.0, 0.0),
                        vertices=8,
                        material=materials["dark_steel"],
                        collection=collection,
                        bevel=0.003,
                    )
                box(
                    parts,
                    f"{prefix}_reflector_{label}_{post_index}",
                    (0.025, 0.16, 0.085),
                    (side * 4.435, y_position, 0.78),
                    materials["reflector"],
                    collection,
                    bevel=0.008,
                )

        # Six framed drain grates lead to visible underside scuppers.
        for drain_index, y_position in enumerate((-3.6, 0.45, 4.35)):
            for side in (-1.0, 1.0):
                label = "left" if side < 0.0 else "right"
                drain_x = side * 4.23
                box(
                    parts,
                    f"{prefix}_drain_frame_{label}_{drain_index}",
                    (0.38, 0.68, 0.025),
                    (drain_x, y_position, 0.014),
                    materials["dark_steel"],
                    collection,
                    bevel=0.012,
                )
                for slat_index, offset in enumerate(
                    (-0.12, -0.06, 0.0, 0.06, 0.12)
                ):
                    box(
                        parts,
                        (
                            f"{prefix}_drain_slat_{label}_"
                            f"{drain_index}_{slat_index}"
                        ),
                        (0.025, 0.54, 0.012),
                        (drain_x + offset, y_position, 0.032),
                        materials["steel"],
                        collection,
                        bevel=0.004,
                    )
                cylinder(
                    parts,
                    f"{prefix}_scupper_{label}_{drain_index}",
                    radius=0.075,
                    depth=0.68,
                    location=(side * 4.67, y_position, -0.76),
                    rotation=(0.0, 0.0, 0.0),
                    vertices=12,
                    material=materials["dark_steel"],
                    collection=collection,
                    bevel=0.01,
                )

        # The abutment face gets construction joints, drainage outlets, and a
        # weathered splash band. All are surface detail, never collision.
        box(
            parts,
            f"{prefix}_backwall_weathering",
            (10.35, 0.035, 0.5),
            (0.0, -5.812, -4.25),
            materials["concrete_weathered"],
            collection,
        )
        for reveal_index, lateral in enumerate((-4.3, -2.15, 0.0, 2.15, 4.3)):
            box(
                parts,
                f"{prefix}_backwall_reveal_{reveal_index}",
                (0.055, 0.035, 3.55),
                (lateral, -5.815, -2.45),
                materials["concrete_weathered"],
                collection,
                bevel=0.008,
            )
        for outlet_index, lateral in enumerate((-3.2, 0.0, 3.2)):
            cylinder(
                parts,
                f"{prefix}_weep_outlet_{outlet_index}",
                radius=0.08,
                depth=0.06,
                location=(lateral, -5.84, -2.85),
                rotation=(math.radians(90.0), 0.0, 0.0),
                vertices=12,
                material=materials["rubber"],
                collection=collection,
                bevel=0.01,
            )

        # Bearing pads and anchor heads clarify the deck load path.
        for bearing_index, lateral in enumerate((-3.25, -1.1, 1.1, 3.25)):
            box(
                parts,
                f"{prefix}_bearing_pad_{bearing_index}",
                (0.56, 0.66, 0.055),
                (lateral, -4.62, -0.825),
                materials["rubber"],
                collection,
                bevel=0.012,
            )
            for bolt_index, offset in enumerate((-0.17, 0.17)):
                cylinder(
                    parts,
                    f"{prefix}_bearing_anchor_{bearing_index}_{bolt_index}",
                    radius=0.03,
                    depth=0.035,
                    location=(lateral + offset, -4.42, -0.61),
                    rotation=(0.0, 0.0, 0.0),
                    vertices=8,
                    material=materials["dark_steel"],
                    collection=collection,
                    bevel=0.004,
                )

        box(
            parts,
            f"{prefix}_abutment_crossbeam",
            (8.65, 0.3, 0.38),
            (0.0, 4.82, -0.94),
            materials["dark_steel"],
            collection,
            bevel=0.035,
            bevel_segments=2,
        )
        for conduit_index, lateral in enumerate((-2.65, 2.65)):
            cylinder(
                parts,
                f"{prefix}_service_conduit_{conduit_index}",
                radius=0.075,
                depth=10.4,
                location=(lateral, 0.25, -1.34),
                rotation=(math.radians(90.0), 0.0, 0.0),
                vertices=12,
                material=materials["dark_steel"],
                collection=collection,
                bevel=0.01,
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
    barriers = []
    for side in (-1.0, 1.0):
        label = "left" if side < 0.0 else "right"
        barrier = BASE.make_box(
            f"collision_barrier_{label}_component",
            dimensions=(0.3, LENGTH_M, 1.04),
            location=(side * 4.64, 0.0, 0.54),
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
        dimensions=(40.0, 44.0, 0.25),
        location=(0.0, 0.0, -4.15),
        material=materials["preview_ground"],
        collection=collection,
        bevel=0.08,
        bevel_segments=2,
    )
    ground["rorng_role"] = "preview-only"
    bpy.ops.object.camera_add(location=(18.5, -22.5, 10.5))
    camera = bpy.context.object
    camera.name = "preview_camera"
    camera.data.lens = 50.0
    BASE.point_camera(camera, (0.0, -0.35, -1.25))
    BASE.move_to_collection(camera, collection)
    bpy.context.scene.camera = camera

    bpy.ops.object.light_add(type="SUN", location=(0.0, 0.0, 12.0))
    sun = bpy.context.object
    sun.name = "preview_sun"
    sun.rotation_euler = (
        math.radians(32.0),
        math.radians(-24.0),
        math.radians(-36.0),
    )
    sun.data.energy = 3.5
    sun.data.angle = math.radians(4.0)
    BASE.move_to_collection(sun, collection)
    bpy.ops.object.light_add(type="AREA", location=(-9.0, -10.0, 11.0))
    fill = bpy.context.object
    fill.name = "preview_fill"
    fill.data.energy = 2_200.0
    fill.data.shape = "DISK"
    fill.data.size = 8.5
    BASE.point_camera(fill, (0.0, -1.0, -1.5))
    BASE.move_to_collection(fill, collection)
    bpy.ops.object.light_add(type="AREA", location=(8.0, 5.0, 7.5))
    rim = bpy.context.object
    rim.name = "preview_rim"
    rim.data.energy = 1_350.0
    rim.data.shape = "RECTANGLE"
    rim.data.size = 6.0
    rim.data.size_y = 6.0
    BASE.point_camera(rim, (0.0, 0.5, -0.8))
    BASE.move_to_collection(rim, collection)

    world = bpy.context.scene.world
    world.use_nodes = True
    background = world.node_tree.nodes.get("Background")
    background.inputs["Color"].default_value = (0.065, 0.085, 0.12, 1.0)
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
    scene.view_settings.exposure = 0.55


def main() -> int:
    args = parse_args()
    root = args.output_root.resolve()
    if not (root / ".git").exists():
        raise RuntimeError(f"--output-root is not a repository root: {root}")
    generator_path = Path(__file__).resolve()
    asset_root = (
        root / "resources/nextgen/cityworld/bridge/transition_12m"
    )
    source_root = (
        root / "content-source/cityworld_next/bridge/transition_12m"
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
    render_collection = BASE.make_collection("rorng_bridge_transition_render")
    collision_collection = BASE.make_collection("rorng_bridge_transition_collision")
    preview_collection = BASE.make_collection("rorng_bridge_transition_preview")
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
    manifest["authoring"]["generator"]["dependencies"] = [
        {
            "path": BASE_GENERATOR_PATH.relative_to(root).as_posix(),
            "sha256": BASE.sha256_file(BASE_GENERATOR_PATH),
        },
    ]
    manifest["connectors"] = [
        {
            "forward": [0.0, -1.0, 0.0],
            "id": "start",
            "lane_centres_x_m": [-1.75, 1.75],
            "position_blender_z_up_m": [0.0, -6.0, 0.0],
            "road_width_m": ROAD_WIDTH_M,
        },
        {
            "forward": [0.0, 1.0, 0.0],
            "id": "end",
            "lane_centres_x_m": [-1.75, 1.75],
            "position_blender_z_up_m": [0.0, 6.0, 0.0],
            "road_width_m": ROAD_WIDTH_M,
        },
    ]
    manifest["geometry"].update(
        {
            "bridge_length_m": LENGTH_M,
            "bridge_width_m": WIDTH_M,
            "road_width_m": ROAD_WIDTH_M,
        }
    )
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
