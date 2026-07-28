#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail-closed validation for project-authored CityWorld Next assets.

The validator uses only the Python standard library and emits canonical JSON.
It validates the asset manifest, artifact hashes, GLB structure, PBR material
coverage, render LODs, connector continuity, and welded collision topology.
"""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict, deque
from dataclasses import dataclass
import hashlib
import json
import math
from pathlib import Path, PurePosixPath
import struct
import sys
from typing import Any, Iterable


ASSET_FORMAT = "ror-cityworld-asset-v1"
REPORT_FORMAT = "ror-cityworld-asset-validation-v1"
GLB_MAGIC = b"glTF"
GLB_JSON_CHUNK = 0x4E4F534A
GLB_BIN_CHUNK = 0x004E4942
MAX_MANIFEST_BYTES = 4 * 1024 * 1024
MAX_GLB_BYTES = 128 * 1024 * 1024
MAX_SOURCE_BYTES = 512 * 1024 * 1024
POSITION_EPSILON = 1e-6

COMPONENT_TYPES: dict[int, tuple[str, int]] = {
    5120: ("b", 1),
    5121: ("B", 1),
    5122: ("h", 2),
    5123: ("H", 2),
    5125: ("I", 4),
    5126: ("f", 4),
}
ACCESSOR_WIDTHS = {
    "SCALAR": 1,
    "VEC2": 2,
    "VEC3": 3,
    "VEC4": 4,
    "MAT2": 4,
    "MAT3": 9,
    "MAT4": 16,
}


class DuplicateKeyError(ValueError):
    pass


@dataclass(frozen=True)
class Diagnostic:
    code: str
    path: str
    message: str

    def as_dict(self) -> dict[str, str]:
        return {"code": self.code, "message": self.message, "path": self.path}


def reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise DuplicateKeyError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def canonical_json(data: Any) -> str:
    return json.dumps(data, ensure_ascii=True, sort_keys=True, separators=(",", ":"))


def sha256_file(path: Path, *, max_bytes: int) -> str:
    size = path.stat().st_size
    if size > max_bytes:
        raise ValueError(f"file exceeds {max_bytes} byte limit")
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def is_sha256(value: Any) -> bool:
    return (
        isinstance(value, str)
        and len(value) == 64
        and all(character in "0123456789abcdef" for character in value)
    )


def safe_relative_path(value: Any) -> str | None:
    if not isinstance(value, str) or not value or "\\" in value:
        return None
    path = PurePosixPath(value)
    if path.is_absolute() or any(part in ("", ".", "..") for part in path.parts):
        return None
    if path.as_posix() != value:
        return None
    return value


def resolve_beneath(root: Path, relative: str) -> Path:
    candidate = (root / relative).resolve()
    candidate.relative_to(root)
    return candidate


def finite_numbers(value: Any, *, pointer: str = "$") -> Iterable[tuple[str, float]]:
    if isinstance(value, bool) or value is None or isinstance(value, str):
        return
    if isinstance(value, (int, float)):
        yield pointer, float(value)
        return
    if isinstance(value, list):
        for index, item in enumerate(value):
            yield from finite_numbers(item, pointer=f"{pointer}[{index}]")
        return
    if isinstance(value, dict):
        for key in sorted(value):
            yield from finite_numbers(value[key], pointer=f"{pointer}.{key}")


def vector_sub(a: tuple[float, float, float], b: tuple[float, float, float]) -> tuple[float, float, float]:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def vector_cross(a: tuple[float, float, float], b: tuple[float, float, float]) -> tuple[float, float, float]:
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def vector_dot(a: tuple[float, float, float], b: tuple[float, float, float]) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def vector_length(value: tuple[float, float, float]) -> float:
    return math.sqrt(vector_dot(value, value))


class Glb:
    def __init__(self, document: dict[str, Any], binary: bytes):
        self.document = document
        self.binary = binary

    @classmethod
    def read(cls, path: Path) -> "Glb":
        size = path.stat().st_size
        if size > MAX_GLB_BYTES:
            raise ValueError(f"GLB exceeds {MAX_GLB_BYTES} byte limit")
        data = path.read_bytes()
        if len(data) < 20 or data[:4] != GLB_MAGIC:
            raise ValueError("invalid GLB magic or truncated header")
        magic, version, declared_length = struct.unpack_from("<4sII", data, 0)
        if magic != GLB_MAGIC or version != 2:
            raise ValueError("GLB must use version 2")
        if declared_length != len(data):
            raise ValueError("GLB declared length does not match file length")

        offset = 12
        json_bytes: bytes | None = None
        binary = b""
        while offset < len(data):
            if offset + 8 > len(data):
                raise ValueError("truncated GLB chunk header")
            chunk_length, chunk_type = struct.unpack_from("<II", data, offset)
            offset += 8
            end = offset + chunk_length
            if end > len(data):
                raise ValueError("GLB chunk exceeds declared file length")
            chunk = data[offset:end]
            offset = end
            if chunk_type == GLB_JSON_CHUNK:
                if json_bytes is not None:
                    raise ValueError("GLB contains multiple JSON chunks")
                json_bytes = chunk
            elif chunk_type == GLB_BIN_CHUNK:
                if binary:
                    raise ValueError("GLB contains multiple BIN chunks")
                binary = chunk
            else:
                raise ValueError(f"unsupported GLB chunk type 0x{chunk_type:08x}")
        if json_bytes is None:
            raise ValueError("GLB has no JSON chunk")
        try:
            document = json.loads(
                json_bytes.rstrip(b" \t\r\n\x00").decode("utf-8"),
                object_pairs_hook=reject_duplicate_keys,
                parse_constant=lambda token: (_ for _ in ()).throw(
                    ValueError(f"non-finite JSON number: {token}")
                ),
            )
        except (UnicodeDecodeError, json.JSONDecodeError, DuplicateKeyError) as error:
            raise ValueError(f"invalid GLB JSON: {error}") from error
        if not isinstance(document, dict):
            raise ValueError("GLB JSON root must be an object")
        return cls(document, binary)

    def accessor(self, index: int) -> list[Any]:
        accessors = self.document.get("accessors", [])
        views = self.document.get("bufferViews", [])
        if not isinstance(index, int) or index < 0 or index >= len(accessors):
            raise ValueError(f"invalid accessor index {index}")
        accessor = accessors[index]
        if "sparse" in accessor:
            raise ValueError(f"sparse accessor {index} is not allowed")
        view_index = accessor.get("bufferView")
        if not isinstance(view_index, int) or view_index < 0 or view_index >= len(views):
            raise ValueError(f"accessor {index} has invalid bufferView")
        view = views[view_index]
        if view.get("buffer", 0) != 0:
            raise ValueError(f"accessor {index} references a non-GLB buffer")

        component_type = accessor.get("componentType")
        accessor_type = accessor.get("type")
        if component_type not in COMPONENT_TYPES or accessor_type not in ACCESSOR_WIDTHS:
            raise ValueError(f"accessor {index} uses an unsupported encoding")
        format_code, component_size = COMPONENT_TYPES[component_type]
        width = ACCESSOR_WIDTHS[accessor_type]
        element_size = component_size * width
        stride = view.get("byteStride", element_size)
        if not isinstance(stride, int) or stride < element_size:
            raise ValueError(f"accessor {index} has invalid byte stride")
        count = accessor.get("count")
        if not isinstance(count, int) or count < 0:
            raise ValueError(f"accessor {index} has invalid count")
        start = int(view.get("byteOffset", 0)) + int(accessor.get("byteOffset", 0))
        if count:
            end = start + (count - 1) * stride + element_size
            if start < 0 or end > len(self.binary):
                raise ValueError(f"accessor {index} exceeds the BIN chunk")

        unpack_format = "<" + format_code * width
        values: list[Any] = []
        for item_index in range(count):
            unpacked = struct.unpack_from(
                unpack_format,
                self.binary,
                start + item_index * stride,
            )
            values.append(unpacked[0] if width == 1 else tuple(float(v) for v in unpacked))
        return values

    def node_by_name(self, name: str) -> dict[str, Any] | None:
        for node in self.document.get("nodes", []):
            if node.get("name") == name:
                return node
        return None

    def mesh_for_node(self, node: dict[str, Any]) -> dict[str, Any]:
        meshes = self.document.get("meshes", [])
        mesh_index = node.get("mesh")
        if not isinstance(mesh_index, int) or mesh_index < 0 or mesh_index >= len(meshes):
            raise ValueError(f"node {node.get('name')} has no valid mesh")
        return meshes[mesh_index]

    def primitive_triangles(self, primitive: dict[str, Any]) -> list[tuple[int, int, int]]:
        if primitive.get("mode", 4) != 4:
            raise ValueError("only TRIANGLES primitives are allowed")
        attributes = primitive.get("attributes", {})
        position_index = attributes.get("POSITION")
        if not isinstance(position_index, int):
            raise ValueError("primitive has no POSITION accessor")
        if "indices" in primitive:
            raw_indices = self.accessor(primitive["indices"])
            indices = [int(value) for value in raw_indices]
        else:
            indices = list(range(len(self.accessor(position_index))))
        if len(indices) % 3:
            raise ValueError("triangle primitive index count is not divisible by three")
        return [
            (indices[offset], indices[offset + 1], indices[offset + 2])
            for offset in range(0, len(indices), 3)
        ]

    def mesh_triangle_count(self, mesh: dict[str, Any]) -> int:
        return sum(
            len(self.primitive_triangles(primitive))
            for primitive in mesh.get("primitives", [])
        )


class Validator:
    def __init__(self, repo_root: Path, manifest_path: Path):
        self.repo_root = repo_root.resolve()
        self.manifest_path = manifest_path.resolve()
        self.diagnostics: list[Diagnostic] = []
        self.manifest: dict[str, Any] | None = None
        self.glb: Glb | None = None
        self.glb_path: Path | None = None
        self.stats = {
            "collision_objects": 0,
            "glb_materials": 0,
            "glb_nodes": 0,
            "lod_objects": 0,
            "triangles": 0,
        }

    def add(self, code: str, path: str, message: str) -> None:
        self.diagnostics.append(Diagnostic(code, path, message))

    def load_manifest(self) -> None:
        try:
            self.manifest_path.relative_to(self.repo_root)
        except ValueError:
            self.add("MANIFEST_OUTSIDE_ROOT", "$", "manifest is outside repository root")
            return
        try:
            if self.manifest_path.stat().st_size > MAX_MANIFEST_BYTES:
                raise ValueError(f"manifest exceeds {MAX_MANIFEST_BYTES} byte limit")
            self.manifest = json.loads(
                self.manifest_path.read_text(encoding="utf-8"),
                object_pairs_hook=reject_duplicate_keys,
                parse_constant=lambda token: (_ for _ in ()).throw(
                    ValueError(f"non-finite JSON number: {token}")
                ),
            )
        except (OSError, UnicodeDecodeError, json.JSONDecodeError, DuplicateKeyError, ValueError) as error:
            self.add("MANIFEST_INVALID", "$", str(error))
            return
        if not isinstance(self.manifest, dict):
            self.add("MANIFEST_ROOT", "$", "manifest root must be an object")
            self.manifest = None
            return
        if self.manifest.get("format") != ASSET_FORMAT:
            self.add("MANIFEST_FORMAT", "$.format", f"expected {ASSET_FORMAT}")
        for pointer, number in finite_numbers(self.manifest):
            if not math.isfinite(number):
                self.add("NONFINITE_NUMBER", pointer, "number must be finite")

    def artifact_path(self, name: str) -> Path | None:
        assert self.manifest is not None
        entry = self.manifest.get("artifacts", {}).get(name)
        pointer = f"$.artifacts.{name}"
        if not isinstance(entry, dict):
            self.add("ARTIFACT_MISSING", pointer, "artifact record is required")
            return None
        relative = safe_relative_path(entry.get("path"))
        expected_hash = entry.get("sha256")
        if relative is None:
            self.add("ARTIFACT_PATH", f"{pointer}.path", "path is not portable and relative")
            return None
        if not is_sha256(expected_hash):
            self.add("ARTIFACT_HASH", f"{pointer}.sha256", "invalid SHA-256")
            return None
        try:
            path = resolve_beneath(self.repo_root, relative)
        except ValueError:
            self.add("ARTIFACT_ESCAPE", f"{pointer}.path", "path escapes repository root")
            return None
        if not path.is_file() or path.is_symlink():
            self.add("ARTIFACT_FILE", f"{pointer}.path", "regular artifact file is missing")
            return None
        try:
            actual_hash = sha256_file(path, max_bytes=MAX_SOURCE_BYTES)
        except (OSError, ValueError) as error:
            self.add("ARTIFACT_READ", f"{pointer}.path", str(error))
            return None
        if actual_hash != expected_hash:
            self.add("ARTIFACT_STALE", pointer, "artifact SHA-256 does not match")
        return path

    def validate_artifacts(self) -> None:
        assert self.manifest is not None
        blend_path = self.artifact_path("blend")
        self.glb_path = self.artifact_path("glb")
        preview_path = self.artifact_path("preview")

        if blend_path is not None:
            try:
                with blend_path.open("rb") as handle:
                    if handle.read(7) != b"BLENDER":
                        self.add("BLEND_HEADER", "$.artifacts.blend", "invalid Blender file header")
            except OSError as error:
                self.add("BLEND_READ", "$.artifacts.blend", str(error))

        if preview_path is not None:
            try:
                data = preview_path.read_bytes()
                if len(data) < 24 or data[:8] != b"\x89PNG\r\n\x1a\n":
                    self.add("PREVIEW_FORMAT", "$.artifacts.preview", "preview must be PNG")
                else:
                    width, height = struct.unpack_from(">II", data, 16)
                    if (width, height) != (1280, 720):
                        self.add(
                            "PREVIEW_DIMENSIONS",
                            "$.artifacts.preview",
                            "preview must be 1280x720",
                        )
            except OSError as error:
                self.add("PREVIEW_READ", "$.artifacts.preview", str(error))

        generator = self.manifest.get("authoring", {}).get("generator", {})
        relative = safe_relative_path(generator.get("path"))
        expected_hash = generator.get("sha256")
        if relative is None or not is_sha256(expected_hash):
            self.add("GENERATOR_RECORD", "$.authoring.generator", "invalid generator record")
        else:
            try:
                generator_path = resolve_beneath(self.repo_root, relative)
                actual_hash = sha256_file(generator_path, max_bytes=MAX_SOURCE_BYTES)
                if actual_hash != expected_hash:
                    self.add(
                        "GENERATOR_STALE",
                        "$.authoring.generator.sha256",
                        "generator SHA-256 does not match",
                    )
            except (OSError, ValueError) as error:
                self.add("GENERATOR_READ", "$.authoring.generator.path", str(error))

    def load_glb(self) -> None:
        if self.glb_path is None:
            return
        try:
            self.glb = Glb.read(self.glb_path)
        except (OSError, ValueError) as error:
            self.add("GLB_INVALID", "$.artifacts.glb", str(error))
            return
        document = self.glb.document
        self.stats["glb_materials"] = len(document.get("materials", []))
        self.stats["glb_nodes"] = len(document.get("nodes", []))
        if document.get("asset", {}).get("version") != "2.0":
            self.add("GLTF_VERSION", "$.glb.asset.version", "glTF asset version must be 2.0")
        allowlisted = set(self.manifest.get("export", {}).get("allowlisted_extensions", []))
        used = set(document.get("extensionsUsed", []))
        required = set(document.get("extensionsRequired", []))
        if used - allowlisted or required - allowlisted:
            self.add(
                "GLTF_EXTENSION",
                "$.glb.extensionsUsed",
                "GLB uses an extension outside the manifest allowlist",
            )
        if document.get("images") or document.get("textures"):
            self.add(
                "GLTF_EMBEDDED_TEXTURE",
                "$.glb",
                "initial bridge profile must not embed image or texture payloads",
            )
        if document.get("animations") or document.get("skins"):
            self.add("GLTF_ANIMATION", "$.glb", "static bridge asset cannot contain animation or skins")
        if document.get("cameras"):
            self.add("GLTF_CAMERA", "$.glb.cameras", "preview cameras cannot enter the GLB")
        for pointer, number in finite_numbers(document, pointer="$.glb"):
            if not math.isfinite(number):
                self.add("GLTF_NONFINITE", pointer, "number must be finite")

    def validate_materials(self) -> None:
        assert self.manifest is not None and self.glb is not None
        declared = self.manifest.get("materials")
        if not isinstance(declared, list) or not declared:
            self.add("MATERIAL_MANIFEST", "$.materials", "material declarations are required")
            return
        glb_materials = self.glb.document.get("materials", [])
        by_name = {material.get("name"): material for material in glb_materials}
        declared_names: set[str] = set()
        for index, declaration in enumerate(declared):
            pointer = f"$.materials[{index}]"
            if not isinstance(declaration, dict):
                self.add("MATERIAL_RECORD", pointer, "material record must be an object")
                continue
            name = declaration.get("name")
            if not isinstance(name, str) or not name.startswith("rorng_"):
                self.add("MATERIAL_NAME", f"{pointer}.name", "material name must use rorng_ prefix")
                continue
            if name in declared_names:
                self.add("MATERIAL_DUPLICATE", f"{pointer}.name", "material name is duplicated")
            declared_names.add(name)
            material = by_name.get(name)
            if material is None:
                self.add("MATERIAL_MISSING", pointer, f"{name} is absent from GLB")
                continue
            pbr = material.get("pbrMetallicRoughness")
            if not isinstance(pbr, dict):
                self.add("MATERIAL_PBR", pointer, f"{name} has no metallic-roughness PBR block")
                continue
            resolved_base_color = pbr.get("baseColorFactor", [1.0, 1.0, 1.0, 1.0])
            resolved_metallic = pbr.get("metallicFactor", 1.0)
            resolved_roughness = pbr.get("roughnessFactor", 1.0)
            declared_base_color = declaration.get("base_color_factor_linear")
            declared_metallic = declaration.get("metallic_factor")
            declared_roughness = declaration.get("roughness_factor")
            if (
                not isinstance(resolved_base_color, list)
                or not isinstance(declared_base_color, list)
                or len(resolved_base_color) != 4
                or len(declared_base_color) != 4
                or any(
                    abs(float(actual) - float(expected)) > POSITION_EPSILON
                    for actual, expected in zip(resolved_base_color, declared_base_color)
                )
                or not isinstance(declared_metallic, (int, float))
                or abs(float(resolved_metallic) - float(declared_metallic)) > POSITION_EPSILON
                or not isinstance(declared_roughness, (int, float))
                or abs(float(resolved_roughness) - float(declared_roughness)) > POSITION_EPSILON
            ):
                self.add(
                    "MATERIAL_FACTOR",
                    pointer,
                    f"{name} resolved glTF factors do not match the manifest",
                )
            if declaration.get("color_space") != "linear-factor":
                self.add("MATERIAL_COLOR_SPACE", pointer, "factor colour space must be explicit")
            if any(key.endswith("Texture") for key in pbr):
                self.add("MATERIAL_TEXTURE", pointer, "initial texture-free profile has a PBR texture")
        actual_names = {name for name in by_name if isinstance(name, str)}
        if actual_names != declared_names:
            self.add(
                "MATERIAL_COVERAGE",
                "$.materials",
                "manifest material names do not exactly cover GLB materials",
            )

        for mesh_index, mesh in enumerate(self.glb.document.get("meshes", [])):
            for primitive_index, primitive in enumerate(mesh.get("primitives", [])):
                material_index = primitive.get("material")
                if not isinstance(material_index, int) or not 0 <= material_index < len(glb_materials):
                    self.add(
                        "PRIMITIVE_MATERIAL",
                        f"$.glb.meshes[{mesh_index}].primitives[{primitive_index}]",
                        "every primitive requires a valid material",
                    )

    def validate_lods(self) -> None:
        assert self.manifest is not None and self.glb is not None
        geometry = self.manifest.get("geometry", {})
        lods = geometry.get("lods")
        if not isinstance(lods, list) or len(lods) != 3:
            self.add("LOD_MANIFEST", "$.geometry.lods", "exactly LOD0, LOD1 and LOD2 are required")
            return
        counts: dict[int, int] = {}
        for index, entry in enumerate(lods):
            pointer = f"$.geometry.lods[{index}]"
            if not isinstance(entry, dict):
                self.add("LOD_RECORD", pointer, "LOD record must be an object")
                continue
            lod = entry.get("lod")
            name = entry.get("name")
            expected_triangles = entry.get("triangles")
            if lod not in (0, 1, 2) or not isinstance(name, str):
                self.add("LOD_IDENTITY", pointer, "invalid LOD identity")
                continue
            node = self.glb.node_by_name(name)
            if node is None:
                self.add("LOD_NODE", pointer, f"GLB node {name} is missing")
                continue
            try:
                mesh = self.glb.mesh_for_node(node)
                actual_triangles = self.glb.mesh_triangle_count(mesh)
            except ValueError as error:
                self.add("LOD_MESH", pointer, str(error))
                continue
            counts[lod] = actual_triangles
            if actual_triangles != expected_triangles:
                self.add("LOD_TRIANGLES", pointer, "manifest triangle count does not match GLB")
            extras = node.get("extras", {})
            if extras.get("rorng_role") != "render" or extras.get("rorng_lod") != lod:
                self.add("LOD_EXTRAS", pointer, "GLB node is missing render/LOD metadata")
            for primitive_index, primitive in enumerate(mesh.get("primitives", [])):
                attributes = set(primitive.get("attributes", {}))
                required = {"POSITION", "NORMAL", "TANGENT", "TEXCOORD_0"}
                if not required.issubset(attributes):
                    self.add(
                        "LOD_ATTRIBUTES",
                        f"{pointer}.primitives[{primitive_index}]",
                        f"missing attributes: {', '.join(sorted(required - attributes))}",
                    )

        self.stats["lod_objects"] = len(counts)
        self.stats["triangles"] += sum(counts.values())
        if set(counts) != {0, 1, 2}:
            return
        ceiling = geometry.get("lod0_triangle_ceiling")
        if not isinstance(ceiling, int) or counts[0] > ceiling:
            self.add("LOD0_BUDGET", "$.geometry.lod0_triangle_ceiling", "LOD0 exceeds triangle ceiling")
        if counts[0] <= 0:
            self.add("LOD0_EMPTY", "$.geometry.lods", "LOD0 must contain triangles")
            return
        lod1_limit = geometry.get("lod1_max_ratio")
        lod2_limit = geometry.get("lod2_max_ratio")
        if not isinstance(lod1_limit, (int, float)) or counts[1] / counts[0] > lod1_limit:
            self.add("LOD1_RATIO", "$.geometry.lod1_max_ratio", "LOD1 reduction is insufficient")
        if not isinstance(lod2_limit, (int, float)) or counts[2] / counts[0] > lod2_limit:
            self.add("LOD2_RATIO", "$.geometry.lod2_max_ratio", "LOD2 reduction is insufficient")

    @staticmethod
    def welded_key(position: tuple[float, float, float]) -> tuple[int, int, int]:
        return tuple(round(component / POSITION_EPSILON) for component in position)

    def validate_collision_mesh(
        self,
        *,
        node: dict[str, Any],
        pointer: str,
    ) -> None:
        assert self.glb is not None
        try:
            mesh = self.glb.mesh_for_node(node)
        except ValueError as error:
            self.add("COLLISION_MESH", pointer, str(error))
            return
        primitives = mesh.get("primitives", [])
        if len(primitives) != 1:
            self.add("COLLISION_PRIMITIVE", pointer, "collision object must have one primitive")
            return
        primitive = primitives[0]
        attributes = primitive.get("attributes", {})
        if "POSITION" not in attributes or "NORMAL" not in attributes:
            self.add("COLLISION_ATTRIBUTES", pointer, "collision requires POSITION and NORMAL")
            return
        try:
            positions_raw = self.glb.accessor(attributes["POSITION"])
            positions = [
                (float(value[0]), float(value[1]), float(value[2]))
                for value in positions_raw
            ]
            triangles = self.glb.primitive_triangles(primitive)
        except (ValueError, TypeError, IndexError) as error:
            self.add("COLLISION_ACCESSOR", pointer, str(error))
            return
        if not positions or not triangles:
            self.add("COLLISION_EMPTY", pointer, "collision mesh cannot be empty")
            return

        keys = [self.welded_key(position) for position in positions]
        unique_positions: dict[tuple[int, int, int], tuple[float, float, float]] = {}
        for key, position in zip(keys, positions):
            unique_positions.setdefault(key, position)
        center = (
            sum(position[0] for position in unique_positions.values()) / len(unique_positions),
            sum(position[1] for position in unique_positions.values()) / len(unique_positions),
            sum(position[2] for position in unique_positions.values()) / len(unique_positions),
        )

        edge_counts: Counter[tuple[tuple[int, int, int], tuple[int, int, int]]] = Counter()
        adjacency: dict[tuple[int, int, int], set[tuple[int, int, int]]] = defaultdict(set)
        degenerate = 0
        inward = 0
        for triangle in triangles:
            try:
                indices = (triangle[0], triangle[1], triangle[2])
                points = (positions[indices[0]], positions[indices[1]], positions[indices[2]])
                triangle_keys = (keys[indices[0]], keys[indices[1]], keys[indices[2]])
            except IndexError:
                self.add("COLLISION_INDEX", pointer, "collision index is out of range")
                return
            edge_a = vector_sub(points[1], points[0])
            edge_b = vector_sub(points[2], points[0])
            normal = vector_cross(edge_a, edge_b)
            if vector_length(normal) <= POSITION_EPSILON:
                degenerate += 1
            centroid = (
                (points[0][0] + points[1][0] + points[2][0]) / 3.0,
                (points[0][1] + points[1][1] + points[2][1]) / 3.0,
                (points[0][2] + points[1][2] + points[2][2]) / 3.0,
            )
            if vector_dot(normal, vector_sub(centroid, center)) <= POSITION_EPSILON:
                inward += 1
            for start, end in (
                (triangle_keys[0], triangle_keys[1]),
                (triangle_keys[1], triangle_keys[2]),
                (triangle_keys[2], triangle_keys[0]),
            ):
                if start == end:
                    degenerate += 1
                    continue
                edge = tuple(sorted((start, end)))
                edge_counts[edge] += 1
                adjacency[start].add(end)
                adjacency[end].add(start)

        if degenerate:
            self.add("COLLISION_DEGENERATE", pointer, f"{degenerate} degenerate elements found")
        open_or_nonmanifold = sum(1 for count in edge_counts.values() if count != 2)
        if open_or_nonmanifold:
            self.add(
                "COLLISION_WATERTIGHT",
                pointer,
                f"{open_or_nonmanifold} welded edges do not have exactly two faces",
            )
        if inward:
            self.add("COLLISION_WINDING", pointer, f"{inward} triangle windings are not outward")

        start = next(iter(unique_positions))
        visited = {start}
        queue: deque[tuple[int, int, int]] = deque([start])
        while queue:
            current = queue.popleft()
            for neighbour in adjacency[current]:
                if neighbour not in visited:
                    visited.add(neighbour)
                    queue.append(neighbour)
        if len(visited) != len(unique_positions):
            self.add("COLLISION_CONNECTED", pointer, "collision mesh has multiple welded components")

    def validate_collisions(self) -> None:
        assert self.manifest is not None and self.glb is not None
        objects = self.manifest.get("collision", {}).get("objects")
        if not isinstance(objects, list) or len(objects) != 3:
            self.add(
                "COLLISION_MANIFEST",
                "$.collision.objects",
                "road plus left/right barrier collision objects are required",
            )
            return
        bounds: list[tuple[str, list[float], list[float]]] = []
        for index, entry in enumerate(objects):
            pointer = f"$.collision.objects[{index}]"
            if not isinstance(entry, dict):
                self.add("COLLISION_RECORD", pointer, "collision record must be an object")
                continue
            name = entry.get("name")
            if not isinstance(name, str):
                self.add("COLLISION_NAME", pointer, "collision name is required")
                continue
            node = self.glb.node_by_name(name)
            if node is None:
                self.add("COLLISION_NODE", pointer, f"GLB node {name} is missing")
                continue
            extras = node.get("extras", {})
            role = entry.get("role")
            if extras.get("rorng_role") != role or not str(role).startswith("collision-"):
                self.add("COLLISION_EXTRAS", pointer, "collision role metadata does not match")
            self.validate_collision_mesh(node=node, pointer=pointer)
            try:
                mesh = self.glb.mesh_for_node(node)
                triangles = self.glb.mesh_triangle_count(mesh)
                if triangles != entry.get("triangles"):
                    self.add("COLLISION_TRIANGLES", pointer, "triangle count does not match GLB")
                self.stats["triangles"] += triangles
            except ValueError as error:
                self.add("COLLISION_TRIANGLES", pointer, str(error))
            declared_bounds = entry.get("bounds_blender_z_up", {})
            minimum = declared_bounds.get("min")
            maximum = declared_bounds.get("max")
            if (
                isinstance(minimum, list)
                and isinstance(maximum, list)
                and len(minimum) == 3
                and len(maximum) == 3
            ):
                bounds.append((name, [float(v) for v in minimum], [float(v) for v in maximum]))

        self.stats["collision_objects"] = len(objects)
        for first_index, (first_name, first_min, first_max) in enumerate(bounds):
            for second_name, second_min, second_max in bounds[first_index + 1 :]:
                overlaps = all(
                    min(first_max[axis], second_max[axis])
                    - max(first_min[axis], second_min[axis])
                    > POSITION_EPSILON
                    for axis in range(3)
                )
                if overlaps:
                    self.add(
                        "COLLISION_INTERSECTION",
                        "$.collision.objects",
                        f"{first_name} and {second_name} AABBs overlap",
                    )

    def validate_connectors(self) -> None:
        assert self.manifest is not None
        connectors = self.manifest.get("connectors")
        if not isinstance(connectors, list) or len(connectors) != 2:
            self.add("CONNECTOR_COUNT", "$.connectors", "start and end connectors are required")
            return
        by_id = {
            connector.get("id"): connector
            for connector in connectors
            if isinstance(connector, dict)
        }
        if set(by_id) != {"start", "end"}:
            self.add("CONNECTOR_IDS", "$.connectors", "connector IDs must be start and end")
            return
        start = by_id["start"]
        end = by_id["end"]
        try:
            start_position = tuple(float(v) for v in start["position_blender_z_up_m"])
            end_position = tuple(float(v) for v in end["position_blender_z_up_m"])
            separation = vector_length(vector_sub(end_position, start_position))
        except (KeyError, TypeError, ValueError):
            self.add("CONNECTOR_POSITION", "$.connectors", "connector positions are invalid")
            return
        expected_length = self.manifest.get("geometry", {}).get("bridge_length_m")
        if not isinstance(expected_length, (int, float)) or abs(separation - expected_length) > POSITION_EPSILON:
            self.add("CONNECTOR_LENGTH", "$.connectors", "connector separation is not bridge length")
        if start.get("lane_centres_x_m") != end.get("lane_centres_x_m"):
            self.add("CONNECTOR_LANES", "$.connectors", "lane centres must match at both ends")
        if start.get("road_width_m") != end.get("road_width_m"):
            self.add("CONNECTOR_WIDTH", "$.connectors", "road width must match at both ends")
        for connector_id, connector in by_id.items():
            forward = connector.get("forward")
            if not isinstance(forward, list) or len(forward) != 3:
                self.add("CONNECTOR_FORWARD", f"$.connectors.{connector_id}", "forward vector is invalid")
                continue
            length = math.sqrt(sum(float(value) ** 2 for value in forward))
            if abs(length - 1.0) > POSITION_EPSILON:
                self.add("CONNECTOR_FORWARD", f"$.connectors.{connector_id}", "forward vector is not unit")

    def validate(self) -> dict[str, Any]:
        self.load_manifest()
        if self.manifest is not None:
            self.validate_artifacts()
            self.load_glb()
        if self.manifest is not None and self.glb is not None:
            self.validate_materials()
            self.validate_lods()
            self.validate_collisions()
            self.validate_connectors()
        diagnostics = sorted(
            (diagnostic.as_dict() for diagnostic in self.diagnostics),
            key=lambda item: (item["code"], item["path"], item["message"]),
        )
        return {
            "diagnostics": diagnostics,
            "format": REPORT_FORMAT,
            "summary": {
                **self.stats,
                "errors": len(diagnostics),
                "valid": not diagnostics,
            },
        }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=Path.cwd(),
        help="repository root used for all manifest-relative paths",
    )
    parser.add_argument("--pretty", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    validator = Validator(args.repo_root, args.manifest)
    report = validator.validate()
    if args.pretty:
        sys.stdout.write(json.dumps(report, indent=2, sort_keys=True) + "\n")
    else:
        sys.stdout.write(canonical_json(report) + "\n")
    return 0 if report["summary"]["valid"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
