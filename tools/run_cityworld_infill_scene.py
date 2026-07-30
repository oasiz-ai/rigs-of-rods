#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Accept CityWorld v6 regional infill with eight UI-free native RGB views.

This standard-library-only gate performs no downloads and never uses the
developer's normal RoR profile. It authenticates CityWorld and the complete
derived overlay through the seamless-corridor gate, requires the embedded
regional-infill manifest to be the exact canonical project plan, rebuilds the
overlay byte-for-byte, and runs one isolated fixed-camera native scene.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
from pathlib import Path
import platform
import re
import shutil
import sys
import tempfile
from typing import Mapping, Sequence
import zipfile


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
CORRIDOR_RUNNER_PATH = (
    REPOSITORY_ROOT / "tools/run_cityworld_corridor_scene.py"
)
INFILL_PLAN_PATH = REPOSITORY_ROOT / "tools/cityworld_infill.py"


def load_module(name: str, path: Path) -> object:
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load module: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


corridor = load_module(
    "ror_cityworld_infill_scene_corridor",
    CORRIDOR_RUNNER_PATH,
)
infill = load_module(
    "ror_cityworld_infill_scene_plan",
    INFILL_PLAN_PATH,
)
base = corridor.base


CITYWORLD_NAME = corridor.CITYWORLD_NAME
OVERLAY_NAME = corridor.OVERLAY_NAME
OVERLAY_TERRAIN = corridor.OVERLAY_TERRAIN
OVERLAY_REPORT_FORMAT = "ror-cityworld-local-overlay-v6"
INFILL_MANIFEST_MEMBER = "cityworld_next_infill_manifest.v1.json"
INFILL_MANIFEST_ROLE = "regional-infill-plan"
INFILL_SOURCE_AUTHENTICATION_FORMAT = (
    "ror-cityworld-regional-infill-source-authentication-v1"
)
FIXTURE_PATH = (
    "tests/fixtures/cityworld_infill_runtime/"
    "cityworld_infill_runtime.as"
)
SCRIPT_NAME = "cityworld_infill_runtime.as"
REPORT_FORMAT = "ror-cityworld-infill-runtime-report-v1"
MAX_MANIFEST_BYTES = 4 * 1024 * 1024
EXPECTED_PLACEMENTS = 46
EXPECTED_ROUTES = 7
EXPECTED_SITES = 8
EXPECTED_STATIONS = 2
EXPECTED_STATION_LIGHTS = 12
EXPECTED_DERIVED_SOURCE_PLACEMENT_RECORDS = 91
CAPTURE_HOLD_FRAMES = 40
PASS_FRAME = 345
RGB_CAPTURE_IDS = (
    "west_farm_belt",
    "sunset_courts",
    "west_highway_service",
    "coyote_arch",
    "arroyo_vista",
    "intercity_service",
    "intercity_farm",
    "sagebrush_arroyo",
)
CAMERA_CONTRACT = (
    (
        RGB_CAPTURE_IDS[0],
        "vector3(860.0f, 80.0f, 560.0f)",
        "vector3(860.0f, 0.1f, 250.0f)",
    ),
    (
        RGB_CAPTURE_IDS[1],
        "vector3(1055.0f, 95.0f, 1780.0f)",
        "vector3(1055.0f, 0.1f, 1360.0f)",
    ),
    (
        RGB_CAPTURE_IDS[2],
        "vector3(970.0f, 32.0f, 1520.0f)",
        "vector3(805.0f, 2.0f, 1395.0f)",
    ),
    (
        RGB_CAPTURE_IDS[3],
        "vector3(3925.0f, 90.0f, 3010.0f)",
        "vector3(3925.0f, 12.0f, 2575.0f)",
    ),
    (
        RGB_CAPTURE_IDS[4],
        "vector3(4240.0f, 115.0f, 3960.0f)",
        "vector3(4240.0f, 0.1f, 3550.0f)",
    ),
    (
        RGB_CAPTURE_IDS[5],
        "vector3(3910.0f, 28.0f, 3710.0f)",
        "vector3(3820.0f, 2.0f, 3635.0f)",
    ),
    (
        RGB_CAPTURE_IDS[6],
        "vector3(4070.0f, 85.0f, 4630.0f)",
        "vector3(4070.0f, 0.1f, 4325.0f)",
    ),
    (
        RGB_CAPTURE_IDS[7],
        "vector3(1255.0f, 75.0f, 750.0f)",
        "vector3(1255.0f, 0.1f, 450.0f)",
    ),
)
SCRIPT_MARKERS = (
    "[RoR|CW2|InfillRuntime] START cameras=8 hold_frames=40 "
    "batch=4 placements=46 routes=7 stations=2 station_lights=12",
    *(
        f"[RoR|CW2|InfillRuntime] CAPTURE index={index} "
        f"id={capture_id} hold_frames=40"
        for index, capture_id in enumerate(RGB_CAPTURE_IDS)
    ),
)
PASS_PATTERN = re.compile(
    r"\[RoR\|CW2\|InfillRuntime\] PASS cameras=8 hold_frames=40 "
    r"frames=(?P<frames>[0-9]+) "
    r"physics_steps=(?P<steps>[0-9]+) "
    r"placements=46 routes=7 stations=2 station_lights=12"
)
DEPENDENCY_PATTERN = corridor.DEPENDENCY_PATTERN
SIDE_PIER_SUMMARY_PATTERN = corridor.SIDE_PIER_SUMMARY_PATTERN
STATION_LIGHT_MARKER = (
    "[RoR|TerrainObject|Lights] "
    "odef=rorng_city_infill_service_station_90x65.odef "
    "spotlights=0 point_lights=6 local_shadow_casters=0"
)
BRIDGE_LIGHT_MARKER = (
    "[RoR|TerrainObject|Lights] "
    "odef=rorng_city_led_streetlight_bridge.odef "
    "spotlights=0 point_lights=1 local_shadow_casters=0"
)
FATAL_MARKERS = (
    "[RoR|CW2|InfillRuntime] FAIL",
    "Could not load script 'cityworld_infill_runtime.as",
    "EXC_BAD_ACCESS",
    "Segmentation fault",
    "RenderingAPIException",
    "OGRE EXCEPTION",
    "GL_INVALID_",
)
INFILL_MATERIAL_FAILURE_TOKENS = (
    "error",
    "not found",
    "cannot",
    "can't",
    "unable",
    "missing",
    "no supportable",
    "blank",
)
HIGHWAY_ODEF_SHA256 = (
    "9caa5752ce5b1cb11f26ad086b93e052f376486d6a8503eb04db0503fe73e602"
)


class InfillSceneFailure(RuntimeError):
    """Fail-closed native acceptance failure for CityWorld regional infill."""


def sha256_file(path: Path) -> str:
    try:
        return corridor.sha256_file(path)
    except corridor.CorridorSceneFailure as error:
        raise InfillSceneFailure(str(error)) from error


def exact_dict(value: object, label: str) -> dict[str, object]:
    if not isinstance(value, dict):
        raise InfillSceneFailure(f"{label} is not an object")
    return value


def exact_list(value: object, label: str) -> list[object]:
    if not isinstance(value, list):
        raise InfillSceneFailure(f"{label} is not an array")
    return value


def ordered_markers(text: str, markers: Sequence[str], label: str) -> None:
    previous = -1
    for marker in markers:
        offset = text.find(marker)
        if offset <= previous:
            raise InfillSceneFailure(
                f"{label} marker is missing or out of order: {marker}"
            )
        previous = offset


def validate_fixture(path: Path) -> dict[str, object]:
    if not path.is_file() or path.is_symlink():
        raise InfillSceneFailure(f"fixture is missing: {path}")
    if not 1 <= path.stat().st_size <= 1024 * 1024:
        raise InfillSceneFailure(f"fixture size is invalid: {path}")
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise InfillSceneFailure(
            f"cannot read infill fixture: {error}"
        ) from error
    required = (
        ("const uint CAPTURE_COUNT = 8;", 1),
        ("const uint CAPTURE_HOLD_FRAMES = 40;", 1),
        ("const uint PASS_FRAME = 345;", 1),
        (
            'console.cVarSet("sim_deterministic_fixed_steps_per_frame", "4");',
            1,
        ),
        ('console.cVarSet("ui_hide_gui", "true");', 1),
        ("MSG_APP_SCREENSHOT_REQUESTED", 1),
        ("placements=46 routes=7 stations=2 station_lights=12", 2),
    )
    for marker, count in required:
        if text.count(marker) != count:
            raise InfillSceneFailure(
                f"fixture contract marker drifted: {marker}"
            )
    for capture_id, position, target in CAMERA_CONTRACT:
        for marker in (f'"{capture_id}"', position, target):
            if text.count(marker) != 1:
                raise InfillSceneFailure(
                    f"fixture camera contract drifted: {capture_id}"
                )
    return {
        "cameras": len(CAMERA_CONTRACT),
        "capture_hold_frames": CAPTURE_HOLD_FRAMES,
        "path": path.relative_to(REPOSITORY_ROOT).as_posix(),
        "sha256": sha256_file(path),
        "size": path.stat().st_size,
    }


def validate_manifest_payload(payload: bytes) -> dict[str, object]:
    expected_payload = infill.canonical_manifest_bytes()
    if payload != expected_payload:
        raise InfillSceneFailure(
            "embedded infill manifest differs from the canonical project plan"
        )
    try:
        manifest = json.loads(
            payload.decode("utf-8"),
            object_pairs_hook=corridor.reject_duplicate_keys,
        )
    except (
        corridor.DuplicateKeyError,
        RecursionError,
        UnicodeDecodeError,
        json.JSONDecodeError,
    ) as error:
        raise InfillSceneFailure(
            f"embedded infill manifest is invalid JSON: {error}"
        ) from error
    manifest = exact_dict(manifest, "infill manifest")
    if manifest.get("format") != infill.FORMAT or manifest.get("version") != 1:
        raise InfillSceneFailure("embedded infill manifest identity drifted")
    audit = exact_dict(manifest.get("audit"), "infill audit")
    summary = exact_dict(audit.get("summary"), "infill summary")
    expected_summary = {
        "access_routes": EXPECTED_ROUTES,
        "assets": 5,
        "placements": EXPECTED_PLACEMENTS,
        "placements_by_category": {
            "farmland": 13,
            "natural-landmark": 14,
            "service-station": EXPECTED_STATIONS,
            "suburb": 17,
        },
        "sites": EXPECTED_SITES,
        "sites_by_category": {
            "farmland": 2,
            "natural-landmark": 2,
            "service-station": EXPECTED_STATIONS,
            "suburb": 2,
        },
    }
    if summary != expected_summary:
        raise InfillSceneFailure("embedded infill summary drifted")
    if (
        len(exact_list(manifest.get("placements"), "infill placements"))
        != EXPECTED_PLACEMENTS
        or len(exact_list(manifest.get("access_routes"), "infill routes"))
        != EXPECTED_ROUTES
        or len(exact_list(manifest.get("sites"), "infill sites"))
        != EXPECTED_SITES
    ):
        raise InfillSceneFailure("embedded infill inventory drifted")
    return manifest


def expected_source_authentication(
    manifest: Mapping[str, object],
) -> dict[str, object]:
    anchors = exact_list(
        manifest.get("source_anchors"),
        "infill source anchors",
    )
    anchor_records = [
        exact_dict(value, f"infill source anchor {index}")
        for index, value in enumerate(anchors)
    ]
    anchor_ids = [
        value.get("anchor_id")
        for value in anchor_records
    ]
    if (
        len(anchor_ids) != 7
        or any(not isinstance(value, str) for value in anchor_ids)
        or len(set(anchor_ids)) != 7
    ):
        raise InfillSceneFailure("infill source-anchor identities drifted")
    line_1354_records = [
        value
        for value in anchor_records
        if value.get("placement_line") == 1354
    ]
    line_0378_records = [
        value
        for value in anchor_records
        if value.get("placement_line") == 378
    ]
    generated = [
        value
        for value in anchor_records
        if "placement_line" not in value
    ]
    if (
        len(line_1354_records) != 1
        or len(line_0378_records) != 5
        or len(generated) != 1
    ):
        raise InfillSceneFailure("infill source-anchor provenance drifted")
    line_1354 = line_1354_records[0]
    highway_position = list(infill.PINNED_HIGHWAY_PLACEMENT_POSITION_M)
    highway_rotation = list(
        infill.PINNED_HIGHWAY_PLACEMENT_ROTATION_DEGREES
    )
    expected_line_0378 = {
        "authored_position_m": highway_position,
        "city": "NeoQueretaro",
        "collision_member": infill.PINNED_HIGHWAY_COLLISION_MEMBER,
        "collision_sha256": infill.PINNED_HIGHWAY_COLLISION_SHA256,
        "connection": "divided surface highway beneath source bridge ramp",
        "decoded_surface_materials": [
            "calleunsolosentido",
            "pavimento",
        ],
        "decoded_surface_triangle_count": 9599,
        "line_number": 378,
        "local_to_world_mapping": [
            "world_x=-local_z",
            "world_y=local_y-0.4",
            "world_z=local_x",
        ],
        "member": infill.PINNED_TOBJ_MEMBER,
        "object": infill.PINNED_HIGHWAY_OBJECT,
        "odef_member": "autopistaQr.odef",
        "odef_sha256": HIGHWAY_ODEF_SHA256,
        "position_m": highway_position,
        "rotation_degrees": highway_rotation,
        "runtime_grounding_applied": False,
        "runtime_position_m": highway_position,
    }
    expected_line_1354 = {
        "line_number": 1354,
        "member": infill.PINNED_TOBJ_MEMBER,
        "object": line_1354.get("placement_object"),
        "position_m": line_1354.get("placement_position_m"),
        "rotation_degrees": line_1354.get(
            "placement_rotation_degrees"
        ),
    }
    return {
        "anchor_ids": anchor_ids,
        "archive_sha256": infill.PINNED_ARCHIVE_SHA256,
        "authenticated_placement_lines": [378, 1354],
        "format": INFILL_SOURCE_AUTHENTICATION_FORMAT,
        "generated_anchor_count": 1,
        "line_0378": expected_line_0378,
        "line_1354": expected_line_1354,
        "native_anchor_count": 6,
        "source_anchor_count": 7,
        "source_tobj": {
            "member": infill.PINNED_TOBJ_MEMBER,
            "sha256": infill.PINNED_TOBJ_SHA256,
        },
    }


def validate_overlay_infill(
    overlay_archive: Path,
    overlay_report: Mapping[str, object],
) -> dict[str, object]:
    if overlay_report.get("format") != OVERLAY_REPORT_FORMAT:
        raise InfillSceneFailure("regional infill requires overlay v6")
    rights = exact_dict(overlay_report.get("rights"), "overlay rights")
    if (
        rights.get("derived_source_placement_record_count")
        != EXPECTED_DERIVED_SOURCE_PLACEMENT_RECORDS
    ):
        raise InfillSceneFailure(
            "regional infill changed the authenticated source-derived "
            "placement count"
        )
    source = exact_dict(overlay_report.get("source"), "overlay source")
    references = exact_dict(
        source.get("references"),
        "overlay source references",
    )
    if (
        references.get("regional_infill_manifest")
        != INFILL_MANIFEST_MEMBER
    ):
        raise InfillSceneFailure(
            "overlay source does not reference the regional infill manifest"
        )
    package = exact_dict(overlay_report.get("package"), "overlay package")
    files = exact_list(package.get("files"), "overlay package files")
    matches = [
        exact_dict(value, "infill package record")
        for value in files
        if isinstance(value, dict)
        and value.get("path") == INFILL_MANIFEST_MEMBER
    ]
    if len(matches) != 1:
        raise InfillSceneFailure(
            "overlay does not inventory one regional infill manifest"
        )
    record = matches[0]
    if (
        set(record) != {"path", "role", "sha256", "size"}
        or record.get("role") != INFILL_MANIFEST_ROLE
    ):
        raise InfillSceneFailure("regional infill package record drifted")
    regional = exact_dict(
        overlay_report.get("regional_infill"),
        "overlay regional infill",
    )
    if set(regional) != {
        "audit",
        "manifest",
        "source_authentication",
        "summary",
    }:
        raise InfillSceneFailure(
            "overlay regional infill fields drifted"
        )
    if regional.get("manifest") != record:
        raise InfillSceneFailure(
            "overlay regional infill does not reference its package record"
        )
    source_authentication = exact_dict(
        regional.get("source_authentication"),
        "regional infill source authentication",
    )
    try:
        with zipfile.ZipFile(overlay_archive, "r") as archive:
            infos = [
                info
                for info in archive.infolist()
                if info.filename == INFILL_MANIFEST_MEMBER
            ]
            if (
                len(infos) != 1
                or not 1 <= infos[0].file_size <= MAX_MANIFEST_BYTES
            ):
                raise InfillSceneFailure(
                    "embedded infill manifest member is missing or oversized"
                )
            payload = archive.read(infos[0])
    except (OSError, zipfile.BadZipFile) as error:
        raise InfillSceneFailure(
            f"cannot read embedded infill manifest: {error}"
        ) from error
    if (
        record.get("sha256") != corridor.sha256_bytes(payload)
        or record.get("size") != len(payload)
    ):
        raise InfillSceneFailure(
            "embedded infill manifest differs from its package record"
        )
    manifest = validate_manifest_payload(payload)
    if (
        regional.get("audit") != manifest["audit"]
        or regional.get("summary") != manifest["audit"]["summary"]
    ):
        raise InfillSceneFailure(
            "overlay regional infill audit or summary drifted"
        )
    if source_authentication != expected_source_authentication(manifest):
        raise InfillSceneFailure(
            "overlay regional infill source authentication drifted"
        )
    return {
        "format": manifest["format"],
        "manifest": record,
        "sha256": corridor.sha256_bytes(payload),
        "summary": manifest["audit"]["summary"],
        "version": manifest["version"],
    }


def build_command(executable: Path) -> tuple[str, ...]:
    command = [str(executable)]
    if sys.platform == "darwin":
        command.extend(("-ApplePersistenceIgnoreState", "YES"))
    command.extend(("-map", OVERLAY_TERRAIN, "-runscript", SCRIPT_NAME))
    return tuple(command)


def isolated_runtime_layout(
    isolated_home: Path,
    executable: Path,
    target_platform: str,
) -> dict[str, Path]:
    return corridor.isolated_runtime_layout(
        isolated_home,
        executable,
        target_platform,
    )


def stage_runtime(
    isolated_home: Path,
    *,
    executable: Path,
    script_path: Path,
    script_record: Mapping[str, object],
    cityworld_archive: Path,
    cityworld_record: Mapping[str, object],
    overlay_archive: Path,
    overlay_record: Mapping[str, object],
    target_platform: str,
) -> tuple[dict[str, Path], list[Path]]:
    layout = isolated_runtime_layout(
        isolated_home,
        executable,
        target_platform,
    )
    for key in ("config", "logs", "mods", "screenshots", "user"):
        layout[key].mkdir(parents=True, exist_ok=True)
    scripts = layout["user"] / "scripts"
    scripts.mkdir()
    staged_script = scripts / SCRIPT_NAME
    staged_cityworld = layout["mods"] / CITYWORLD_NAME
    staged_overlay = layout["mods"] / OVERLAY_NAME
    shutil.copyfile(script_path, staged_script)
    shutil.copyfile(cityworld_archive, staged_cityworld)
    shutil.copyfile(overlay_archive, staged_overlay)
    for staged, expected, label in (
        (staged_script, script_record.get("sha256"), "infill script"),
        (
            staged_cityworld,
            cityworld_record.get("sha256"),
            "CityWorld archive",
        ),
        (
            staged_overlay,
            overlay_record.get("sha256"),
            "overlay archive",
        ),
    ):
        try:
            corridor.verify_staged_file(staged, expected, label)
        except corridor.CorridorSceneFailure as error:
            raise InfillSceneFailure(str(error)) from error
    configs = base.write_runtime_config(
        layout["config"],
        target_platform=target_platform,
    )
    return layout, configs


def validate_runtime_logs(
    returncode: int,
    stdout: str,
    engine_log: str,
    script_log: str,
    *,
    target_platform: str,
) -> dict[str, object]:
    if returncode != 0:
        raise InfillSceneFailure(
            f"RoR infill scene exited with {returncode}"
        )
    combined = "\n".join((stdout, engine_log, script_log))
    for marker in FATAL_MARKERS:
        if marker in combined:
            raise InfillSceneFailure(
                f"runtime logged fatal marker: {marker}"
            )
    if "Error =" in script_log:
        raise InfillSceneFailure("AngelScript compiler emitted an error")
    ordered_markers(script_log, SCRIPT_MARKERS, "infill AngelScript")
    passes = list(PASS_PATTERN.finditer(script_log))
    if len(passes) != 1:
        raise InfillSceneFailure(
            "infill scene did not emit exactly one PASS"
        )
    if passes[0].start() <= script_log.find(SCRIPT_MARKERS[-1]):
        raise InfillSceneFailure(
            "infill PASS preceded the final RGB capture"
        )
    frames = int(passes[0].group("frames"))
    steps = int(passes[0].group("steps"))
    if frames != PASS_FRAME or not 1280 <= steps <= 5000:
        raise InfillSceneFailure(
            "infill deterministic frame/physics count drifted"
        )
    if (
        script_log.count("[RoR|CW2|InfillRuntime] CAPTURE index=")
        != len(RGB_CAPTURE_IDS)
    ):
        raise InfillSceneFailure(
            "infill scene did not request exactly eight RGB captures"
        )

    for marker in (
        corridor.CITYWORLD_FALLBACK_LIGHTING_MARKER,
        "===== TERRAIN LOADING DONE CityWorldNextLocalOverlay.terrn2",
        corridor.ENGINE_MARKERS[1],
    ):
        if engine_log.count(marker) != 1:
            raise InfillSceneFailure(
                f"engine marker must appear exactly once: {marker}"
            )
    dependencies = list(DEPENDENCY_PATTERN.finditer(engine_log))
    if len(dependencies) != 1:
        raise InfillSceneFailure(
            "CityWorld terrain dependency was not mounted exactly once"
        )
    dependency_path = dependencies[0].group("path").replace("\\", "/")
    if not dependency_path.endswith("/mods/" + CITYWORLD_NAME):
        raise InfillSceneFailure(
            "terrain dependency mounted an unexpected CityWorld path"
        )
    side_piers = sorted(
        tuple(int(value) for value in match.groups())
        for match in SIDE_PIER_SUMMARY_PATTERN.finditer(engine_log)
    )
    expected_side_piers = sorted(((46, 46, 0), (56, 56, 0)))
    if side_piers != expected_side_piers:
        raise InfillSceneFailure(
            "complete two-corridor side-pier summaries drifted"
        )
    if engine_log.count(BRIDGE_LIGHT_MARKER) != corridor.EXPECTED_LIGHTS:
        raise InfillSceneFailure("native bridge light count drifted")
    station_instances = engine_log.count(STATION_LIGHT_MARKER)
    if (
        station_instances != EXPECTED_STATIONS
        or station_instances * 6 != EXPECTED_STATION_LIGHTS
    ):
        raise InfillSceneFailure(
            "native service-station light count drifted"
        )
    for line in engine_log.splitlines():
        lowered = line.casefold()
        if (
            "rorng_city_infill_" in lowered
            and any(token in lowered for token in INFILL_MATERIAL_FAILURE_TOKENS)
        ):
            raise InfillSceneFailure(
                "infill material did not resolve: " + line
            )
    return {
        "captures": len(RGB_CAPTURE_IDS),
        "frames": frames,
        "physics_steps": steps,
        "renderer": base.parse_renderer_identity(
            engine_log,
            target_platform,
        ),
        "side_piers": [list(item) for item in side_piers],
        "station_light_instances": station_instances,
        "station_lights": station_instances * 6,
    }


def validate_rgb_screenshots(
    directory: Path,
) -> tuple[list[Path], dict[str, dict[str, object]]]:
    try:
        entries = sorted(directory.iterdir(), key=lambda path: path.name)
    except OSError as error:
        raise InfillSceneFailure(
            f"cannot inspect screenshot directory: {error}"
        ) from error
    if (
        len(entries) != len(RGB_CAPTURE_IDS)
        or any(
            path.suffix.lower() != ".png"
            or not path.is_file()
            or path.is_symlink()
            for path in entries
        )
    ):
        raise InfillSceneFailure(
            "runtime must emit exactly eight regular PNG screenshots"
        )
    records: dict[str, dict[str, object]] = {}
    hashes: set[str] = set()
    for capture_id, path in zip(RGB_CAPTURE_IDS, entries):
        try:
            record = base.validate_rgb_png(path)
        except base.BridgeSceneFailure as error:
            raise InfillSceneFailure(
                f"{capture_id} RGB validation failed: {error}"
            ) from error
        digest = record.get("sha256")
        if not isinstance(digest, str) or not digest or digest in hashes:
            raise InfillSceneFailure(
                "the eight fixed infill RGB screenshots are not distinct"
            )
        hashes.add(digest)
        records[capture_id] = {
            **record,
            "source_filename": path.name,
        }
    return entries, records


def collect_diagnostics(
    artifact_staging: Path,
    *,
    stdout: str,
    engine_log: str,
    script_log: str,
    config_paths: Sequence[Path],
) -> dict[str, object]:
    directory = artifact_staging / "diagnostics"
    directory.mkdir()
    artifacts = {
        "stdout": directory / "runtime.stdout",
        "engine_log": directory / "RoR.log",
        "script_log": directory / "Angelscript.log",
    }
    artifacts["stdout"].write_text(stdout, encoding="utf-8")
    artifacts["engine_log"].write_text(engine_log, encoding="utf-8")
    artifacts["script_log"].write_text(script_log, encoding="utf-8")
    records: dict[str, object] = {}
    for label, path in artifacts.items():
        records[label] = {
            "artifact": path.relative_to(artifact_staging).as_posix(),
            "sha256": sha256_file(path),
            "size": path.stat().st_size,
        }
    configs = {}
    for path in config_paths:
        destination = directory / path.name
        shutil.copy2(path, destination)
        configs[path.name] = {
            "artifact": destination.relative_to(
                artifact_staging
            ).as_posix(),
            "sha256": sha256_file(destination),
            "size": destination.stat().st_size,
        }
    records["configs"] = configs
    return records


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--cityworld-archive", required=True, type=Path)
    parser.add_argument("--overlay-archive", required=True, type=Path)
    parser.add_argument("--artifact-dir", required=True, type=Path)
    parser.add_argument(
        "--repository",
        type=Path,
        default=Path(__file__).resolve().parents[1],
    )
    parser.add_argument("--rebuild-timeout", type=int, default=180)
    parser.add_argument("--runtime-timeout", type=int, default=180)
    args = parser.parse_args(argv)
    if args.rebuild_timeout <= 0 or args.runtime_timeout <= 0:
        parser.error("timeouts must be positive")
    return args


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    base.renderer_contract(sys.platform)
    repository = args.repository.resolve()
    if repository != REPOSITORY_ROOT.resolve():
        raise InfillSceneFailure(
            "--repository must be the checkout containing this runner"
        )
    executable = args.executable.resolve()
    cityworld_archive = args.cityworld_archive.resolve()
    overlay_archive = args.overlay_archive.resolve()
    artifact_dir = args.artifact_dir.resolve()
    if not executable.is_file() or executable.is_symlink():
        raise InfillSceneFailure(
            f"executable is missing or is a symlink: {executable}"
        )
    if artifact_dir.exists():
        raise InfillSceneFailure(
            f"artifact directory already exists: {artifact_dir}"
        )

    cityworld_record = corridor.validate_cityworld_archive(
        cityworld_archive
    )
    overlay_report, overlay_record = corridor.validate_overlay_archive(
        overlay_archive,
        repository,
    )
    infill_record = validate_overlay_infill(
        overlay_archive,
        overlay_report,
    )
    overlay_rebuild = corridor.verify_overlay_rebuild(
        cityworld_archive,
        overlay_archive,
        overlay_report,
        repository,
        args.rebuild_timeout,
    )
    fixture_path = repository / FIXTURE_PATH
    fixture_record = validate_fixture(fixture_path)

    artifact_dir.parent.mkdir(parents=True, exist_ok=True)
    artifact_staging = Path(
        tempfile.mkdtemp(
            prefix=f".{artifact_dir.name}.partial-",
            dir=artifact_dir.parent,
        )
    )
    published = False
    try:
        with tempfile.TemporaryDirectory(
            prefix="ror-cityworld-infill-"
        ) as temporary:
            isolated_home = Path(temporary)
            layout, config_paths = stage_runtime(
                isolated_home,
                executable=executable,
                script_path=fixture_path,
                script_record=fixture_record,
                cityworld_archive=cityworld_archive,
                cityworld_record=cityworld_record,
                overlay_archive=overlay_archive,
                overlay_record=overlay_record,
                target_platform=sys.platform,
            )
            environment = os.environ.copy()
            environment["ROR_D0_SCENE_HOME"] = str(isolated_home)
            environment["ALSOFT_DRIVERS"] = "null"
            environment["ALSOFT_LOGLEVEL"] = "0"
            command = build_command(executable)
            completed = base.run_command(
                command,
                args.runtime_timeout,
                cwd=executable.parent,
                environment=environment,
            )
            stdout = base.decode_output(completed.stdout)
            engine_log = base.read_required(
                layout["logs"] / "RoR.log",
                "RoR engine log",
            )
            script_log = base.read_required(
                layout["logs"] / "Angelscript.log",
                "AngelScript log",
            )
            metrics = validate_runtime_logs(
                completed.returncode,
                stdout,
                engine_log,
                script_log,
                target_platform=sys.platform,
            )
            screenshots, rgb_records = validate_rgb_screenshots(
                layout["screenshots"]
            )
            diagnostics = collect_diagnostics(
                artifact_staging,
                stdout=stdout,
                engine_log=engine_log,
                script_log=script_log,
                config_paths=config_paths,
            )
            rgb_directory = artifact_staging / "rgb"
            rgb_directory.mkdir()
            rgb_artifacts: dict[str, str] = {}
            for capture_id, screenshot in zip(
                RGB_CAPTURE_IDS,
                screenshots,
            ):
                destination = rgb_directory / f"{capture_id}.png"
                shutil.copy2(screenshot, destination)
                rgb_artifacts[capture_id] = (
                    destination.relative_to(artifact_staging).as_posix()
                )

        report: dict[str, object] = {
            "acceptance": {
                "fixed_ui_free_rgb_views": len(RGB_CAPTURE_IDS),
                "infill_material_resolution_verified": True,
                "native_service_station_lights_verified":
                    EXPECTED_STATION_LIGHTS,
                "status": "passed",
            },
            "archives": {
                "cityworld": cityworld_record,
                "overlay": overlay_record,
            },
            "artifacts": {
                "diagnostics": diagnostics,
                "rgb": rgb_artifacts,
            },
            "command": list(command),
            "executable": {
                "path": str(executable),
                "sha256": sha256_file(executable),
                "size": executable.stat().st_size,
            },
            "fixture": fixture_record,
            "format": REPORT_FORMAT,
            "infill": infill_record,
            "machine": platform.machine(),
            "metrics": metrics,
            "overlay_rebuild": overlay_rebuild,
            "platform": platform.platform(),
            "repository_commit": base.git_output(
                repository,
                ("rev-parse", "HEAD"),
            ),
            "rgb": rgb_records,
            "runners": {
                "tools/run_cityworld_bridge_scene.py": {
                    "sha256": sha256_file(corridor.BASE_PATH),
                },
                "tools/run_cityworld_corridor_scene.py": {
                    "sha256": sha256_file(CORRIDOR_RUNNER_PATH),
                },
                "tools/run_cityworld_infill_scene.py": {
                    "sha256": sha256_file(Path(__file__).resolve()),
                },
            },
        }
        report_name = "cityworld_infill_runtime.report.json"
        report_path = artifact_staging / report_name
        report_path.write_text(
            base.canonical_json(report) + "\n",
            encoding="utf-8",
        )
        report_sha = sha256_file(report_path)
        corridor.publish_artifact_directory(
            artifact_staging,
            artifact_dir,
        )
        published = True
    finally:
        if not published:
            shutil.rmtree(artifact_staging, ignore_errors=True)

    published_report = artifact_dir / report_name
    print(
        "CityWorld regional-infill runtime acceptance passed: "
        f"{published_report} sha256={report_sha}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (
        InfillSceneFailure,
        corridor.CorridorSceneFailure,
        base.BridgeSceneFailure,
    ) as error:
        print(
            f"CityWorld regional-infill runtime acceptance failed: {error}",
            file=sys.stderr,
        )
        raise SystemExit(1)
