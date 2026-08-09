#!/usr/bin/env python3
"""Compile strict authored OGRE 14 material semantics into RORMAT2 bytes.

The compiler intentionally performs no material-name, texture-filename,
texture-unit-position, lighting, or specular inference. Every emitted byte is
an exact authored declaration validated below and by the C++ parser.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile
from typing import Any, Iterable, Mapping, NoReturn, Sequence


SCHEMA_ID = "ror.ogre14.material-semantic-catalog.v2"
MAGIC = b"RORMAT2\0"
FORMAT_VERSION = 2
HEADER_BYTES = 64
MAX_INPUT_BYTES = 64 * 1024 * 1024
MAX_RECORDS = 65_536
MAX_TEXTURE_UNITS = 32
MAX_TOTAL_STRING_BYTES = 16 * 1024 * 1024


class CatalogError(ValueError):
    """A fail-closed source-catalog validation error."""


def _fail(path: str, detail: str) -> NoReturn:
    raise CatalogError(f"{path}: {detail}")


def _strict_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            _fail(key, "duplicate JSON member")
        result[key] = value
    return result


def _reject_constant(value: str) -> NoReturn:
    _fail("json", f"non-finite constant {value!r} is forbidden")


def load_source(path: Path) -> Mapping[str, Any]:
    raw = path.read_bytes()
    if not raw or len(raw) > MAX_INPUT_BYTES:
        _fail("catalog", "source is empty or exceeds the 64 MiB cap")
    try:
        decoded = raw.decode("utf-8")
    except UnicodeDecodeError as error:
        raise CatalogError("catalog: source is not UTF-8") from error
    try:
        value = json.loads(
            decoded,
            object_pairs_hook=_strict_object,
            parse_constant=_reject_constant,
        )
    except json.JSONDecodeError as error:
        raise CatalogError(
            f"catalog: malformed JSON or trailing data at {error.pos}"
        ) from error
    if not isinstance(value, dict):
        _fail("catalog", "root must be an object")
    return value


def _object(value: Any, keys: Iterable[str], path: str) -> Mapping[str, Any]:
    if not isinstance(value, dict):
        _fail(path, "must be an object")
    expected = set(keys)
    actual = set(value)
    if actual != expected:
        missing = sorted(expected - actual)
        unknown = sorted(actual - expected)
        _fail(path, f"missing={missing!r} unknown={unknown!r}")
    return value


def _array(value: Any, path: str, *, maximum: int) -> Sequence[Any]:
    if not isinstance(value, list):
        _fail(path, "must be an array")
    if len(value) > maximum:
        _fail(path, f"contains more than {maximum} entries")
    return value


def _integer(value: Any, path: str, minimum: int, maximum: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        _fail(path, "must be an integer")
    if value < minimum or value > maximum:
        _fail(path, f"must be in [{minimum}, {maximum}]")
    return value


def _boolean(value: Any, path: str) -> bool:
    if not isinstance(value, bool):
        _fail(path, "must be a boolean")
    return value


def _string(value: Any, path: str, maximum_bytes: int) -> str:
    if not isinstance(value, str) or not value or "\0" in value:
        _fail(path, "must be a nonempty NUL-free string")
    try:
        encoded = value.encode("utf-8")
    except UnicodeEncodeError as error:
        raise CatalogError(f"{path}: must be valid UTF-8") from error
    if len(encoded) > maximum_bytes or len(encoded) > 0xFFFF:
        _fail(path, f"UTF-8 encoding exceeds {maximum_bytes} bytes")
    return value


def _enum(value: Any, values: Mapping[str, int], path: str) -> int:
    if not isinstance(value, str) or value not in values:
        _fail(path, f"must be one of {sorted(values)!r}")
    return values[value]


def _sha256(value: Any, path: str) -> bytes:
    if (
        not isinstance(value, str)
        or len(value) != 64
        or any(character not in "0123456789abcdef" for character in value)
    ):
        _fail(path, "must be exactly 64 lowercase hexadecimal characters")
    return bytes.fromhex(value)


def _f32_bits(value: Any, path: str) -> int:
    if (
        not isinstance(value, str)
        or len(value) != 8
        or any(character not in "0123456789abcdef" for character in value)
    ):
        _fail(path, "must be exactly 8 lowercase float32-bit hex characters")
    bits = int(value, 16)
    if bits & 0x7F800000 == 0x7F800000:
        _fail(path, "NaN and infinity float32 encodings are forbidden")
    return bits


def _packed_string(value: str) -> bytes:
    encoded = value.encode("utf-8")
    return struct.pack("<H", len(encoded)) + encoded


FILTER = {"NONE": 0, "POINT": 1, "LINEAR": 2, "ANISOTROPIC": 3}
ADDRESS = {"WRAP": 0, "MIRROR": 1, "CLAMP": 2, "BORDER": 3}
COMPARE = {
    "ALWAYS_FAIL": 0,
    "ALWAYS_PASS": 1,
    "LESS": 2,
    "LESS_EQUAL": 3,
    "EQUAL": 4,
    "NOT_EQUAL": 5,
    "GREATER_EQUAL": 6,
    "GREATER": 7,
}
BLEND_FACTOR = {
    "ONE": 0,
    "ZERO": 1,
    "DESTINATION_COLOR": 2,
    "SOURCE_COLOR": 3,
    "ONE_MINUS_DESTINATION_COLOR": 4,
    "ONE_MINUS_SOURCE_COLOR": 5,
    "DESTINATION_ALPHA": 6,
    "SOURCE_ALPHA": 7,
    "ONE_MINUS_DESTINATION_ALPHA": 8,
    "ONE_MINUS_SOURCE_ALPHA": 9,
}
BLEND_OPERATION = {
    "ADD": 0,
    "SUBTRACT": 1,
    "REVERSE_SUBTRACT": 2,
    "MINIMUM": 3,
    "MAXIMUM": 4,
}
CULL = {"NONE": 0, "CLOCKWISE": 1, "ANTICLOCKWISE": 2}
MANUAL_CULL = {"NONE": 0, "BACK": 1, "FRONT": 2}
RUNTIME_GENERATION = {
    "AUTHORED": 0,
    "REPAIRED_SCRIPT": 1,
    "RTSS_GENERATED": 2,
    "RUNTIME_LISTENER": 3,
}
BASE_COLOR_SEMANTIC = {"UNLIT": 0, "ROUGH_DIELECTRIC_PBR": 1}
COLOR_ROLE = {"BASE_COLOR_SRGB": 0, "LINEAR_DATA": 1}
TEXTURE_SEMANTIC = {
    "BASE_COLOR": 0,
    "NORMAL": 1,
    "METALLIC_ROUGHNESS": 2,
    "OCCLUSION": 3,
    "EMISSIVE": 4,
    "ENVIRONMENT": 5,
    "DETAIL": 6,
}
SWIZZLE = {"RED": 0, "GREEN": 1, "BLUE": 2, "ALPHA": 3, "ZERO": 4, "ONE": 5}
COMBINE_OPERATION = {
    "REPLACE": 0,
    "MODULATE": 1,
    "ADD": 2,
    "SUBTRACT": 3,
    "BLEND_TEXTURE_ALPHA": 4,
    "DOT_PRODUCT": 5,
}
COMBINE_SOURCE = {
    "TEXTURE": 0,
    "CURRENT": 1,
    "DIFFUSE": 2,
    "SPECULAR": 3,
    "MANUAL": 4,
}
ENVIRONMENT = {"NONE": 0, "REFLECTION_2D": 1, "CUBE_REFLECTION": 2, "CUBE_NORMAL": 3}
SHADOW_AUGMENTATION = {"NONE": 0, "RECEIVE": 1, "CAST": 2, "RECEIVE_AND_CAST": 3}
SHADOW_TECHNIQUE = {"NONE": 0, "STENCIL": 1, "PSSM": 2}


PASS_KEYS = (
    "source_color",
    "destination_color",
    "source_alpha",
    "destination_alpha",
    "color_operation",
    "alpha_operation",
    "color_write_mask",
    "depth_check_enabled",
    "depth_write_enabled",
    "depth_compare",
    "constant_depth_bias_f32_bits",
    "slope_scale_depth_bias_f32_bits",
    "iteration_depth_bias_f32_bits",
    "cull",
    "manual_cull",
    "alpha_reject",
    "alpha_reject_value",
    "alpha_to_coverage",
    "solid_fill",
    "pass_iteration_count",
)
SAMPLER_KEYS = (
    "minification",
    "magnification",
    "mip",
    "address_u",
    "address_v",
    "address_w",
    "mip_lod_bias_f32_bits",
    "minimum_lod_f32_bits",
    "maximum_lod_f32_bits",
    "maximum_anisotropy",
    "compare_enabled",
    "compare_operation",
    "border_color_f32_bits",
)
COMBINE_KEYS = (
    "color_operation",
    "color_source_one",
    "color_source_two",
    "alpha_operation",
    "alpha_source_one",
    "alpha_source_two",
    "color_manual_one_f32_bits",
    "color_manual_two_f32_bits",
    "color_manual_factor_f32_bits",
    "alpha_manual_one_f32_bits",
    "alpha_manual_two_f32_bits",
    "alpha_manual_factor_f32_bits",
)
UNIT_KEYS = (
    "ordinal",
    "exact_unit_name",
    "texture_resource_group",
    "exact_texture_name",
    "semantic",
    "color_role",
    "swizzle",
    "texture_coordinate_set",
    "projective",
    "uv_transform_f32_bits",
    "sampler",
    "combine",
)
RECORD_KEYS = (
    "package_archive_sha256",
    "resource_group",
    "resource_generation",
    "source_script_member",
    "source_script_sha256",
    "effective_script_sha256",
    "repair_plan_version",
    "material_name",
    "native_structure_sha256",
    "selected_scheme",
    "selected_lod",
    "runtime_generation",
    "base_color_semantic",
    "registry_texture_color_role",
    "lowering_algorithm",
    "lowering_version",
    "declaration_revision",
    "pass",
    "environment_augmentation",
    "environment_texture_unit",
    "shadow_augmentation",
    "shadow_technique",
    "texture_units",
)


class _CompilationBudget:
    def __init__(self) -> None:
        self.string_bytes = 0

    def string(self, value: Any, path: str, maximum: int) -> str:
        result = _string(value, path, maximum)
        self.string_bytes += len(result.encode("utf-8"))
        if self.string_bytes > MAX_TOTAL_STRING_BYTES:
            _fail(path, "aggregate strings exceed 16 MiB")
        return result


def _compile_pass(value: Any, path: str) -> bytes:
    facts = _object(value, PASS_KEYS, path)
    mask = _integer(facts["color_write_mask"], f"{path}.color_write_mask", 1, 15)
    iterations = _integer(facts["pass_iteration_count"], f"{path}.pass_iteration_count", 1, 0xFFFFFFFF)
    result = bytearray()
    for field in ("source_color", "destination_color", "source_alpha", "destination_alpha"):
        result.append(_enum(facts[field], BLEND_FACTOR, f"{path}.{field}"))
    result.append(_enum(facts["color_operation"], BLEND_OPERATION, f"{path}.color_operation"))
    result.append(_enum(facts["alpha_operation"], BLEND_OPERATION, f"{path}.alpha_operation"))
    result.append(mask)
    result.append(int(_boolean(facts["depth_check_enabled"], f"{path}.depth_check_enabled")))
    result.append(int(_boolean(facts["depth_write_enabled"], f"{path}.depth_write_enabled")))
    result.append(_enum(facts["depth_compare"], COMPARE, f"{path}.depth_compare"))
    for field in ("constant_depth_bias_f32_bits", "slope_scale_depth_bias_f32_bits", "iteration_depth_bias_f32_bits"):
        result += struct.pack("<I", _f32_bits(facts[field], f"{path}.{field}"))
    result.append(_enum(facts["cull"], CULL, f"{path}.cull"))
    result.append(_enum(facts["manual_cull"], MANUAL_CULL, f"{path}.manual_cull"))
    result.append(_enum(facts["alpha_reject"], COMPARE, f"{path}.alpha_reject"))
    result.append(_integer(facts["alpha_reject_value"], f"{path}.alpha_reject_value", 0, 255))
    result.append(int(_boolean(facts["alpha_to_coverage"], f"{path}.alpha_to_coverage")))
    result.append(int(_boolean(facts["solid_fill"], f"{path}.solid_fill")))
    result += struct.pack("<I", iterations)
    return bytes(result)


def _compile_sampler(value: Any, path: str) -> bytes:
    facts = _object(value, SAMPLER_KEYS, path)
    result = bytearray()
    for field in ("minification", "magnification", "mip"):
        result.append(_enum(facts[field], FILTER, f"{path}.{field}"))
    for field in ("address_u", "address_v", "address_w"):
        result.append(_enum(facts[field], ADDRESS, f"{path}.{field}"))
    lod_bits = []
    for field in ("mip_lod_bias_f32_bits", "minimum_lod_f32_bits", "maximum_lod_f32_bits"):
        bits = _f32_bits(facts[field], f"{path}.{field}")
        lod_bits.append(bits)
        result += struct.pack("<I", bits)
    minimum_lod = struct.unpack("<f", struct.pack("<I", lod_bits[1]))[0]
    maximum_lod = struct.unpack("<f", struct.pack("<I", lod_bits[2]))[0]
    if minimum_lod > maximum_lod:
        _fail(path, "minimum LOD exceeds maximum LOD")
    result += struct.pack("<I", _integer(facts["maximum_anisotropy"], f"{path}.maximum_anisotropy", 1, 16))
    result.append(int(_boolean(facts["compare_enabled"], f"{path}.compare_enabled")))
    result.append(_enum(facts["compare_operation"], COMPARE, f"{path}.compare_operation"))
    border = _array(facts["border_color_f32_bits"], f"{path}.border_color_f32_bits", maximum=4)
    if len(border) != 4:
        _fail(f"{path}.border_color_f32_bits", "must contain exactly four channels")
    for index, bits in enumerate(border):
        result += struct.pack("<I", _f32_bits(bits, f"{path}.border_color_f32_bits[{index}]"))
    return bytes(result)


def _compile_combine(value: Any, path: str) -> bytes:
    facts = _object(value, COMBINE_KEYS, path)
    result = bytearray(
        (
            _enum(facts["color_operation"], COMBINE_OPERATION, f"{path}.color_operation"),
            _enum(facts["color_source_one"], COMBINE_SOURCE, f"{path}.color_source_one"),
            _enum(facts["color_source_two"], COMBINE_SOURCE, f"{path}.color_source_two"),
            _enum(facts["alpha_operation"], COMBINE_OPERATION, f"{path}.alpha_operation"),
            _enum(facts["alpha_source_one"], COMBINE_SOURCE, f"{path}.alpha_source_one"),
            _enum(facts["alpha_source_two"], COMBINE_SOURCE, f"{path}.alpha_source_two"),
        )
    )
    for field in ("color_manual_one_f32_bits", "color_manual_two_f32_bits"):
        channels = _array(facts[field], f"{path}.{field}", maximum=4)
        if len(channels) != 4:
            _fail(f"{path}.{field}", "must contain exactly four channels")
        for index, bits in enumerate(channels):
            result += struct.pack("<I", _f32_bits(bits, f"{path}.{field}[{index}]"))
    for field in (
        "color_manual_factor_f32_bits",
        "alpha_manual_one_f32_bits",
        "alpha_manual_two_f32_bits",
        "alpha_manual_factor_f32_bits",
    ):
        result += struct.pack("<I", _f32_bits(facts[field], f"{path}.{field}"))
    return bytes(result)


def _compile_unit(value: Any, path: str, ordinal: int, budget: _CompilationBudget) -> tuple[bytes, int, int]:
    unit = _object(value, UNIT_KEYS, path)
    exact_ordinal = _integer(unit["ordinal"], f"{path}.ordinal", 0, 0xFFFE)
    if exact_ordinal != ordinal:
        _fail(f"{path}.ordinal", "texture units must be in exact zero-based order")
    unit_name = budget.string(unit["exact_unit_name"], f"{path}.exact_unit_name", 255)
    texture_group = budget.string(unit["texture_resource_group"], f"{path}.texture_resource_group", 255)
    texture_name = budget.string(unit["exact_texture_name"], f"{path}.exact_texture_name", 255)
    semantic = _enum(unit["semantic"], TEXTURE_SEMANTIC, f"{path}.semantic")
    color_role = _enum(unit["color_role"], COLOR_ROLE, f"{path}.color_role")
    swizzle = _array(unit["swizzle"], f"{path}.swizzle", maximum=4)
    if len(swizzle) != 4:
        _fail(f"{path}.swizzle", "must contain exactly four channels")
    uv = _array(unit["uv_transform_f32_bits"], f"{path}.uv_transform_f32_bits", maximum=9)
    if len(uv) != 9:
        _fail(f"{path}.uv_transform_f32_bits", "must contain exactly nine values")
    result = bytearray(struct.pack("<H", exact_ordinal))
    result += _packed_string(unit_name) + _packed_string(texture_group) + _packed_string(texture_name)
    result += bytes((semantic, color_role))
    result += bytes(_enum(channel, SWIZZLE, f"{path}.swizzle[{index}]") for index, channel in enumerate(swizzle))
    result.append(_integer(unit["texture_coordinate_set"], f"{path}.texture_coordinate_set", 0, 255))
    result.append(int(_boolean(unit["projective"], f"{path}.projective")))
    for index, bits in enumerate(uv):
        result += struct.pack("<I", _f32_bits(bits, f"{path}.uv_transform_f32_bits[{index}]"))
    result += _compile_sampler(unit["sampler"], f"{path}.sampler")
    result += _compile_combine(unit["combine"], f"{path}.combine")
    return bytes(result), semantic, color_role


def _compile_record(value: Any, index: int, budget: _CompilationBudget) -> tuple[tuple[bytes, bytes], bytes]:
    path = f"records[{index}]"
    record = _object(value, RECORD_KEYS, path)
    group = budget.string(record["resource_group"], f"{path}.resource_group", 255)
    material = budget.string(record["material_name"], f"{path}.material_name", 255)
    member = budget.string(record["source_script_member"], f"{path}.source_script_member", 4096)
    scheme = budget.string(record["selected_scheme"], f"{path}.selected_scheme", 255)
    lowering = budget.string(record["lowering_algorithm"], f"{path}.lowering_algorithm", 128)
    generation = _integer(record["resource_generation"], f"{path}.resource_generation", 1, 0xFFFFFFFFFFFFFFFE)
    repair_version = _integer(record["repair_plan_version"], f"{path}.repair_plan_version", 1, 0xFFFFFFFF)
    lowering_version = _integer(record["lowering_version"], f"{path}.lowering_version", 1, 0xFFFFFFFF)
    revision = _integer(record["declaration_revision"], f"{path}.declaration_revision", 1, 0xFFFFFFFFFFFFFFFE)
    source_script_sha256 = _sha256(record["source_script_sha256"], f"{path}.source_script_sha256")
    effective_script_sha256 = _sha256(record["effective_script_sha256"], f"{path}.effective_script_sha256")
    runtime_generation = _enum(record["runtime_generation"], RUNTIME_GENERATION, f"{path}.runtime_generation")
    if (
        runtime_generation == RUNTIME_GENERATION["AUTHORED"]
        and source_script_sha256 != effective_script_sha256
    ) or (
        runtime_generation == RUNTIME_GENERATION["REPAIRED_SCRIPT"]
        and source_script_sha256 == effective_script_sha256
    ):
        _fail(path, "runtime generation disagrees with exact script digests")
    registry_role = _enum(record["registry_texture_color_role"], COLOR_ROLE, f"{path}.registry_texture_color_role")
    units = _array(record["texture_units"], f"{path}.texture_units", maximum=MAX_TEXTURE_UNITS)
    compiled_units: list[bytes] = []
    base_color_count = 0
    environment_ordinals: set[int] = set()
    for unit_index, unit in enumerate(units):
        compiled, semantic, color_role = _compile_unit(unit, f"{path}.texture_units[{unit_index}]", unit_index, budget)
        if semantic == TEXTURE_SEMANTIC["BASE_COLOR"]:
            base_color_count += 1
            if color_role != registry_role:
                _fail(path, "base-color unit and registry texture color role disagree")
        if semantic == TEXTURE_SEMANTIC["ENVIRONMENT"]:
            environment_ordinals.add(unit_index)
        compiled_units.append(compiled)
    if base_color_count > 1:
        _fail(path, "v2 lowering permits at most one base-color unit")
    environment = _enum(record["environment_augmentation"], ENVIRONMENT, f"{path}.environment_augmentation")
    environment_unit_value = record["environment_texture_unit"]
    if environment == ENVIRONMENT["NONE"]:
        if environment_unit_value is not None:
            _fail(f"{path}.environment_texture_unit", "must be null when augmentation is NONE")
        environment_unit = 0xFFFF
    else:
        environment_unit = _integer(environment_unit_value, f"{path}.environment_texture_unit", 0, len(units) - 1)
        if environment_unit not in environment_ordinals:
            _fail(f"{path}.environment_texture_unit", "must reference an ENVIRONMENT semantic unit")
    shadow = _enum(record["shadow_augmentation"], SHADOW_AUGMENTATION, f"{path}.shadow_augmentation")
    shadow_technique = _enum(record["shadow_technique"], SHADOW_TECHNIQUE, f"{path}.shadow_technique")
    if (shadow == 0) != (shadow_technique == 0):
        _fail(path, "shadow augmentation and technique must both be NONE or both declared")

    result = bytearray(_sha256(record["package_archive_sha256"], f"{path}.package_archive_sha256"))
    result += _packed_string(group)
    result += struct.pack("<Q", generation)
    result += _packed_string(member)
    result += source_script_sha256
    result += effective_script_sha256
    result += struct.pack("<I", repair_version)
    result += _packed_string(material)
    result += _sha256(record["native_structure_sha256"], f"{path}.native_structure_sha256")
    result += _packed_string(scheme)
    result += struct.pack("<I", _integer(record["selected_lod"], f"{path}.selected_lod", 0, 0xFFFFFFFF))
    result.append(runtime_generation)
    result.append(_enum(record["base_color_semantic"], BASE_COLOR_SEMANTIC, f"{path}.base_color_semantic"))
    result.append(registry_role)
    result += _packed_string(lowering)
    result += struct.pack("<I", lowering_version)
    result += struct.pack("<Q", revision)
    result += _compile_pass(record["pass"], f"{path}.pass")
    result += bytes((environment,)) + struct.pack("<H", environment_unit)
    result += bytes((shadow, shadow_technique))
    result += struct.pack("<H", len(units))
    for compiled in compiled_units:
        result += compiled
    return (group.encode("utf-8"), material.encode("utf-8")), bytes(result)


def compile_document(document: Mapping[str, Any]) -> bytes:
    root = _object(document, ("schema", "records"), "catalog")
    if root["schema"] != SCHEMA_ID:
        _fail("catalog.schema", f"must equal {SCHEMA_ID!r}")
    records = _array(root["records"], "catalog.records", maximum=MAX_RECORDS)
    if not records:
        _fail("catalog.records", "must contain at least one explicit record")
    budget = _CompilationBudget()
    compiled = [_compile_record(record, index, budget) for index, record in enumerate(records)]
    compiled.sort(key=lambda item: item[0])
    for index in range(1, len(compiled)):
        if compiled[index - 1][0] == compiled[index][0]:
            group, material = compiled[index][0]
            _fail("catalog.records", f"duplicate exact key {(group, material)!r}")
    payload = b"".join(value for _, value in compiled)
    if len(payload) > MAX_INPUT_BYTES - HEADER_BYTES:
        _fail("catalog", "compiled payload exceeds the 64 MiB cap")
    digest = hashlib.sha256(payload).digest()
    header = struct.pack(
        "<8sHHIII32sQ",
        MAGIC,
        FORMAT_VERSION,
        HEADER_BYTES,
        0,
        len(compiled),
        len(payload),
        digest,
        0,
    )
    if len(header) != HEADER_BYTES:
        raise AssertionError("internal RORMAT2 header size mismatch")
    return header + payload


def compile_file(source: Path, output: Path) -> str:
    compiled = compile_document(load_source(source))
    output.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{output.name}.", suffix=".tmp", dir=output.parent
    )
    try:
        with os.fdopen(descriptor, "wb") as temporary:
            temporary.write(compiled)
            temporary.flush()
            os.fsync(temporary.fileno())
        os.replace(temporary_name, output)
    except BaseException:
        try:
            os.unlink(temporary_name)
        except FileNotFoundError:
            pass
        raise
    return hashlib.sha256(compiled[HEADER_BYTES:]).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    arguments = parser.parse_args()
    try:
        digest = compile_file(arguments.source, arguments.output)
    except (CatalogError, OSError) as error:
        parser.error(str(error))
    print(f"rormat2_sha256={digest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
