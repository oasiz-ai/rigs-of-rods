#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile a validated CityWorld Next glTF asset into bounded RoR resources.

The runtime never parses the interchange GLB.  This offline compiler validates
the project-owned asset contract, lowers the allowlisted static mesh profile to
deterministic OGRE XML, invokes the pinned OgreXMLConverter, and emits portable
little-endian ``.mesh`` files plus an ODEF, material fallback, and canonical
conversion report.

The initial v1 profile deliberately accepts only applied object transforms and
texture-free metallic/roughness factors.  Unsupported input fails closed rather
than being guessed at runtime.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import math
import os
from pathlib import Path, PurePosixPath
import re
import struct
import subprocess
import sys
import tempfile
from typing import Any, Iterable
import xml.etree.ElementTree as ET


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from validate_cityworld_asset import (  # noqa: E402
    ALLOWED_RUNTIME_PARENT_MATERIALS,
    Glb,
    Validator,
    canonical_json,
    reject_duplicate_keys,
)


COMPILER_FORMAT = "ror-cityworld-scene-compiler-v1"
REPORT_FORMAT = "ror-cityworld-scene-compile-report-v1"
COMPILED_RECORD_FORMAT = "ror-cityworld-compiled-asset-v1"
OGRE_MESH_HEADER = b"\x00\x10[MeshSerializer_v1.100]\n"
OGRE_CONVERTER_VERSION = "OgreXMLConverter Tsathoggua (14.5.2)"
MAX_NODES = 4_096
MAX_MESHES = 4_096
MAX_MATERIALS = 4_096
MAX_PRIMITIVES = 8_192
MAX_VERTICES = 4_000_000
MAX_INDICES = 12_000_000
MAX_OUTPUT_BYTES = 256 * 1024 * 1024
FLOAT_EPSILON = 1e-5
LOD_DISTANCES_M = (80.0, 180.0)
ASSET_ID_PATTERN = re.compile(r"^rorng_[a-z0-9_]+$")
# This compiler revision changes only mesh bindings and material declarations
# that opt into ``runtime_parent_material``. Existing checked assets were
# produced by this byte-compatible prior revision; retain their reports only
# when the opt-in is absent and all regenerated deterministic intermediates
# still match.
BYTE_COMPATIBLE_COMPILER_SHA256_WITHOUT_RUNTIME_PARENT = frozenset(
    {
        "e073ac1015198aecb609e8bf3c7b70d9013bc9d77b68d8d06d6ddfed470a4059",
    }
)
# This prior revision differs only in where the read-only ``-v`` converter
# probe writes its diagnostic log. Mesh lowering and every emitted byte are
# unchanged, including assets that opt into ``runtime_parent_material``.
BYTE_COMPATIBLE_COMPILER_SHA256 = frozenset(
    {
        "9e65172d4895cac5b033c23485cff4d3744557c76db62cd01ec083f01971ce47",
    }
)


class CompileFailure(RuntimeError):
    """A deterministic, user-facing compile failure."""


@dataclass(frozen=True)
class PrimitiveData:
    name: str
    material: str
    positions: tuple[tuple[float, float, float], ...]
    normals: tuple[tuple[float, float, float], ...]
    tangents: tuple[tuple[float, float, float, float], ...]
    texcoords: tuple[tuple[float, float], ...]
    triangles: tuple[tuple[int, int, int], ...]


@dataclass(frozen=True)
class MeshSource:
    output_name: str
    source_node: str
    role: str
    primitives: tuple[PrimitiveData, ...]
    manual_lods: tuple[tuple[float, str], ...] = ()


@dataclass(frozen=True)
class Intermediate:
    path: str
    role: str
    data: bytes


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    if not path.is_file() or path.is_symlink():
        raise CompileFailure(f"required regular file is missing: {path}")
    if path.stat().st_size > MAX_OUTPUT_BYTES:
        raise CompileFailure(f"file exceeds {MAX_OUTPUT_BYTES} byte limit: {path.name}")
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(
            path.read_text(encoding="utf-8"),
            object_pairs_hook=reject_duplicate_keys,
            parse_constant=lambda token: (_ for _ in ()).throw(
                ValueError(f"non-finite JSON number: {token}")
            ),
        )
    except (OSError, UnicodeDecodeError, json.JSONDecodeError, ValueError) as error:
        raise CompileFailure(f"cannot read {path.name}: {error}") from error
    if not isinstance(value, dict):
        raise CompileFailure(f"{path.name} root must be an object")
    return value


def canonical_pretty(value: dict[str, Any]) -> bytes:
    return (
        json.dumps(value, indent=2, ensure_ascii=True, sort_keys=True) + "\n"
    ).encode("utf-8")


def portable_relative_path(root: Path, path: Path) -> str:
    try:
        relative = path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError as error:
        raise CompileFailure(f"path escapes repository root: {path}") from error
    parsed = PurePosixPath(relative)
    if (
        not relative
        or parsed.is_absolute()
        or "\\" in relative
        or any(part in ("", ".", "..") for part in parsed.parts)
    ):
        raise CompileFailure(f"path is not portable: {relative}")
    return relative


def declared_path(root: Path, value: Any) -> Path:
    if not isinstance(value, str) or not value or "\\" in value:
        raise CompileFailure("declared path is not a portable relative path")
    parsed = PurePosixPath(value)
    if parsed.is_absolute() or any(part in ("", ".", "..") for part in parsed.parts):
        raise CompileFailure(f"unsafe declared path: {value}")
    path = (root / value).resolve()
    try:
        path.relative_to(root.resolve())
    except ValueError as error:
        raise CompileFailure(f"declared path escapes repository: {value}") from error
    return path


def finite_tuple(value: Any, width: int, label: str) -> tuple[float, ...]:
    if not isinstance(value, tuple) or len(value) != width:
        raise CompileFailure(f"{label} must contain {width} components")
    converted = tuple(float(component) for component in value)
    if not all(math.isfinite(component) for component in converted):
        raise CompileFailure(f"{label} contains a non-finite component")
    return converted


def vector_length(value: Iterable[float]) -> float:
    return math.sqrt(sum(component * component for component in value))


def stable_float(value: float) -> str:
    if not math.isfinite(value):
        raise CompileFailure("cannot serialize a non-finite floating-point value")
    if value == 0.0:
        value = 0.0
    result = format(value, ".9g")
    if result == "-0":
        return "0"
    return result


def xml_bytes(root: ET.Element) -> bytes:
    ET.indent(root, space="  ")
    return b'<?xml version="1.0" encoding="UTF-8"?>\n' + ET.tostring(
        root,
        encoding="utf-8",
        short_empty_elements=True,
    ) + b"\n"


def transformed_blender_connector(value: list[Any]) -> list[float]:
    """Blender Z-up to glTF/OGRE Y-up: (x, y, z) -> (x, z, -y)."""
    if len(value) != 3:
        raise CompileFailure("connector position must contain three components")
    x, y, z = (float(component) for component in value)
    if not all(math.isfinite(component) for component in (x, y, z)):
        raise CompileFailure("connector position contains a non-finite component")
    return [x, z, -y]


class SceneCompiler:
    def __init__(self, repo_root: Path, manifest_path: Path):
        self.repo_root = repo_root.resolve()
        self.manifest_path = manifest_path.resolve()
        self.manifest: dict[str, Any] = {}
        self.glb: Glb | None = None
        self.glb_path: Path | None = None
        self.asset_id = ""
        self.material_names: tuple[str, ...] = ()
        self.meshes: tuple[MeshSource, ...] = ()
        self.vertex_count = 0
        self.index_count = 0
        self.primitive_count = 0

    def prepare(self) -> None:
        validator = Validator(self.repo_root, self.manifest_path)
        validation = validator.validate()
        if not validation["summary"]["valid"]:
            first = validation["diagnostics"][0]
            raise CompileFailure(
                "asset validation failed: "
                f"{first['code']} at {first['path']}: {first['message']}"
            )
        if validator.manifest is None or validator.glb is None or validator.glb_path is None:
            raise CompileFailure("asset validator did not retain validated inputs")
        self.manifest = validator.manifest
        self.glb = validator.glb
        self.glb_path = validator.glb_path
        asset = self.manifest.get("asset", {})
        self.asset_id = asset.get("id", "")
        if not isinstance(self.asset_id, str) or not ASSET_ID_PATTERN.fullmatch(
            self.asset_id
        ):
            raise CompileFailure("asset ID is not a stable rorng_ identifier")
        self._validate_profile()
        self._extract_meshes()

    def _validate_profile(self) -> None:
        assert self.glb is not None
        document = self.glb.document
        nodes = document.get("nodes", [])
        meshes = document.get("meshes", [])
        materials = document.get("materials", [])
        if not isinstance(nodes, list) or len(nodes) > MAX_NODES:
            raise CompileFailure(f"node count exceeds the v1 limit of {MAX_NODES}")
        if not isinstance(meshes, list) or len(meshes) > MAX_MESHES:
            raise CompileFailure(f"mesh count exceeds the v1 limit of {MAX_MESHES}")
        if not isinstance(materials, list) or len(materials) > MAX_MATERIALS:
            raise CompileFailure(
                f"material count exceeds the v1 limit of {MAX_MATERIALS}"
            )
        if document.get("extensionsUsed") or document.get("extensionsRequired"):
            raise CompileFailure("v1 does not allow glTF extensions")
        buffers = document.get("buffers")
        if not isinstance(buffers, list) or len(buffers) != 1:
            raise CompileFailure("v1 requires exactly one embedded GLB buffer")
        buffer_length = buffers[0].get("byteLength")
        if (
            not isinstance(buffer_length, int)
            or buffer_length < 0
            or buffer_length > len(self.glb.binary)
            or len(self.glb.binary) - buffer_length > 3
        ):
            raise CompileFailure("GLB buffer length or padding is invalid")
        views = document.get("bufferViews", [])
        if not isinstance(views, list):
            raise CompileFailure("GLB bufferViews must be an array")
        for index, view in enumerate(views):
            if not isinstance(view, dict) or view.get("buffer", 0) != 0:
                raise CompileFailure(f"bufferView {index} is invalid")
            offset = view.get("byteOffset", 0)
            length = view.get("byteLength")
            stride = view.get("byteStride")
            if (
                not isinstance(offset, int)
                or not isinstance(length, int)
                or offset < 0
                or length < 0
                or offset + length > buffer_length
            ):
                raise CompileFailure(f"bufferView {index} has invalid bounds")
            if stride is not None and (
                not isinstance(stride, int)
                or stride < 4
                or stride > 252
                or stride % 4
            ):
                raise CompileFailure(f"bufferView {index} has invalid byte stride")

        scenes = document.get("scenes")
        default_scene = document.get("scene")
        if not isinstance(scenes, list) or len(scenes) != 1 or default_scene != 0:
            raise CompileFailure("v1 requires exactly one default glTF scene")
        scene = scenes[0]
        roots = scene.get("nodes")
        if (
            not isinstance(roots, list)
            or len(roots) != len(nodes)
            or roots != list(range(len(nodes)))
        ):
            raise CompileFailure(
                "v1 requires every node once in deterministic scene-root order"
            )

        names: set[str] = set()
        referenced_meshes: set[int] = set()
        for index, node in enumerate(nodes):
            if not isinstance(node, dict):
                raise CompileFailure(f"node {index} is not an object")
            name = node.get("name")
            if (
                not isinstance(name, str)
                or not ASSET_ID_PATTERN.fullmatch(name)
                or name in names
            ):
                raise CompileFailure(f"node {index} has an invalid or duplicate name")
            names.add(name)
            if "children" in node:
                raise CompileFailure(f"node {name} has unsupported child nodes")
            if any(key in node for key in ("matrix", "translation", "rotation", "scale")):
                raise CompileFailure(
                    f"node {name} has an unapplied transform; apply transforms in Blender"
                )
            mesh_index = node.get("mesh")
            if (
                not isinstance(mesh_index, int)
                or mesh_index < 0
                or mesh_index >= len(meshes)
                or mesh_index in referenced_meshes
            ):
                raise CompileFailure(f"node {name} has an invalid or reused mesh")
            referenced_meshes.add(mesh_index)
        if referenced_meshes != set(range(len(meshes))):
            raise CompileFailure("v1 rejects unreferenced glTF meshes")

        scene_extras = scene.get("extras", {})
        if (
            scene_extras.get("rorng_authoring_axis") != "blender-z-up"
            or scene_extras.get("rorng_interchange_axis") != "gltf-y-up"
            or scene_extras.get("rorng_units") != "metres"
        ):
            raise CompileFailure("scene axis/unit metadata is incomplete")
        if self.manifest.get("authoring", {}).get("transforms_applied") is not True:
            raise CompileFailure("manifest must require applied authoring transforms")
        export = self.manifest.get("export", {})
        if export.get("y_up") is not True or export.get("profile") != "gltf-2.0":
            raise CompileFailure("manifest must declare the glTF 2.0 Y-up profile")

        material_names: list[str] = []
        for index, material in enumerate(materials):
            name = material.get("name") if isinstance(material, dict) else None
            if (
                not isinstance(name, str)
                or not ASSET_ID_PATTERN.fullmatch(name)
                or name in material_names
            ):
                raise CompileFailure(
                    f"material {index} has an invalid or duplicate stable name"
                )
            material_names.append(name)
        self.material_names = tuple(material_names)

    def _accessor(
        self,
        index: Any,
        *,
        component_type: int,
        accessor_type: str,
        label: str,
    ) -> list[Any]:
        assert self.glb is not None
        accessors = self.glb.document.get("accessors", [])
        if not isinstance(index, int) or not 0 <= index < len(accessors):
            raise CompileFailure(f"{label} references an invalid accessor")
        descriptor = accessors[index]
        view_index = descriptor.get("bufferView")
        count = descriptor.get("count")
        offset = descriptor.get("byteOffset", 0)
        if (
            descriptor.get("componentType") != component_type
            or descriptor.get("type") != accessor_type
            or descriptor.get("normalized", False) is not False
            or not isinstance(view_index, int)
            or not isinstance(count, int)
            or count < 0
            or not isinstance(offset, int)
            or offset < 0
        ):
            raise CompileFailure(f"{label} uses an unsupported accessor encoding")
        try:
            return self.glb.accessor(index)
        except (ValueError, TypeError, IndexError, struct.error) as error:
            raise CompileFailure(f"{label} cannot be decoded: {error}") from error

    def _primitive(
        self,
        *,
        node_name: str,
        primitive_index: int,
        primitive: dict[str, Any],
        render: bool,
    ) -> PrimitiveData:
        assert self.glb is not None
        label = f"{node_name}.primitives[{primitive_index}]"
        if primitive.get("mode", 4) != 4:
            raise CompileFailure(f"{label} is not a triangle list")
        if primitive.get("targets"):
            raise CompileFailure(f"{label} has unsupported morph targets")
        attributes = primitive.get("attributes")
        if not isinstance(attributes, dict):
            raise CompileFailure(f"{label} has no attribute map")
        required = {"POSITION", "NORMAL"}
        if render:
            required.update({"TANGENT", "TEXCOORD_0"})
        if set(attributes) != required:
            raise CompileFailure(
                f"{label} attributes must be exactly {', '.join(sorted(required))}"
            )

        raw_positions = self._accessor(
            attributes["POSITION"],
            component_type=5126,
            accessor_type="VEC3",
            label=f"{label}.POSITION",
        )
        raw_normals = self._accessor(
            attributes["NORMAL"],
            component_type=5126,
            accessor_type="VEC3",
            label=f"{label}.NORMAL",
        )
        positions = tuple(
            finite_tuple(value, 3, f"{label}.POSITION") for value in raw_positions
        )
        normals = tuple(
            finite_tuple(value, 3, f"{label}.NORMAL") for value in raw_normals
        )
        if len(positions) != len(normals) or not positions:
            raise CompileFailure(f"{label} has mismatched or empty vertex data")

        tangents: tuple[tuple[float, float, float, float], ...] = ()
        texcoords: tuple[tuple[float, float], ...] = ()
        if render:
            raw_tangents = self._accessor(
                attributes["TANGENT"],
                component_type=5126,
                accessor_type="VEC4",
                label=f"{label}.TANGENT",
            )
            raw_texcoords = self._accessor(
                attributes["TEXCOORD_0"],
                component_type=5126,
                accessor_type="VEC2",
                label=f"{label}.TEXCOORD_0",
            )
            tangents = tuple(
                finite_tuple(value, 4, f"{label}.TANGENT")
                for value in raw_tangents
            )
            texcoords = tuple(
                finite_tuple(value, 2, f"{label}.TEXCOORD_0")
                for value in raw_texcoords
            )
            if len(tangents) != len(positions) or len(texcoords) != len(positions):
                raise CompileFailure(f"{label} has mismatched render attributes")

        for index, normal in enumerate(normals):
            if abs(vector_length(normal) - 1.0) > 2e-3:
                raise CompileFailure(f"{label}.NORMAL[{index}] is not unit length")
        for index, tangent in enumerate(tangents):
            if abs(vector_length(tangent[:3]) - 1.0) > 2e-3:
                raise CompileFailure(f"{label}.TANGENT[{index}] is not unit length")
            dot = sum(
                normals[index][axis] * tangent[axis] for axis in range(3)
            )
            if abs(dot) > 2e-3 or abs(abs(tangent[3]) - 1.0) > FLOAT_EPSILON:
                raise CompileFailure(
                    f"{label}.TANGENT[{index}] has invalid parity or orthogonality"
                )

        index_accessor = primitive.get("indices")
        accessors = self.glb.document.get("accessors", [])
        if (
            not isinstance(index_accessor, int)
            or not 0 <= index_accessor < len(accessors)
        ):
            raise CompileFailure(f"{label} requires an explicit index accessor")
        index_descriptor = accessors[index_accessor]
        component_type = index_descriptor.get("componentType")
        if component_type not in (5121, 5123, 5125):
            raise CompileFailure(f"{label} index type is not unsigned integer")
        raw_indices = self._accessor(
            index_accessor,
            component_type=component_type,
            accessor_type="SCALAR",
            label=f"{label}.indices",
        )
        indices = tuple(int(value) for value in raw_indices)
        if len(indices) % 3 or not indices:
            raise CompileFailure(f"{label} has an invalid triangle index count")
        if min(indices) < 0 or max(indices) >= len(positions):
            raise CompileFailure(f"{label} has an out-of-range index")
        triangles = tuple(
            (indices[offset], indices[offset + 1], indices[offset + 2])
            for offset in range(0, len(indices), 3)
        )
        for triangle_index, triangle in enumerate(triangles):
            if len(set(triangle)) != 3:
                raise CompileFailure(
                    f"{label}.triangles[{triangle_index}] has repeated vertices"
                )

        material_index = primitive.get("material")
        if (
            not isinstance(material_index, int)
            or not 0 <= material_index < len(self.material_names)
        ):
            raise CompileFailure(f"{label} has an invalid material")
        return PrimitiveData(
            name=f"{node_name}_part_{primitive_index:03d}",
            material=self.material_names[material_index],
            positions=positions,
            normals=normals,
            tangents=tangents,
            texcoords=texcoords,
            triangles=triangles,
        )

    def _extract_meshes(self) -> None:
        assert self.glb is not None
        geometry = self.manifest["geometry"]
        collision = self.manifest["collision"]
        render_entries = sorted(geometry["lods"], key=lambda item: item["lod"])
        collision_entries = sorted(
            collision["objects"],
            key=lambda item: (item["role"], item["name"]),
        )
        sources: list[MeshSource] = []
        for entry in render_entries:
            node = self.glb.node_by_name(entry["name"])
            if node is None:
                raise CompileFailure(f"missing render node {entry['name']}")
            mesh = self.glb.mesh_for_node(node)
            primitives = tuple(
                self._primitive(
                    node_name=entry["name"],
                    primitive_index=index,
                    primitive=primitive,
                    render=True,
                )
                for index, primitive in enumerate(mesh.get("primitives", []))
            )
            output_name = f"{self.asset_id}_lod{entry['lod']}.mesh"
            manual_lods: tuple[tuple[float, str], ...] = ()
            if entry["lod"] == 0:
                manual_lods = (
                    (LOD_DISTANCES_M[0], f"{self.asset_id}_lod1.mesh"),
                    (LOD_DISTANCES_M[1], f"{self.asset_id}_lod2.mesh"),
                )
            sources.append(
                MeshSource(
                    output_name=output_name,
                    source_node=entry["name"],
                    role=f"render-lod{entry['lod']}",
                    primitives=primitives,
                    manual_lods=manual_lods,
                )
            )
        for entry in collision_entries:
            node = self.glb.node_by_name(entry["name"])
            if node is None:
                raise CompileFailure(f"missing collision node {entry['name']}")
            mesh = self.glb.mesh_for_node(node)
            for index, primitive in enumerate(mesh.get("primitives", [])):
                attributes = primitive.get("attributes")
                if not isinstance(attributes, dict) or set(attributes) != {
                    "NORMAL",
                    "POSITION",
                    "TANGENT",
                    "TEXCOORD_0",
                }:
                    raise CompileFailure(
                        f"{entry['name']}.primitives[{index}] has an "
                        "unsupported collision attribute profile"
                    )
            primitives = tuple(
                self._primitive(
                    node_name=entry["name"],
                    primitive_index=index,
                    primitive={
                        **primitive,
                        "attributes": {
                            key: value
                            for key, value in primitive["attributes"].items()
                            if key in ("POSITION", "NORMAL")
                        },
                    },
                    render=False,
                )
                for index, primitive in enumerate(mesh.get("primitives", []))
            )
            suffix = entry["name"].removeprefix(self.asset_id + "_")
            sources.append(
                MeshSource(
                    output_name=f"{self.asset_id}_{suffix}.mesh",
                    source_node=entry["name"],
                    role=entry["role"],
                    primitives=primitives,
                )
            )

        self.primitive_count = sum(len(mesh.primitives) for mesh in sources)
        self.vertex_count = sum(
            len(primitive.positions)
            for mesh in sources
            for primitive in mesh.primitives
        )
        self.index_count = sum(
            len(primitive.triangles) * 3
            for mesh in sources
            for primitive in mesh.primitives
        )
        if self.primitive_count > MAX_PRIMITIVES:
            raise CompileFailure(
                f"primitive count exceeds the v1 limit of {MAX_PRIMITIVES}"
            )
        if self.vertex_count > MAX_VERTICES:
            raise CompileFailure(
                f"vertex count exceeds the v1 limit of {MAX_VERTICES}"
            )
        if self.index_count > MAX_INDICES:
            raise CompileFailure(
                f"index count exceeds the v1 limit of {MAX_INDICES}"
            )
        self.meshes = tuple(sources)

    def _mesh_xml(self, source: MeshSource) -> bytes:
        root = ET.Element("mesh")
        submeshes = ET.SubElement(root, "submeshes")
        for primitive in source.primitives:
            use_32_bit = any(
                vertex > 65_535
                for triangle in primitive.triangles
                for vertex in triangle
            )
            submesh = ET.SubElement(
                submeshes,
                "submesh",
                {
                    "material": self._runtime_material_name(
                        primitive.material
                    ),
                    "usesharedvertices": "false",
                    "use32bitindexes": "true" if use_32_bit else "false",
                    "operationtype": "triangle_list",
                },
            )
            faces = ET.SubElement(
                submesh,
                "faces",
                {"count": str(len(primitive.triangles))},
            )
            for triangle in primitive.triangles:
                ET.SubElement(
                    faces,
                    "face",
                    {
                        "v1": str(triangle[0]),
                        "v2": str(triangle[1]),
                        "v3": str(triangle[2]),
                    },
                )
            geometry = ET.SubElement(
                submesh,
                "geometry",
                {"vertexcount": str(len(primitive.positions))},
            )
            attributes = {"positions": "true", "normals": "true"}
            if primitive.tangents:
                attributes.update(
                    {
                        "tangents": "true",
                        "tangent_dimensions": "4",
                        "texture_coords": "1",
                        "texture_coord_dimensions_0": "float2",
                    }
                )
            vertex_buffer = ET.SubElement(geometry, "vertexbuffer", attributes)
            for index, (position, normal) in enumerate(
                zip(primitive.positions, primitive.normals)
            ):
                vertex = ET.SubElement(vertex_buffer, "vertex")
                ET.SubElement(
                    vertex,
                    "position",
                    {
                        "x": stable_float(position[0]),
                        "y": stable_float(position[1]),
                        "z": stable_float(position[2]),
                    },
                )
                ET.SubElement(
                    vertex,
                    "normal",
                    {
                        "x": stable_float(normal[0]),
                        "y": stable_float(normal[1]),
                        "z": stable_float(normal[2]),
                    },
                )
                if primitive.tangents:
                    tangent = primitive.tangents[index]
                    texcoord = primitive.texcoords[index]
                    ET.SubElement(
                        vertex,
                        "tangent",
                        {
                            "x": stable_float(tangent[0]),
                            "y": stable_float(tangent[1]),
                            "z": stable_float(tangent[2]),
                            "w": stable_float(tangent[3]),
                        },
                    )
                    ET.SubElement(
                        vertex,
                        "texcoord",
                        {
                            "u": stable_float(texcoord[0]),
                            "v": stable_float(texcoord[1]),
                        },
                    )
        if source.manual_lods:
            level_of_detail = ET.SubElement(
                root,
                "levelofdetail",
                {
                    "strategy": "distance_sphere",
                    "numlevels": str(1 + len(source.manual_lods)),
                    "manual": "true",
                },
            )
            for distance, mesh_name in source.manual_lods:
                ET.SubElement(
                    level_of_detail,
                    "lodmanual",
                    {
                        "value": stable_float(distance),
                        "meshname": mesh_name,
                    },
                )
        names = ET.SubElement(root, "submeshnames")
        for index, primitive in enumerate(source.primitives):
            ET.SubElement(
                names,
                "submeshname",
                {"name": primitive.name, "index": str(index)},
            )
        return xml_bytes(root)

    def _runtime_material_name(self, name: str) -> str:
        declaration = next(
            (
                material
                for material in self.manifest["materials"]
                if material["name"] == name
            ),
            None,
        )
        if declaration is None:
            raise CompileFailure(f"material {name} has no declaration")
        if "runtime_parent_material" not in declaration:
            return name
        runtime_parent = declaration["runtime_parent_material"]
        if (
            not isinstance(runtime_parent, str)
            or runtime_parent not in ALLOWED_RUNTIME_PARENT_MATERIALS
        ):
            raise CompileFailure(
                f"material {name} has an invalid runtime parent material"
            )
        # OGRE resolves inherited base materials only inside the resource group
        # currently parsing the script. CityWorld overlays are initialized in
        # their own group, before a cross-group base lookup can succeed.
        # Binding the allowlisted core material directly preserves its exact
        # technique and texture while avoiding a blank child material.
        return runtime_parent

    def runtime_material_bindings(self) -> list[dict[str, str]]:
        declarations = {
            material["name"]: material
            for material in self.manifest["materials"]
        }
        return [
            {
                "authored_material": name,
                "runtime_material": self._runtime_material_name(name),
            }
            for name in self.material_names
            if "runtime_parent_material" in declarations[name]
        ]

    def material_model(self) -> str:
        if self.runtime_material_bindings():
            return (
                "ogre-rtss-metallic-roughness-fallback-"
                "with-direct-core-bindings-v1"
            )
        return "ogre-rtss-metallic-roughness-fallback-v1"

    def _material_bytes(self) -> bytes:
        declarations = {
            material["name"]: material for material in self.manifest["materials"]
        }
        lines = [
            "// Generated by ror-cityworld-scene-compiler-v1.",
            "// Texture-free RTShader-compatible fallback from glTF metallic-roughness factors.",
            "",
        ]
        for name in self.material_names:
            material = declarations[name]
            if "runtime_parent_material" in material:
                self._runtime_material_name(name)
                continue
            color = [float(value) for value in material["base_color_factor_linear"]]
            metallic = float(material["metallic_factor"])
            roughness = float(material["roughness_factor"])
            emissive = [
                float(value)
                for value in material.get(
                    "emissive_factor_linear",
                    [0.0, 0.0, 0.0],
                )
            ]
            dielectric = 0.04
            specular = [
                dielectric * (1.0 - metallic) + color[index] * metallic
                for index in range(3)
            ]
            shininess = max(1.0, min(128.0, 2.0 / max(roughness**4, 1e-4) - 2.0))
            lines.extend(
                [
                    f"material {name}",
                    "{",
                    "  technique",
                    "  {",
                    "    pass",
                    "    {",
                    "      ambient "
                    + " ".join(stable_float(value) for value in color),
                    "      diffuse "
                    + " ".join(stable_float(value) for value in color),
                    "      specular "
                    + " ".join(stable_float(value) for value in specular)
                    + " "
                    + stable_float(color[3])
                    + " "
                    + stable_float(shininess),
                    *(
                        [
                            "      emissive "
                            + " ".join(
                                stable_float(value) for value in emissive
                            )
                        ]
                        if any(value > 0.0 for value in emissive)
                        else []
                    ),
                    "    }",
                    "  }",
                    "}",
                    "",
                ]
            )
        return ("\n".join(lines).rstrip() + "\n").encode("utf-8")

    def _odef_bytes(self) -> bytes:
        collision_entries = sorted(
            self.manifest["collision"]["objects"],
            key=lambda item: (item["role"], item["name"]),
        )
        lines = [
            f"{self.asset_id}_lod0.mesh",
            "1, 1, 1",
            "standard",
            "",
        ]
        for entry in collision_entries:
            suffix = entry["name"].removeprefix(self.asset_id + "_")
            friction = "asphalt" if entry["role"] == "collision-road" else "concrete"
            lines.extend(
                [
                    "beginmesh",
                    f"mesh {self.asset_id}_{suffix}.mesh",
                    f"stdfriction {friction}",
                    "endmesh",
                    "",
                ]
            )
        for light in self.runtime_light_contract():
            position = light["position_ogre_y_up_m"]
            color = light["color_linear"]
            lines.append(
                "pointlight "
                + ", ".join(
                    stable_float(value)
                    for value in (
                        *position,
                        0.0,
                        -1.0,
                        0.0,
                        *color,
                        light["range_m"],
                    )
                )
            )
        if self.runtime_light_contract():
            lines.append("")
        lines.append("end")
        return ("\n".join(lines) + "\n").encode("utf-8")

    def intermediates(self) -> tuple[Intermediate, ...]:
        if not self.meshes:
            raise CompileFailure("prepare() must be called before compilation")
        values = [
            Intermediate(
                path=mesh.output_name + ".xml",
                role=mesh.role,
                data=self._mesh_xml(mesh),
            )
            for mesh in self.meshes
        ]
        values.extend(
            [
                Intermediate(
                    path=f"{self.asset_id}.material",
                    role="material-fallback",
                    data=self._material_bytes(),
                ),
                Intermediate(
                    path=f"{self.asset_id}.odef",
                    role="terrain-object",
                    data=self._odef_bytes(),
                ),
            ]
        )
        return tuple(values)

    def connector_runtime_contract(self) -> list[dict[str, Any]]:
        result = []
        for connector in sorted(
            self.manifest["connectors"], key=lambda item: item["id"]
        ):
            result.append(
                {
                    "id": connector["id"],
                    "lane_centres_x_m": connector["lane_centres_x_m"],
                    "position_ogre_y_up_m": transformed_blender_connector(
                        connector["position_blender_z_up_m"]
                    ),
                    "road_width_m": connector["road_width_m"],
                }
            )
        return result

    def runtime_light_contract(self) -> list[dict[str, Any]]:
        declaration = self.manifest.get("runtime_lights")
        if declaration is None:
            return []
        return [
            {
                "color_linear": [
                    float(component)
                    for component in light["color_linear"]
                ],
                "id": light["id"],
                "position_ogre_y_up_m": transformed_blender_connector(
                    light["position_blender_z_up_m"]
                ),
                "range_m": float(light["range_m"]),
                "type": light["type"],
            }
            for light in sorted(
                declaration["lights"],
                key=lambda item: item["id"],
            )
        ]


def converter_identity(converter: Path) -> dict[str, str]:
    if not converter.is_file() or converter.is_symlink():
        raise CompileFailure(f"OgreXMLConverter is missing or unsafe: {converter}")
    converter_command = str(converter.resolve())
    with tempfile.TemporaryDirectory(
        prefix="ror-ogre-converter-identity-"
    ) as temporary_directory:
        try:
            result = subprocess.run(
                [converter_command, "-v"],
                check=False,
                capture_output=True,
                text=True,
                timeout=30,
                cwd=temporary_directory,
            )
        except (OSError, subprocess.SubprocessError) as error:
            raise CompileFailure(
                f"cannot execute OgreXMLConverter: {error}"
            ) from error
    version = result.stdout.strip()
    if result.returncode != 0 or version != OGRE_CONVERTER_VERSION:
        raise CompileFailure(
            "OgreXMLConverter version is not pinned "
            f"(expected {OGRE_CONVERTER_VERSION!r}, received {version!r})"
        )
    return {
        "executable": converter.name,
        "sha256": sha256_file(converter),
        "version": version,
    }


def convert_mesh(
    converter: Path,
    xml_path: Path,
    mesh_path: Path,
    log_path: Path,
) -> None:
    command = [
        str(converter),
        "-q",
        "-gl",
        "-E",
        "little",
        "-log",
        str(log_path),
        str(xml_path),
        str(mesh_path),
    ]
    try:
        result = subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
            timeout=120,
        )
    except (OSError, subprocess.SubprocessError) as error:
        raise CompileFailure(f"OgreXMLConverter failed to execute: {error}") from error
    if result.returncode != 0:
        message = (result.stderr or result.stdout).strip()
        if log_path.is_file():
            try:
                log_message = log_path.read_text(
                    encoding="utf-8",
                    errors="replace",
                ).strip()
            except OSError:
                log_message = ""
            if log_message:
                message = f"{message}\n{log_message}".strip()
        raise CompileFailure(
            f"OgreXMLConverter rejected {xml_path.name}: {message[:1000]}"
        )
    if not mesh_path.is_file() or mesh_path.is_symlink():
        raise CompileFailure(f"OgreXMLConverter did not create {mesh_path.name}")
    if mesh_path.stat().st_size > MAX_OUTPUT_BYTES:
        raise CompileFailure(f"compiled mesh exceeds {MAX_OUTPUT_BYTES} bytes")
    with mesh_path.open("rb") as handle:
        header = handle.read(len(OGRE_MESH_HEADER))
    if header != OGRE_MESH_HEADER:
        raise CompileFailure(
            f"{mesh_path.name} does not use the pinned OGRE mesh format"
        )


def output_record(
    repo_root: Path,
    path: Path,
    *,
    role: str,
    content_path: Path | None = None,
) -> dict[str, Any]:
    source = path if content_path is None else content_path
    return {
        "path": portable_relative_path(repo_root, path),
        "role": role,
        "sha256": sha256_file(source),
        "size": source.stat().st_size,
    }


def source_contract(manifest: dict[str, Any]) -> dict[str, Any]:
    return {
        key: value
        for key, value in manifest.items()
        if key != "compiled"
    }


def uses_runtime_parent_material(manifest: dict[str, Any]) -> bool:
    materials = manifest.get("materials")
    return isinstance(materials, list) and any(
        isinstance(material, dict)
        and "runtime_parent_material" in material
        for material in materials
    )


def compile_asset(
    compiler: SceneCompiler,
    converter: Path,
    output_directory: Path,
) -> dict[str, Any]:
    converter_info = converter_identity(converter)
    output_directory = output_directory.resolve()
    try:
        output_directory.relative_to(compiler.repo_root)
    except ValueError as error:
        raise CompileFailure("output directory must be inside the repository") from error
    if output_directory.is_symlink():
        raise CompileFailure("output directory cannot be a symbolic link")

    intermediates = compiler.intermediates()
    intermediate_hashes = {
        item.path: sha256_bytes(item.data)
        for item in intermediates
        if item.path.endswith(".mesh.xml")
    }
    output_directory.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix=f".{compiler.asset_id}-compile-",
        dir=output_directory.parent,
    ) as temporary_directory_name:
        temporary_directory = Path(temporary_directory_name)
        output_records: list[dict[str, Any]] = []
        role_by_mesh = {
            mesh.output_name: mesh.role for mesh in compiler.meshes
        }
        for item in intermediates:
            temporary_path = temporary_directory / item.path
            temporary_path.write_bytes(item.data)
            if item.path.endswith(".mesh.xml"):
                mesh_name = item.path.removesuffix(".xml")
                mesh_path = temporary_directory / mesh_name
                convert_mesh(
                    converter,
                    temporary_path,
                    mesh_path,
                    temporary_directory / f"{mesh_name}.converter.log",
                )
                output_records.append(
                    output_record(
                        compiler.repo_root,
                        output_directory / mesh_name,
                        role=role_by_mesh[mesh_name],
                        content_path=mesh_path,
                    )
                    | {
                        "_source": mesh_path,
                    }
                )
            else:
                output_records.append(
                    output_record(
                        compiler.repo_root,
                        output_directory / item.path,
                        role=item.role,
                        content_path=temporary_path,
                    )
                    | {
                        "_source": temporary_path,
                    }
                )

        compiler_path = Path(__file__).resolve()
        assert compiler.glb_path is not None
        report_path = output_directory / f"{compiler.asset_id}.compile.json"
        runtime_material_bindings = compiler.runtime_material_bindings()
        report = {
            "asset": {
                "id": compiler.asset_id,
                "version": compiler.manifest["asset"]["version"],
            },
            "basis": {
                "authoring": "blender-z-up-metres",
                "authoring_to_interchange": [
                    [1.0, 0.0, 0.0, 0.0],
                    [0.0, 0.0, 1.0, 0.0],
                    [0.0, -1.0, 0.0, 0.0],
                    [0.0, 0.0, 0.0, 1.0],
                ],
                "compiler_transform": "identity",
                "interchange": "gltf-y-up-metres",
                "runtime": "ogre-y-up-metres",
            },
            "compiler": {
                "format": COMPILER_FORMAT,
                "path": portable_relative_path(compiler.repo_root, compiler_path),
                "sha256": sha256_file(compiler_path),
            },
            "connectors": compiler.connector_runtime_contract(),
            "format": REPORT_FORMAT,
            "inputs": {
                "asset_contract_sha256": sha256_bytes(
                    canonical_json(source_contract(compiler.manifest)).encode("utf-8")
                ),
                "glb": {
                    "path": portable_relative_path(
                        compiler.repo_root, compiler.glb_path
                    ),
                    "sha256": sha256_file(compiler.glb_path),
                },
            },
            "limits": {
                "indices": MAX_INDICES,
                "materials": MAX_MATERIALS,
                "meshes": MAX_MESHES,
                "nodes": MAX_NODES,
                "primitives": MAX_PRIMITIVES,
                "vertices": MAX_VERTICES,
            },
            "material_model": compiler.material_model(),
            "mesh_format": "MeshSerializer_v1.100-little-endian",
            "ogre_converter": converter_info,
            "options": {
                "colour_packing": "gl",
                "endian": "little",
                "lod_distances_m": list(LOD_DISTANCES_M),
                "transforms": "applied-only",
            },
            "runtime_lights": compiler.runtime_light_contract(),
            **(
                {
                    "runtime_material_bindings":
                        runtime_material_bindings,
                }
                if runtime_material_bindings
                else {}
            ),
            "outputs": [
                {key: value for key, value in record.items() if key != "_source"}
                for record in sorted(output_records, key=lambda item: item["path"])
            ],
            "source_stats": {
                "indices": compiler.index_count,
                "materials": len(compiler.material_names),
                "meshes": len(compiler.meshes),
                "primitives": compiler.primitive_count,
                "vertices": compiler.vertex_count,
            },
            "xml_intermediate_sha256": dict(sorted(intermediate_hashes.items())),
        }
        report_bytes = canonical_pretty(report)
        report_temporary_path = temporary_directory / report_path.name
        report_temporary_path.write_bytes(report_bytes)

        output_directory.mkdir(parents=True, exist_ok=True)
        expected_names = {
            Path(record["path"]).name for record in output_records
        } | {report_path.name}
        existing_names = {
            path.name
            for path in output_directory.iterdir()
            if path.is_file() and not path.is_symlink()
        }
        unexpected = existing_names - expected_names
        if unexpected:
            raise CompileFailure(
                "output directory contains unexpected files: "
                + ", ".join(sorted(unexpected))
            )
        for record in output_records:
            source = record["_source"]
            destination = output_directory / Path(record["path"]).name
            os.replace(source, destination)
        os.replace(report_temporary_path, report_path)
        compiler.manifest["compiled"] = {
            "format": COMPILED_RECORD_FORMAT,
            "outputs": report["outputs"],
            "report": {
                "path": portable_relative_path(
                    compiler.repo_root,
                    report_path,
                ),
                "sha256": sha256_file(report_path),
            },
        }
        manifest_temporary_path = (
            temporary_directory / compiler.manifest_path.name
        )
        manifest_temporary_path.write_bytes(
            canonical_pretty(compiler.manifest)
        )
        os.replace(manifest_temporary_path, compiler.manifest_path)
    return report


def validate_checked_outputs(
    compiler: SceneCompiler,
    output_directory: Path,
) -> dict[str, Any]:
    output_directory = output_directory.resolve()
    report_path = output_directory / f"{compiler.asset_id}.compile.json"
    report = load_json(report_path)
    if report.get("format") != REPORT_FORMAT:
        raise CompileFailure("checked compile report has an unsupported format")
    expected_basis = {
        "authoring": "blender-z-up-metres",
        "authoring_to_interchange": [
            [1.0, 0.0, 0.0, 0.0],
            [0.0, 0.0, 1.0, 0.0],
            [0.0, -1.0, 0.0, 0.0],
            [0.0, 0.0, 0.0, 1.0],
        ],
        "compiler_transform": "identity",
        "interchange": "gltf-y-up-metres",
        "runtime": "ogre-y-up-metres",
    }
    runtime_material_bindings = compiler.runtime_material_bindings()
    if (
        report.get("asset")
        != {
            "id": compiler.asset_id,
            "version": compiler.manifest["asset"]["version"],
        }
        or report.get("basis") != expected_basis
        or report.get("connectors") != compiler.connector_runtime_contract()
        or report.get("limits")
        != {
            "indices": MAX_INDICES,
            "materials": MAX_MATERIALS,
            "meshes": MAX_MESHES,
            "nodes": MAX_NODES,
            "primitives": MAX_PRIMITIVES,
            "vertices": MAX_VERTICES,
        }
        or report.get("material_model") != compiler.material_model()
        or report.get("mesh_format")
        != "MeshSerializer_v1.100-little-endian"
        or report.get("options")
        != {
            "colour_packing": "gl",
            "endian": "little",
            "lod_distances_m": list(LOD_DISTANCES_M),
            "transforms": "applied-only",
        }
        or report.get("runtime_lights")
        != compiler.runtime_light_contract()
        or report.get("source_stats")
        != {
            "indices": compiler.index_count,
            "materials": len(compiler.material_names),
            "meshes": len(compiler.meshes),
            "primitives": compiler.primitive_count,
            "vertices": compiler.vertex_count,
        }
    ):
        raise CompileFailure("checked compile report has stale profile metadata")
    if (
        report.get("runtime_material_bindings")
        != runtime_material_bindings
        if runtime_material_bindings
        else "runtime_material_bindings" in report
    ):
        raise CompileFailure(
            "checked compile report has stale runtime material bindings"
        )
    converter = report.get("ogre_converter")
    if (
        not isinstance(converter, dict)
        or converter.get("executable") != "OgreXMLConverter"
        or converter.get("version") != OGRE_CONVERTER_VERSION
        or not isinstance(converter.get("sha256"), str)
        or len(converter["sha256"]) != 64
        or any(character not in "0123456789abcdef" for character in converter["sha256"])
    ):
        raise CompileFailure("checked compile report has invalid converter identity")
    compiler_record = report.get("compiler", {})
    compiler_path = Path(__file__).resolve()
    current_compiler_sha256 = sha256_file(compiler_path)
    checked_compiler_sha256 = compiler_record.get("sha256")
    compiler_is_current = checked_compiler_sha256 == current_compiler_sha256
    compiler_is_byte_compatible = (
        checked_compiler_sha256 in BYTE_COMPATIBLE_COMPILER_SHA256
        or (
            not uses_runtime_parent_material(compiler.manifest)
            and checked_compiler_sha256
            in BYTE_COMPATIBLE_COMPILER_SHA256_WITHOUT_RUNTIME_PARENT
        )
    )
    if (
        compiler_record.get("format") != COMPILER_FORMAT
        or compiler_record.get("path")
        != portable_relative_path(compiler.repo_root, compiler_path)
        or not (compiler_is_current or compiler_is_byte_compatible)
    ):
        raise CompileFailure("checked compile report has stale compiler identity")
    assert compiler.glb_path is not None
    if (
        report.get("inputs", {}).get("asset_contract_sha256")
        != sha256_bytes(
            canonical_json(source_contract(compiler.manifest)).encode("utf-8")
        )
        or report.get("inputs", {}).get("glb", {}).get("sha256")
        != sha256_file(compiler.glb_path)
    ):
        raise CompileFailure("checked compile report has stale source identity")
    expected_intermediates = {
        item.path: sha256_bytes(item.data)
        for item in compiler.intermediates()
        if item.path.endswith(".mesh.xml")
    }
    if report.get("xml_intermediate_sha256") != dict(
        sorted(expected_intermediates.items())
    ):
        raise CompileFailure("checked compile report has stale XML intermediates")

    outputs = report.get("outputs")
    if not isinstance(outputs, list) or not outputs:
        raise CompileFailure("checked compile report has no output records")
    expected_roles = {
        portable_relative_path(
            compiler.repo_root,
            output_directory / mesh.output_name,
        ): mesh.role
        for mesh in compiler.meshes
    }
    expected_roles[
        portable_relative_path(
            compiler.repo_root,
            output_directory / f"{compiler.asset_id}.material",
        )
    ] = "material-fallback"
    expected_roles[
        portable_relative_path(
            compiler.repo_root,
            output_directory / f"{compiler.asset_id}.odef",
        )
    ] = "terrain-object"
    expected_generated_sha256 = {
        portable_relative_path(
            compiler.repo_root,
            output_directory / intermediate.path,
        ): sha256_bytes(intermediate.data)
        for intermediate in compiler.intermediates()
        if not intermediate.path.endswith(".mesh.xml")
    }
    declared_paths: set[str] = set()
    for record in outputs:
        if not isinstance(record, dict) or set(record) != {
            "path",
            "role",
            "sha256",
            "size",
        }:
            raise CompileFailure("checked output record is not an object")
        path = declared_path(compiler.repo_root, record.get("path"))
        try:
            path.relative_to(output_directory)
        except ValueError as error:
            raise CompileFailure("checked output escapes its output directory") from error
        relative = portable_relative_path(compiler.repo_root, path)
        if relative in declared_paths:
            raise CompileFailure("checked compile report duplicates an output path")
        declared_paths.add(relative)
        if expected_roles.get(relative) != record.get("role"):
            raise CompileFailure(f"checked output role is stale: {path.name}")
        if path.stat().st_size != record.get("size"):
            raise CompileFailure(f"checked output size is stale: {path.name}")
        if sha256_file(path) != record.get("sha256"):
            raise CompileFailure(f"checked output hash is stale: {path.name}")
        generated_sha256 = expected_generated_sha256.get(relative)
        if (
            generated_sha256 is not None
            and record.get("sha256") != generated_sha256
        ):
            raise CompileFailure(
                f"checked generated output is stale: {path.name}"
            )
        if path.suffix == ".mesh":
            with path.open("rb") as handle:
                if handle.read(len(OGRE_MESH_HEADER)) != OGRE_MESH_HEADER:
                    raise CompileFailure(
                        f"checked output has unsupported mesh format: {path.name}"
                    )

    if declared_paths != set(expected_roles):
        raise CompileFailure("checked compile report has missing or extra outputs")
    expected_files = {
        path.name
        for path in output_directory.iterdir()
        if path.is_file() and not path.is_symlink()
    }
    declared_names = {Path(value).name for value in declared_paths}
    declared_names.add(report_path.name)
    if expected_files != declared_names:
        raise CompileFailure("checked output directory has undeclared files")

    compiled = compiler.manifest.get("compiled")
    if not isinstance(compiled, dict) or compiled.get("format") != COMPILED_RECORD_FORMAT:
        raise CompileFailure("asset manifest has no supported compiled record")
    if compiled.get("report", {}).get("path") != portable_relative_path(
        compiler.repo_root, report_path
    ) or compiled.get("report", {}).get("sha256") != sha256_file(report_path):
        raise CompileFailure("asset manifest has a stale compile-report record")
    manifest_outputs = compiled.get("outputs")
    canonical_outputs = [
        {
            "path": record["path"],
            "role": record["role"],
            "sha256": record["sha256"],
            "size": record["size"],
        }
        for record in sorted(outputs, key=lambda item: item["path"])
    ]
    if manifest_outputs != canonical_outputs:
        raise CompileFailure("asset manifest compiled outputs are stale")
    return report


def default_output_directory(compiler: SceneCompiler) -> Path:
    return compiler.manifest_path.parent / "compiled"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--repo-root", type=Path, default=Path.cwd())
    parser.add_argument(
        "--converter",
        type=Path,
        help="pinned OgreXMLConverter used for production mesh output",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        help="repository-local output directory (default: bridge/compiled)",
    )
    parser.add_argument(
        "--validate-checked",
        action="store_true",
        help="validate checked outputs without executing OgreXMLConverter",
    )
    parser.add_argument("--pretty", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        compiler = SceneCompiler(args.repo_root, args.manifest)
        compiler.prepare()
        output_directory = (
            args.output_dir.resolve()
            if args.output_dir is not None
            else default_output_directory(compiler)
        )
        if args.validate_checked:
            if args.converter is not None:
                raise CompileFailure(
                    "--converter cannot be combined with --validate-checked"
                )
            report = validate_checked_outputs(compiler, output_directory)
        else:
            if args.converter is None:
                raise CompileFailure(
                    "production compilation requires an explicit --converter"
                )
            report = compile_asset(
                compiler,
                args.converter.resolve(),
                output_directory,
            )
        if args.pretty:
            sys.stdout.write(json.dumps(report, indent=2, sort_keys=True) + "\n")
        else:
            sys.stdout.write(canonical_json(report) + "\n")
        return 0
    except (CompileFailure, OSError, ValueError, KeyError, TypeError) as error:
        failure = {
            "error": str(error),
            "format": REPORT_FORMAT,
            "valid": False,
        }
        sys.stdout.write(canonical_json(failure) + "\n")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
