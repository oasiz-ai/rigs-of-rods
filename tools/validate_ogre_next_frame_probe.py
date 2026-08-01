#!/usr/bin/env python3
"""Validate one UI-free OGRE-Next PBS frame and its runtime report."""

from __future__ import annotations

import argparse
from collections import Counter
import json
import math
from pathlib import Path
import re
import sys
from typing import Any


WIDTH = 192
HEIGHT = 128
WARMUP_FRAMES = 4
PIXEL_FORMAT = "PFG_RGBA8_UNORM"
MIN_DISTINCT_RGB8 = 8
MIN_FOREGROUND_PIXELS = 512
MIN_LUMINANCE_SPAN = 0.05
FNV1A64_OFFSET = 14695981039346656037
FNV1A64_PRIME = 1099511628211
FNV1A64_MASK = (1 << 64) - 1
RENDERERS = {
    "macos-arm64-metal": "Metal Rendering Subsystem",
    "windows-x64-d3d11": "Direct3D11 Rendering Subsystem",
    "linux-x86_64-vulkan": "Vulkan Rendering Subsystem",
}


class FrameValidationError(RuntimeError):
    """The frame artifact does not satisfy the reviewed raster contract."""


def _require_exact_int(value: object, expected: int, label: str) -> None:
    if type(value) is not int or value != expected:
        raise FrameValidationError(
            f"{label} must be the integer {expected}, got {value!r}"
        )


def _require_count(value: object, minimum: int, maximum: int, label: str) -> int:
    if type(value) is not int or not minimum <= value <= maximum:
        raise FrameValidationError(
            f"{label} must be an integer in [{minimum}, {maximum}], "
            f"got {value!r}"
        )
    return value


def _require_finite_number(value: object, label: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise FrameValidationError(f"{label} must be a finite number")
    converted = float(value)
    if not math.isfinite(converted):
        raise FrameValidationError(f"{label} must be finite")
    return converted


def read_report(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise FrameValidationError(f"could not read frame report: {error}") from error
    if not isinstance(value, dict):
        raise FrameValidationError("frame report root must be an object")
    return value


def read_ppm(path: Path) -> bytes:
    header = f"P6\n{WIDTH} {HEIGHT}\n255\n".encode("ascii")
    try:
        payload = path.read_bytes()
    except OSError as error:
        raise FrameValidationError(f"could not read frame image: {error}") from error
    if not payload.startswith(header):
        raise FrameValidationError("frame image is not the canonical P6 RGB8 artifact")
    pixels = payload[len(header) :]
    expected_bytes = WIDTH * HEIGHT * 3
    if len(pixels) != expected_bytes:
        raise FrameValidationError(
            f"frame image has {len(pixels)} RGB bytes, expected {expected_bytes}"
        )
    return pixels


def fnv1a64(payload: bytes) -> int:
    result = FNV1A64_OFFSET
    for value in payload:
        result ^= value
        result = (result * FNV1A64_PRIME) & FNV1A64_MASK
    return result


def inspect_pixels(pixels: bytes) -> dict[str, int | float | str]:
    colours = Counter(
        (pixels[offset], pixels[offset + 1], pixels[offset + 2])
        for offset in range(0, len(pixels), 3)
    )
    largest_colour_run = max(colours.values())
    luminances = [
        (0.2126 * red + 0.7152 * green + 0.0722 * blue) / 255.0
        for red, green, blue in colours
    ]
    return {
        "distinct_rgb8_values": len(colours),
        "non_background_pixels": WIDTH * HEIGHT - largest_colour_run,
        "minimum_luminance": min(luminances),
        "maximum_luminance": max(luminances),
        "rgb8_fnv1a64": f"{fnv1a64(pixels):016x}",
    }


def validate(
    report: dict[str, Any], pixels: bytes, expected_platform_policy: str
) -> dict[str, int | float | str]:
    expected_renderer = RENDERERS.get(expected_platform_policy)
    if expected_renderer is None:
        raise FrameValidationError(
            f"unreviewed OGRE-Next frame policy: {expected_platform_policy}"
        )
    _require_exact_int(report.get("schema_version"), 1, "schema_version")
    if report.get("status") != "pass":
        raise FrameValidationError("frame report status is not pass")
    if report.get("platform_policy") != expected_platform_policy:
        raise FrameValidationError("frame report platform policy does not match the runner")
    if report.get("renderer") != expected_renderer:
        raise FrameValidationError("frame report renderer does not match its platform policy")
    if report.get("native_ray_tracing") != "not_evaluated":
        raise FrameValidationError("raster frame probe must not claim native ray tracing")

    frame = report.get("frame")
    if not isinstance(frame, dict):
        raise FrameValidationError("frame report is missing the frame object")
    _require_exact_int(frame.get("width"), WIDTH, "frame.width")
    _require_exact_int(frame.get("height"), HEIGHT, "frame.height")
    _require_exact_int(
        frame.get("warmup_frames"), WARMUP_FRAMES, "frame.warmup_frames"
    )
    if frame.get("pixel_format") != PIXEL_FORMAT:
        raise FrameValidationError("frame pixel format is not the reviewed RGB8 target")
    for field in ("hlms_pbs_geometry", "compositor2", "gpu_readback"):
        if frame.get(field) is not True:
            raise FrameValidationError(f"frame.{field} was not proven")
    if frame.get("ui_included") is not False:
        raise FrameValidationError("frame readback must exclude UI")

    total_pixels = WIDTH * HEIGHT
    distinct = _require_count(
        frame.get("distinct_rgb8_values"),
        MIN_DISTINCT_RGB8,
        total_pixels,
        "frame.distinct_rgb8_values",
    )
    foreground = _require_count(
        frame.get("non_background_pixels"),
        MIN_FOREGROUND_PIXELS,
        total_pixels - 1,
        "frame.non_background_pixels",
    )
    minimum_luminance = _require_finite_number(
        frame.get("minimum_luminance"), "frame.minimum_luminance"
    )
    maximum_luminance = _require_finite_number(
        frame.get("maximum_luminance"), "frame.maximum_luminance"
    )
    if not 0.0 <= minimum_luminance <= maximum_luminance <= 1.0:
        raise FrameValidationError("reported luminance is outside the UNORM range")
    if maximum_luminance - minimum_luminance < MIN_LUMINANCE_SPAN:
        raise FrameValidationError("reported frame luminance span is too small")
    reported_hash = frame.get("rgb8_fnv1a64")
    if not isinstance(reported_hash, str) or re.fullmatch(
        r"[0-9a-f]{16}", reported_hash
    ) is None:
        raise FrameValidationError("frame RGB hash is not a lowercase FNV-1a-64")

    observed = inspect_pixels(pixels)
    if observed["distinct_rgb8_values"] != distinct:
        raise FrameValidationError("report and image disagree on distinct RGB8 values")
    if observed["non_background_pixels"] != foreground:
        raise FrameValidationError("report and image disagree on foreground pixels")
    if observed["rgb8_fnv1a64"] != reported_hash:
        raise FrameValidationError("report and image RGB hashes disagree")
    observed_span = float(observed["maximum_luminance"]) - float(
        observed["minimum_luminance"]
    )
    if observed_span < MIN_LUMINANCE_SPAN:
        raise FrameValidationError("RGB8 image luminance span is too small")
    return observed


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--image", required=True, type=Path)
    parser.add_argument(
        "--platform-policy", required=True, choices=tuple(RENDERERS)
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        observed = validate(
            read_report(args.report),
            read_ppm(args.image),
            args.platform_policy,
        )
    except FrameValidationError as error:
        print(f"OGRE-Next frame validation failed: {error}", file=sys.stderr)
        return 1
    print(
        json.dumps(
            {
                "schema_version": 1,
                "status": "pass",
                "platform_policy": args.platform_policy,
                "observed": observed,
            },
            indent=2,
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
