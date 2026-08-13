#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile validated glTF/material sources directly to ``.rornative`` v1."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
from typing import Any, Iterable


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from validate_cityworld_asset import (  # noqa: E402
    canonical_json,
    safe_relative_path,
    sha256_file,
)
from validate_native_render_asset import (  # noqa: E402
    BASE_COLOR_TRANSFERS,
    MATERIAL_ALPHA_TEST_MODES,
    MATERIAL_BLEND_MODES,
    MATERIAL_MODELS,
    MATERIAL_TEXTURE_SLOTS,
    MATERIAL_WORKFLOWS,
    MAX_SOURCE_BYTES,
    NativeRenderAssetValidator,
    INSTANCE_FLAGS,
    SAMPLER_ADDRESS_MODES,
    SAMPLER_COMPARE_OPERATIONS,
    SAMPLER_FILTERS,
    _float32_round_trip,
)


COMPILER_FORMAT = "ror-native-render-compiler-v1"
REPORT_FORMAT = "ror-native-render-compile-report-v1"
PACKAGE_MANIFEST_FORMAT = "ror-native-render-package-manifest-v1"
PACKAGE_MAGIC = b"RORNAT1\x00"
PACKAGE_VERSION = 1
PACKAGE_HEADER_BYTES = 80
PACKAGE_HEADER = struct.Struct("<8sIIIIIIQ32s8s")
RECORD_HEADER = struct.Struct("<IIQQ")
RECORD_MANIFEST = 1
RECORD_MESH = 2
RECORD_TEXTURE = 3
RECORD_MATERIAL = 4
RECORD_SAMPLER = 5
RECORD_STATIC_INSTANCE = 6
MAX_PACKAGE_BYTES = 256 * 1024 * 1024


class CompileFailure(RuntimeError):
    """Stable expected compiler failure."""


@dataclass(frozen=True)
class Record:
    record_type: int
    source_id: int
    payload: bytes

    def encode(self) -> bytes:
        return RECORD_HEADER.pack(
            self.record_type,
            0,
            self.source_id,
            len(self.payload),
        ) + self.payload


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def canonical_pretty(value: dict[str, Any]) -> bytes:
    return (json.dumps(value, ensure_ascii=True, indent=2, sort_keys=True) + "\n").encode("ascii")


def canonical_float(value: Any) -> float:
    rounded, error = _float32_round_trip(value)
    if rounded is None or error is not None:
        raise CompileFailure(f"cannot encode non-canonical binary32: {error}")
    return rounded


def encode_string(value: str) -> bytes:
    try:
        encoded = value.encode("ascii")
    except UnicodeEncodeError as error:
        raise CompileFailure("package strings must be ASCII") from error
    if not encoded or len(encoded) > 255 or any(byte < 0x20 or byte > 0x7E for byte in encoded):
        raise CompileFailure("package string is empty, non-printable, or too long")
    return struct.pack("<I", len(encoded)) + encoded


def source_identity(package_id: str, kind: str, logical_id: str) -> int:
    payload = (
        b"ror-native-render-source-id-v1\x00"
        + package_id.encode("ascii")
        + b"\x00"
        + kind.encode("ascii")
        + b"\x00"
        + logical_id.encode("ascii")
    )
    value = int.from_bytes(hashlib.sha256(payload).digest()[:8], "big")
    if value == 0:
        raise CompileFailure("derived source identity is zero")
    return value


def pack_float_vectors(values: Iterable[Iterable[Any]], width: int) -> bytes:
    result = bytearray()
    packer = struct.Struct("<" + "f" * width)
    for value in values:
        components = tuple(canonical_float(component) for component in value)
        if len(components) != width:
            raise CompileFailure("vector width changed after validation")
        result.extend(packer.pack(*components))
    return bytes(result)


def pack_uint32_values(values: Iterable[Any]) -> bytes:
    """Pack bounded indices without constructing an attacker-sized format."""

    result = bytearray()
    chunk: list[int] = []
    for value in values:
        if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= 0xFFFFFFFF:
            raise CompileFailure("index changed outside uint32 after validation")
        chunk.append(value)
        if len(chunk) == 16384:
            result.extend(struct.pack(f"<{len(chunk)}I", *chunk))
            chunk.clear()
    if chunk:
        result.extend(struct.pack(f"<{len(chunk)}I", *chunk))
    return bytes(result)


class NativeRenderAssetCompiler:
    def __init__(self, repo_root: Path, manifest_path: Path):
        root_candidate = repo_root if repo_root.is_absolute() else Path.cwd() / repo_root
        self.repo_root_lexical = Path(root_candidate.absolute())
        self.validator = NativeRenderAssetValidator(repo_root, manifest_path)
        self.repo_root = self.validator.repo_root
        self.manifest_path = self.validator.manifest_path
        self.validation_report: dict[str, Any] | None = None
        self.manifest: dict[str, Any] | None = None
        self.package_id = ""
        self.asset_ids: dict[tuple[str, str], int] = {}
        self.object_ids: dict[str, int] = {}

    def prepare(self) -> None:
        self.validation_report = self.validator.validate()
        if not self.validation_report["summary"]["valid"]:
            diagnostic = self.validation_report["diagnostics"][0]
            raise CompileFailure(
                "source validation failed: "
                f"{diagnostic['code']} at {diagnostic['path']}: {diagnostic['message']}"
            )
        if self.validator.manifest is None or self.validator.glb is None:
            raise CompileFailure("source validator did not retain validated inputs")
        self.manifest = self.validator.manifest
        self.package_id = self.manifest["package"]["id"]
        all_values: dict[int, str] = {}
        for kind, entries in (
            ("mesh", self.manifest["meshes"]),
            ("texture", self.manifest["textures"]),
            ("material", self.manifest["materials"]),
            ("sampler", self.manifest["samplers"]),
        ):
            for entry in entries:
                logical_id = entry["id"]
                value = source_identity(self.package_id, kind, logical_id)
                if value in all_values:
                    raise CompileFailure(
                        f"source identity collision between {all_values[value]} and {kind}:{logical_id}"
                    )
                all_values[value] = f"{kind}:{logical_id}"
                self.asset_ids[(kind, logical_id)] = value
        for entry in self.manifest["meshes"]:
            logical_id = entry["object_id"]
            value = source_identity(self.package_id, "object", logical_id)
            if value in self.object_ids.values():
                raise CompileFailure("static object source identity collision")
            self.object_ids[logical_id] = value

    def _mesh_payload(self, entry: dict[str, Any]) -> bytes:
        source = self.validator.mesh_sources[entry["id"]]["glb"]
        if not isinstance(source, dict):
            raise CompileFailure("validated GLB mesh source disappeared")
        positions = source["positions"]
        normals = source["normals"]
        tangents = source["tangents"]
        texcoords = source["texcoords"]
        indices = source["index_values"]
        index_format = 0 if len(positions) <= 65535 and max(indices) <= 65535 else 1
        counts = (
            len(positions),
            len(normals),
            len(tangents),
            0,
            len(texcoords),
            0,
            0,
            len(indices),
        )
        payload = bytearray()
        payload.extend(struct.pack("<I", 1))
        payload.extend(encode_string(entry["id"]))
        payload.extend(struct.pack("<BBBBQ", 0, index_format, 0, 0, 1))
        payload.extend(
            struct.pack(
                "<6f",
                *(canonical_float(value) for value in source["bounds_min"]),
                *(canonical_float(value) for value in source["bounds_max"]),
            )
        )
        payload.extend(struct.pack("<8I", *counts))
        payload.extend(pack_float_vectors(positions, 3))
        payload.extend(pack_float_vectors(normals, 3))
        payload.extend(pack_float_vectors(tangents, 4))
        payload.extend(pack_float_vectors(texcoords, 2))
        payload.extend(pack_uint32_values(indices))
        return bytes(payload)

    def _texture_payload(self, entry: dict[str, Any]) -> bytes:
        images = self.validator.texture_images[entry["id"]]
        color_space = 1 if entry["color_space"] == "srgb" else 0
        payload = bytearray()
        payload.extend(struct.pack("<I", 1))
        payload.extend(encode_string(entry["id"]))
        payload.extend(struct.pack("<BBBB", 0, 2, color_space, 0))
        payload.extend(struct.pack("<4I", images[0].width, images[0].height, 1, len(images)))
        for image in images:
            row_pitch = image.width * 4
            layer_pitch = row_pitch * image.height
            payload.extend(
                struct.pack(
                    "<IIQQQ",
                    image.width,
                    image.height,
                    row_pitch,
                    layer_pitch,
                    len(image.rgba),
                )
            )
            payload.extend(image.rgba)
        return bytes(payload)

    def _sampler_payload(self, entry: dict[str, Any]) -> bytes:
        payload = bytearray()
        payload.extend(struct.pack("<I", 1))
        payload.extend(encode_string(entry["id"]))
        payload.extend(
            struct.pack(
                "<BBBBBBH",
                SAMPLER_FILTERS[entry["minification_filter"]],
                SAMPLER_FILTERS[entry["magnification_filter"]],
                SAMPLER_FILTERS[entry["mip_filter"]],
                SAMPLER_ADDRESS_MODES[entry["address_u"]],
                SAMPLER_ADDRESS_MODES[entry["address_v"]],
                SAMPLER_ADDRESS_MODES[entry["address_w"]],
                0,
            )
        )
        payload.extend(
            struct.pack(
                "<3f",
                canonical_float(entry["mip_lod_bias"]),
                canonical_float(entry["minimum_lod"]),
                canonical_float(entry["maximum_lod"]),
            )
        )
        payload.extend(
            struct.pack(
                "<B3xf",
                int(entry["anisotropy_enabled"]),
                canonical_float(entry["maximum_anisotropy"]),
            )
        )
        payload.extend(
            struct.pack(
                "<BBH",
                int(entry["compare_enabled"]),
                SAMPLER_COMPARE_OPERATIONS[entry["compare_operation"]],
                0,
            )
        )
        payload.extend(
            struct.pack(
                "<4f", *(canonical_float(value) for value in entry["border_color"])
            )
        )
        return bytes(payload)

    def _binding_payload(self, binding: dict[str, Any] | None) -> bytes:
        if binding is None:
            return struct.pack("<QQB7x5f", 0, 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0)
        texture_id = self.asset_ids[("texture", binding["texture"])]
        sampler_id = self.asset_ids[("sampler", binding["sampler"])]
        return struct.pack(
            "<QQB7x5f",
            texture_id,
            sampler_id,
            binding["uv_set"],
            canonical_float(binding["scale"][0]),
            canonical_float(binding["scale"][1]),
            canonical_float(binding["offset"][0]),
            canonical_float(binding["offset"][1]),
            canonical_float(binding["rotation_radians"]),
        )

    def _material_payload(self, entry: dict[str, Any]) -> bytes:
        payload = bytearray()
        payload.extend(struct.pack("<I", 4))
        payload.extend(encode_string(entry["id"]))
        payload.extend(
            struct.pack(
                "<8B",
                MATERIAL_MODELS[entry["model"]],
                MATERIAL_WORKFLOWS[entry["workflow"]],
                MATERIAL_BLEND_MODES[entry["blend_mode"]],
                MATERIAL_ALPHA_TEST_MODES[entry["alpha_test_mode"]],
                BASE_COLOR_TRANSFERS[entry["base_color_transfer"]],
                int(entry["double_sided"]),
                int(entry["depth_write"]),
                0,
            )
        )
        factors = (
            *entry["base_color_factor"],
            entry["metallic_factor"],
            entry["roughness_factor"],
            *entry["specular_factor"],
            entry["normal_scale"],
            entry["occlusion_strength"],
            *entry["emissive_factor"],
            entry["emissive_strength"],
            entry["alpha_cutoff"],
            entry["index_of_refraction"],
        )
        if len(factors) != 17:
            raise CompileFailure("material factor layout changed")
        payload.extend(struct.pack("<17f", *(canonical_float(value) for value in factors)))
        bindings = entry["textures"]
        for slot in MATERIAL_TEXTURE_SLOTS:
            payload.extend(self._binding_payload(bindings.get(slot)))
        return bytes(payload)

    def _instance_payload(self, entry: dict[str, Any]) -> bytes:
        payload = bytearray()
        payload.extend(struct.pack("<I", 1))
        payload.extend(
            struct.pack(
                "<QQ",
                self.asset_ids[("mesh", entry["id"])],
                self.asset_ids[("material", entry["material"])],
            )
        )
        payload.extend(
            struct.pack(
                "<16f",
                *(canonical_float(value) for value in entry["render_from_object"]),
            )
        )
        flags = 0
        for token in entry["instance_flags"]:
            flags |= INSTANCE_FLAGS[token]
        payload.extend(struct.pack("<II", 0xFFFFFFFF, flags))
        return bytes(payload)

    def _asset_manifest_records(self) -> list[dict[str, Any]]:
        assert self.manifest is not None
        records: list[dict[str, Any]] = []
        for kind, entries in (
            ("mesh", self.manifest["meshes"]),
            ("texture", self.manifest["textures"]),
            ("material", self.manifest["materials"]),
            ("sampler", self.manifest["samplers"]),
        ):
            for entry in entries:
                records.append(
                    {
                        "kind": kind,
                        "logical_id": entry["id"],
                        "source_id_hex": f"{self.asset_ids[(kind, entry['id'])]:016x}",
                    }
                )
        return sorted(records, key=lambda item: item["source_id_hex"])

    def embedded_manifest(self) -> bytes:
        assert self.manifest is not None
        package = self.manifest["package"]
        source = self.manifest["source"]
        compiler_hash = sha256_file(Path(__file__).resolve(), max_bytes=MAX_SOURCE_BYTES)
        tool_dependencies = [
            {
                "path": "tools/validate_cityworld_asset.py",
                "sha256": sha256_file(
                    SCRIPT_DIR / "validate_cityworld_asset.py",
                    max_bytes=MAX_SOURCE_BYTES,
                ),
            },
            {
                "path": "tools/validate_native_render_asset.py",
                "sha256": sha256_file(
                    SCRIPT_DIR / "validate_native_render_asset.py",
                    max_bytes=MAX_SOURCE_BYTES,
                ),
            },
        ]
        if self.validator.manifest_bytes is None:
            raise CompileFailure("validated source declaration bytes disappeared")
        source_manifest_hash = sha256_bytes(self.validator.manifest_bytes)
        value = {
            "assets": self._asset_manifest_records(),
            "claims": self.manifest["claims"],
            "compiler": {
                "dependencies": tool_dependencies,
                "format": COMPILER_FORMAT,
                "path": "tools/compile_native_render_asset.py",
                "sha256": compiler_hash,
            },
            "counts": {
                "assets": len(self.asset_ids),
                "instances": len(self.object_ids),
                **self.validator.stats,
            },
            "format": PACKAGE_MANIFEST_FORMAT,
            "instances": sorted(
                (
                    {
                        "flags": entry["instance_flags"],
                        "logical_id": logical_id,
                        "source_id_hex": f"{source_id:016x}",
                    }
                    for entry in self.manifest["meshes"]
                    for logical_id, source_id in (
                        (entry["object_id"], self.object_ids[entry["object_id"]]),
                    )
                ),
                key=lambda item: item["source_id_hex"],
            ),
            "package": {
                "author": package["author"],
                "creation_attestation": package["creation_attestation"],
                "id": self.package_id,
                "license": package["license"],
                "modified": package["modified"],
                "origin_class": package["origin_class"],
                "source_revision": package["source_revision"],
                "source_uri": package["source_uri"],
            },
            "source": {
                "composition": source["composition"],
                "generator": source["generator"],
                "glb": source["glb"],
                "manifest_path": self.manifest_path.relative_to(self.repo_root).as_posix(),
                "manifest_sha256": source_manifest_hash,
            },
        }
        encoded = canonical_json(value).encode("ascii")
        if len(encoded) > 1024 * 1024:
            raise CompileFailure("embedded package manifest exceeds v1 limit")
        return encoded

    def build_package(self) -> tuple[bytes, dict[str, Any]]:
        if self.manifest is None:
            self.prepare()
        assert self.manifest is not None
        assets: list[Record] = []
        for entry in self.manifest["meshes"]:
            assets.append(
                Record(RECORD_MESH, self.asset_ids[("mesh", entry["id"])], self._mesh_payload(entry))
            )
        for entry in self.manifest["textures"]:
            assets.append(
                Record(RECORD_TEXTURE, self.asset_ids[("texture", entry["id"])], self._texture_payload(entry))
            )
        for entry in self.manifest["materials"]:
            assets.append(
                Record(RECORD_MATERIAL, self.asset_ids[("material", entry["id"])], self._material_payload(entry))
            )
        for entry in self.manifest["samplers"]:
            assets.append(
                Record(RECORD_SAMPLER, self.asset_ids[("sampler", entry["id"])], self._sampler_payload(entry))
            )
        assets.sort(key=lambda record: record.source_id)
        instances = sorted(
            (
                Record(
                    RECORD_STATIC_INSTANCE,
                    self.object_ids[entry["object_id"]],
                    self._instance_payload(entry),
                )
                for entry in self.manifest["meshes"]
            ),
            key=lambda record: record.source_id,
        )
        records = [Record(RECORD_MANIFEST, 0, self.embedded_manifest()), *assets, *instances]
        body_builder = bytearray()
        for record in records:
            body_builder.extend(record.encode())
            if PACKAGE_HEADER_BYTES + len(body_builder) > MAX_PACKAGE_BYTES:
                raise CompileFailure("package exceeds v1 byte limit")
        body = bytes(body_builder)
        body_digest = hashlib.sha256(body).digest()
        package_size = PACKAGE_HEADER_BYTES + len(body)
        if package_size > MAX_PACKAGE_BYTES:
            raise CompileFailure("package exceeds v1 byte limit")
        header = PACKAGE_HEADER.pack(
            PACKAGE_MAGIC,
            PACKAGE_VERSION,
            PACKAGE_HEADER_BYTES,
            0,
            len(records),
            len(assets),
            len(instances),
            package_size,
            body_digest,
            b"\x00" * 8,
        )
        package_bytes = header + body
        outputs = self.manifest["outputs"]
        report = {
            "compiler": {
                "dependencies": [
                    {
                        "path": "tools/validate_cityworld_asset.py",
                        "sha256": sha256_file(
                            SCRIPT_DIR / "validate_cityworld_asset.py",
                            max_bytes=MAX_SOURCE_BYTES,
                        ),
                    },
                    {
                        "path": "tools/validate_native_render_asset.py",
                        "sha256": sha256_file(
                            SCRIPT_DIR / "validate_native_render_asset.py",
                            max_bytes=MAX_SOURCE_BYTES,
                        ),
                    },
                ],
                "format": COMPILER_FORMAT,
                "path": "tools/compile_native_render_asset.py",
                "sha256": sha256_file(Path(__file__).resolve(), max_bytes=MAX_SOURCE_BYTES),
            },
            "format": REPORT_FORMAT,
            "output": {
                "body_sha256": body_digest.hex(),
                "bytes": len(package_bytes),
                "path": outputs["package_path"],
                "record_count": len(records),
                "sha256": sha256_bytes(package_bytes),
            },
            "package_id": self.package_id,
            "source": {
                "composition": self.manifest["source"]["composition"],
                "glb": self.manifest["source"]["glb"],
                "manifest_path": self.manifest_path.relative_to(self.repo_root).as_posix(),
                "manifest_sha256": sha256_bytes(self.validator.manifest_bytes),
            },
            "summary": {
                "asset_count": len(assets),
                "instance_count": len(instances),
                **self.validator.stats,
            },
        }
        return package_bytes, report


def declared_output(repo_root: Path, value: Any, suffix: str) -> Path:
    relative = safe_relative_path(value)
    if relative is None or not relative.endswith(suffix):
        raise CompileFailure(f"declared output must be a portable {suffix} path")
    return checked_output_path(repo_root, repo_root / relative, suffix)


def checked_output_path(
    repo_root: Path,
    value: Path,
    suffix: str,
    *,
    lexical_root: Path | None = None,
) -> Path:
    root = repo_root.resolve()
    candidate = value if value.is_absolute() else Path.cwd() / value
    candidate = Path(candidate.absolute())
    if not candidate.as_posix().endswith(suffix):
        raise CompileFailure(f"output path must end with {suffix}")
    argument_root = root if lexical_root is None else lexical_root
    try:
        relative = candidate.relative_to(argument_root)
    except ValueError as error:
        raise CompileFailure("output override escapes repository root") from error
    lexical = root / relative
    cursor = root
    for component in relative.parts:
        cursor = cursor / component
        if cursor.is_symlink():
            raise CompileFailure("output path must not traverse a symlink")
    return lexical


def atomic_write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as handle:
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
    except BaseException:
        try:
            temporary.unlink()
        except OSError:
            pass
        raise


def validate_checked(path: Path, expected: bytes, label: str) -> None:
    if not path.is_file() or path.is_symlink():
        raise CompileFailure(f"checked {label} is missing or not a regular file: {path}")
    actual = path.read_bytes()
    if actual != expected:
        raise CompileFailure(
            f"checked {label} is stale: expected {sha256_bytes(expected)}, got {sha256_bytes(actual)}"
        )


def _arguments(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--repo-root", type=Path, default=Path.cwd())
    parser.add_argument("--output", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--validate-checked", action="store_true")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = _arguments(sys.argv[1:] if argv is None else argv)
    try:
        compiler = NativeRenderAssetCompiler(args.repo_root, args.manifest)
        compiler.prepare()
        package_bytes, report = compiler.build_package()
        assert compiler.manifest is not None
        output_path = (
            checked_output_path(
                compiler.repo_root,
                args.output,
                ".rornative",
                lexical_root=compiler.repo_root_lexical,
            )
            if args.output is not None
            else declared_output(compiler.repo_root, compiler.manifest["outputs"]["package_path"], ".rornative")
        )
        report_path = (
            checked_output_path(
                compiler.repo_root,
                args.report,
                ".compile.json",
                lexical_root=compiler.repo_root_lexical,
            )
            if args.report is not None
            else declared_output(compiler.repo_root, compiler.manifest["outputs"]["report_path"], ".compile.json")
        )
        report_bytes = canonical_pretty(report)
        if args.validate_checked:
            validate_checked(output_path, package_bytes, "package")
            validate_checked(report_path, report_bytes, "report")
        else:
            atomic_write(output_path, package_bytes)
            atomic_write(report_path, report_bytes)
        print(canonical_json(report))
        return 0
    except MemoryError:
        print(
            "native render asset compile failed: bounded allocation could not be satisfied",
            file=sys.stderr,
        )
        return 1
    except (OverflowError, struct.error):
        print(
            "native render asset compile failed: binary packing rejected a bounded field",
            file=sys.stderr,
        )
        return 1
    except (CompileFailure, OSError, ValueError) as error:
        print(f"native render asset compile failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
