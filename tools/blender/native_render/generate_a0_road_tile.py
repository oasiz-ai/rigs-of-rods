#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the independently authored 12 m A0 native road-tile sources."""

from __future__ import annotations

import argparse
from functools import lru_cache
import hashlib
import json
import math
from pathlib import Path
import struct
from typing import Callable, Iterable


GENERATOR_ID = "ror-native-a0-road-tile-generator-v1"
PACKAGE_ID = "rorng_a0_road_tile_12m"
SOURCE_DIRECTORY = Path("content-source/native_render/a0_road_tile_12m")
MANIFEST_PATH = SOURCE_DIRECTORY / f"{PACKAGE_ID}.native.json"
GLB_PATH = SOURCE_DIRECTORY / f"{PACKAGE_ID}.glb"
COMPOSITION_PATH = SOURCE_DIRECTORY / f"{PACKAGE_ID}.composition.json"
PREVIEW_PATH = SOURCE_DIRECTORY / f"{PACKAGE_ID}.composition.ppm"
PREVIEW_WIDTH = 640
PREVIEW_HEIGHT = 360
SURFACE_TEXTURE_SIZE = 512


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def canonical_pretty(value: dict[str, object]) -> bytes:
    return (json.dumps(value, ensure_ascii=True, indent=2, sort_keys=True) + "\n").encode("ascii")


def canonical_json(value: dict[str, object]) -> bytes:
    return json.dumps(value, ensure_ascii=True, sort_keys=True, separators=(",", ":")).encode("ascii")


def align4(value: bytearray) -> None:
    while len(value) % 4:
        value.append(0)


def write_tga_rgba(width: int, height: int, pixels: Iterable[tuple[int, int, int, int]]) -> bytes:
    values = tuple(pixels)
    if len(values) != width * height:
        raise RuntimeError("TGA pixel count is incorrect")
    header = struct.pack(
        "<BBBHHBHHHHBB",
        0,
        0,
        2,
        0,
        0,
        0,
        0,
        0,
        width,
        height,
        32,
        0x28,
    )
    payload = bytearray(header)
    for red, green, blue, alpha in values:
        if any(component < 0 or component > 255 for component in (red, green, blue, alpha)):
            raise RuntimeError("TGA component is outside uint8")
        payload.extend((blue, green, red, alpha))
    return bytes(payload)


def quad(
    x0: float,
    x1: float,
    y: float,
    z0: float,
    z1: float,
    *,
    uv_v1: float = 1.0,
) -> tuple[list[tuple[float, float, float]], list[tuple[float, float]], list[int]]:
    positions = [
        (x0, y, z0),
        (x0, y, z1),
        (x1, y, z1),
        (x1, y, z0),
    ]
    texcoords = [(0.0, 0.0), (0.0, uv_v1), (1.0, uv_v1), (1.0, 0.0)]
    return positions, texcoords, [0, 1, 2, 0, 2, 3]


def _dot3(left: tuple[float, float, float], right: tuple[float, float, float]) -> float:
    return sum(left[index] * right[index] for index in range(3))


def _cross3(
    left: tuple[float, float, float], right: tuple[float, float, float]
) -> tuple[float, float, float]:
    return (
        left[1] * right[2] - left[2] * right[1],
        left[2] * right[0] - left[0] * right[2],
        left[0] * right[1] - left[1] * right[0],
    )


def _normalize3(value: tuple[float, float, float]) -> tuple[float, float, float]:
    length = math.sqrt(_dot3(value, value))
    if length <= 1e-12:
        raise ValueError("cannot normalize a zero-length vector")
    return tuple(component / length for component in value)


def derive_tangents(
    positions: list[tuple[float, float, float]],
    normals: list[tuple[float, float, float]],
    texcoords: list[tuple[float, float]],
    indices: list[int],
) -> list[tuple[float, float, float, float]]:
    """Derive tangent.xyz from increasing U and w from B=w*cross(N,T)."""

    tangent_sums = [[0.0, 0.0, 0.0] for _ in positions]
    bitangent_sums = [[0.0, 0.0, 0.0] for _ in positions]
    for triangle in range(0, len(indices), 3):
        a, b, c = indices[triangle : triangle + 3]
        p0, p1, p2 = positions[a], positions[b], positions[c]
        uv0, uv1, uv2 = texcoords[a], texcoords[b], texcoords[c]
        edge1 = tuple(p1[axis] - p0[axis] for axis in range(3))
        edge2 = tuple(p2[axis] - p0[axis] for axis in range(3))
        du1, dv1 = uv1[0] - uv0[0], uv1[1] - uv0[1]
        du2, dv2 = uv2[0] - uv0[0], uv2[1] - uv0[1]
        determinant = du1 * dv2 - dv1 * du2
        if abs(determinant) <= 1e-12:
            raise ValueError("mesh contains a degenerate UV triangle")
        reciprocal = 1.0 / determinant
        tangent = tuple(
            (edge1[axis] * dv2 - edge2[axis] * dv1) * reciprocal
            for axis in range(3)
        )
        bitangent = tuple(
            (edge2[axis] * du1 - edge1[axis] * du2) * reciprocal
            for axis in range(3)
        )
        for vertex in (a, b, c):
            for axis in range(3):
                tangent_sums[vertex][axis] += tangent[axis]
                bitangent_sums[vertex][axis] += bitangent[axis]

    result: list[tuple[float, float, float, float]] = []
    for normal, tangent_sum, bitangent_sum in zip(
        normals, tangent_sums, bitangent_sums
    ):
        tangent_projection = _dot3(normal, tuple(tangent_sum))
        tangent = _normalize3(
            tuple(
                tangent_sum[axis] - normal[axis] * tangent_projection
                for axis in range(3)
            )
        )
        handedness = (
            1.0
            if _dot3(_cross3(normal, tangent), tuple(bitangent_sum)) > 0.0
            else -1.0
        )
        result.append((*tangent, handedness))
    return result


def box_mesh(
    x0: float,
    x1: float,
    y0: float,
    y1: float,
    z0: float,
    z1: float,
) -> tuple[
    list[tuple[float, float, float]],
    list[tuple[float, float, float]],
    list[tuple[float, float, float, float]],
    list[tuple[float, float]],
    list[int],
]:
    """Create a closed hard-edged box with UV-derived tangent bases."""

    faces = (
        # positions, outward normal
        (((x0, y1, z0), (x0, y1, z1), (x1, y1, z1), (x1, y1, z0)), (0.0, 1.0, 0.0)),
        (((x0, y0, z1), (x0, y0, z0), (x1, y0, z0), (x1, y0, z1)), (0.0, -1.0, 0.0)),
        (((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)), (0.0, 0.0, 1.0)),
        (((x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0)), (0.0, 0.0, -1.0)),
        (((x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1)), (1.0, 0.0, 0.0)),
        (((x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)), (-1.0, 0.0, 0.0)),
    )
    positions: list[tuple[float, float, float]] = []
    normals: list[tuple[float, float, float]] = []
    texcoords: list[tuple[float, float]] = []
    indices: list[int] = []
    for face_positions, normal in faces:
        base = len(positions)
        positions.extend(face_positions)
        normals.extend((normal,) * 4)
        texcoords.extend(((0.0, 0.0), (0.0, 1.0), (1.0, 1.0), (1.0, 0.0)))
        indices.extend((base, base + 1, base + 2, base, base + 2, base + 3))
    tangents = derive_tangents(positions, normals, texcoords, indices)
    return positions, normals, tangents, texcoords, indices


def joined_box_mesh(
    boxes: tuple[tuple[float, float, float, float, float, float], ...],
) -> tuple[
    list[tuple[float, float, float]],
    list[tuple[float, float, float]],
    list[tuple[float, float, float, float]],
    list[tuple[float, float]],
    list[int],
]:
    """Join independently closed boxes into one deterministic mesh record."""

    positions: list[tuple[float, float, float]] = []
    normals: list[tuple[float, float, float]] = []
    tangents: list[tuple[float, float, float, float]] = []
    texcoords: list[tuple[float, float]] = []
    indices: list[int] = []
    for bounds in boxes:
        part_positions, part_normals, part_tangents, part_uvs, part_indices = (
            box_mesh(*bounds)
        )
        base = len(positions)
        positions.extend(part_positions)
        normals.extend(part_normals)
        tangents.extend(part_tangents)
        texcoords.extend(part_uvs)
        indices.extend(base + value for value in part_indices)
    return positions, normals, tangents, texcoords, indices


def reflector_mesh() -> tuple[list[tuple[float, float, float]], list[tuple[float, float]], list[int]]:
    positions: list[tuple[float, float, float]] = []
    texcoords: list[tuple[float, float]] = []
    indices: list[int] = []
    for z in (-4.0, 0.0, 4.0):
        for x in (-2.2, 2.2):
            source_positions, source_uvs, source_indices = quad(
                x - 0.1,
                x + 0.1,
                0.03,
                z - 0.16,
                z + 0.16,
            )
            base = len(positions)
            positions.extend(source_positions)
            texcoords.extend(source_uvs)
            indices.extend(base + value for value in source_indices)
    return positions, texcoords, indices


def build_glb() -> bytes:
    lane_positions, lane_uvs, lane_indices = quad(-0.12, 0.12, 0.012, -6.0, 6.0)
    reflector_positions, reflector_uvs, reflector_indices = reflector_mesh()
    road_positions, road_uvs, road_indices = quad(-3.0, 3.0, 0.0, -6.0, 6.0)
    wet_positions, wet_uvs, wet_indices = quad(0.35, 2.75, 0.006, -5.3, 5.3)
    gate_positions, gate_normals, gate_tangents, gate_uvs, gate_indices = (
        joined_box_mesh(
            (
                (-1.8, -1.55, 0.0, 1.2, -1.65, -1.35),
                (1.55, 1.8, 0.0, 1.2, -1.65, -1.35),
                (-1.8, 1.8, 1.2, 1.45, -1.65, -1.35),
            )
        )
    )

    def horizontal_mesh(
        positions: list[tuple[float, float, float]],
        texcoords: list[tuple[float, float]],
        indices: list[int],
        material_index: int,
    ) -> dict[str, object]:
        normals = [(0.0, 1.0, 0.0)] * len(positions)
        return {
            "indices": indices,
            "material": material_index,
            "normals": normals,
            "positions": positions,
            "tangents": derive_tangents(positions, normals, texcoords, indices),
            "texcoords": texcoords,
        }

    meshes = {
        "rorng_a0_lane_decal_mesh": horizontal_mesh(lane_positions, lane_uvs, lane_indices, 0),
        "rorng_a0_reflector_mesh": horizontal_mesh(reflector_positions, reflector_uvs, reflector_indices, 1),
        "rorng_a0_road_shadow_gate_mesh": {
            "indices": gate_indices,
            "material": 2,
            "normals": gate_normals,
            "positions": gate_positions,
            "tangents": gate_tangents,
            "texcoords": gate_uvs,
        },
        "rorng_a0_road_surface_mesh": horizontal_mesh(road_positions, road_uvs, road_indices, 2),
        "rorng_a0_wet_asphalt_mesh": horizontal_mesh(wet_positions, wet_uvs, wet_indices, 3),
    }
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
        align4(binary)
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
            unpacked = packer.unpack(packed)
            packed_values.append(tuple(float(component) for component in unpacked))
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
        material_index = mesh["material"]
        assert isinstance(positions, list)
        assert isinstance(normals, list)
        assert isinstance(tangents, list)
        assert isinstance(texcoords, list)
        assert isinstance(indices, list)
        assert isinstance(material_index, int)
        position_accessor = append_accessor(positions, 5126, "VEC3", 34962, bounds=True)
        normal_accessor = append_accessor(normals, 5126, "VEC3", 34962)
        tangent_accessor = append_accessor(tangents, 5126, "VEC4", 34962)
        texcoord_accessor = append_accessor(texcoords, 5126, "VEC2", 34962)
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
                            "TEXCOORD_0": texcoord_accessor,
                        },
                        "indices": index_accessor,
                        "material": material_index,
                        "mode": 4,
                    }
                ],
            }
        )

    document: dict[str, object] = {
        "accessors": accessors,
        "asset": {"generator": GENERATOR_ID, "version": "2.0"},
        "bufferViews": views,
        "buffers": [{"byteLength": len(binary)}],
        "materials": [
            {"name": "rorng_a0_lane_decal_material"},
            {"name": "rorng_a0_reflector_material"},
            {"name": "rorng_a0_road_surface_material"},
            {"name": "rorng_a0_wet_asphalt_material"},
        ],
        "meshes": document_meshes,
        "nodes": [
            {"mesh": index, "name": mesh["name"]}
            for index, mesh in enumerate(document_meshes)
        ],
        "scene": 0,
        "scenes": [{"nodes": list(range(len(document_meshes)))}],
    }
    json_payload = bytearray(canonical_json(document))
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


def build_composition_preview() -> bytes:
    """Create a deterministic framing preview, not renderer evidence."""

    width, height = PREVIEW_WIDTH, PREVIEW_HEIGHT
    pixels = bytearray(width * height * 3)
    for y in range(height):
        interpolation = y / max(1, height - 1)
        color = (
            int(38 + 28 * interpolation),
            int(55 + 35 * interpolation),
            int(76 + 40 * interpolation),
        )
        for x in range(width):
            offset = (y * width + x) * 3
            pixels[offset : offset + 3] = bytes(color)

    camera = (8.0, 7.0, 10.0)
    target = (0.0, 0.0, -0.2)
    forward = _normalize3(tuple(target[index] - camera[index] for index in range(3)))
    right = _normalize3(_cross3(forward, (0.0, 1.0, 0.0)))
    up = _cross3(right, forward)
    tangent = math.tan(math.radians(50.0) * 0.5)
    aspect = width / height

    def project(point: tuple[float, float, float]) -> tuple[float, float]:
        relative = tuple(point[index] - camera[index] for index in range(3))
        depth = _dot3(relative, forward)
        if depth <= 0.1:
            raise ValueError("preview point lies behind the camera")
        ndc_x = _dot3(relative, right) / (depth * tangent * aspect)
        ndc_y = _dot3(relative, up) / (depth * tangent)
        return ((ndc_x * 0.5 + 0.5) * (width - 1), (0.5 - ndc_y * 0.5) * (height - 1))

    def triangle(
        points: tuple[tuple[float, float], tuple[float, float], tuple[float, float]],
        color: tuple[int, int, int],
        alpha: int = 255,
    ) -> None:
        (x0, y0), (x1, y1), (x2, y2) = points
        minimum_x = max(0, int(math.floor(min(x0, x1, x2))))
        maximum_x = min(width - 1, int(math.ceil(max(x0, x1, x2))))
        minimum_y = max(0, int(math.floor(min(y0, y1, y2))))
        maximum_y = min(height - 1, int(math.ceil(max(y0, y1, y2))))

        def edge(
            a: tuple[float, float], b: tuple[float, float], p: tuple[float, float]
        ) -> float:
            return (p[0] - a[0]) * (b[1] - a[1]) - (p[1] - a[1]) * (b[0] - a[0])

        area = edge((x0, y0), (x1, y1), (x2, y2))
        if abs(area) <= 1e-9:
            return
        for pixel_y in range(minimum_y, maximum_y + 1):
            for pixel_x in range(minimum_x, maximum_x + 1):
                sample = (pixel_x + 0.5, pixel_y + 0.5)
                weights = (
                    edge((x1, y1), (x2, y2), sample),
                    edge((x2, y2), (x0, y0), sample),
                    edge((x0, y0), (x1, y1), sample),
                )
                if not (
                    all(value >= 0.0 for value in weights)
                    or all(value <= 0.0 for value in weights)
                ):
                    continue
                offset = (pixel_y * width + pixel_x) * 3
                for channel in range(3):
                    pixels[offset + channel] = (
                        color[channel] * alpha
                        + pixels[offset + channel] * (255 - alpha)
                        + 127
                    ) // 255

    def world_quad(
        points: tuple[
            tuple[float, float, float],
            tuple[float, float, float],
            tuple[float, float, float],
            tuple[float, float, float],
        ],
        color: tuple[int, int, int],
        alpha: int = 255,
    ) -> None:
        projected = tuple(project(point) for point in points)
        triangle((projected[0], projected[1], projected[2]), color, alpha)
        triangle((projected[0], projected[2], projected[3]), color, alpha)

    world_quad(((-3.0, 0.0, -6.0), (-3.0, 0.0, 6.0), (3.0, 0.0, 6.0), (3.0, 0.0, -6.0)), (49, 53, 57))
    world_quad(((0.35, 0.006, -5.3), (0.35, 0.006, 5.3), (2.75, 0.006, 5.3), (2.75, 0.006, -5.3)), (30, 48, 61))
    # A deliberately obvious wet highlight guide in the authoring preview.
    world_quad(((1.45, 0.008, -5.0), (1.75, 0.008, 4.8), (2.18, 0.008, 4.8), (1.88, 0.008, -5.0)), (118, 161, 186), 135)
    # Exact projected gate shadow ROI for sun direction (0.60,-0.64,0.48).
    world_quad(((-1.8, 0.01, -1.65), (-1.8, 0.01, -1.35), (3.159375, 0.01, -0.2625), (3.159375, 0.01, -0.5625)), (11, 14, 18), 185)
    for z0 in (-5.5, -3.5, -1.5, 0.5, 2.5, 4.5):
        world_quad(((-0.12, 0.018, z0), (-0.12, 0.018, z0 + 1.0), (0.12, 0.018, z0 + 1.0), (0.12, 0.018, z0)), (244, 239, 207))
    for z in (-4.0, 0.0, 4.0):
        for x in (-2.2, 2.2):
            world_quad(((x - 0.1, 0.03, z - 0.16), (x - 0.1, 0.03, z + 0.16), (x + 0.1, 0.03, z + 0.16), (x + 0.1, 0.03, z - 0.16)), (255, 126, 18))
    # Gate front and crossbar top, kept last so the caster reads clearly.
    world_quad(((-1.8, 0.0, -1.35), (-1.8, 1.2, -1.35), (-1.55, 1.2, -1.35), (-1.55, 0.0, -1.35)), (105, 111, 116))
    world_quad(((1.55, 0.0, -1.35), (1.55, 1.2, -1.35), (1.8, 1.2, -1.35), (1.8, 0.0, -1.35)), (105, 111, 116))
    world_quad(((-1.8, 1.2, -1.35), (-1.8, 1.45, -1.35), (1.8, 1.45, -1.35), (1.8, 1.2, -1.35)), (105, 111, 116))
    world_quad(((-1.8, 1.45, -1.65), (-1.8, 1.45, -1.35), (1.8, 1.45, -1.35), (1.8, 1.45, -1.65)), (157, 166, 174))
    return f"P6\n{width} {height}\n255\n".encode("ascii") + bytes(pixels)


def build_composition_descriptor(preview: bytes) -> bytes:
    value: dict[str, object] = {
        "camera": {
            "far_clip_m": 50.0,
            "near_clip_m": 0.1,
            "position_m": [8.0, 7.0, 10.0],
            "target_m": [0.0, 0.0, -0.2],
            "up": [0.0, 1.0, 0.0],
            "vertical_fov_degrees": 50.0,
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
            "maximum_xz_m": [3.159375, -0.2625],
            "minimum_xz_m": [-1.8, -1.65],
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
            "maximum_m": [3.0, 1.45, 6.0],
            "minimum_m": [-3.0, 0.0, -6.0],
        },
    }
    return canonical_pretty(value)


def binding(texture: str, sampler: str, *, scale: tuple[float, float] = (1.0, 1.0)) -> dict[str, object]:
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
    alpha_test: str = "disabled",
    alpha_cutoff: float = 0.5,
    metallic: float = 0.0,
    roughness: float = 1.0,
    emissive: tuple[float, float, float] = (0.0, 0.0, 0.0),
    emissive_strength: float = 1.0,
) -> dict[str, object]:
    return {
        "alpha_cutoff": alpha_cutoff,
        "alpha_test_mode": alpha_test,
        "base_color_factor": [1.0, 1.0, 1.0, 1.0],
        "base_color_transfer": "srgb_decode_before_filter",
        "blend_mode": "replace",
        "depth_write": True,
        "double_sided": False,
        "emissive_factor": list(emissive),
        "emissive_strength": emissive_strength,
        "id": identifier,
        "index_of_refraction": 1.5,
        "metallic_factor": metallic,
        "model": "pbr_metallic_roughness",
        "normal_scale": 1.0,
        "occlusion_strength": 1.0,
        "roughness_factor": roughness,
        "specular_factor": [1.0, 1.0, 1.0],
        "textures": textures,
        "workflow": workflow,
    }


Pixel = tuple[int, int, int, int]


def _noise(x: int, y: int, seed: int) -> int:
    value = (x * 0x45D9F3B + y * 0x119DE1F3 + seed * 0x27D4EB2D) & 0xFFFFFFFF
    value ^= value >> 16
    value = (value * 0x45D9F3B) & 0xFFFFFFFF
    value ^= value >> 16
    return value & 0xFF


def _smooth_q8(remainder: int, span: int) -> int:
    """Return smoothstep(remainder / span) in deterministic Q8."""

    phase = (remainder * 256) // span
    return (phase * phase * (768 - 2 * phase) + 32768) // 65536


def _lerp_q8(left: int, right: int, weight: int) -> int:
    return (left * (256 - weight) + right * weight + 128) // 256


def _periodic_value_noise(x: int, y: int, cell: int, seed: int) -> int:
    """Sample a seamless, fixed-point value-noise octave in [-128, 127]."""

    if SURFACE_TEXTURE_SIZE % cell:
        raise ValueError("surface texture size must be divisible by every noise cell")
    cell_count = SURFACE_TEXTURE_SIZE // cell
    grid_x = x // cell
    grid_y = y // cell
    blend_x = _smooth_q8(x % cell, cell)
    blend_y = _smooth_q8(y % cell, cell)
    corners = (
        _noise(grid_x % cell_count, grid_y % cell_count, seed) - 128,
        _noise((grid_x + 1) % cell_count, grid_y % cell_count, seed) - 128,
        _noise(grid_x % cell_count, (grid_y + 1) % cell_count, seed) - 128,
        _noise(
            (grid_x + 1) % cell_count,
            (grid_y + 1) % cell_count,
            seed,
        )
        - 128,
    )
    upper = _lerp_q8(corners[0], corners[1], blend_x)
    lower = _lerp_q8(corners[2], corners[3], blend_x)
    return _lerp_q8(upper, lower, blend_y)


def _clamp_byte(value: int) -> int:
    return min(255, max(0, value))


def _triangle_wave(value: int, period: int) -> int:
    """Return a periodic 0..period triangular ridge without libm drift."""

    phase = value % period
    return period - abs(phase * 2 - period)


def _mip_chain(
    width: int,
    height: int,
    pixel: Callable[[int, int], Pixel],
    *,
    srgb: bool,
    positive_z_normal: bool = False,
) -> list[tuple[int, int, tuple[Pixel, ...]]]:
    def canonicalize_normal_rg(red: int, green: int) -> Pixel:
        """Encode the +Z evidence implied by the exact final RG8 bytes."""
        decoded_x = 2.0 * red / 255.0 - 1.0
        decoded_y = 2.0 * green / 255.0 - 1.0
        decoded_z = math.sqrt(
            max(0.0, 1.0 - decoded_x * decoded_x - decoded_y * decoded_y)
        )
        encoded_z = min(255, max(128, int((decoded_z + 1.0) * 127.5 + 0.5)))
        return (red, green, encoded_z, 255)

    def canonicalize_base_normal(value: Pixel) -> Pixel:
        if not positive_z_normal:
            return value
        return canonicalize_normal_rg(value[0], value[1])

    def downsample_normal(samples: tuple[Pixel, ...]) -> Pixel:
        average_x = 0.0
        average_y = 0.0
        average_z = 0.0
        for sample in samples:
            decoded_x = 2.0 * sample[0] / 255.0 - 1.0
            decoded_y = 2.0 * sample[1] / 255.0 - 1.0
            decoded_z = math.sqrt(
                max(0.0, 1.0 - decoded_x * decoded_x - decoded_y * decoded_y)
            )
            average_x += decoded_x
            average_y += decoded_y
            average_z += decoded_z
        inverse_count = 1.0 / len(samples)
        average = (
            average_x * inverse_count,
            average_y * inverse_count,
            average_z * inverse_count,
        )
        normalized = _normalize3(average)
        # Quantize the averaged unit vector's XY first, then derive source B
        # from those final RG8 values because that is exactly what the native
        # upload and pinned PBS consume/reconstruct.
        encoded_x = min(255, max(0, int((normalized[0] + 1.0) * 127.5 + 0.5)))
        encoded_y = min(255, max(0, int((normalized[1] + 1.0) * 127.5 + 0.5)))
        return canonicalize_normal_rg(encoded_x, encoded_y)

    pixels = tuple(
        canonicalize_base_normal(pixel(x, y))
        for y in range(height)
        for x in range(width)
    )
    result = [(width, height, pixels)]
    while width > 1 or height > 1:
        next_width = max(1, width // 2)
        next_height = max(1, height // 2)
        reduced: list[Pixel] = []
        for y in range(next_height):
            for x in range(next_width):
                samples = tuple(
                    pixels[min(height - 1, y * 2 + dy) * width + min(width - 1, x * 2 + dx)]
                    for dy in range(2)
                    for dx in range(2)
                )
                if positive_z_normal:
                    reduced.append(downsample_normal(samples))
                    continue
                channels: list[int] = []
                for channel in range(4):
                    values = tuple(sample[channel] for sample in samples)
                    if srgb and channel < 3:
                        # Deterministic gamma-two approximation to a linear-light
                        # box filter. This prevents the authored sRGB mip chain
                        # from darkening high-contrast lane/emissive detail.
                        mean_square = (sum(value * value for value in values) + 2) // 4
                        channels.append(math.isqrt(mean_square))
                    else:
                        channels.append((sum(values) + 2) // 4)
                reduced.append(tuple(channels))  # type: ignore[arg-type]
        width, height, pixels = next_width, next_height, tuple(reduced)
        result.append((width, height, pixels))
    return result


def build_sources(repo_root: Path) -> None:
    source_root = repo_root / SOURCE_DIRECTORY
    texture_root = source_root / "textures"
    texture_root.mkdir(parents=True, exist_ok=True)
    glb = build_glb()
    preview = build_composition_preview()
    composition = build_composition_descriptor(preview)
    def lane_pixel(x: int, y: int) -> Pixel:
        edge = min(x, 15 - x)
        alpha = 255 if edge >= 2 and y < 11 else (96 if edge == 1 and y < 11 else 0)
        return (248, 244, 218, alpha)

    def reflector_base(x: int, y: int) -> Pixel:
        radius = abs(x * 2 - 7) + abs(y * 2 - 7)
        return (255, max(92, 176 - radius * 5), 24, 255)

    def reflector_emissive(x: int, y: int) -> Pixel:
        radius = abs(x * 2 - 7) + abs(y * 2 - 7)
        intensity = max(80, 255 - radius * 11)
        return (intensity, max(24, intensity // 3), 5, 255)

    def reflector_specular(x: int, y: int) -> Pixel:
        highlight = max(150, 245 - (abs(x - 3) + abs(y - 3)) * 12)
        return (highlight, max(140, highlight - 12), max(125, highlight - 30), 255)

    @lru_cache(maxsize=None)
    def road_height_tile(x: int, y: int) -> int:
        coarse = _periodic_value_noise(x, y, 64, 11)
        aggregate = _periodic_value_noise(x, y, 16, 13)
        grit = _periodic_value_noise(x, y, 4, 17)
        grain = _noise(x % SURFACE_TEXTURE_SIZE, y % SURFACE_TEXTURE_SIZE, 19) - 128
        pore = max(0, -88 - grit)
        stone = max(0, grain - 112)
        return (
            coarse // 10
            + aggregate // 6
            + grit // 4
            + grain // 7
            - pore * 2
            + stone
        )

    def road_height(x: int, y: int) -> int:
        return road_height_tile(
            x % SURFACE_TEXTURE_SIZE,
            y % SURFACE_TEXTURE_SIZE,
        )

    def road_base(x: int, y: int) -> Pixel:
        broad = _periodic_value_noise(x, y, 128, 23)
        aggregate = _periodic_value_noise(x, y, 32, 29)
        grit = _periodic_value_noise(x, y, 4, 31)
        grain = _noise(
            x % SURFACE_TEXTURE_SIZE,
            y % SURFACE_TEXTURE_SIZE,
            37,
        ) - 128
        pit = max(0, -86 - grit)
        exposed_stone = max(0, grain - 115)
        value = _clamp_byte(
            52
            + broad // 16
            + aggregate // 18
            + grit // 28
            + grain // 72
            - pit // 3
            + exposed_stone // 2
        )
        warmth = exposed_stone // 10
        return (
            _clamp_byte(value + warmth),
            _clamp_byte(value - 1),
            _clamp_byte(value - 2 - warmth // 2),
            255,
        )

    def road_metallic_roughness(x: int, y: int) -> Pixel:
        broad = _periodic_value_noise(x, y, 128, 41)
        aggregate = _periodic_value_noise(x, y, 16, 43)
        grit = _periodic_value_noise(x, y, 4, 47)
        grain = _noise(
            x % SURFACE_TEXTURE_SIZE,
            y % SURFACE_TEXTURE_SIZE,
            53,
        ) - 128
        pit = max(0, -90 - grit)
        polished_stone = max(0, grain - 116)
        roughness = max(
            198,
            min(
                244,
                220
                + broad // 20
                + aggregate // 25
                + grit // 32
                + pit // 2
                - polished_stone // 2,
            ),
        )
        # Canonical metallic/roughness packing: roughness G, metallic B.
        return (255, roughness, 0, 255)

    def road_normal(x: int, y: int) -> Pixel:
        derivative_x = road_height(x + 1, y) - road_height(x - 1, y)
        derivative_y = road_height(x, y + 1) - road_height(x, y - 1)
        tangent_x = max(-38, min(38, -derivative_x))
        tangent_y = max(-38, min(38, -derivative_y))
        return (128 + tangent_x, 128 + tangent_y, 255, 255)

    @lru_cache(maxsize=None)
    def wet_film_tile(x: int, y: int) -> tuple[int, int, int]:
        return (
            _periodic_value_noise(x, y, 128, 67),
            _periodic_value_noise(x, y, 32, 71),
            _periodic_value_noise(x, y, 8, 73),
        )

    def wet_film(x: int, y: int) -> tuple[int, int, int]:
        return wet_film_tile(
            x % SURFACE_TEXTURE_SIZE,
            y % SURFACE_TEXTURE_SIZE,
        )

    @lru_cache(maxsize=None)
    def wet_height_tile(x: int, y: int) -> int:
        film, flow, micro = wet_film(x, y)
        warp = film // 2 + flow // 3
        diagonal_ripple = _triangle_wave(x + y * 3 + warp, 64) - 32
        cross_ripple = _triangle_wave(x * 3 - y - warp, 32) - 16
        diagonal_weight = max(0, flow + 56)
        cross_weight = max(0, -flow - 8)
        return (
            film // 28
            + flow // 22
            + micro // 14
            + diagonal_ripple * diagonal_weight // 640
            + cross_ripple * cross_weight // 1024
        )


    def wet_height(x: int, y: int) -> int:
        return wet_height_tile(
            x % SURFACE_TEXTURE_SIZE,
            y % SURFACE_TEXTURE_SIZE,
        )

    def wet_base(x: int, y: int) -> Pixel:
        film, flow, micro = wet_film(x, y)
        value = _clamp_byte(31 - film // 20 - flow // 32 + micro // 56)
        return (
            _clamp_byte(value - 4),
            _clamp_byte(value),
            _clamp_byte(value + 5),
            255,
        )

    def wet_normal(x: int, y: int) -> Pixel:
        derivative_x = wet_height(x + 1, y) - wet_height(x - 1, y)
        derivative_y = wet_height(x, y + 1) - wet_height(x, y - 1)
        tangent_x = max(-16, min(16, -derivative_x))
        tangent_y = max(-16, min(16, -derivative_y))
        return (128 + tangent_x, 128 + tangent_y, 255, 255)

    def wet_specular(x: int, y: int) -> Pixel:
        film, flow, micro = wet_film(x, y)
        value = max(220, min(252, 236 + film // 18 + flow // 28 + micro // 56))
        return (value, value, min(255, value + 3), 255)

    texture_definitions = (
        ("rorng_a0_lane_base", "base_color", "srgb", "lane_base", 16, 16, lane_pixel),
        ("rorng_a0_reflector_base", "base_color", "srgb", "reflector_base", 8, 8, reflector_base),
        ("rorng_a0_reflector_emissive", "emissive", "srgb", "reflector_emissive", 8, 8, reflector_emissive),
        ("rorng_a0_reflector_specular", "specular", "linear", "reflector_specular", 8, 8, reflector_specular),
        ("rorng_a0_road_base", "base_color", "srgb", "road_base", SURFACE_TEXTURE_SIZE, SURFACE_TEXTURE_SIZE, road_base),
        ("rorng_a0_road_metallic_roughness", "metallic_roughness", "linear", "road_metallic_roughness", SURFACE_TEXTURE_SIZE, SURFACE_TEXTURE_SIZE, road_metallic_roughness),
        ("rorng_a0_road_normal", "normal", "linear", "road_normal", SURFACE_TEXTURE_SIZE, SURFACE_TEXTURE_SIZE, road_normal),
        ("rorng_a0_wet_base", "base_color", "srgb", "wet_base", SURFACE_TEXTURE_SIZE, SURFACE_TEXTURE_SIZE, wet_base),
        ("rorng_a0_wet_normal", "normal", "linear", "wet_normal", SURFACE_TEXTURE_SIZE, SURFACE_TEXTURE_SIZE, wet_normal),
        ("rorng_a0_wet_specular", "specular", "linear", "wet_specular", SURFACE_TEXTURE_SIZE, SURFACE_TEXTURE_SIZE, wet_specular),
    )
    texture_payloads: dict[str, bytes] = {}
    texture_specs: list[tuple[str, str, str, tuple[tuple[str, int, int], ...]]] = []
    for identifier, role, color_space, stem, width, height, pixel in texture_definitions:
        mip_records: list[tuple[str, int, int]] = []
        for level, (mip_width, mip_height, pixels) in enumerate(
            _mip_chain(
                width,
                height,
                pixel,
                srgb=color_space == "srgb",
                positive_z_normal=role == "normal",
            )
        ):
            name = f"{stem}_mip{level}.tga"
            texture_payloads[name] = write_tga_rgba(mip_width, mip_height, pixels)
            mip_records.append((name, mip_width, mip_height))
        texture_specs.append((identifier, role, color_space, tuple(mip_records)))
    (repo_root / GLB_PATH).parent.mkdir(parents=True, exist_ok=True)
    (repo_root / GLB_PATH).write_bytes(glb)
    (repo_root / PREVIEW_PATH).write_bytes(preview)
    (repo_root / COMPOSITION_PATH).write_bytes(composition)
    for name, payload in texture_payloads.items():
        (texture_root / name).write_bytes(payload)

    generator_relative = Path(__file__).resolve().relative_to(repo_root.resolve()).as_posix()
    generator_hash = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()

    def source_record(relative: Path, payload: bytes) -> dict[str, object]:
        return {"path": relative.as_posix(), "sha256": sha256_bytes(payload)}

    textures: list[dict[str, object]] = []
    for identifier, role, color_space, records in texture_specs:
        mips = []
        for name, width, height in records:
            payload = texture_payloads[name]
            mips.append(
                {
                    "height": height,
                    "path": (SOURCE_DIRECTORY / "textures" / name).as_posix(),
                    "sha256": sha256_bytes(payload),
                    "width": width,
                }
            )
        textures.append(
            {
                "color_space": color_space,
                "format": "rgba8_unorm",
                "id": identifier,
                "mips": mips,
                "role": role,
            }
        )

    identity = [
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0,
    ]
    manifest: dict[str, object] = {
        "claims": {
            "ambient_occlusion": False,
            "collision": False,
            "lods": False,
            "native_terrain": False,
            "visual_only": True,
        },
        "format": "ror-native-render-source-v1",
        "materials": [
            material(
                "rorng_a0_lane_decal_material",
                workflow="metallic_roughness",
                alpha_test="greater_equal",
                alpha_cutoff=0.5,
                roughness=0.72,
                textures={
                    "base_color": binding(
                        "rorng_a0_lane_base",
                        "rorng_a0_mipped_repeat_sampler",
                        scale=(1.0, 6.0),
                    )
                },
            ),
            material(
                "rorng_a0_reflector_material",
                workflow="specular",
                roughness=0.18,
                emissive=(1.0, 0.38, 0.04),
                emissive_strength=4.0,
                textures={
                    "base_color": binding("rorng_a0_reflector_base", "rorng_a0_mipped_clamp_sampler"),
                    "emissive": binding("rorng_a0_reflector_emissive", "rorng_a0_mipped_clamp_sampler"),
                    "specular": binding("rorng_a0_reflector_specular", "rorng_a0_mipped_clamp_sampler"),
                },
            ),
            material(
                "rorng_a0_road_surface_material",
                workflow="metallic_roughness",
                metallic=1.0,
                roughness=1.0,
                textures={
                    "base_color": binding("rorng_a0_road_base", "rorng_a0_mipped_repeat_sampler", scale=(2.0, 4.0)),
                    "metallic_roughness": binding("rorng_a0_road_metallic_roughness", "rorng_a0_mipped_repeat_sampler", scale=(2.0, 4.0)),
                    "normal": binding("rorng_a0_road_normal", "rorng_a0_mipped_repeat_sampler", scale=(2.0, 4.0)),
                },
            ),
            material(
                "rorng_a0_wet_asphalt_material",
                workflow="specular",
                roughness=0.08,
                textures={
                    "base_color": binding("rorng_a0_wet_base", "rorng_a0_mipped_repeat_sampler", scale=(1.0, 4.0)),
                    "normal": binding("rorng_a0_wet_normal", "rorng_a0_mipped_repeat_sampler", scale=(1.0, 4.0)),
                    "specular": binding("rorng_a0_wet_specular", "rorng_a0_mipped_repeat_sampler", scale=(1.0, 4.0)),
                },
            ),
        ],
        "meshes": [
            {
                "id": "rorng_a0_lane_decal_mesh",
                "instance_flags": [],
                "material": "rorng_a0_lane_decal_material",
                "node": "rorng_a0_lane_decal_mesh",
                "object_id": "rorng_a0_lane_decal_object",
                "render_from_object": identity,
            },
            {
                "id": "rorng_a0_reflector_mesh",
                "instance_flags": [],
                "material": "rorng_a0_reflector_material",
                "node": "rorng_a0_reflector_mesh",
                "object_id": "rorng_a0_reflector_object",
                "render_from_object": identity,
            },
            {
                "id": "rorng_a0_road_shadow_gate_mesh",
                "instance_flags": [
                    "casts_shadow",
                    "receives_shadow",
                    "visible_in_reflections",
                ],
                "material": "rorng_a0_road_surface_material",
                "node": "rorng_a0_road_shadow_gate_mesh",
                "object_id": "rorng_a0_road_shadow_gate_object",
                "render_from_object": identity,
            },
            {
                "id": "rorng_a0_road_surface_mesh",
                "instance_flags": [
                    "receives_shadow",
                    "visible_in_reflections",
                ],
                "material": "rorng_a0_road_surface_material",
                "node": "rorng_a0_road_surface_mesh",
                "object_id": "rorng_a0_road_surface_object",
                "render_from_object": identity,
            },
            {
                "id": "rorng_a0_wet_asphalt_mesh",
                "instance_flags": [
                    "receives_shadow",
                    "visible_in_reflections",
                ],
                "material": "rorng_a0_wet_asphalt_material",
                "node": "rorng_a0_wet_asphalt_mesh",
                "object_id": "rorng_a0_wet_asphalt_object",
                "render_from_object": identity,
            },
        ],
        "outputs": {
            "package_path": "resources/nextgen/native/a0_road_tile_12m/rorng_a0_road_tile_12m.rornative",
            "report_path": "resources/nextgen/native/a0_road_tile_12m/rorng_a0_road_tile_12m.compile.json",
        },
        "package": {
            "author": "Rigs of Rods contributors",
            "creation_attestation": "Project-original geometry, textures, and material declarations were independently authored for V2; no geometry, texture, material-script, or shader bytes were copied from legacy content or another simulator.",
            "dimensions_m": [6.0, 1.45, 12.0],
            "display_name": "A0 native 12 metre visual road tile",
            "id": PACKAGE_ID,
            "license": "GPL-3.0-or-later",
            "modified": False,
            "origin_class": "project_original",
            "source_revision": "sha256-pinned-source-set-v1",
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
                "id": "rorng_a0_mipped_clamp_sampler",
                "magnification_filter": "linear",
                "maximum_anisotropy": 4.0,
                "maximum_lod": 3.0,
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
                "id": "rorng_a0_mipped_repeat_sampler",
                "magnification_filter": "linear",
                "maximum_anisotropy": 4.0,
                "maximum_lod": 9.0,
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
