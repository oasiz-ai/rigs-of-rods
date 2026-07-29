#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the rights-cleared CityWorld modular storefront family.

Run with the pinned Blender 5.2 LTS authoring version:

    blender --background --factory-startup --python \
      tools/blender/cityworld_next/generate_cityworld_storefront_family.py -- \
      --output-root /path/to/rigs-of-rods

Only project-authored procedural geometry and factor materials are emitted.
The audited legacy dimensions are compatibility facts; no source mesh,
material, texture, or collision geometry is imported by this generator.
"""

from __future__ import annotations

import argparse
import hashlib
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
RETENTION_CONTRACT_PATH = SCRIPT_DIRECTORY / "artifact_retention_contract.py"
BASE_SPEC = importlib.util.spec_from_file_location(
    "rorng_storefront_helpers",
    BASE_GENERATOR_PATH,
)
if BASE_SPEC is None or BASE_SPEC.loader is None:
    raise RuntimeError("cannot load the CityWorld authoring helpers")
BASE = importlib.util.module_from_spec(BASE_SPEC)
BASE_SPEC.loader.exec_module(BASE)
CANONICALIZER_SPEC = importlib.util.spec_from_file_location(
    "rorng_storefront_glb_canonicalizer",
    CANONICALIZER_PATH,
)
if CANONICALIZER_SPEC is None or CANONICALIZER_SPEC.loader is None:
    raise RuntimeError("cannot load the static GLB canonicalizer")
CANONICALIZER = importlib.util.module_from_spec(CANONICALIZER_SPEC)
CANONICALIZER_SPEC.loader.exec_module(CANONICALIZER)
RETENTION_SPEC = importlib.util.spec_from_file_location(
    "rorng_storefront_artifact_retention",
    RETENTION_CONTRACT_PATH,
)
if RETENTION_SPEC is None or RETENTION_SPEC.loader is None:
    raise RuntimeError("cannot load the storefront artifact retention contract")
RETENTION = importlib.util.module_from_spec(RETENTION_SPEC)
RETENTION_SPEC.loader.exec_module(RETENTION)

AUTHORING_DEPENDENCY_PATHS = (
    BASE_GENERATOR_PATH,
    CANONICALIZER_PATH,
    RETENTION_CONTRACT_PATH,
)

ASSET_VERSION = 1
ASSET_PROFILE = "static-building-v1"
FAMILY_FORMAT = "ror-cityworld-storefront-family-v1"
GENERATOR_ID = "ror-cityworld-storefront-family-generator-v1"
FAMILY_ID = "rorng_city_storefront_family"
SELECTOR_NAMESPACE = "cityworld:penguinville:storefronts:v1"
SOURCE_ARCHIVE_SHA256 = (
    "ebeac2f0204f25ca1955f29ca1583b2afa4517a3a848feb1db203814acac2ef3"
)

# Counts and bounds were audited read-only from the pinned compatibility
# archive.  They are not used to derive any authored mesh topology.
VARIANTS: tuple[dict[str, Any], ...] = (
    {
        "asset_id": "rorng_city_storefront_corner_20x20",
        "legacy_object": "store02",
        "label": "contemporary-corner",
        "placement_count": 6,
        "footprint_m": (20.0, 20.0),
        "legacy_bounds_m": {
            "min": (-10.0, -10.0, -1.0),
            "max": (10.0, 10.0, 12.7238),
        },
        "body_height_m": 10.65,
        "height_limit_m": 12.7238,
        "stories": 3,
        "style": "contemporary",
        "source_yaws": (0.0, 0.0, 180.0, 0.0, -180.0, -90.0),
        "render_sha256": (
            "1bf020b9786206ae46b98d04066dda05bf85fcc0863f514b4fb303e39a2abe1f"
        ),
        "collision_sha256": (
            "1d189b589170ba24962f580f728a901e3dc6774cec7cd6ddff6149fbcb330a58"
        ),
    },
    {
        "asset_id": "rorng_city_storefront_heritage_20x30",
        "legacy_object": "store03",
        "label": "heritage-mixed-use",
        "placement_count": 9,
        "footprint_m": (20.0, 30.0),
        "legacy_bounds_m": {
            "min": (-10.0, -15.0, -1.0),
            "max": (10.0, 15.0, 16.5552),
        },
        "body_height_m": 14.25,
        "height_limit_m": 16.5552,
        "stories": 4,
        "style": "heritage",
        "source_yaws": (90.0, -90.0, 0.0, 0.0, 0.0, 0.0, 0.0, -90.0, -90.0),
        "render_sha256": (
            "b0ccde2c053607cb0e1f7c78ef55461bf2fdda0706f5662ab220d6d27beaff99"
        ),
        "collision_sha256": (
            "36ea0e53be1909afa1031917f0e5893873aca92f3cf95e4befe119f387756f68"
        ),
    },
    {
        "asset_id": "rorng_city_storefront_market_20x50",
        "legacy_object": "store05",
        "label": "market-hall",
        "placement_count": 9,
        "footprint_m": (20.0, 50.0),
        "legacy_bounds_m": {
            "min": (-10.0, -25.0, -1.0),
            "max": (10.0, 25.0, 20.5984),
        },
        "body_height_m": 18.1,
        "height_limit_m": 20.5984,
        "stories": 5,
        "style": "market",
        "source_yaws": (-180.0, 90.0, 180.0, 90.0, 90.0, -90.0, 0.0, 180.0, -90.0),
        "render_sha256": (
            "66e5d94aceebbeea2ef658eaeb0ae764560dbe877ab13618998f1eaf3a130d3d"
        ),
        "collision_sha256": (
            "ead083e47534df7f9d1db32c0be7f608882ec7dad96f319314ce879709491e4e"
        ),
    },
    {
        "asset_id": "rorng_city_storefront_arcade_30x10",
        "legacy_object": "store06",
        "label": "industrial-arcade",
        "placement_count": 9,
        "footprint_m": (30.0, 10.0),
        "legacy_bounds_m": {
            "min": (-15.0, -5.0, -1.0),
            "max": (15.0, 5.0, 12.3121),
        },
        "body_height_m": 10.35,
        "height_limit_m": 12.3121,
        "stories": 3,
        "style": "industrial",
        "source_yaws": (-90.0, -180.0, -90.0, 0.0, -180.0, 0.0, -180.0, -90.0, -180.0),
        "render_sha256": (
            "6f1fe827a62f49db82a699ea01af9401c6ee65d24ef4d1a9c578cd0674530b26"
        ),
        "collision_sha256": (
            "5eb686c44552a1450705c4b4acfe67a23a5e3b027ad68ddd5987fc92b740f7f2"
        ),
    },
    {
        "asset_id": "rorng_city_storefront_gabled_20x10",
        "legacy_object": "store08",
        "label": "gabled-townhouse",
        "placement_count": 7,
        "footprint_m": (20.0, 10.0),
        "legacy_bounds_m": {
            "min": (-10.0, -5.05501, -1.0),
            "max": (10.0, 5.0, 16.9956),
        },
        "body_height_m": 13.0,
        "height_limit_m": 16.9956,
        "stories": 4,
        "style": "gabled",
        "source_yaws": (-90.0, -90.0, -90.0, -180.0, -180.0, -90.0, 0.0),
        "render_sha256": (
            "04c0f4d00c22e6cb033db72e14f26ef10c97527e219d8dd402b34bdcca8947a0"
        ),
        "collision_sha256": (
            "7ef51b2da959f3ca90ab97a5fa7373f38692253ec6a96974aea152e6f7549dec"
        ),
    },
)

PALETTES: dict[str, dict[str, tuple[float, ...]]] = {
    "contemporary": {
        "facade": (0.46, 0.49, 0.48, 1.0),
        "accent": (0.075, 0.17, 0.21, 1.0),
        "trim": (0.68, 0.66, 0.59, 1.0),
        "sign": (0.06, 0.24, 0.31, 1.0),
    },
    "heritage": {
        "facade": (0.38, 0.115, 0.055, 1.0),
        "accent": (0.54, 0.24, 0.095, 1.0),
        "trim": (0.58, 0.52, 0.41, 1.0),
        "sign": (0.24, 0.055, 0.025, 1.0),
    },
    "market": {
        "facade": (0.26, 0.285, 0.29, 1.0),
        "accent": (0.52, 0.19, 0.055, 1.0),
        "trim": (0.63, 0.59, 0.47, 1.0),
        "sign": (0.62, 0.22, 0.045, 1.0),
    },
    "industrial": {
        "facade": (0.29, 0.075, 0.04, 1.0),
        "accent": (0.055, 0.065, 0.07, 1.0),
        "trim": (0.31, 0.34, 0.34, 1.0),
        "sign": (0.065, 0.07, 0.075, 1.0),
    },
    "gabled": {
        "facade": (0.33, 0.095, 0.055, 1.0),
        "accent": (0.11, 0.15, 0.17, 1.0),
        "trim": (0.65, 0.60, 0.48, 1.0),
        "sign": (0.10, 0.22, 0.19, 1.0),
    },
}


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
    """Zero UV accessors for this explicitly texture-free asset profile."""

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
    scene = bpy.context.scene
    scene["rorng_asset_profile"] = ASSET_PROFILE


def make_materials(
    asset_id: str,
    style: str,
) -> dict[str, bpy.types.Material]:
    palette = PALETTES[style]
    return {
        "collision": BASE.make_material(
            f"{asset_id}_collision_debug",
            (0.82, 0.03, 0.025, 1.0),
            metallic=0.0,
            roughness=1.0,
        ),
        "concrete": BASE.make_material(
            f"{asset_id}_grounded_concrete",
            (0.32, 0.33, 0.31, 1.0),
            metallic=0.0,
            roughness=0.86,
        ),
        "facade": BASE.make_material(
            f"{asset_id}_facade",
            palette["facade"],
            metallic=0.0,
            roughness=0.78,
        ),
        "accent": BASE.make_material(
            f"{asset_id}_facade_accent",
            palette["accent"],
            metallic=0.0,
            roughness=0.68,
        ),
        "trim": BASE.make_material(
            f"{asset_id}_architectural_trim",
            palette["trim"],
            metallic=0.0,
            roughness=0.59,
        ),
        "glass": BASE.make_material(
            f"{asset_id}_glass",
            (0.035, 0.11, 0.16, 1.0),
            metallic=0.16,
            roughness=0.16,
        ),
        "glass_lit": BASE.make_material(
            f"{asset_id}_glass_warm_interior",
            (0.42, 0.22, 0.075, 1.0),
            metallic=0.0,
            roughness=0.3,
            emission=(0.95, 0.46, 0.12),
            emission_strength=0.72,
        ),
        "metal": BASE.make_material(
            f"{asset_id}_powdercoat_metal",
            (0.025, 0.035, 0.045, 1.0),
            metallic=0.68,
            roughness=0.3,
        ),
        "roof": BASE.make_material(
            f"{asset_id}_roof",
            (0.065, 0.075, 0.08, 1.0),
            metallic=0.38,
            roughness=0.52,
        ),
        "sign": BASE.make_material(
            f"{asset_id}_signage",
            palette["sign"],
            metallic=0.12,
            roughness=0.38,
        ),
        "preview_ground": BASE.make_material(
            "rorng_preview_storefront_ground",
            (0.075, 0.085, 0.09, 1.0),
            metallic=0.0,
            roughness=0.94,
        ),
        "preview_road": BASE.make_material(
            "rorng_preview_storefront_road",
            (0.028, 0.034, 0.04, 1.0),
            metallic=0.0,
            roughness=0.91,
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
    segments: int = 1,
) -> bpy.types.Object:
    obj = BASE.make_box(
        name,
        dimensions=dimensions,
        location=location,
        material=material,
        collection=collection,
        bevel=bevel,
        bevel_segments=segments,
    )
    parts.append(obj)
    return obj


def add_cylinder(
    parts: list[bpy.types.Object],
    name: str,
    *,
    radius: float,
    depth: float,
    location: tuple[float, float, float],
    material: bpy.types.Material,
    collection: bpy.types.Collection,
    vertices: int,
) -> bpy.types.Object:
    obj = BASE.make_cylinder(
        name,
        radius=radius,
        depth=depth,
        location=location,
        rotation=(0.0, 0.0, 0.0),
        vertices=vertices,
        material=material,
        collection=collection,
        bevel=min(0.04, radius * 0.1),
    )
    parts.append(obj)
    return obj


def make_gabled_roof(
    *,
    name: str,
    width: float,
    depth: float,
    eave_z: float,
    ridge_z: float,
    material: bpy.types.Material,
    collection: bpy.types.Collection,
) -> list[bpy.types.Object]:
    half_width = width * 0.5
    rise = ridge_z - eave_z
    panel_length = math.hypot(half_width, rise)
    slope = math.atan2(rise, half_width)
    panels: list[bpy.types.Object] = []
    for side in (-1.0, 1.0):
        label = "left" if side < 0.0 else "right"
        bpy.ops.mesh.primitive_cube_add(
            size=1.0,
            location=(
                side * width * 0.25,
                0.0,
                eave_z + rise * 0.5,
            ),
            rotation=(0.0, side * slope, 0.0),
        )
        panel = bpy.context.object
        panel.name = f"{name}_{label}"
        panel.dimensions = (panel_length, depth, 0.28)
        BASE.apply_transform(panel)
        BASE.add_bevel(panel, 0.045, 2)
        BASE.assign_material(panel, material)
        BASE.move_to_collection(panel, collection)
        panels.append(panel)
    return panels


def add_window_frame_front(
    parts: list[bpy.types.Object],
    *,
    prefix: str,
    x: float,
    y: float,
    z: float,
    width: float,
    height: float,
    lit: bool,
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
    bevel: float,
) -> None:
    pane_material = materials["glass_lit"] if lit else materials["glass"]
    add_box(
        parts,
        f"{prefix}_pane",
        (width, 0.08, height),
        (x, y, z),
        pane_material,
        collection,
        bevel=0.015,
        segments=1,
    )
    frame = 0.095
    depth = 0.12
    for suffix, fx, fz, fw, fh in (
        ("left", x - width * 0.5, z, frame, height + 0.18),
        ("right", x + width * 0.5, z, frame, height + 0.18),
        ("top", x, z + height * 0.5, width + 0.18, frame),
        ("bottom", x, z - height * 0.5, width + 0.18, frame),
        ("mullion", x, z, frame * 0.72, height),
    ):
        add_box(
            parts,
            f"{prefix}_{suffix}",
            (fw, depth, fh),
            (fx, y - 0.015, fz),
            materials["metal"],
            collection,
            bevel=bevel,
            segments=2,
        )


def add_window_frame_side(
    parts: list[bpy.types.Object],
    *,
    prefix: str,
    x: float,
    y: float,
    z: float,
    width: float,
    height: float,
    lit: bool,
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
    bevel: float,
) -> None:
    pane_material = materials["glass_lit"] if lit else materials["glass"]
    add_box(
        parts,
        f"{prefix}_pane",
        (0.08, width, height),
        (x, y, z),
        pane_material,
        collection,
        bevel=0.015,
        segments=1,
    )
    frame = 0.095
    depth = 0.12
    for suffix, fy, fz, fw, fh in (
        ("left", y - width * 0.5, z, frame, height + 0.18),
        ("right", y + width * 0.5, z, frame, height + 0.18),
        ("top", y, z + height * 0.5, width + 0.18, frame),
        ("bottom", y, z - height * 0.5, width + 0.18, frame),
        ("mullion", y, z, frame * 0.72, height),
    ):
        add_box(
            parts,
            f"{prefix}_{suffix}",
            (depth, fw, fh),
            (x - 0.015, fy, fz),
            materials["metal"],
            collection,
            bevel=bevel,
            segments=2,
        )


def add_ground_storefront(
    parts: list[bpy.types.Object],
    *,
    spec: dict[str, Any],
    lod: int,
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
) -> None:
    width, depth = spec["footprint_m"]
    front_y = -depth * 0.5 + 0.07
    if lod == 2:
        add_box(
            parts,
            "lod2_storefront_glazing",
            (width - 1.0, 0.09, 2.4),
            (0.0, front_y, 1.7),
            materials["glass"],
            collection,
        )
        add_box(
            parts,
            "lod2_storefront_sign",
            (width - 1.4, 0.12, 0.55),
            (0.0, front_y - 0.015, 3.35),
            materials["sign"],
            collection,
        )
        return

    bay_count = max(3, int((width - 1.0) // 3.7))
    bay_width = (width - 1.4) / bay_count
    frame = 0.12 if lod == 0 else 0.08
    for index in range(bay_count):
        x = -width * 0.5 + 0.7 + bay_width * (index + 0.5)
        lit = lod == 0 and index % 3 == 1
        if lod == 0:
            add_window_frame_front(
                parts,
                prefix=f"lod0_storefront_bay_{index}",
                x=x,
                y=front_y,
                z=1.72,
                width=bay_width - 0.34,
                height=2.55,
                lit=lit,
                collection=collection,
                materials=materials,
                bevel=0.025,
            )
        else:
            add_box(
                parts,
                f"lod1_storefront_bay_{index}",
                (bay_width - 0.28, 0.09, 2.45),
                (x, front_y, 1.72),
                materials["glass"],
                collection,
                bevel=0.025,
                segments=1,
            )
    entry_x = 0.0 if bay_count % 2 else -bay_width * 0.5
    add_box(
        parts,
        f"lod{lod}_entry_door",
        (1.25, 0.11, 2.55),
        (entry_x, front_y - 0.025, 1.42),
        materials["glass_lit"] if lod == 0 else materials["glass"],
        collection,
        bevel=0.035,
        segments=2 if lod == 0 else 1,
    )
    add_box(
        parts,
        f"lod{lod}_canopy",
        (width - 1.0, 1.18, 0.18),
        (0.0, -depth * 0.5 + 0.55, 3.35),
        materials["metal"],
        collection,
        bevel=0.06 if lod == 0 else 0.025,
        segments=2 if lod == 0 else 1,
    )
    add_box(
        parts,
        f"lod{lod}_signboard",
        (min(width - 1.6, 9.2), 0.16, 0.82),
        (0.0, front_y - 0.035, 4.02),
        materials["sign"],
        collection,
        bevel=0.075 if lod == 0 else 0.02,
        segments=3 if lod == 0 else 1,
    )
    if lod == 0:
        # Project-owned abstract glyph geometry.  It remains readable as a
        # sign at street distance without introducing a third-party mark.
        for index, x in enumerate((-2.7, -1.35, 0.0, 1.35, 2.7)):
            add_box(
                parts,
                f"lod0_sign_glyph_{index}",
                (0.62, 0.07, 0.19 + (index % 2) * 0.12),
                (x, front_y - 0.13, 4.02),
                materials["trim"],
                collection,
                bevel=0.035,
                segments=2,
            )


def add_upper_facades(
    parts: list[bpy.types.Object],
    *,
    spec: dict[str, Any],
    lod: int,
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
) -> None:
    width, depth = spec["footprint_m"]
    body_height = float(spec["body_height_m"])
    stories = int(spec["stories"])
    front_y = -depth * 0.5 + 0.065
    if lod == 2:
        add_box(
            parts,
            "lod2_upper_glass_band",
            (width - 2.0, 0.07, max(1.2, body_height - 5.6)),
            (0.0, front_y, (body_height + 4.1) * 0.5),
            materials["glass"],
            collection,
        )
        return

    floor_height = (body_height - 4.25) / max(1, stories - 1)
    front_columns = max(3, int((width - 1.5) // 3.2))
    side_columns = max(2, int((depth - 2.5) // 5.2))
    window_width = min(2.05, (width - 1.6) / front_columns - 0.45)
    for floor in range(1, stories):
        z = 4.25 + (floor - 0.5) * floor_height
        if lod == 1:
            add_box(
                parts,
                f"lod1_front_glass_floor_{floor}",
                (width - 2.0, 0.075, min(1.55, floor_height - 0.5)),
                (0.0, front_y, z),
                materials["glass"],
                collection,
                bevel=0.02,
                segments=1,
            )
        else:
            for column in range(front_columns):
                x = (
                    -width * 0.5
                    + 0.8
                    + (width - 1.6) * (column + 0.5) / front_columns
                )
                add_window_frame_front(
                    parts,
                    prefix=f"lod0_front_f{floor}_w{column}",
                    x=x,
                    y=front_y,
                    z=z,
                    width=window_width,
                    height=min(1.72, floor_height - 0.55),
                    lit=(floor + column) % 7 == 0,
                    collection=collection,
                    materials=materials,
                    bevel=0.02,
                )
        band_z = 4.25 + floor * floor_height
        add_box(
            parts,
            f"lod{lod}_front_floor_band_{floor}",
            (width - 0.5, 0.16, 0.16 if lod == 0 else 0.11),
            (0.0, front_y, band_z),
            materials["trim"],
            collection,
            bevel=0.025 if lod == 0 else 0.0,
            segments=2 if lod == 0 else 1,
        )

    if lod == 0:
        side_window_width = min(2.15, (depth - 2.5) / side_columns - 0.6)
        for side in (-1.0, 1.0):
            x = side * (width * 0.5 - 0.065)
            label = "left" if side < 0.0 else "right"
            for floor in range(1, stories):
                z = 4.25 + (floor - 0.5) * floor_height
                for column in range(side_columns):
                    y = (
                        -depth * 0.5
                        + 1.25
                        + (depth - 2.5) * (column + 0.5) / side_columns
                    )
                    add_window_frame_side(
                        parts,
                        prefix=f"lod0_{label}_f{floor}_w{column}",
                        x=x,
                        y=y,
                        z=z,
                        width=side_window_width,
                        height=min(1.65, floor_height - 0.62),
                        lit=(floor + column + (1 if side > 0.0 else 0)) % 9 == 0,
                        collection=collection,
                        materials=materials,
                        bevel=0.018,
                    )


def add_roof_detail(
    parts: list[bpy.types.Object],
    *,
    spec: dict[str, Any],
    lod: int,
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
) -> None:
    width, depth = spec["footprint_m"]
    body_height = float(spec["body_height_m"])
    height_limit = float(spec["height_limit_m"])
    style = str(spec["style"])
    if style == "gabled":
        roof_panels = make_gabled_roof(
            name=f"lod{lod}_gabled_roof",
            width=width - 0.35,
            depth=depth - 0.35,
            eave_z=body_height,
            ridge_z=min(height_limit - 0.12, body_height + 3.72),
            material=materials["roof"],
            collection=collection,
        )
        parts.extend(roof_panels)
        if lod == 0:
            for x in (-width * 0.27, width * 0.24):
                add_box(
                    parts,
                    f"lod0_gable_dormer_{x:+.2f}",
                    (2.25, 1.4, 1.45),
                    (x, -depth * 0.5 + 0.82, body_height + 1.1),
                    materials["accent"],
                    collection,
                    bevel=0.08,
                    segments=2,
                )
                add_box(
                    parts,
                    f"lod0_gable_dormer_window_{x:+.2f}",
                    (1.28, 0.07, 0.62),
                    (x, -depth * 0.5 + 0.085, body_height + 1.08),
                    materials["glass_lit"],
                    collection,
                    bevel=0.025,
                    segments=1,
                )
                add_box(
                    parts,
                    f"lod0_gable_dormer_cap_{x:+.2f}",
                    (2.42, 1.58, 0.16),
                    (x, -depth * 0.5 + 0.82, body_height + 1.87),
                    materials["roof"],
                    collection,
                    bevel=0.045,
                    segments=2,
                )
        return

    roof_z = body_height + 0.18
    add_box(
        parts,
        f"lod{lod}_roof_slab",
        (width - 0.35, depth - 0.35, 0.34),
        (0.0, 0.0, roof_z),
        materials["roof"],
        collection,
        bevel=0.08 if lod == 0 else 0.0,
        segments=2 if lod == 0 else 1,
    )
    parapet_height = 0.72
    parapet_z = body_height + parapet_height * 0.5
    for label, dimensions, location in (
        (
            "front",
            (width - 0.25, 0.32, parapet_height),
            (0.0, -depth * 0.5 + 0.18, parapet_z),
        ),
        (
            "rear",
            (width - 0.25, 0.32, parapet_height),
            (0.0, depth * 0.5 - 0.18, parapet_z),
        ),
        (
            "left",
            (0.32, depth - 0.9, parapet_height),
            (-width * 0.5 + 0.18, 0.0, parapet_z),
        ),
        (
            "right",
            (0.32, depth - 0.9, parapet_height),
            (width * 0.5 - 0.18, 0.0, parapet_z),
        ),
    ):
        add_box(
            parts,
            f"lod{lod}_parapet_{label}",
            dimensions,
            location,
            materials["trim"],
            collection,
            bevel=0.06 if lod == 0 else 0.0,
            segments=2 if lod == 0 else 1,
        )
    if lod == 2:
        return
    unit_count = 1 if lod == 1 else max(2, min(5, int(depth // 10.0)))
    available = max(1.0, depth - 5.0)
    for index in range(unit_count):
        y = (
            0.0
            if unit_count == 1
            else -available * 0.5 + available * index / (unit_count - 1)
        )
        x = -width * 0.18 if index % 2 == 0 else width * 0.18
        unit_height = min(1.15, height_limit - body_height - 0.85)
        add_box(
            parts,
            f"lod{lod}_hvac_unit_{index}",
            (2.35, 1.55, unit_height),
            (x, y, body_height + 0.72 + unit_height * 0.5),
            materials["metal"],
            collection,
            bevel=0.11 if lod == 0 else 0.02,
            segments=3 if lod == 0 else 1,
        )
        if lod == 0:
            add_cylinder(
                parts,
                f"lod0_hvac_fan_{index}",
                radius=0.48,
                depth=0.08,
                location=(
                    x,
                    y,
                    body_height + 0.74 + unit_height,
                ),
                material=materials["accent"],
                collection=collection,
                vertices=24,
            )


def build_render_lod(
    *,
    spec: dict[str, Any],
    lod: int,
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
) -> bpy.types.Object:
    asset_id = str(spec["asset_id"])
    width, depth = spec["footprint_m"]
    body_height = float(spec["body_height_m"])
    parts: list[bpy.types.Object] = []
    add_box(
        parts,
        f"lod{lod}_ground_plinth",
        (width, depth, 0.28),
        (0.0, 0.0, 0.14),
        materials["concrete"],
        collection,
        bevel=0.055 if lod == 0 else 0.0,
        segments=2 if lod == 0 else 1,
    )
    add_box(
        parts,
        f"lod{lod}_main_mass",
        (width - 0.44, depth - 0.44, body_height),
        (0.0, 0.0, body_height * 0.5),
        materials["facade"],
        collection,
        bevel=0.12 if lod == 0 else 0.025 if lod == 1 else 0.0,
        segments=3 if lod == 0 else 1,
    )
    add_ground_storefront(
        parts,
        spec=spec,
        lod=lod,
        collection=collection,
        materials=materials,
    )
    add_upper_facades(
        parts,
        spec=spec,
        lod=lod,
        collection=collection,
        materials=materials,
    )
    add_roof_detail(
        parts,
        spec=spec,
        lod=lod,
        collection=collection,
        materials=materials,
    )

    style = str(spec["style"])
    front_y = -depth * 0.5 + 0.05
    if style == "contemporary" and lod < 2:
        fin_count = 5 if lod == 0 else 3
        for index in range(fin_count):
            x = -width * 0.34 + width * 0.68 * index / max(1, fin_count - 1)
            add_box(
                parts,
                f"lod{lod}_contemporary_fin_{index}",
                (0.16 if lod == 0 else 0.12, 0.32, body_height - 4.9),
                (x, front_y, (body_height + 4.55) * 0.5),
                materials["accent"],
                collection,
                bevel=0.035 if lod == 0 else 0.0,
                segments=2 if lod == 0 else 1,
            )
    elif style == "heritage" and lod == 0:
        for side in (-1.0, 1.0):
            label = "left" if side < 0.0 else "right"
            x = side * (width * 0.5 - 0.34)
            for level in range(7):
                z = 0.65 + level * (body_height - 1.3) / 6.0
                add_box(
                    parts,
                    f"lod0_heritage_quoin_{label}_{level}",
                    (0.62, 0.34, 0.62),
                    (x, front_y, z),
                    materials["trim"],
                    collection,
                    bevel=0.055,
                    segments=2,
                )
    elif style == "market" and lod < 2:
        monitor_height = 1.16 if lod == 0 else 0.72
        add_box(
            parts,
            f"lod{lod}_market_roof_monitor",
            (4.4, depth - 8.0, monitor_height),
            (0.0, 1.0, body_height + 0.9 + monitor_height * 0.5),
            materials["accent"],
            collection,
            bevel=0.12 if lod == 0 else 0.0,
            segments=3 if lod == 0 else 1,
        )
        if lod == 0:
            for side in (-1.0, 1.0):
                add_box(
                    parts,
                    f"lod0_market_clerestory_{'left' if side < 0 else 'right'}",
                    (0.08, depth - 9.0, 0.62),
                    (side * 2.17, 1.0, body_height + 1.5),
                    materials["glass_lit"],
                    collection,
                    bevel=0.025,
                    segments=1,
                )
    elif style == "industrial" and lod < 2:
        post_count = 7 if lod == 0 else 4
        for index in range(post_count):
            x = -width * 0.43 + width * 0.86 * index / max(1, post_count - 1)
            add_box(
                parts,
                f"lod{lod}_industrial_arcade_post_{index}",
                (0.22, 0.42, 3.75),
                (x, front_y, 1.95),
                materials["metal"],
                collection,
                bevel=0.035 if lod == 0 else 0.0,
                segments=2 if lod == 0 else 1,
            )
        add_box(
            parts,
            f"lod{lod}_industrial_arcade_header",
            (width - 1.1, 0.44, 0.34),
            (0.0, front_y, 3.88),
            materials["metal"],
            collection,
            bevel=0.045 if lod == 0 else 0.0,
            segments=2 if lod == 0 else 1,
        )

    if lod == 0:
        # Pilasters, cornice, rainwater leaders and service entry establish
        # depth and scale from both road and alley viewpoints.
        front_y = -depth * 0.5 + 0.055
        for index, x in enumerate((-width * 0.5 + 0.42, width * 0.5 - 0.42)):
            add_box(
                parts,
                f"lod0_front_pilaster_{index}",
                (0.42, 0.24, body_height - 0.45),
                (x, front_y, (body_height + 0.15) * 0.5),
                materials["accent"],
                collection,
                bevel=0.045,
                segments=2,
            )
        add_box(
            parts,
            "lod0_cornice",
            (width - 0.2, 0.44, 0.42),
            (0.0, front_y, body_height - 0.28),
            materials["trim"],
            collection,
            bevel=0.085,
            segments=3,
        )
        for side in (-1.0, 1.0):
            label = "left" if side < 0.0 else "right"
            add_cylinder(
                parts,
                f"lod0_downspout_{label}",
                radius=0.075,
                depth=max(0.4, body_height - 0.72),
                location=(
                    side * (width * 0.5 - 0.34),
                    depth * 0.5 - 0.24,
                    (body_height - 0.72) * 0.5,
                ),
                material=materials["metal"],
                collection=collection,
                vertices=12,
            )
        add_box(
            parts,
            "lod0_rear_service_door",
            (1.45, 0.12, 2.55),
            (0.0, depth * 0.5 - 0.07, 1.34),
            materials["metal"],
            collection,
            bevel=0.04,
            segments=2,
        )
    obj = BASE.join_components(
        parts,
        name=f"{asset_id}_lod{lod}",
        role="render",
        lod=lod,
    )
    obj["rorng_family_id"] = FAMILY_ID
    obj["rorng_storefront_style"] = spec["style"]
    return obj


def build_collision(
    *,
    spec: dict[str, Any],
    collection: bpy.types.Collection,
    material: bpy.types.Material,
) -> list[bpy.types.Object]:
    asset_id = str(spec["asset_id"])
    width, depth = spec["footprint_m"]
    body_height = float(spec["body_height_m"])
    proxy = BASE.make_box(
        "collision_building_component",
        dimensions=(width - 0.34, depth - 0.34, body_height),
        location=(0.0, 0.0, body_height * 0.5),
        material=material,
        collection=collection,
    )
    proxy = BASE.join_components(
        [proxy],
        name=f"{asset_id}_collision_fixture",
        role="collision-fixture",
        lod=None,
    )
    return [proxy]


def add_preview_scene(
    *,
    spec: dict[str, Any],
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
    preview_path: Path,
) -> None:
    width, depth = spec["footprint_m"]
    height = float(spec["height_limit_m"])
    BASE.make_box(
        "preview_ground",
        dimensions=(max(46.0, width + 24.0), max(46.0, depth + 20.0), 0.18),
        location=(0.0, 0.0, -0.09),
        material=materials["preview_ground"],
        collection=collection,
        bevel=0.06,
        bevel_segments=2,
    )
    BASE.make_box(
        "preview_road",
        dimensions=(max(46.0, width + 24.0), 8.4, 0.08),
        location=(0.0, -depth * 0.5 - 4.25, 0.005),
        material=materials["preview_road"],
        collection=collection,
        bevel=0.04,
        bevel_segments=2,
    )
    bpy.ops.object.camera_add(
        location=(
            width * 1.1 + 15.0,
            -depth * 0.85 - 18.0,
            height * 0.72 + 4.5,
        )
    )
    camera = bpy.context.object
    camera.name = "preview_camera"
    camera.data.lens = 50.0
    BASE.point_camera(camera, (0.0, -depth * 0.12, height * 0.38))
    BASE.move_to_collection(camera, collection)
    bpy.context.scene.camera = camera

    bpy.ops.object.light_add(
        type="AREA",
        location=(-width * 0.8 - 10.0, -depth * 0.65 - 8.0, height + 16.0),
    )
    key = bpy.context.object
    key.name = "preview_key"
    key.data.energy = 2_150.0
    key.data.shape = "DISK"
    key.data.size = 10.0
    BASE.point_camera(key, (0.0, 0.0, height * 0.42))
    BASE.move_to_collection(key, collection)

    bpy.ops.object.light_add(
        type="AREA",
        location=(width * 0.85 + 8.0, -depth * 0.1, height * 0.45),
    )
    fill = bpy.context.object
    fill.name = "preview_fill"
    fill.data.energy = 1_050.0
    fill.data.size = 8.0
    BASE.point_camera(fill, (0.0, 0.0, height * 0.36))
    BASE.move_to_collection(fill, collection)

    bpy.ops.object.light_add(
        type="AREA",
        location=(0.0, depth * 0.5 + 8.0, height + 8.0),
    )
    rim = bpy.context.object
    rim.name = "preview_rim"
    rim.data.energy = 1_500.0
    rim.data.size = 7.0
    BASE.point_camera(rim, (0.0, 0.0, height * 0.55))
    BASE.move_to_collection(rim, collection)

    world = bpy.context.scene.world
    world.use_nodes = True
    background = world.node_tree.nodes.get("Background")
    background.inputs["Color"].default_value = (0.045, 0.065, 0.095, 1.0)
    background.inputs["Strength"].default_value = 0.5
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
    scene.view_settings.exposure = 0.25


def asset_paths(root: Path, asset_id: str) -> dict[str, Path]:
    relative = Path("buildings/storefront_family") / asset_id
    source_root = root / "content-source/cityworld_next" / relative
    runtime_root = root / "resources/nextgen/cityworld" / relative
    return {
        "blend": source_root / f"{asset_id}.blend",
        "glb": runtime_root / f"{asset_id}.glb",
        "manifest": runtime_root / f"{asset_id}.asset.json",
        "preview": source_root / f"{asset_id}_preview.png",
    }


def generate_variant(root: Path, spec: dict[str, Any]) -> dict[str, Any]:
    asset_id = str(spec["asset_id"])
    paths = asset_paths(root, asset_id)
    for path in paths.values():
        path.parent.mkdir(parents=True, exist_ok=True)
    generator_path = Path(__file__).resolve()
    generator_hash = BASE.sha256_file(generator_path)
    dependencies = [
        {
            "path": dependency.relative_to(root).as_posix(),
            "sha256": BASE.sha256_file(dependency),
        }
        for dependency in AUTHORING_DEPENDENCY_PATHS
    ]
    generator_record = {
        "dependencies": dependencies,
        "format": GENERATOR_ID,
        "path": generator_path.relative_to(root).as_posix(),
        "sha256": generator_hash,
    }
    previous_manifest = RETENTION.load_previous_manifest(paths["manifest"])
    retain_authenticated_artifacts = (
        previous_manifest is not None
        and RETENTION.generator_identity_matches(
            previous_manifest,
            blender_version=bpy.app.version_string,
            generator=generator_record,
        )
    )
    authenticated_hashes: dict[str, str] = {}
    if retain_authenticated_artifacts:
        authenticated_hashes = RETENTION.authenticate_retained_artifacts(
            previous_manifest,
            repo_root=root,
            expected_paths={
                "blend": paths["blend"],
                "glb": paths["glb"],
                "preview": paths["preview"],
            },
        )

    candidate_blend = paths["blend"].with_name(f".{asset_id}.candidate.blend")
    candidate_glb = paths["glb"].with_name(f".{asset_id}.candidate.glb")
    candidate_preview = paths["preview"].with_name(f".{asset_id}.candidate.png")
    candidate_blend.unlink(missing_ok=True)
    candidate_glb.unlink(missing_ok=True)
    candidate_preview.unlink(missing_ok=True)
    reset_scene_fully(asset_id)
    render_collection = BASE.make_collection(f"{asset_id}_render")
    collision_collection = BASE.make_collection(f"{asset_id}_collision")
    preview_collection = BASE.make_collection(f"{asset_id}_preview")
    materials = make_materials(asset_id, str(spec["style"]))
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
    CANONICALIZER.canonicalize_glb_geometry(candidate_glb)
    canonicalize_textureless_texcoords(candidate_glb)

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
    candidate_glb_hash = BASE.sha256_file(candidate_glb)
    if retain_authenticated_artifacts:
        if candidate_glb_hash != authenticated_hashes["glb"]:
            candidate_blend.unlink(missing_ok=True)
            candidate_glb.unlink(missing_ok=True)
            candidate_preview.unlink(missing_ok=True)
            raise RETENTION.ArtifactContractError(
                "deterministic storefront GLB changed under the same generator, "
                "dependencies, and Blender version"
            )
        candidate_blend.unlink()
        candidate_glb.unlink()
        candidate_preview.unlink()
    else:
        os.replace(candidate_blend, paths["blend"])
        os.replace(candidate_glb, paths["glb"])
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
    manifest["authoring"]["artifact_reproducibility"] = {
        "blend": "authenticated-retained-session-metadata-bearing",
        "glb": "byte-deterministic-pinned-toolchain",
        "preview": "authenticated-retained-render-metadata-bearing",
    }
    manifest["authoring"]["generator"] = generator_record
    manifest["authoring"]["procedural_provenance"] = {
        "external_geometry": False,
        "external_materials": False,
        "external_textures": False,
        "legacy_facts_only": [
            "placement-count",
            "axis-aligned-bounds",
            "source-yaw",
        ],
        "method": "deterministic-project-authored-blender-python",
        "rights_basis": "GPL-3.0-or-later project-authored source",
    }
    manifest["collision"].pop("road_surface_z_m", None)
    manifest["collision"]["profile"] = "single-watertight-proxy-v1"
    manifest["collision"]["purpose"] = "conservative-building-envelope"
    manifest["connectors"] = []
    lod_entries = manifest["geometry"]["lods"]
    manifest["geometry"] = {
        "asset_family": FAMILY_ID,
        "detail_profile": {
            "facade": (
                "recessed-glazing-frames-mullions-doors-canopy-signage-"
                "pilasters-cornice"
            ),
            "roof": "parapet-hvac-fans-drainage-or-authored-gable",
            "runtime_materials": "portable-factor-lit-rtshader-compatible",
        },
        "footprint_m": [float(value) for value in spec["footprint_m"]],
        "ground_plane_z_m": 0.0,
        "height_limit_m": float(spec["height_limit_m"]),
        "legacy_object": spec["legacy_object"],
        "lod0_triangle_ceiling": 70_000,
        "lod1_max_ratio": 0.35,
        "lod2_max_ratio": 0.12,
        "lods": lod_entries,
        "style_variant": spec["label"],
        "texcoord_policy": "canonical-zero-textureless-v1",
    }
    manifest["storefront"] = {
        "emissive_policy": {
            "day_safe": True,
            "material": f"{asset_id}_glass_warm_interior",
            "purpose": "selected-occupied-interior-windows-only",
            "runtime_point_lights": 0,
        },
        "family": FAMILY_ID,
        "grounding": {
            "foundation_below_ground_m": 0.0,
            "minimum_render_z_m": 0.0,
            "minimum_collision_z_m": 0.0,
            "placement_vertical_offset_m": 0.0,
        },
        "legacy_object": spec["legacy_object"],
        "placement_integration": "deferred",
        "style": spec["style"],
    }
    for material in manifest["materials"]:
        if material["name"] == f"{asset_id}_glass_warm_interior":
            material["emissive_factor_linear"] = [0.684, 0.3312, 0.0864]
    paths["manifest"].write_text(
        json.dumps(manifest, indent=2, ensure_ascii=True, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return {
        "asset_id": asset_id,
        "footprint_m": list(spec["footprint_m"]),
        "legacy_object": spec["legacy_object"],
        "lod_triangles": [BASE.triangle_count(obj) for obj in lod_objects],
        "manifest": paths["manifest"].relative_to(root).as_posix(),
        "placement_count": spec["placement_count"],
        "style": spec["label"],
    }


def selector_assignments() -> list[dict[str, Any]]:
    assignments: list[dict[str, Any]] = []
    global_ordinal = 0
    for spec in VARIANTS:
        legacy_object = str(spec["legacy_object"])
        for source_ordinal, source_yaw in enumerate(spec["source_yaws"]):
            normalized_yaw = float(source_yaw) % 360.0
            payload = (
                f"{SELECTOR_NAMESPACE}:{legacy_object}:{source_ordinal}:"
                f"{normalized_yaw:.3f}"
            ).encode("ascii")
            assignments.append(
                {
                    "digest_sha256": hashlib.sha256(payload).hexdigest(),
                    "global_ordinal": global_ordinal,
                    "legacy_object": legacy_object,
                    "source_ordinal": source_ordinal,
                    "uniform_scale": 1.0,
                    "variant": spec["asset_id"],
                    "yaw_degrees": normalized_yaw,
                    "yaw_offset_degrees": 0.0,
                }
            )
            global_ordinal += 1
    return assignments


def write_family_contract(
    root: Path,
    generated: list[dict[str, Any]],
) -> Path:
    path = (
        root
        / "content-source/cityworld_next/buildings/storefront_family/"
        "rorng_city_storefront_family.v1.json"
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
                "legacy_facts_only": [
                    "placement-count",
                    "axis-aligned-bounds",
                    "source-yaw",
                ],
                "method": "deterministic-project-authored-blender-python",
                "rights_basis": "GPL-3.0-or-later project-authored source",
            },
            "artifact_reproducibility": {
                "blend": "authenticated-retained-session-metadata-bearing",
                "glb": "byte-deterministic-pinned-toolchain",
                "preview": "authenticated-retained-render-metadata-bearing",
            },
        },
        "format": FAMILY_FORMAT,
        "legacy_audit": {
            "archive": {
                "bytes": 158_845_395,
                "entries": 1_411,
                "name": "CityWorld.zip",
                "sha256": SOURCE_ARCHIVE_SHA256,
            },
            "method": (
                "read-only-placement-count-and-ogre-14.5.2-vertex-bounds"
            ),
            "objects": [
                {
                    "collision_mesh_sha256": spec["collision_sha256"],
                    "legacy_bounds_m": {
                        key: list(value)
                        for key, value in spec["legacy_bounds_m"].items()
                    },
                    "legacy_object": spec["legacy_object"],
                    "placement_count": spec["placement_count"],
                    "render_mesh_sha256": spec["render_sha256"],
                }
                for spec in VARIANTS
            ],
            "placement_count": sum(
                int(spec["placement_count"]) for spec in VARIANTS
            ),
            "source_geometry_imported": False,
            "source_materials_imported": False,
            "source_textures_imported": False,
        },
        "placement_target": {
            "city": "Penguinville",
            "integration_status": "asset-ready-placement-deferred",
            "map": "CityWorld",
            "placement_count": sum(
                int(spec["placement_count"]) for spec in VARIANTS
            ),
        },
        "selector": {
            "algorithm": "legacy-object-exact-fit-and-source-transform-v1",
            "assignments": selector_assignments(),
            "namespace": SELECTOR_NAMESPACE,
            "scale_policy": "preserve-source-uniform-one",
            "yaw_policy": "preserve-source-yaw-no-offset",
        },
        "variants": [
            {
                "asset_id": entry["asset_id"],
                "footprint_m": entry["footprint_m"],
                "legacy_object": entry["legacy_object"],
                "manifest": entry["manifest"],
                "placement_count": entry["placement_count"],
                "style": entry["style"],
            }
            for entry in generated
        ],
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
                "placements": sum(
                    int(spec["placement_count"]) for spec in VARIANTS
                ),
                "variants": generated,
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
