#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run and gate the playable frame-time budget of a pinned scene.

The roadmap's CityWorld visual milestone and the Ogre-Next combined-runtime
milestone both declare a sustained frame-rate budget with no measurement seam.
This tool is that seam's driver: it stages an isolated profile at an exact
requested resolution with the frame-rate limiter and VSync disabled, runs the
already built executable for a fixed number of recorded frames, and accepts
only a complete `ror-frame-time-budget-v1` receipt that matches the request.

The tool performs no downloads, never writes inside the repository, and never
rewrites or reuses a receipt. Anything it cannot prove is a failure, not a
warning.
"""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import time
from typing import Mapping, Sequence


RECEIPT_FORMAT = "ror-frame-time-budget-v1"
REPORT_FORMAT = "ror-playable-performance-run-v1"

#: Reserved by `kFrameTimeBudgetFailureExitCode`; 73/74 belong to the renderer
#: child contract.
BUDGET_FAILURE_EXIT_CODE = 75

MAX_RECEIPT_BYTES = 64 * 1024
MAX_LOG_BYTES = 256 * 1024 * 1024

#: A renderer fault always invalidates the measurement: a run that is losing
#: draw calls or dying is not the scene we budgeted.  A combined-runtime
#: capture rejection is handled separately: one may occur while the actor's
#: authenticated material authority is still being published, but it is
#: accepted only when a later final native frame proves the visible scene and
#: exact native distance-LOD state. The current camera is allowed to select
#: base detail; camera placement is not a renderer-availability gate.
FATAL_LOG_MARKERS = (
    "Validation Failed: Sampler error:",
    "GL_INVALID_",
    "RenderingAPIException",
    "OGRE EXCEPTION",
    "EXC_BAD_ACCESS",
    "Segmentation fault",
)

CAPTURE_REJECTED_MARKER = "status='capture_rejected'"

#: Content diagnostics are always counted and reported, but only gated when the
#: caller asks. The pinned baseline content emits some of these, so making them
#: fatal by default would measure the fixture rather than the renderer. The
#: CityWorld visual milestone, which declares that no missing-material
#: diagnostic may appear, passes `--require-clean-content`.
CONTENT_LOG_MARKERS = (
    "has no supportable Techniques",
    "Can't assign material",
    "Cannot locate resource",
    "Could not load texture",
)

RENDERER_IDENTITY_PATTERNS = {
    "device": re.compile(r"Device Name: (?P<value>[^\r\n]+)"),
    "render_system": re.compile(r"RenderSystem Name: (?P<value>[^\r\n]+)"),
    "vendor": re.compile(r"GPU Vendor: (?P<value>[^\r\n]+)"),
}

GL3PLUS_RENDER_SYSTEM = "OpenGL 3+ Rendering Subsystem"
D3D11_RENDER_SYSTEM = "Direct3D11 Rendering Subsystem"

class PerformanceSceneFailure(RuntimeError):
    """Fail-closed diagnostic for an invalid performance run or artifact."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def is_macos_app_bundle_executable(executable: Path) -> bool:
    """Mirror `IsMacOSAppBundleProcessDirectory` on the executable's parent."""

    process_dir = executable.parent
    if process_dir.name != "MacOS" or process_dir.parent.name != "Contents":
        return False
    bundle_root = process_dir.parent.parent.name
    return len(bundle_root) > 4 and bundle_root.endswith(".app")


def runtime_layout(
    isolated_home: Path,
    target_platform: str,
    executable: Path | None = None,
) -> dict[str, Path]:
    """Mirror the game's own per-platform user-directory layout.

    macOS resolves two different trees: a packaged `.app` uses Apple's Library
    directories, while a development build writes `~/RigsOfRods`. Guessing
    wrong would stage the run's configuration where the game never reads it, so
    the same rule is reproduced here from the executable path.
    """

    if target_platform == "darwin":
        if executable is None:
            raise PerformanceSceneFailure(
                "the macOS layout requires the executable path"
            )
        if is_macos_app_bundle_executable(executable):
            user = (
                isolated_home
                / "Library" / "Application Support" / "Rigs of Rods"
            )
            logs = isolated_home / "Library" / "Logs" / "Rigs of Rods"
        else:
            # A development build with its own `config` directory would write
            # inside the build tree and escape this run's isolation.
            if (executable.parent / "config").is_dir():
                raise PerformanceSceneFailure(
                    "the executable directory contains a portable `config` "
                    f"directory, which escapes the isolated profile: "
                    f"{executable.parent}"
                )
            user = isolated_home / "RigsOfRods"
            logs = user / "logs"
    elif target_platform == "win32":
        user = isolated_home / "My Games" / "Rigs of Rods"
        logs = user / "logs"
    elif target_platform == "linux":
        user = isolated_home / ".rigsofrods"
        logs = user / "logs"
    else:
        raise PerformanceSceneFailure(
            f"unsupported runtime platform: {target_platform}"
        )
    return {
        "config": user / "config",
        "logs": logs,
        "mods": user / "mods",
        "screenshots": user / "screenshots",
        "user": user,
    }


#: Explicit graphics presets. A budget measured against "whatever the config
#: defaulted to" is not reproducible, so every run names and pins its settings
#: and the chosen preset is copied into the run report. `high` is the preset
#: the roadmap's macOS arm64 CityWorld budget is declared against: PSSM
#: shadows at the highest quality, full vegetation, reflection + refraction
#: water, anisotropic filtering, and dynamic reflections every frame.
#: Explicit graphics presets. A budget measured against "whatever the config
#: defaulted to" is not reproducible, so every run names and pins its settings,
#: the chosen preset is copied into the run report, and the effective values
#: are re-read from the runtime log.
#:
#: Each entry is (value written to RoR.cfg, effective CVar value expected back).
#: The two differ because `AppConfig`'s `ParseHelper` maps enum-valued settings
#: from their exact display strings and silently selects the first enumerator
#: for anything it does not recognize: writing "1" for `gfx_shadow_type` turns
#: shadows off rather than selecting PSSM. Settings that fall through to the
#: generic assignment keep their raw values.
GRAPHICS_PRESETS = {
    "high": {
        "gfx_shadow_type": ("Parallel-split Shadow Maps", "1"),
        "gfx_shadow_quality": (3, "3"),
        "gfx_texture_filter": ("Anisotropic (best looking)", "3"),
        "gfx_anisotropy": (8, "8"),
        "gfx_vegetation_mode": ("Full (best looking, slower)", "3"),
        "gfx_water_mode": (
            "Reflection + refraction (speed optimized)", "3"),
        # Caelum is not compiled into every configuration; Sandstorm is the
        # portable sky every build can honour.
        "gfx_sky_mode": ("Sandstorm (fastest)", "0"),
        "gfx_envmap_enabled": ("true", "1"),
        "gfx_envmap_rate": (1, "1"),
        "gfx_particles_mode": (1, "1"),
        "gfx_skidmarks_mode": (1, "1"),
        "gfx_sight_range": (5000, "5000"),
        "gfx_postprocess_mode": (0, "0"),
        "gfx_auto_lod": ("true", "1"),
    },
    "low": {
        "gfx_shadow_type": ("No shadows (fastest)", "0"),
        "gfx_shadow_quality": (0, "0"),
        "gfx_texture_filter": ("Bilinear", "1"),
        "gfx_anisotropy": (1, "1"),
        "gfx_vegetation_mode": ("None (fastest)", "0"),
        "gfx_water_mode": ("Basic (fastest)", "1"),
        "gfx_sky_mode": ("Sandstorm (fastest)", "0"),
        "gfx_envmap_enabled": ("false", "0"),
        "gfx_envmap_rate": (0, "0"),
        "gfx_particles_mode": (0, "0"),
        "gfx_skidmarks_mode": (0, "0"),
        "gfx_sight_range": (2000, "2000"),
        "gfx_postprocess_mode": (0, "0"),
        "gfx_auto_lod": ("true", "1"),
    },
}

# The combined executable renders shadows and reflections only in Ogre-Next.
# Its hidden Ogre 14 scene producer disables the duplicate RTT passes after the
# presenter has consumed the requested configuration.  These are therefore
# expected producer values, not evidence that the visible renderer is in a low
# quality mode.
COMBINED_PRODUCER_OVERRIDES = {
    "gfx_shadow_type": "0",
    "gfx_envmap_enabled": "0",
    "gfx_envmap_rate": "0",
}

#: The runtime's authoritative one-line statement of its effective graphics
#: settings, emitted when the budget is armed. Parsing this instead of the
#: per-CVar assignment lines avoids the ambiguity that a setting already at its
#: target value is never logged as a change.
GRAPHICS_STATEMENT_PATTERN = re.compile(
    r"\[RoR\|Perf\] Graphics:(?P<body>[^\r\n]+)"
)
GRAPHICS_SETTING_PATTERN = re.compile(
    r"(?P<name>gfx_[a-z_]+)=(?P<value>\S+)"
)
NATIVE_LIGHTING_PATTERN = re.compile(
    r"\[RoR\|RendererCombined\|NativeLighting\](?P<body>[^\r\n]+)"
)
NATIVE_LIGHTING_FIELD_PATTERN = re.compile(
    r"(?P<name>[a-z][a-z0-9_]*)=(?P<value>\S+)"
)
SCENE_SOURCE_TIMING_PATTERN = re.compile(
    r"\[RoR\|SceneSource\] captures=(?P<captures>[0-9]+) mean_ns "
    r"(?P<body>[^\r\n]+)"
)
SCENE_SOURCE_TIMING_FIELD_PATTERN = re.compile(
    r"(?P<name>[a-z][a-z0-9_]*)=(?P<value>[0-9]+)"
)
SCENE_SOURCE_REQUIRED_TIMINGS = (
    "terrain",
    "static",
    "dynamic",
    "retained",
    "merge",
    "union",
    "particles",
    "material_apply",
    "other",
    "material_index",
    "material_plan",
    "material_authority",
    "material_owners",
    "material_finalize",
)
OGRE_NEXT_PRESENTATION_OWNER_RECEIPT = (
    "[RoR|RendererCombined|Startup] presentation_owner=ogre-next "
    "visible_window=true legacy_visible_fallback=false"
)
OGRE_NEXT_PRESENTATION_BACKEND_PATTERN = re.compile(
    re.escape(OGRE_NEXT_PRESENTATION_OWNER_RECEIPT)
    + r" backend=(?P<backend>ogre-next-(?:metal|vulkan|d3d11))"
)
OGRE14_HIDDEN_RESOURCE_HOST_RECEIPT = (
    "[RoR|RendererCombined|Startup] resource_host=ogre14 "
    "visible_window=false protected=true"
)
ACTOR_CONTROL_PATTERN = re.compile(
    r"\[RoR\|RendererCombined\|ActorControl\](?P<body>[^\r\n]+)"
)
ACTOR_CONTROL_FIELD_PATTERN = re.compile(
    r"(?P<name>[a-z][a-z0-9_]*)=(?P<value>\S+)"
)
ACTOR_CONTROL_SPAWN_MARKER = "== Spawning vehicle:"
ACTOR_CONTROL_PRESENTED_SCENE_MARKER = (
    "[RoR|RendererCombined|RetainedScene]"
)


def effective_graphics_settings(text: str) -> dict[str, str]:
    """Read the runtime's own statement of its effective graphics settings."""

    bodies = GRAPHICS_STATEMENT_PATTERN.findall(text)
    if not bodies:
        raise PerformanceSceneFailure(
            "the runtime log does not state its effective graphics settings"
        )
    if len(bodies) > 1:
        raise PerformanceSceneFailure(
            f"the runtime stated its graphics settings {len(bodies)} times"
        )
    return {
        match.group("name"): match.group("value")
        for match in GRAPHICS_SETTING_PATTERN.finditer(bodies[0])
    }


def verify_graphics_preset(
    text: str,
    preset_name: str,
    *,
    combined_runtime: bool = False,
) -> dict[str, str]:
    """Prove the run actually used the preset it was asked for.

    Writing a setting is not the same as it taking effect. A value the config
    parser does not recognize is replaced silently, so a run can report a
    "high" preset while rendering with shadows disabled. Compare what the
    runtime states it used, not what was requested.
    """

    try:
        preset = GRAPHICS_PRESETS[preset_name]
    except KeyError as error:
        raise PerformanceSceneFailure(
            f"unknown graphics preset: {preset_name}"
        ) from error

    observed = effective_graphics_settings(text)
    missing = sorted(name for name in preset if name not in observed)
    if missing:
        raise PerformanceSceneFailure(
            "the runtime did not state these settings: " + ", ".join(missing)
        )

    expected_values = {
        name: expected for name, (_, expected) in preset.items()
    }
    if combined_runtime:
        expected_values.update(COMBINED_PRODUCER_OVERRIDES)
    wrong = {
        name: (observed[name], expected)
        for name, expected in expected_values.items()
        if observed[name] != expected
    }
    if wrong:
        raise PerformanceSceneFailure(
            f"the {preset_name} preset did not take effect: "
            + ", ".join(
                f"{name} is {actual!r}, expected {expected!r}"
                for name, (actual, expected) in sorted(wrong.items())
            )
        )
    return {name: observed[name] for name in preset}


def verify_combined_native_distance_lod(text: str) -> dict[str, object]:
    """Prove the visible Ogre-Next frame owns an exact native LOD state.

    The portable ladder and focused selector fixture are useful development
    checks, but they do not get to decide whether the production renderer may
    run. The playable gate consumes the final native frame receipt and requires
    a published ladder plus internally consistent base-or-reduced selection
    while the visible Ogre-Next PSSM/reflection path is active.
    """

    native_receipts = list(NATIVE_LIGHTING_PATTERN.finditer(text))
    if not native_receipts:
        raise PerformanceSceneFailure(
            "the combined runtime emitted no native lighting/LOD receipt"
        )
    final_native_receipt = native_receipts[-1]
    final_capture_rejection = text.rfind(CAPTURE_REJECTED_MARKER)
    if final_capture_rejection > final_native_receipt.start():
        raise PerformanceSceneFailure(
            "the final combined-runtime capture rejection was not recovered "
            "by a later native lighting/LOD frame"
        )
    fields = {
        match.group("name"): match.group("value")
        for match in NATIVE_LIGHTING_FIELD_PATTERN.finditer(
            final_native_receipt.group("body")
        )
    }
    required = {
        "schema_version",
        "available",
        "pbs",
        "casters",
        "lod_items",
        "lod_reduced",
        "lod_max",
        "lod_level_sum",
        "triangles_base",
        "triangles_selected",
        "lod_exact",
        "pssm",
        "reflection_initialized",
        "native_scene_lighting",
        "gpu_only",
        "no_ogre14_lighting",
        "completed_frames",
    }
    missing = sorted(required - fields.keys())
    if missing:
        raise PerformanceSceneFailure(
            "the native lighting/LOD receipt is missing: "
            + ", ".join(missing)
        )

    true_fields = (
        "available",
        "lod_exact",
        "reflection_initialized",
        "native_scene_lighting",
        "gpu_only",
        "no_ogre14_lighting",
    )
    false_values = sorted(name for name in true_fields if fields[name] != "true")
    if false_values:
        raise PerformanceSceneFailure(
            "the visible Ogre-Next quality/LOD path is incomplete: "
            + ", ".join(
                f"{name}={fields[name]!r}" for name in false_values
            )
        )

    numeric_names = (
        "schema_version",
        "pbs",
        "casters",
        "lod_items",
        "lod_reduced",
        "lod_max",
        "lod_level_sum",
        "triangles_base",
        "triangles_selected",
        "completed_frames",
    )
    try:
        numbers = {name: int(fields[name]) for name in numeric_names}
    except ValueError as error:
        raise PerformanceSceneFailure(
            "the native lighting/LOD receipt contains a non-integer counter"
        ) from error
    if numbers["schema_version"] < 6:
        raise PerformanceSceneFailure(
            "the native lighting receipt predates exact distance-LOD audit v6"
        )
    if numbers["pbs"] <= 0 or numbers["completed_frames"] <= 0:
        raise PerformanceSceneFailure(
            "the native receipt does not describe a completed PBS frame"
        )
    if numbers["casters"] < 0:
        raise PerformanceSceneFailure(
            "the native receipt contains a negative shadow-caster count"
        )
    if numbers["casters"] > 0 and fields["pssm"] != "true":
        raise PerformanceSceneFailure(
            "the visible Ogre-Next quality path omitted PSSM while the scene "
            "contained native shadow casters"
        )
    lod_counter_names = (
        "lod_items",
        "lod_reduced",
        "lod_max",
        "lod_level_sum",
    )
    if any(numbers[name] < 0 for name in lod_counter_names):
        raise PerformanceSceneFailure(
            "the native distance-LOD receipt contains a negative counter"
        )
    if numbers["lod_items"] <= 0:
        raise PerformanceSceneFailure(
            "the visible scene published no native distance-LOD ladders"
        )
    if numbers["lod_reduced"] > numbers["lod_items"]:
        raise PerformanceSceneFailure(
            "the native LOD reduced-item count exceeds its ladder-item count"
        )
    if numbers["triangles_base"] <= 0 or not (
        0 < numbers["triangles_selected"] <= numbers["triangles_base"]
    ):
        raise PerformanceSceneFailure(
            "native distance LOD published an invalid visible triangle total"
    )
    reduced_this_frame = numbers["lod_reduced"] > 0
    if reduced_this_frame:
        if (
            numbers["lod_max"] <= 0
            or numbers["lod_level_sum"] < numbers["lod_reduced"]
            or numbers["triangles_selected"] >= numbers["triangles_base"]
        ):
            raise PerformanceSceneFailure(
                "the reduced native LOD selection is internally inconsistent"
            )
    elif (
        numbers["lod_max"] != 0
        or numbers["lod_level_sum"] != 0
        or numbers["triangles_selected"] != numbers["triangles_base"]
    ):
        raise PerformanceSceneFailure(
            "the base native LOD selection is internally inconsistent"
        )
    return {
        **numbers,
        **{name: fields[name] == "true" for name in true_fields},
        "pssm": fields["pssm"] == "true",
        "reduced_this_frame": reduced_this_frame,
    }


def read_scene_source_timing(text: str) -> dict[str, object]:
    """Read the latest accepted-frame scene-source phase heartbeat.

    This is runtime evidence, not a synthetic benchmark: the values are
    accumulated only when the joined capture commits. Keeping material Apply
    separate from particle enumeration prevents the performance report from
    optimizing the wrong subsystem.
    """

    matches = list(SCENE_SOURCE_TIMING_PATTERN.finditer(text))
    if not matches:
        raise PerformanceSceneFailure(
            "the combined runtime emitted no scene-source phase receipt"
        )
    latest = matches[-1]
    captures = int(latest.group("captures"))
    if captures <= 0:
        raise PerformanceSceneFailure(
            "the scene-source phase receipt has no accepted captures"
        )
    fields: dict[str, int] = {}
    for match in SCENE_SOURCE_TIMING_FIELD_PATTERN.finditer(
        latest.group("body")
    ):
        name = match.group("name")
        if name in fields:
            raise PerformanceSceneFailure(
                f"the scene-source phase receipt repeats field {name}"
            )
        fields[name] = int(match.group("value"))
    missing = [
        name for name in SCENE_SOURCE_REQUIRED_TIMINGS if name not in fields
    ]
    if missing:
        raise PerformanceSceneFailure(
            "the scene-source phase receipt is missing timing fields: "
            + ", ".join(missing)
        )
    return {"captures": captures, "mean_ns": fields}


def verify_combined_presentation_ownership(
    text: str, target_platform: str
) -> dict[str, object]:
    """Prove Ogre-Next, not the hidden resource host, owns presentation."""

    visible_receipts = text.count(OGRE_NEXT_PRESENTATION_OWNER_RECEIPT)
    hidden_receipts = text.count(OGRE14_HIDDEN_RESOURCE_HOST_RECEIPT)
    if visible_receipts != 1:
        raise PerformanceSceneFailure(
            "the combined runtime did not emit exactly one Ogre-Next visible "
            f"presentation receipt: observed={visible_receipts}"
        )
    if hidden_receipts != 1:
        raise PerformanceSceneFailure(
            "the combined runtime did not emit exactly one protected hidden "
            f"resource-host receipt: observed={hidden_receipts}"
        )
    backend_receipts = OGRE_NEXT_PRESENTATION_BACKEND_PATTERN.findall(text)
    expected_backends = {
        "darwin": "ogre-next-metal",
        "linux": "ogre-next-vulkan",
        "win32": "ogre-next-d3d11",
    }
    expected_backend = expected_backends.get(target_platform)
    if backend_receipts != [expected_backend]:
        raise PerformanceSceneFailure(
            "the visible Ogre-Next backend receipt does not match the host: "
            f"observed={backend_receipts!r}, expected={expected_backend!r}"
        )
    return {
        "presentation_owner": "ogre-next",
        "visible_render_system": expected_backend,
        "visible_window": True,
        "legacy_visible_fallback": False,
        "resource_host": "ogre14",
        "resource_host_visible": False,
        "resource_host_protected": True,
    }


def verify_combined_actor_control(text: str) -> dict[str, object]:
    """Validate one native-window -> actor -> Ogre-Next frame receipt."""

    receipts = list(ACTOR_CONTROL_PATTERN.finditer(text))
    if len(receipts) != 1:
        raise PerformanceSceneFailure(
            "the combined runtime did not emit exactly one actor-control "
            f"receipt: observed={len(receipts)}"
        )
    fields: dict[str, str] = {}
    for match in ACTOR_CONTROL_FIELD_PATTERN.finditer(
        receipts[0].group("body")
    ):
        name = match.group("name")
        if name in fields:
            raise PerformanceSceneFailure(
                f"the actor-control receipt repeats field {name}"
            )
        fields[name] = match.group("value")

    exact = {
        "schema": "ror.ogre_next_actor_control_receipt.v1",
        "qualified": "true",
        "input_source": "visible_window_sdl",
        "presenter": "ogre-next",
        "legacy_visible_fallback": "false",
        "control": "truck_accelerate",
    }
    wrong = {
        name: (fields.get(name), expected)
        for name, expected in exact.items()
        if fields.get(name) != expected
    }
    if wrong:
        raise PerformanceSceneFailure(
            "the actor-control receipt identity changed: "
            + ", ".join(
                f"{name}={actual!r}, expected {expected!r}"
                for name, (actual, expected) in sorted(wrong.items())
            )
        )

    integer_names = (
        "actor_instance_id",
        "key",
        "press_transition",
        "press_event_id",
        "press_frame_id",
        "press_dynamic_updates",
        "press_scene_draws",
        "release_transition",
        "release_event_id",
        "release_frame_id",
        "release_dynamic_updates",
        "release_scene_draws",
    )
    float_names = (
        "press_issued",
        "press_resolved",
        "release_issued",
        "release_resolved",
    )
    missing = sorted(
        (set(integer_names) | set(float_names)) - fields.keys()
    )
    if missing:
        raise PerformanceSceneFailure(
            "the actor-control receipt is missing: " + ", ".join(missing)
        )
    try:
        integers = {name: int(fields[name]) for name in integer_names}
        floats = {name: float(fields[name]) for name in float_names}
    except ValueError as error:
        raise PerformanceSceneFailure(
            "the actor-control receipt contains a malformed number"
        ) from error
    if any(not math.isfinite(value) for value in floats.values()):
        raise PerformanceSceneFailure(
            "the actor-control receipt contains a non-finite value"
        )

    positive = tuple(name for name in integer_names if name != "actor_instance_id")
    if integers["actor_instance_id"] < 0 or any(
        integers[name] <= 0 for name in positive
    ):
        raise PerformanceSceneFailure(
            "the actor-control receipt contains a non-positive identity or "
            "native-scene counter"
        )
    if not (
        integers["release_transition"] > integers["press_transition"]
        and integers["release_event_id"] >= integers["press_event_id"]
        and integers["release_frame_id"] > integers["press_frame_id"]
    ):
        raise PerformanceSceneFailure(
            "the actor-control press/release/native-frame order is invalid"
        )
    if not (
        floats["press_issued"] >= 0.5
        and floats["press_resolved"] >= 0.25
        and abs(floats["release_issued"]) <= 0.001
        and abs(floats["release_resolved"]) <= 0.05
    ):
        raise PerformanceSceneFailure(
            "the actor-control issued/resolved values do not prove press and "
            "release"
        )
    return {
        "schema": exact["schema"],
        "qualified": True,
        "input_source": exact["input_source"],
        "presenter": exact["presenter"],
        "legacy_visible_fallback": False,
        "control": exact["control"],
        **integers,
        **floats,
    }


def render_system_name(target_platform: str) -> str:
    if target_platform in ("darwin", "linux"):
        return GL3PLUS_RENDER_SYSTEM
    if target_platform == "win32":
        return D3D11_RENDER_SYSTEM
    raise PerformanceSceneFailure(
        f"unsupported runtime platform: {target_platform}"
    )


def build_ror_config(request: "BudgetRequest") -> str:
    """Compose the archived settings one budget run depends on.

    The budget itself is a non-archived lifecycle contract and is armed on the
    command line instead, so a stale config file can never silently re-arm a
    later session. Only the frame-rate limiter belongs here, and it is
    re-proven from the receipt: a measurement taken behind a limiter or VSync
    describes the presentation cadence, not the scene.
    """

    try:
        preset = GRAPHICS_PRESETS[request.graphics_preset]
    except KeyError as error:
        raise PerformanceSceneFailure(
            f"unknown graphics preset: {request.graphics_preset}"
        ) from error

    lines = [
        "; Generated by tools/run_playable_performance_scene.py",
        f"; graphics preset: {request.graphics_preset}",
        "app_config_long_names=false",
        "app_disable_online_api=true",
        "app_force_cache_update=true",
        "audio_master_volume=0",
        "gfx_fps_limit=0",
    ]
    lines.extend(
        f"{name}={value}" for name, (value, _) in sorted(preset.items()))
    lines.append("")
    return "\n".join(lines)


def build_budget_arguments(request: "BudgetRequest") -> tuple[str, ...]:
    """The explicit per-launch arming for the non-archived budget CVars."""

    return (
        "-frame-budget", request.mode,
        "-frame-budget-receipt", str(request.receipt_path),
        "-frame-budget-scenario", request.scenario_id,
        "-frame-budget-sustained-ms", format(request.sustained_ms, ".4f"),
        "-frame-budget-percentile", str(request.percentile),
        "-frame-budget-percentile-ms", format(request.percentile_ms, ".4f"),
        "-frame-budget-warmup", str(request.warmup_frames),
        "-frame-budget-minimum", str(request.minimum_frames),
        "-frame-budget-frames", str(request.requested_frames),
    )


def build_ogre_config(request: "BudgetRequest") -> str:
    """Pin the exact requested video mode with VSync off."""

    system = render_system_name(request.target_platform)
    if system == GL3PLUS_RENDER_SYSTEM:
        body = (
            "Colour Depth=32",
            "Content Scaling Factor=1",
            "Debug Layer=Off",
            "Display Frequency=N/A",
            "FSAA= 0",
            "Full Screen=No",
            "Reversed Z-Buffer=No",
            "Separate Shader Objects=Yes",
            "VSync=No",
            "VSync Interval=1",
            f"Video Mode={request.width} x {request.height}",
            "sRGB Gamma Conversion=No",
        )
    else:
        body = (
            "Allow NVPerfHUD=No",
            "Debug Layer=Off",
            "Driver type=Hardware",
            "FSAA=1",
            "Full Screen=No",
            "Information Queue Exceptions Bottom Level="
            "No information queue exceptions",
            "Max Requested Feature Levels=11.0",
            "Min Requested Feature Levels=9.1",
            "Rendering Device=(default)",
            "Reversed Z-Buffer=No",
            "VSync=No",
            "VSync Interval=1",
            f"Video Mode={request.width} x {request.height} @ 32-bit colour",
            "sRGB Gamma Conversion=No",
        )
    return "\n".join(
        (f"Render System={system}", "", f"[{system}]", *body, "")
    )


class BudgetRequest:
    """The immutable request one run must satisfy exactly."""

    def __init__(
        self,
        *,
        scenario_id: str,
        terrain: str,
        actor: str,
        width: int,
        height: int,
        warmup_frames: int,
        minimum_frames: int,
        requested_frames: int,
        sustained_ms: float,
        percentile: int,
        percentile_ms: float,
        receipt_path: Path,
        mode: str = "gate",
        graphics_preset: str = "high",
        target_platform: str = sys.platform,
    ) -> None:
        if mode not in ("measure", "gate"):
            raise PerformanceSceneFailure(f"unsupported budget mode: {mode}")
        if graphics_preset not in GRAPHICS_PRESETS:
            raise PerformanceSceneFailure(
                f"unknown graphics preset: {graphics_preset}"
            )
        if not scenario_id:
            raise PerformanceSceneFailure("a scenario id is required")
        if width <= 0 or height <= 0:
            raise PerformanceSceneFailure(
                f"invalid resolution: {width}x{height}"
            )
        if warmup_frames < 0 or minimum_frames <= 0:
            raise PerformanceSceneFailure("invalid frame counts")
        if requested_frames < minimum_frames:
            raise PerformanceSceneFailure(
                "the requested frame count is below the minimum: "
                f"{requested_frames} < {minimum_frames}"
            )
        if not 0 < percentile <= 100:
            raise PerformanceSceneFailure(
                f"percentile is outside 1..100: {percentile}"
            )
        if not sustained_ms > 0.0 or not percentile_ms > 0.0:
            raise PerformanceSceneFailure("budgets must be positive")
        if percentile_ms < sustained_ms:
            raise PerformanceSceneFailure(
                "the percentile ceiling is below the sustained budget"
            )
        if not receipt_path.is_absolute():
            raise PerformanceSceneFailure(
                f"the receipt path must be absolute: {receipt_path}"
            )
        # An unsupported platform must be refused while composing the request,
        # not after a run has already burned minutes of wall clock.
        render_system_name(target_platform)

        self.scenario_id = scenario_id
        self.terrain = terrain
        self.actor = actor
        self.width = width
        self.height = height
        self.warmup_frames = warmup_frames
        self.minimum_frames = minimum_frames
        self.requested_frames = requested_frames
        self.sustained_ms = sustained_ms
        self.percentile = percentile
        self.percentile_ms = percentile_ms
        self.receipt_path = receipt_path
        self.mode = mode
        self.graphics_preset = graphics_preset
        self.target_platform = target_platform

    def as_record(self) -> dict[str, object]:
        return {
            "scenario_id": self.scenario_id,
            "terrain": self.terrain,
            "actor": self.actor,
            "width": self.width,
            "height": self.height,
            "warmup_frames": self.warmup_frames,
            "minimum_frames": self.minimum_frames,
            "requested_frames": self.requested_frames,
            "sustained_budget_ms": self.sustained_ms,
            "percentile": self.percentile,
            "percentile_budget_ms": self.percentile_ms,
            "mode": self.mode,
            "graphics_preset": self.graphics_preset,
            "graphics_settings": {
                name: expected
                for name, (_, expected)
                in GRAPHICS_PRESETS[self.graphics_preset].items()
            },
        }


def load_receipt(path: Path) -> dict[str, object]:
    if not path.is_file() or path.is_symlink():
        raise PerformanceSceneFailure(f"the receipt was not created: {path}")
    size = path.stat().st_size
    if size <= 0 or size > MAX_RECEIPT_BYTES:
        raise PerformanceSceneFailure(
            f"the receipt has an invalid size: {size}"
        )
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise PerformanceSceneFailure(
            f"the receipt is not readable JSON: {path}"
        ) from error
    if not isinstance(document, dict):
        raise PerformanceSceneFailure("the receipt is not a JSON object")
    return document


def validate_receipt(
    document: Mapping[str, object],
    request: BudgetRequest,
) -> dict[str, object]:
    """Prove the receipt describes exactly the requested measurement.

    Every declared field is compared with the request. A receipt that measured
    a different resolution, scenario, or budget is rejected rather than
    reported, because it is evidence for a run nobody asked for.
    """

    if document.get("format") != RECEIPT_FORMAT:
        raise PerformanceSceneFailure(
            f"unexpected receipt format: {document.get('format')!r}"
        )

    required_numbers = (
        "observed_frames",
        "warmup_frames",
        "accepted_frames",
        "rejected_frames",
        "saturated_frames",
        "over_budget_frames",
        "minimum_ms",
        "mean_ms",
        "maximum_ms",
        "p50_ms",
        "p95_ms",
        "p99_ms",
        "ranked_ms",
        "mean_fps",
    )
    for key in required_numbers:
        value = document.get(key)
        if not isinstance(value, (int, float)) or isinstance(value, bool):
            raise PerformanceSceneFailure(
                f"the receipt field {key!r} is not a number"
            )
        if value < 0:
            raise PerformanceSceneFailure(
                f"the receipt field {key!r} is negative"
            )

    exact_fields = {
        "mode": request.mode,
        "scenario_id": request.scenario_id,
        "width": request.width,
        "height": request.height,
        "percentile": request.percentile,
        "requested_frames": request.requested_frames,
        "minimum_frames": request.minimum_frames,
        "warmup_frames_requested": request.warmup_frames,
    }
    for key, expected in exact_fields.items():
        if document.get(key) != expected:
            raise PerformanceSceneFailure(
                f"the receipt {key!r} is {document.get(key)!r}, "
                f"expected {expected!r}"
            )

    for key, expected in (
        ("sustained_budget_ms", request.sustained_ms),
        ("percentile_budget_ms", request.percentile_ms),
    ):
        recorded = document.get(key)
        if not isinstance(recorded, (int, float)) or isinstance(recorded, bool):
            raise PerformanceSceneFailure(f"the receipt {key!r} is not a number")
        # The receipt serializes four decimals; compare at that resolution.
        if abs(float(recorded) - expected) > 5e-5:
            raise PerformanceSceneFailure(
                f"the receipt {key!r} is {recorded!r}, expected {expected!r}"
            )

    if document.get("terrain") != request.terrain:
        raise PerformanceSceneFailure(
            f"the receipt terrain is {document.get('terrain')!r}, "
            f"expected {request.terrain!r}"
        )

    # A limiter or an unrequested VSync makes the distribution describe the
    # presentation cadence rather than the renderer.
    if document.get("fps_limit") != 0:
        raise PerformanceSceneFailure(
            f"a frame-rate limiter was active: {document.get('fps_limit')!r}"
        )
    if document.get("vsync") is not False:
        raise PerformanceSceneFailure(
            f"VSync was not disabled: {document.get('vsync')!r}"
        )
    if document.get("fullscreen") is not False:
        raise PerformanceSceneFailure(
            "the run was fullscreen; the budget pins a windowed video mode"
        )
    # In the two-process bridge the game loop only produces scenes for a
    # separate presentation child, so its interval is a producer cadence and
    # not a frame rate. Such a run reports thousands of "frames" per second.
    if document.get("presents_frames") is not True:
        raise PerformanceSceneFailure(
            "the measured loop did not present its own frames, so its "
            "interval is a producer cadence rather than a frame rate; "
            "measure the presenting process instead"
        )

    accepted = int(document["accepted_frames"])
    rejected = int(document["rejected_frames"])
    observed = int(document["observed_frames"])
    warmup = int(document["warmup_frames"])
    if rejected != 0:
        raise PerformanceSceneFailure(
            f"the run rejected {rejected} malformed frame interval(s)"
        )
    if warmup != request.warmup_frames:
        raise PerformanceSceneFailure(
            f"the run recorded {warmup} warm-up frames, "
            f"expected {request.warmup_frames}"
        )
    if accepted < request.minimum_frames:
        raise PerformanceSceneFailure(
            f"the run recorded {accepted} frames, "
            f"below the {request.minimum_frames} frame minimum"
        )
    if observed < accepted + warmup:
        raise PerformanceSceneFailure(
            "the receipt frame accounting is inconsistent: "
            f"observed={observed}, accepted={accepted}, warmup={warmup}"
        )

    combined_runtime = document.get("renderer") == "ogre-next-combined"
    if combined_runtime:
        if document.get("requires_native_scene_draw_metrics") is not True:
            raise PerformanceSceneFailure(
                "the combined runtime did not require renderer-owned native "
                "scene draw metrics"
            )
        native_numeric = (
            "native_scene_draw_p99_limit",
            "native_scene_draw_exact_samples",
            "native_scene_draw_rejected_samples",
            "native_scene_draw_p99",
            "native_scene_draw_maximum",
        )
        native_values: dict[str, int] = {}
        for key in native_numeric:
            value = document.get(key)
            if (
                not isinstance(value, int)
                or isinstance(value, bool)
                or value < 0
            ):
                raise PerformanceSceneFailure(
                    f"the combined receipt field {key!r} is not a "
                    "non-negative integer"
                )
            native_values[key] = value
        if native_values["native_scene_draw_p99_limit"] != 2500:
            raise PerformanceSceneFailure(
                "the native scene draw ceiling is not the roadmap's 2500"
            )
        if native_values["native_scene_draw_exact_samples"] != accepted:
            raise PerformanceSceneFailure(
                "the native scene draw distribution does not cover every "
                f"accepted frame: exact={native_values['native_scene_draw_exact_samples']} "
                f"accepted={accepted}"
            )
        if native_values["native_scene_draw_rejected_samples"] != 0:
            raise PerformanceSceneFailure(
                "the native scene draw distribution contains rejected or "
                "duplicate compositor receipts"
            )
        if native_values["native_scene_draw_p99"] <= 0:
            raise PerformanceSceneFailure(
                "the visible Ogre-Next scene published no draw submissions"
            )
        if (
            native_values["native_scene_draw_p99"]
            > native_values["native_scene_draw_maximum"]
        ):
            raise PerformanceSceneFailure(
                "the native scene draw p99 exceeds its measured maximum"
            )
        if (
            request.mode == "gate"
            and native_values["native_scene_draw_p99"]
            > native_values["native_scene_draw_p99_limit"]
        ):
            raise PerformanceSceneFailure(
                "the native Ogre-Next scene draw budget failed: "
                f"p99={native_values['native_scene_draw_p99']} "
                f"limit={native_values['native_scene_draw_p99_limit']}"
            )

    ordered = (
        float(document["minimum_ms"]),
        float(document["p50_ms"]),
        float(document["p95_ms"]),
        float(document["p99_ms"]),
    )
    if list(ordered) != sorted(ordered):
        raise PerformanceSceneFailure(
            f"the receipt percentiles are not monotonic: {ordered}"
        )
    # The histogram reports an upper bin edge, so a ranked value may exceed the
    # exact maximum by at most one bin width.
    bin_width_ms = float(document.get("bin_width_ns", 0)) / 1e6
    if bin_width_ms <= 0.0:
        raise PerformanceSceneFailure("the receipt bin width is missing")
    if ordered[3] > float(document["maximum_ms"]) + bin_width_ms:
        raise PerformanceSceneFailure(
            "the receipt p99 exceeds the measured maximum"
        )

    verdict = document.get("verdict")
    passed = document.get("passed")
    if request.mode == "gate":
        if verdict != "pass" or passed is not True:
            raise PerformanceSceneFailure(
                f"the frame-time budget failed: verdict={verdict!r}, "
                f"mean_ms={document.get('mean_ms')!r}, "
                f"p{request.percentile}_ms={document.get('ranked_ms')!r}, "
                f"budget={request.sustained_ms}/{request.percentile_ms} ms"
            )
    elif verdict != "advisory":
        raise PerformanceSceneFailure(
            f"a measure-only run reported verdict={verdict!r}"
        )

    return dict(document)


#: Common display refresh intervals, in milliseconds. A distribution whose
#: median sits on one of these is very likely paced by the compositor rather
#: than by the renderer, even when the run requested VSync off.
REFRESH_INTERVALS_MS = (
    1000.0 / 24.0,
    1000.0 / 30.0,
    1000.0 / 48.0,
    1000.0 / 50.0,
    1000.0 / 60.0,
    1000.0 / 72.0,
    1000.0 / 90.0,
    1000.0 / 120.0,
    1000.0 / 144.0,
    1000.0 / 240.0,
)

#: Private renderer-diagnostic scenes are deliberately excluded from the
#: playable-performance gate.  They exercise the Ogre-Next presentation stack
#: but do not contain the joined RoR simulation, vehicle, terrain, or gameplay
#: surface whose budget this runner qualifies.
NON_PLAYABLE_COMBINED_OPTIONS = frozenset(
    ("--native-visual-showcase", "--native-visual-showcase-a0")
)

#: How close the median must sit to a refresh interval, and how tight the
#: distribution must be, before pacing is reported.
PACING_MEDIAN_TOLERANCE_MS = 0.5
PACING_SPREAD_TOLERANCE_MS = 2.5


def detect_presentation_pacing(
    receipt: Mapping[str, object],
) -> dict[str, object] | None:
    """Report a distribution that looks paced by the display, not the renderer.

    A paced run understates the renderer, so this never changes a pass into a
    fail. It exists so a recorded number is not later read as the renderer's
    cost when it is really the compositor's cadence.
    """

    median = float(receipt["p50_ms"])
    spread = float(receipt["p95_ms"]) - median
    if spread > PACING_SPREAD_TOLERANCE_MS:
        return None
    for interval in REFRESH_INTERVALS_MS:
        if abs(median - interval) <= PACING_MEDIAN_TOLERANCE_MS:
            return {
                "suspected_hz": round(1000.0 / interval, 3),
                "interval_ms": round(interval, 4),
                "median_ms": median,
                "p95_minus_median_ms": round(spread, 4),
            }
    return None


def scan_runtime_log(
    text: str,
    *,
    require_clean_content: bool = False,
) -> dict[str, object]:
    """Reject a run whose renderer faulted; count content diagnostics."""

    found = sorted({marker for marker in FATAL_LOG_MARKERS if marker in text})
    if found:
        raise PerformanceSceneFailure(
            "the runtime log contains fatal diagnostics: " + ", ".join(found)
        )

    content = {
        marker: text.count(marker)
        for marker in CONTENT_LOG_MARKERS
        if marker in text
    }
    if content and require_clean_content:
        raise PerformanceSceneFailure(
            "the runtime log contains content diagnostics: "
            + ", ".join(
                f"{marker} x{count}" for marker, count in sorted(content.items())
            )
        )

    identity: dict[str, object] = {"content_diagnostics": content}
    capture_rejections = text.count(CAPTURE_REJECTED_MARKER)
    # Capture is a producer transaction and may reject an update while the
    # presenter continues displaying its last committed frame. Preserve the
    # diagnostic count here; the combined-runtime acceptance path separately
    # requires a later native lighting/LOD receipt to prove recovery, a
    # completed PBS frame, and an exact base-or-reduced native selection. A
    # blank, stale, or bootstrap-only run still fails that renderer-owned gate.
    identity["startup_capture_rejections"] = capture_rejections
    for key, pattern in RENDERER_IDENTITY_PATTERNS.items():
        match = pattern.search(text)
        if match is not None:
            identity[key] = match.group("value").strip()
    if "render_system" not in identity:
        raise PerformanceSceneFailure(
            "the runtime log does not identify a render system"
        )
    return identity


def read_runtime_log(path: Path) -> str:
    if not path.is_file():
        raise PerformanceSceneFailure(f"the runtime log is missing: {path}")
    size = path.stat().st_size
    if size <= 0 or size > MAX_LOG_BYTES:
        raise PerformanceSceneFailure(
            f"the runtime log has an invalid size: {size}"
        )
    return path.read_text(encoding="utf-8", errors="replace")


def stage_runtime(
    artifact_dir: Path,
    request: BudgetRequest,
    mod_archives: Sequence[Path],
    executable: Path,
) -> tuple[Path, dict[str, Path], list[dict[str, str]]]:
    isolated_home = artifact_dir / "home"
    layout = runtime_layout(
        isolated_home, request.target_platform, executable)
    for directory in layout.values():
        directory.mkdir(parents=True, exist_ok=True)

    (layout["config"] / "RoR.cfg").write_text(
        build_ror_config(request), encoding="utf-8")
    (layout["config"] / "ogre.cfg").write_text(
        build_ogre_config(request), encoding="utf-8")

    staged: list[dict[str, str]] = []
    for archive in mod_archives:
        resolved = archive.resolve()
        if not resolved.is_file():
            raise PerformanceSceneFailure(f"mod archive is missing: {archive}")
        link = layout["mods"] / resolved.name
        if link.exists() or link.is_symlink():
            raise PerformanceSceneFailure(
                f"duplicate mod archive name: {resolved.name}"
            )
        # A symlink keeps the user's own archive byte-identical and unmoved.
        link.symlink_to(resolved)
        staged.append(
            {
                "name": resolved.name,
                "source": str(resolved),
                "sha256": sha256_file(resolved),
            }
        )
    return isolated_home, layout, staged


def build_command(
    executable: Path,
    request: BudgetRequest,
    launcher_arguments: Sequence[str] = (),
) -> tuple[str, ...]:
    """Compose the launch.

    Launcher options must precede the game arguments: the public renderer
    launcher parses its own options first and forwards everything after them
    to the selected child byte-for-byte.
    """

    non_playable = sorted(
        option
        for option in launcher_arguments
        if option in NON_PLAYABLE_COMBINED_OPTIONS
    )
    if non_playable:
        raise PerformanceSceneFailure(
            "the playable-performance gate cannot select renderer-only "
            "showcase mode: " + ", ".join(non_playable)
        )

    command = [str(executable)]
    command.extend(launcher_arguments)
    if request.target_platform == "darwin":
        command.extend(("-ApplePersistenceIgnoreState", "YES"))
    command.extend(("-checkcache", "-map", request.terrain))
    if request.actor:
        command.extend(("-truck", request.actor, "-enter"))
    command.extend(build_budget_arguments(request))
    return tuple(command)


def build_environment(
    isolated_home: Path,
    request: BudgetRequest,
) -> dict[str, str]:
    environment = os.environ.copy()
    environment.pop("SNAP_USER_COMMON", None)
    environment["ROR_D0_SCENE_HOME"] = str(isolated_home)
    environment["ROR_D0_EXACT_WINDOW_EXTENT"] = (
        f"{request.width}x{request.height}"
    )
    environment["ALSOFT_DRIVERS"] = "null"
    environment["ALSOFT_LOGLEVEL"] = "0"
    return environment


def wait_for_runtime_marker(
    log_path: Path,
    marker: str,
    process: subprocess.Popen[bytes],
    timeout_seconds: float,
) -> None:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        returncode = process.poll()
        if returncode is not None:
            raise PerformanceSceneFailure(
                f"the runtime exited with code {returncode} before {marker!r}"
            )
        try:
            if marker in log_path.read_text(
                encoding="utf-8", errors="replace"
            ):
                return
        except FileNotFoundError:
            pass
        time.sleep(0.1)
    raise PerformanceSceneFailure(
        f"the runtime did not emit {marker!r} within {timeout_seconds:.0f}s"
    )


def drive_linux_up_key(process_id: int, hold_seconds: float) -> str:
    xdotool = shutil.which("xdotool")
    if xdotool is None:
        raise PerformanceSceneFailure(
            "--qualify-actor-control requires xdotool on Linux"
        )
    deadline = time.monotonic() + 30.0
    window = ""
    while time.monotonic() < deadline:
        found = subprocess.run(
            [xdotool, "search", "--onlyvisible", "--pid", str(process_id)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=5,
        )
        candidates = found.stdout.decode("ascii", errors="ignore").split()
        if found.returncode == 0 and candidates:
            window = candidates[-1]
            break
        time.sleep(0.1)
    if not window:
        raise PerformanceSceneFailure(
            "xdotool found no visible runtime window owned by the child PID"
        )
    subprocess.run(
        [xdotool, "windowfocus", "--sync", window],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        timeout=10,
    )
    subprocess.run(
        [xdotool, "keydown", "Up"],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        timeout=10,
    )
    time.sleep(hold_seconds)
    subprocess.run(
        [xdotool, "keyup", "Up"],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        timeout=10,
    )
    return "x11-xtest-xdotool"


def find_windows_process_window(process_id: int) -> int:
    user32 = ctypes.windll.user32
    windows: list[int] = []
    callback_type = ctypes.WINFUNCTYPE(
        ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p
    )

    @callback_type
    def visit(window: int, _: int) -> bool:
        owner = ctypes.c_ulong()
        user32.GetWindowThreadProcessId(window, ctypes.byref(owner))
        if owner.value == process_id and user32.IsWindowVisible(window):
            windows.append(int(window))
        return True

    user32.EnumWindows(visit, 0)
    return windows[-1] if windows else 0


def drive_windows_up_key(process_id: int, hold_seconds: float) -> str:
    user32 = ctypes.windll.user32
    user32.ShowWindow.argtypes = (ctypes.c_void_p, ctypes.c_int)
    user32.PostMessageW.argtypes = (
        ctypes.c_void_p,
        ctypes.c_uint,
        ctypes.c_size_t,
        ctypes.c_ssize_t,
    )
    user32.PostMessageW.restype = ctypes.c_bool
    deadline = time.monotonic() + 30.0
    window = 0
    while time.monotonic() < deadline:
        window = find_windows_process_window(process_id)
        if window:
            break
        time.sleep(0.1)
    if not window:
        raise PerformanceSceneFailure(
            "User32 found no visible runtime window owned by the child PID"
        )
    user32.ShowWindow(window, 5)
    # Send the transition to the exact child-owned HWND. SDL's Win32 event
    # pump converts these OS messages before the game target sees them; the
    # process under test still contains no synthetic input hook.
    if not user32.PostMessageW(window, 0x0100, 0x26, 0x01480001):
        raise PerformanceSceneFailure(
            "User32 could not post Up Arrow down to the runtime window"
        )
    time.sleep(hold_seconds)
    if not user32.PostMessageW(window, 0x0101, 0x26, 0xC1480001):
        raise PerformanceSceneFailure(
            "User32 could not post Up Arrow release to the runtime window"
        )
    return "win32-user32-window-message"


def drive_macos_up_key(process_id: int, hold_seconds: float) -> str:
    osascript = shutil.which("osascript")
    if osascript is None:
        raise PerformanceSceneFailure(
            "--qualify-actor-control requires osascript on macOS"
        )
    focus = subprocess.run(
        [
            osascript,
            "-e",
            "tell application \"System Events\" to set frontmost of "
            f"first application process whose unix id is {process_id} "
            "to true",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        timeout=10,
    )
    if focus.returncode != 0:
        raise PerformanceSceneFailure(
            "System Events could not focus the runtime window: "
            + focus.stderr.decode("utf-8", errors="replace").strip()
        )
    application_services = ctypes.CDLL(
        "/System/Library/Frameworks/ApplicationServices.framework/"
        "ApplicationServices"
    )
    core_foundation = ctypes.CDLL(
        "/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation"
    )
    application_services.CGEventCreateKeyboardEvent.restype = ctypes.c_void_p
    application_services.CGEventCreateKeyboardEvent.argtypes = (
        ctypes.c_void_p,
        ctypes.c_ushort,
        ctypes.c_bool,
    )
    application_services.CGEventPost.argtypes = (
        ctypes.c_uint32, ctypes.c_void_p
    )
    core_foundation.CFRelease.argtypes = (ctypes.c_void_p,)

    def post(pressed: bool) -> None:
        # 0x7e is the hardware-independent macOS virtual key for Up Arrow.
        event = application_services.CGEventCreateKeyboardEvent(
            None, 0x7E, pressed
        )
        if not event:
            raise PerformanceSceneFailure(
                "CoreGraphics could not create the Up Arrow event"
            )
        try:
            # kCGHIDEventTap routes through the same focused system event
            # path as physical keyboard input. The runner has already made
            # the exact child process frontmost, so no in-process test hook is
            # involved.
            application_services.CGEventPost(0, event)
        finally:
            core_foundation.CFRelease(event)

    post(True)
    time.sleep(hold_seconds)
    post(False)
    return "macos-coregraphics-hid-event"


def drive_external_actor_control(
    target_platform: str,
    process_id: int,
    hold_seconds: float = 3.0,
) -> dict[str, object]:
    if target_platform == "linux":
        backend = drive_linux_up_key(process_id, hold_seconds)
    elif target_platform == "win32":
        backend = drive_windows_up_key(process_id, hold_seconds)
    elif target_platform == "darwin":
        backend = drive_macos_up_key(process_id, hold_seconds)
    else:
        raise PerformanceSceneFailure(
            f"actor-control qualification is unsupported on {target_platform}"
        )
    return {
        "process_external": True,
        "backend": backend,
        "key": "up",
        "hold_seconds": hold_seconds,
    }


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--artifact-dir", required=True, type=Path)
    parser.add_argument("--scenario-id", required=True)
    parser.add_argument("--terrain", required=True)
    parser.add_argument("--actor", default="")
    parser.add_argument("--mod-archive", action="append", default=[], type=Path)
    parser.add_argument(
        "--launcher-argument",
        action="append",
        default=[],
        help="option passed to the renderer launcher before game arguments",
    )
    parser.add_argument(
        "--graphics-preset",
        choices=sorted(GRAPHICS_PRESETS),
        default="high",
    )
    parser.add_argument("--width", type=int, default=1920)
    parser.add_argument("--height", type=int, default=1080)
    parser.add_argument("--warmup-frames", type=int, default=120)
    parser.add_argument("--minimum-frames", type=int, default=600)
    parser.add_argument("--frames", type=int, default=1800)
    parser.add_argument("--sustained-ms", type=float, default=1000.0 / 60.0)
    parser.add_argument("--percentile", type=int, default=95)
    parser.add_argument("--percentile-ms", type=float, default=18.3)
    parser.add_argument(
        "--require-clean-content",
        action="store_true",
        help="fail when the run logs any missing material or resource",
    )
    parser.add_argument(
        "--measure-only",
        action="store_true",
        help="record the distribution without enforcing the budget",
    )
    parser.add_argument(
        "--qualify-actor-control",
        action="store_true",
        help=(
            "drive Up Arrow through the host OS and require a visible-window "
            "SDL -> authoritative truck -> completed Ogre-Next frame receipt"
        ),
    )
    parser.add_argument(
        "--actor-control-hold-seconds",
        type=float,
        default=3.0,
        help=(
            "seconds to hold Up Arrow during actor-control qualification; "
            "the default spans the slow software-rendered CI cadence"
        ),
    )
    parser.add_argument("--timeout", type=int, default=1800)
    parser.add_argument("--repository", type=Path, default=Path.cwd())
    return parser.parse_args(list(argv))


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)

    try:
        artifact_dir = args.artifact_dir
        if artifact_dir.exists():
            raise PerformanceSceneFailure(
                f"the artifact directory already exists: {artifact_dir}"
            )
        executable = args.executable.resolve()
        if not executable.is_file() or not os.access(executable, os.X_OK):
            raise PerformanceSceneFailure(
                f"the executable is missing or not executable: {executable}"
            )
        artifact_dir.mkdir(parents=True)

        request = BudgetRequest(
            scenario_id=args.scenario_id,
            terrain=args.terrain,
            actor=args.actor,
            width=args.width,
            height=args.height,
            warmup_frames=args.warmup_frames,
            minimum_frames=args.minimum_frames,
            requested_frames=args.frames,
            sustained_ms=round(args.sustained_ms, 4),
            percentile=args.percentile,
            percentile_ms=round(args.percentile_ms, 4),
            receipt_path=(artifact_dir / "frame-time-receipt.json").resolve(),
            mode="measure" if args.measure_only else "gate",
            graphics_preset=args.graphics_preset,
        )

        isolated_home, layout, staged = stage_runtime(
            artifact_dir, request, args.mod_archive, executable)
        command = build_command(
            executable, request, args.launcher_argument)
        if args.qualify_actor_control and not request.actor:
            raise PerformanceSceneFailure(
                "--qualify-actor-control requires an explicit actor"
            )
        if not (0.05 <= args.actor_control_hold_seconds <= 30.0):
            raise PerformanceSceneFailure(
                "--actor-control-hold-seconds must be between 0.05 and 30"
            )
        driver: dict[str, object] | None = None
        console_path = artifact_dir / "console.txt"
        with console_path.open("wb") as console_stream:
            process = subprocess.Popen(
                list(command),
                env=build_environment(isolated_home, request),
                stdout=console_stream,
                stderr=subprocess.STDOUT,
            )
            started = time.monotonic()
            try:
                if args.qualify_actor_control:
                    readiness_timeout = min(
                        120.0, float(args.timeout)
                    )
                    wait_for_runtime_marker(
                        layout["logs"] / "RoR.log",
                        ACTOR_CONTROL_SPAWN_MARKER,
                        process,
                        readiness_timeout,
                    )
                    # Spawning precedes the first native scene submission. On
                    # software Vulkan/D3D runners, that submission can spend
                    # several seconds compiling shaders. Sending a bounded
                    # press during that synchronous interval lets both OS
                    # transitions accumulate before the game can resolve the
                    # pressed state. Wait until Ogre-Next has completed and
                    # retained the first actor scene, then drive the next
                    # ordinary input/simulation/presentation boundaries.
                    readiness_remaining = max(
                        0.1,
                        readiness_timeout - (time.monotonic() - started),
                    )
                    wait_for_runtime_marker(
                        layout["logs"] / "RoR.log",
                        ACTOR_CONTROL_PRESENTED_SCENE_MARKER,
                        process,
                        readiness_remaining,
                    )
                    driver = drive_external_actor_control(
                        request.target_platform,
                        process.pid,
                        args.actor_control_hold_seconds,
                    )
                remaining = max(
                    0.1, float(args.timeout) - (time.monotonic() - started)
                )
                returncode = process.wait(timeout=remaining)
            except BaseException:
                if process.poll() is None:
                    process.kill()
                    process.wait()
                raise
        console = console_path.read_text(encoding="utf-8", errors="replace")

        log_text = read_runtime_log(layout["logs"] / "RoR.log")
        (artifact_dir / "RoR.log").write_text(log_text, encoding="utf-8")

        # The receipt is read before the exit code is judged so a failed gate
        # reports the measured numbers instead of a bare exit status. A missing
        # receipt inverts that: the exit status is then the only fact there is,
        # and a crashed run must not be reported as a mere missing file.
        if not request.receipt_path.is_file():
            raise PerformanceSceneFailure(
                f"the runtime exited with code {returncode} without "
                f"retaining a receipt at {request.receipt_path}; the run did "
                "not reach the end of its render loop"
            )
        document = load_receipt(request.receipt_path)
        identity = scan_runtime_log(
            log_text, require_clean_content=args.require_clean_content)
        receipt = validate_receipt(document, request)
        combined_runtime = receipt.get("renderer") == "ogre-next-combined"
        identity["effective_graphics_settings"] = verify_graphics_preset(
            log_text,
            request.graphics_preset,
            combined_runtime=combined_runtime,
        )
        if combined_runtime:
            ownership = verify_combined_presentation_ownership(
                log_text, request.target_platform
            )
            identity["resource_host_render_system"] = identity.pop(
                "render_system"
            )
            identity["render_system"] = ownership["visible_render_system"]
            identity["presentation_ownership"] = ownership
            identity["native_distance_lod"] = (
                verify_combined_native_distance_lod(log_text)
            )
            identity["scene_source_timing"] = read_scene_source_timing(
                log_text
            )
            if args.qualify_actor_control:
                identity["actor_control"] = verify_combined_actor_control(
                    log_text
                )
                identity["actor_control_driver"] = driver
        elif args.qualify_actor_control:
            raise PerformanceSceneFailure(
                "actor-control qualification requires RoR-Combined"
            )

        if returncode != 0:
            raise PerformanceSceneFailure(
                f"the runtime exited with code {returncode}"
                + (
                    " (frame-time budget failure)"
                    if returncode == BUDGET_FAILURE_EXIT_CODE
                    else ""
                )
            )

        pacing = detect_presentation_pacing(receipt)
        report = {
            "format": REPORT_FORMAT,
            "presentation_pacing": pacing,
            "platform": platform.platform(),
            "machine": platform.machine(),
            "executable": str(executable),
            "executable_sha256": sha256_file(executable),
            "command": list(command),
            "request": request.as_record(),
            "renderer": identity,
            "mod_archives": staged,
            "receipt": receipt,
        }
        (artifact_dir / "performance-run.json").write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        if pacing is not None:
            print(
                "NOTE the frame interval distribution looks paced at "
                f"{pacing['suspected_hz']} Hz "
                f"(median {pacing['median_ms']:.4f} ms); the measurement "
                "describes presentation cadence, not renderer headroom"
            )
        print(
            "PASS "
            f"frames={receipt['accepted_frames']} "
            f"mean_ms={receipt['mean_ms']:.4f} "
            f"mean_fps={receipt['mean_fps']:.3f} "
            f"p95_ms={receipt['p95_ms']:.4f} "
            f"p99_ms={receipt['p99_ms']:.4f} "
            f"max_ms={receipt['maximum_ms']:.4f}"
        )
        return 0
    except subprocess.TimeoutExpired:
        print(
            f"FAIL the runtime exceeded {args.timeout} seconds",
            file=sys.stderr,
        )
        return 1
    except subprocess.CalledProcessError as exc:
        print(
            "FAIL the external actor-control driver failed: "
            f"command={exc.cmd!r} returncode={exc.returncode}",
            file=sys.stderr,
        )
        return 1
    except PerformanceSceneFailure as failure:
        print(f"FAIL {failure}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
