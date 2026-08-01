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
SURFACE_MODES = {
    "macos-arm64-metal": "macos-hidden-native",
    "windows-x64-d3d11": "windows-hidden-native",
    "linux-x86_64-vulkan": "linux-null-window-offscreen",
}
SHADER_MEDIA_PROVENANCE = {
    "shader_media_root": "Samples/Media/Hlms",
    "shader_media_license_expression": (
        "MIT AND LicenseRef-Heitz-LTC-Paper-Notice"
    ),
    "shader_media_third_party_source_path": (
        "Samples/Media/Hlms/Pbs/Any/AreaLights_LTC_piece_ps.any"
    ),
    "shader_media_third_party_source_sha256": (
        "44146bd7eee4bd6a3bb9428352e89dc20d7690b32c609e62c5f9330678f3a124"
    ),
    "shader_media_notice_path": (
        "licenses/LicenseRef-Heitz-LTC-Paper-Notice.txt"
    ),
    "shader_media_notice_sha256": (
        "cc942875917be271c92fdc1fdec7a17da92b45dadf42a979b69583003f38bba6"
    ),
    "shader_media_upstream_source": "https://github.com/selfshadow/ltc_code/",
    "shader_media_paper_reference": (
        "Real-Time Polygonal-Light Shading with Linearly Transformed "
        "Cosines, ACM TOG 35(4), 2016"
    ),
    "shader_media_source_and_binary_notice_required": True,
    "shader_media_paper_reference_required": True,
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


def read_capability_report(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise FrameValidationError(
            f"could not read capability report: {error}"
        ) from error
    if not isinstance(value, dict):
        raise FrameValidationError("capability report root must be an object")
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


def inspect_pixels(pixels: bytes) -> dict[str, Any]:
    pixel_values = [
        (pixels[offset], pixels[offset + 1], pixels[offset + 2])
        for offset in range(0, len(pixels), 3)
    ]
    colours = Counter(pixel_values)
    background, background_pixels = colours.most_common(1)[0]
    foreground = [
        (index % WIDTH, index // WIDTH)
        for index, colour in enumerate(pixel_values)
        if colour != background
    ]
    bounds = None
    if foreground:
        x_values = [point[0] for point in foreground]
        y_values = [point[1] for point in foreground]
        bounds = {
            "min_x": min(x_values),
            "max_x": max(x_values),
            "min_y": min(y_values),
            "max_y": max(y_values),
        }
    corner_indices = (0, WIDTH - 1, (HEIGHT - 1) * WIDTH, WIDTH * HEIGHT - 1)
    corners = [list(pixel_values[index]) for index in corner_indices]
    center = list(pixel_values[(HEIGHT // 2) * WIDTH + WIDTH // 2])
    luminances = [
        (0.2126 * red + 0.7152 * green + 0.0722 * blue) / 255.0
        for red, green, blue in colours
    ]
    return {
        "distinct_rgb8_values": len(colours),
        "background_rgb8": list(background),
        "background_pixels": background_pixels,
        "non_background_pixels": WIDTH * HEIGHT - background_pixels,
        "foreground_bounds": bounds,
        "corner_rgb8": corners,
        "center_rgb8": center,
        "minimum_luminance": min(luminances),
        "maximum_luminance": max(luminances),
        "rgb8_fnv1a64": f"{fnv1a64(pixels):016x}",
    }


def validate(
    report: dict[str, Any],
    pixels: bytes,
    expected_platform_policy: str,
    capability_report: dict[str, Any],
) -> dict[str, Any]:
    expected_renderer = RENDERERS.get(expected_platform_policy)
    if expected_renderer is None:
        raise FrameValidationError(
            f"unreviewed OGRE-Next frame policy: {expected_platform_policy}"
        )
    _require_exact_int(report.get("schema_version"), 3, "schema_version")
    if report.get("status") != "pass":
        raise FrameValidationError("frame report status is not pass")
    if report.get("platform_policy") != expected_platform_policy:
        raise FrameValidationError("frame report platform policy does not match the runner")
    if report.get("renderer") != expected_renderer:
        raise FrameValidationError("frame report renderer does not match its platform policy")
    if report.get("surface_mode") != SURFACE_MODES[expected_platform_policy]:
        raise FrameValidationError("frame report surface mode does not match its policy")
    device_name = report.get("device_name")
    if (
        not isinstance(device_name, str)
        or not device_name.strip()
        or device_name == "(default)"
    ):
        raise FrameValidationError("frame report does not identify the initialized device")
    if report.get("native_ray_tracing") != "not_evaluated":
        raise FrameValidationError("raster frame probe must not claim native ray tracing")

    provenance = report.get("provenance")
    build = report.get("build")
    lifecycle = report.get("lifecycle")
    if not isinstance(provenance, dict) or not isinstance(build, dict):
        raise FrameValidationError("frame report is missing provenance or build identity")
    if not isinstance(lifecycle, dict) or lifecycle.get(
        "renderer_shutdown_completed"
    ) is not True:
        raise FrameValidationError("frame report does not prove clean renderer shutdown")
    ogre_commit = provenance.get("ogre_next_commit")
    ogre_archive = provenance.get("ogre_next_archive_sha256")
    abi_cookie = build.get("abi_cookie")
    if not isinstance(ogre_commit, str) or re.fullmatch(
        r"[0-9a-f]{40}", ogre_commit
    ) is None:
        raise FrameValidationError("frame OGRE-Next commit is malformed")
    if not isinstance(ogre_archive, str) or re.fullmatch(
        r"[0-9a-f]{64}", ogre_archive
    ) is None:
        raise FrameValidationError("frame OGRE-Next archive hash is malformed")
    if not isinstance(abi_cookie, str) or re.fullmatch(
        r"[0-9a-f]{32}", abi_cookie
    ) is None:
        raise FrameValidationError("frame ABI cookie is malformed")
    if build.get("ogre_version") != "3.0.0":
        raise FrameValidationError("frame OGRE-Next version is outside the reviewed pin")
    failed_shader_media = [
        field
        for field, expected in SHADER_MEDIA_PROVENANCE.items()
        if provenance.get(field) != expected
    ]
    if failed_shader_media:
        raise FrameValidationError(
            "frame shader-media provenance failed closed on "
            + ", ".join(sorted(failed_shader_media))
        )

    capability_provenance = capability_report.get("provenance")
    capability_build = capability_report.get("build")
    capability_renderer = capability_report.get("capabilities", {}).get("renderer")
    if (
        capability_report.get("status") != "pass"
        or not isinstance(capability_provenance, dict)
        or not isinstance(capability_build, dict)
        or not isinstance(capability_renderer, dict)
    ):
        raise FrameValidationError("capability report is not a valid passing join input")
    joins = {
        "commit": capability_provenance.get("commit") == ogre_commit,
        "archive": capability_provenance.get("archive_sha256") == ogre_archive,
        "abi": capability_build.get("abi_cookie") == abi_cookie,
        "version": capability_build.get("ogre_version") == build.get("ogre_version"),
        "policy": capability_build.get("platform_policy")
        == expected_platform_policy,
        "renderer": capability_renderer.get("name") == expected_renderer,
    }
    joins.update(
        {
            f"shader_media.{field}": capability_provenance.get(field)
            == provenance.get(field)
            for field in SHADER_MEDIA_PROVENANCE
        }
    )
    failed_joins = [name for name, passed in joins.items() if not passed]
    if failed_joins:
        raise FrameValidationError(
            "frame and capability reports disagree on "
            + ", ".join(sorted(failed_joins))
        )

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
    if not math.isclose(
        float(observed["minimum_luminance"]),
        minimum_luminance,
        rel_tol=0.0,
        abs_tol=1e-6,
    ) or not math.isclose(
        float(observed["maximum_luminance"]),
        maximum_luminance,
        rel_tol=0.0,
        abs_tol=1e-6,
    ):
        raise FrameValidationError("report and image luminance extrema disagree")
    observed_span = float(observed["maximum_luminance"]) - float(
        observed["minimum_luminance"]
    )
    if observed_span < MIN_LUMINANCE_SPAN:
        raise FrameValidationError("RGB8 image luminance span is too small")
    background = observed["background_rgb8"]
    if int(observed["background_pixels"]) <= total_pixels // 2:
        raise FrameValidationError("frame has no dominant scene background")
    if any(corner != background for corner in observed["corner_rgb8"]):
        raise FrameValidationError("frame corners do not agree with the modal background")
    if observed["center_rgb8"] == background:
        raise FrameValidationError("expected PBS geometry is absent from frame center")
    bounds = observed["foreground_bounds"]
    if not isinstance(bounds, dict):
        raise FrameValidationError("frame has no foreground bounds")
    if (
        bounds["min_x"] < 16
        or bounds["max_x"] > WIDTH - 17
        or bounds["min_y"] < 8
        or bounds["max_y"] > HEIGHT - 9
        or bounds["max_x"] - bounds["min_x"] + 1 < WIDTH // 4
        or bounds["max_y"] - bounds["min_y"] + 1 < HEIGHT // 3
    ):
        raise FrameValidationError(
            "foreground does not occupy the reviewed central geometry region"
        )
    return observed


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--image", required=True, type=Path)
    parser.add_argument("--capability-report", required=True, type=Path)
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
            read_capability_report(args.capability_report),
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
