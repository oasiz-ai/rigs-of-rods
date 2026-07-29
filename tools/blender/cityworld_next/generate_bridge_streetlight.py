#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate a collisionless, parapet-mounted CityWorld bridge streetlight."""

from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import struct
import sys
import zlib

import bpy


SCRIPT_DIRECTORY = Path(__file__).resolve().parent
LED_GENERATOR_PATH = SCRIPT_DIRECTORY / "generate_led_streetlight.py"
LED_SPEC = importlib.util.spec_from_file_location(
    "rorng_bridge_streetlight_helpers",
    LED_GENERATOR_PATH,
)
if LED_SPEC is None or LED_SPEC.loader is None:
    raise RuntimeError("cannot load the CityWorld streetlight helpers")
LED = importlib.util.module_from_spec(LED_SPEC)
LED_SPEC.loader.exec_module(LED)

BASE = LED.BASE
CANONICALIZER = LED.CANONICALIZER
ASSET_ID = "rorng_city_led_streetlight_bridge"
ASSET_VERSION = 1
ASSET_PROFILE = "static-visual-v1"
GENERATOR_ID = "ror-cityworld-bridge-streetlight-generator-v1"
FIXTURE_HEIGHT_M = 7.48
FOOTPRINT_DIAMETER_M = 0.4
LIGHT_POSITION_M = [0.0, 1.58, 7.12]
LIGHT_COLOR_LINEAR = [1.0, 0.72, 0.3]
LIGHT_RANGE_M = 24.0
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
PNG_METADATA_CHUNKS = {
    b"eXIf",
    b"iTXt",
    b"tEXt",
    b"tIME",
    b"zTXt",
}
MAX_PREVIEW_BYTES = 64 * 1024 * 1024


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


def build_render_lod(
    lod: int,
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
) -> bpy.types.Object:
    original_cylinder = LED.cylinder

    def bridge_mount_cylinder(
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
        if name.endswith("_foundation"):
            radius = 0.2
            depth = 0.08
            location = (0.0, 0.0, 0.04)
            material = materials["galvanized"]
        elif name.endswith("_base_flange"):
            radius = 0.18
            depth = 0.05
            location = (0.0, 0.0, 0.105)
        elif "_anchor_bolt_" in name:
            x, y, _ = location
            location = (
                0.12 if x > 0.0 else -0.12,
                0.12 if y > 0.0 else -0.12,
                0.16,
            )
            depth = 0.06
        original_cylinder(
            parts,
            name,
            radius,
            depth,
            location,
            rotation,
            vertices,
            material,
            collection,
            bevel=bevel,
        )

    LED.cylinder = bridge_mount_cylinder
    try:
        return LED.build_render_lod(lod, collection, materials)
    finally:
        LED.cylinder = original_cylinder


def add_parapet_preview(
    collection: bpy.types.Collection,
    material: bpy.types.Material,
) -> None:
    parapet = BASE.make_box(
        "preview_bridge_parapet",
        dimensions=(0.45, 4.0, 0.95),
        location=(0.0, 0.0, -0.475),
        material=material,
        collection=collection,
        bevel=0.035,
        bevel_segments=2,
    )
    parapet["rorng_role"] = "preview-only"


def canonicalize_preview_png(path: Path) -> None:
    payload = path.read_bytes()
    if len(payload) > MAX_PREVIEW_BYTES:
        raise RuntimeError("preview PNG exceeds the canonicalization limit")
    if not payload.startswith(PNG_SIGNATURE):
        raise RuntimeError("preview is not a PNG file")

    output = bytearray(PNG_SIGNATURE)
    offset = len(PNG_SIGNATURE)
    chunk_types: list[bytes] = []
    while offset < len(payload):
        if offset + 12 > len(payload):
            raise RuntimeError("preview PNG has a truncated chunk header")
        length = struct.unpack_from(">I", payload, offset)[0]
        end = offset + 12 + length
        if end > len(payload):
            raise RuntimeError("preview PNG has a truncated chunk payload")
        chunk_type = payload[offset + 4 : offset + 8]
        chunk_data = payload[offset + 8 : offset + 8 + length]
        expected_crc = struct.unpack_from(">I", payload, offset + 8 + length)[0]
        actual_crc = zlib.crc32(chunk_type + chunk_data) & 0xFFFFFFFF
        if expected_crc != actual_crc:
            raise RuntimeError("preview PNG has an invalid chunk checksum")
        chunk_types.append(chunk_type)
        if chunk_type not in PNG_METADATA_CHUNKS:
            output.extend(payload[offset:end])
        offset = end
        if chunk_type == b"IEND":
            break

    if (
        offset != len(payload)
        or not chunk_types
        or chunk_types[0] != b"IHDR"
        or b"IDAT" not in chunk_types
        or chunk_types[-1] != b"IEND"
    ):
        raise RuntimeError("preview PNG has an invalid chunk sequence")
    path.write_bytes(output)


def sanitize_file_browser_paths() -> None:
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type != "FILE_BROWSER":
                continue
            parameters = getattr(area.spaces.active, "params", None)
            if parameters is not None:
                parameters.directory = b"//"
                parameters.filename = ""


def main() -> int:
    args = parse_args()
    root = args.output_root.resolve()
    if not (root / ".git").exists():
        raise RuntimeError(f"--output-root is not a repository root: {root}")

    generator_path = Path(__file__).resolve()
    asset_root = (
        root
        / "resources/nextgen/cityworld/fixtures/led_streetlight_bridge"
    )
    source_root = (
        root
        / "content-source/cityworld_next/fixtures/led_streetlight_bridge"
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
    LED.ASSET_ID = ASSET_ID
    LED.ASSET_VERSION = ASSET_VERSION
    LED.ASSET_PROFILE = ASSET_PROFILE
    LED.GENERATOR_ID = GENERATOR_ID
    LED.reset_scene_fully()

    render_collection = BASE.make_collection(
        "rorng_bridge_streetlight_render"
    )
    preview_collection = BASE.make_collection(
        "rorng_bridge_streetlight_preview"
    )
    materials = LED.make_materials()
    lod_objects = [
        build_render_lod(lod, render_collection, materials)
        for lod in range(3)
    ]

    bpy.ops.object.select_all(action="DESELECT")
    for obj in lod_objects:
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

    for obj in lod_objects[1:]:
        obj.hide_render = True
        obj.hide_set(True)
    lod_objects[0].hide_render = False
    lod_objects[0].hide_set(False)
    LED.add_preview_scene(preview_collection, materials, preview_path)
    add_parapet_preview(
        preview_collection,
        materials["preview_ground"],
    )
    bpy.context.scene.render.filepath = f"//{preview_path.name}"
    sanitize_file_browser_paths()
    bpy.ops.wm.save_as_mainfile(filepath=str(blend_path), compress=False)
    bpy.context.scene.render.filepath = str(preview_path)
    bpy.ops.render.render(write_still=True)
    canonicalize_preview_png(preview_path)

    manifest_materials = {
        key: material
        for key, material in materials.items()
        if key not in {"collision", "concrete"}
    }
    manifest = BASE.make_manifest(
        root=root,
        generator_path=generator_path,
        blend_path=blend_path,
        glb_path=glb_path,
        preview_path=preview_path,
        lod_objects=lod_objects,
        collision_objects=[],
        materials=manifest_materials,
    )
    manifest["asset"]["profile"] = ASSET_PROFILE
    manifest["authoring"]["generator"]["format"] = GENERATOR_ID
    manifest["authoring"]["generator"]["dependencies"] = [
        {
            "path": dependency_path.relative_to(root).as_posix(),
            "sha256": BASE.sha256_file(dependency_path),
        }
        for dependency_path in (
            LED_GENERATOR_PATH,
            LED.BASE_GENERATOR_PATH,
            LED.CANONICALIZER_PATH,
        )
    ]
    manifest["authoring"]["procedural_provenance"] = {
        "external_geometry": False,
        "external_textures": False,
        "method": "deterministic-project-authored-blender-python",
        "rights_basis": "GPL-3.0-or-later project-authored source",
    }
    manifest["collision"].pop("road_surface_z_m", None)
    manifest["collision"]["profile"] = "collisionless-visual-v1"
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
    manifest["runtime_lights"] = {
        "lights": [
            {
                "color_linear": LIGHT_COLOR_LINEAR,
                "id": "rorng_bridge_streetlight_warm",
                "position_blender_z_up_m": LIGHT_POSITION_M,
                "range_m": LIGHT_RANGE_M,
                "type": "point",
            }
        ],
        "profile": "ror-cityworld-local-lights-v1",
    }
    for material in manifest["materials"]:
        if material["name"] == "rorng_fixture_led_lens_emissive":
            material["emissive_factor_linear"] = LIGHT_COLOR_LINEAR

    manifest_path.write_text(
        json.dumps(manifest, indent=2, ensure_ascii=True, sort_keys=True)
        + "\n",
        encoding="utf-8",
    )
    print(
        json.dumps(
            {
                "asset_id": ASSET_ID,
                "blend": str(blend_path),
                "glb": str(glb_path),
                "lod_triangles": [
                    BASE.triangle_count(obj)
                    for obj in lod_objects
                ],
                "manifest": str(manifest_path),
                "preview": str(preview_path),
                "runtime_lights": 1,
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
