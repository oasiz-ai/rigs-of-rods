#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the original CityWorld Next modular LED streetlight fixture."""

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
CANONICALIZER_PATH = SCRIPT_DIRECTORY / "canonicalize_static_glb.py"
BASE_SPEC = importlib.util.spec_from_file_location(
    "rorng_led_streetlight_helpers",
    BASE_GENERATOR_PATH,
)
if BASE_SPEC is None or BASE_SPEC.loader is None:
    raise RuntimeError("cannot load the CityWorld authoring helpers")
BASE = importlib.util.module_from_spec(BASE_SPEC)
BASE_SPEC.loader.exec_module(BASE)
CANONICALIZER_SPEC = importlib.util.spec_from_file_location(
    "rorng_static_glb_canonicalizer",
    CANONICALIZER_PATH,
)
if (
    CANONICALIZER_SPEC is None
    or CANONICALIZER_SPEC.loader is None
):
    raise RuntimeError("cannot load the static GLB canonicalizer")
CANONICALIZER = importlib.util.module_from_spec(CANONICALIZER_SPEC)
CANONICALIZER_SPEC.loader.exec_module(CANONICALIZER)

ASSET_ID = "rorng_city_led_streetlight"
ASSET_VERSION = 1
ASSET_PROFILE = "static-fixture-v1"
GENERATOR_ID = "ror-cityworld-led-streetlight-generator-v1"
FIXTURE_HEIGHT_M = 7.58
FOOTPRINT_DIAMETER_M = 0.76


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
    bpy.context.scene["rorng_asset_profile"] = ASSET_PROFILE


def make_materials() -> dict[str, bpy.types.Material]:
    return {
        "collision": BASE.make_material(
            "rorng_fixture_collision_debug",
            (0.8, 0.04, 0.03, 1.0),
            metallic=0.0,
            roughness=1.0,
        ),
        "concrete": BASE.make_material(
            "rorng_fixture_precast_concrete",
            (0.28, 0.3, 0.31, 1.0),
            metallic=0.0,
            roughness=0.9,
        ),
        "gasket": BASE.make_material(
            "rorng_fixture_lens_gasket",
            (0.008, 0.011, 0.014, 1.0),
            metallic=0.0,
            roughness=0.82,
        ),
        "galvanized": BASE.make_material(
            "rorng_fixture_galvanized_steel",
            (0.24, 0.28, 0.31, 1.0),
            metallic=0.72,
            roughness=0.36,
        ),
        "lens": BASE.make_material(
            "rorng_fixture_led_lens_emissive",
            (1.0, 0.74, 0.34, 1.0),
            metallic=0.0,
            roughness=0.22,
            emission=(1.0, 0.72, 0.3),
            emission_strength=1.0,
        ),
        "powdercoat": BASE.make_material(
            "rorng_fixture_powdercoat_graphite",
            (0.035, 0.045, 0.055, 1.0),
            metallic=0.82,
            roughness=0.28,
        ),
        "preview_ground": BASE.make_material(
            "rorng_preview_fixture_ground",
            (0.075, 0.085, 0.09, 1.0),
            metallic=0.0,
            roughness=0.94,
        ),
        "preview_road": BASE.make_material(
            "rorng_preview_fixture_road",
            (0.018, 0.024, 0.032, 1.0),
            metallic=0.0,
            roughness=0.92,
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


def tapered_pole(
    parts: list[bpy.types.Object],
    name: str,
    radius_bottom: float,
    radius_top: float,
    depth: float,
    location: tuple[float, float, float],
    vertices: int,
    material: bpy.types.Material,
    collection: bpy.types.Collection,
) -> None:
    bpy.ops.mesh.primitive_cone_add(
        vertices=vertices,
        radius1=radius_bottom,
        radius2=radius_top,
        depth=depth,
        end_fill_type="NGON",
        location=location,
    )
    obj = bpy.context.object
    obj.name = name
    BASE.apply_transform(obj)
    BASE.assign_material(obj, material)
    BASE.move_to_collection(obj, collection)
    parts.append(obj)


def build_render_lod(
    lod: int,
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
) -> bpy.types.Object:
    prefix = f"lod{lod}"
    parts: list[bpy.types.Object] = []
    pole_vertices = (32, 14, 8)[lod]
    base_vertices = (32, 16, 8)[lod]
    bevel = 0.025 if lod == 0 else 0.0

    cylinder(
        parts,
        f"{prefix}_foundation",
        radius=0.38,
        depth=0.16,
        location=(0.0, 0.0, 0.08),
        rotation=(0.0, 0.0, 0.0),
        vertices=base_vertices,
        material=materials["concrete"],
        collection=collection,
        bevel=0.018 if lod == 0 else 0.0,
    )
    cylinder(
        parts,
        f"{prefix}_base_flange",
        radius=0.31 if lod < 2 else 0.26,
        depth=0.055,
        location=(0.0, 0.0, 0.19),
        rotation=(0.0, 0.0, 0.0),
        vertices=base_vertices,
        material=materials["galvanized"],
        collection=collection,
        bevel=0.012 if lod == 0 else 0.0,
    )
    tapered_pole(
        parts,
        f"{prefix}_pole",
        radius_bottom=0.15 if lod < 2 else 0.13,
        radius_top=0.075 if lod < 2 else 0.07,
        depth=6.86,
        location=(0.0, 0.0, 3.65),
        vertices=pole_vertices,
        material=materials["powdercoat"],
        collection=collection,
    )

    if lod < 2:
        cylinder(
            parts,
            f"{prefix}_base_collar",
            radius=0.2,
            depth=0.48,
            location=(0.0, 0.0, 0.43),
            rotation=(0.0, 0.0, 0.0),
            vertices=pole_vertices,
            material=materials["powdercoat"],
            collection=collection,
            bevel=0.018 if lod == 0 else 0.0,
        )
        cylinder(
            parts,
            f"{prefix}_arm",
            radius=0.075,
            depth=1.58,
            location=(0.0, 0.72, 7.16),
            rotation=(math.radians(90.0), 0.0, 0.0),
            vertices=pole_vertices,
            material=materials["powdercoat"],
            collection=collection,
            bevel=0.015 if lod == 0 else 0.0,
        )
        cylinder(
            parts,
            f"{prefix}_arm_joint",
            radius=0.135,
            depth=0.2,
            location=(0.0, 0.0, 7.08),
            rotation=(0.0, 0.0, 0.0),
            vertices=pole_vertices,
            material=materials["galvanized"],
            collection=collection,
            bevel=0.016 if lod == 0 else 0.0,
        )
    else:
        box(
            parts,
            f"{prefix}_arm",
            (0.14, 1.52, 0.14),
            (0.0, 0.7, 7.15),
            materials["powdercoat"],
            collection,
        )

    housing_bevel = 0.055 if lod == 0 else 0.025 if lod == 1 else 0.0
    box(
        parts,
        f"{prefix}_luminaire_body",
        (0.46 if lod < 2 else 0.4, 1.08, 0.17),
        (0.0, 1.55, 7.24),
        materials["powdercoat"],
        collection,
        bevel=housing_bevel,
        bevel_segments=3 if lod == 0 else 1,
    )
    box(
        parts,
        f"{prefix}_lens_gasket",
        (0.36 if lod < 2 else 0.32, 0.86, 0.035),
        (0.0, 1.57, 7.145),
        materials["gasket"],
        collection,
        bevel=0.025 if lod == 0 else 0.0,
        bevel_segments=2,
    )
    box(
        parts,
        f"{prefix}_led_lens",
        (0.31 if lod < 2 else 0.28, 0.78, 0.024),
        (0.0, 1.58, 7.12),
        materials["lens"],
        collection,
        bevel=0.02 if lod == 0 else 0.0,
        bevel_segments=2,
    )

    if lod == 0:
        for bolt_index, (x_position, y_position) in enumerate(
            (
                (-0.19, -0.19),
                (-0.19, 0.19),
                (0.19, -0.19),
                (0.19, 0.19),
            )
        ):
            cylinder(
                parts,
                f"{prefix}_anchor_bolt_{bolt_index}",
                radius=0.033,
                depth=0.075,
                location=(x_position, y_position, 0.24),
                rotation=(0.0, 0.0, 0.0),
                vertices=10,
                material=materials["galvanized"],
                collection=collection,
                bevel=0.005,
            )
        box(
            parts,
            f"{prefix}_access_panel",
            (0.14, 0.025, 0.42),
            (0.0, -0.157, 1.25),
            materials["galvanized"],
            collection,
            bevel=0.018,
            bevel_segments=2,
        )
        for hinge_index, z_position in enumerate((1.1, 1.4)):
            box(
                parts,
                f"{prefix}_access_hinge_{hinge_index}",
                (0.026, 0.018, 0.055),
                (-0.058, -0.174, z_position),
                materials["gasket"],
                collection,
                bevel=0.004,
            )
        box(
            parts,
            f"{prefix}_housing_upper_shell",
            (0.4, 0.96, 0.075),
            (0.0, 1.5, 7.34),
            materials["galvanized"],
            collection,
            bevel=0.035,
            bevel_segments=2,
        )
        for fin_index, y_position in enumerate(
            (1.18, 1.29, 1.4, 1.51, 1.62, 1.73, 1.84)
        ):
            box(
                parts,
                f"{prefix}_heat_fin_{fin_index}",
                (0.31, 0.03, 0.04),
                (0.0, y_position, 7.405),
                materials["powdercoat"],
                collection,
                bevel=0.006,
            )
        box(
            parts,
            f"{prefix}_rear_service_cap",
            (0.39, 0.075, 0.12),
            (0.0, 1.02, 7.25),
            materials["gasket"],
            collection,
            bevel=0.018,
            bevel_segments=2,
        )
        box(
            parts,
            f"{prefix}_front_bezel",
            (0.39, 0.07, 0.12),
            (0.0, 2.08, 7.23),
            materials["galvanized"],
            collection,
            bevel=0.022,
            bevel_segments=2,
        )
        cylinder(
            parts,
            f"{prefix}_photocell",
            radius=0.045,
            depth=0.065,
            location=(0.0, 1.16, 7.44),
            rotation=(0.0, 0.0, 0.0),
            vertices=16,
            material=materials["gasket"],
            collection=collection,
            bevel=0.008,
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
    proxy = BASE.make_cylinder(
        "collision_fixture_component",
        radius=0.34,
        depth=7.2,
        location=(0.0, 0.0, 3.6),
        rotation=(0.0, 0.0, 0.0),
        vertices=12,
        material=material,
        collection=collection,
    )
    return [
        BASE.join_components(
            [proxy],
            name=f"{ASSET_ID}_collision_fixture",
            role="collision-fixture",
            lod=None,
        )
    ]


def add_preview_scene(
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
    preview_path: Path,
) -> None:
    ground = BASE.make_box(
        "preview_fixture_sidewalk",
        dimensions=(11.0, 12.0, 0.18),
        location=(0.0, 0.0, -0.09),
        material=materials["preview_ground"],
        collection=collection,
        bevel=0.06,
        bevel_segments=2,
    )
    ground["rorng_role"] = "preview-only"
    road = BASE.make_box(
        "preview_fixture_road",
        dimensions=(11.0, 5.5, 0.1),
        location=(0.0, 4.3, -0.04),
        material=materials["preview_road"],
        collection=collection,
        bevel=0.03,
        bevel_segments=2,
    )
    road["rorng_role"] = "preview-only"

    bpy.ops.object.camera_add(location=(14.5, -17.5, 8.0))
    camera = bpy.context.object
    camera.name = "preview_camera"
    camera.data.lens = 50.0
    BASE.point_camera(camera, (0.0, 0.65, 3.75))
    BASE.move_to_collection(camera, collection)
    bpy.context.scene.camera = camera

    bpy.ops.object.light_add(type="SUN", location=(0.0, 0.0, 11.0))
    sun = bpy.context.object
    sun.name = "preview_sun"
    sun.rotation_euler = (
        math.radians(38.0),
        math.radians(-24.0),
        math.radians(-34.0),
    )
    sun.data.energy = 2.4
    sun.data.angle = math.radians(4.0)
    BASE.move_to_collection(sun, collection)

    bpy.ops.object.light_add(type="AREA", location=(-5.0, -4.0, 7.0))
    fill = bpy.context.object
    fill.name = "preview_fill"
    fill.data.energy = 1_150.0
    fill.data.shape = "DISK"
    fill.data.size = 5.0
    BASE.point_camera(fill, (0.0, 0.5, 3.7))
    BASE.move_to_collection(fill, collection)

    bpy.ops.object.light_add(type="AREA", location=(4.0, 5.0, 8.5))
    rim = bpy.context.object
    rim.name = "preview_rim"
    rim.data.energy = 950.0
    rim.data.shape = "RECTANGLE"
    rim.data.size = 4.0
    rim.data.size_y = 4.0
    BASE.point_camera(rim, (0.0, 1.0, 4.4))
    BASE.move_to_collection(rim, collection)

    world = bpy.context.scene.world
    world.use_nodes = True
    background = world.node_tree.nodes.get("Background")
    background.inputs["Color"].default_value = (0.018, 0.032, 0.065, 1.0)
    background.inputs["Strength"].default_value = 0.3

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
    scene.view_settings.exposure = 0.35


def main() -> int:
    args = parse_args()
    root = args.output_root.resolve()
    if not (root / ".git").exists():
        raise RuntimeError(f"--output-root is not a repository root: {root}")

    generator_path = Path(__file__).resolve()
    asset_root = root / "resources/nextgen/cityworld/fixtures/led_streetlight"
    source_root = (
        root / "content-source/cityworld_next/fixtures/led_streetlight"
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
    render_collection = BASE.make_collection("rorng_led_streetlight_render")
    collision_collection = BASE.make_collection(
        "rorng_led_streetlight_collision"
    )
    preview_collection = BASE.make_collection("rorng_led_streetlight_preview")
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
    CANONICALIZER.canonicalize_glb_geometry(glb_path)

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
    manifest["asset"]["profile"] = ASSET_PROFILE
    manifest["authoring"]["generator"]["format"] = GENERATOR_ID
    manifest["authoring"]["generator"]["dependencies"] = [
        {
            "path": dependency_path.relative_to(root).as_posix(),
            "sha256": BASE.sha256_file(dependency_path),
        }
        for dependency_path in (
            BASE_GENERATOR_PATH,
            CANONICALIZER_PATH,
        )
    ]
    manifest["authoring"]["procedural_provenance"] = {
        "external_geometry": False,
        "external_textures": False,
        "method": "deterministic-project-authored-blender-python",
        "rights_basis": "GPL-3.0-or-later project-authored source",
    }
    manifest["collision"].pop("road_surface_z_m", None)
    manifest["collision"]["profile"] = "single-watertight-proxy-v1"
    manifest["connectors"] = []
    lod_entries = manifest["geometry"]["lods"]
    manifest["geometry"] = {
        "fixture_height_m": FIXTURE_HEIGHT_M,
        "footprint_diameter_m": FOOTPRINT_DIAMETER_M,
        "lod0_triangle_ceiling": 12_000,
        "lod1_max_ratio": 0.35,
        "lod2_max_ratio": 0.1,
        "lods": lod_entries,
    }
    for material in manifest["materials"]:
        if material["name"] == "rorng_fixture_led_lens_emissive":
            material["emissive_factor_linear"] = [1.0, 0.72, 0.3]

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
