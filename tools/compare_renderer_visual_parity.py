#!/usr/bin/env python3
"""Compare two strictly profiled renderer PNG frames and emit a receipt.

This is deliberately dependency-free.  It implements only the narrow PNG and
metadata profiles documented in doc/nextgen/RENDERER_VISUAL_PARITY_ORACLE.md.
"""

from __future__ import annotations

import argparse
from array import array
import hashlib
import json
import math
import os
from pathlib import Path
import re
import struct
import sys
import tempfile
from typing import Any, Dict, Iterable, List, Mapping, Sequence, Tuple
import zlib


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
METADATA_SCHEMA = "ror.renderer_visual_parity_frame_metadata.v1"
RECEIPT_SCHEMA = "ror.renderer_visual_parity_receipt.v1"
METRIC_PROFILE = "ror.renderer_visual_parity_metrics.global_srgb_v1"

MAX_PNG_BYTES = 256 * 1024 * 1024
MAX_METADATA_BYTES = 1024 * 1024
MAX_DIMENSION = 16_384
MAX_PIXELS = 16_777_216
MAX_JSON_DEPTH = 16
MAX_JSON_CONTAINER_ITEMS = 4096
MAX_JSON_STRING_BYTES = 16_384

DEFAULT_THRESHOLDS = {
    "linear_rgb_mae_max": 0.03,
    "linear_rgb_rmse_max": 0.06,
    "luminance_global_ssim_min": 0.98,
    "sobel_edge_disagreement_mean_max": 0.05,
    "changed_pixel_fraction_max": 1.0,
}

_HEX_SHA256 = re.compile(r"^[0-9a-f]{64}$")
_PNG_CHUNK_TYPE = re.compile(rb"^[A-Za-z]{4}$")
_TOP_LEVEL_METADATA_KEYS = {
    "schema",
    "renderer",
    "content",
    "camera",
    "exposure",
    "weather",
    "resolution",
    "color_space",
    "ui_free",
}
_MATCHED_METADATA_KEYS = (
    "content",
    "camera",
    "exposure",
    "weather",
    "resolution",
    "color_space",
    "ui_free",
)


class ParityError(RuntimeError):
    """Raised when an input cannot enter the visual comparison contract."""


class DecodedPng:
    def __init__(
        self,
        *,
        width: int,
        height: int,
        color_type: int,
        rgb: bytes,
        source_has_alpha: bool,
        nonopaque_pixel_count: int,
        blank: bool,
    ) -> None:
        self.width = width
        self.height = height
        self.color_type = color_type
        self.rgb = rgb
        self.source_has_alpha = source_has_alpha
        self.nonopaque_pixel_count = nonopaque_pixel_count
        self.blank = blank


class StableSum:
    """Fixed-order Neumaier accumulation for bounded cross-platform drift."""

    def __init__(self) -> None:
        self._sum = 0.0
        self._correction = 0.0

    def add(self, value: float) -> None:
        candidate = self._sum + value
        if abs(self._sum) >= abs(value):
            self._correction += (self._sum - candidate) + value
        else:
            self._correction += (value - candidate) + self._sum
        self._sum = candidate

    def value(self) -> float:
        return self._sum + self._correction


def _read_bounded(path: Path, limit: int, label: str) -> bytes:
    try:
        size = path.stat().st_size
    except OSError as exc:
        raise ParityError(f"cannot stat {label}: {exc}") from exc
    if size <= 0:
        raise ParityError(f"{label} is empty")
    if size > limit:
        raise ParityError(f"{label} exceeds the {limit}-byte limit")
    try:
        payload = path.read_bytes()
    except OSError as exc:
        raise ParityError(f"cannot read {label}: {exc}") from exc
    if len(payload) != size:
        raise ParityError(f"{label} changed while it was read")
    return payload


def _sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _paeth(left: int, above: int, upper_left: int) -> int:
    estimate = left + above - upper_left
    left_distance = abs(estimate - left)
    above_distance = abs(estimate - above)
    upper_left_distance = abs(estimate - upper_left)
    if left_distance <= above_distance and left_distance <= upper_left_distance:
        return left
    if above_distance <= upper_left_distance:
        return above
    return upper_left


def _unfilter_scanline(
    filter_type: int,
    encoded: bytes,
    previous: bytearray,
    bytes_per_pixel: int,
) -> bytearray:
    decoded = bytearray(len(encoded))
    if filter_type == 0:
        decoded[:] = encoded
    elif filter_type == 1:
        for index, value in enumerate(encoded):
            left = decoded[index - bytes_per_pixel] if index >= bytes_per_pixel else 0
            decoded[index] = (value + left) & 0xFF
    elif filter_type == 2:
        for index, value in enumerate(encoded):
            decoded[index] = (value + previous[index]) & 0xFF
    elif filter_type == 3:
        for index, value in enumerate(encoded):
            left = decoded[index - bytes_per_pixel] if index >= bytes_per_pixel else 0
            decoded[index] = (value + ((left + previous[index]) // 2)) & 0xFF
    elif filter_type == 4:
        for index, value in enumerate(encoded):
            left = decoded[index - bytes_per_pixel] if index >= bytes_per_pixel else 0
            upper_left = previous[index - bytes_per_pixel] if index >= bytes_per_pixel else 0
            decoded[index] = (
                value + _paeth(left, previous[index], upper_left)
            ) & 0xFF
    else:
        raise ParityError(f"PNG uses unsupported scanline filter {filter_type}")
    return decoded


def decode_strict_png(payload: bytes, label: str) -> DecodedPng:
    if len(payload) > MAX_PNG_BYTES:
        raise ParityError(f"{label} exceeds the {MAX_PNG_BYTES}-byte PNG limit")
    if not payload.startswith(PNG_SIGNATURE):
        raise ParityError(f"{label} has an invalid PNG signature")

    offset = len(PNG_SIGNATURE)
    width = 0
    height = 0
    color_type = -1
    bytes_per_pixel = 0
    saw_ihdr = False
    saw_idat = False
    saw_iend = False
    idat_closed = False
    compressed_parts: List[bytes] = []
    compressed_bytes = 0

    while offset < len(payload):
        if len(payload) - offset < 12:
            raise ParityError(f"{label} has a truncated PNG chunk")
        length = struct.unpack(">I", payload[offset : offset + 4])[0]
        chunk_type = payload[offset + 4 : offset + 8]
        offset += 8
        if not _PNG_CHUNK_TYPE.fullmatch(chunk_type):
            raise ParityError(f"{label} has an invalid PNG chunk type")
        if 97 <= chunk_type[2] <= 122:
            raise ParityError(f"{label} sets the reserved PNG chunk-type bit")
        if length > len(payload) - offset - 4:
            raise ParityError(f"{label} has a truncated {chunk_type!r} chunk")
        chunk_data = payload[offset : offset + length]
        stored_crc = struct.unpack(">I", payload[offset + length : offset + length + 4])[0]
        computed_crc = zlib.crc32(chunk_type)
        computed_crc = zlib.crc32(chunk_data, computed_crc) & 0xFFFFFFFF
        if stored_crc != computed_crc:
            raise ParityError(f"{label} has a CRC mismatch in {chunk_type.decode('ascii')}")
        offset += length + 4

        if not saw_ihdr and chunk_type != b"IHDR":
            raise ParityError(f"{label} does not begin with IHDR")
        if saw_iend:
            raise ParityError(f"{label} has data after IEND")

        if chunk_type == b"IHDR":
            if saw_ihdr or length != 13:
                raise ParityError(f"{label} has an invalid or duplicate IHDR")
            (
                width,
                height,
                bit_depth,
                color_type,
                compression_method,
                filter_method,
                interlace_method,
            ) = struct.unpack(">IIBBBBB", chunk_data)
            if width < 3 or height < 3:
                raise ParityError(f"{label} dimensions must be at least 3x3")
            if width > MAX_DIMENSION or height > MAX_DIMENSION:
                raise ParityError(f"{label} exceeds the maximum PNG dimension")
            if width * height > MAX_PIXELS:
                raise ParityError(f"{label} exceeds the maximum decoded pixel count")
            if bit_depth != 8:
                raise ParityError(f"{label} is not an 8-bit PNG")
            if color_type not in (2, 6):
                raise ParityError(f"{label} is not RGB or RGBA")
            if compression_method != 0 or filter_method != 0:
                raise ParityError(f"{label} uses an unsupported PNG method")
            if interlace_method != 0:
                raise ParityError(f"{label} is interlaced")
            bytes_per_pixel = 3 if color_type == 2 else 4
            saw_ihdr = True
        elif chunk_type == b"IDAT":
            if not saw_ihdr or idat_closed or saw_iend or length == 0:
                raise ParityError(f"{label} has an invalid IDAT sequence")
            saw_idat = True
            compressed_bytes += length
            if compressed_bytes > MAX_PNG_BYTES:
                raise ParityError(f"{label} has excessive IDAT data")
            compressed_parts.append(chunk_data)
        elif chunk_type == b"IEND":
            if length != 0 or not saw_idat:
                raise ParityError(f"{label} has an invalid IEND")
            saw_iend = True
            idat_closed = True
            if offset != len(payload):
                raise ParityError(f"{label} has trailing bytes after IEND")
        else:
            # The initial profile deliberately rejects every ancillary chunk.
            # Color interpretation therefore comes only from the required
            # metadata color_space value, never from ambiguous PNG metadata.
            raise ParityError(
                f"{label} contains disallowed PNG chunk {chunk_type.decode('ascii')}"
            )

        if saw_idat and chunk_type != b"IDAT":
            idat_closed = True

    if not saw_ihdr or not saw_idat or not saw_iend:
        raise ParityError(f"{label} is missing a required PNG chunk")

    row_bytes = width * bytes_per_pixel
    expected_bytes = height * (row_bytes + 1)
    decompressor = zlib.decompressobj()
    try:
        inflated = decompressor.decompress(
            b"".join(compressed_parts), expected_bytes + 1
        )
    except zlib.error as exc:
        raise ParityError(f"{label} has invalid IDAT compression: {exc}") from exc
    if (
        len(inflated) != expected_bytes
        or not decompressor.eof
        or decompressor.unconsumed_tail
        or decompressor.unused_data
    ):
        raise ParityError(f"{label} has a non-canonical decoded byte count or stream")
    try:
        flushed = decompressor.flush()
    except zlib.error as exc:
        raise ParityError(f"{label} has invalid IDAT termination: {exc}") from exc
    if flushed:
        raise ParityError(f"{label} has buffered bytes after its exact image payload")

    rgb = bytearray(width * height * 3)
    previous = bytearray(row_bytes)
    source_offset = 0
    target_offset = 0
    nonopaque_pixel_count = 0
    first_rgb: Tuple[int, int, int] | None = None
    blank = True
    for _row in range(height):
        filter_type = inflated[source_offset]
        source_offset += 1
        encoded = inflated[source_offset : source_offset + row_bytes]
        source_offset += row_bytes
        decoded = _unfilter_scanline(
            filter_type, encoded, previous, bytes_per_pixel
        )
        for pixel_offset in range(0, row_bytes, bytes_per_pixel):
            red = decoded[pixel_offset]
            green = decoded[pixel_offset + 1]
            blue = decoded[pixel_offset + 2]
            current_rgb = (red, green, blue)
            if first_rgb is None:
                first_rgb = current_rgb
            elif current_rgb != first_rgb:
                blank = False
            rgb[target_offset : target_offset + 3] = bytes(current_rgb)
            target_offset += 3
            if bytes_per_pixel == 4 and decoded[pixel_offset + 3] != 255:
                nonopaque_pixel_count += 1
        previous = decoded

    if nonopaque_pixel_count:
        raise ParityError(
            f"{label} has {nonopaque_pixel_count} non-opaque alpha pixels"
        )
    if blank:
        raise ParityError(f"{label} is a blank constant-color frame")

    return DecodedPng(
        width=width,
        height=height,
        color_type=color_type,
        rgb=bytes(rgb),
        source_has_alpha=(color_type == 6),
        nonopaque_pixel_count=nonopaque_pixel_count,
        blank=blank,
    )


def _reject_json_constant(value: str) -> None:
    raise ParityError(f"metadata contains non-finite JSON number {value}")


def _object_without_duplicates(pairs: Iterable[Tuple[str, Any]]) -> Dict[str, Any]:
    result: Dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ParityError(f"metadata contains duplicate object key {key!r}")
        result[key] = value
    return result


def _validate_json_tree(value: Any, label: str, depth: int = 0) -> None:
    if depth > MAX_JSON_DEPTH:
        raise ParityError(f"{label} exceeds the metadata nesting limit")
    if value is None or isinstance(value, bool):
        return
    if isinstance(value, str):
        try:
            encoded = value.encode("utf-8")
        except UnicodeEncodeError as exc:
            raise ParityError(
                f"{label} contains a non-Unicode-scalar string"
            ) from exc
        if len(encoded) > MAX_JSON_STRING_BYTES:
            raise ParityError(f"{label} contains an oversized string")
        return
    if isinstance(value, int):
        if abs(value) > 9_007_199_254_740_991:
            raise ParityError(f"{label} integer is outside the exact JSON range")
        return
    if isinstance(value, float):
        if not math.isfinite(value):
            raise ParityError(f"{label} contains a non-finite number")
        return
    if isinstance(value, list):
        if len(value) > MAX_JSON_CONTAINER_ITEMS:
            raise ParityError(f"{label} contains an oversized array")
        for item in value:
            _validate_json_tree(item, label, depth + 1)
        return
    if isinstance(value, dict):
        if len(value) > MAX_JSON_CONTAINER_ITEMS:
            raise ParityError(f"{label} contains an oversized object")
        for key, item in value.items():
            if not isinstance(key, str):
                raise ParityError(f"{label} contains a non-string object key")
            _validate_json_tree(key, label, depth + 1)
            _validate_json_tree(item, label, depth + 1)
        return
    raise ParityError(f"{label} contains unsupported JSON value {type(value).__name__}")


def _require_nonempty_string(container: Mapping[str, Any], key: str, label: str) -> str:
    value = container.get(key)
    if not isinstance(value, str) or not value or value.strip() != value:
        raise ParityError(f"{label}.{key} must be a nonempty trimmed string")
    return value


def _require_finite_number(value: Any, label: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ParityError(f"{label} must be a finite JSON number")
    number = float(value)
    if not math.isfinite(number):
        raise ParityError(f"{label} must be finite")
    return number


def _validate_metadata_shape(metadata: Any, label: str) -> Dict[str, Any]:
    _validate_json_tree(metadata, label)
    if not isinstance(metadata, dict):
        raise ParityError(f"{label} root must be an object")
    if set(metadata) != _TOP_LEVEL_METADATA_KEYS:
        missing = sorted(_TOP_LEVEL_METADATA_KEYS - set(metadata))
        extra = sorted(set(metadata) - _TOP_LEVEL_METADATA_KEYS)
        raise ParityError(
            f"{label} top-level keys are invalid (missing={missing}, extra={extra})"
        )
    if metadata["schema"] != METADATA_SCHEMA:
        raise ParityError(f"{label}.schema is not {METADATA_SCHEMA}")
    if metadata["color_space"] != "srgb":
        raise ParityError(f"{label}.color_space must be 'srgb'")
    if metadata["ui_free"] is not True:
        raise ParityError(f"{label}.ui_free must be true")

    renderer = metadata["renderer"]
    if not isinstance(renderer, dict):
        raise ParityError(f"{label}.renderer must be an object")
    _require_nonempty_string(renderer, "name", f"{label}.renderer")
    _require_nonempty_string(renderer, "backend", f"{label}.renderer")
    renderer_sha = _require_nonempty_string(
        renderer, "build_sha256", f"{label}.renderer"
    )
    if not _HEX_SHA256.fullmatch(renderer_sha):
        raise ParityError(f"{label}.renderer.build_sha256 must be lowercase SHA-256")

    content = metadata["content"]
    if not isinstance(content, dict):
        raise ParityError(f"{label}.content must be an object")
    _require_nonempty_string(content, "scene", f"{label}.content")
    content_sha = _require_nonempty_string(
        content, "content_sha256", f"{label}.content"
    )
    if not _HEX_SHA256.fullmatch(content_sha):
        raise ParityError(f"{label}.content.content_sha256 must be lowercase SHA-256")

    camera = metadata["camera"]
    if not isinstance(camera, dict):
        raise ParityError(f"{label}.camera must be an object")
    if camera.get("projection") != "perspective":
        raise ParityError(f"{label}.camera.projection must be 'perspective'")
    position = camera.get("position")
    orientation = camera.get("orientation_xyzw")
    if not isinstance(position, list) or len(position) != 3:
        raise ParityError(f"{label}.camera.position must contain three numbers")
    if not isinstance(orientation, list) or len(orientation) != 4:
        raise ParityError(
            f"{label}.camera.orientation_xyzw must contain four numbers"
        )
    for index, value in enumerate(position):
        _require_finite_number(value, f"{label}.camera.position[{index}]")
    orientation_values = [
        _require_finite_number(value, f"{label}.camera.orientation_xyzw[{index}]")
        for index, value in enumerate(orientation)
    ]
    orientation_norm = math.sqrt(sum(value * value for value in orientation_values))
    if abs(orientation_norm - 1.0) > 1.0e-4:
        raise ParityError(f"{label}.camera.orientation_xyzw is not normalized")
    vertical_fov = _require_finite_number(
        camera.get("vertical_fov_degrees"),
        f"{label}.camera.vertical_fov_degrees",
    )
    near_clip = _require_finite_number(
        camera.get("near_clip"), f"{label}.camera.near_clip"
    )
    far_clip = _require_finite_number(
        camera.get("far_clip"), f"{label}.camera.far_clip"
    )
    if not (0.0 < vertical_fov < 180.0):
        raise ParityError(f"{label}.camera.vertical_fov_degrees is out of range")
    if not (0.0 < near_clip < far_clip):
        raise ParityError(f"{label}.camera clip planes are invalid")

    for object_key in ("exposure", "weather"):
        value = metadata[object_key]
        if not isinstance(value, dict) or not value:
            raise ParityError(f"{label}.{object_key} must be a nonempty object")

    resolution = metadata["resolution"]
    if not isinstance(resolution, dict) or set(resolution) != {"width", "height"}:
        raise ParityError(f"{label}.resolution must contain only width and height")
    for dimension in ("width", "height"):
        value = resolution[dimension]
        if isinstance(value, bool) or not isinstance(value, int) or value < 3:
            raise ParityError(f"{label}.resolution.{dimension} is invalid")
        if value > MAX_DIMENSION:
            raise ParityError(f"{label}.resolution.{dimension} is too large")
    return metadata


def parse_metadata(payload: bytes, label: str) -> Dict[str, Any]:
    if len(payload) > MAX_METADATA_BYTES:
        raise ParityError(f"{label} exceeds the metadata size limit")
    try:
        text = payload.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise ParityError(f"{label} is not UTF-8") from exc
    try:
        metadata = json.loads(
            text,
            object_pairs_hook=_object_without_duplicates,
            parse_constant=_reject_json_constant,
        )
    except ParityError:
        raise
    except (json.JSONDecodeError, RecursionError, ValueError) as exc:
        raise ParityError(f"{label} is invalid JSON: {exc}") from exc
    return _validate_metadata_shape(metadata, label)


def _require_matching_metadata(
    reference: Mapping[str, Any], candidate: Mapping[str, Any]
) -> Dict[str, Any]:
    matched: Dict[str, Any] = {}
    for key in _MATCHED_METADATA_KEYS:
        # Python considers True == 1 and 1 == 1.0.  The capture contract is
        # stricter: JSON types and numeric representations are semantic input,
        # so compare their canonical serializations instead.
        if _canonical_json_bytes(reference[key]) != _canonical_json_bytes(
            candidate[key]
        ):
            raise ParityError(f"reference and candidate metadata mismatch at {key}")
        matched[key] = reference[key]
    return matched


def _srgb_to_linear_table() -> Tuple[float, ...]:
    values: List[float] = []
    for encoded in range(256):
        srgb = encoded / 255.0
        if srgb <= 0.04045:
            linear = srgb / 12.92
        else:
            linear = ((srgb + 0.055) / 1.055) ** 2.4
        values.append(linear)
    return tuple(values)


_SRGB_TO_LINEAR = _srgb_to_linear_table()


def _finite_metric(value: float) -> float:
    if not math.isfinite(value):
        raise ParityError("a derived metric is non-finite")
    return 0.0 if value == 0.0 else value


def _luminance_sobel(luminance: Sequence[float], width: int, x: int, y: int) -> Tuple[float, float]:
    top = (y - 1) * width
    middle = y * width
    bottom = (y + 1) * width
    top_left = luminance[top + x - 1]
    top_center = luminance[top + x]
    top_right = luminance[top + x + 1]
    middle_left = luminance[middle + x - 1]
    middle_right = luminance[middle + x + 1]
    bottom_left = luminance[bottom + x - 1]
    bottom_center = luminance[bottom + x]
    bottom_right = luminance[bottom + x + 1]
    gradient_x = (
        -top_left
        + top_right
        - (2.0 * middle_left)
        + (2.0 * middle_right)
        - bottom_left
        + bottom_right
    )
    gradient_y = (
        -top_left
        - (2.0 * top_center)
        - top_right
        + bottom_left
        + (2.0 * bottom_center)
        + bottom_right
    )
    return gradient_x, gradient_y


def compute_metrics(reference: DecodedPng, candidate: DecodedPng) -> Dict[str, Any]:
    if reference.width != candidate.width or reference.height != candidate.height:
        raise ParityError("reference and candidate PNG dimensions differ")
    pixel_count = reference.width * reference.height
    absolute_sums = [StableSum(), StableSum(), StableSum()]
    squared_sums = [StableSum(), StableSum(), StableSum()]
    aggregate_absolute = StableSum()
    aggregate_squared = StableSum()
    reference_luminance = array("d")
    candidate_luminance = array("d")
    changed_pixels = 0

    for offset in range(0, len(reference.rgb), 3):
        changed = False
        reference_linear = [0.0, 0.0, 0.0]
        candidate_linear = [0.0, 0.0, 0.0]
        for channel in range(3):
            reference_byte = reference.rgb[offset + channel]
            candidate_byte = candidate.rgb[offset + channel]
            if reference_byte != candidate_byte:
                changed = True
            reference_value = _SRGB_TO_LINEAR[reference_byte]
            candidate_value = _SRGB_TO_LINEAR[candidate_byte]
            reference_linear[channel] = reference_value
            candidate_linear[channel] = candidate_value
            difference = abs(reference_value - candidate_value)
            squared = difference * difference
            absolute_sums[channel].add(difference)
            squared_sums[channel].add(squared)
            aggregate_absolute.add(difference)
            aggregate_squared.add(squared)
        if changed:
            changed_pixels += 1
        reference_luminance.append(
            (0.2126 * reference_linear[0])
            + (0.7152 * reference_linear[1])
            + (0.0722 * reference_linear[2])
        )
        candidate_luminance.append(
            (0.2126 * candidate_linear[0])
            + (0.7152 * candidate_linear[1])
            + (0.0722 * candidate_linear[2])
        )

    reference_luminance_sum = StableSum()
    candidate_luminance_sum = StableSum()
    for value in reference_luminance:
        reference_luminance_sum.add(value)
    for value in candidate_luminance:
        candidate_luminance_sum.add(value)
    reference_mean = reference_luminance_sum.value() / pixel_count
    candidate_mean = candidate_luminance_sum.value() / pixel_count

    reference_variance_sum = StableSum()
    candidate_variance_sum = StableSum()
    covariance_sum = StableSum()
    for reference_value, candidate_value in zip(
        reference_luminance, candidate_luminance
    ):
        reference_delta = reference_value - reference_mean
        candidate_delta = candidate_value - candidate_mean
        reference_variance_sum.add(reference_delta * reference_delta)
        candidate_variance_sum.add(candidate_delta * candidate_delta)
        covariance_sum.add(reference_delta * candidate_delta)
    reference_variance = reference_variance_sum.value() / pixel_count
    candidate_variance = candidate_variance_sum.value() / pixel_count
    covariance = covariance_sum.value() / pixel_count
    c1 = 0.01 * 0.01
    c2 = 0.03 * 0.03
    ssim_denominator = (
        (reference_mean * reference_mean + candidate_mean * candidate_mean + c1)
        * (reference_variance + candidate_variance + c2)
    )
    if ssim_denominator <= 0.0:
        raise ParityError("global luminance SSIM denominator is invalid")
    ssim = (
        (2.0 * reference_mean * candidate_mean + c1)
        * (2.0 * covariance + c2)
    ) / ssim_denominator
    ssim = min(1.0, max(-1.0, ssim))

    edge_sum = StableSum()
    edge_samples = (reference.width - 2) * (reference.height - 2)
    # For a difference field in [-1, 1], the joint maximum of the two Sobel
    # components over a 3x3 neighborhood is sqrt(80) = 4*sqrt(5).  The looser
    # independent-component box (8*sqrt(2)) is not jointly attainable.
    edge_normalizer = 4.0 * math.sqrt(5.0)
    for y in range(1, reference.height - 1):
        for x in range(1, reference.width - 1):
            reference_x, reference_y = _luminance_sobel(
                reference_luminance, reference.width, x, y
            )
            candidate_x, candidate_y = _luminance_sobel(
                candidate_luminance, candidate.width, x, y
            )
            disagreement = math.hypot(
                reference_x - candidate_x, reference_y - candidate_y
            ) / edge_normalizer
            edge_sum.add(min(1.0, disagreement))

    per_channel = {}
    for index, name in enumerate(("red", "green", "blue")):
        per_channel[name] = {
            "mean_absolute_error": _finite_metric(
                absolute_sums[index].value() / pixel_count
            ),
            "root_mean_square_error": _finite_metric(
                math.sqrt(squared_sums[index].value() / pixel_count)
            ),
        }
    aggregate_samples = pixel_count * 3
    return {
        "changed_pixel_count": changed_pixels,
        "changed_pixel_fraction": _finite_metric(changed_pixels / pixel_count),
        "linear_rgb": {
            "mean_absolute_error": _finite_metric(
                aggregate_absolute.value() / aggregate_samples
            ),
            "root_mean_square_error": _finite_metric(
                math.sqrt(aggregate_squared.value() / aggregate_samples)
            ),
            "per_channel": per_channel,
        },
        "luminance_global_ssim": _finite_metric(ssim),
        "sobel_edge_disagreement_mean": _finite_metric(
            edge_sum.value() / edge_samples
        ),
    }


def _canonical_json_bytes(value: Any) -> bytes:
    try:
        encoded = json.dumps(
            value,
            allow_nan=False,
            ensure_ascii=True,
            separators=(",", ":"),
            sort_keys=True,
        )
    except (TypeError, ValueError) as exc:
        raise ParityError(f"cannot serialize canonical JSON: {exc}") from exc
    return (encoded + "\n").encode("ascii")


def _paths_alias(left: Path, right: Path) -> bool:
    try:
        if left.resolve(strict=False) == right.resolve(strict=False):
            return True
    except (OSError, RuntimeError) as exc:
        raise ParityError(f"cannot resolve path identity: {exc}") from exc
    try:
        if left.exists() and right.exists() and os.path.samefile(left, right):
            return True
    except OSError as exc:
        raise ParityError(f"cannot compare path identity: {exc}") from exc
    return False


def _reject_output_alias(output: Path, protected_paths: Sequence[Path]) -> None:
    for protected_path in protected_paths:
        if _paths_alias(output, protected_path):
            raise ParityError("output receipt must not replace an input")


def _atomic_write(
    path: Path, payload: bytes, protected_paths: Sequence[Path]
) -> None:
    parent = path.parent
    if not parent.is_dir():
        raise ParityError(f"output parent directory does not exist: {parent}")
    _reject_output_alias(path, protected_paths)
    temporary_name: str | None = None
    try:
        descriptor, temporary_name = tempfile.mkstemp(
            prefix=f".{path.name}.", suffix=".tmp", dir=str(parent)
        )
        with os.fdopen(descriptor, "wb") as destination:
            destination.write(payload)
            destination.flush()
            os.fsync(destination.fileno())
        # Revalidate immediately before replacement so an alias introduced
        # while metrics were computed cannot turn the receipt write into an
        # input replacement.
        _reject_output_alias(path, protected_paths)
        os.replace(temporary_name, path)
        temporary_name = None
    except OSError as exc:
        raise ParityError(f"cannot atomically write output receipt: {exc}") from exc
    finally:
        if temporary_name is not None:
            try:
                os.unlink(temporary_name)
            except FileNotFoundError:
                pass


def _parse_threshold(value: str) -> float:
    try:
        number = float(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("threshold must be a number") from exc
    if not math.isfinite(number) or number < 0.0 or number > 1.0:
        raise argparse.ArgumentTypeError("threshold must be finite and in [0, 1]")
    return number


def _threshold_checks(metrics: Mapping[str, Any], thresholds: Mapping[str, float]) -> Dict[str, bool]:
    linear_rgb = metrics["linear_rgb"]
    return {
        "changed_pixel_fraction": (
            metrics["changed_pixel_fraction"]
            <= thresholds["changed_pixel_fraction_max"]
        ),
        "linear_rgb_mae": (
            linear_rgb["mean_absolute_error"] <= thresholds["linear_rgb_mae_max"]
        ),
        "linear_rgb_rmse": (
            linear_rgb["root_mean_square_error"]
            <= thresholds["linear_rgb_rmse_max"]
        ),
        "luminance_global_ssim": (
            metrics["luminance_global_ssim"]
            >= thresholds["luminance_global_ssim_min"]
        ),
        "sobel_edge_disagreement_mean": (
            metrics["sobel_edge_disagreement_mean"]
            <= thresholds["sobel_edge_disagreement_mean_max"]
        ),
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Compare strict UI-free renderer PNGs and write an atomic receipt."
    )
    parser.add_argument("--reference", required=True, type=Path)
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--reference-metadata", required=True, type=Path)
    parser.add_argument("--candidate-metadata", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument(
        "--max-linear-mae",
        type=_parse_threshold,
        default=DEFAULT_THRESHOLDS["linear_rgb_mae_max"],
    )
    parser.add_argument(
        "--max-linear-rmse",
        type=_parse_threshold,
        default=DEFAULT_THRESHOLDS["linear_rgb_rmse_max"],
    )
    parser.add_argument(
        "--min-luminance-ssim",
        type=_parse_threshold,
        default=DEFAULT_THRESHOLDS["luminance_global_ssim_min"],
    )
    parser.add_argument(
        "--max-edge-disagreement",
        type=_parse_threshold,
        default=DEFAULT_THRESHOLDS["sobel_edge_disagreement_mean_max"],
    )
    parser.add_argument(
        "--max-changed-pixel-fraction",
        type=_parse_threshold,
        default=DEFAULT_THRESHOLDS["changed_pixel_fraction_max"],
    )
    return parser


def run(args: argparse.Namespace) -> bool:
    input_paths = (
        args.reference,
        args.candidate,
        args.reference_metadata,
        args.candidate_metadata,
    )
    if _paths_alias(args.reference, args.candidate):
        raise ParityError(
            "reference and candidate PNGs must be distinct filesystem objects"
        )
    if _paths_alias(args.reference_metadata, args.candidate_metadata):
        raise ParityError(
            "reference and candidate metadata must be distinct filesystem objects"
        )
    _reject_output_alias(args.output, input_paths)

    reference_png_bytes = _read_bounded(
        args.reference, MAX_PNG_BYTES, "reference PNG"
    )
    candidate_png_bytes = _read_bounded(
        args.candidate, MAX_PNG_BYTES, "candidate PNG"
    )
    reference_metadata_bytes = _read_bounded(
        args.reference_metadata, MAX_METADATA_BYTES, "reference metadata"
    )
    candidate_metadata_bytes = _read_bounded(
        args.candidate_metadata, MAX_METADATA_BYTES, "candidate metadata"
    )

    reference_metadata = parse_metadata(
        reference_metadata_bytes, "reference metadata"
    )
    candidate_metadata = parse_metadata(
        candidate_metadata_bytes, "candidate metadata"
    )
    matched_metadata = _require_matching_metadata(
        reference_metadata, candidate_metadata
    )
    reference_png = decode_strict_png(reference_png_bytes, "reference PNG")
    candidate_png = decode_strict_png(candidate_png_bytes, "candidate PNG")
    if (
        reference_png.width != candidate_png.width
        or reference_png.height != candidate_png.height
    ):
        raise ParityError("reference and candidate PNG dimensions differ")
    expected_resolution = {
        "width": reference_png.width,
        "height": reference_png.height,
    }
    if reference_metadata["resolution"] != expected_resolution:
        raise ParityError("metadata resolution does not match decoded PNG dimensions")

    metrics = compute_metrics(reference_png, candidate_png)
    thresholds = {
        "linear_rgb_mae_max": args.max_linear_mae,
        "linear_rgb_rmse_max": args.max_linear_rmse,
        "luminance_global_ssim_min": args.min_luminance_ssim,
        "sobel_edge_disagreement_mean_max": args.max_edge_disagreement,
        "changed_pixel_fraction_max": args.max_changed_pixel_fraction,
    }
    checks = _threshold_checks(metrics, thresholds)
    passed = all(checks.values())
    matched_bytes = _canonical_json_bytes(matched_metadata)
    tool_bytes = Path(__file__).resolve().read_bytes()
    receipt = {
        "schema": RECEIPT_SCHEMA,
        "tool": {
            "metric_profile": METRIC_PROFILE,
            "source_sha256": _sha256(tool_bytes),
        },
        "inputs": {
            "reference": {
                "png_bytes": len(reference_png_bytes),
                "png_sha256": _sha256(reference_png_bytes),
                "metadata_bytes": len(reference_metadata_bytes),
                "metadata_sha256": _sha256(reference_metadata_bytes),
                "renderer": reference_metadata["renderer"],
            },
            "candidate": {
                "png_bytes": len(candidate_png_bytes),
                "png_sha256": _sha256(candidate_png_bytes),
                "metadata_bytes": len(candidate_metadata_bytes),
                "metadata_sha256": _sha256(candidate_metadata_bytes),
                "renderer": candidate_metadata["renderer"],
            },
        },
        "matched_capture_contract": matched_metadata,
        "matched_capture_contract_sha256": _sha256(matched_bytes),
        "comparison_semantics": {
            "candidate_goal": "meet_or_exceed_reference_quality",
            "improvement_claimed_by_symmetric_metrics": False,
            "pixel_identity_required": False,
            "reference_role": "regression_floor",
            "symmetric_difference_budget": True,
        },
        "dimensions": {
            "width": reference_png.width,
            "height": reference_png.height,
            "pixel_count": reference_png.width * reference_png.height,
        },
        "validation": {
            "alpha": {
                "candidate_nonopaque_pixel_count": candidate_png.nonopaque_pixel_count,
                "candidate_source_has_alpha": candidate_png.source_has_alpha,
                "reference_nonopaque_pixel_count": reference_png.nonopaque_pixel_count,
                "reference_source_has_alpha": reference_png.source_has_alpha,
                "required_mode": "opaque",
            },
            "blank_frames_rejected": True,
            "derived_metrics_finite": True,
            "metadata_numbers_finite": True,
            "png_profile": "8bit-noninterlaced-rgb-or-opaque-rgba-no-ancillary-v1",
        },
        "metrics": metrics,
        "thresholds": thresholds,
        "threshold_checks": checks,
        "passed": passed,
    }
    _atomic_write(args.output, _canonical_json_bytes(receipt), input_paths)
    return passed


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        passed = run(args)
    except ParityError as exc:
        print(f"visual parity error: {exc}", file=sys.stderr)
        return 2
    if not passed:
        print("visual parity thresholds failed", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
