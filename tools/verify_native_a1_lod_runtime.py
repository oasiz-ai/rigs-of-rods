#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify one Ogre-Next A1 runtime log and frame-budget receipt."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

from run_playable_performance_scene import (
    FATAL_LOG_MARKERS,
    PerformanceSceneFailure,
    verify_combined_native_distance_lod,
    verify_combined_presentation_ownership,
)


SCHEMA = "ror.native_a1_distance_lod_runtime.v1"
PACKAGE_ID = "rorng_a1_native_course_60m"
SELECTED_MARKER = (
    "[RoR|RendererCombined|NativeShowcase] Selected exact "
    "forward-native scene:"
)
SELECTED_PATTERN = re.compile(
    r"^\s+path='(?P<path>[^'\r\n]+)', "
    r"package='(?P<package>[^'\r\n]+)', "
    r"sha256='(?P<sha256>[0-9a-f]{64})', "
    r"assets=(?P<assets>[0-9]+), instances=(?P<instances>[0-9]+), "
    r"source_version=(?P<source_version>[0-9]+), "
    r"pipeline='(?P<pipeline>[^'\r\n]+)', hdr=(?P<hdr>true|false), "
    r"native_rt=(?P<native_rt>true|false), "
    r"profile=(?P<profile>[a-z0-9_]+), "
    r"motion='(?P<motion>[^'\r\n]+)', fixed_hz=(?P<fixed_hz>[0-9]+), "
    r"revolution_ticks=(?P<revolution_ticks>[0-9]+), "
    r"refraction=(?P<refraction>[a-z0-9_]+), "
    r"motion_vectors=(?P<motion_vectors>true|false)$"
)
MAX_LOG_BYTES = 32 * 1024 * 1024
MAX_FRAME_RECEIPT_BYTES = 1024 * 1024
MAX_PACKAGE_BYTES = 256 * 1024 * 1024
MAX_EXECUTABLE_BYTES = 512 * 1024 * 1024
METAL_RT_MARKER = "[RoR|RendererCombined|MetalRT|SunVisibilityV2] "
CANONICAL_FIELD = re.compile(r"(?P<name>[a-z][a-z0-9_]*)=(?P<value>[A-Za-z0-9_.+-]+)")
LIGHTING_MODES = {
    "metal-rt-sun-visibility-v2": {
        "platforms": frozenset(("darwin",)),
        "pipeline": "rt4_pbr_hdr_metal_sun_visibility_v2",
        "native_rt": "true",
        "native_scene_draws": 30,
    },
    "raster-hdr-pssm": {
        "platforms": frozenset(("linux", "win32")),
        "pipeline": "rt4_pbr_pssm_hdr_preview",
        "native_rt": "false",
        "native_scene_draws": 10,
    },
}


class ReceiptFailure(RuntimeError):
    """Stable expected verification failure."""


def canonical_sha256(value: str, field: str) -> str:
    if re.fullmatch(r"[0-9a-f]{64}", value) is None:
        raise ReceiptFailure(f"{field} must be canonical SHA-256")
    return value


def canonical_commit(value: str) -> str:
    if re.fullmatch(r"[0-9a-f]{40}", value) is None:
        raise ReceiptFailure("repository commit must be canonical Git SHA-1")
    return value


def read_regular_file(
    path: Path, label: str, maximum: int, *, allow_empty: bool = False
) -> bytes:
    if not path.is_absolute() or not path.is_file() or path.is_symlink():
        raise ReceiptFailure(f"{label} must be an absolute regular file")
    before = path.stat()
    if (
        before.st_size < 0
        or (before.st_size == 0 and not allow_empty)
        or before.st_size > maximum
    ):
        raise ReceiptFailure(f"{label} has an invalid size")
    payload = path.read_bytes()
    after = path.stat()
    if (
        len(payload) != before.st_size
        or before.st_dev != after.st_dev
        or before.st_ino != after.st_ino
        or before.st_size != after.st_size
        or before.st_mtime_ns != after.st_mtime_ns
    ):
        raise ReceiptFailure(f"{label} changed while it was read")
    return payload


def read_log(
    path: Path, label: str, *, allow_empty: bool = False
) -> tuple[bytes, str]:
    payload = read_regular_file(
        path, label, MAX_LOG_BYTES, allow_empty=allow_empty
    )
    try:
        return payload, payload.decode("utf-8")
    except UnicodeDecodeError as error:
        raise ReceiptFailure(f"{label} is not UTF-8") from error


def read_frame_receipt(path: Path) -> tuple[bytes, dict[str, object]]:
    payload = read_regular_file(
        path, "frame receipt", MAX_FRAME_RECEIPT_BYTES
    )
    def unique_object(pairs: list[tuple[str, object]]) -> dict[str, object]:
        result: dict[str, object] = {}
        for name, value in pairs:
            if name in result:
                raise ReceiptFailure(f"frame receipt repeats field {name}")
            result[name] = value
        return result
    try:
        value = json.loads(payload, object_pairs_hook=unique_object)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ReceiptFailure("frame receipt is not valid UTF-8 JSON") from error
    if not isinstance(value, dict):
        raise ReceiptFailure("frame receipt is not a JSON object")
    return payload, value


def canonical_line_fields(line: str, marker: str, label: str) -> dict[str, str]:
    if not line.startswith(marker):
        raise ReceiptFailure(f"{label} line does not start canonically")
    body = line[len(marker) :]
    tokens = body.split(" ")
    if not tokens or any(not token for token in tokens):
        raise ReceiptFailure(f"{label} line has non-canonical spacing")
    fields: dict[str, str] = {}
    for token in tokens:
        match = CANONICAL_FIELD.fullmatch(token)
        if match is None:
            raise ReceiptFailure(f"{label} line contains non-canonical text")
        name = match.group("name")
        if name in fields:
            raise ReceiptFailure(f"{label} line repeats field {name}")
        fields[name] = match.group("value")
    return fields


def verify_metal_rt_receipts(text: str, accepted_frames: int) -> dict[str, object]:
    lines = [line for line in text.splitlines() if METAL_RT_MARKER in line]
    if not lines:
        raise ReceiptFailure("Metal lighting mode emitted no SunVisibilityV2 receipt")
    parsed = [
        canonical_line_fields(line, METAL_RT_MARKER, "Metal SunVisibilityV2")
        for line in lines
    ]
    required = {
        "schema_version",
        "frame",
        "snapshot",
        "view",
        "plan",
        "selected",
        "admitted",
        "excluded",
        "receivers",
        "casters",
        "unique_meshes",
        "blas_build",
        "blas_hit",
        "blas_refit",
        "tlas_build",
        "tlas_hit",
        "tlas_refit",
        "primary_rays",
        "sun_rays",
        "visible_texels",
        "occluded_texels",
        "gpu_ns",
        "supports_rt",
        "apple_family9",
        "same_ogre_device",
        "same_ogre_queue",
        "same_ogre_timeline",
        "shader_lock",
        "sun_direct_only",
        "completed",
        "cpu_content_readbacks",
        "gpu_content_readbacks",
        "completed_frames",
    }
    numeric = required - {
        "supports_rt",
        "apple_family9",
        "same_ogre_device",
        "same_ogre_queue",
        "same_ogre_timeline",
        "shader_lock",
        "sun_direct_only",
        "completed",
    }
    frames: list[int] = []
    first_view: int | None = None
    first_plan: int | None = None
    for receipt_index, fields in enumerate(parsed):
        missing = sorted(required - fields.keys())
        if missing:
            raise ReceiptFailure(
                "Metal SunVisibilityV2 receipt is missing: " + ", ".join(missing)
            )
        extra = sorted(fields.keys() - required)
        if extra:
            raise ReceiptFailure(
                "Metal SunVisibilityV2 receipt has unknown fields: "
                + ", ".join(extra)
            )
        try:
            numbers = {name: int(fields[name]) for name in numeric}
        except ValueError as error:
            raise ReceiptFailure(
                "Metal SunVisibilityV2 receipt contains a non-integer counter"
            ) from error
        if numbers["schema_version"] != 2:
            raise ReceiptFailure("Metal SunVisibilityV2 schema changed")
        if numbers["frame"] != numbers["snapshot"] or numbers["frame"] != numbers["completed_frames"]:
            raise ReceiptFailure("Metal SunVisibilityV2 frame lineage changed")
        if numbers["view"] <= 0 or numbers["plan"] <= 0:
            raise ReceiptFailure("Metal SunVisibilityV2 view or plan is invalid")
        if first_view is None:
            first_view = numbers["view"]
            first_plan = numbers["plan"]
        elif numbers["view"] != first_view or numbers["plan"] != first_plan:
            raise ReceiptFailure("Metal SunVisibilityV2 view or plan changed")
        if (
            numbers["selected"] != 9
            or numbers["admitted"] != 7
            or numbers["excluded"] != 2
            or numbers["receivers"] != 7
            or numbers["casters"] != 4
            or numbers["unique_meshes"] != 7
        ):
            raise ReceiptFailure("Metal SunVisibilityV2 A1 scene counters changed")
        first_receipt = receipt_index == 0
        if (
            numbers["blas_build"] != (7 if first_receipt else 0)
            or numbers["blas_hit"] != (0 if first_receipt else 7)
            or numbers["blas_refit"] != 0
            or numbers["tlas_build"] != (1 if first_receipt else 0)
            or numbers["tlas_hit"] != (0 if first_receipt else 1)
            or numbers["tlas_refit"] != 0
        ):
            raise ReceiptFailure("Metal SunVisibilityV2 acceleration reuse changed")
        if (
            numbers["primary_rays"] <= 0
            or numbers["sun_rays"] <= 0
            or numbers["gpu_ns"] <= 0
            or numbers["visible_texels"] + numbers["occluded_texels"]
            != numbers["primary_rays"]
        ):
            raise ReceiptFailure("Metal SunVisibilityV2 ray counters are invalid")
        for name in (
            "supports_rt",
            "apple_family9",
            "same_ogre_device",
            "same_ogre_queue",
            "same_ogre_timeline",
            "shader_lock",
            "sun_direct_only",
            "completed",
        ):
            if fields[name] != "true":
                raise ReceiptFailure(f"Metal SunVisibilityV2 {name} is not true")
        if numbers["cpu_content_readbacks"] != 0 or numbers["gpu_content_readbacks"] != 0:
            raise ReceiptFailure("Metal SunVisibilityV2 performed a production readback")
        frames.append(numbers["frame"])
    if frames != list(range(1, frames[-1] + 1)):
        raise ReceiptFailure("Metal SunVisibilityV2 frame sequence is not exact")
    minimum_completed = accepted_frames + 3
    if frames[-1] < minimum_completed:
        raise ReceiptFailure(
            "Metal SunVisibilityV2 completed fewer frames than the bounded receipt"
        )
    return {
        "completed_frames": frames[-1],
        "frame_sequence_exact": True,
        "receipt_count": len(parsed),
        "selected_instances": 9,
        "admitted_instances": 7,
        "production_readbacks": 0,
    }


def build_receipt(
    text: str,
    log_bytes: bytes,
    stderr_text: str,
    stderr_bytes: bytes,
    frame_receipt: dict[str, object],
    frame_receipt_bytes: bytes,
    package_bytes: bytes,
    executable_bytes: bytes,
    *,
    platform: str,
    lighting_mode: str,
    package_path: Path,
    package_sha256: str,
    repository_commit: str,
    scenario_id: str,
    accepted_frames: int,
    expected: dict[str, int],
) -> dict[str, object]:
    lighting = LIGHTING_MODES[lighting_mode]
    diagnostics = text + "\n" + stderr_text
    forbidden_diagnostics = (
        *FATAL_LOG_MARKERS,
        "[RoR|Perf] Refusing frame budget:",
        "[RoR|RendererCombined|Scene] Snapshot not presented",
    )
    found_diagnostics = sorted(
        marker for marker in forbidden_diagnostics if marker in diagnostics
    )
    if found_diagnostics:
        raise ReceiptFailure(
            "runtime emitted fatal or refusal diagnostics: "
            + ", ".join(found_diagnostics)
        )
    for marker in (
        "[RoR|RendererCombined|Startup]",
        SELECTED_MARKER,
        "[RoR|RendererCombined|NativeLighting]",
        METAL_RT_MARKER,
    ):
        if marker in stderr_text:
            raise ReceiptFailure(
                "runtime stderr contains an authoritative receipt marker"
            )
    selected = [line for line in text.splitlines() if SELECTED_MARKER in line]
    if len(selected) != 1:
        raise ReceiptFailure(
            "runtime did not select exactly one forward-native showcase"
        )
    selected_line = selected[0]
    selected_body = selected_line.split(SELECTED_MARKER, 1)[1]
    selected_match = SELECTED_PATTERN.fullmatch(selected_body)
    if selected_match is None:
        raise ReceiptFailure("selected showcase line is not canonical")
    fields = selected_match.groupdict()
    selected_expected = {
        "assets": "38",
        "fixed_hz": "60",
        "hdr": "true",
        "instances": "9",
        "package": PACKAGE_ID,
        "pipeline": lighting["pipeline"],
        "sha256": package_sha256,
        "source_version": "1",
        "profile": "a1_native_course",
        "motion": "turntable_thin_glass_slab",
        "motion_vectors": "false",
        "native_rt": lighting["native_rt"],
        "refraction": "thin_parallel_slab_screen_space",
        "revolution_ticks": "360",
    }
    wrong_selected = {
        field: (fields.get(field), value)
        for field, value in selected_expected.items()
        if fields.get(field) != value
    }
    if wrong_selected:
        raise ReceiptFailure(
            "selected showcase identity changed: "
            + ", ".join(
                f"{field}={actual!r}, expected {value!r}"
                for field, (actual, value) in sorted(wrong_selected.items())
            )
        )
    try:
        selected_path = Path(fields["path"]).resolve(strict=True)
        canonical_package_path = package_path.resolve(strict=True)
    except OSError as error:
        raise ReceiptFailure("selected showcase path cannot be resolved") from error
    if selected_path != canonical_package_path:
        raise ReceiptFailure("selected showcase path does not name the staged package")
    if text.find(SELECTED_MARKER) >= text.rfind(
        "[RoR|RendererCombined|NativeLighting]"
    ):
        raise ReceiptFailure("native lighting receipt does not follow A1 selection")

    try:
        ownership = verify_combined_presentation_ownership(text, platform)
        native = verify_combined_native_distance_lod(text)
    except PerformanceSceneFailure as error:
        raise ReceiptFailure(str(error)) from error
    wrong = {
        field: (native.get(field), value)
        for field, value in expected.items()
        if native.get(field) != value
    }
    if wrong:
        raise ReceiptFailure(
            "A1 native LOD selection changed: "
            + ", ".join(
                f"{field}={actual!r}, expected {value!r}"
                for field, (actual, value) in sorted(wrong.items())
            )
        )
    mode_native_expected: dict[str, object]
    if lighting_mode == "raster-hdr-pssm":
        mode_native_expected = {
            "pbs": 9,
            "casters": 4,
            "receivers": 7,
            "hdr_topology": 1,
            "pssm": True,
            "pssm_populated_finalize": True,
        }
    else:
        mode_native_expected = {
            "pbs": 9,
            "casters": 0,
            "receivers": 0,
            "hdr_topology": 0,
            "pssm": False,
            "pssm_populated_finalize": False,
        }
    wrong_mode_native = {
        field: (native.get(field), value)
        for field, value in mode_native_expected.items()
        if native.get(field) != value
    }
    if wrong_mode_native:
        raise ReceiptFailure(
            "A1 native lighting topology changed: "
            + ", ".join(
                f"{field}={actual!r}, expected {value!r}"
                for field, (actual, value) in sorted(
                    wrong_mode_native.items()
                )
            )
        )
    if native.get("reduced_this_frame") is not True:
        raise ReceiptFailure("A1 native LOD ladder was present but not reduced")

    metal_rt: dict[str, object] | None = None
    if lighting_mode == "metal-rt-sun-visibility-v2":
        metal_rt = verify_metal_rt_receipts(text, accepted_frames)
        if text.find(METAL_RT_MARKER) <= text.rfind(
            "[RoR|RendererCombined|NativeLighting]"
        ):
            raise ReceiptFailure("Metal RT completion does not follow native lighting")
    elif METAL_RT_MARKER in text:
        raise ReceiptFailure("raster lighting mode emitted a Metal RT receipt")

    frame_expected: dict[str, object] = {
        "format": "ror-frame-time-budget-v1",
        "mode": "measure",
        "verdict": "advisory",
        "passed": False,
        "scenario_id": scenario_id,
        "renderer": "ogre-next-combined",
        "presents_frames": True,
        "requires_native_scene_draw_metrics": True,
        "requested_frames": accepted_frames,
        "observed_frames": accepted_frames + 2,
        "warmup_frames_requested": 2,
        "warmup_frames": 2,
        "minimum_frames": 8,
        "accepted_frames": accepted_frames,
        "rejected_frames": 0,
        "native_scene_draw_exact_samples": accepted_frames,
        "native_scene_draw_rejected_samples": 0,
    }
    wrong_frame = {
        field: (frame_receipt.get(field), value)
        for field, value in frame_expected.items()
        if type(frame_receipt.get(field)) is not type(value)
        or frame_receipt.get(field) != value
    }
    if wrong_frame:
        raise ReceiptFailure(
            "frame-budget receipt changed: "
            + ", ".join(
                f"{field}={actual!r}, expected {value!r}"
                for field, (actual, value) in sorted(wrong_frame.items())
            )
        )
    draw_p99 = frame_receipt.get("native_scene_draw_p99")
    draw_maximum = frame_receipt.get("native_scene_draw_maximum")
    draw_limit = frame_receipt.get("native_scene_draw_p99_limit")
    expected_draws = lighting["native_scene_draws"]
    if (
        not isinstance(draw_p99, int)
        or isinstance(draw_p99, bool)
        or not isinstance(draw_maximum, int)
        or isinstance(draw_maximum, bool)
        or not isinstance(draw_limit, int)
        or isinstance(draw_limit, bool)
        or draw_p99 != expected_draws
        or draw_maximum != expected_draws
        or draw_limit != 2500
    ):
        raise ReceiptFailure(
            "frame-budget native scene draw counters changed from "
            f"{expected_draws}/{expected_draws}/2500 for {lighting_mode}"
        )

    actual_package_sha256 = hashlib.sha256(package_bytes).hexdigest()
    if actual_package_sha256 != package_sha256:
        raise ReceiptFailure(
            "staged package SHA-256 does not match the selected package"
        )

    return {
        "evidence_class": "runtime-input-and-receipt-verification",
        "executable_sha256": hashlib.sha256(executable_bytes).hexdigest(),
        "frame_budget": {
            **frame_expected,
            "native_scene_draw_p99": draw_p99,
            "native_scene_draw_maximum": draw_maximum,
            "native_scene_draw_p99_limit": draw_limit,
            "sha256": hashlib.sha256(frame_receipt_bytes).hexdigest(),
        },
        "lighting_mode": {
            "id": lighting_mode,
            "pipeline": lighting["pipeline"],
            "native_rt": lighting["native_rt"] == "true",
        },
        "log_sha256": hashlib.sha256(log_bytes).hexdigest(),
        "stderr_log_sha256": hashlib.sha256(stderr_bytes).hexdigest(),
        "metal_rt_sun_visibility_v2": metal_rt,
        "native_distance_lod": native,
        "package": {
            "bytes": len(package_bytes),
            "id": PACKAGE_ID,
            "sha256": actual_package_sha256,
        },
        "platform": platform,
        "presentation_ownership": ownership,
        "repository_commit": repository_commit,
        "schema": SCHEMA,
        "selected_showcase_line_sha256": hashlib.sha256(
            selected_line.encode("utf-8")
        ).hexdigest(),
    }


def arguments(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", required=True, type=Path)
    parser.add_argument("--stderr-log", required=True, type=Path)
    parser.add_argument("--frame-budget-receipt", required=True, type=Path)
    parser.add_argument("--package", required=True, type=Path)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--platform", required=True, choices=("darwin", "linux", "win32"))
    parser.add_argument(
        "--expected-lighting-mode",
        required=True,
        choices=tuple(LIGHTING_MODES),
    )
    parser.add_argument("--package-sha256", required=True)
    parser.add_argument("--repository-commit", required=True)
    parser.add_argument("--scenario-id", required=True)
    parser.add_argument("--accepted-frames", required=True, type=int)
    for field in (
        "lod-items",
        "lod-reduced",
        "lod-max",
        "lod-level-sum",
        "triangles-base",
        "triangles-selected",
        "completed-frames",
    ):
        parser.add_argument(f"--expected-{field}", required=True, type=int)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = arguments(sys.argv[1:] if argv is None else argv)
    try:
        package_sha256 = canonical_sha256(args.package_sha256, "package SHA-256")
        repository_commit = canonical_commit(args.repository_commit)
        if args.platform != sys.platform:
            raise ReceiptFailure(
                f"platform {args.platform!r} does not match host {sys.platform!r}"
            )
        lighting = LIGHTING_MODES[args.expected_lighting_mode]
        if args.platform not in lighting["platforms"]:
            raise ReceiptFailure(
                f"lighting mode {args.expected_lighting_mode!r} is invalid on "
                f"platform {args.platform!r}"
            )
        if re.fullmatch(r"[a-z0-9][a-z0-9._-]{0,127}", args.scenario_id) is None:
            raise ReceiptFailure("scenario id is not canonical")
        if not args.output.is_absolute() or args.output.exists():
            raise ReceiptFailure("output must be a new absolute path")
        if not args.output.parent.is_dir() or args.output.parent.is_symlink():
            raise ReceiptFailure("output parent must be an existing regular directory")
        expected = {
            "lod_items": args.expected_lod_items,
            "lod_reduced": args.expected_lod_reduced,
            "lod_max": args.expected_lod_max,
            "lod_level_sum": args.expected_lod_level_sum,
            "triangles_base": args.expected_triangles_base,
            "triangles_selected": args.expected_triangles_selected,
            "completed_frames": args.expected_completed_frames,
        }
        if args.accepted_frames <= 0 or any(value < 0 for value in expected.values()):
            raise ReceiptFailure("expected frame and LOD counters are invalid")
        log_bytes, text = read_log(args.log, "runtime stdout log")
        stderr_bytes, stderr_text = read_log(
            args.stderr_log, "runtime stderr log", allow_empty=True
        )
        frame_receipt_bytes, frame_receipt = read_frame_receipt(
            args.frame_budget_receipt
        )
        package_bytes = read_regular_file(
            args.package, "staged package", MAX_PACKAGE_BYTES
        )
        executable_bytes = read_regular_file(
            args.executable, "runtime executable", MAX_EXECUTABLE_BYTES
        )
        receipt = build_receipt(
            text,
            log_bytes,
            stderr_text,
            stderr_bytes,
            frame_receipt,
            frame_receipt_bytes,
            package_bytes,
            executable_bytes,
            platform=args.platform,
            lighting_mode=args.expected_lighting_mode,
            package_path=args.package,
            package_sha256=package_sha256,
            repository_commit=repository_commit,
            scenario_id=args.scenario_id,
            accepted_frames=args.accepted_frames,
            expected=expected,
        )
        encoded = (
            json.dumps(receipt, ensure_ascii=True, indent=2, sort_keys=True) + "\n"
        )
        with args.output.open("x", encoding="ascii", newline="\n") as stream:
            stream.write(encoded)
        print(json.dumps(receipt, ensure_ascii=True, sort_keys=True, separators=(",", ":")))
        return 0
    except (OSError, ReceiptFailure) as error:
        print(f"native A1 LOD receipt failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
