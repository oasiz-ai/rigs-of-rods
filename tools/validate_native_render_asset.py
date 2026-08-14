#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate forward-native renderer sources without renderer dependencies.

The v1 contract deliberately accepts a small, canonical static-glTF profile
and explicit uncompressed texture/material declarations. Unsupported or
ambiguous input fails closed; no legacy resource is consulted.
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
import stat
import struct
import sys
from typing import Any, Iterable


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

# Reuse the repository's reviewed duplicate-key, canonical-JSON, SHA-256, and
# strict GLB/accessor implementation. This is shared parsing logic only: the
# native compiler does not invoke the CityWorld compiler or emit legacy files.
from validate_cityworld_asset import (  # noqa: E402
    Glb,
    canonical_json,
    is_sha256,
    reject_duplicate_keys,
    resolve_beneath,
    safe_relative_path,
)


SOURCE_FORMAT = "ror-native-render-source-v1"
SOURCE_FORMAT_V2 = "ror-native-render-source-v2"
REPORT_FORMAT = "ror-native-render-validation-v1"
COMPOSITION_FORMAT = "ror-native-render-composition-v1"
PACKAGE_ID_PATTERN = re.compile(r"^rorng_[a-z0-9_]+$")
ASCII_TEXT_PATTERN = re.compile(r"^[\x20-\x7e]+$")
MAX_MANIFEST_BYTES = 2 * 1024 * 1024
MAX_SOURCE_BYTES = 128 * 1024 * 1024
MAX_GLTF_NODES = 4096
MAX_VERTICES = 4_000_000
MAX_INDICES = 12_000_000
MAX_TEXTURE_DIMENSION = 16384
MAX_TEXTURE_BYTES = 256 * 1024 * 1024
MAX_TEXTURE_WORKING_SET_BYTES = 256 * 1024 * 1024
MAX_ASSET_COUNT = 4096
MAX_NAME_BYTES = 255

ORIGIN_CLASSES = frozenset(
    {
        "project_original",
        "clean_room_recreation",
        "rights_cleared_derivative",
        "legacy_compat_conversion",
    }
)
TEXTURE_ROLES = frozenset(
    {
        "base_color",
        "metallic_roughness",
        "normal",
        "occlusion",
        "emissive",
        "specular",
    }
)
SRGB_TEXTURE_ROLES = frozenset({"base_color", "emissive"})
LINEAR_TEXTURE_ROLES = TEXTURE_ROLES - SRGB_TEXTURE_ROLES
MATERIAL_TEXTURE_SLOTS = (
    "base_color",
    "metallic_roughness",
    "normal",
    "occlusion",
    "emissive",
    "specular",
)
SAMPLER_FILTERS = {"nearest": 0, "linear": 1}
SAMPLER_ADDRESS_MODES = {
    "repeat": 0,
    "mirrored_repeat": 1,
    "clamp_to_edge": 2,
    "clamp_to_border": 3,
}
SAMPLER_COMPARE_OPERATIONS = {
    "never": 0,
    "less": 1,
    "equal": 2,
    "less_equal": 3,
    "greater": 4,
    "not_equal": 5,
    "greater_equal": 6,
    "always": 7,
}
MATERIAL_MODELS = {"pbr_metallic_roughness": 0, "unlit": 1}
MATERIAL_WORKFLOWS = {"metallic_roughness": 0, "specular": 1}
MATERIAL_TRANSMISSION_MODES = {"none": 0, "thin_parallel_slab": 1}
MATERIAL_BLEND_MODES = {
    "replace": 0,
    "straight_source_over": 1,
    "legacy_straight_alpha": 2,
}
MATERIAL_ALPHA_TEST_MODES = {
    "disabled": 0,
    "greater": 1,
    "greater_equal": 2,
}
BASE_COLOR_TRANSFERS = {
    "srgb_decode_before_filter": 0,
    "srgb_display_domain_filter_then_decode": 1,
}
INSTANCE_FLAGS = {
    "casts_shadow": 1 << 0,
    "receives_shadow": 1 << 1,
    "visible_in_reflections": 1 << 2,
}


@dataclass(frozen=True)
class Diagnostic:
    code: str
    path: str
    message: str

    def as_dict(self) -> dict[str, str]:
        return {"code": self.code, "message": self.message, "path": self.path}


@dataclass(frozen=True)
class TgaImage:
    width: int
    height: int
    rgba: bytes


def _is_number(value: Any) -> bool:
    return not isinstance(value, bool) and isinstance(value, (int, float))


def _float32_round_trip(value: Any) -> tuple[float | None, str | None]:
    if not _is_number(value):
        return None, "expected a JSON number"
    if isinstance(value, float) and value == 0.0 and math.copysign(1.0, value) < 0.0:
        return None, "negative zero is not canonical"
    # Refuse enormous Python integers before converting them. This keeps the
    # diagnostic stable across Python builds and avoids version-specific
    # OverflowError text for hostile multi-thousand-digit JSON integers.
    if isinstance(value, int) and value.bit_length() > 1024:
        return None, "number is outside finite binary32 range"
    try:
        converted = float(value)
        if not math.isfinite(converted):
            return None, "number must be finite"
        packed = struct.pack("<f", converted)
        rounded = struct.unpack("<f", packed)[0]
    except (OverflowError, ValueError, struct.error):
        return None, "number is outside finite binary32 range"
    if not math.isfinite(rounded):
        return None, "number is outside finite binary32 range"
    if converted != 0.0 and rounded == 0.0:
        return None, "number underflows binary32"
    if rounded == 0.0 and math.copysign(1.0, rounded) < 0.0:
        return None, "negative zero is not canonical"
    return rounded, None


def _binary32(value: float) -> float:
    """Round one arithmetic result exactly as an IEEE-754 binary32 operation."""

    try:
        return struct.unpack("<f", struct.pack("<f", value))[0]
    except (OverflowError, struct.error):
        return math.copysign(math.inf, value)


def _binary32_add(left: float, right: float) -> float:
    return _binary32(left + right)


def _binary32_subtract(left: float, right: float) -> float:
    return _binary32(left - right)


def _binary32_multiply(left: float, right: float) -> float:
    return _binary32(left * right)


def _binary32_dot3(left: Iterable[float], right: Iterable[float]) -> float:
    left_values = tuple(left)
    right_values = tuple(right)
    return _binary32_add(
        _binary32_add(
            _binary32_multiply(left_values[0], right_values[0]),
            _binary32_multiply(left_values[1], right_values[1]),
        ),
        _binary32_multiply(left_values[2], right_values[2]),
    )


def _binary32_cross3(
    left: tuple[float, float, float], right: tuple[float, float, float]
) -> tuple[float, float, float]:
    return (
        _binary32_subtract(
            _binary32_multiply(left[1], right[2]),
            _binary32_multiply(left[2], right[1]),
        ),
        _binary32_subtract(
            _binary32_multiply(left[2], right[0]),
            _binary32_multiply(left[0], right[2]),
        ),
        _binary32_subtract(
            _binary32_multiply(left[0], right[1]),
            _binary32_multiply(left[1], right[0]),
        ),
    )


def _binary32_normalized3(
    value: tuple[float, float, float],
) -> tuple[float, float, float] | None:
    # NativeRenderAssetPackage.cpp intentionally accumulates the length in
    # double, rounds the reciprocal to binary32, then performs binary32
    # component multiplies. Keep this source gate byte-for-byte semantic with
    # the runtime gate rather than relying on Python's binary64 arithmetic.
    x, y, z = value
    length_squared = (float(x) * float(x) + float(y) * float(y)) + float(z) * float(z)
    if not math.isfinite(length_squared) or length_squared <= 1e-16:
        return None
    reciprocal = _binary32(1.0 / math.sqrt(length_squared))
    result = tuple(_binary32_multiply(component, reciprocal) for component in value)
    return result if all(math.isfinite(component) for component in result) else None


def _binary32_linear_determinant(matrix: tuple[float, ...]) -> float:
    """Mirror RenderMath::LinearDeterminant's ordered binary32 expression."""

    m00, m01, m02 = matrix[0], matrix[4], matrix[8]
    m10, m11, m12 = matrix[1], matrix[5], matrix[9]
    m20, m21, m22 = matrix[2], matrix[6], matrix[10]
    first = _binary32_multiply(
        m00,
        _binary32_subtract(
            _binary32_multiply(m11, m22),
            _binary32_multiply(m12, m21),
        ),
    )
    second = _binary32_multiply(
        m01,
        _binary32_subtract(
            _binary32_multiply(m10, m22),
            _binary32_multiply(m12, m20),
        ),
    )
    third = _binary32_multiply(
        m02,
        _binary32_subtract(
            _binary32_multiply(m10, m21),
            _binary32_multiply(m11, m20),
        ),
    )
    return _binary32_add(_binary32_subtract(first, second), third)


def _is_binary32_canonical_affine(matrix: tuple[float, ...]) -> bool:
    return (
        len(matrix) == 16
        and all(math.isfinite(value) for value in matrix)
        and matrix[3] == 0.0
        and matrix[7] == 0.0
        and matrix[11] == 0.0
        and matrix[15] == 1.0
    )


def _has_binary32_invertible_affine_transform(matrix: tuple[float, ...]) -> bool:
    if not _is_binary32_canonical_affine(matrix):
        return False
    determinant = _binary32_linear_determinant(matrix)
    return math.isfinite(determinant) and abs(determinant) > _binary32(1.0e-8)


def _double_length3(value: Iterable[float]) -> float:
    x, y, z = (float(component) for component in value)
    return math.sqrt((x * x + y * y) + z * z)


def _is_canonical_ascii(value: Any, *, maximum: int = MAX_NAME_BYTES) -> bool:
    return (
        isinstance(value, str)
        and bool(value)
        and value == value.strip()
        and len(value.encode("ascii", errors="ignore")) <= maximum
        and ASCII_TEXT_PATTERN.fullmatch(value) is not None
    )


def _exact_keys(
    value: Any,
    required: Iterable[str],
    *,
    optional: Iterable[str] = (),
) -> bool:
    return isinstance(value, dict) and set(value) == set(required) | (
        set(value) & set(optional)
    ) and set(required).issubset(value)


def _align4(value: int) -> int:
    return (value + 3) & ~3


def _dot3(left: Iterable[float], right: Iterable[float]) -> float:
    return sum(float(a) * float(b) for a, b in zip(left, right))


def _cross3(
    left: tuple[float, float, float], right: tuple[float, float, float]
) -> tuple[float, float, float]:
    return (
        left[1] * right[2] - left[2] * right[1],
        left[2] * right[0] - left[0] * right[2],
        left[0] * right[1] - left[1] * right[0],
    )


def _normalized3(value: tuple[float, float, float]) -> tuple[float, float, float] | None:
    length_squared = _dot3(value, value)
    if not math.isfinite(length_squared) or length_squared <= 1e-16:
        return None
    reciprocal = 1.0 / math.sqrt(length_squared)
    return tuple(component * reciprocal for component in value)


def _has_negative_zero(values: Iterable[Any]) -> bool:
    return any(
        isinstance(value, float)
        and value == 0.0
        and math.copysign(1.0, value) < 0.0
        for value in values
    )


def _read_regular_file(path: Path, maximum: int) -> bytes:
    flags = os.O_RDONLY | getattr(os, "O_BINARY", 0) | getattr(os, "O_NOFOLLOW", 0)
    descriptor = os.open(path, flags)
    try:
        metadata = os.fstat(descriptor)
        if not stat.S_ISREG(metadata.st_mode):
            raise ValueError("source is not a regular file")
        with os.fdopen(descriptor, "rb", closefd=False) as handle:
            data = handle.read(maximum + 1)
        if len(data) > maximum:
            raise ValueError(f"file exceeds {maximum} byte limit")
        if len(data) != metadata.st_size:
            raise ValueError("source size changed while it was read")
        return data
    finally:
        os.close(descriptor)


def decode_tga_rgba(data: bytes, expected_width: int, expected_height: int) -> TgaImage:
    """Read the canonical uncompressed BGRA/TGA source profile."""

    if len(data) < 18:
        raise ValueError("TGA header is truncated")
    (
        identifier_bytes,
        color_map_type,
        image_type,
        color_map_first,
        color_map_length,
        color_map_bits,
        x_origin,
        y_origin,
        width,
        height,
        bits_per_pixel,
        descriptor,
    ) = struct.unpack_from("<BBBHHBHHHHBB", data, 0)
    if (
        identifier_bytes != 0
        or color_map_type != 0
        or image_type != 2
        or color_map_first != 0
        or color_map_length != 0
        or color_map_bits != 0
        or x_origin != 0
        or y_origin != 0
        or bits_per_pixel != 32
        or descriptor != 0x28
    ):
        raise ValueError(
            "TGA must be uncompressed 32-bit BGRA, top-left origin, with 8 alpha bits"
        )
    if width != expected_width or height != expected_height:
        raise ValueError("TGA dimensions do not match the declaration")
    required_size = 18 + width * height * 4
    if len(data) != required_size:
        raise ValueError("TGA byte count is not canonical")
    source = memoryview(data)[18:]
    rgba = bytearray(len(source))
    for offset in range(0, len(source), 4):
        rgba[offset] = source[offset + 2]
        rgba[offset + 1] = source[offset + 1]
        rgba[offset + 2] = source[offset]
        rgba[offset + 3] = source[offset + 3]
    return TgaImage(width, height, bytes(rgba))


class NativeRenderAssetValidator:
    def __init__(self, repo_root: Path, manifest_path: Path):
        root_candidate = repo_root if repo_root.is_absolute() else Path.cwd() / repo_root
        self.repo_root_lexical = Path(root_candidate.absolute())
        self.repo_root = self.repo_root_lexical.resolve()
        candidate = manifest_path if manifest_path.is_absolute() else Path.cwd() / manifest_path
        candidate = Path(candidate.absolute())
        try:
            relative = candidate.relative_to(self.repo_root_lexical)
            self.manifest_path = self.repo_root / relative
        except ValueError:
            self.manifest_path = candidate
        self.manifest: dict[str, Any] | None = None
        self.source_format = SOURCE_FORMAT
        self.glb: Glb | None = None
        self.glb_path: Path | None = None
        self.composition_path: Path | None = None
        self.manifest_bytes: bytes | None = None
        self.source_bytes: dict[Path, bytes] = {}
        self.texture_images: dict[str, tuple[TgaImage, ...]] = {}
        self.mesh_sources: dict[str, dict[str, Any]] = {}
        self.diagnostics: list[Diagnostic] = []
        self.stats = {
            "indices": 0,
            "instances": 0,
            "materials": 0,
            "meshes": 0,
            "samplers": 0,
            "texture_bytes": 0,
            "textures": 0,
            "triangles": 0,
            "vertices": 0,
        }

    def add(self, code: str, path: str, message: str) -> None:
        self.diagnostics.append(Diagnostic(code, path, message))

    def _record(self, value: Any, keys: Iterable[str], pointer: str) -> dict[str, Any] | None:
        if not isinstance(value, dict):
            self.add("FIELD_TYPE", pointer, "field must be an object")
            return None
        expected = set(keys)
        missing = sorted(expected - set(value))
        unknown = sorted(set(value) - expected)
        for key in missing:
            self.add("FIELD_MISSING", f"{pointer}.{key}", "required field is missing")
        for key in unknown:
            self.add("FIELD_UNKNOWN", f"{pointer}.{key}", "field is not part of v1")
        return value if not missing and not unknown else None

    def _array(self, value: Any, pointer: str, *, maximum: int = 4096) -> list[Any] | None:
        if not isinstance(value, list):
            self.add("FIELD_TYPE", pointer, "field must be an array")
            return None
        if not value:
            self.add("ARRAY_EMPTY", pointer, "array must not be empty")
            return None
        if len(value) > maximum:
            self.add("LIMIT_EXCEEDED", pointer, f"array exceeds {maximum} entries")
            return None
        return value

    def _identifier(self, value: Any, pointer: str) -> str | None:
        if not isinstance(value, str) or PACKAGE_ID_PATTERN.fullmatch(value) is None:
            self.add("IDENTIFIER_INVALID", pointer, "expected a canonical rorng_ identifier")
            return None
        if len(value.encode("ascii")) > MAX_NAME_BYTES:
            self.add("IDENTIFIER_TOO_LONG", pointer, "identifier exceeds 255 bytes")
            return None
        return value

    def _text(self, value: Any, pointer: str, *, maximum: int = 1024) -> str | None:
        if not _is_canonical_ascii(value, maximum=maximum):
            self.add("TEXT_INVALID", pointer, "expected bounded canonical printable ASCII")
            return None
        return value

    def _number(
        self,
        value: Any,
        pointer: str,
        *,
        minimum: float | None = None,
        maximum: float | None = None,
    ) -> float | None:
        result, error = _float32_round_trip(value)
        if error is not None or result is None:
            code = (
                "NUMBER_NEGATIVE_ZERO"
                if error == "negative zero is not canonical"
                else "NUMBER_FLOAT32"
            )
            self.add(code, pointer, error or "number is not binary32")
            return None
        if minimum is not None and result < minimum:
            self.add("NUMBER_RANGE", pointer, f"number must be at least {minimum}")
            return None
        if maximum is not None and result > maximum:
            self.add("NUMBER_RANGE", pointer, f"number must be at most {maximum}")
            return None
        return result

    def _vector(
        self,
        value: Any,
        width: int,
        pointer: str,
        *,
        minimum: float | None = None,
        maximum: float | None = None,
    ) -> tuple[float, ...] | None:
        if not isinstance(value, list) or len(value) != width:
            self.add("VECTOR_INVALID", pointer, f"expected {width} finite components")
            return None
        converted = tuple(
            self._number(
                component,
                f"{pointer}[{index}]",
                minimum=minimum,
                maximum=maximum,
            )
            for index, component in enumerate(value)
        )
        if any(component is None for component in converted):
            return None
        return tuple(float(component) for component in converted if component is not None)

    def _enum(self, value: Any, choices: dict[str, int], pointer: str) -> str | None:
        if not isinstance(value, str) or value not in choices:
            self.add("ENUM_INVALID", pointer, f"expected one of {sorted(choices)}")
            return None
        return value

    def _source_path(
        self,
        record: Any,
        pointer: str,
        *,
        maximum: int = MAX_SOURCE_BYTES,
    ) -> Path | None:
        entry = self._record(record, ("path", "sha256"), pointer)
        if entry is None:
            return None
        relative = safe_relative_path(entry.get("path"))
        expected_hash = entry.get("sha256")
        if relative is None:
            self.add("PATH_INVALID", f"{pointer}.path", "path must be portable and relative")
            return None
        if not is_sha256(expected_hash):
            self.add("SHA256_INVALID", f"{pointer}.sha256", "expected lowercase SHA-256")
            return None
        try:
            lexical_path = self.repo_root
            for component in PurePosixPath(relative).parts:
                lexical_path = lexical_path / component
                if lexical_path.is_symlink():
                    raise ValueError("source path traverses a symlink")
            path = resolve_beneath(self.repo_root, relative)
            if not path.is_file() or path.is_symlink():
                raise ValueError("source is not a regular non-symlink file")
            data = _read_regular_file(path, maximum)
            actual_hash = hashlib.sha256(data).hexdigest()
        except (OSError, ValueError) as error:
            self.add("SOURCE_UNREADABLE", f"{pointer}.path", str(error))
            return None
        if actual_hash != expected_hash:
            self.add("SOURCE_HASH_MISMATCH", pointer, "source SHA-256 does not match")
            return None
        self.source_bytes[path] = data
        return path

    def load_manifest(self) -> None:
        try:
            relative = self.manifest_path.relative_to(self.repo_root)
            lexical_path = self.repo_root
            for component in relative.parts:
                lexical_path = lexical_path / component
                if lexical_path.is_symlink():
                    raise ValueError("manifest path traverses a symlink")
            if not self.manifest_path.is_file() or self.manifest_path.is_symlink():
                raise ValueError("manifest is not a regular non-symlink file")
            self.manifest_bytes = _read_regular_file(
                self.manifest_path, MAX_MANIFEST_BYTES
            )
            manifest_text = self.manifest_bytes.decode("utf-8")
        except (OSError, UnicodeDecodeError, ValueError):
            self.add("MANIFEST_INVALID", "$", "manifest is not a regular canonical UTF-8 file")
            return
        try:
            value = json.loads(
                manifest_text,
                object_pairs_hook=reject_duplicate_keys,
                parse_constant=lambda token: (_ for _ in ()).throw(
                    ValueError(f"non-finite JSON number: {token}")
                ),
            )
        except (json.JSONDecodeError, ValueError):
            self.add("MANIFEST_INVALID", "$", "manifest is not duplicate-free finite JSON")
            return
        manifest = self._record(
            value,
            (
                "claims",
                "format",
                "materials",
                "meshes",
                "outputs",
                "package",
                "samplers",
                "source",
                "textures",
            ),
            "$",
        )
        if manifest is None:
            return
        self.manifest = manifest
        canonical_source = json.dumps(
            manifest,
            ensure_ascii=True,
            indent=2,
            sort_keys=True,
        ) + "\n"
        if manifest_text != canonical_source:
            self.add(
                "MANIFEST_NONCANONICAL",
                "$",
                "source declaration must use canonical sorted pretty JSON",
            )
        source_format = manifest.get("format")
        if source_format not in (SOURCE_FORMAT, SOURCE_FORMAT_V2):
            self.add(
                "FORMAT_UNSUPPORTED", "$.format",
                f"expected {SOURCE_FORMAT} or {SOURCE_FORMAT_V2}",
            )
        else:
            self.source_format = source_format

    def validate_package_record(self) -> None:
        assert self.manifest is not None
        package = self._record(
            self.manifest.get("package"),
            (
                "author",
                "creation_attestation",
                "dimensions_m",
                "display_name",
                "id",
                "license",
                "modified",
                "origin_class",
                "source_revision",
                "source_uri",
            ),
            "$.package",
        )
        if package is None:
            return
        self._identifier(package.get("id"), "$.package.id")
        self._text(package.get("display_name"), "$.package.display_name")
        self._text(package.get("author"), "$.package.author")
        self._text(package.get("license"), "$.package.license", maximum=128)
        self._text(package.get("source_uri"), "$.package.source_uri", maximum=2048)
        self._text(package.get("source_revision"), "$.package.source_revision", maximum=256)
        attestation = self._text(
            package.get("creation_attestation"),
            "$.package.creation_attestation",
            maximum=4096,
        )
        origin = package.get("origin_class")
        if origin not in ORIGIN_CLASSES:
            self.add("ORIGIN_CLASS_INVALID", "$.package.origin_class", "unknown A0 origin class")
        if not isinstance(package.get("modified"), bool):
            self.add("FIELD_TYPE", "$.package.modified", "modified must be boolean")
        dimensions = self._vector(
            package.get("dimensions_m"), 3, "$.package.dimensions_m", minimum=0.0
        )
        if dimensions is not None and any(value <= 0.0 for value in dimensions):
            self.add("DIMENSIONS_INVALID", "$.package.dimensions_m", "all dimensions must be positive")
        if origin in {"project_original", "clean_room_recreation"} and (
            attestation is None
            or "no geometry, texture, material-script, or shader bytes were copied"
            not in attestation.lower()
        ):
            self.add(
                "A0_ATTESTATION_MISSING",
                "$.package.creation_attestation",
                "original/clean-room sources must attest that no protected asset bytes were copied",
            )

    def validate_source_record(self) -> None:
        assert self.manifest is not None
        source = self._record(
            self.manifest.get("source"),
            (
                "composition",
                "coordinate_system",
                "generator",
                "glb",
                "tangent_basis",
                "uv_origin",
            ),
            "$.source",
        )
        if source is None:
            return
        expected = {
            "coordinate_system": "right-handed-y-up-meters",
            "tangent_basis": "tangent-w-times-cross-normal-tangent",
            "uv_origin": "upper-left",
        }
        for field, required in expected.items():
            if source.get(field) != required:
                self.add("SOURCE_CONVENTION", f"$.source.{field}", f"expected {required}")
        self.glb_path = self._source_path(source.get("glb"), "$.source.glb")
        self.composition_path = self._source_path(
            source.get("composition"),
            "$.source.composition",
            maximum=64 * 1024,
        )
        self._source_path(source.get("generator"), "$.source.generator")

    def validate_composition(
        self, glb_meshes: dict[str, dict[str, Any]]
    ) -> None:
        """Validate the checked showcase camera/light/framing contract."""

        if self.composition_path is None:
            return
        pointer = "$.source.composition"
        try:
            payload = self.source_bytes[self.composition_path]
            text = payload.decode("utf-8")
            value = json.loads(
                text,
                object_pairs_hook=reject_duplicate_keys,
                parse_constant=lambda token: (_ for _ in ()).throw(
                    ValueError(f"non-finite JSON number: {token}")
                ),
            )
        except (KeyError, UnicodeDecodeError, json.JSONDecodeError, ValueError):
            self.add(
                "COMPOSITION_INVALID",
                pointer,
                "composition descriptor is not duplicate-free finite UTF-8 JSON",
            )
            return
        if not isinstance(value, dict) or text != (
            json.dumps(value, ensure_ascii=True, indent=2, sort_keys=True) + "\n"
        ):
            self.add(
                "COMPOSITION_NONCANONICAL",
                pointer,
                "composition descriptor must use canonical sorted pretty JSON",
            )
        descriptor = self._record(
            value,
            (
                "camera",
                "environment",
                "exposure",
                "format",
                "package_id",
                "preview",
                "shadow_roi",
                "sun",
                "world_aabb",
            ),
            pointer,
        )
        if descriptor is None:
            return
        if descriptor.get("format") != COMPOSITION_FORMAT:
            self.add(
                "COMPOSITION_FORMAT",
                f"{pointer}.format",
                f"expected {COMPOSITION_FORMAT}",
            )
        package = self.manifest.get("package") if self.manifest is not None else {}
        if descriptor.get("package_id") != (
            package.get("id") if isinstance(package, dict) else None
        ):
            self.add(
                "COMPOSITION_PACKAGE",
                f"{pointer}.package_id",
                "composition package identity does not match the source declaration",
            )

        camera = self._record(
            descriptor.get("camera"),
            (
                "far_clip_m",
                "near_clip_m",
                "position_m",
                "target_m",
                "up",
                "vertical_fov_degrees",
            ),
            f"{pointer}.camera",
        )
        camera_position = camera_target = camera_up = None
        near_clip = far_clip = vertical_fov = None
        if camera is not None:
            camera_position = self._vector(
                camera.get("position_m"), 3, f"{pointer}.camera.position_m"
            )
            camera_target = self._vector(
                camera.get("target_m"), 3, f"{pointer}.camera.target_m"
            )
            camera_up = self._vector(camera.get("up"), 3, f"{pointer}.camera.up")
            near_clip = self._number(
                camera.get("near_clip_m"),
                f"{pointer}.camera.near_clip_m",
                minimum=0.001,
            )
            far_clip = self._number(
                camera.get("far_clip_m"),
                f"{pointer}.camera.far_clip_m",
                minimum=0.01,
                maximum=10000.0,
            )
            vertical_fov = self._number(
                camera.get("vertical_fov_degrees"),
                f"{pointer}.camera.vertical_fov_degrees",
                minimum=10.0,
                maximum=120.0,
            )
            if (
                near_clip is not None
                and far_clip is not None
                and near_clip >= far_clip
            ):
                self.add(
                    "COMPOSITION_CAMERA_CLIP",
                    f"{pointer}.camera",
                    "near clip must be smaller than far clip",
                )

        environment = self._record(
            descriptor.get("environment"),
            ("background_luminance_cd_m2", "color_space", "mode"),
            f"{pointer}.environment",
        )
        if environment is not None:
            self._number(
                environment.get("background_luminance_cd_m2"),
                f"{pointer}.environment.background_luminance_cd_m2",
                minimum=0.0,
                maximum=1000000.0,
            )
            if environment.get("color_space") != "rec709-d65-linear-unit-luminance":
                self.add(
                    "COMPOSITION_ENVIRONMENT",
                    f"{pointer}.environment.color_space",
                    "environment requires the v1 linear D65 luminance convention",
                )
            if environment.get("mode") != "analytic-clear-sky":
                self.add(
                    "COMPOSITION_ENVIRONMENT",
                    f"{pointer}.environment.mode",
                    "environment requires the analytic-clear-sky v1 mode",
                )

        exposure = self._record(
            descriptor.get("exposure"),
            ("ev100", "white_balance_kelvin"),
            f"{pointer}.exposure",
        )
        if exposure is not None:
            self._number(
                exposure.get("ev100"),
                f"{pointer}.exposure.ev100",
                minimum=-24.0,
                maximum=32.0,
            )
            self._number(
                exposure.get("white_balance_kelvin"),
                f"{pointer}.exposure.white_balance_kelvin",
                minimum=1000.0,
                maximum=20000.0,
            )

        preview = self._record(
            descriptor.get("preview"),
            ("format", "height", "path", "sha256", "status", "width"),
            f"{pointer}.preview",
        )
        preview_width = preview_height = None
        if preview is not None:
            preview_width = preview.get("width")
            preview_height = preview.get("height")
            if (
                isinstance(preview_width, bool)
                or not isinstance(preview_width, int)
                or not 64 <= preview_width <= 4096
                or isinstance(preview_height, bool)
                or not isinstance(preview_height, int)
                or not 64 <= preview_height <= 4096
            ):
                self.add(
                    "COMPOSITION_PREVIEW_DIMENSIONS",
                    f"{pointer}.preview",
                    "preview dimensions are outside the bounded v1 profile",
                )
            if preview.get("format") != "ppm-p6-rgb8" or preview.get("status") != "authoring-layout-preview-not-renderer-evidence":
                self.add(
                    "COMPOSITION_PREVIEW_PROFILE",
                    f"{pointer}.preview",
                    "preview must be explicitly labeled as the v1 non-evidence PPM",
                )
            maximum_preview = (
                32 + preview_width * preview_height * 3
                if isinstance(preview_width, int)
                and not isinstance(preview_width, bool)
                and isinstance(preview_height, int)
                and not isinstance(preview_height, bool)
                and 0 < preview_width <= 4096
                and 0 < preview_height <= 4096
                else 64 * 1024 * 1024
            )
            preview_path = self._source_path(
                {"path": preview.get("path"), "sha256": preview.get("sha256")},
                f"{pointer}.preview",
                maximum=maximum_preview,
            )
            if preview_path is not None and isinstance(preview_width, int) and isinstance(preview_height, int):
                preview_bytes = self.source_bytes[preview_path]
                header = f"P6\n{preview_width} {preview_height}\n255\n".encode("ascii")
                raster = preview_bytes[len(header) :] if preview_bytes.startswith(header) else b""
                required_colors = (
                    bytes((49, 53, 57)),
                    bytes((30, 48, 61)),
                    bytes((244, 239, 207)),
                    bytes((255, 126, 18)),
                    bytes((105, 111, 116)),
                )
                if len(raster) != preview_width * preview_height * 3 or any(
                    color not in raster for color in required_colors
                ):
                    self.add(
                        "COMPOSITION_PREVIEW_CONTENT",
                        f"{pointer}.preview",
                        "preview does not contain the exact bounded road/wet/lane/reflector/gate framing",
                    )

        sun = self._record(
            descriptor.get("sun"),
            ("direction_toward_scene", "illuminance_lux", "spectrum"),
            f"{pointer}.sun",
        )
        sun_direction = None
        if sun is not None:
            sun_direction = self._vector(
                sun.get("direction_toward_scene"),
                3,
                f"{pointer}.sun.direction_toward_scene",
            )
            self._number(
                sun.get("illuminance_lux"),
                f"{pointer}.sun.illuminance_lux",
                minimum=1.0,
                maximum=200000.0,
            )
            if sun.get("spectrum") != "d65":
                self.add(
                    "COMPOSITION_SUN",
                    f"{pointer}.sun.spectrum",
                    "v1 showcase sun requires D65",
                )
            if sun_direction is not None and (
                abs(_dot3(sun_direction, sun_direction) - 1.0) > 1e-6
                or sun_direction[1] >= -1e-4
            ):
                self.add(
                    "COMPOSITION_SUN_DIRECTION",
                    f"{pointer}.sun.direction_toward_scene",
                    "sun direction must be normalized and point toward the receiver plane",
                )

        world = self._record(
            descriptor.get("world_aabb"),
            ("maximum_m", "minimum_m"),
            f"{pointer}.world_aabb",
        )
        world_minimum = world_maximum = None
        if world is not None:
            world_minimum = self._vector(
                world.get("minimum_m"), 3, f"{pointer}.world_aabb.minimum_m"
            )
            world_maximum = self._vector(
                world.get("maximum_m"), 3, f"{pointer}.world_aabb.maximum_m"
            )
        if glb_meshes:
            actual_minimum = tuple(
                min(float(mesh["bounds_min"][axis]) for mesh in glb_meshes.values())
                for axis in range(3)
            )
            actual_maximum = tuple(
                max(float(mesh["bounds_max"][axis]) for mesh in glb_meshes.values())
                for axis in range(3)
            )
            if world_minimum != actual_minimum or world_maximum != actual_maximum:
                self.add(
                    "COMPOSITION_WORLD_BOUNDS",
                    f"{pointer}.world_aabb",
                    "composition world AABB must exactly match decoded GLB bounds",
                )

        roi = self._record(
            descriptor.get("shadow_roi"),
            (
                "maximum_xz_m",
                "minimum_xz_m",
                "receiver_surface_y_m",
                "required_intersections",
            ),
            f"{pointer}.shadow_roi",
        )
        roi_minimum = roi_maximum = None
        receiver_y = None
        if roi is not None:
            roi_minimum = self._vector(
                roi.get("minimum_xz_m"), 2, f"{pointer}.shadow_roi.minimum_xz_m"
            )
            roi_maximum = self._vector(
                roi.get("maximum_xz_m"), 2, f"{pointer}.shadow_roi.maximum_xz_m"
            )
            receiver_y = self._number(
                roi.get("receiver_surface_y_m"),
                f"{pointer}.shadow_roi.receiver_surface_y_m",
            )
            intersections = roi.get("required_intersections")
            required = [
                "rorng_a0_road_surface_mesh",
                "rorng_a0_wet_asphalt_mesh",
            ]
            if intersections != required:
                self.add(
                    "COMPOSITION_SHADOW_INTERSECTIONS",
                    f"{pointer}.shadow_roi.required_intersections",
                    "shadow ROI must explicitly cover the dry and wet receivers",
                )
            if roi_minimum is not None and roi_maximum is not None:
                if any(
                    roi_minimum[axis] >= roi_maximum[axis] for axis in range(2)
                ):
                    self.add(
                        "COMPOSITION_SHADOW_ROI",
                        f"{pointer}.shadow_roi",
                        "shadow ROI must have positive XZ area",
                    )
                for logical_id in required:
                    mesh = glb_meshes.get(logical_id)
                    if mesh is None or not (
                        roi_minimum[0] < float(mesh["bounds_max"][0])
                        and roi_maximum[0] > float(mesh["bounds_min"][0])
                        and roi_minimum[1] < float(mesh["bounds_max"][2])
                        and roi_maximum[1] > float(mesh["bounds_min"][2])
                    ):
                        self.add(
                            "COMPOSITION_SHADOW_INTERSECTION",
                            f"{pointer}.shadow_roi",
                            f"shadow ROI does not intersect {logical_id}",
                        )

        gate = glb_meshes.get("rorng_a0_road_shadow_gate_mesh")
        if (
            gate is not None
            and sun_direction is not None
            and receiver_y is not None
            and roi_minimum is not None
            and roi_maximum is not None
        ):
            projected: list[tuple[float, float]] = []
            for x in (float(gate["bounds_min"][0]), float(gate["bounds_max"][0])):
                for y in (float(gate["bounds_min"][1]), float(gate["bounds_max"][1])):
                    for z in (float(gate["bounds_min"][2]), float(gate["bounds_max"][2])):
                        parameter = (receiver_y - y) / sun_direction[1]
                        projected.append(
                            (
                                x + parameter * sun_direction[0],
                                z + parameter * sun_direction[2],
                            )
                        )
            expected_minimum = tuple(min(value[axis] for value in projected) for axis in range(2))
            expected_maximum = tuple(max(value[axis] for value in projected) for axis in range(2))
            if any(
                abs(roi_minimum[axis] - expected_minimum[axis]) > 1e-5
                or abs(roi_maximum[axis] - expected_maximum[axis]) > 1e-5
                for axis in range(2)
            ):
                self.add(
                    "COMPOSITION_SHADOW_GEOMETRY",
                    f"{pointer}.shadow_roi",
                    "shadow ROI must be the exact gate projection along the normalized sun direction",
                )

        if (
            camera_position is not None
            and camera_target is not None
            and camera_up is not None
            and near_clip is not None
            and far_clip is not None
            and vertical_fov is not None
            and world_minimum is not None
            and world_maximum is not None
            and isinstance(preview_width, int)
            and isinstance(preview_height, int)
            and preview_width > 0
            and preview_height > 0
        ):
            forward = _normalized3(
                tuple(camera_target[axis] - camera_position[axis] for axis in range(3))
            )
            right = (
                _normalized3(_cross3(forward, camera_up))
                if forward is not None
                else None
            )
            up = _cross3(right, forward) if right is not None and forward is not None else None
            if forward is None or right is None or up is None or abs(_dot3(camera_up, camera_up) - 1.0) > 1e-6:
                self.add(
                    "COMPOSITION_CAMERA_BASIS",
                    f"{pointer}.camera",
                    "camera target/up do not form a canonical view basis",
                )
            else:
                tangent = math.tan(math.radians(vertical_fov) * 0.5)
                aspect = preview_width / preview_height
                for corner_index, corner in enumerate(
                    (
                        (x, y, z)
                        for x in (world_minimum[0], world_maximum[0])
                        for y in (world_minimum[1], world_maximum[1])
                        for z in (world_minimum[2], world_maximum[2])
                    )
                ):
                    relative = tuple(corner[axis] - camera_position[axis] for axis in range(3))
                    depth = _dot3(relative, forward)
                    ndc_x = _dot3(relative, right) / (depth * tangent * aspect) if depth > 0.0 else math.inf
                    ndc_y = _dot3(relative, up) / (depth * tangent) if depth > 0.0 else math.inf
                    if not near_clip <= depth <= far_clip or abs(ndc_x) > 1.0 or abs(ndc_y) > 1.0:
                        self.add(
                            "COMPOSITION_CAMERA_FRAMING",
                            f"{pointer}.camera",
                            f"world AABB corner {corner_index} is outside the checked camera frustum",
                        )
                        break

    def validate_claims(self) -> None:
        assert self.manifest is not None
        claims = self._record(
            self.manifest.get("claims"),
            ("ambient_occlusion", "collision", "lods", "native_terrain", "visual_only"),
            "$.claims",
        )
        if claims is None:
            return
        required = {
            "ambient_occlusion": False,
            "collision": False,
            "lods": False,
            "native_terrain": False,
            "visual_only": True,
        }
        for field, expected in required.items():
            if claims.get(field) is not expected:
                self.add(
                    "V1_SCOPE_CLAIM",
                    f"$.claims.{field}",
                    f"v1 fixture requires {field}={str(expected).lower()}",
                )

    def validate_outputs(self) -> None:
        assert self.manifest is not None
        outputs = self._record(
            self.manifest.get("outputs"),
            ("package_path", "report_path"),
            "$.outputs",
        )
        if outputs is None:
            return
        for field, suffix in (("package_path", ".rornative"), ("report_path", ".compile.json")):
            relative = safe_relative_path(outputs.get(field))
            if relative is None or not relative.endswith(suffix):
                self.add("OUTPUT_PATH_INVALID", f"$.outputs.{field}", f"expected a portable {suffix} path")
                continue
            try:
                lexical_path = self.repo_root
                for component in PurePosixPath(relative).parts:
                    lexical_path = lexical_path / component
                    if lexical_path.is_symlink():
                        raise ValueError("output path traverses a symlink")
                resolve_beneath(self.repo_root, relative)
            except ValueError:
                self.add("OUTPUT_PATH_ESCAPE", f"$.outputs.{field}", "output path escapes repository root")

    def validate_samplers(self) -> dict[str, dict[str, Any]]:
        assert self.manifest is not None
        values = self._array(self.manifest.get("samplers"), "$.samplers")
        result: dict[str, dict[str, Any]] = {}
        if values is None:
            return result
        previous = ""
        keys = (
            "address_u",
            "address_v",
            "address_w",
            "anisotropy_enabled",
            "border_color",
            "compare_enabled",
            "compare_operation",
            "id",
            "magnification_filter",
            "maximum_anisotropy",
            "maximum_lod",
            "minimum_lod",
            "minification_filter",
            "mip_filter",
            "mip_lod_bias",
        )
        for index, value in enumerate(values):
            pointer = f"$.samplers[{index}]"
            sampler = self._record(value, keys, pointer)
            if sampler is None:
                continue
            identifier = self._identifier(sampler.get("id"), f"{pointer}.id")
            if identifier is None:
                continue
            if identifier <= previous:
                self.add("ORDER_INVALID", f"{pointer}.id", "sampler IDs must be strictly sorted")
            previous = identifier
            if identifier in result:
                self.add("IDENTIFIER_DUPLICATE", f"{pointer}.id", "duplicate sampler ID")
                continue
            for field in ("minification_filter", "magnification_filter", "mip_filter"):
                self._enum(sampler.get(field), SAMPLER_FILTERS, f"{pointer}.{field}")
            for field in ("address_u", "address_v", "address_w"):
                self._enum(sampler.get(field), SAMPLER_ADDRESS_MODES, f"{pointer}.{field}")
            for field in ("anisotropy_enabled", "compare_enabled"):
                if not isinstance(sampler.get(field), bool):
                    self.add("FIELD_TYPE", f"{pointer}.{field}", "field must be boolean")
            self._enum(
                sampler.get("compare_operation"),
                SAMPLER_COMPARE_OPERATIONS,
                f"{pointer}.compare_operation",
            )
            lod_bias = self._number(sampler.get("mip_lod_bias"), f"{pointer}.mip_lod_bias", minimum=-16.0, maximum=16.0)
            minimum_lod = self._number(sampler.get("minimum_lod"), f"{pointer}.minimum_lod", minimum=0.0, maximum=32.0)
            maximum_lod = self._number(sampler.get("maximum_lod"), f"{pointer}.maximum_lod", minimum=0.0, maximum=32.0)
            anisotropy = self._number(sampler.get("maximum_anisotropy"), f"{pointer}.maximum_anisotropy", minimum=1.0, maximum=16.0)
            self._vector(sampler.get("border_color"), 4, f"{pointer}.border_color", minimum=0.0, maximum=1.0)
            if minimum_lod is not None and maximum_lod is not None and minimum_lod > maximum_lod:
                self.add("SAMPLER_LOD_RANGE", pointer, "minimum_lod exceeds maximum_lod")
            if sampler.get("anisotropy_enabled") is False and anisotropy != 1.0:
                self.add("SAMPLER_ANISOTROPY", pointer, "disabled anisotropy requires maximum 1")
            if sampler.get("compare_enabled") is False and sampler.get("compare_operation") != "always":
                self.add("SAMPLER_COMPARE", pointer, "disabled comparison requires operation always")
            _ = lod_bias
            result[identifier] = sampler
        self.stats["samplers"] = len(result)
        return result

    def validate_textures(self) -> dict[str, dict[str, Any]]:
        assert self.manifest is not None
        values = self._array(self.manifest.get("textures"), "$.textures")
        result: dict[str, dict[str, Any]] = {}
        if values is None:
            return result
        previous = ""
        declared_texture_bytes = 0
        declared_source_bytes = 0
        pending_mips: dict[
            str, list[tuple[dict[str, Any], str, int, int]]
        ] = {}

        # Structural pass. Compute the complete declared source plus decoded
        # working set before opening even the first texture file.
        for index, value in enumerate(values):
            pointer = f"$.textures[{index}]"
            texture = self._record(value, ("color_space", "format", "id", "mips", "role"), pointer)
            if texture is None:
                continue
            identifier = self._identifier(texture.get("id"), f"{pointer}.id")
            if identifier is None:
                continue
            if identifier <= previous:
                self.add("ORDER_INVALID", f"{pointer}.id", "texture IDs must be strictly sorted")
            previous = identifier
            if identifier in result:
                self.add("IDENTIFIER_DUPLICATE", f"{pointer}.id", "duplicate texture ID")
                continue
            role = texture.get("role")
            if role not in TEXTURE_ROLES:
                self.add("TEXTURE_ROLE", f"{pointer}.role", "unsupported texture role")
            expected_space = "srgb" if role in SRGB_TEXTURE_ROLES else "linear"
            if texture.get("color_space") != expected_space:
                self.add("TEXTURE_COLOR_SPACE", f"{pointer}.color_space", f"{role} requires {expected_space}")
            if texture.get("format") != "rgba8_unorm":
                self.add("TEXTURE_FORMAT", f"{pointer}.format", "v1 accepts rgba8_unorm only")
            mips = self._array(texture.get("mips"), f"{pointer}.mips", maximum=15)
            texture_mips: list[tuple[dict[str, Any], str, int, int]] = []
            previous_width = 0
            previous_height = 0
            if mips is not None:
                for mip_index, mip_value in enumerate(mips):
                    mip_pointer = f"{pointer}.mips[{mip_index}]"
                    mip = self._record(mip_value, ("height", "path", "sha256", "width"), mip_pointer)
                    if mip is None:
                        continue
                    width_value = mip.get("width")
                    height_value = mip.get("height")
                    if isinstance(width_value, bool) or not isinstance(width_value, int) or not 1 <= width_value <= MAX_TEXTURE_DIMENSION:
                        self.add("TEXTURE_DIMENSIONS", f"{mip_pointer}.width", "invalid mip width")
                        continue
                    if isinstance(height_value, bool) or not isinstance(height_value, int) or not 1 <= height_value <= MAX_TEXTURE_DIMENSION:
                        self.add("TEXTURE_DIMENSIONS", f"{mip_pointer}.height", "invalid mip height")
                        continue
                    if mip_index and (
                        width_value != max(1, previous_width // 2)
                        or height_value != max(1, previous_height // 2)
                    ):
                        self.add("TEXTURE_MIP_CHAIN", mip_pointer, "mip dimensions are not the next complete level")
                    previous_width, previous_height = width_value, height_value
                    rgba_bytes = width_value * height_value * 4
                    declared_texture_bytes += rgba_bytes
                    declared_source_bytes += 18 + rgba_bytes
                    texture_mips.append((mip, mip_pointer, width_value, height_value))
                if previous_width != 1 or previous_height != 1:
                    self.add("TEXTURE_MIP_CHAIN", f"{pointer}.mips", "mip chain must end at 1x1")
            pending_mips[identifier] = texture_mips
            result[identifier] = texture

        working_set_bytes = declared_source_bytes + declared_texture_bytes
        if declared_texture_bytes > MAX_TEXTURE_BYTES:
            self.add("LIMIT_EXCEEDED", "$.textures", "decoded texture bytes exceed v1 limit")
        if working_set_bytes > MAX_TEXTURE_WORKING_SET_BYTES:
            self.add(
                "TEXTURE_WORKING_SET_EXCEEDED",
                "$.textures",
                "declared TGA source plus decoded RGBA bytes exceed the v1 working-set limit",
            )
        if (
            declared_texture_bytes <= MAX_TEXTURE_BYTES
            and working_set_bytes <= MAX_TEXTURE_WORKING_SET_BYTES
        ):
            for identifier, mip_records in pending_mips.items():
                images: list[TgaImage] = []
                for mip, mip_pointer, width_value, height_value in mip_records:
                    rgba_bytes = width_value * height_value * 4
                    path = self._source_path(
                        {"path": mip.get("path"), "sha256": mip.get("sha256")},
                        mip_pointer,
                        maximum=18 + rgba_bytes,
                    )
                    if path is None:
                        continue
                    try:
                        image = decode_tga_rgba(
                            self.source_bytes.pop(path), width_value, height_value
                        )
                        images.append(image)
                        self.stats["texture_bytes"] += len(image.rgba)
                    except (MemoryError, OSError, ValueError):
                        self.add(
                            "TEXTURE_SOURCE_INVALID",
                            mip_pointer,
                            "texture source could not be decoded within the v1 profile",
                        )
                if images and len(images) == len(mip_records):
                    self.texture_images[identifier] = tuple(images)
        self.stats["textures"] = len(result)
        return result

    def _validate_texture_binding(
        self,
        binding: Any,
        slot: str,
        pointer: str,
        textures: dict[str, dict[str, Any]],
        samplers: dict[str, dict[str, Any]],
    ) -> None:
        record = self._record(
            binding,
            ("offset", "rotation_radians", "sampler", "scale", "texture", "uv_set"),
            pointer,
        )
        if record is None:
            return
        texture_id = self._identifier(record.get("texture"), f"{pointer}.texture")
        sampler_id = self._identifier(record.get("sampler"), f"{pointer}.sampler")
        if texture_id not in textures:
            self.add("REFERENCE_MISSING", f"{pointer}.texture", "unknown texture ID")
        elif textures[texture_id].get("role") != slot:
            self.add("TEXTURE_ROLE_MISMATCH", f"{pointer}.texture", f"texture role must be {slot}")
        if sampler_id not in samplers:
            self.add("REFERENCE_MISSING", f"{pointer}.sampler", "unknown sampler ID")
        if record.get("uv_set") != 0:
            self.add("UV_SET_UNSUPPORTED", f"{pointer}.uv_set", "v1 supports TEXCOORD_0 only")
        scale = self._vector(record.get("scale"), 2, f"{pointer}.scale")
        if scale is not None and any(component == 0.0 for component in scale):
            self.add(
                "TEXTURE_SCALE_INVALID",
                f"{pointer}.scale",
                "texture scale components must be nonzero after binary32 rounding",
            )
        self._vector(record.get("offset"), 2, f"{pointer}.offset")
        self._number(record.get("rotation_radians"), f"{pointer}.rotation_radians")

    def validate_materials(
        self,
        textures: dict[str, dict[str, Any]],
        samplers: dict[str, dict[str, Any]],
    ) -> dict[str, dict[str, Any]]:
        assert self.manifest is not None
        values = self._array(self.manifest.get("materials"), "$.materials")
        result: dict[str, dict[str, Any]] = {}
        if values is None:
            return result
        keys = (
            "alpha_cutoff",
            "alpha_test_mode",
            "base_color_factor",
            "base_color_transfer",
            "blend_mode",
            "depth_write",
            "double_sided",
            "emissive_factor",
            "emissive_strength",
            "id",
            "index_of_refraction",
            "metallic_factor",
            "model",
            "normal_scale",
            "occlusion_strength",
            "roughness_factor",
            "specular_factor",
            "textures",
            "workflow",
        )
        if self.source_format == SOURCE_FORMAT_V2:
            keys = (
                *keys,
                "attenuation_color",
                "attenuation_distance_m",
                "slab_thickness_m",
                "transmission_factor",
                "transmission_mode",
            )
        previous = ""
        for index, value in enumerate(values):
            pointer = f"$.materials[{index}]"
            material = self._record(value, keys, pointer)
            if material is None:
                continue
            identifier = self._identifier(material.get("id"), f"{pointer}.id")
            if identifier is None:
                continue
            if identifier <= previous:
                self.add("ORDER_INVALID", f"{pointer}.id", "material IDs must be strictly sorted")
            previous = identifier
            if identifier in result:
                self.add("IDENTIFIER_DUPLICATE", f"{pointer}.id", "duplicate material ID")
                continue
            model = self._enum(material.get("model"), MATERIAL_MODELS, f"{pointer}.model")
            workflow = self._enum(material.get("workflow"), MATERIAL_WORKFLOWS, f"{pointer}.workflow")
            self._enum(material.get("blend_mode"), MATERIAL_BLEND_MODES, f"{pointer}.blend_mode")
            alpha_test = self._enum(material.get("alpha_test_mode"), MATERIAL_ALPHA_TEST_MODES, f"{pointer}.alpha_test_mode")
            self._enum(material.get("base_color_transfer"), BASE_COLOR_TRANSFERS, f"{pointer}.base_color_transfer")
            for field in ("depth_write", "double_sided"):
                if not isinstance(material.get(field), bool):
                    self.add("FIELD_TYPE", f"{pointer}.{field}", "field must be boolean")
            self._vector(material.get("base_color_factor"), 4, f"{pointer}.base_color_factor", minimum=0.0, maximum=1.0)
            metallic = self._number(material.get("metallic_factor"), f"{pointer}.metallic_factor", minimum=0.0, maximum=1.0)
            self._number(material.get("roughness_factor"), f"{pointer}.roughness_factor", minimum=0.0, maximum=1.0)
            specular = self._vector(material.get("specular_factor"), 3, f"{pointer}.specular_factor", minimum=0.0, maximum=1.0)
            self._number(material.get("normal_scale"), f"{pointer}.normal_scale", minimum=0.0)
            self._number(material.get("occlusion_strength"), f"{pointer}.occlusion_strength", minimum=0.0, maximum=1.0)
            self._vector(material.get("emissive_factor"), 3, f"{pointer}.emissive_factor", minimum=0.0)
            self._number(material.get("emissive_strength"), f"{pointer}.emissive_strength", minimum=0.0)
            self._number(material.get("alpha_cutoff"), f"{pointer}.alpha_cutoff", minimum=0.0, maximum=1.0)
            self._number(material.get("index_of_refraction"), f"{pointer}.index_of_refraction", minimum=1.0, maximum=3.0)
            if self.source_format == SOURCE_FORMAT_V2:
                transmission_mode = self._enum(
                    material.get("transmission_mode"),
                    MATERIAL_TRANSMISSION_MODES,
                    f"{pointer}.transmission_mode",
                )
                transmission_factor = self._number(
                    material.get("transmission_factor"),
                    f"{pointer}.transmission_factor", minimum=0.0,
                    maximum=1.0,
                )
                attenuation_color = self._vector(
                    material.get("attenuation_color"), 3,
                    f"{pointer}.attenuation_color", minimum=0.0,
                    maximum=1.0,
                )
                attenuation_distance = self._number(
                    material.get("attenuation_distance_m"),
                    f"{pointer}.attenuation_distance_m", minimum=0.0,
                )
                slab_thickness = self._number(
                    material.get("slab_thickness_m"),
                    f"{pointer}.slab_thickness_m", minimum=0.0,
                )
                if transmission_mode == "none":
                    if (transmission_factor != 0.0 or
                            attenuation_color != (1.0, 1.0, 1.0) or
                            attenuation_distance != 1.0 or
                            slab_thickness != 0.0):
                        self.add(
                            "MATERIAL_TRANSMISSION", pointer,
                            "absent transmission requires canonical factor/color/distance/thickness",
                        )
                elif transmission_mode == "thin_parallel_slab":
                    if (model != "pbr_metallic_roughness" or
                            workflow != "specular" or
                            material.get("blend_mode") != "replace" or
                            alpha_test != "disabled" or
                            material.get("double_sided") is not False or
                            material.get("depth_write") is not False or
                            transmission_factor is None or
                            transmission_factor <= 0.0 or
                            attenuation_distance is None or
                            attenuation_distance <= 0.0 or
                            slab_thickness is None or slab_thickness <= 0.0 or
                            not isinstance(material.get("index_of_refraction"), (int, float)) or
                            material.get("index_of_refraction") <= 1.0):
                        self.add(
                            "MATERIAL_TRANSMISSION", pointer,
                            "thin slab requires single-sided specular PBR, replace/disabled alpha, no depth write, IOR above one, and positive factor/distance/thickness",
                        )
            bindings = material.get("textures")
            if not isinstance(bindings, dict):
                self.add("FIELD_TYPE", f"{pointer}.textures", "textures must be an object")
                bindings = {}
            for unknown in sorted(set(bindings) - set(MATERIAL_TEXTURE_SLOTS)):
                self.add("FIELD_UNKNOWN", f"{pointer}.textures.{unknown}", "unknown material texture slot")
            for slot in MATERIAL_TEXTURE_SLOTS:
                if slot in bindings:
                    self._validate_texture_binding(
                        bindings[slot], slot, f"{pointer}.textures.{slot}", textures, samplers
                    )
            if model == "unlit" and workflow != "metallic_roughness":
                self.add("MATERIAL_MODEL", pointer, "unlit requires the canonical metallic_roughness workflow token")
            if workflow == "metallic_roughness" and "specular" in bindings:
                self.add("MATERIAL_WORKFLOW", f"{pointer}.textures.specular", "metallic_roughness cannot bind specular")
            if workflow == "metallic_roughness" and specular != (1.0, 1.0, 1.0):
                self.add(
                    "MATERIAL_WORKFLOW",
                    f"{pointer}.specular_factor",
                    "metallic_roughness requires canonical unused specular factor [1,1,1]",
                )
            if workflow == "specular" and "metallic_roughness" in bindings:
                self.add("MATERIAL_WORKFLOW", f"{pointer}.textures.metallic_roughness", "specular workflow cannot bind metallic_roughness")
            if workflow == "specular" and metallic != 0.0:
                self.add(
                    "MATERIAL_WORKFLOW",
                    f"{pointer}.metallic_factor",
                    "specular workflow requires canonical unused metallic factor 0",
                )
            if alpha_test != "disabled" and "base_color" not in bindings:
                self.add("MATERIAL_ALPHA", pointer, "alpha testing requires a base-color texture")
            result[identifier] = material
        self.stats["materials"] = len(result)
        return result

    def _glb_chunks(self, data: bytes) -> tuple[bytes, bytes, bytes]:
        if len(data) < 28:
            raise ValueError("GLB is truncated")
        magic, version, declared = struct.unpack_from("<4sII", data, 0)
        if magic != b"glTF" or version != 2 or declared != len(data):
            raise ValueError("GLB header is invalid")
        offset = 12
        chunks: list[tuple[int, bytes]] = []
        while offset < len(data):
            if offset + 8 > len(data):
                raise ValueError("GLB chunk header is truncated")
            length, kind = struct.unpack_from("<II", data, offset)
            offset += 8
            if length % 4 or offset + length > len(data):
                raise ValueError("GLB chunk length is invalid")
            chunks.append((kind, data[offset : offset + length]))
            offset += length
        if [kind for kind, _payload in chunks] != [0x4E4F534A, 0x004E4942]:
            raise ValueError("GLB must contain exactly JSON then BIN")
        return data, chunks[0][1], chunks[1][1]

    def validate_glb(self, materials: dict[str, dict[str, Any]]) -> dict[str, dict[str, Any]]:
        if self.glb_path is None:
            return {}
        try:
            _data, json_bytes, binary = self._glb_chunks(
                self.source_bytes[self.glb_path]
            )
            document = json.loads(
                json_bytes.rstrip(b" \t\r\n\x00").decode("utf-8"),
                object_pairs_hook=reject_duplicate_keys,
                parse_constant=lambda token: (_ for _ in ()).throw(
                    ValueError(f"non-finite JSON number: {token}")
                ),
            )
            if not isinstance(document, dict):
                raise ValueError("GLB JSON root must be an object")
            self.glb = Glb(document, binary)
        except (OSError, ValueError, struct.error) as error:
            self.add("GLB_INVALID", "$.source.glb", str(error))
            return {}
        document = self.glb.document
        expected_json = canonical_json(document).encode("ascii")
        if json_bytes[: len(expected_json)] != expected_json or any(
            value != 0x20 for value in json_bytes[len(expected_json) :]
        ):
            self.add("GLB_JSON_NONCANONICAL", "$.source.glb", "GLB JSON is not canonical key-sorted ASCII")
        top_keys = {
            "accessors",
            "asset",
            "bufferViews",
            "buffers",
            "materials",
            "meshes",
            "nodes",
            "scene",
            "scenes",
        }
        if set(document) != top_keys:
            self.add("GLB_DOCUMENT_PROFILE", "$.source.glb", "GLB top-level fields do not match v1")
        asset = document.get("asset")
        if not _exact_keys(asset, ("generator", "version")) or asset.get("version") != "2.0" or not _is_canonical_ascii(asset.get("generator"), maximum=255):
            self.add("GLB_ASSET_PROFILE", "$.source.glb.asset", "invalid canonical glTF asset record")
        nodes = document.get("nodes")
        meshes = document.get("meshes")
        glb_materials = document.get("materials")
        scenes = document.get("scenes")
        buffers = document.get("buffers")
        views = document.get("bufferViews")
        accessors = document.get("accessors")
        arrays = (nodes, meshes, glb_materials, scenes, buffers, views, accessors)
        if any(not isinstance(value, list) for value in arrays):
            self.add("GLB_DOCUMENT_PROFILE", "$.source.glb", "required glTF arrays are missing")
            return {}
        assert isinstance(nodes, list)
        assert isinstance(meshes, list)
        assert isinstance(glb_materials, list)
        assert isinstance(scenes, list)
        assert isinstance(buffers, list)
        assert isinstance(views, list)
        assert isinstance(accessors, list)
        if not 1 <= len(nodes) <= MAX_GLTF_NODES or len(meshes) != len(nodes):
            self.add("GLB_NODE_COUNT", "$.source.glb.nodes", "v1 requires one mesh per bounded node")
            return {}
        expected_accessor_count = len(nodes) * 5
        if (
            len(accessors) != expected_accessor_count
            or len(views) != expected_accessor_count
        ):
            self.add(
                "GLB_ACCESSOR_COUNT",
                "$.source.glb.accessors",
                "v1 requires exactly five dedicated accessors and bufferViews per mesh",
            )
            return {}
        if document.get("scene") != 0 or len(scenes) != 1 or not _exact_keys(scenes[0], ("nodes",)) or scenes[0].get("nodes") != list(range(len(nodes))):
            self.add("GLB_SCENE_PROFILE", "$.source.glb.scenes", "every node must appear once in the one default scene")
        if len(buffers) != 1 or not _exact_keys(buffers[0], ("byteLength",)):
            self.add("GLB_BUFFER_PROFILE", "$.source.glb.buffers", "v1 requires one embedded buffer")
            return {}
        buffer_bytes = buffers[0].get("byteLength")
        if isinstance(buffer_bytes, bool) or not isinstance(buffer_bytes, int) or buffer_bytes < 0 or buffer_bytes > len(binary) or len(binary) - buffer_bytes > 3 or any(binary[buffer_bytes:]):
            self.add("GLB_BUFFER_PROFILE", "$.source.glb.buffers[0]", "buffer length/padding is invalid")
            return {}
        material_names: list[str] = []
        for index, material in enumerate(glb_materials):
            if not _exact_keys(material, ("name",)):
                self.add("GLB_MATERIAL_PROFILE", f"$.source.glb.materials[{index}]", "GLB materials carry names only")
                continue
            name = material.get("name")
            material_names.append(name if isinstance(name, str) else "")
            if name not in materials:
                self.add("GLB_MATERIAL_REFERENCE", f"$.source.glb.materials[{index}].name", "material is absent from native declaration")
        if material_names != sorted(material_names) or len(set(material_names)) != len(material_names):
            self.add("GLB_MATERIAL_ORDER", "$.source.glb.materials", "material names must be unique and sorted")

        usage: dict[int, tuple[str, int]] = {}
        mesh_records: dict[str, dict[str, Any]] = {}
        total_vertices = 0
        total_indices = 0
        union_min = [math.inf, math.inf, math.inf]
        union_max = [-math.inf, -math.inf, -math.inf]
        node_names: list[str] = []
        mesh_names: list[str] = []
        expected_attributes = {
            "NORMAL": ("VEC3", 5126),
            "POSITION": ("VEC3", 5126),
            "TANGENT": ("VEC4", 5126),
            "TEXCOORD_0": ("VEC2", 5126),
        }
        for index, (node, mesh) in enumerate(zip(nodes, meshes)):
            node_pointer = f"$.source.glb.nodes[{index}]"
            mesh_pointer = f"$.source.glb.meshes[{index}]"
            if not _exact_keys(node, ("mesh", "name")) or node.get("mesh") != index:
                self.add("GLB_NODE_PROFILE", node_pointer, "node must name the same-index mesh with no transform")
                continue
            if not _exact_keys(mesh, ("name", "primitives")):
                self.add("GLB_MESH_PROFILE", mesh_pointer, "mesh record is not canonical")
                continue
            node_name = node.get("name")
            mesh_name = mesh.get("name")
            node_names.append(node_name if isinstance(node_name, str) else "")
            mesh_names.append(mesh_name if isinstance(mesh_name, str) else "")
            if node_name != mesh_name or self._identifier(node_name, f"{node_pointer}.name") is None:
                self.add("GLB_NODE_NAME", node_pointer, "node and mesh require one matching rorng_ name")
                continue
            primitives = mesh.get("primitives")
            if not isinstance(primitives, list) or len(primitives) != 1:
                self.add("GLB_PRIMITIVE_COUNT", f"{mesh_pointer}.primitives", "v1 requires one primitive per mesh")
                continue
            primitive = primitives[0]
            if not _exact_keys(primitive, ("attributes", "indices", "material", "mode")) or primitive.get("mode") != 4:
                self.add("GLB_PRIMITIVE_PROFILE", f"{mesh_pointer}.primitives[0]", "primitive must be indexed TRIANGLES")
                continue
            attributes = primitive.get("attributes")
            if not isinstance(attributes, dict) or set(attributes) != set(expected_attributes):
                self.add("GLB_VERTEX_PROFILE", f"{mesh_pointer}.primitives[0].attributes", "exact POSITION/NORMAL/TANGENT/TEXCOORD_0 streams are required")
                continue
            indices_accessor = primitive.get("indices")
            material_index = primitive.get("material")
            if isinstance(material_index, bool) or not isinstance(material_index, int) or not 0 <= material_index < len(glb_materials):
                self.add("GLB_MATERIAL_REFERENCE", f"{mesh_pointer}.primitives[0].material", "material index is invalid")
                continue
            accessor_indexes = [attributes[name] for name in sorted(attributes)] + [indices_accessor]
            if any(isinstance(item, bool) or not isinstance(item, int) or not 0 <= item < len(accessors) for item in accessor_indexes):
                self.add("GLB_ACCESSOR_REFERENCE", mesh_pointer, "primitive accessor reference is invalid")
                continue
            accessor_profile_valid = True
            for semantic, accessor_index in attributes.items():
                if accessor_index in usage:
                    self.add("GLB_ACCESSOR_REUSE", mesh_pointer, "accessor is referenced more than once")
                usage[accessor_index] = (semantic, index)
                accessor = accessors[accessor_index]
                expected_type, expected_component = expected_attributes[semantic]
                allowed_keys = {"bufferView", "componentType", "count", "type"}
                if semantic == "POSITION":
                    allowed_keys |= {"min", "max"}
                if not isinstance(accessor, dict) or set(accessor) != allowed_keys or accessor.get("type") != expected_type or accessor.get("componentType") != expected_component:
                    self.add("GLB_ACCESSOR_PROFILE", f"$.source.glb.accessors[{accessor_index}]", f"invalid {semantic} accessor")
                    accessor_profile_valid = False
                count = accessor.get("count") if isinstance(accessor, dict) else None
                view_index = accessor.get("bufferView") if isinstance(accessor, dict) else None
                if (
                    isinstance(count, bool)
                    or not isinstance(count, int)
                    or not 1 <= count <= MAX_VERTICES
                    or isinstance(view_index, bool)
                    or not isinstance(view_index, int)
                    or not 0 <= view_index < len(views)
                ):
                    self.add(
                        "GLB_ACCESSOR_BOUNDS",
                        f"$.source.glb.accessors[{accessor_index}]",
                        "vertex accessor count/view is outside the bounded v1 profile",
                    )
                    accessor_profile_valid = False
            if indices_accessor in usage:
                self.add("GLB_ACCESSOR_REUSE", mesh_pointer, "index accessor is referenced more than once")
            usage[indices_accessor] = ("INDICES", index)
            index_record = accessors[indices_accessor]
            if not isinstance(index_record, dict) or set(index_record) != {"bufferView", "componentType", "count", "type"} or index_record.get("type") != "SCALAR" or index_record.get("componentType") not in (5123, 5125):
                self.add("GLB_ACCESSOR_PROFILE", f"$.source.glb.accessors[{indices_accessor}]", "indices must be packed uint16 or uint32")
                continue
            index_count = index_record.get("count")
            index_view = index_record.get("bufferView")
            if (
                isinstance(index_count, bool)
                or not isinstance(index_count, int)
                or not 1 <= index_count <= MAX_INDICES
                or isinstance(index_view, bool)
                or not isinstance(index_view, int)
                or not 0 <= index_view < len(views)
            ):
                self.add(
                    "GLB_ACCESSOR_BOUNDS",
                    f"$.source.glb.accessors[{indices_accessor}]",
                    "index accessor count/view is outside the bounded v1 profile",
                )
                continue
            if not accessor_profile_valid:
                continue
            try:
                positions = self.glb.accessor(attributes["POSITION"])
                normals = self.glb.accessor(attributes["NORMAL"])
                tangents = self.glb.accessor(attributes["TANGENT"])
                texcoords = self.glb.accessor(attributes["TEXCOORD_0"])
                indices_values = self.glb.accessor(indices_accessor)
            except (ValueError, struct.error) as error:
                self.add("GLB_ACCESSOR_INVALID", mesh_pointer, str(error))
                continue
            vertex_count = len(positions)
            if not vertex_count or any(len(stream) != vertex_count for stream in (normals, tangents, texcoords)):
                self.add("GLB_VERTEX_COUNT", mesh_pointer, "vertex streams have inconsistent or empty counts")
                continue
            if not indices_values or len(indices_values) % 3:
                self.add("GLB_INDEX_COUNT", mesh_pointer, "indices must contain complete triangles")
                continue
            finite_streams = True
            for vertex_index, (position, normal, tangent, texcoord) in enumerate(zip(positions, normals, tangents, texcoords)):
                if not (
                    isinstance(position, tuple) and len(position) == 3
                    and isinstance(normal, tuple) and len(normal) == 3
                    and isinstance(tangent, tuple) and len(tangent) == 4
                    and isinstance(texcoord, tuple) and len(texcoord) == 2
                    and all(math.isfinite(float(component)) for values in (position, normal, tangent, texcoord) for component in values)
                ):
                    self.add("GLB_VERTEX_NONFINITE", f"{mesh_pointer}.vertex[{vertex_index}]", "vertex stream is malformed or non-finite")
                    finite_streams = False
                    break
                if any(
                    _has_negative_zero(stream)
                    for stream in (position, normal, tangent, texcoord)
                ):
                    self.add(
                        "GLB_VERTEX_NEGATIVE_ZERO",
                        f"{mesh_pointer}.vertex[{vertex_index}]",
                        "vertex streams must not contain negative binary32 zero",
                    )
                    finite_streams = False
                    break
                normal_length = _double_length3(normal)
                tangent_length = _double_length3(tangent[:3])
                orthogonality = abs(
                    _binary32_dot3(
                        tuple(float(component) for component in normal),
                        tuple(float(component) for component in tangent[:3]),
                    )
                )
                if abs(normal_length - 1.0) > 1e-5 or abs(tangent_length - 1.0) > 1e-5 or orthogonality > 1e-5 or float(tangent[3]) not in (-1.0, 1.0):
                    self.add("GLB_TANGENT_BASIS", f"{mesh_pointer}.vertex[{vertex_index}]", "normal/tangent basis is not canonical")
                    finite_streams = False
                    break
            if not finite_streams:
                continue
            integer_indices = [int(value) for value in indices_values]
            if any(value < 0 or value >= vertex_count for value in integer_indices):
                self.add("GLB_INDEX_RANGE", mesh_pointer, "index addresses a missing vertex")
                continue
            for triangle_index in range(0, len(integer_indices), 3):
                a, b, c = integer_indices[triangle_index : triangle_index + 3]
                if len({a, b, c}) != 3:
                    self.add("GLB_TRIANGLE_DEGENERATE", mesh_pointer, "triangle repeats a vertex")
                    break
                first = positions[a]
                second = positions[b]
                third = positions[c]
                ab = tuple(
                    _binary32_subtract(float(second[axis]), float(first[axis]))
                    for axis in range(3)
                )
                ac = tuple(
                    _binary32_subtract(float(third[axis]), float(first[axis]))
                    for axis in range(3)
                )
                face_normal = _binary32_normalized3(
                    _binary32_cross3(ab, ac)
                )
                if face_normal is None:
                    self.add("GLB_TRIANGLE_DEGENERATE", mesh_pointer, "triangle has zero area")
                    break
                if any(
                    _binary32_dot3(
                        face_normal,
                        tuple(float(component) for component in normals[vertex]),
                    )
                    <= 0.0
                    for vertex in (a, b, c)
                ):
                    self.add(
                        "GLB_TRIANGLE_WINDING",
                        f"{mesh_pointer}.triangle[{triangle_index // 3}]",
                        "geometric face normal must point into every authored vertex-normal hemisphere",
                    )
                    break
                uv0, uv1, uv2 = texcoords[a], texcoords[b], texcoords[c]
                du1 = _binary32_subtract(float(uv1[0]), float(uv0[0]))
                dv1 = _binary32_subtract(float(uv1[1]), float(uv0[1]))
                du2 = _binary32_subtract(float(uv2[0]), float(uv0[0]))
                dv2 = _binary32_subtract(float(uv2[1]), float(uv0[1]))
                uv_determinant = _binary32_subtract(
                    _binary32_multiply(du1, dv2),
                    _binary32_multiply(dv1, du2),
                )
                if not math.isfinite(uv_determinant) or abs(uv_determinant) <= _binary32(1e-12):
                    self.add(
                        "GLB_UV_DEGENERATE",
                        f"{mesh_pointer}.triangle[{triangle_index // 3}]",
                        "triangle UV derivatives are singular",
                    )
                    break
                reciprocal = _binary32(1.0 / uv_determinant)
                u_direction = tuple(
                    _binary32_multiply(
                        _binary32_subtract(
                            _binary32_multiply(ab[axis], dv2),
                            _binary32_multiply(ac[axis], dv1),
                        ),
                        reciprocal,
                    )
                    for axis in range(3)
                )
                v_direction = tuple(
                    _binary32_multiply(
                        _binary32_subtract(
                            _binary32_multiply(ac[axis], du1),
                            _binary32_multiply(ab[axis], du2),
                        ),
                        reciprocal,
                    )
                    for axis in range(3)
                )
                tangent_failed = False
                for vertex in (a, b, c):
                    normal = tuple(float(component) for component in normals[vertex])
                    actual_tangent = tuple(
                        float(component) for component in tangents[vertex][:3]
                    )
                    tangent_projection = _binary32_dot3(normal, u_direction)
                    projected_u = tuple(
                        _binary32_subtract(
                            u_direction[axis],
                            _binary32_multiply(normal[axis], tangent_projection),
                        )
                        for axis in range(3)
                    )
                    expected_tangent = _binary32_normalized3(projected_u)
                    if (
                        expected_tangent is None
                        or _binary32_dot3(expected_tangent, actual_tangent)
                        < _binary32(0.9999)
                    ):
                        self.add(
                            "GLB_TANGENT_U_DIRECTION",
                            f"{mesh_pointer}.triangle[{triangle_index // 3}]",
                            "authored tangent does not follow increasing TEXCOORD_0 U",
                        )
                        tangent_failed = True
                        break
                    bitangent_projection = _binary32_dot3(normal, v_direction)
                    projected_v = tuple(
                        _binary32_subtract(
                            v_direction[axis],
                            _binary32_multiply(normal[axis], bitangent_projection),
                        )
                        for axis in range(3)
                    )
                    expected_bitangent = _binary32_normalized3(projected_v)
                    reconstructed_bitangent = tuple(
                        _binary32_multiply(float(tangents[vertex][3]), component)
                        for component in _binary32_cross3(normal, actual_tangent)
                    )
                    if (
                        expected_bitangent is None
                        or _binary32_dot3(
                            expected_bitangent, reconstructed_bitangent
                        )
                        < _binary32(0.9999)
                    ):
                        self.add(
                            "GLB_TANGENT_HANDEDNESS",
                            f"{mesh_pointer}.triangle[{triangle_index // 3}]",
                            "tangent w does not reconstruct B=w*cross(N,T) toward increasing V",
                        )
                        tangent_failed = True
                        break
                if tangent_failed:
                    break
            minimum = [min(float(position[axis]) for position in positions) for axis in range(3)]
            maximum = [max(float(position[axis]) for position in positions) for axis in range(3)]
            position_accessor = accessors[attributes["POSITION"]]
            if position_accessor.get("min") != minimum or position_accessor.get("max") != maximum:
                self.add("GLB_POSITION_BOUNDS", f"$.source.glb.accessors[{attributes['POSITION']}]", "POSITION min/max are not exact")
            for axis in range(3):
                union_min[axis] = min(union_min[axis], minimum[axis])
                union_max[axis] = max(union_max[axis], maximum[axis])
            total_vertices += vertex_count
            total_indices += len(integer_indices)
            material_name = material_names[material_index] if material_index < len(material_names) else ""
            mesh_records[node_name] = {
                "attributes": attributes,
                "indices": indices_accessor,
                "material": material_name,
                "positions": positions,
                "normals": normals,
                "tangents": tangents,
                "texcoords": texcoords,
                "index_values": integer_indices,
                "bounds_min": minimum,
                "bounds_max": maximum,
            }
        if node_names != sorted(node_names) or len(set(node_names)) != len(node_names) or mesh_names != node_names:
            self.add("GLB_NODE_ORDER", "$.source.glb.nodes", "matching node/mesh names must be unique and sorted")
        if set(usage) != set(range(len(accessors))) or len(views) != len(accessors):
            self.add("GLB_ACCESSOR_CLOSURE", "$.source.glb.accessors", "every accessor and bufferView must be used exactly once")
        cursor = 0
        component_sizes = {5123: 2, 5125: 4, 5126: 4}
        widths = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}
        for index, (view, accessor) in enumerate(zip(views, accessors)):
            semantic = usage.get(index, ("", 0))[0]
            component = accessor.get("componentType") if isinstance(accessor, dict) else None
            value_type = accessor.get("type") if isinstance(accessor, dict) else None
            count = accessor.get("count") if isinstance(accessor, dict) else None
            expected_length = (
                component_sizes.get(component, 0)
                * widths.get(value_type, 0)
                * (count if isinstance(count, int) and not isinstance(count, bool) else 0)
            )
            expected_target = 34963 if semantic == "INDICES" else 34962
            expected_offset = _align4(cursor)
            if expected_offset > cursor and any(binary[cursor:expected_offset]):
                self.add("GLB_BUFFER_PADDING", f"$.source.glb.bufferViews[{index}]", "inter-view padding must be zero")
            accessor_view = accessor.get("bufferView") if isinstance(accessor, dict) else None
            if not _exact_keys(view, ("buffer", "byteLength", "byteOffset", "target")) or view.get("buffer") != 0 or view.get("byteOffset") != expected_offset or view.get("byteLength") != expected_length or view.get("target") != expected_target or accessor_view != index:
                self.add("GLB_BUFFER_VIEW", f"$.source.glb.bufferViews[{index}]", "bufferView/accessor layout is not canonical")
            cursor = expected_offset + expected_length
        if buffer_bytes != cursor:
            self.add("GLB_BUFFER_CLOSURE", "$.source.glb.buffers[0].byteLength", "buffer has unused or missing bytes")
        if total_vertices > MAX_VERTICES or total_indices > MAX_INDICES:
            self.add("LIMIT_EXCEEDED", "$.source.glb", "mesh data exceeds v1 limits")
        package = self.manifest.get("package") if self.manifest is not None else {}
        declared_dimensions = package.get("dimensions_m") if isinstance(package, dict) else None
        rounded_dimensions = (
            tuple(_float32_round_trip(value)[0] for value in declared_dimensions)
            if isinstance(declared_dimensions, list)
            and len(declared_dimensions) == 3
            else ()
        )
        if rounded_dimensions and all(value is not None for value in rounded_dimensions) and mesh_records:
            actual = [union_max[axis] - union_min[axis] for axis in range(3)]
            if any(abs(actual[axis] - float(rounded_dimensions[axis])) > 1e-6 for axis in range(3)):
                self.add("DIMENSIONS_MISMATCH", "$.package.dimensions_m", "declared dimensions do not match GLB bounds")
        self.stats["meshes"] = len(mesh_records)
        self.stats["vertices"] = total_vertices
        self.stats["indices"] = total_indices
        self.stats["triangles"] = total_indices // 3
        return mesh_records

    def validate_mesh_declarations(
        self,
        glb_meshes: dict[str, dict[str, Any]],
        materials: dict[str, dict[str, Any]],
    ) -> None:
        assert self.manifest is not None
        values = self._array(self.manifest.get("meshes"), "$.meshes", maximum=MAX_GLTF_NODES)
        if values is None:
            return
        previous = ""
        declared_nodes: set[str] = set()
        object_ids: set[str] = set()
        for index, value in enumerate(values):
            pointer = f"$.meshes[{index}]"
            mesh = self._record(
                value,
                (
                    "id",
                    "instance_flags",
                    "material",
                    "node",
                    "object_id",
                    "render_from_object",
                ),
                pointer,
            )
            if mesh is None:
                continue
            identifier = self._identifier(mesh.get("id"), f"{pointer}.id")
            node = self._identifier(mesh.get("node"), f"{pointer}.node")
            material = self._identifier(mesh.get("material"), f"{pointer}.material")
            object_id = self._identifier(mesh.get("object_id"), f"{pointer}.object_id")
            if identifier is not None:
                if identifier <= previous:
                    self.add("ORDER_INVALID", f"{pointer}.id", "mesh IDs must be strictly sorted")
                previous = identifier
            if node in declared_nodes:
                self.add("IDENTIFIER_DUPLICATE", f"{pointer}.node", "GLB node is declared twice")
            if node is not None:
                declared_nodes.add(node)
            if object_id in object_ids:
                self.add("IDENTIFIER_DUPLICATE", f"{pointer}.object_id", "object ID is duplicated")
            if object_id is not None:
                object_ids.add(object_id)
            if node not in glb_meshes:
                self.add("REFERENCE_MISSING", f"{pointer}.node", "unknown GLB node")
            elif glb_meshes[node].get("material") != material:
                self.add("MATERIAL_MISMATCH", f"{pointer}.material", "declaration disagrees with GLB primitive")
            if material not in materials:
                self.add("REFERENCE_MISSING", f"{pointer}.material", "unknown material ID")
            flags = mesh.get("instance_flags")
            if not isinstance(flags, list):
                self.add(
                    "FIELD_TYPE",
                    f"{pointer}.instance_flags",
                    "instance flags must be an array",
                )
            elif any(
                not isinstance(flag, str) or flag not in INSTANCE_FLAGS
                for flag in flags
            ) or flags != sorted(flags) or len(flags) != len(set(flags)):
                self.add(
                    "INSTANCE_FLAGS_INVALID",
                    f"{pointer}.instance_flags",
                    "instance flags must be unique supported tokens in sorted order",
                )
            matrix = self._vector(mesh.get("render_from_object"), 16, f"{pointer}.render_from_object")
            if matrix is not None:
                if not _is_binary32_canonical_affine(matrix):
                    self.add("TRANSFORM_INVALID", f"{pointer}.render_from_object", "matrix must be canonical affine column-major")
                elif not _has_binary32_invertible_affine_transform(matrix):
                    self.add("TRANSFORM_INVALID", f"{pointer}.render_from_object", "matrix linear transform is not invertible")
            if identifier is not None and node is not None:
                self.mesh_sources[identifier] = {"declaration": mesh, "glb": glb_meshes.get(node)}
        if declared_nodes != set(glb_meshes):
            self.add("GLB_NODE_CLOSURE", "$.meshes", "every GLB node must be declared exactly once")
        self.stats["instances"] = len(object_ids)

    def validate(self) -> dict[str, Any]:
        self.load_manifest()
        if self.manifest is not None:
            asset_total = sum(
                len(value) if isinstance(value, list) else 0
                for value in (
                    self.manifest.get("meshes"),
                    self.manifest.get("textures"),
                    self.manifest.get("materials"),
                    self.manifest.get("samplers"),
                )
            )
            if asset_total > MAX_ASSET_COUNT:
                self.add(
                    "ASSET_LIMIT_EXCEEDED",
                    "$",
                    f"aggregate asset declarations exceed {MAX_ASSET_COUNT}",
                )
            self.validate_package_record()
            self.validate_source_record()
            self.validate_claims()
            self.validate_outputs()
            samplers = self.validate_samplers()
            textures = self.validate_textures()
            materials = self.validate_materials(textures, samplers)
            glb_meshes = self.validate_glb(materials)
            self.validate_composition(glb_meshes)
            self.validate_mesh_declarations(glb_meshes, materials)
        diagnostics = sorted(
            (item.as_dict() for item in self.diagnostics),
            key=lambda item: (item["code"], item["path"], item["message"]),
        )
        return {
            "diagnostics": diagnostics,
            "format": REPORT_FORMAT,
            "source_format": self.source_format,
            "summary": {
                **self.stats,
                "diagnostic_count": len(diagnostics),
                "valid": not diagnostics,
            },
        }


def _arguments(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--repo-root", type=Path, default=Path.cwd())
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = _arguments(sys.argv[1:] if argv is None else argv)
    validator = NativeRenderAssetValidator(args.repo_root, args.manifest)
    report = validator.validate()
    print(json.dumps(report, ensure_ascii=True, sort_keys=True, separators=(",", ":")))
    return 0 if report["summary"]["valid"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
