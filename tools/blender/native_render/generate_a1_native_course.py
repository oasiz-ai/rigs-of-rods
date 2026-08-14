#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the project-original A1 60 metre forward-native visual course."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
from functools import lru_cache
from pathlib import Path
import struct
from types import ModuleType
from typing import Callable, Iterable


PACKAGE_ID = "rorng_a1_native_course_60m"
GENERATOR_ID = "rorng-a1-native-course-generator-v1"
SOURCE_DIRECTORY = Path("content-source/native_render/a1_native_course_60m")
GLB_PATH = SOURCE_DIRECTORY / f"{PACKAGE_ID}.glb"
MANIFEST_PATH = SOURCE_DIRECTORY / f"{PACKAGE_ID}.native.json"
COMPOSITION_PATH = SOURCE_DIRECTORY / f"{PACKAGE_ID}.composition.json"
PREVIEW_PATH = SOURCE_DIRECTORY / f"{PACKAGE_ID}.composition.ppm"
ALIGNMENT_PATH = SOURCE_DIRECTORY / f"{PACKAGE_ID}.alignment.json"
PACKAGE_PATH = Path("resources/nextgen/native/a1_native_course_60m") / f"{PACKAGE_ID}.rornative"
REPORT_PATH = Path("resources/nextgen/native/a1_native_course_60m") / f"{PACKAGE_ID}.compile.json"
AUTHORING_UTILITY_PATH = Path("tools/blender/native_render/generate_a0_road_tile.py")
AUTHORING_UTILITY_SHA256 = "cb45a7578501edcc927efb7b88a3115d9a3798c90843019c15ad943caaad40c8"
PREVIEW_WIDTH = 960
PREVIEW_HEIGHT = 540
ROAD_TEXTURE_SIZE = 1024
WET_TEXTURE_SIZE = 1024
SHOULDER_TEXTURE_SIZE = 512

Pixel = tuple[int, int, int, int]
Mesh = dict[str, object]


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def canonical_pretty(value: dict[str, object]) -> bytes:
    return (json.dumps(value, ensure_ascii=True, indent=2, sort_keys=True) + "\n").encode("ascii")


def load_authoring_utility(repo_root: Path) -> ModuleType:
    path = repo_root / AUTHORING_UTILITY_PATH
    actual = sha256_bytes(path.read_bytes())
    if actual != AUTHORING_UTILITY_SHA256:
        raise RuntimeError(
            f"A1 authoring utility checkpoint mismatch: expected={AUTHORING_UTILITY_SHA256}, actual={actual}"
        )
    spec = importlib.util.spec_from_file_location("ror_a1_pinned_authoring_utility", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("could not load pinned A1 authoring utility")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _append_horizontal_quad(
    utility: ModuleType,
    positions: list[tuple[float, float, float]],
    texcoords: list[tuple[float, float]],
    indices: list[int],
    x0: float,
    x1: float,
    y: float,
    z0: float,
    z1: float,
) -> None:
    source_positions, source_uvs, source_indices = utility.quad(x0, x1, y, z0, z1)
    base = len(positions)
    positions.extend(source_positions)
    texcoords.extend(source_uvs)
    indices.extend(base + index for index in source_indices)


def horizontal_batch(
    utility: ModuleType,
    rectangles: Iterable[tuple[float, float, float, float, float]],
    material_index: int,
) -> Mesh:
    positions: list[tuple[float, float, float]] = []
    texcoords: list[tuple[float, float]] = []
    indices: list[int] = []
    for rectangle in rectangles:
        _append_horizontal_quad(utility, positions, texcoords, indices, *rectangle)
    normals = [(0.0, 1.0, 0.0)] * len(positions)
    return {
        "indices": indices,
        "material": material_index,
        "normals": normals,
        "positions": positions,
        "tangents": utility.derive_tangents(positions, normals, texcoords, indices),
        "texcoords": texcoords,
    }


def box_batch(
    utility: ModuleType,
    boxes: Iterable[tuple[float, float, float, float, float, float]],
    material_index: int,
) -> Mesh:
    positions, normals, tangents, texcoords, indices = utility.joined_box_mesh(tuple(boxes))
    return {
        "indices": indices,
        "material": material_index,
        "normals": normals,
        "positions": positions,
        "tangents": tangents,
        "texcoords": texcoords,
    }


def build_meshes(utility: ModuleType) -> dict[str, Mesh]:
    material_names = (
        "rorng_a1_barrier_material",
        "rorng_a1_calibration_material",
        "rorng_a1_curb_material",
        "rorng_a1_glass_material",
        "rorng_a1_lane_marking_material",
        "rorng_a1_road_surface_material",
        "rorng_a1_shoulder_material",
        "rorng_a1_wet_asphalt_material",
    )
    material_index = {name: index for index, name in enumerate(material_names)}

    barrier_boxes: list[tuple[float, float, float, float, float, float]] = [
        (-6.35, -6.05, 0.58, 0.88, -27.0, 27.0),
        (6.05, 6.35, 0.58, 0.88, -27.0, 27.0),
    ]
    for z in range(-24, 25, 6):
        barrier_boxes.extend(
            (
                (-6.38, -6.02, 0.0, 0.64, z - 0.09, z + 0.09),
                (6.02, 6.38, 0.0, 0.64, z - 0.09, z + 0.09),
            )
        )

    calibration_boxes: list[tuple[float, float, float, float, float, float]] = []
    for z in (-24.0, -12.0, 0.0, 12.0, 24.0):
        for x in (-5.35, 5.35):
            calibration_boxes.extend(
                (
                    (x - 0.08, x + 0.08, 0.04, 0.92, z - 0.10, z + 0.10),
                    (x - 0.14, x + 0.14, 0.92, 1.15, z - 0.13, z + 0.13),
                )
            )

    gate_boxes = (
        (-3.85, -3.55, 0.0, 2.62, 5.8, 6.2),
        (3.55, 3.85, 0.0, 2.62, 5.8, 6.2),
        (-3.85, 3.85, 2.62, 2.9, 5.8, 6.2),
    )

    lane_rectangles: list[tuple[float, float, float, float, float]] = [
        (-3.72, -3.58, 0.014, -29.0, 29.0),
        (3.58, 3.72, 0.014, -29.0, 29.0),
        (-3.72, 3.72, 0.016, -25.15, -24.85),
        (-3.72, 3.72, 0.016, 24.85, 25.15),
    ]
    for z in range(-27, 28, 6):
        lane_rectangles.append((-0.09, 0.09, 0.015, float(z), float(z + 3)))

    meshes = {
        "rorng_a1_barrier_mesh": box_batch(
            utility, barrier_boxes, material_index["rorng_a1_barrier_material"]
        ),
        "rorng_a1_calibration_marker_mesh": box_batch(
            utility,
            calibration_boxes,
            material_index["rorng_a1_calibration_material"],
        ),
        "rorng_a1_curb_mesh": box_batch(
            utility,
            (
                (-4.15, -4.0, -0.02, 0.12, -30.0, 30.0),
                (4.0, 4.15, -0.02, 0.12, -30.0, 30.0),
            ),
            material_index["rorng_a1_curb_material"],
        ),
        "rorng_a1_glass_slab_mesh": box_batch(
            utility,
            ((-2.0, 2.0, 0.25, 3.25, -0.04, 0.04),),
            material_index["rorng_a1_glass_material"],
        ),
        "rorng_a1_lane_marking_mesh": horizontal_batch(
            utility,
            lane_rectangles,
            material_index["rorng_a1_lane_marking_material"],
        ),
        "rorng_a0_road_shadow_gate_mesh": box_batch(
            utility, gate_boxes, material_index["rorng_a1_barrier_material"]
        ),
        "rorng_a0_road_surface_mesh": horizontal_batch(
            utility,
            ((-4.0, 4.0, 0.0, -30.0, 30.0),),
            material_index["rorng_a1_road_surface_material"],
        ),
        "rorng_a1_shoulder_mesh": horizontal_batch(
            utility,
            (
                (-6.5, -4.15, -0.02, -30.0, 30.0),
                (4.15, 6.5, -0.02, -30.0, 30.0),
            ),
            material_index["rorng_a1_shoulder_material"],
        ),
        "rorng_a0_wet_asphalt_mesh": horizontal_batch(
            utility,
            ((0.2, 3.8, 0.006, -2.0, 12.0),),
            material_index["rorng_a1_wet_asphalt_material"],
        ),
    }
    return dict(sorted(meshes.items()))


def build_glb(utility: ModuleType) -> bytes:
    meshes = build_meshes(utility)
    binary = bytearray()
    views: list[dict[str, object]] = []
    accessors: list[dict[str, object]] = []
    document_meshes: list[dict[str, object]] = []

    def append_accessor(
        values: list[tuple[float, ...]] | list[int],
        component_type: int,
        value_type: str,
        target: int,
        *,
        bounds: bool = False,
    ) -> int:
        utility.align4(binary)
        offset = len(binary)
        widths = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}
        formats = {5123: "H", 5125: "I", 5126: "f"}
        width = widths[value_type]
        packer = struct.Struct("<" + formats[component_type] * width)
        packed_values: list[tuple[float, ...]] = []
        for value in values:
            components = (value,) if width == 1 else tuple(value)  # type: ignore[arg-type]
            packed = packer.pack(*components)
            binary.extend(packed)
            packed_values.append(tuple(float(component) for component in packer.unpack(packed)))
        views.append(
            {
                "buffer": 0,
                "byteLength": len(values) * packer.size,
                "byteOffset": offset,
                "target": target,
            }
        )
        accessor: dict[str, object] = {
            "bufferView": len(views) - 1,
            "componentType": component_type,
            "count": len(values),
            "type": value_type,
        }
        if bounds:
            accessor["min"] = [
                min(value[axis] for value in packed_values) for axis in range(width)
            ]
            accessor["max"] = [
                max(value[axis] for value in packed_values) for axis in range(width)
            ]
        accessors.append(accessor)
        return len(accessors) - 1

    for name, mesh in meshes.items():
        positions = mesh["positions"]
        normals = mesh["normals"]
        tangents = mesh["tangents"]
        texcoords = mesh["texcoords"]
        indices = mesh["indices"]
        material = mesh["material"]
        assert isinstance(positions, list)
        assert isinstance(normals, list)
        assert isinstance(tangents, list)
        assert isinstance(texcoords, list)
        assert isinstance(indices, list)
        assert isinstance(material, int)
        position_accessor = append_accessor(positions, 5126, "VEC3", 34962, bounds=True)
        normal_accessor = append_accessor(normals, 5126, "VEC3", 34962)
        tangent_accessor = append_accessor(tangents, 5126, "VEC4", 34962)
        uv_accessor = append_accessor(texcoords, 5126, "VEC2", 34962)
        index_type = 5123 if max(indices) <= 65535 else 5125
        index_accessor = append_accessor(indices, index_type, "SCALAR", 34963)
        document_meshes.append(
            {
                "name": name,
                "primitives": [
                    {
                        "attributes": {
                            "NORMAL": normal_accessor,
                            "POSITION": position_accessor,
                            "TANGENT": tangent_accessor,
                            "TEXCOORD_0": uv_accessor,
                        },
                        "indices": index_accessor,
                        "material": material,
                        "mode": 4,
                    }
                ],
            }
        )

    material_names = (
        "rorng_a1_barrier_material",
        "rorng_a1_calibration_material",
        "rorng_a1_curb_material",
        "rorng_a1_glass_material",
        "rorng_a1_lane_marking_material",
        "rorng_a1_road_surface_material",
        "rorng_a1_shoulder_material",
        "rorng_a1_wet_asphalt_material",
    )
    document: dict[str, object] = {
        "accessors": accessors,
        "asset": {"generator": GENERATOR_ID, "version": "2.0"},
        "bufferViews": views,
        "buffers": [{"byteLength": len(binary)}],
        "materials": [{"name": name} for name in material_names],
        "meshes": document_meshes,
        "nodes": [
            {"mesh": index, "name": mesh["name"]}
            for index, mesh in enumerate(document_meshes)
        ],
        "scene": 0,
        "scenes": [{"nodes": list(range(len(document_meshes)))}],
    }
    json_payload = bytearray(utility.canonical_json(document))
    while len(json_payload) % 4:
        json_payload.append(0x20)
    while len(binary) % 4:
        binary.append(0)
    chunks = (
        struct.pack("<II", len(json_payload), 0x4E4F534A)
        + json_payload
        + struct.pack("<II", len(binary), 0x004E4942)
        + binary
    )
    return struct.pack("<4sII", b"glTF", 2, 12 + len(chunks)) + chunks


def build_preview() -> bytes:
    pixels = bytearray(PREVIEW_WIDTH * PREVIEW_HEIGHT * 3)
    for y in range(PREVIEW_HEIGHT):
        interpolation = y / max(1, PREVIEW_HEIGHT - 1)
        color = (
            int(34 + 28 * interpolation),
            int(48 + 34 * interpolation),
            int(66 + 42 * interpolation),
        )
        for x in range(PREVIEW_WIDTH):
            offset = (y * PREVIEW_WIDTH + x) * 3
            pixels[offset : offset + 3] = bytes(color)

    def project(x: float, z: float) -> tuple[float, float]:
        return (
            PREVIEW_WIDTH * 0.5 + x * 18.0 + z * 5.2,
            PREVIEW_HEIGHT * 0.5 - z * 6.4 + x * 2.0,
        )

    def triangle(points: tuple[tuple[float, float], ...], color: tuple[int, int, int]) -> None:
        (x0, y0), (x1, y1), (x2, y2) = points
        minimum_x = max(0, int(math.floor(min(x0, x1, x2))))
        maximum_x = min(PREVIEW_WIDTH - 1, int(math.ceil(max(x0, x1, x2))))
        minimum_y = max(0, int(math.floor(min(y0, y1, y2))))
        maximum_y = min(PREVIEW_HEIGHT - 1, int(math.ceil(max(y0, y1, y2))))

        def edge(a: tuple[float, float], b: tuple[float, float], p: tuple[float, float]) -> float:
            return (p[0] - a[0]) * (b[1] - a[1]) - (p[1] - a[1]) * (b[0] - a[0])

        for pixel_y in range(minimum_y, maximum_y + 1):
            for pixel_x in range(minimum_x, maximum_x + 1):
                sample = (pixel_x + 0.5, pixel_y + 0.5)
                weights = (
                    edge((x1, y1), (x2, y2), sample),
                    edge((x2, y2), (x0, y0), sample),
                    edge((x0, y0), (x1, y1), sample),
                )
                if all(value >= 0.0 for value in weights) or all(value <= 0.0 for value in weights):
                    offset = (pixel_y * PREVIEW_WIDTH + pixel_x) * 3
                    pixels[offset : offset + 3] = bytes(color)

    def quad(x0: float, x1: float, z0: float, z1: float, color: tuple[int, int, int]) -> None:
        points = (project(x0, z0), project(x0, z1), project(x1, z1), project(x1, z0))
        triangle((points[0], points[1], points[2]), color)
        triangle((points[0], points[2], points[3]), color)

    quad(-6.5, -4.15, -30.0, 30.0, (82, 77, 66))
    quad(4.15, 6.5, -30.0, 30.0, (82, 77, 66))
    quad(-4.0, 4.0, -30.0, 30.0, (49, 53, 57))
    quad(0.2, 3.8, -2.0, 12.0, (30, 48, 61))
    quad(-4.15, -4.0, -29.5, 29.5, (156, 51, 44))
    quad(4.0, 4.15, -29.5, 29.5, (236, 225, 196))
    quad(-3.72, -3.58, -29.0, 29.0, (244, 239, 207))
    quad(3.58, 3.72, -29.0, 29.0, (244, 239, 207))
    for z in range(-27, 28, 6):
        quad(-0.09, 0.09, float(z), float(z + 3), (244, 239, 207))
    quad(-3.72, 3.72, -25.15, -24.85, (244, 239, 207))
    quad(-3.72, 3.72, 24.85, 25.15, (244, 239, 207))
    quad(-6.35, -6.05, -27.0, 27.0, (105, 111, 116))
    quad(6.05, 6.35, -27.0, 27.0, (105, 111, 116))
    for z in (-24.0, -12.0, 0.0, 12.0, 24.0):
        quad(-5.49, -5.21, z - 0.13, z + 0.13, (255, 126, 18))
        quad(5.21, 5.49, z - 0.13, z + 0.13, (255, 126, 18))
    quad(-3.85, 3.85, 5.8, 6.2, (105, 111, 116))
    return f"P6\n{PREVIEW_WIDTH} {PREVIEW_HEIGHT}\n255\n".encode("ascii") + bytes(pixels)


def build_composition(preview: bytes) -> bytes:
    value: dict[str, object] = {
        "camera": {
            "far_clip_m": 140.0,
            "near_clip_m": 0.1,
            "position_m": [35.0, 34.0, 48.0],
            "target_m": [0.0, 0.8, 0.0],
            "up": [0.0, 1.0, 0.0],
            "vertical_fov_degrees": 55.0,
        },
        "environment": {
            "background_luminance_cd_m2": 2000.0,
            "color_space": "rec709-d65-linear-unit-luminance",
            "mode": "analytic-clear-sky",
        },
        "exposure": {"ev100": 14.0, "white_balance_kelvin": 6500.0},
        "format": "ror-native-render-composition-v1",
        "package_id": PACKAGE_ID,
        "preview": {
            "format": "ppm-p6-rgb8",
            "height": PREVIEW_HEIGHT,
            "path": PREVIEW_PATH.as_posix(),
            "sha256": sha256_bytes(preview),
            "status": "authoring-layout-preview-not-renderer-evidence",
            "width": PREVIEW_WIDTH,
        },
        "shadow_roi": {
            "maximum_xz_m": [6.56875, 8.375],
            "minimum_xz_m": [-3.85, 5.8],
            "receiver_surface_y_m": 0.0,
            "required_intersections": [
                "rorng_a0_road_surface_mesh",
                "rorng_a0_wet_asphalt_mesh",
            ],
        },
        "sun": {
            "direction_toward_scene": [0.6, -0.64, 0.48],
            "illuminance_lux": 110000.0,
            "spectrum": "d65",
        },
        "world_aabb": {
            "maximum_m": [6.5, 3.25, 30.0],
            "minimum_m": [-6.5, -0.02, -30.0],
        },
    }
    return canonical_pretty(value)


def build_alignment() -> bytes:
    placements: list[dict[str, object]] = []

    def signed_token(value: int) -> str:
        return f"n{abs(value):02d}" if value < 0 else f"p{value:02d}"

    def placement(
        identifier: str,
        batch_mesh_id: str,
        category: str,
        position: tuple[float, float, float],
        dimensions: tuple[float, float, float],
        *,
        surface_id: str | None = None,
    ) -> None:
        placements.append(
            {
                "batch_mesh_id": batch_mesh_id,
                "category": category,
                "collision_binding": None,
                "dimensions_m": list(dimensions),
                "id": identifier,
                "position_m": list(position),
                "rotation_y_degrees": 0.0,
                "surface_id": surface_id,
            }
        )

    placement("barrier_left_rail", "rorng_a1_barrier_mesh", "barrier", (-6.2, 0.73, 0.0), (0.3, 0.3, 54.0))
    placement("barrier_right_rail", "rorng_a1_barrier_mesh", "barrier", (6.2, 0.73, 0.0), (0.3, 0.3, 54.0))
    for z in range(-24, 25, 6):
        token = signed_token(z)
        placement(f"barrier_left_post_{token}", "rorng_a1_barrier_mesh", "barrier_post", (-6.2, 0.32, float(z)), (0.36, 0.64, 0.18))
        placement(f"barrier_right_post_{token}", "rorng_a1_barrier_mesh", "barrier_post", (6.2, 0.32, float(z)), (0.36, 0.64, 0.18))
    placement(
        "curb_left",
        "rorng_a1_curb_mesh",
        "curb",
        (-4.075, 0.05, 0.0),
        (0.15, 0.14, 60.0),
        surface_id="curb_left",
    )
    placement(
        "curb_right",
        "rorng_a1_curb_mesh",
        "curb",
        (4.075, 0.05, 0.0),
        (0.15, 0.14, 60.0),
        surface_id="curb_right",
    )
    for z in (-24, -12, 0, 12, 24):
        token = signed_token(z)
        placement(f"calibration_left_{token}", "rorng_a1_calibration_marker_mesh", "calibration_marker", (-5.35, 0.595, float(z)), (0.28, 1.11, 0.26))
        placement(f"calibration_right_{token}", "rorng_a1_calibration_marker_mesh", "calibration_marker", (5.35, 0.595, float(z)), (0.28, 1.11, 0.26))
    placement("shadow_gate_left_post", "rorng_a0_road_shadow_gate_mesh", "calibration_gate", (-3.7, 1.31, 6.0), (0.3, 2.62, 0.4))
    placement("shadow_gate_right_post", "rorng_a0_road_shadow_gate_mesh", "calibration_gate", (3.7, 1.31, 6.0), (0.3, 2.62, 0.4))
    placement("shadow_gate_crossbar", "rorng_a0_road_shadow_gate_mesh", "calibration_gate", (0.0, 2.76, 6.0), (7.7, 0.28, 0.4))
    placement("thin_glass_slab", "rorng_a1_glass_slab_mesh", "refraction_witness", (0.0, 1.75, 0.0), (4.0, 3.0, 0.08))
    for z in range(-27, 28, 6):
        placement(f"lane_center_dash_{signed_token(z)}", "rorng_a1_lane_marking_mesh", "lane_marking", (0.0, 0.015, z + 1.5), (0.18, 0.002, 3.0))
    placement("lane_left_edge", "rorng_a1_lane_marking_mesh", "lane_marking", (-3.65, 0.014, 0.0), (0.14, 0.002, 58.0))
    placement("lane_right_edge", "rorng_a1_lane_marking_mesh", "lane_marking", (3.65, 0.014, 0.0), (0.14, 0.002, 58.0))
    placement("start_line", "rorng_a1_lane_marking_mesh", "lane_marking", (0.0, 0.016, -25.0), (7.44, 0.002, 0.3))
    placement("finish_line", "rorng_a1_lane_marking_mesh", "lane_marking", (0.0, 0.016, 25.0), (7.44, 0.002, 0.3))
    placements.sort(key=lambda item: str(item["id"]))

    value: dict[str, object] = {
        "collision": {
            "binding_exists": False,
            "status": "pending",
        },
        "coordinate_system": "right-handed-y-up-meters",
        "course": {
            "centerline_m": [[0.0, 0.0, -30.0], [0.0, 0.0, 30.0]],
            "driveable_visual_bounds_m": {
                "maximum": [4.0, 0.0, 30.0],
                "minimum": [-4.0, 0.0, -30.0],
            },
            "finish_center_m": [0.0, 0.016, 25.0],
            "length_m": 60.0,
            "nominal_road_width_m": 8.0,
            "start_center_m": [0.0, 0.016, -25.0],
        },
        "format": "ror-native-course-alignment-v1",
        "package_id": PACKAGE_ID,
        "placements": placements,
        "seams": [
            {
                "boundary_kind": "curb_shoulder_vertical_face",
                "left_surface": "shoulder_left",
                "lower_surface_y_m": -0.02,
                "right_surface": "curb_left",
                "seam_x_m": -4.15,
                "upper_surface_y_m": 0.12,
                "vertical_face_max_y_m": 0.12,
                "vertical_face_mesh_id": "rorng_a1_curb_mesh",
                "vertical_face_min_y_m": -0.02,
                "z_range_m": [-30.0, 30.0],
            },
            {
                "boundary_kind": "road_curb_vertical_face",
                "left_surface": "curb_left",
                "lower_surface_y_m": 0.0,
                "right_surface": "dry_asphalt",
                "seam_x_m": -4.0,
                "upper_surface_y_m": 0.12,
                "vertical_face_max_y_m": 0.12,
                "vertical_face_mesh_id": "rorng_a1_curb_mesh",
                "vertical_face_min_y_m": -0.02,
                "z_range_m": [-30.0, 30.0],
            },
            {
                "boundary_kind": "road_curb_vertical_face",
                "left_surface": "dry_asphalt",
                "lower_surface_y_m": 0.0,
                "right_surface": "curb_right",
                "seam_x_m": 4.0,
                "upper_surface_y_m": 0.12,
                "vertical_face_max_y_m": 0.12,
                "vertical_face_mesh_id": "rorng_a1_curb_mesh",
                "vertical_face_min_y_m": -0.02,
                "z_range_m": [-30.0, 30.0],
            },
            {
                "boundary_kind": "curb_shoulder_vertical_face",
                "left_surface": "curb_right",
                "lower_surface_y_m": -0.02,
                "right_surface": "shoulder_right",
                "seam_x_m": 4.15,
                "upper_surface_y_m": 0.12,
                "vertical_face_max_y_m": 0.12,
                "vertical_face_mesh_id": "rorng_a1_curb_mesh",
                "vertical_face_min_y_m": -0.02,
                "z_range_m": [-30.0, 30.0],
            },
        ],
        "surfaces": [
            {
                "classification": "raised_curb_top",
                "collision_binding": None,
                "geometry_y_range_m": [-0.02, 0.12],
                "id": "curb_left",
                "mesh_id": "rorng_a1_curb_mesh",
                "mesh_component_index": 0,
                "physics_material": None,
                "polygon_xz_m": [[-4.15, -30.0], [-4.15, 30.0], [-4.0, 30.0], [-4.0, -30.0]],
                "slope_dy_dx_dy_dz": [0.0, 0.0],
                "visual_y_m": 0.12,
            },
            {
                "classification": "raised_curb_top",
                "collision_binding": None,
                "geometry_y_range_m": [-0.02, 0.12],
                "id": "curb_right",
                "mesh_id": "rorng_a1_curb_mesh",
                "mesh_component_index": 1,
                "physics_material": None,
                "polygon_xz_m": [[4.0, -30.0], [4.0, 30.0], [4.15, 30.0], [4.15, -30.0]],
                "slope_dy_dx_dy_dz": [0.0, 0.0],
                "visual_y_m": 0.12,
            },
            {
                "classification": "dry_asphalt",
                "collision_binding": None,
                "geometry_y_range_m": [0.0, 0.0],
                "id": "dry_asphalt",
                "mesh_id": "rorng_a0_road_surface_mesh",
                "mesh_component_index": 0,
                "physics_material": None,
                "polygon_xz_m": [[-4.0, -30.0], [-4.0, 30.0], [4.0, 30.0], [4.0, -30.0]],
                "slope_dy_dx_dy_dz": [0.0, 0.0],
                "visual_y_m": 0.0,
            },
            {
                "classification": "shoulder",
                "collision_binding": None,
                "geometry_y_range_m": [-0.02, -0.02],
                "id": "shoulder_left",
                "mesh_id": "rorng_a1_shoulder_mesh",
                "mesh_component_index": 0,
                "physics_material": None,
                "polygon_xz_m": [[-6.5, -30.0], [-6.5, 30.0], [-4.15, 30.0], [-4.15, -30.0]],
                "slope_dy_dx_dy_dz": [0.0, 0.0],
                "visual_y_m": -0.02,
            },
            {
                "classification": "shoulder",
                "collision_binding": None,
                "geometry_y_range_m": [-0.02, -0.02],
                "id": "shoulder_right",
                "mesh_id": "rorng_a1_shoulder_mesh",
                "mesh_component_index": 1,
                "physics_material": None,
                "polygon_xz_m": [[4.15, -30.0], [4.15, 30.0], [6.5, 30.0], [6.5, -30.0]],
                "slope_dy_dx_dy_dz": [0.0, 0.0],
                "visual_y_m": -0.02,
            },
            {
                "classification": "wet_asphalt_visual_overlay",
                "collision_binding": None,
                "geometry_y_range_m": [0.006, 0.006],
                "id": "wet_asphalt_overlay",
                "mesh_id": "rorng_a0_wet_asphalt_mesh",
                "mesh_component_index": 0,
                "physics_material": None,
                "polygon_xz_m": [[0.2, -2.0], [0.2, 12.0], [3.8, 12.0], [3.8, -2.0]],
                "slope_dy_dx_dy_dz": [0.0, 0.0],
                "visual_y_m": 0.006,
            },
        ],
        "units": "meters",
        "visual_only": True,
    }
    return canonical_pretty(value)


def binding(texture: str, sampler: str, *, scale: tuple[float, float]) -> dict[str, object]:
    return {
        "offset": [0.0, 0.0],
        "rotation_radians": 0.0,
        "sampler": sampler,
        "scale": list(scale),
        "texture": texture,
        "uv_set": 0,
    }


def material(
    identifier: str,
    *,
    workflow: str,
    textures: dict[str, dict[str, object]],
    base_color: tuple[float, float, float, float] = (1.0, 1.0, 1.0, 1.0),
    metallic: float = 0.0,
    roughness: float = 1.0,
    emissive: tuple[float, float, float] = (0.0, 0.0, 0.0),
    emissive_strength: float = 1.0,
    depth_write: bool = True,
    index_of_refraction: float = 1.5,
    transmission_mode: str = "none",
    transmission_factor: float = 0.0,
    attenuation_color: tuple[float, float, float] = (1.0, 1.0, 1.0),
    attenuation_distance_m: float = 1.0,
    slab_thickness_m: float = 0.0,
) -> dict[str, object]:
    return {
        "alpha_cutoff": 0.5,
        "alpha_test_mode": "disabled",
        "base_color_factor": list(base_color),
        "base_color_transfer": "srgb_decode_before_filter",
        "blend_mode": "replace",
        "depth_write": depth_write,
        "double_sided": False,
        "emissive_factor": list(emissive),
        "emissive_strength": emissive_strength,
        "id": identifier,
        "index_of_refraction": index_of_refraction,
        "metallic_factor": metallic,
        "model": "pbr_metallic_roughness",
        "normal_scale": 1.0,
        "occlusion_strength": 1.0,
        "roughness_factor": roughness,
        "specular_factor": [1.0, 1.0, 1.0],
        "textures": textures,
        "transmission_mode": transmission_mode,
        "transmission_factor": transmission_factor,
        "attenuation_color": list(attenuation_color),
        "attenuation_distance_m": attenuation_distance_m,
        "slab_thickness_m": slab_thickness_m,
        "workflow": workflow,
    }


def periodic_value_noise(utility: ModuleType, x: int, y: int, size: int, cell: int, seed: int) -> int:
    if size % cell:
        raise ValueError("texture size must be divisible by every noise cell")
    cell_count = size // cell
    grid_x = x // cell
    grid_y = y // cell
    blend_x = utility._smooth_q8(x % cell, cell)
    blend_y = utility._smooth_q8(y % cell, cell)
    corners = (
        utility._noise(grid_x % cell_count, grid_y % cell_count, seed) - 128,
        utility._noise((grid_x + 1) % cell_count, grid_y % cell_count, seed) - 128,
        utility._noise(grid_x % cell_count, (grid_y + 1) % cell_count, seed) - 128,
        utility._noise((grid_x + 1) % cell_count, (grid_y + 1) % cell_count, seed) - 128,
    )
    upper = utility._lerp_q8(corners[0], corners[1], blend_x)
    lower = utility._lerp_q8(corners[2], corners[3], blend_x)
    return utility._lerp_q8(upper, lower, blend_y)


def build_texture_definitions(utility: ModuleType) -> tuple[tuple[str, str, str, str, int, int, Callable[[int, int], Pixel]], ...]:
    clamp = utility._clamp_byte

    @lru_cache(maxsize=None)
    def road_height_tile(x: int, y: int) -> int:
        coarse = periodic_value_noise(utility, x, y, ROAD_TEXTURE_SIZE, 128, 101)
        aggregate = periodic_value_noise(utility, x, y, ROAD_TEXTURE_SIZE, 32, 103)
        grit = periodic_value_noise(utility, x, y, ROAD_TEXTURE_SIZE, 8, 107)
        grain = utility._noise(x, y, 109) - 128
        return coarse // 11 + aggregate // 6 + grit // 4 + grain // 8

    def road_height(x: int, y: int) -> int:
        return road_height_tile(x % ROAD_TEXTURE_SIZE, y % ROAD_TEXTURE_SIZE)

    def road_base(x: int, y: int) -> Pixel:
        broad = periodic_value_noise(utility, x, y, ROAD_TEXTURE_SIZE, 128, 113)
        aggregate = periodic_value_noise(utility, x, y, ROAD_TEXTURE_SIZE, 32, 127)
        grit = periodic_value_noise(utility, x, y, ROAD_TEXTURE_SIZE, 8, 131)
        grain = utility._noise(x, y, 137) - 128
        value = clamp(48 + broad // 18 + aggregate // 14 + grit // 24 + grain // 70)
        return (clamp(value + 2), value, clamp(value - 2), 255)

    def road_mr(x: int, y: int) -> Pixel:
        aggregate = periodic_value_noise(utility, x, y, ROAD_TEXTURE_SIZE, 32, 139)
        grit = periodic_value_noise(utility, x, y, ROAD_TEXTURE_SIZE, 8, 149)
        roughness = max(188, min(242, 218 + aggregate // 20 + grit // 24))
        return (255, roughness, 0, 255)

    def road_normal(x: int, y: int) -> Pixel:
        dx = road_height(x + 1, y) - road_height(x - 1, y)
        dy = road_height(x, y + 1) - road_height(x, y - 1)
        return (128 + max(-42, min(42, -dx)), 128 + max(-42, min(42, -dy)), 255, 255)

    @lru_cache(maxsize=None)
    def wet_height_tile(x: int, y: int) -> int:
        film = periodic_value_noise(utility, x, y, WET_TEXTURE_SIZE, 128, 151)
        flow = periodic_value_noise(utility, x, y, WET_TEXTURE_SIZE, 32, 157)
        micro = periodic_value_noise(utility, x, y, WET_TEXTURE_SIZE, 8, 163)
        ripple = utility._triangle_wave(x + y * 3 + flow // 2, 64) - 32
        return film // 30 + flow // 24 + micro // 16 + ripple * max(0, flow + 48) // 720

    def wet_height(x: int, y: int) -> int:
        return wet_height_tile(x % WET_TEXTURE_SIZE, y % WET_TEXTURE_SIZE)

    def wet_base(x: int, y: int) -> Pixel:
        film = periodic_value_noise(utility, x, y, WET_TEXTURE_SIZE, 128, 167)
        flow = periodic_value_noise(utility, x, y, WET_TEXTURE_SIZE, 32, 173)
        value = clamp(29 - film // 22 - flow // 36)
        return (clamp(value - 5), value, clamp(value + 7), 255)

    def wet_normal(x: int, y: int) -> Pixel:
        dx = wet_height(x + 1, y) - wet_height(x - 1, y)
        dy = wet_height(x, y + 1) - wet_height(x, y - 1)
        return (128 + max(-17, min(17, -dx)), 128 + max(-17, min(17, -dy)), 255, 255)

    def wet_specular(x: int, y: int) -> Pixel:
        film = periodic_value_noise(utility, x, y, WET_TEXTURE_SIZE, 128, 179)
        flow = periodic_value_noise(utility, x, y, WET_TEXTURE_SIZE, 32, 181)
        value = max(222, min(253, 239 + film // 20 + flow // 28))
        return (value, value, min(255, value + 2), 255)

    @lru_cache(maxsize=None)
    def shoulder_height_tile(x: int, y: int) -> int:
        stone = periodic_value_noise(utility, x, y, SHOULDER_TEXTURE_SIZE, 16, 191)
        grain = utility._noise(x, y, 193) - 128
        return stone // 3 + grain // 4

    def shoulder_height(x: int, y: int) -> int:
        return shoulder_height_tile(x % SHOULDER_TEXTURE_SIZE, y % SHOULDER_TEXTURE_SIZE)

    def shoulder_base(x: int, y: int) -> Pixel:
        soil = periodic_value_noise(utility, x, y, SHOULDER_TEXTURE_SIZE, 64, 197)
        gravel = periodic_value_noise(utility, x, y, SHOULDER_TEXTURE_SIZE, 16, 199)
        value = clamp(82 + soil // 12 + gravel // 8)
        return (clamp(value + 10), clamp(value + 4), clamp(value - 8), 255)

    def shoulder_mr(x: int, y: int) -> Pixel:
        gravel = periodic_value_noise(utility, x, y, SHOULDER_TEXTURE_SIZE, 16, 211)
        return (255, max(210, min(250, 232 + gravel // 16)), 0, 255)

    def shoulder_normal(x: int, y: int) -> Pixel:
        dx = shoulder_height(x + 1, y) - shoulder_height(x - 1, y)
        dy = shoulder_height(x, y + 1) - shoulder_height(x, y - 1)
        return (128 + max(-48, min(48, -dx)), 128 + max(-48, min(48, -dy)), 255, 255)

    def barrier_base(x: int, y: int) -> Pixel:
        streak = periodic_value_noise(utility, x, y, 256, 32, 223)
        value = clamp(132 + streak // 9)
        return (clamp(value - 7), value, clamp(value + 9), 255)

    def barrier_mr(x: int, y: int) -> Pixel:
        grain = periodic_value_noise(utility, x, y, 256, 16, 227)
        return (255, max(88, min(164, 122 + grain // 7)), 220, 255)

    def barrier_normal(x: int, y: int) -> Pixel:
        grain_x = periodic_value_noise(utility, x, y, 256, 8, 229) // 12
        grain_y = periodic_value_noise(utility, x, y, 256, 8, 233) // 12
        return (128 + grain_x, 128 + grain_y, 255, 255)

    def curb_base(x: int, y: int) -> Pixel:
        red = ((y // 32) % 2) == 0
        grain = periodic_value_noise(utility, x, y, 256, 16, 239) // 16
        if red:
            return (clamp(164 + grain), clamp(42 + grain // 2), clamp(35 + grain // 3), 255)
        return (clamp(224 + grain), clamp(218 + grain), clamp(198 + grain), 255)

    def curb_normal(x: int, y: int) -> Pixel:
        return (
            128 + periodic_value_noise(utility, x, y, 256, 8, 241) // 14,
            128 + periodic_value_noise(utility, x, y, 256, 8, 251) // 14,
            255,
            255,
        )

    def lane_base(x: int, y: int) -> Pixel:
        wear = periodic_value_noise(utility, x, y, 128, 16, 257)
        crack = abs(periodic_value_noise(utility, x, y, 128, 8, 263)) > 114
        value = 174 if crack else clamp(235 + wear // 18)
        return (value, clamp(value - 4), clamp(value - 22), 255)

    def lane_normal(x: int, y: int) -> Pixel:
        return (
            128 + periodic_value_noise(utility, x, y, 128, 8, 269) // 18,
            128 + periodic_value_noise(utility, x, y, 128, 8, 271) // 18,
            255,
            255,
        )

    def calibration_base(x: int, y: int) -> Pixel:
        center = abs(x * 2 - 15) + abs(y * 2 - 15)
        return (255, max(82, 174 - center * 3), 18, 255)

    def calibration_emissive(x: int, y: int) -> Pixel:
        center = abs(x * 2 - 15) + abs(y * 2 - 15)
        intensity = max(92, 255 - center * 7)
        return (intensity, max(26, intensity // 3), 4, 255)

    def calibration_specular(x: int, y: int) -> Pixel:
        value = max(156, 247 - (abs(x - 7) + abs(y - 7)) * 7)
        return (value, max(140, value - 14), max(120, value - 34), 255)

    return (
        ("rorng_a1_barrier_base", "base_color", "srgb", "barrier_base", 256, 256, barrier_base),
        ("rorng_a1_barrier_metallic_roughness", "metallic_roughness", "linear", "barrier_metallic_roughness", 256, 256, barrier_mr),
        ("rorng_a1_barrier_normal", "normal", "linear", "barrier_normal", 256, 256, barrier_normal),
        ("rorng_a1_calibration_base", "base_color", "srgb", "calibration_base", 16, 16, calibration_base),
        ("rorng_a1_calibration_emissive", "emissive", "srgb", "calibration_emissive", 16, 16, calibration_emissive),
        ("rorng_a1_calibration_specular", "specular", "linear", "calibration_specular", 16, 16, calibration_specular),
        ("rorng_a1_curb_base", "base_color", "srgb", "curb_base", 256, 256, curb_base),
        ("rorng_a1_curb_normal", "normal", "linear", "curb_normal", 256, 256, curb_normal),
        ("rorng_a1_lane_base", "base_color", "srgb", "lane_base", 128, 128, lane_base),
        ("rorng_a1_lane_normal", "normal", "linear", "lane_normal", 128, 128, lane_normal),
        ("rorng_a1_road_base", "base_color", "srgb", "road_base", ROAD_TEXTURE_SIZE, ROAD_TEXTURE_SIZE, road_base),
        ("rorng_a1_road_metallic_roughness", "metallic_roughness", "linear", "road_metallic_roughness", ROAD_TEXTURE_SIZE, ROAD_TEXTURE_SIZE, road_mr),
        ("rorng_a1_road_normal", "normal", "linear", "road_normal", ROAD_TEXTURE_SIZE, ROAD_TEXTURE_SIZE, road_normal),
        ("rorng_a1_shoulder_base", "base_color", "srgb", "shoulder_base", SHOULDER_TEXTURE_SIZE, SHOULDER_TEXTURE_SIZE, shoulder_base),
        ("rorng_a1_shoulder_metallic_roughness", "metallic_roughness", "linear", "shoulder_metallic_roughness", SHOULDER_TEXTURE_SIZE, SHOULDER_TEXTURE_SIZE, shoulder_mr),
        ("rorng_a1_shoulder_normal", "normal", "linear", "shoulder_normal", SHOULDER_TEXTURE_SIZE, SHOULDER_TEXTURE_SIZE, shoulder_normal),
        ("rorng_a1_wet_base", "base_color", "srgb", "wet_base", WET_TEXTURE_SIZE, WET_TEXTURE_SIZE, wet_base),
        ("rorng_a1_wet_normal", "normal", "linear", "wet_normal", WET_TEXTURE_SIZE, WET_TEXTURE_SIZE, wet_normal),
        ("rorng_a1_wet_specular", "specular", "linear", "wet_specular", WET_TEXTURE_SIZE, WET_TEXTURE_SIZE, wet_specular),
    )


def build_sources(repo_root: Path) -> None:
    utility = load_authoring_utility(repo_root)
    source_root = repo_root / SOURCE_DIRECTORY
    texture_root = source_root / "textures"
    texture_root.mkdir(parents=True, exist_ok=True)
    glb = build_glb(utility)
    preview = build_preview()
    composition = build_composition(preview)
    alignment = build_alignment()

    texture_payloads: dict[str, bytes] = {}
    texture_specs: list[tuple[str, str, str, tuple[tuple[str, int, int], ...]]] = []
    for identifier, role, color_space, stem, width, height, pixel in build_texture_definitions(utility):
        records: list[tuple[str, int, int]] = []
        for level, (mip_width, mip_height, pixels) in enumerate(
            utility._mip_chain(
                width,
                height,
                pixel,
                srgb=color_space == "srgb",
                positive_z_normal=role == "normal",
            )
        ):
            name = f"{stem}_mip{level}.tga"
            texture_payloads[name] = utility.write_tga_rgba(mip_width, mip_height, pixels)
            records.append((name, mip_width, mip_height))
        texture_specs.append((identifier, role, color_space, tuple(records)))

    source_root.mkdir(parents=True, exist_ok=True)
    (repo_root / GLB_PATH).write_bytes(glb)
    (repo_root / PREVIEW_PATH).write_bytes(preview)
    (repo_root / COMPOSITION_PATH).write_bytes(composition)
    (repo_root / ALIGNMENT_PATH).write_bytes(alignment)
    for name, payload in texture_payloads.items():
        (texture_root / name).write_bytes(payload)

    generator_relative = Path(__file__).resolve().relative_to(repo_root.resolve()).as_posix()
    generator_hash = sha256_bytes(Path(__file__).read_bytes())

    def source_record(relative: Path, payload: bytes) -> dict[str, object]:
        return {"path": relative.as_posix(), "sha256": sha256_bytes(payload)}

    textures: list[dict[str, object]] = []
    for identifier, role, color_space, records in texture_specs:
        textures.append(
            {
                "color_space": color_space,
                "format": "rgba8_unorm",
                "id": identifier,
                "mips": [
                    {
                        "height": height,
                        "path": (SOURCE_DIRECTORY / "textures" / name).as_posix(),
                        "sha256": sha256_bytes(texture_payloads[name]),
                        "width": width,
                    }
                    for name, width, height in records
                ],
                "role": role,
            }
        )

    repeat_sampler = "rorng_a1_mipped_repeat_sampler"
    clamp_sampler = "rorng_a1_mipped_clamp_sampler"
    materials = [
        material(
            "rorng_a1_barrier_material",
            workflow="metallic_roughness",
            metallic=1.0,
            roughness=1.0,
            textures={
                "base_color": binding("rorng_a1_barrier_base", repeat_sampler, scale=(2.0, 12.0)),
                "metallic_roughness": binding("rorng_a1_barrier_metallic_roughness", repeat_sampler, scale=(2.0, 12.0)),
                "normal": binding("rorng_a1_barrier_normal", repeat_sampler, scale=(2.0, 12.0)),
            },
        ),
        material(
            "rorng_a1_calibration_material",
            workflow="specular",
            roughness=0.16,
            emissive=(1.0, 0.32, 0.03),
            emissive_strength=3.5,
            textures={
                "base_color": binding("rorng_a1_calibration_base", clamp_sampler, scale=(1.0, 1.0)),
                "emissive": binding("rorng_a1_calibration_emissive", clamp_sampler, scale=(1.0, 1.0)),
                "specular": binding("rorng_a1_calibration_specular", clamp_sampler, scale=(1.0, 1.0)),
            },
        ),
        material(
            "rorng_a1_curb_material",
            workflow="metallic_roughness",
            roughness=0.82,
            textures={
                "base_color": binding("rorng_a1_curb_base", repeat_sampler, scale=(1.0, 15.0)),
                "normal": binding("rorng_a1_curb_normal", repeat_sampler, scale=(1.0, 15.0)),
            },
        ),
        material(
            "rorng_a1_glass_material",
            workflow="specular",
            base_color=(0.0, 0.0, 0.0, 1.0),
            roughness=0.025,
            depth_write=False,
            index_of_refraction=1.52,
            transmission_mode="thin_parallel_slab",
            transmission_factor=0.96,
            attenuation_color=(0.82, 0.94, 0.98),
            attenuation_distance_m=0.75,
            slab_thickness_m=0.08,
            textures={},
        ),
        material(
            "rorng_a1_lane_marking_material",
            workflow="metallic_roughness",
            roughness=0.58,
            textures={
                "base_color": binding("rorng_a1_lane_base", repeat_sampler, scale=(1.0, 3.0)),
                "normal": binding("rorng_a1_lane_normal", repeat_sampler, scale=(1.0, 3.0)),
            },
        ),
        material(
            "rorng_a1_road_surface_material",
            workflow="metallic_roughness",
            metallic=1.0,
            roughness=1.0,
            textures={
                "base_color": binding("rorng_a1_road_base", repeat_sampler, scale=(2.0, 16.0)),
                "metallic_roughness": binding("rorng_a1_road_metallic_roughness", repeat_sampler, scale=(2.0, 16.0)),
                "normal": binding("rorng_a1_road_normal", repeat_sampler, scale=(2.0, 16.0)),
            },
        ),
        material(
            "rorng_a1_shoulder_material",
            workflow="metallic_roughness",
            metallic=1.0,
            roughness=1.0,
            textures={
                "base_color": binding("rorng_a1_shoulder_base", repeat_sampler, scale=(3.0, 18.0)),
                "metallic_roughness": binding("rorng_a1_shoulder_metallic_roughness", repeat_sampler, scale=(3.0, 18.0)),
                "normal": binding("rorng_a1_shoulder_normal", repeat_sampler, scale=(3.0, 18.0)),
            },
        ),
        material(
            "rorng_a1_wet_asphalt_material",
            workflow="specular",
            roughness=0.07,
            textures={
                "base_color": binding("rorng_a1_wet_base", repeat_sampler, scale=(1.0, 4.0)),
                "normal": binding("rorng_a1_wet_normal", repeat_sampler, scale=(1.0, 4.0)),
                "specular": binding("rorng_a1_wet_specular", repeat_sampler, scale=(1.0, 4.0)),
            },
        ),
    ]

    identity = [
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0,
    ]
    mesh_materials = {
        "rorng_a1_barrier_mesh": "rorng_a1_barrier_material",
        "rorng_a1_calibration_marker_mesh": "rorng_a1_calibration_material",
        "rorng_a1_curb_mesh": "rorng_a1_curb_material",
        "rorng_a1_glass_slab_mesh": "rorng_a1_glass_material",
        "rorng_a1_lane_marking_mesh": "rorng_a1_lane_marking_material",
        "rorng_a0_road_shadow_gate_mesh": "rorng_a1_barrier_material",
        "rorng_a0_road_surface_mesh": "rorng_a1_road_surface_material",
        "rorng_a1_shoulder_mesh": "rorng_a1_shoulder_material",
        "rorng_a0_wet_asphalt_mesh": "rorng_a1_wet_asphalt_material",
    }
    passive = {"rorng_a1_glass_slab_mesh", "rorng_a1_lane_marking_mesh"}
    receivers = {
        "rorng_a0_road_surface_mesh",
        "rorng_a1_shoulder_mesh",
        "rorng_a0_wet_asphalt_mesh",
    }
    manifest_meshes = []
    for mesh_id, material_id in sorted(mesh_materials.items()):
        if mesh_id in passive:
            flags: list[str] = []
        elif mesh_id in receivers:
            flags = ["receives_shadow", "visible_in_reflections"]
        else:
            flags = ["casts_shadow", "receives_shadow", "visible_in_reflections"]
        manifest_meshes.append(
            {
                "id": mesh_id,
                "instance_flags": flags,
                "material": material_id,
                "node": mesh_id,
                "object_id": mesh_id.replace("_mesh", "_object"),
                "render_from_object": identity,
            }
        )

    manifest: dict[str, object] = {
        "claims": {
            "ambient_occlusion": False,
            "collision": False,
            "lods": False,
            "native_terrain": False,
            "visual_only": True,
        },
        "format": "ror-native-render-source-v2",
        "materials": materials,
        "meshes": manifest_meshes,
        "outputs": {
            "package_path": PACKAGE_PATH.as_posix(),
            "report_path": REPORT_PATH.as_posix(),
        },
        "package": {
            "author": "Rigs of Rods contributors",
            "creation_attestation": "Project-original A1 geometry, textures, material declarations, and alignment data were independently authored for V2. The generator reuses only hash-pinned project-owned A0 authoring utility code; no geometry, texture, material-script, or shader bytes were copied from A0, legacy content, or another simulator.",
            "dimensions_m": [13.0, 3.27, 60.0],
            "display_name": "A1 native 60 metre visual calibration course",
            "id": PACKAGE_ID,
            "license": "GPL-3.0-or-later",
            "modified": False,
            "origin_class": "project_original",
            "source_revision": "sha256-pinned-source-set-v2-thin-slab-transmission",
            "source_uri": "https://github.com/RigsOfRods/rigs-of-rods",
        },
        "samplers": [
            {
                "address_u": "clamp_to_edge",
                "address_v": "clamp_to_edge",
                "address_w": "clamp_to_edge",
                "anisotropy_enabled": True,
                "border_color": [0.0, 0.0, 0.0, 0.0],
                "compare_enabled": False,
                "compare_operation": "always",
                "id": clamp_sampler,
                "magnification_filter": "linear",
                "maximum_anisotropy": 4.0,
                "maximum_lod": 4.0,
                "minimum_lod": 0.0,
                "minification_filter": "linear",
                "mip_filter": "linear",
                "mip_lod_bias": 0.0,
            },
            {
                "address_u": "repeat",
                "address_v": "repeat",
                "address_w": "repeat",
                "anisotropy_enabled": True,
                "border_color": [0.0, 0.0, 0.0, 0.0],
                "compare_enabled": False,
                "compare_operation": "always",
                "id": repeat_sampler,
                "magnification_filter": "linear",
                "maximum_anisotropy": 4.0,
                "maximum_lod": 10.0,
                "minimum_lod": 0.0,
                "minification_filter": "linear",
                "mip_filter": "linear",
                "mip_lod_bias": 0.0,
            },
        ],
        "source": {
            "composition": source_record(COMPOSITION_PATH, composition),
            "coordinate_system": "right-handed-y-up-meters",
            "generator": {"path": generator_relative, "sha256": generator_hash},
            "glb": source_record(GLB_PATH, glb),
            "tangent_basis": "tangent-w-times-cross-normal-tangent",
            "uv_origin": "upper-left",
        },
        "textures": textures,
    }
    (repo_root / MANIFEST_PATH).write_bytes(canonical_pretty(manifest))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path.cwd())
    args = parser.parse_args()
    build_sources(args.repo_root.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
