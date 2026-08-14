#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate renderer-neutral visual-course placement data for later physics binding."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
from pathlib import PurePosixPath
import re
import struct
import sys
from typing import Any


FORMAT = "ror-native-course-alignment-v1"
IDENTIFIER = re.compile(r"[a-z][a-z0-9_]{0,127}")
MAX_BYTES = 1024 * 1024
MAX_GLB_BYTES = 64 * 1024 * 1024
GEOMETRY_EPSILON = 1e-5


def close(left: float, right: float) -> bool:
    return abs(left - right) <= GEOMETRY_EPSILON


def same_vector(left: tuple[float, ...], right: tuple[float, ...]) -> bool:
    return len(left) == len(right) and all(close(a, b) for a, b in zip(left, right))


def safe_relative_path(value: Any) -> PurePosixPath | None:
    if not isinstance(value, str) or not value or "\\" in value:
        return None
    candidate = PurePosixPath(value)
    if candidate.is_absolute() or any(part in ("", ".", "..") for part in candidate.parts):
        return None
    return candidate


def decode_glb_components(payload: bytes) -> dict[str, list[dict[str, Any]]]:
    """Decode the bounded v1 GLB geometry needed by the alignment oracle."""

    if len(payload) < 28:
        raise ValueError("GLB is truncated")
    magic, version, declared_size = struct.unpack_from("<4sII", payload, 0)
    if magic != b"glTF" or version != 2 or declared_size != len(payload):
        raise ValueError("GLB header is invalid")
    json_size, json_kind = struct.unpack_from("<II", payload, 12)
    json_start = 20
    json_end = json_start + json_size
    if json_kind != 0x4E4F534A or json_end + 8 > len(payload):
        raise ValueError("GLB JSON chunk is invalid")
    binary_size, binary_kind = struct.unpack_from("<II", payload, json_end)
    binary_start = json_end + 8
    binary_end = binary_start + binary_size
    if binary_kind != 0x004E4942 or binary_end != len(payload):
        raise ValueError("GLB binary chunk is invalid")
    document = json.loads(payload[json_start:json_end].rstrip(b" \x00"), object_pairs_hook=object_pairs)
    if not isinstance(document, dict):
        raise ValueError("GLB document root is invalid")
    nodes = document.get("nodes")
    meshes = document.get("meshes")
    accessors = document.get("accessors")
    views = document.get("bufferViews")
    if not all(isinstance(value, list) for value in (nodes, meshes, accessors, views)):
        raise ValueError("GLB arrays are missing")
    assert isinstance(nodes, list)
    assert isinstance(meshes, list)
    assert isinstance(accessors, list)
    assert isinstance(views, list)
    binary = payload[binary_start:binary_end]

    def accessor_values(index: Any, expected_type: str) -> list[tuple[float, ...]] | list[int]:
        if isinstance(index, bool) or not isinstance(index, int) or not 0 <= index < len(accessors):
            raise ValueError("GLB accessor reference is invalid")
        accessor = accessors[index]
        if not isinstance(accessor, dict) or accessor.get("type") != expected_type:
            raise ValueError("GLB accessor type is invalid")
        view_index = accessor.get("bufferView")
        count = accessor.get("count")
        component = accessor.get("componentType")
        if (
            isinstance(view_index, bool)
            or not isinstance(view_index, int)
            or not 0 <= view_index < len(views)
            or isinstance(count, bool)
            or not isinstance(count, int)
            or count <= 0
        ):
            raise ValueError("GLB accessor bounds are invalid")
        view = views[view_index]
        if not isinstance(view, dict):
            raise ValueError("GLB buffer view is invalid")
        formats = {5123: "H", 5125: "I", 5126: "f"}
        widths = {"SCALAR": 1, "VEC3": 3}
        expected_components = {"SCALAR": {5123, 5125}, "VEC3": {5126}}
        if component not in expected_components.get(expected_type, set()) or expected_type not in widths:
            raise ValueError("GLB accessor component is unsupported")
        width = widths[expected_type]
        unpacker = struct.Struct("<" + formats[component] * width)
        view_offset = view.get("byteOffset", 0)
        accessor_offset = accessor.get("byteOffset", 0)
        byte_length = view.get("byteLength")
        if any(isinstance(item, bool) or not isinstance(item, int) for item in (view_offset, accessor_offset, byte_length)):
            raise ValueError("GLB accessor offsets are invalid")
        if "byteStride" in view or "sparse" in accessor or accessor.get("normalized", False) is not False:
            raise ValueError("GLB accessor layout is unsupported")
        start = view_offset + accessor_offset
        required = count * unpacker.size
        if start < 0 or byte_length < accessor_offset + required or start + required > len(binary):
            raise ValueError("GLB accessor escapes its binary buffer")
        values = [unpacker.unpack_from(binary, start + item * unpacker.size) for item in range(count)]
        if expected_type == "SCALAR":
            return [int(value[0]) for value in values]
        return [tuple(float(component_value) for component_value in value) for value in values]

    result: dict[str, list[dict[str, Any]]] = {}
    if len(nodes) != len(meshes):
        raise ValueError("GLB node/mesh cardinality differs")
    for node_index, (node, mesh) in enumerate(zip(nodes, meshes)):
        if not isinstance(node, dict) or not isinstance(mesh, dict):
            raise ValueError("GLB node or mesh is invalid")
        name = node.get("name")
        primitives = mesh.get("primitives")
        if (
            not isinstance(name, str)
            or set(node) != {"mesh", "name"}
            or node.get("mesh") != node_index
            or set(mesh) != {"name", "primitives"}
            or mesh.get("name") != name
            or not isinstance(primitives, list)
            or len(primitives) != 1
            or not isinstance(primitives[0], dict)
        ):
            raise ValueError("GLB node/mesh profile is invalid")
        primitive = primitives[0]
        attributes = primitive.get("attributes")
        if (
            set(primitive) != {"attributes", "indices", "material", "mode"}
            or primitive.get("mode") != 4
            or not isinstance(attributes, dict)
            or set(attributes) != {"NORMAL", "POSITION", "TANGENT", "TEXCOORD_0"}
        ):
            raise ValueError("GLB primitive attributes are invalid")
        positions_value = accessor_values(attributes.get("POSITION"), "VEC3")
        indices_value = accessor_values(primitive.get("indices"), "SCALAR")
        positions = [tuple(value) for value in positions_value]
        indices = [int(value) for value in indices_value]
        if len(indices) % 3 or any(index < 0 or index >= len(positions) for index in indices):
            raise ValueError("GLB primitive indices are invalid")

        # Weld hard-edged face vertices by exact binary32 position so each
        # authored box/quad becomes one geometric component.
        parent = list(range(len(positions)))

        def find(value: int) -> int:
            while parent[value] != value:
                parent[value] = parent[parent[value]]
                value = parent[value]
            return value

        def union(left: int, right: int) -> None:
            left_root = find(left)
            right_root = find(right)
            if left_root != right_root:
                parent[right_root] = left_root

        first_by_position: dict[tuple[float, float, float], int] = {}
        for vertex_index, position in enumerate(positions):
            prior = first_by_position.setdefault(position, vertex_index)
            union(prior, vertex_index)
        for triangle_index in range(0, len(indices), 3):
            a, b, c = indices[triangle_index : triangle_index + 3]
            union(a, b)
            union(a, c)

        component_vertices: dict[int, set[int]] = {}
        component_triangles: dict[int, list[tuple[int, int, int]]] = {}
        for vertex_index in range(len(positions)):
            component_vertices.setdefault(find(vertex_index), set()).add(vertex_index)
        for triangle_index in range(0, len(indices), 3):
            triangle = tuple(indices[triangle_index : triangle_index + 3])
            component_triangles.setdefault(find(triangle[0]), []).append(triangle)  # type: ignore[arg-type]
        components: list[dict[str, Any]] = []
        for root in sorted(component_vertices):
            vertex_indexes = component_vertices[root]
            component_positions = [positions[index] for index in sorted(vertex_indexes)]
            components.append(
                {
                    "bounds_min": tuple(min(position[axis] for position in component_positions) for axis in range(3)),
                    "bounds_max": tuple(max(position[axis] for position in component_positions) for axis in range(3)),
                    "positions": positions,
                    "triangles": component_triangles.get(root, []),
                }
            )
        if name in result:
            raise ValueError("GLB node names are not unique")
        result[name] = sorted(
            components,
            key=lambda component: (component["bounds_min"], component["bounds_max"]),
        )
    return result


class DuplicateKey(ValueError):
    pass


def object_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise DuplicateKey(f"duplicate key: {key}")
        result[key] = value
    return result


def canonical_pretty(value: Any) -> bytes:
    return (json.dumps(value, ensure_ascii=True, indent=2, sort_keys=True) + "\n").encode("ascii")


def has_negative_zero(value: float) -> bool:
    return value == 0.0 and math.copysign(1.0, value) < 0.0


class Validator:
    def __init__(self, path: Path, repo_root: Path) -> None:
        self.path = path
        self.repo_root = repo_root.resolve()
        self.diagnostics: list[dict[str, str]] = []
        self.value: dict[str, Any] | None = None
        self.native_mesh_ids: set[str] = set()
        self.native_package_id: str | None = None
        self.native_mesh_components: dict[str, list[dict[str, Any]]] = {}
        self.surface_geometry: dict[str, dict[str, Any]] = {}

    def add(self, code: str, path: str, message: str) -> None:
        self.diagnostics.append({"code": code, "message": message, "path": path})

    def identifier(self, value: Any, path: str) -> str | None:
        if not isinstance(value, str) or IDENTIFIER.fullmatch(value) is None:
            self.add("IDENTIFIER_INVALID", path, "expected a canonical lowercase identifier")
            return None
        return value

    def number(self, value: Any, path: str) -> float | None:
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            self.add("NUMBER_INVALID", path, "expected a finite JSON number")
            return None
        try:
            converted = float(value)
        except (OverflowError, ValueError):
            self.add("NUMBER_INVALID", path, "number is outside the finite range")
            return None
        if not math.isfinite(converted) or has_negative_zero(converted):
            self.add("NUMBER_INVALID", path, "number must be finite and not negative zero")
            return None
        return converted

    def integer(self, value: Any, path: str) -> int | None:
        if isinstance(value, bool) or not isinstance(value, int) or value < 0:
            self.add("INTEGER_INVALID", path, "expected a nonnegative integer")
            return None
        return value

    def vector(self, value: Any, width: int, path: str) -> tuple[float, ...] | None:
        if not isinstance(value, list) or len(value) != width:
            self.add("VECTOR_INVALID", path, f"expected exactly {width} components")
            return None
        converted = tuple(
            self.number(component, f"{path}[{index}]")
            for index, component in enumerate(value)
        )
        if any(component is None for component in converted):
            return None
        return tuple(float(component) for component in converted)  # type: ignore[arg-type]

    def record(self, value: Any, keys: tuple[str, ...], path: str) -> dict[str, Any] | None:
        if not isinstance(value, dict) or set(value) != set(keys):
            self.add("RECORD_INVALID", path, f"expected exact keys {sorted(keys)}")
            return None
        return value

    def load(self) -> None:
        try:
            if not self.path.is_file() or self.path.is_symlink():
                self.add("SOURCE_INVALID", "$", "alignment source must be a direct regular file")
                return
            payload = self.path.read_bytes()
            if not 1 <= len(payload) <= MAX_BYTES:
                self.add("SOURCE_SIZE", "$", "alignment source exceeds the bounded v1 size")
                return
            value = json.loads(payload, object_pairs_hook=object_pairs)
            if not isinstance(value, dict):
                self.add("ROOT_INVALID", "$", "root must be an object")
                return
            self.value = value
            if payload != canonical_pretty(value):
                self.add("SOURCE_CANONICAL", "$", "alignment must use canonical sorted pretty JSON")
        except (OSError, UnicodeError, json.JSONDecodeError, DuplicateKey, ValueError) as error:
            self.add("SOURCE_INVALID", "$", str(error))

    def resolve_native_source(self, value: Any) -> Path | None:
        relative = safe_relative_path(value)
        if relative is None:
            self.add(
                "NATIVE_GLB_INVALID",
                "$.source.glb.path",
                "GLB path must be a safe repository-relative path",
            )
            return None
        candidate = self.repo_root.joinpath(*relative.parts)
        try:
            candidate.resolve(strict=False).relative_to(self.repo_root)
        except (OSError, ValueError):
            self.add("NATIVE_GLB_INVALID", "$.source.glb.path", "GLB path escapes the repository root")
            return None
        if not candidate.is_file() or candidate.is_symlink():
            self.add("NATIVE_GLB_INVALID", "$.source.glb.path", "GLB must be a direct regular file")
            return None
        return candidate

    def load_native_manifest(self) -> None:
        suffix = ".alignment.json"
        if not self.path.name.endswith(suffix):
            self.add("NATIVE_MANIFEST_PATH", "$", "alignment file must end with .alignment.json")
            return
        native_path = self.path.with_name(self.path.name[: -len(suffix)] + ".native.json")
        try:
            if not native_path.is_file() or native_path.is_symlink():
                self.add("NATIVE_MANIFEST_MISSING", "$", "sibling native manifest must be a direct regular file")
                return
            payload = native_path.read_bytes()
            if not 1 <= len(payload) <= MAX_BYTES:
                self.add("NATIVE_MANIFEST_SIZE", "$", "sibling native manifest exceeds the bounded size")
                return
            value = json.loads(payload, object_pairs_hook=object_pairs)
            if not isinstance(value, dict) or payload != canonical_pretty(value):
                self.add("NATIVE_MANIFEST_INVALID", "$", "sibling native manifest is not canonical JSON")
                return
            package = value.get("package")
            meshes = value.get("meshes")
            source = value.get("source")
            if not isinstance(package, dict) or not isinstance(package.get("id"), str):
                self.add("NATIVE_MANIFEST_INVALID", "$.package", "sibling native package identity is missing")
                return
            if not isinstance(meshes, list) or not meshes:
                self.add("NATIVE_MANIFEST_INVALID", "$.meshes", "sibling native mesh declarations are missing")
                return
            mesh_ids = [mesh.get("id") if isinstance(mesh, dict) else None for mesh in meshes]
            if not all(
                isinstance(mesh_id, str) and IDENTIFIER.fullmatch(mesh_id) is not None
                for mesh_id in mesh_ids
            ):
                self.add("NATIVE_MANIFEST_INVALID", "$.meshes", "sibling native mesh identifiers are invalid")
                return
            if mesh_ids != sorted(mesh_ids) or len(set(mesh_ids)) != len(mesh_ids):
                self.add("NATIVE_MANIFEST_INVALID", "$.meshes", "sibling native mesh identifiers must be unique and sorted")
                return
            identity = [
                1.0, 0.0, 0.0, 0.0,
                0.0, 1.0, 0.0, 0.0,
                0.0, 0.0, 1.0, 0.0,
                0.0, 0.0, 0.0, 1.0,
            ]
            if any(
                not isinstance(mesh, dict) or mesh.get("id") != mesh.get("node")
                for mesh in meshes
            ):
                self.add(
                    "NATIVE_MESH_BINDING",
                    "$.meshes",
                    "A1 alignment geometry requires each manifest mesh ID to name its GLB node",
                )
                return
            if any(mesh.get("render_from_object") != identity for mesh in meshes):
                self.add(
                    "NATIVE_MESH_TRANSFORM",
                    "$.meshes",
                    "A1 alignment geometry requires exact identity render_from_object transforms",
                )
                return
            if not isinstance(source, dict) or not isinstance(source.get("glb"), dict):
                self.add("NATIVE_MANIFEST_INVALID", "$.source.glb", "sibling native GLB source record is missing")
                return
            glb_record = source["glb"]
            if set(glb_record) != {"path", "sha256"}:
                self.add("NATIVE_MANIFEST_INVALID", "$.source.glb", "sibling native GLB source record has unknown keys")
                return
            expected_sha = glb_record.get("sha256")
            if not isinstance(expected_sha, str) or re.fullmatch(r"[0-9a-f]{64}", expected_sha) is None:
                self.add("NATIVE_MANIFEST_INVALID", "$.source.glb.sha256", "sibling native GLB hash is invalid")
                return
            glb_path = self.resolve_native_source(glb_record.get("path"))
            if glb_path is None:
                return
            glb_payload = glb_path.read_bytes()
            if not 1 <= len(glb_payload) <= MAX_GLB_BYTES:
                self.add("NATIVE_GLB_INVALID", "$.source.glb", "GLB exceeds the bounded alignment-oracle size")
                return
            if hashlib.sha256(glb_payload).hexdigest() != expected_sha:
                self.add("NATIVE_GLB_HASH", "$.source.glb.sha256", "sibling native GLB hash does not match its bytes")
                return
            components = decode_glb_components(glb_payload)
            if set(mesh_ids) != set(components):
                self.add("NATIVE_GLB_MESH_SET", "$.meshes", "manifest mesh IDs and decoded GLB nodes differ")
                return
            self.native_package_id = package["id"]
            self.native_mesh_ids = set(mesh_ids)
            self.native_mesh_components = components
        except (OSError, UnicodeError, json.JSONDecodeError, DuplicateKey, ValueError, struct.error) as error:
            self.add("NATIVE_MANIFEST_INVALID", "$", str(error))

    def rectangle_bounds(
        self,
        value: Any,
        path: str,
    ) -> tuple[float, float, float, float] | None:
        if not isinstance(value, list) or len(value) != 4:
            self.add("SURFACE_POLYGON", path, "surface polygon must be one canonical four-point rectangle")
            return None
        points = [self.vector(point, 2, f"{path}[{index}]") for index, point in enumerate(value)]
        if any(point is None for point in points):
            return None
        typed = [point for point in points if point is not None]
        minimum_x = min(point[0] for point in typed)
        maximum_x = max(point[0] for point in typed)
        minimum_z = min(point[1] for point in typed)
        maximum_z = max(point[1] for point in typed)
        expected = (
            (minimum_x, minimum_z),
            (minimum_x, maximum_z),
            (maximum_x, maximum_z),
            (maximum_x, minimum_z),
        )
        if minimum_x >= maximum_x or minimum_z >= maximum_z or any(
            not same_vector(point, canonical) for point, canonical in zip(typed, expected)
        ):
            self.add("SURFACE_POLYGON", path, "surface polygon must use canonical nondegenerate rectangle winding")
            return None
        return minimum_x, maximum_x, minimum_z, maximum_z

    def validate_horizontal_component(
        self,
        component: dict[str, Any],
        bounds: tuple[float, float, float, float],
        visual_y: float,
        path: str,
    ) -> None:
        positions = component["positions"]
        face_triangles: list[tuple[tuple[float, float], ...]] = []
        cross_components: list[float] = []
        for triangle in component["triangles"]:
            points = [positions[index] for index in triangle]
            if not all(close(point[1], visual_y) for point in points):
                continue
            first, second, third = points
            ab = tuple(second[axis] - first[axis] for axis in range(3))
            ac = tuple(third[axis] - first[axis] for axis in range(3))
            cross_components.append(ab[2] * ac[0] - ab[0] * ac[2])
            face_triangles.append(tuple((point[0], point[2]) for point in points))
        if not face_triangles:
            self.add("SURFACE_GEOMETRY", path, "decoded mesh component has no top face at visual_y_m")
            return
        expected_corners = (
            (bounds[0], bounds[2]),
            (bounds[0], bounds[3]),
            (bounds[1], bounds[3]),
            (bounds[1], bounds[2]),
        )
        if not self.rectangular_face_topology(face_triangles, expected_corners):
            self.add("SURFACE_TOPOLOGY", path, "decoded surface top must be one exact two-triangle rectangle")
        if len(cross_components) != 2 or any(value <= GEOMETRY_EPSILON for value in cross_components):
            self.add("SURFACE_WINDING", path, "surface top triangles must both wind toward positive Y")

    def validate_vertical_face(
        self,
        component: dict[str, Any],
        seam_x: float,
        z_range: tuple[float, float],
        face_range: tuple[float, float],
        path: str,
    ) -> None:
        positions = component["positions"]
        face_triangles: list[tuple[tuple[float, float], ...]] = []
        cross_components: list[float] = []
        for triangle in component["triangles"]:
            points = [positions[index] for index in triangle]
            if not all(close(point[0], seam_x) for point in points):
                continue
            first, second, third = points
            ab = tuple(second[axis] - first[axis] for axis in range(3))
            ac = tuple(third[axis] - first[axis] for axis in range(3))
            cross_components.append(ab[1] * ac[2] - ab[2] * ac[1])
            face_triangles.append(tuple((point[1], point[2]) for point in points))
        if not face_triangles:
            self.add("SEAM_GEOMETRY", path, "decoded curb component has no vertical face at seam_x_m")
            return
        expected_corners = (
            (face_range[0], z_range[0]),
            (face_range[0], z_range[1]),
            (face_range[1], z_range[1]),
            (face_range[1], z_range[0]),
        )
        if not self.rectangular_face_topology(face_triangles, expected_corners):
            self.add("SEAM_FACE_TOPOLOGY", path, "decoded curb seam face must be one exact two-triangle rectangle")
        component_center_x = (
            component["bounds_min"][0] + component["bounds_max"][0]
        ) * 0.5
        expected_sign = -1.0 if seam_x < component_center_x else 1.0
        if len(cross_components) != 2 or any(
            value * expected_sign <= GEOMETRY_EPSILON
            for value in cross_components
        ):
            self.add("SEAM_FACE_WINDING", path, "curb seam triangles must both use the expected outward X orientation")

    def rectangular_face_topology(
        self,
        triangles: list[tuple[tuple[float, float], ...]],
        expected_corners: tuple[tuple[float, float], ...],
    ) -> bool:
        """Require exact rectangular coverage, not only matching area/extrema."""

        def corner_index(point: tuple[float, float]) -> int | None:
            matches = [
                index
                for index, corner in enumerate(expected_corners)
                if same_vector(point, corner)
            ]
            return matches[0] if len(matches) == 1 else None

        if len(triangles) != 2:
            return False
        indexed: list[tuple[int, int, int]] = []
        for triangle in triangles:
            if len(triangle) != 3:
                return False
            converted = tuple(corner_index(point) for point in triangle)
            if any(index is None for index in converted):
                return False
            typed = tuple(int(index) for index in converted if index is not None)
            if len(typed) != 3 or len(set(typed)) != 3:
                return False
            indexed.append(typed)
        if set(indexed[0]) | set(indexed[1]) != {0, 1, 2, 3}:
            return False
        edge_counts: dict[tuple[int, int], int] = {}
        for triangle in indexed:
            for left, right in (
                (triangle[0], triangle[1]),
                (triangle[1], triangle[2]),
                (triangle[2], triangle[0]),
            ):
                edge = (min(left, right), max(left, right))
                edge_counts[edge] = edge_counts.get(edge, 0) + 1
        perimeter = {(0, 1), (1, 2), (2, 3), (0, 3)}
        diagonals = {(0, 2), (1, 3)}
        return (
            all(edge_counts.get(edge) == 1 for edge in perimeter)
            and len([edge for edge in diagonals if edge_counts.get(edge) == 2]) == 1
            and len(edge_counts) == 5
            and sum(edge_counts.values()) == 6
        )

    def validate(self) -> None:
        self.load()
        if self.value is None:
            return
        self.load_native_manifest()
        root = self.record(
            self.value,
            (
                "collision",
                "coordinate_system",
                "course",
                "format",
                "package_id",
                "placements",
                "seams",
                "surfaces",
                "units",
                "visual_only",
            ),
            "$",
        )
        if root is None:
            return
        if root["format"] != FORMAT:
            self.add("FORMAT_UNSUPPORTED", "$.format", f"expected {FORMAT}")
        package_id = self.identifier(root["package_id"], "$.package_id")
        if package_id is not None and package_id != self.native_package_id:
            self.add("PACKAGE_MISMATCH", "$.package_id", "alignment and sibling native package identities differ")
        if root["coordinate_system"] != "right-handed-y-up-meters" or root["units"] != "meters":
            self.add("COORDINATE_SYSTEM", "$", "v1 requires right-handed Y-up metre coordinates")
        if root["visual_only"] is not True:
            self.add("VISUAL_ONLY_REQUIRED", "$.visual_only", "alignment is visual-only")

        collision = self.record(root["collision"], ("binding_exists", "status"), "$.collision")
        if collision is not None and collision != {"binding_exists": False, "status": "pending"}:
            self.add("COLLISION_NONCLAIM", "$.collision", "collision must remain explicitly pending and absent")

        course = self.record(
            root["course"],
            (
                "centerline_m",
                "driveable_visual_bounds_m",
                "finish_center_m",
                "length_m",
                "nominal_road_width_m",
                "start_center_m",
            ),
            "$.course",
        )
        course_length = None
        road_width = None
        bounds_minimum = bounds_maximum = None
        if course is not None:
            course_length = self.number(course["length_m"], "$.course.length_m")
            road_width = self.number(course["nominal_road_width_m"], "$.course.nominal_road_width_m")
            if course_length is not None and not 48.0 <= course_length <= 72.0:
                self.add("COURSE_LENGTH", "$.course.length_m", "course length must be within the bounded 48-72 metre slice")
            if road_width is not None and road_width <= 0.0:
                self.add("COURSE_WIDTH", "$.course.nominal_road_width_m", "road width must be positive")
            centerline = course["centerline_m"]
            if not isinstance(centerline, list) or not 2 <= len(centerline) <= 256:
                self.add("CENTERLINE_INVALID", "$.course.centerline_m", "centerline needs 2-256 points")
            else:
                points = [self.vector(point, 3, f"$.course.centerline_m[{index}]") for index, point in enumerate(centerline)]
                if all(point is not None for point in points) and course_length is not None:
                    measured = sum(
                        math.dist(points[index], points[index + 1])  # type: ignore[arg-type]
                        for index in range(len(points) - 1)
                    )
                    if abs(measured - course_length) > 1e-6:
                        self.add("CENTERLINE_LENGTH", "$.course.centerline_m", "centerline length must equal declared length")
            self.vector(course["start_center_m"], 3, "$.course.start_center_m")
            self.vector(course["finish_center_m"], 3, "$.course.finish_center_m")
            bounds = self.record(course["driveable_visual_bounds_m"], ("maximum", "minimum"), "$.course.driveable_visual_bounds_m")
            if bounds is not None:
                bounds_minimum = self.vector(bounds["minimum"], 3, "$.course.driveable_visual_bounds_m.minimum")
                bounds_maximum = self.vector(bounds["maximum"], 3, "$.course.driveable_visual_bounds_m.maximum")
                if bounds_minimum is not None and bounds_maximum is not None and any(
                    bounds_minimum[axis] >= bounds_maximum[axis] for axis in (0, 2)
                ):
                    self.add("COURSE_BOUNDS", "$.course.driveable_visual_bounds_m", "XZ bounds must have positive area")

        expected_classifications = {
            "curb_left": "raised_curb_top",
            "curb_right": "raised_curb_top",
            "dry_asphalt": "dry_asphalt",
            "shoulder_left": "shoulder",
            "shoulder_right": "shoulder",
            "wet_asphalt_overlay": "wet_asphalt_visual_overlay",
        }
        surfaces = root["surfaces"]
        surface_ids: set[str] = set()
        surface_mesh_ids: set[str] = set()
        surface_component_references: dict[str, set[int]] = {}
        if not isinstance(surfaces, list) or not 1 <= len(surfaces) <= 64:
            self.add("SURFACES_INVALID", "$.surfaces", "expected 1-64 surface records")
        else:
            listed_ids = [surface.get("id") if isinstance(surface, dict) else None for surface in surfaces]
            if (
                not all(isinstance(identifier, str) for identifier in listed_ids)
                or listed_ids != sorted(listed_ids)
                or len(set(listed_ids)) != len(listed_ids)
            ):
                self.add("SURFACE_ORDER", "$.surfaces", "surface IDs must be unique strings in sorted order")
            for index, value in enumerate(surfaces):
                path = f"$.surfaces[{index}]"
                surface = self.record(
                    value,
                    (
                        "classification",
                        "collision_binding",
                        "geometry_y_range_m",
                        "id",
                        "mesh_component_index",
                        "mesh_id",
                        "physics_material",
                        "polygon_xz_m",
                        "slope_dy_dx_dy_dz",
                        "visual_y_m",
                    ),
                    path,
                )
                if surface is None:
                    continue
                identifier = self.identifier(surface["id"], f"{path}.id")
                mesh_id = self.identifier(surface["mesh_id"], f"{path}.mesh_id")
                classification = self.identifier(surface["classification"], f"{path}.classification")
                component_index = self.integer(
                    surface["mesh_component_index"],
                    f"{path}.mesh_component_index",
                )
                y_range = self.vector(
                    surface["geometry_y_range_m"],
                    2,
                    f"{path}.geometry_y_range_m",
                )
                visual_y = self.number(surface["visual_y_m"], f"{path}.visual_y_m")
                slope = self.vector(
                    surface["slope_dy_dx_dy_dz"],
                    2,
                    f"{path}.slope_dy_dx_dy_dz",
                )
                polygon_bounds = self.rectangle_bounds(
                    surface["polygon_xz_m"],
                    f"{path}.polygon_xz_m",
                )
                if identifier is not None:
                    surface_ids.add(identifier)
                    expected_classification = expected_classifications.get(identifier)
                    if expected_classification is not None and classification != expected_classification:
                        self.add(
                            "SURFACE_CLASSIFICATION",
                            f"{path}.classification",
                            "surface classification does not match its A1 role",
                        )
                if mesh_id is not None:
                    surface_mesh_ids.add(mesh_id)
                    if mesh_id not in self.native_mesh_ids:
                        self.add("SURFACE_MESH", f"{path}.mesh_id", "surface references a mesh absent from the native package")
                if surface["collision_binding"] is not None or surface["physics_material"] is not None:
                    self.add("COLLISION_NONCLAIM", path, "surface physics and collision bindings must be null")
                if y_range is not None and y_range[0] > y_range[1]:
                    self.add("SURFACE_HEIGHT_RANGE", f"{path}.geometry_y_range_m", "surface geometry height range is reversed")
                if slope is not None and not same_vector(slope, (0.0, 0.0)):
                    self.add("SURFACE_SLOPE", f"{path}.slope_dy_dx_dy_dz", "A1 decoded surfaces must declare zero dy/dx and dy/dz")
                components = self.native_mesh_components.get(mesh_id or "", [])
                if component_index is not None and component_index >= len(components):
                    self.add("SURFACE_COMPONENT", f"{path}.mesh_component_index", "surface component index is absent from the decoded GLB mesh")
                    continue
                if (
                    identifier is None
                    or mesh_id is None
                    or component_index is None
                    or y_range is None
                    or visual_y is None
                    or polygon_bounds is None
                ):
                    continue
                component = components[component_index]
                surface_component_references.setdefault(mesh_id, set()).add(component_index)
                minimum = component["bounds_min"]
                maximum = component["bounds_max"]
                expected_component_bounds = (
                    polygon_bounds[0],
                    y_range[0],
                    polygon_bounds[2],
                    polygon_bounds[1],
                    y_range[1],
                    polygon_bounds[3],
                )
                actual_component_bounds = (*minimum, *maximum)
                if not same_vector(actual_component_bounds, expected_component_bounds):
                    self.add("SURFACE_GEOMETRY", path, "surface polygon/height range does not equal its decoded GLB component bounds")
                if not close(visual_y, maximum[1]):
                    self.add("SURFACE_GEOMETRY", f"{path}.visual_y_m", "visual surface height must equal decoded component maximum Y")
                self.validate_horizontal_component(component, polygon_bounds, visual_y, path)
                self.surface_geometry[identifier] = {
                    "bounds": polygon_bounds,
                    "component": component,
                    "mesh_id": mesh_id,
                    "visual_y": visual_y,
                    "y_range": y_range,
                }

        if surface_ids != set(expected_classifications):
            self.add("SURFACE_SET", "$.surfaces", "A1 slice requires dry, wet-overlay, both shoulders, and separate left/right curb surfaces")
        for mesh_id in surface_mesh_ids:
            expected_indexes = set(range(len(self.native_mesh_components.get(mesh_id, []))))
            if surface_component_references.get(mesh_id, set()) != expected_indexes:
                self.add(
                    "SURFACE_COMPONENT_COVERAGE",
                    "$.surfaces",
                    "every decoded component of an A1 surface mesh must have exactly one surface binding",
                )

        placements = root["placements"]
        categories: set[str] = set()
        placement_mesh_ids: set[str] = set()
        curb_surface_ids: set[str] = set()
        if not isinstance(placements, list) or not 1 <= len(placements) <= 512:
            self.add("PLACEMENTS_INVALID", "$.placements", "expected 1-512 placement records")
        else:
            placement_ids = [placement.get("id") if isinstance(placement, dict) else None for placement in placements]
            if (
                not all(isinstance(identifier, str) for identifier in placement_ids)
                or placement_ids != sorted(placement_ids)
                or len(set(placement_ids)) != len(placement_ids)
            ):
                self.add("PLACEMENT_ORDER", "$.placements", "placement IDs must be unique strings in sorted order")
            for index, value in enumerate(placements):
                path = f"$.placements[{index}]"
                placement = self.record(
                    value,
                    (
                        "batch_mesh_id",
                        "category",
                        "collision_binding",
                        "dimensions_m",
                        "id",
                        "position_m",
                        "rotation_y_degrees",
                        "surface_id",
                    ),
                    path,
                )
                if placement is None:
                    continue
                identifier = self.identifier(placement["id"], f"{path}.id")
                batch_mesh = self.identifier(placement["batch_mesh_id"], f"{path}.batch_mesh_id")
                category = self.identifier(placement["category"], f"{path}.category")
                dimensions = self.vector(placement["dimensions_m"], 3, f"{path}.dimensions_m")
                position = self.vector(placement["position_m"], 3, f"{path}.position_m")
                rotation = self.number(placement["rotation_y_degrees"], f"{path}.rotation_y_degrees")
                surface_id_value = placement["surface_id"]
                surface_id = (
                    None
                    if surface_id_value is None
                    else self.identifier(surface_id_value, f"{path}.surface_id")
                )
                if category is not None:
                    categories.add(category)
                if batch_mesh is not None:
                    placement_mesh_ids.add(batch_mesh)
                    if batch_mesh not in self.native_mesh_ids:
                        self.add("PLACEMENT_MESH", f"{path}.batch_mesh_id", "placement references a mesh absent from the native package")
                if placement["collision_binding"] is not None:
                    self.add("COLLISION_NONCLAIM", f"{path}.collision_binding", "placement collision binding must be null")
                if dimensions is not None and any(component <= 0.0 for component in dimensions):
                    self.add("PLACEMENT_DIMENSIONS", f"{path}.dimensions_m", "placement dimensions must be positive")
                if category == "curb":
                    if identifier not in {"curb_left", "curb_right"} or surface_id != identifier:
                        self.add("CURB_PLACEMENT_BINDING", path, "curb placement ID must bind the same named curb surface")
                    if surface_id is not None:
                        curb_surface_ids.add(surface_id)
                    geometry = self.surface_geometry.get(surface_id or "")
                    if geometry is None:
                        self.add("CURB_PLACEMENT_BINDING", f"{path}.surface_id", "curb placement references no decoded curb surface")
                    elif dimensions is not None and position is not None and rotation is not None:
                        component = geometry["component"]
                        minimum = component["bounds_min"]
                        maximum = component["bounds_max"]
                        actual_position = tuple(
                            (minimum[axis] + maximum[axis]) * 0.5
                            for axis in range(3)
                        )
                        actual_dimensions = tuple(
                            maximum[axis] - minimum[axis]
                            for axis in range(3)
                        )
                        if (
                            batch_mesh != geometry["mesh_id"]
                            or not close(rotation, 0.0)
                            or not same_vector(position, actual_position)
                            or not same_vector(dimensions, actual_dimensions)
                        ):
                            self.add("CURB_PLACEMENT_GEOMETRY", path, "curb placement does not equal its decoded GLB component bounds")
                elif surface_id_value is not None:
                    self.add("PLACEMENT_SURFACE_BINDING", f"{path}.surface_id", "only curb placements bind alignment surfaces in A1")

        required_categories = {"barrier", "barrier_post", "calibration_gate", "calibration_marker", "curb", "lane_marking"}
        if not required_categories.issubset(categories):
            self.add("PLACEMENT_COVERAGE", "$.placements", "required barrier, curb, marking, and calibration placements are missing")
        if curb_surface_ids != {"curb_left", "curb_right"}:
            self.add("CURB_PLACEMENT_BINDING", "$.placements", "both curb surfaces require one explicit placement binding")
        if self.native_mesh_ids and surface_mesh_ids | placement_mesh_ids != self.native_mesh_ids:
            self.add("MESH_COVERAGE", "$", "every native mesh must be covered by a surface or explicit placement")

        seams = root["seams"]
        expected_seams = (
            ("shoulder_left", "curb_left", -4.15, "curb_shoulder_vertical_face"),
            ("curb_left", "dry_asphalt", -4.0, "road_curb_vertical_face"),
            ("dry_asphalt", "curb_right", 4.0, "road_curb_vertical_face"),
            ("curb_right", "shoulder_right", 4.15, "curb_shoulder_vertical_face"),
        )
        if not isinstance(seams, list) or len(seams) != len(expected_seams):
            self.add("SEAMS_INVALID", "$.seams", "A1 course requires four explicit shoulder/curb/road boundaries")
        else:
            seam_positions: list[float] = []
            for index, (value, expected) in enumerate(zip(seams, expected_seams)):
                path = f"$.seams[{index}]"
                seam = self.record(
                    value,
                    (
                        "boundary_kind",
                        "left_surface",
                        "lower_surface_y_m",
                        "right_surface",
                        "seam_x_m",
                        "upper_surface_y_m",
                        "vertical_face_max_y_m",
                        "vertical_face_mesh_id",
                        "vertical_face_min_y_m",
                        "z_range_m",
                    ),
                    path,
                )
                if seam is None:
                    continue
                left_id = seam["left_surface"] if isinstance(seam["left_surface"], str) else ""
                right_id = seam["right_surface"] if isinstance(seam["right_surface"], str) else ""
                seam_x = self.number(seam["seam_x_m"], f"{path}.seam_x_m")
                lower = self.number(seam["lower_surface_y_m"], f"{path}.lower_surface_y_m")
                upper = self.number(seam["upper_surface_y_m"], f"{path}.upper_surface_y_m")
                face_minimum = self.number(seam["vertical_face_min_y_m"], f"{path}.vertical_face_min_y_m")
                face_maximum = self.number(seam["vertical_face_max_y_m"], f"{path}.vertical_face_max_y_m")
                z_range = self.vector(seam["z_range_m"], 2, f"{path}.z_range_m")
                face_mesh = self.identifier(seam["vertical_face_mesh_id"], f"{path}.vertical_face_mesh_id")
                if (left_id, right_id, seam["boundary_kind"]) != (expected[0], expected[1], expected[3]):
                    self.add("SEAM_ROLE", path, "seam role/order does not match the A1 shoulder-curb-road topology")
                if seam_x is not None:
                    seam_positions.append(seam_x)
                    if not close(seam_x, expected[2]):
                        self.add("SEAM_POSITION", f"{path}.seam_x_m", "seam is not at the exact A1 road-curb or curb-shoulder boundary")
                left = self.surface_geometry.get(left_id)
                right = self.surface_geometry.get(right_id)
                curb = (
                    left
                    if left_id.startswith("curb_")
                    else right
                    if right_id.startswith("curb_")
                    else None
                )
                if left is None or right is None or curb is None:
                    self.add("SEAM_SURFACE", path, "seam references missing decoded surface geometry")
                    continue
                expected_x = left["bounds"][1]
                expected_right_x = right["bounds"][0]
                expected_z = (
                    max(left["bounds"][2], right["bounds"][2]),
                    min(left["bounds"][3], right["bounds"][3]),
                )
                expected_lower = min(left["visual_y"], right["visual_y"])
                expected_upper = max(left["visual_y"], right["visual_y"])
                expected_face_range = curb["y_range"]
                if (
                    seam_x is None
                    or not close(expected_x, expected_right_x)
                    or not close(seam_x, expected_x)
                ):
                    self.add("SEAM_GEOMETRY", f"{path}.seam_x_m", "seam X must equal both decoded surface polygon boundaries")
                if (
                    z_range is None
                    or expected_z[0] >= expected_z[1]
                    or not same_vector(z_range, expected_z)
                ):
                    self.add("SEAM_GEOMETRY", f"{path}.z_range_m", "seam Z range must equal the decoded shared polygon boundary")
                if (
                    lower is None
                    or upper is None
                    or not close(lower, expected_lower)
                    or not close(upper, expected_upper)
                ):
                    self.add("SEAM_HEIGHT", path, "logical seam heights must equal the adjacent decoded surface heights")
                if (
                    face_minimum is None
                    or face_maximum is None
                    or not same_vector((face_minimum, face_maximum), expected_face_range)
                ):
                    self.add("SEAM_FACE_HEIGHT", path, "vertical-face height range must equal the decoded curb component")
                if face_mesh != curb["mesh_id"]:
                    self.add("SEAM_FACE_MESH", f"{path}.vertical_face_mesh_id", "vertical face must bind the decoded curb mesh")
                if (
                    seam_x is not None
                    and z_range is not None
                    and face_minimum is not None
                    and face_maximum is not None
                ):
                    self.validate_vertical_face(
                        curb["component"],
                        seam_x,
                        z_range,
                        (face_minimum, face_maximum),
                        path,
                    )
            if seam_positions != sorted(seam_positions):
                self.add("SEAM_ORDER", "$.seams", "seams must be ordered by X")

    def report(self) -> dict[str, Any]:
        payload = self.path.read_bytes() if self.path.is_file() and not self.path.is_symlink() else b""
        value = self.value or {}
        return {
            "diagnostics": self.diagnostics,
            "format": "ror-native-course-alignment-validation-v1",
            "source": {
                "bytes": len(payload),
                "path": self.path.as_posix(),
                "sha256": hashlib.sha256(payload).hexdigest(),
            },
            "summary": {
                "placements": len(value.get("placements", [])) if isinstance(value.get("placements"), list) else 0,
                "seams": len(value.get("seams", [])) if isinstance(value.get("seams"), list) else 0,
                "surfaces": len(value.get("surfaces", [])) if isinstance(value.get("surfaces"), list) else 0,
                "valid": not self.diagnostics,
            },
        }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("alignment", type=Path)
    parser.add_argument("--repo-root", type=Path, default=Path.cwd())
    args = parser.parse_args()
    root = args.repo_root.resolve()
    path = args.alignment if args.alignment.is_absolute() else root / args.alignment
    validator = Validator(path.absolute(), root)
    validator.validate()
    print(json.dumps(validator.report(), ensure_ascii=True, sort_keys=True, separators=(",", ":")))
    return 0 if not validator.diagnostics else 1


if __name__ == "__main__":
    raise SystemExit(main())
