#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the crowned-to-flat Penguinville corridor seam transition."""

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
    "rorng_penguin_road_seam_helpers",
    BASE_GENERATOR_PATH,
)
if BASE_SPEC is None or BASE_SPEC.loader is None:
    raise RuntimeError("cannot load the CityWorld authoring helpers")
BASE = importlib.util.module_from_spec(BASE_SPEC)
BASE_SPEC.loader.exec_module(BASE)

ASSET_ID = "rorng_city_penguin_road_seam_12m"
ASSET_VERSION = 2
GENERATOR_ID = "ror-cityworld-penguin-road-seam-generator-v2"
LENGTH_M = 12.0
HALF_LENGTH_M = LENGTH_M / 2.0

# These source-edge measurements are derived from the authenticated
# crossroadavenuesidewalk collision mesh. The replacement intersection is
# centred at world Z=370.023095 m, so the two road edges are symmetric here.
HALF_WIDTH_M = 4.875085
ROAD_WIDTH_M = HALF_WIDTH_M * 2.0
SOURCE_CROWN_X_M = -0.023093
SOURCE_CROWN_HEIGHT_M = 0.098013
SLAB_DEPTH_M = 0.12


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
            "rorng_penguin_seam_asphalt",
            (0.045, 0.052, 0.058, 1.0),
            metallic=0.0,
            roughness=0.94,
        ),
        "collision": BASE.make_material(
            "rorng_penguin_seam_collision_debug",
            (0.82, 0.03, 0.02, 1.0),
            metallic=0.0,
            roughness=1.0,
        ),
        "preview_ground": BASE.make_material(
            "rorng_preview_penguin_seam_ground",
            (0.07, 0.085, 0.095, 1.0),
            metallic=0.0,
            roughness=0.97,
        ),
    }


def mesh_object(
    name: str,
    vertices: list[tuple[float, float, float]],
    faces: list[tuple[int, ...]],
    material: bpy.types.Material,
    collection: bpy.types.Collection,
    *,
    role: str,
    lod: int | None,
) -> bpy.types.Object:
    expanded_vertices: list[tuple[float, float, float]] = []
    expanded_faces: list[tuple[int, ...]] = []
    for face in faces:
        first = len(expanded_vertices)
        expanded_vertices.extend(vertices[index] for index in face)
        expanded_faces.append(
            tuple(range(first, first + len(face)))
        )
    mesh = bpy.data.meshes.new(name + "_mesh")
    mesh.from_pydata(expanded_vertices, [], expanded_faces)
    mesh.materials.append(material)
    uv_layer = mesh.uv_layers.new(name="UVMap")
    mesh.validate(verbose=True)
    mesh.update(calc_edges=True)
    for polygon in mesh.polygons:
        normal = polygon.normal
        dominant_axis = max(
            range(3),
            key=lambda axis: abs(normal[axis]),
        )
        for loop_index in polygon.loop_indices:
            vertex = expanded_vertices[
                mesh.loops[loop_index].vertex_index
            ]
            if dominant_axis == 0:
                uv = (
                    (vertex[1] + HALF_LENGTH_M) / LENGTH_M,
                    (vertex[2] + SLAB_DEPTH_M)
                    / (SOURCE_CROWN_HEIGHT_M + SLAB_DEPTH_M),
                )
            elif dominant_axis == 1:
                uv = (
                    (vertex[0] + HALF_WIDTH_M) / ROAD_WIDTH_M,
                    (vertex[2] + SLAB_DEPTH_M)
                    / (SOURCE_CROWN_HEIGHT_M + SLAB_DEPTH_M),
                )
            else:
                if material.name == "rorng_penguin_seam_asphalt":
                    # Match ProceduralRoad::textureFit(TEXFIT_ROAD): the
                    # longitudinal coordinate repeats every 10 metres and
                    # the carriageway occupies the road2 atlas band
                    # v=0.072..0.423. Blender's V is inverted by glTF export,
                    # hence the authored 1-v below. End at u=0 so the first
                    # procedural segment begins at essentially the same
                    # repeat phase.
                    uv = (
                        (vertex[1] - HALF_LENGTH_M) / 10.0,
                        1.0
                        - (
                            0.072
                            + (vertex[0] + HALF_WIDTH_M)
                            / ROAD_WIDTH_M
                            * (0.423 - 0.072)
                        ),
                    )
                else:
                    uv = (
                        (vertex[0] + HALF_WIDTH_M) / ROAD_WIDTH_M,
                        (vertex[1] + HALF_LENGTH_M) / LENGTH_M,
                    )
            uv_layer.data[loop_index].uv = uv
    obj = bpy.data.objects.new(name, mesh)
    collection.objects.link(obj)
    obj["rorng_role"] = role
    if lod is not None:
        obj["rorng_lod"] = lod
    return obj


def road_prism(
    name: str,
    material: bpy.types.Material,
    collection: bpy.types.Collection,
    *,
    role: str,
    lod: int | None,
) -> bpy.types.Object:
    left = -HALF_WIDTH_M
    right = HALF_WIDTH_M
    start = -HALF_LENGTH_M
    end = HALF_LENGTH_M
    bottom = -SLAB_DEPTH_M
    vertices = [
        (left, start, 0.0),
        (SOURCE_CROWN_X_M, start, SOURCE_CROWN_HEIGHT_M),
        (right, start, 0.0),
        (left, end, 0.0),
        (SOURCE_CROWN_X_M, end, 0.0),
        (right, end, 0.0),
        (left, start, bottom),
        (right, start, bottom),
        (left, end, bottom),
        (right, end, bottom),
    ]
    faces = [
        (0, 1, 4, 3),
        (1, 2, 5, 4),
        (6, 8, 9, 7),
        (6, 0, 3, 8),
        (7, 9, 5, 2),
        (6, 7, 2, 1, 0),
        (9, 8, 3, 4, 5),
    ]
    return mesh_object(
        name,
        vertices,
        faces,
        material,
        collection,
        role=role,
        lod=lod,
    )


def build_render_lod(
    lod: int,
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
) -> bpy.types.Object:
    parts = [
        road_prism(
            f"lod{lod}_road_component",
            materials["asphalt"],
            collection,
            role="render-component",
            lod=lod,
        )
    ]
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
    collision = road_prism(
        "collision_road_component",
        material,
        collection,
        role="collision-component",
        lod=None,
    )
    objects = [
        BASE.join_components(
            [collision],
            name=f"{ASSET_ID}_collision_road",
            role="collision-road",
            lod=None,
        )
    ]
    for side in (-1.0, 1.0):
        label = "left" if side < 0.0 else "right"
        shoulder = BASE.make_box(
            f"collision_shoulder_{label}_component",
            dimensions=(0.1, LENGTH_M, SLAB_DEPTH_M),
            location=(
                side * (HALF_WIDTH_M + 0.1),
                0.0,
                -SLAB_DEPTH_M / 2.0,
            ),
            material=material,
            collection=collection,
        )
        objects.append(
            BASE.join_components(
                [shoulder],
                name=f"{ASSET_ID}_collision_shoulder_{label}",
                role=f"collision-shoulder-{label}",
                lod=None,
            )
        )
    return objects


def add_preview_scene(
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
    preview_path: Path,
) -> None:
    ground = BASE.make_box(
        "preview_ground",
        dimensions=(18.0, 25.0, 0.15),
        location=(0.0, 0.0, -0.22),
        material=materials["preview_ground"],
        collection=collection,
    )
    ground["rorng_role"] = "preview-only"
    bpy.ops.object.camera_add(location=(11.5, -15.0, 7.2))
    camera = bpy.context.object
    camera.name = "preview_camera"
    camera.data.lens = 56.0
    BASE.point_camera(camera, (0.0, 0.2, 0.0))
    BASE.move_to_collection(camera, collection)
    bpy.context.scene.camera = camera

    bpy.ops.object.light_add(type="SUN", location=(0.0, 0.0, 10.0))
    sun = bpy.context.object
    sun.name = "preview_sun"
    sun.rotation_euler = (
        math.radians(30.0),
        math.radians(-20.0),
        math.radians(-38.0),
    )
    sun.data.energy = 3.0
    sun.data.angle = math.radians(4.0)
    BASE.move_to_collection(sun, collection)
    bpy.ops.object.light_add(type="AREA", location=(-7.0, -6.0, 8.0))
    fill = bpy.context.object
    fill.name = "preview_fill"
    fill.data.energy = 1_500.0
    fill.data.shape = "DISK"
    fill.data.size = 7.0
    BASE.point_camera(fill, (0.0, 0.0, 0.0))
    BASE.move_to_collection(fill, collection)

    world = bpy.context.scene.world
    world.use_nodes = True
    background = world.node_tree.nodes.get("Background")
    background.inputs["Color"].default_value = (0.06, 0.075, 0.1, 1.0)
    background.inputs["Strength"].default_value = 0.48
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


def apply_runtime_preview_material(
    material: bpy.types.Material,
    texture_path: Path,
) -> None:
    """Preview the core road2 parent without exporting its texture to GLB."""
    if material.node_tree is None:
        raise RuntimeError("asphalt preview material has no node tree")
    principled = material.node_tree.nodes.get("Principled BSDF")
    if principled is None:
        raise RuntimeError("asphalt preview material has no Principled BSDF")
    image = bpy.data.images.load(str(texture_path), check_existing=True)
    image.pack()
    texture = material.node_tree.nodes.new("ShaderNodeTexImage")
    texture.name = "road2_runtime_parent_preview"
    texture.image = image
    material.node_tree.links.new(
        texture.outputs["Color"],
        principled.inputs["Base Color"],
    )


def main() -> int:
    args = parse_args()
    root = args.output_root.resolve()
    if not (root / ".git").exists():
        raise RuntimeError(f"--output-root is not a repository root: {root}")
    generator_path = Path(__file__).resolve()
    asset_root = (
        root
        / "resources/nextgen/cityworld/streetscape/penguin_road_seam_12m"
    )
    source_root = (
        root
        / "content-source/cityworld_next/streetscape/"
        "penguin_road_seam_12m"
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
    render_collection = BASE.make_collection("rorng_penguin_seam_render")
    collision_collection = BASE.make_collection("rorng_penguin_seam_collision")
    preview_collection = BASE.make_collection("rorng_penguin_seam_preview")
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
    apply_runtime_preview_material(
        materials["asphalt"],
        root / "resources/textures/road2.dds",
    )
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
            "cross_section": {
                "crown_height_m": SOURCE_CROWN_HEIGHT_M,
                "crown_x_m": SOURCE_CROWN_X_M,
                "edge_height_m": 0.0,
            },
            "forward": [0.0, -1.0, 0.0],
            "id": "start",
            "lane_centres_x_m": [-2.4375425, 2.4375425],
            "position_blender_z_up_m": [
                SOURCE_CROWN_X_M,
                -HALF_LENGTH_M,
                SOURCE_CROWN_HEIGHT_M,
            ],
            "road_width_m": ROAD_WIDTH_M,
        },
        {
            "cross_section": {
                "crown_height_m": 0.0,
                "edge_height_m": 0.0,
            },
            "forward": [0.0, 1.0, 0.0],
            "id": "end",
            "lane_centres_x_m": [-2.4375425, 2.4375425],
            "position_blender_z_up_m": [
                0.0,
                HALF_LENGTH_M,
                0.0,
            ],
            "road_width_m": ROAD_WIDTH_M,
        },
    ]
    manifest["geometry"].update(
        {
            "bridge_length_m": math.sqrt(
                LENGTH_M * LENGTH_M
                + SOURCE_CROWN_X_M * SOURCE_CROWN_X_M
                + SOURCE_CROWN_HEIGHT_M * SOURCE_CROWN_HEIGHT_M
            ),
            "bridge_width_m": ROAD_WIDTH_M + 0.3,
            "lod1_max_ratio": 1.0,
            "lod2_max_ratio": 1.0,
            "road_width_m": ROAD_WIDTH_M,
            "runtime_surface_material": "road2",
            "runtime_surface_uv_profile":
                "ror-procedural-road-road2-atlas-v1",
            "source_crown_fade_length_m": LENGTH_M,
            "source_crown_height_m": SOURCE_CROWN_HEIGHT_M,
        }
    )
    manifest["collision"]["road_surface_profile"] = (
        "authenticated-source-crown-to-flat-v1"
    )
    for material in manifest["materials"]:
        if material["name"] == "rorng_penguin_seam_asphalt":
            material["runtime_parent_material"] = "road2"
    manifest["runtime_material_dependencies"] = [
        {
            "material": "road2",
            "material_script_path": "resources/materials/ror.material",
            "material_script_sha256": BASE.sha256_file(
                root / "resources/materials/ror.material"
            ),
            "texture_path": "resources/textures/road2.dds",
            "texture_sha256": BASE.sha256_file(
                root / "resources/textures/road2.dds"
            ),
        }
    ]
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
