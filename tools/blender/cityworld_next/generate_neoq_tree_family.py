#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the original NeoQueretaro urban broadleaf tree family.

Run with Blender 5.2:

    blender --background --factory-startup --python \
      tools/blender/cityworld_next/generate_neoq_tree_family.py -- \
      --output-root /path/to/rigs-of-rods

The three variants are entirely procedural and project-authored.  They use
opaque geometry foliage because the current bounded CityWorld compiler does
not yet lower alpha-tested textures or impostor atlases.  Wind and future
impostor requirements are retained as deterministic metadata.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import sys
from typing import Any

import bpy
from mathutils import Vector


SCRIPT_DIRECTORY = Path(__file__).resolve().parent
BASE_GENERATOR_PATH = SCRIPT_DIRECTORY / "generate_bridge_kit.py"
CANONICALIZER_PATH = SCRIPT_DIRECTORY / "canonicalize_static_glb.py"
BASE_SPEC = importlib.util.spec_from_file_location(
    "rorng_neoq_tree_helpers",
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
if CANONICALIZER_SPEC is None or CANONICALIZER_SPEC.loader is None:
    raise RuntimeError("cannot load the static GLB canonicalizer")
CANONICALIZER = importlib.util.module_from_spec(CANONICALIZER_SPEC)
CANONICALIZER_SPEC.loader.exec_module(CANONICALIZER)

ASSET_VERSION = 1
ASSET_PROFILE = "static-fixture-v1"
FAMILY_FORMAT = "ror-cityworld-tree-family-v1"
GENERATOR_ID = "ror-cityworld-neoq-tree-family-generator-v1"
WIND_FORMAT = "ror-cityworld-vegetation-wind-v1"
IMPOSTOR_FORMAT = "ror-cityworld-vegetation-impostor-contract-v1"
FAMILY_ID = "rorng_city_neoq_tree_family"
SELECTOR_NAMESPACE = "cityworld:neoqueretaro:arbol1qr:v1"
SOURCE_PLACEMENT_COUNT = 18

VARIANTS: tuple[dict[str, Any], ...] = (
    {
        "id": "rorng_city_neoq_tree_round",
        "label": "round",
        "seed": 0x42A53111,
        "height_m": 9.4,
        "crown_radius_m": (3.45, 3.2, 3.25),
        "crown_center_m": (0.0, 0.0, 6.25),
        "lean_m": (0.16, -0.08),
        "trunk_radius_m": 0.47,
    },
    {
        "id": "rorng_city_neoq_tree_columnar",
        "label": "columnar",
        "seed": 0xC17A2E09,
        "height_m": 10.6,
        "crown_radius_m": (2.35, 2.25, 4.05),
        "crown_center_m": (-0.08, 0.02, 6.55),
        "lean_m": (-0.1, 0.12),
        "trunk_radius_m": 0.43,
    },
    {
        "id": "rorng_city_neoq_tree_windswept",
        "label": "windswept",
        "seed": 0x91F45BD3,
        "height_m": 8.9,
        "crown_radius_m": (3.85, 2.85, 2.95),
        "crown_center_m": (0.72, 0.08, 5.95),
        "lean_m": (0.48, 0.06),
        "trunk_radius_m": 0.5,
    },
)


class StableRng:
    """Tiny fixed-width generator whose sequence is independent of Python."""

    def __init__(self, seed: int):
        self.state = seed & 0xFFFFFFFF

    def unit(self) -> float:
        value = self.state
        value ^= (value << 13) & 0xFFFFFFFF
        value ^= value >> 17
        value ^= (value << 5) & 0xFFFFFFFF
        self.state = value & 0xFFFFFFFF
        return self.state / 0xFFFFFFFF

    def signed(self) -> float:
        return self.unit() * 2.0 - 1.0


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


def reset_scene_fully(asset_id: str) -> None:
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
    bpy.context.scene["rorng_asset_profile"] = ASSET_PROFILE


def make_materials(asset_id: str) -> dict[str, bpy.types.Material]:
    return {
        "bark": BASE.make_material(
            f"{asset_id}_bark",
            (0.17, 0.095, 0.045, 1.0),
            metallic=0.0,
            roughness=0.91,
        ),
        "collision": BASE.make_material(
            f"{asset_id}_collision_debug",
            (0.82, 0.03, 0.025, 1.0),
            metallic=0.0,
            roughness=1.0,
        ),
        "foliage_dark": BASE.make_material(
            f"{asset_id}_foliage_dark",
            (0.045, 0.19, 0.052, 1.0),
            metallic=0.0,
            roughness=0.83,
        ),
        "foliage_light": BASE.make_material(
            f"{asset_id}_foliage_light",
            (0.11, 0.32, 0.075, 1.0),
            metallic=0.0,
            roughness=0.78,
        ),
        "preview_ground": BASE.make_material(
            "rorng_preview_tree_ground",
            (0.075, 0.09, 0.065, 1.0),
            metallic=0.0,
            roughness=0.97,
        ),
        "preview_path": BASE.make_material(
            "rorng_preview_tree_path",
            (0.12, 0.115, 0.1, 1.0),
            metallic=0.0,
            roughness=0.92,
        ),
    }


def smooth_mesh(obj: bpy.types.Object) -> None:
    for polygon in obj.data.polygons:
        polygon.use_smooth = True


def make_tapered_segment(
    *,
    name: str,
    start: Vector,
    end: Vector,
    radius_start: float,
    radius_end: float,
    vertices: int,
    material: bpy.types.Material,
    collection: bpy.types.Collection,
) -> bpy.types.Object:
    direction = end - start
    length = direction.length
    if length <= 1e-6:
        raise RuntimeError(f"zero-length tree segment: {name}")
    bpy.ops.mesh.primitive_cone_add(
        vertices=vertices,
        radius1=radius_start,
        radius2=radius_end,
        depth=length,
        end_fill_type="NGON",
        location=(start + end) * 0.5,
    )
    obj = bpy.context.object
    obj.name = name
    obj.rotation_euler = direction.to_track_quat("Z", "Y").to_euler()
    BASE.apply_transform(obj)
    smooth_mesh(obj)
    BASE.assign_material(obj, material)
    BASE.move_to_collection(obj, collection)
    return obj


def make_curved_trunk(
    *,
    name: str,
    spec: dict[str, Any],
    vertices_per_ring: int,
    material: bpy.types.Material,
    collection: bpy.types.Collection,
) -> bpy.types.Object:
    trunk_radius = float(spec["trunk_radius_m"])
    lean = Vector((*spec["lean_m"], 0.0))
    levels = (0.0, 0.72, 1.42, 2.12, 2.78, 3.42, 4.05)
    radii = (1.0, 0.92, 0.8, 0.67, 0.52, 0.39, 0.27)
    seed_phase = (int(spec["seed"]) & 0xFFFF) * math.tau / 65536.0
    centers: list[Vector] = []
    for index, level in enumerate(levels):
        progress = level / levels[-1]
        kink = math.sin(seed_phase + index * 1.37) * 0.055 * progress
        centers.append(
            Vector(
                (
                    lean.x * progress**1.35 + kink,
                    lean.y * progress**1.35
                    + math.cos(seed_phase + index * 1.11)
                    * 0.04
                    * progress,
                    level,
                )
            )
        )

    vertices: list[tuple[float, float, float]] = []
    for center, radius_scale in zip(centers, radii):
        radius = trunk_radius * radius_scale
        for segment in range(vertices_per_ring):
            angle = math.tau * segment / vertices_per_ring
            vertices.append(
                (
                    center.x + math.cos(angle) * radius,
                    center.y + math.sin(angle) * radius,
                    center.z,
                )
            )
    faces: list[tuple[int, ...]] = []
    for ring in range(len(centers) - 1):
        current = ring * vertices_per_ring
        following = (ring + 1) * vertices_per_ring
        for segment in range(vertices_per_ring):
            next_segment = (segment + 1) % vertices_per_ring
            faces.append(
                (
                    current + segment,
                    current + next_segment,
                    following + next_segment,
                    following + segment,
                )
            )
    faces.append(tuple(reversed(range(vertices_per_ring))))
    top_start = (len(centers) - 1) * vertices_per_ring
    faces.append(
        tuple(top_start + segment for segment in range(vertices_per_ring))
    )

    mesh = bpy.data.meshes.new(f"{name}_mesh")
    mesh.from_pydata(vertices, [], faces)
    mesh.update(calc_edges=True)
    obj = bpy.data.objects.new(name, mesh)
    collection.objects.link(obj)
    BASE.assign_material(obj, material)
    uv_layer = mesh.uv_layers.new(name="UVMap")
    for polygon in mesh.polygons:
        for loop_index in polygon.loop_indices:
            vertex_index = mesh.loops[loop_index].vertex_index
            ring = min(vertex_index // vertices_per_ring, len(centers) - 1)
            segment = vertex_index % vertices_per_ring
            uv_layer.data[loop_index].uv = (
                segment / vertices_per_ring,
                ring / (len(centers) - 1),
            )
        polygon.use_smooth = polygon.index < len(faces) - 2
    BASE.apply_transform(obj)
    return obj


def make_canopy_blob(
    *,
    name: str,
    location: Vector,
    radii: Vector,
    segments: int,
    rings: int,
    rotation_z: float,
    material: bpy.types.Material,
    collection: bpy.types.Collection,
) -> bpy.types.Object:
    bpy.ops.mesh.primitive_uv_sphere_add(
        segments=segments,
        ring_count=rings,
        radius=1.0,
        location=location,
        rotation=(0.0, 0.0, rotation_z),
    )
    obj = bpy.context.object
    obj.name = name
    obj.scale = radii
    BASE.apply_transform(obj)
    smooth_mesh(obj)
    BASE.assign_material(obj, material)
    BASE.move_to_collection(obj, collection)
    if not obj.data.uv_layers:
        raise RuntimeError(f"tree canopy has no authored UVs: {name}")
    return obj


def crown_sample(
    rng: StableRng,
    spec: dict[str, Any],
    *,
    lod: int,
    index: int,
    count: int,
) -> tuple[Vector, Vector]:
    radius = Vector(spec["crown_radius_m"])
    center = Vector(spec["crown_center_m"])
    # A low-discrepancy angular progression gives stable crown coverage while
    # the fixed generator perturbs it enough to avoid repeated rows.
    phase = index * 2.399963229728653 + rng.signed() * 0.22
    layer = ((index * 7) % count + 0.5) / count
    radial = math.sqrt(min(1.0, 0.1 + 1.12 * layer))
    vertical = rng.signed() * 0.74
    if spec["label"] == "columnar":
        radial *= 0.72 + 0.18 * rng.unit()
    elif spec["label"] == "windswept":
        radial *= 0.82 + 0.26 * rng.unit()
    position = center + Vector(
        (
            math.cos(phase) * radius.x * radial,
            math.sin(phase) * radius.y * radial,
            vertical * radius.z * 0.62,
        )
    )
    if spec["label"] == "windswept":
        position.x += 0.55 * (vertical + 0.35)
    edge_scale = 0.72 + 0.28 * (1.0 - min(radial, 1.0))
    base_blob = (0.48, 0.82, 1.22)[lod]
    radii = Vector(
        (
            base_blob * edge_scale * (0.88 + 0.3 * rng.unit()),
            base_blob * edge_scale * (0.68 + 0.28 * rng.unit()),
            base_blob * edge_scale * (0.52 + 0.24 * rng.unit()),
        )
    )
    return position, radii


def build_render_lod(
    *,
    spec: dict[str, Any],
    lod: int,
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
) -> bpy.types.Object:
    asset_id = spec["id"]
    rng = StableRng(int(spec["seed"]) ^ ((lod + 1) * 0x9E3779B9))
    parts: list[bpy.types.Object] = []
    trunk_vertices = (20, 12, 8)[lod]
    branch_vertices = (10, 7, 5)[lod]
    trunk_radius = float(spec["trunk_radius_m"])
    lean = Vector((*spec["lean_m"], 0.0))

    if lod == 0:
        parts.append(
            make_curved_trunk(
                name="lod0_trunk",
                spec=spec,
                vertices_per_ring=trunk_vertices,
                material=materials["bark"],
                collection=collection,
            )
        )
    else:
        levels = (0.0, 3.85 if lod == 1 else 3.55)
        radii = (trunk_radius, trunk_radius * (0.29 if lod == 1 else 0.24))
        trunk_points = [
            Vector(
                (
                    lean.x * (level / max(levels[-1], 0.1)) ** 1.35,
                    lean.y * (level / max(levels[-1], 0.1)) ** 1.35,
                    level,
                )
            )
            for level in levels
        ]
        parts.append(
            make_tapered_segment(
                name=f"lod{lod}_trunk",
                start=trunk_points[0],
                end=trunk_points[1],
                radius_start=radii[0],
                radius_end=radii[1],
                vertices=trunk_vertices,
                material=materials["bark"],
                collection=collection,
            )
        )

    if lod == 0:
        for index in range(7):
            angle = math.tau * index / 7.0 + 0.29
            start = Vector(
                (
                    math.cos(angle) * trunk_radius * 0.3,
                    math.sin(angle) * trunk_radius * 0.3,
                    0.58,
                )
            )
            end = Vector(
                (
                    math.cos(angle) * trunk_radius * 1.85,
                    math.sin(angle) * trunk_radius * 1.85,
                    0.06,
                )
            )
            parts.append(
                make_tapered_segment(
                    name=f"lod0_root_flare_{index:02d}",
                    start=start,
                    end=end,
                    radius_start=trunk_radius * 0.19,
                    radius_end=trunk_radius * 0.055,
                    vertices=8,
                    material=materials["bark"],
                    collection=collection,
                )
            )

    branch_count = (22, 6, 0)[lod]
    crown_center = Vector(spec["crown_center_m"])
    crown_radius = Vector(spec["crown_radius_m"])
    for index in range(branch_count):
        angle = index * 2.399963229728653 + rng.signed() * 0.2
        start_z = 2.55 + (index % 5) * 0.28 + rng.unit() * 0.16
        start = Vector(
            (
                lean.x * (start_z / 4.1) ** 1.3,
                lean.y * (start_z / 4.1) ** 1.3,
                start_z,
            )
        )
        length_factor = 0.55 + rng.unit() * 0.29
        end = Vector(
            (
                crown_center.x
                + math.cos(angle) * crown_radius.x * length_factor,
                crown_center.y
                + math.sin(angle) * crown_radius.y * length_factor,
                crown_center.z
                + rng.signed() * crown_radius.z * 0.39
                - (0.18 if index % 3 == 0 else 0.0),
            )
        )
        if spec["label"] == "windswept":
            end.x += 0.45
        parts.append(
            make_tapered_segment(
                name=f"lod{lod}_branch_{index:02d}",
                start=start,
                end=end,
                radius_start=trunk_radius * (0.15 if lod == 0 else 0.12),
                radius_end=trunk_radius * 0.035,
                vertices=branch_vertices,
                material=materials["bark"],
                collection=collection,
            )
        )

    blob_count = (138, 18, 5)[lod]
    segments = (12, 10, 8)[lod]
    rings = (7, 6, 4)[lod]
    for index in range(blob_count):
        position, blob_radii = crown_sample(
            rng,
            spec,
            lod=lod,
            index=index,
            count=blob_count,
        )
        material = (
            materials["foliage_light"]
            if (index * 5 + int(spec["seed"])) % 7 in (0, 1)
            else materials["foliage_dark"]
        )
        parts.append(
            make_canopy_blob(
                name=f"lod{lod}_canopy_{index:02d}",
                location=position,
                radii=blob_radii,
                segments=segments,
                rings=rings,
                rotation_z=rng.unit() * math.tau,
                material=material,
                collection=collection,
            )
        )

    obj = BASE.join_components(
        parts,
        name=f"{asset_id}_lod{lod}",
        role="render",
        lod=lod,
    )
    obj["rorng_family_id"] = FAMILY_ID
    obj["rorng_silhouette_variant"] = spec["label"]
    obj["rorng_wind_format"] = WIND_FORMAT
    obj["rorng_wind_anchor_height_m"] = 0.65
    obj["rorng_wind_canopy_start_height_m"] = 2.5
    obj["rorng_wind_max_tip_displacement_m"] = 0.48
    obj["rorng_wind_phase_source"] = "instance-hash"
    obj["rorng_impostor_format"] = IMPOSTOR_FORMAT
    obj["rorng_impostor_status"] = "contract-only"
    return obj


def build_collision(
    *,
    spec: dict[str, Any],
    collection: bpy.types.Collection,
    material: bpy.types.Material,
) -> list[bpy.types.Object]:
    asset_id = spec["id"]
    radius = min(0.56, float(spec["trunk_radius_m"]) * 1.08)
    height = 3.45
    proxy = BASE.make_cylinder(
        "collision_trunk_component",
        radius=radius,
        depth=height,
        location=(0.0, 0.0, height * 0.5),
        rotation=(0.0, 0.0, 0.0),
        vertices=12,
        material=material,
        collection=collection,
    )
    collision = BASE.join_components(
        [proxy],
        name=f"{asset_id}_collision_fixture",
        role="collision-fixture",
        lod=None,
    )
    collision["rorng_collision_profile"] = "trunk-only"
    return [collision]


def add_preview_scene(
    *,
    spec: dict[str, Any],
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
    preview_path: Path,
) -> None:
    ground = BASE.make_box(
        "preview_tree_ground",
        dimensions=(19.0, 17.0, 0.18),
        location=(0.0, 0.0, -0.09),
        material=materials["preview_ground"],
        collection=collection,
        bevel=0.08,
        bevel_segments=2,
    )
    ground["rorng_role"] = "preview-only"
    path = BASE.make_box(
        "preview_tree_path",
        dimensions=(4.2, 17.0, 0.04),
        location=(-5.3, 0.0, 0.015),
        material=materials["preview_path"],
        collection=collection,
        bevel=0.025,
        bevel_segments=2,
    )
    path["rorng_role"] = "preview-only"

    bpy.ops.object.camera_add(location=(14.8, -17.8, 8.6))
    camera = bpy.context.object
    camera.name = "preview_camera"
    camera.data.lens = 54.0
    BASE.point_camera(camera, (0.0, 0.0, float(spec["height_m"]) * 0.47))
    BASE.move_to_collection(camera, collection)
    bpy.context.scene.camera = camera

    bpy.ops.object.light_add(type="SUN", location=(0.0, 0.0, 13.0))
    sun = bpy.context.object
    sun.name = "preview_sun"
    sun.rotation_euler = (
        math.radians(31.0),
        math.radians(-19.0),
        math.radians(-37.0),
    )
    sun.data.energy = 2.85
    sun.data.angle = math.radians(4.5)
    BASE.move_to_collection(sun, collection)

    bpy.ops.object.light_add(type="AREA", location=(-7.5, -6.0, 9.5))
    fill = bpy.context.object
    fill.name = "preview_fill"
    fill.data.energy = 1_050.0
    fill.data.shape = "DISK"
    fill.data.size = 6.5
    BASE.point_camera(fill, (0.0, 0.0, float(spec["height_m"]) * 0.52))
    BASE.move_to_collection(fill, collection)

    world = bpy.context.scene.world
    world.use_nodes = True
    background = world.node_tree.nodes.get("Background")
    background.inputs["Color"].default_value = (0.09, 0.14, 0.21, 1.0)
    background.inputs["Strength"].default_value = 0.36

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
    scene.view_settings.exposure = 0.25


def asset_paths(root: Path, asset_id: str) -> dict[str, Path]:
    variant_root = root / "resources/nextgen/cityworld/vegetation" / asset_id
    source_root = root / "content-source/cityworld_next/vegetation" / asset_id
    return {
        "blend": source_root / f"{asset_id}.blend",
        "glb": variant_root / f"{asset_id}.glb",
        "manifest": variant_root / f"{asset_id}.asset.json",
        "preview": source_root / f"{asset_id}_preview.png",
    }


def generate_variant(root: Path, spec: dict[str, Any]) -> dict[str, Any]:
    asset_id = str(spec["id"])
    paths = asset_paths(root, asset_id)
    for path in paths.values():
        path.parent.mkdir(parents=True, exist_ok=True)
    generator_path = Path(__file__).resolve()
    generator_hash = BASE.sha256_file(generator_path)
    dependency_records = [
        {
            "path": dependency_path.relative_to(root).as_posix(),
            "sha256": BASE.sha256_file(dependency_path),
        }
        for dependency_path in (
            BASE_GENERATOR_PATH,
            CANONICALIZER_PATH,
        )
    ]
    previous_glb_hash = (
        BASE.sha256_file(paths["glb"])
        if paths["glb"].is_file() and not paths["glb"].is_symlink()
        else None
    )
    previous_generator_hash: str | None = None
    previous_blender_version: str | None = None
    previous_dependencies: list[dict[str, str]] | None = None
    if paths["manifest"].is_file() and not paths["manifest"].is_symlink():
        try:
            previous_manifest = json.loads(
                paths["manifest"].read_text(encoding="utf-8")
            )
            previous_authoring = previous_manifest.get("authoring", {})
            previous_generator = previous_authoring.get("generator", {})
            previous_generator_hash = previous_generator.get("sha256")
            previous_blender_version = previous_authoring.get(
                "blender_version"
            )
            previous_dependencies = previous_generator.get("dependencies")
        except (OSError, UnicodeDecodeError, json.JSONDecodeError):
            previous_generator_hash = None
    candidate_blend = paths["blend"].with_name(
        f".{asset_id}.candidate.blend"
    )
    candidate_preview = paths["preview"].with_name(
        f".{asset_id}.candidate.png"
    )
    candidate_blend.unlink(missing_ok=True)
    candidate_preview.unlink(missing_ok=True)

    reset_scene_fully(asset_id)
    render_collection = BASE.make_collection(f"{asset_id}_render")
    collision_collection = BASE.make_collection(f"{asset_id}_collision")
    preview_collection = BASE.make_collection(f"{asset_id}_preview")
    materials = make_materials(asset_id)
    lod_objects = [
        build_render_lod(
            spec=spec,
            lod=lod,
            collection=render_collection,
            materials=materials,
        )
        for lod in range(3)
    ]
    collision_objects = build_collision(
        spec=spec,
        collection=collision_collection,
        material=materials["collision"],
    )
    asset_objects = [*lod_objects, *collision_objects]

    bpy.ops.object.select_all(action="DESELECT")
    for obj in asset_objects:
        obj.hide_set(False)
        obj.select_set(True)
    bpy.context.view_layer.objects.active = lod_objects[0]
    bpy.ops.export_scene.gltf(
        filepath=str(paths["glb"]),
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
    CANONICALIZER.canonicalize_glb_geometry(paths["glb"])

    for obj in [lod_objects[1], lod_objects[2], *collision_objects]:
        obj.hide_render = True
        obj.hide_set(True)
    lod_objects[0].hide_render = False
    lod_objects[0].hide_set(False)
    add_preview_scene(
        spec=spec,
        collection=preview_collection,
        materials=materials,
        preview_path=candidate_preview,
    )
    bpy.ops.wm.save_as_mainfile(filepath=str(candidate_blend), compress=False)
    bpy.ops.render.render(write_still=True)
    preserve_checked_source = (
        previous_glb_hash == BASE.sha256_file(paths["glb"])
        and previous_generator_hash == generator_hash
        and previous_blender_version == bpy.app.version_string
        and previous_dependencies == dependency_records
        and paths["blend"].is_file()
        and not paths["blend"].is_symlink()
        and paths["preview"].is_file()
        and not paths["preview"].is_symlink()
    )
    if preserve_checked_source:
        candidate_blend.unlink()
        candidate_preview.unlink()
    else:
        os.replace(candidate_blend, paths["blend"])
        os.replace(candidate_preview, paths["preview"])

    manifest = BASE.make_manifest(
        root=root,
        generator_path=generator_path,
        blend_path=paths["blend"],
        glb_path=paths["glb"],
        preview_path=paths["preview"],
        lod_objects=lod_objects,
        collision_objects=collision_objects,
        materials=materials,
    )
    manifest["asset"]["profile"] = ASSET_PROFILE
    manifest["authoring"]["generator"]["format"] = GENERATOR_ID
    manifest["authoring"]["generator"]["dependencies"] = dependency_records
    manifest["authoring"]["procedural_provenance"] = {
        "external_geometry": False,
        "external_textures": False,
        "method": "deterministic-project-authored-blender-python",
        "rights_basis": "GPL-3.0-or-later project-authored source",
    }
    manifest["collision"].pop("road_surface_z_m", None)
    manifest["collision"]["profile"] = "single-watertight-proxy-v1"
    manifest["collision"]["purpose"] = "trunk-only-vehicle-contact"
    manifest["connectors"] = []
    lod_entries = manifest["geometry"]["lods"]
    crown_radius = list(spec["crown_radius_m"])
    manifest["geometry"] = {
        "fixture_height_m": float(spec["height_m"]),
        "footprint_diameter_m": round(max(crown_radius[:2]) * 2.0, 3),
        "lod0_triangle_ceiling": 35_000,
        "lod1_max_ratio": 0.4,
        "lod2_max_ratio": 0.12,
        "lods": lod_entries,
    }
    manifest["vegetation"] = {
        "family": FAMILY_ID,
        "foliage": {
            "alpha_mode": "opaque-geometry",
            "mip_safe": True,
            "texture_dependencies": [],
        },
        "impostor": {
            "compiler_emits": False,
            "format": IMPOSTOR_FORMAT,
            "status": "contract-only",
        },
        "silhouette_variant": spec["label"],
        "wind": {
            "anchor_height_m": 0.65,
            "canopy_start_height_m": 2.5,
            "format": WIND_FORMAT,
            "max_tip_displacement_m": 0.48,
            "node_extras": True,
            "phase_source": "instance-hash",
            "runtime_consumes": False,
        },
    }
    paths["manifest"].write_text(
        json.dumps(manifest, indent=2, ensure_ascii=True, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return {
        "asset_id": asset_id,
        "lod_triangles": [BASE.triangle_count(obj) for obj in lod_objects],
        "manifest": paths["manifest"].relative_to(root).as_posix(),
        "silhouette": spec["label"],
    }


def placement_assignment(ordinal: int) -> dict[str, Any]:
    payload = f"{SELECTOR_NAMESPACE}:{ordinal}".encode("ascii")
    digest = hashlib.sha256(payload).digest()
    variant = VARIANTS[int.from_bytes(digest[:8], "little") % len(VARIANTS)]
    yaw_degrees = round(
        int.from_bytes(digest[8:10], "little") * 360.0 / 65536.0,
        3,
    )
    scale = round(
        0.94 + int.from_bytes(digest[10:12], "little") * 0.12 / 65535.0,
        5,
    )
    return {
        "digest_sha256": digest.hex(),
        "placement_ordinal": ordinal,
        "scale": scale,
        "variant": variant["id"],
        "yaw_degrees": yaw_degrees,
    }


def write_family_contract(
    root: Path,
    generated: list[dict[str, Any]],
) -> Path:
    path = (
        root
        / "content-source/cityworld_next/vegetation/"
        "rorng_city_neoq_tree_family.v1.json"
    )
    path.parent.mkdir(parents=True, exist_ok=True)
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
                "format": GENERATOR_ID,
                "path": Path(__file__).resolve().relative_to(root).as_posix(),
                "sha256": BASE.sha256_file(Path(__file__).resolve()),
            },
            "procedural_provenance": {
                "external_geometry": False,
                "external_textures": False,
                "method": "deterministic-project-authored-blender-python",
                "rights_basis": "GPL-3.0-or-later project-authored source",
            },
        },
        "format": FAMILY_FORMAT,
        "impostor": {
            "alpha": {
                "coverage_preserving_mips": True,
                "edge_dilation_px": 8,
                "mode": "mask",
            },
            "atlas_resolution_px": [4096, 4096],
            "azimuth_degrees": [0, 45, 90, 135, 180, 225, 270, 315],
            "compiler_emits": False,
            "elevation_degrees": [0, 20, 40],
            "format": IMPOSTOR_FORMAT,
            "frame_count": 24,
            "pivot": "trunk-ground-centre",
            "status": "contract-only",
        },
        "placement_target": {
            "integration_status": "asset-ready-placement-deferred",
            "legacy_object": "arbol1Qr",
            "map": "CityWorld/NeoQueretaro",
            "placement_count": SOURCE_PLACEMENT_COUNT,
        },
        "selector": {
            "algorithm": "sha256-little-endian-modulo-v1",
            "assignments": [
                placement_assignment(ordinal)
                for ordinal in range(SOURCE_PLACEMENT_COUNT)
            ],
            "namespace": SELECTOR_NAMESPACE,
        },
        "variants": [
            {
                "asset_id": entry["asset_id"],
                "manifest": entry["manifest"],
                "silhouette": entry["silhouette"],
                "weight": 1,
            }
            for entry in generated
        ],
        "wind": {
            "anchor_height_m": 0.65,
            "canopy_start_height_m": 2.5,
            "format": WIND_FORMAT,
            "max_tip_displacement_m": 0.48,
            "node_extras": [
                "rorng_wind_format",
                "rorng_wind_anchor_height_m",
                "rorng_wind_canopy_start_height_m",
                "rorng_wind_max_tip_displacement_m",
                "rorng_wind_phase_source",
            ],
            "phase_source": "instance-hash",
            "runtime_consumes": False,
        },
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
    generated = [generate_variant(root, spec) for spec in VARIANTS]
    family_path = write_family_contract(root, generated)
    print(
        json.dumps(
            {
                "family": FAMILY_ID,
                "family_manifest": family_path.relative_to(root).as_posix(),
                "placements": SOURCE_PLACEMENT_COUNT,
                "variants": generated,
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
