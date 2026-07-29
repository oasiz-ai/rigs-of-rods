#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prove CityWorld PSSM with paired physics, RGB, and frame-time evidence.

The gate runs the same deterministic bridge-to-gateway traversal twice in
fresh isolated homes. The control disables shadows and the treatment enables
the exact quality-2 OGRE 14 PSSM profile. It rejects provenance, physics,
camera, or sample-count drift before measuring localized cast-shadow pixels
and bounded frame-time overhead.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import math
import os
from pathlib import Path
import sys
from typing import Mapping, Sequence


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
BASE_RUNNER_PATH = REPOSITORY_ROOT / "tools/run_cityworld_bridge_scene.py"
GATEWAY_RUNNER_PATH = REPOSITORY_ROOT / "tools/run_cityworld_gateway_scene.py"
REPORT_FORMAT = "ror-cityworld-shadow-comparison-report-v1"
GATEWAY_REPORT_FORMAT = "ror-cityworld-gateway-runtime-report-v2"
RGB_ARTIFACT_NAME = "cityworld_gateway_rgb.png"
SHADOW_QUALITY = 2
EXPECTED_WIDTH = 1280
EXPECTED_HEIGHT = 720
DEFAULT_MAX_P95_MS = 18.333
MAX_MEAN_OVERHEAD_MS = 2.0
MAX_P95_OVERHEAD_MS = 5.0

MIN_CHANGED_FRACTION = 0.01
MAX_CHANGED_FRACTION = 0.10
MIN_DARKENED_FRACTION = 0.01
MIN_STRONGLY_DARKENED_FRACTION = 0.005
MAX_LIGHTENED_FRACTION = 0.005
MIN_MEAN_ABSOLUTE_CHANNEL_DELTA = 0.15
MAX_MEAN_SIGNED_LUMINANCE_DELTA = -0.10
MIN_DARK_TO_LIGHT_COUNT_RATIO = 4

ROI_LEFT = 500
ROI_RIGHT = 780
ROI_TOP = 230
ROI_BOTTOM = 340
MIN_ROI_DARKENED_FRACTION = 0.08
MIN_ROI_STRONGLY_DARKENED_FRACTION = 0.04
MAX_ROI_LIGHTENED_FRACTION = 0.01

IDENTITY_FIELDS = (
    "additional_cityworld_compile_reports",
    "cityworld_compile_report_sha256",
    "cityworld_compile_schema",
    "content_commit",
    "corridor",
    "executable",
    "executable_sha256",
    "format",
    "machine",
    "platform",
    "repository_commit",
    "runners",
    "runtime_content",
    "runtime_pack",
    "vehicle_archive",
)
PHYSICS_METRIC_FIELDS = (
    "distance_m",
    "exit_x",
    "exit_z",
    "max_path_error_m",
    "max_y",
    "min_y",
    "physics_steps",
    "speed_mps",
    "turn_degrees",
)
RENDERER_IDENTITY_FIELDS = (
    "api_version",
    "device",
    "render_system",
    "vendor",
)


def load_module(name: str, path: Path) -> object:
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load module: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


BASE = load_module("ror_cityworld_bridge_scene_for_shadow", BASE_RUNNER_PATH)


def require_mapping(value: object, label: str) -> Mapping[str, object]:
    if not isinstance(value, dict):
        raise BASE.BridgeSceneFailure(f"{label} is not an object")
    return value


def require_number(value: object, label: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise BASE.BridgeSceneFailure(f"{label} is not numeric")
    number = float(value)
    if not math.isfinite(number):
        raise BASE.BridgeSceneFailure(f"{label} is not finite")
    return number


def require_digest_record(
    value: object,
    label: str,
) -> Mapping[str, object]:
    record = require_mapping(value, label)
    artifact = record.get("artifact")
    digest = record.get("sha256")
    size = record.get("size")
    if (
        not isinstance(artifact, str)
        or not artifact
        or not isinstance(digest, str)
        or len(digest) != 64
        or any(character not in "0123456789abcdef" for character in digest)
        or isinstance(size, bool)
        or not isinstance(size, int)
        or size <= 0
    ):
        raise BASE.BridgeSceneFailure(f"{label} is not a digest record")
    return record


def validate_child_report(
    report: Mapping[str, object],
    shadow_mode: str,
) -> None:
    if report.get("format") != GATEWAY_REPORT_FORMAT:
        raise BASE.BridgeSceneFailure(
            f"{shadow_mode} child report has the wrong format"
        )
    rendering = require_mapping(
        report.get("rendering"),
        f"{shadow_mode} rendering",
    )
    if (
        rendering.get("shadow_mode") != shadow_mode
        or rendering.get("shadow_quality") != SHADOW_QUALITY
        or rendering.get("width") != EXPECTED_WIDTH
        or rendering.get("height") != EXPECTED_HEIGHT
    ):
        raise BASE.BridgeSceneFailure(
            f"{shadow_mode} child rendering identity drifted"
        )
    if shadow_mode == "none":
        if rendering.get("pssm") is not None:
            raise BASE.BridgeSceneFailure(
                "no-shadow child unexpectedly reports PSSM"
            )
    else:
        require_mapping(rendering.get("pssm"), "PSSM configuration")
    renderer_identity = require_mapping(
        rendering.get("device"),
        f"{shadow_mode} renderer identity",
    )
    for field in RENDERER_IDENTITY_FIELDS:
        value = renderer_identity.get(field)
        if not isinstance(value, str) or not value:
            raise BASE.BridgeSceneFailure(
                f"{shadow_mode} renderer identity omits {field}"
            )
    configs = require_mapping(
        rendering.get("configs"),
        f"{shadow_mode} rendering configs",
    )
    for name in ("RoR.cfg", "ogre.cfg"):
        config = require_mapping(
            configs.get(name),
            f"{shadow_mode} {name}",
        )
        for phase in ("requested", "effective"):
            require_digest_record(
                config.get(phase),
                f"{shadow_mode} {phase} {name}",
            )
    ror_config = require_mapping(
        configs.get("RoR.cfg"),
        f"{shadow_mode} RoR.cfg",
    )
    for field in (
        "requested_shadow_normalized_sha256",
        "effective_shadow_normalized_sha256",
    ):
        digest = ror_config.get(field)
        if (
            not isinstance(digest, str)
            or len(digest) != 64
            or any(
                character not in "0123456789abcdef"
                for character in digest
            )
        ):
            raise BASE.BridgeSceneFailure(
                f"{shadow_mode} RoR.cfg omits {field}"
            )

    metrics = require_mapping(
        report.get("metrics"),
        f"{shadow_mode} metrics",
    )
    for field in (*PHYSICS_METRIC_FIELDS, "frame_samples"):
        require_number(metrics.get(field), f"{shadow_mode} metric {field}")
    for field in ("frame_mean_ms", "frame_p95_ms", "frame_max_ms"):
        require_number(metrics.get(field), f"{shadow_mode} metric {field}")
    rgb = require_mapping(report.get("rgb"), f"{shadow_mode} RGB record")
    if (
        rgb.get("width") != EXPECTED_WIDTH
        or rgb.get("height") != EXPECTED_HEIGHT
    ):
        raise BASE.BridgeSceneFailure(
            f"{shadow_mode} child RGB dimensions drifted"
        )


def compare_identity(
    baseline: Mapping[str, object],
    pssm: Mapping[str, object],
) -> dict[str, object]:
    for field in IDENTITY_FIELDS:
        if field not in baseline or field not in pssm:
            raise BASE.BridgeSceneFailure(
                f"paired child reports omit identity field: {field}"
            )
        if baseline[field] != pssm[field]:
            raise BASE.BridgeSceneFailure(
                f"paired child identity differs: {field}"
            )

    baseline_rendering = require_mapping(
        baseline["rendering"],
        "baseline rendering",
    )
    pssm_rendering = require_mapping(pssm["rendering"], "PSSM rendering")
    baseline_configs = require_mapping(
        baseline_rendering.get("configs"),
        "baseline rendering configs",
    )
    pssm_configs = require_mapping(
        pssm_rendering.get("configs"),
        "PSSM rendering configs",
    )
    if baseline_rendering.get("device") != pssm_rendering.get("device"):
        raise BASE.BridgeSceneFailure("paired renderer identity differs")
    baseline_ogre = require_mapping(
        baseline_configs.get("ogre.cfg"),
        "baseline OGRE configuration",
    )
    pssm_ogre = require_mapping(
        pssm_configs.get("ogre.cfg"),
        "PSSM OGRE configuration",
    )
    for phase in ("requested", "effective"):
        if baseline_ogre.get(phase) != pssm_ogre.get(phase):
            raise BASE.BridgeSceneFailure(
                "paired renderer configuration differs: " + phase
            )
    baseline_ror = require_mapping(
        baseline_configs.get("RoR.cfg"),
        "baseline RoR configuration",
    )
    pssm_ror = require_mapping(
        pssm_configs.get("RoR.cfg"),
        "PSSM RoR configuration",
    )
    for phase in ("requested", "effective"):
        baseline_phase = require_mapping(
            baseline_ror.get(phase),
            f"baseline {phase} RoR configuration",
        )
        pssm_phase = require_mapping(
            pssm_ror.get(phase),
            f"PSSM {phase} RoR configuration",
        )
        if baseline_phase.get("sha256") == pssm_phase.get("sha256"):
            raise BASE.BridgeSceneFailure(
                "paired RoR configurations do not encode distinct "
                f"shadow modes: {phase}"
            )
        normalized_field = phase + "_shadow_normalized_sha256"
        if (
            baseline_ror.get(normalized_field) !=
            pssm_ror.get(normalized_field)
        ):
            raise BASE.BridgeSceneFailure(
                "paired RoR configuration differs outside the shadow "
                f"setting: {phase}"
            )

    baseline_metrics = require_mapping(
        baseline["metrics"],
        "baseline metrics",
    )
    pssm_metrics = require_mapping(pssm["metrics"], "PSSM metrics")
    physics: dict[str, object] = {}
    for field in PHYSICS_METRIC_FIELDS:
        if baseline_metrics.get(field) != pssm_metrics.get(field):
            raise BASE.BridgeSceneFailure(
                f"PSSM changed deterministic traversal metric: {field}"
            )
        physics[field] = baseline_metrics[field]
    if baseline_metrics.get("frame_samples") != pssm_metrics.get(
        "frame_samples"
    ):
        raise BASE.BridgeSceneFailure(
            "paired frame sample counts are not identical"
        )
    return physics


def compare_performance(
    baseline: Mapping[str, object],
    pssm: Mapping[str, object],
    max_p95_ms: float,
) -> dict[str, object]:
    baseline_metrics = require_mapping(
        baseline["metrics"],
        "baseline metrics",
    )
    pssm_metrics = require_mapping(pssm["metrics"], "PSSM metrics")
    baseline_samples = int(
        require_number(
            baseline_metrics.get("frame_samples"),
            "baseline frame sample count",
        )
    )
    pssm_samples = int(
        require_number(
            pssm_metrics.get("frame_samples"),
            "PSSM frame sample count",
        )
    )
    if baseline_samples != pssm_samples or baseline_samples < 500:
        raise BASE.BridgeSceneFailure(
            "paired performance sample count is invalid"
        )

    baseline_mean = require_number(
        baseline_metrics.get("frame_mean_ms"),
        "baseline frame mean",
    )
    pssm_mean = require_number(
        pssm_metrics.get("frame_mean_ms"),
        "PSSM frame mean",
    )
    baseline_p95 = require_number(
        baseline_metrics.get("frame_p95_ms"),
        "baseline frame p95",
    )
    pssm_p95 = require_number(
        pssm_metrics.get("frame_p95_ms"),
        "PSSM frame p95",
    )
    mean_overhead = pssm_mean - baseline_mean
    p95_overhead = pssm_p95 - baseline_p95
    if baseline_p95 > max_p95_ms or pssm_p95 > max_p95_ms:
        raise BASE.BridgeSceneFailure(
            f"paired p95 exceeds {max_p95_ms:.3f} ms"
        )
    if mean_overhead > MAX_MEAN_OVERHEAD_MS:
        raise BASE.BridgeSceneFailure("PSSM mean frame overhead is excessive")
    if p95_overhead > MAX_P95_OVERHEAD_MS:
        raise BASE.BridgeSceneFailure("PSSM p95 frame overhead is excessive")
    return {
        "baseline_mean_ms": baseline_mean,
        "baseline_p95_ms": baseline_p95,
        "max_p95_ms": max_p95_ms,
        "pssm_mean_ms": pssm_mean,
        "pssm_mean_overhead_ms": round(mean_overhead, 6),
        "pssm_p95_ms": pssm_p95,
        "pssm_p95_overhead_ms": round(p95_overhead, 6),
        "samples": baseline_samples,
    }


def compare_rgb(
    baseline_pixels: bytes,
    pssm_pixels: bytes,
    width: int = EXPECTED_WIDTH,
    height: int = EXPECTED_HEIGHT,
) -> dict[str, object]:
    expected_size = width * height * 3
    if (
        width <= 0
        or height <= 0
        or len(baseline_pixels) != expected_size
        or len(pssm_pixels) != expected_size
    ):
        raise BASE.BridgeSceneFailure("paired RGB buffers have invalid sizes")
    if not (
        0 <= ROI_LEFT < ROI_RIGHT <= width
        and 0 <= ROI_TOP < ROI_BOTTOM <= height
    ):
        raise BASE.BridgeSceneFailure(
            "cast-shadow ROI is outside the paired RGB extent"
        )

    changed = 0
    darkened = 0
    strongly_darkened = 0
    lightened = 0
    absolute_channel_delta = 0
    signed_luminance_delta = 0
    roi_darkened = 0
    roi_strongly_darkened = 0
    roi_lightened = 0
    roi_pixels = (ROI_RIGHT - ROI_LEFT) * (ROI_BOTTOM - ROI_TOP)

    for pixel_index in range(width * height):
        offset = pixel_index * 3
        red_delta = baseline_pixels[offset] - pssm_pixels[offset]
        green_delta = (
            baseline_pixels[offset + 1] -
            pssm_pixels[offset + 1]
        )
        blue_delta = (
            baseline_pixels[offset + 2] -
            pssm_pixels[offset + 2]
        )
        absolute_channel_delta += (
            abs(red_delta) + abs(green_delta) + abs(blue_delta)
        )
        if max(abs(red_delta), abs(green_delta), abs(blue_delta)) >= 4:
            changed += 1

        luminance_delta = (
            2126 * red_delta +
            7152 * green_delta +
            722 * blue_delta
        )
        signed_luminance_delta -= luminance_delta
        is_darkened = luminance_delta >= 4 * 10000
        is_strongly_darkened = luminance_delta >= 12 * 10000
        is_lightened = luminance_delta <= -4 * 10000
        darkened += int(is_darkened)
        strongly_darkened += int(is_strongly_darkened)
        lightened += int(is_lightened)

        x = pixel_index % width
        y = pixel_index // width
        if ROI_LEFT <= x < ROI_RIGHT and ROI_TOP <= y < ROI_BOTTOM:
            roi_darkened += int(is_darkened)
            roi_strongly_darkened += int(is_strongly_darkened)
            roi_lightened += int(is_lightened)

    pixel_count = width * height
    changed_fraction = changed / pixel_count
    darkened_fraction = darkened / pixel_count
    strongly_darkened_fraction = strongly_darkened / pixel_count
    lightened_fraction = lightened / pixel_count
    mean_absolute_channel_delta = (
        absolute_channel_delta / (pixel_count * 3)
    )
    mean_signed_luminance_delta = (
        signed_luminance_delta / (pixel_count * 10000)
    )
    roi_darkened_fraction = roi_darkened / roi_pixels
    roi_strongly_darkened_fraction = roi_strongly_darkened / roi_pixels
    roi_lightened_fraction = roi_lightened / roi_pixels

    if not MIN_CHANGED_FRACTION <= changed_fraction <= MAX_CHANGED_FRACTION:
        raise BASE.BridgeSceneFailure(
            "PSSM changed-pixel fraction is outside its localized gate"
        )
    if darkened_fraction < MIN_DARKENED_FRACTION:
        raise BASE.BridgeSceneFailure(
            "PSSM does not darken enough occluded pixels"
        )
    if strongly_darkened_fraction < MIN_STRONGLY_DARKENED_FRACTION:
        raise BASE.BridgeSceneFailure(
            "PSSM does not produce enough strong cast-shadow pixels"
        )
    if lightened_fraction > MAX_LIGHTENED_FRACTION:
        raise BASE.BridgeSceneFailure(
            "PSSM unexpectedly lightens too many pixels"
        )
    if darkened < MIN_DARK_TO_LIGHT_COUNT_RATIO * max(lightened, 1):
        raise BASE.BridgeSceneFailure(
            "PSSM image change is not dominated by darkening"
        )
    if mean_absolute_channel_delta < MIN_MEAN_ABSOLUTE_CHANNEL_DELTA:
        raise BASE.BridgeSceneFailure(
            "PSSM mean channel difference is too small"
        )
    if mean_signed_luminance_delta > MAX_MEAN_SIGNED_LUMINANCE_DELTA:
        raise BASE.BridgeSceneFailure(
            "PSSM mean luminance change does not darken the scene"
        )
    if roi_darkened_fraction < MIN_ROI_DARKENED_FRACTION:
        raise BASE.BridgeSceneFailure(
            "PSSM cast-shadow ROI has insufficient darkening"
        )
    if (
        roi_strongly_darkened_fraction <
        MIN_ROI_STRONGLY_DARKENED_FRACTION
    ):
        raise BASE.BridgeSceneFailure(
            "PSSM cast-shadow ROI has insufficient strong darkening"
        )
    if roi_lightened_fraction > MAX_ROI_LIGHTENED_FRACTION:
        raise BASE.BridgeSceneFailure(
            "PSSM cast-shadow ROI has excessive lightening"
        )

    return {
        "changed_by_4_fraction": round(changed_fraction, 9),
        "darkened_by_4_fraction": round(darkened_fraction, 9),
        "darkened_by_12_fraction": round(
            strongly_darkened_fraction,
            9,
        ),
        "height": height,
        "lightened_by_4_fraction": round(lightened_fraction, 9),
        "mean_absolute_channel_delta": round(
            mean_absolute_channel_delta,
            9,
        ),
        "mean_signed_luminance_delta": round(
            mean_signed_luminance_delta,
            9,
        ),
        "roi": {
            "bottom": ROI_BOTTOM,
            "darkened_by_4_fraction": round(
                roi_darkened_fraction,
                9,
            ),
            "darkened_by_12_fraction": round(
                roi_strongly_darkened_fraction,
                9,
            ),
            "left": ROI_LEFT,
            "lightened_by_4_fraction": round(
                roi_lightened_fraction,
                9,
            ),
            "right": ROI_RIGHT,
            "top": ROI_TOP,
        },
        "width": width,
    }


def build_gateway_command(
    repository: Path,
    executable: Path,
    artifact_dir: Path,
    timeout: int,
    shadow_mode: str,
    runtime_content: Path | None,
) -> tuple[str, ...]:
    command = [
        sys.executable,
        str(repository / "tools/run_cityworld_gateway_scene.py"),
        "--executable",
        str(executable),
        "--repository",
        str(repository),
        "--artifact-dir",
        str(artifact_dir),
        "--timeout",
        str(timeout),
        "--shadow-mode",
        shadow_mode,
        "--shadow-quality",
        str(SHADOW_QUALITY),
    ]
    if runtime_content is not None:
        command.extend(("--runtime-content", str(runtime_content)))
    return tuple(command)


def run_gateway(
    command: Sequence[str],
    repository: Path,
    timeout: int,
    label: str,
) -> str:
    completed = BASE.run_command(
        command,
        timeout * 4,
        cwd=repository,
    )
    output = BASE.decode_output(completed.stdout)
    if completed.returncode != 0:
        raise BASE.BridgeSceneFailure(
            f"{label} child gate failed:\n{output}"
        )
    return output


def artifact_record(
    artifact_dir: Path,
    relative_path: str,
) -> dict[str, object]:
    path = artifact_dir / relative_path
    if not path.is_file() or path.is_symlink():
        raise BASE.BridgeSceneFailure(
            f"paired artifact is missing: {relative_path}"
        )
    size = path.stat().st_size
    if size <= 0:
        raise BASE.BridgeSceneFailure(
            f"paired artifact is empty: {relative_path}"
        )
    return {
        "path": relative_path,
        "sha256": BASE.sha256_file(path),
        "size": size,
    }


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument(
        "--repository",
        type=Path,
        default=REPOSITORY_ROOT,
    )
    parser.add_argument("--runtime-content", type=Path)
    parser.add_argument("--artifact-dir", required=True, type=Path)
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument(
        "--max-p95-ms",
        type=float,
        default=DEFAULT_MAX_P95_MS,
    )
    args = parser.parse_args(argv)
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if not math.isfinite(args.max_p95_ms) or args.max_p95_ms <= 0.0:
        parser.error("--max-p95-ms must be finite and positive")
    return args


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    repository = args.repository.resolve()
    executable = args.executable.resolve()
    runtime_content = (
        None
        if args.runtime_content is None
        else args.runtime_content.resolve()
    )
    artifact_dir = args.artifact_dir.resolve()
    if not executable.is_file():
        raise BASE.BridgeSceneFailure(
            f"RoR executable does not exist: {executable}"
        )
    if artifact_dir.exists():
        raise BASE.BridgeSceneFailure(
            "artifact directory already exists; choose a fresh path: "
            f"{artifact_dir}"
        )
    artifact_dir.mkdir(parents=True)
    diagnostics = artifact_dir / "diagnostics"
    diagnostics.mkdir()

    baseline_dir = artifact_dir / "no-shadows"
    pssm_dir = artifact_dir / "pssm"
    baseline_command = build_gateway_command(
        repository,
        executable,
        baseline_dir,
        args.timeout,
        "none",
        runtime_content,
    )
    baseline_stdout = run_gateway(
        baseline_command,
        repository,
        args.timeout,
        "no-shadow",
    )
    (diagnostics / "no-shadows.stdout").write_text(
        baseline_stdout,
        encoding="utf-8",
    )
    pssm_command = build_gateway_command(
        repository,
        executable,
        pssm_dir,
        args.timeout,
        "pssm",
        runtime_content,
    )
    pssm_stdout = run_gateway(
        pssm_command,
        repository,
        args.timeout,
        "PSSM",
    )
    (diagnostics / "pssm.stdout").write_text(
        pssm_stdout,
        encoding="utf-8",
    )

    baseline_report = BASE.load_json(baseline_dir / "report.json")
    pssm_report = BASE.load_json(pssm_dir / "report.json")
    validate_child_report(baseline_report, "none")
    validate_child_report(pssm_report, "pssm")
    physics = compare_identity(baseline_report, pssm_report)
    performance = compare_performance(
        baseline_report,
        pssm_report,
        args.max_p95_ms,
    )
    baseline_rgb_record, baseline_pixels = BASE.decode_rgb_png(
        baseline_dir / "rgb" / RGB_ARTIFACT_NAME
    )
    pssm_rgb_record, pssm_pixels = BASE.decode_rgb_png(
        pssm_dir / "rgb" / RGB_ARTIFACT_NAME
    )
    visual = compare_rgb(baseline_pixels, pssm_pixels)

    report = {
        "artifacts": {
            "baseline_report": artifact_record(
                artifact_dir,
                "no-shadows/report.json",
            ),
            "baseline_rgb": artifact_record(
                artifact_dir,
                "no-shadows/rgb/" + RGB_ARTIFACT_NAME,
            ),
            "baseline_stdout": artifact_record(
                artifact_dir,
                "diagnostics/no-shadows.stdout",
            ),
            "pssm_report": artifact_record(
                artifact_dir,
                "pssm/report.json",
            ),
            "pssm_rgb": artifact_record(
                artifact_dir,
                "pssm/rgb/" + RGB_ARTIFACT_NAME,
            ),
            "pssm_stdout": artifact_record(
                artifact_dir,
                "diagnostics/pssm.stdout",
            ),
        },
        "executable_sha256": baseline_report["executable_sha256"],
        "format": REPORT_FORMAT,
        "performance": performance,
        "physics": physics,
        "repository_commit": baseline_report["repository_commit"],
        "rgb": {
            "baseline": baseline_rgb_record,
            "comparison": visual,
            "pssm": pssm_rgb_record,
        },
        "runner": {
            "path": "tools/run_cityworld_shadow_scene.py",
            "sha256": BASE.sha256_file(
                repository / "tools/run_cityworld_shadow_scene.py"
            ),
        },
        "shadow_quality": SHADOW_QUALITY,
    }
    temporary = artifact_dir / "report.json.tmp"
    report_path = artifact_dir / "report.json"
    temporary.write_text(
        json.dumps(report, indent=2, ensure_ascii=True, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    os.replace(temporary, report_path)
    print(
        "CityWorld PSSM comparison gate passed: "
        f"darkened={visual['darkened_by_4_fraction']:.3%} "
        f"strong={visual['darkened_by_12_fraction']:.3%} "
        f"p95={performance['pssm_p95_ms']:.3f}ms "
        f"report={report_path}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except BASE.BridgeSceneFailure as error:
        print(f"CityWorld PSSM comparison gate failed: {error}", file=sys.stderr)
        raise SystemExit(1)
