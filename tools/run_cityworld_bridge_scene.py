#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the compiled CityWorld bridge through rendering and vehicle physics.

This standard-library-only gate performs no downloads and never touches a
developer's normal RoR profile. It validates the checked CityWorld package,
builds a deterministic local runtime pack from pinned project content, drives
the pinned DAF across three exact bridge modules, requires shader/material and
collision evidence, and fully decodes one UI-free 1280x720 RGB screenshot.
"""

from __future__ import annotations

import argparse
import binascii
import hashlib
import json
import math
import os
from pathlib import Path, PurePosixPath
import platform
import re
import shutil
import struct
import subprocess
import sys
from typing import Iterable, Mapping, NamedTuple, Sequence
import zipfile
import zlib


CONTENT_COMMIT = "34fefdd126784bf87b068fc283f812525d159dd7"
ASSET_MANIFEST = (
    "resources/nextgen/cityworld/bridge/"
    "rorng_city_bridge_span_20m.asset.json"
)
COMPILE_REPORT = (
    "resources/nextgen/cityworld/bridge/compiled/"
    "rorng_city_bridge_span_20m.compile.json"
)
ADDITIONAL_ASSET_PACKAGES: tuple[tuple[str, str], ...] = ()
FIXTURE_DIRECTORY = "tests/fixtures/cityworld_bridge_runtime"
FIXTURE_FILES = (
    "LICENSE.md",
    "cityworld_bridge_runtime.as",
    "cityworld_bridge_runtime.terrn2",
    "cityworld_bridge_runtime.tobj",
)
TERRAIN = "cityworld_bridge_runtime.terrn2"
RUNTIME_PACK = "cityworld-next-bridge-runtime.zip"
REPORT_FORMAT = "ror-cityworld-bridge-runtime-report-v1"
PROCESS_DIAGNOSTIC_FORMAT = "ror-cityworld-runtime-process-diagnostic-v1"
RGB_ARTIFACT_NAME = "cityworld_bridge_rgb.png"
SUCCESS_PREFIX = "CityWorld bridge runtime gate passed"
DEVIATION_METRIC_KEY = "lateral_error_m"
DEVIATION_LABEL = "lateral"
RUNNER_PATHS = ("tools/run_cityworld_bridge_scene.py",)
EXTRA_REPORT_FIELDS: dict[str, object] = {}
VEHICLE_ARCHIVE = "dafsemi.zip"
VEHICLE_ENTRY = "b6b0UID-semi.truck"
EXPECTED_WIDTH = 1280
EXPECTED_HEIGHT = 720
EXACT_WINDOW_EXTENT_CONTRACT = f"{EXPECTED_WIDTH}x{EXPECTED_HEIGHT}"
MAX_SCREENSHOT_BYTES = 32 * 1024 * 1024
MAX_PACK_MEMBER_BYTES = 64 * 1024 * 1024
MAX_CONFIG_BYTES = 1024 * 1024
FALLBACK_AMBIENT_SCALE = 0.35


def fallback_lighting_marker(
    terrain_ambient_rgb: tuple[float, float, float],
) -> str:
    ambient_rgb = tuple(
        max(0.0, min(1.0, channel)) * FALLBACK_AMBIENT_SCALE
        for channel in terrain_ambient_rgb
    )
    return (
        "[RoR|Terrain|Lighting] policy=fallback-v1 "
        "ambient_scale=0.350 directional_shadow_casters=1 "
        f"ambient_rgb={ambient_rgb[0]:.3f},"
        f"{ambient_rgb[1]:.3f},{ambient_rgb[2]:.3f}"
    )


FALLBACK_LIGHTING_MARKER = fallback_lighting_marker((0.72, 0.72, 0.72))
PSSM_PREFIX = "[RoR|Shadow|PSSM] enabled"
PSSM_PATTERN = re.compile(
    r"\[RoR\|Shadow\|PSSM\] enabled "
    r"quality=(?P<quality>[0-3]) "
    r"cascades=(?P<cascades>[0-9]+) "
    r"rtss_receiver=(?P<receiver>[01]) "
    r"format=(?P<format>[A-Za-z0-9_]+) "
    r"sizes=(?P<w0>[0-9]+)x(?P<h0>[0-9]+)/"
    r"(?P<w1>[0-9]+)x(?P<h1>[0-9]+)/"
    r"(?P<w2>[0-9]+)x(?P<h2>[0-9]+) "
    r"lambda=(?P<lambda>-?[0-9.eE+]+) "
    r"near=(?P<near>-?[0-9.eE+]+) "
    r"far=(?P<far>-?[0-9.eE+]+) "
    r"splits=(?P<s0>-?[0-9.eE+]+)/"
    r"(?P<s1>-?[0-9.eE+]+)/"
    r"(?P<s2>-?[0-9.eE+]+)/"
    r"(?P<s3>-?[0-9.eE+]+)"
)
PSSM_QUALITY_PROFILES = {
    0: (((1024, 1024), (1024, 1024), (512, 512)), 0.98),
    1: (((2048, 2048), (1024, 1024), (1024, 1024)), 0.975),
    2: (((3072, 3072), (2048, 2048), (2048, 2048)), 0.97),
    3: (((4096, 4096), (3072, 3072), (2048, 2048)), 0.965),
}
POSTPROCESS_MODES = {
    "none": 0,
    "v0a": 1,
}
PHYSICS_MODES = {
    "async": True,
    "sync": False,
}
POSTPROCESS_PREFIX = "[RoR|PostProcess]"
POSTPROCESS_PATTERN = re.compile(
    r"\[RoR\|PostProcess\] "
    r"event=(?P<event>[a-z0-9_]+) "
    r"requested=(?P<requested>-?[0-9]+) "
    r"effective=(?P<effective>-?[0-9]+) "
    r"backend=(?P<backend>[a-z0-9_]+) "
    r"status=(?P<status>[a-z0-9_]+) "
    r"stage=(?P<stage>[a-z0-9_]+) "
    r"backing=(?P<width>[0-9]+)x(?P<height>[0-9]+) "
    r"renderer=(?P<renderer>.+?) "
    r"detail=(?P<detail>[^\r\n]+)"
)
POSTPROCESS_BACKENDS = {
    "darwin": "gl3plus_glsl330",
    "linux": "gl3plus_glsl330",
    "win32": "d3d11_sm4",
}


class RendererContract(NamedTuple):
    """Exact OGRE renderer contract for one supported host platform."""

    backend: str
    render_system: str
    api_version_pattern: re.Pattern[str]
    config_lines: tuple[str, ...]


GL3PLUS_CONTRACT = RendererContract(
    backend="gl3plus",
    render_system="OpenGL 3+ Rendering Subsystem",
    api_version_pattern=re.compile(
        r"GL_VERSION = (?P<value>[^\r\n]+)"
    ),
    config_lines=(
        "Render System=OpenGL 3+ Rendering Subsystem",
        "",
        "[OpenGL 3+ Rendering Subsystem]",
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
        f"Video Mode={EXPECTED_WIDTH} x {EXPECTED_HEIGHT}",
        "sRGB Gamma Conversion=No",
        "",
    ),
)
D3D11_CONTRACT = RendererContract(
    backend="d3d11",
    render_system="Direct3D11 Rendering Subsystem",
    api_version_pattern=re.compile(
        r"D3D11: Device Feature Level "
        r"(?P<value>[0-9]+\.[0-9]+)"
    ),
    config_lines=(
        "Render System=Direct3D11 Rendering Subsystem",
        "",
        "[Direct3D11 Rendering Subsystem]",
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
        f"Video Mode={EXPECTED_WIDTH} x {EXPECTED_HEIGHT} @ 32-bit colour",
        "sRGB Gamma Conversion=No",
        "",
    ),
)
RENDERER_CONTRACTS = {
    "darwin": GL3PLUS_CONTRACT,
    "linux": GL3PLUS_CONTRACT,
    "win32": D3D11_CONTRACT,
}
RENDERER_IDENTITY_PATTERNS = {
    "device": re.compile(r"Device Name: (?P<value>[^\r\n]+)"),
    "render_system": re.compile(
        r"RenderSystem Name: (?P<value>[^\r\n]+)"
    ),
    "vendor": re.compile(r"GPU Vendor: (?P<value>[^\r\n]+)"),
}

SIMPLE2_FILES = (
    "simple2-asphalt_diffusespecular.dds",
    "simple2-asphalt_normalheight.dds",
    "simple2-gravel_diffusespecular.dds",
    "simple2-gravel_normalheight.dds",
    "simple2-page-0-0.otc",
    "simple2.os",
    "simple2.otc",
    "simple2_a-page-0-0.otc",
    "simple2_a.otc",
    "simple2_groundmodel.cfg",
    "simple2_landuse.cfg",
    "simple2_traction.png",
)

SCRIPT_MARKERS = (
    "[RoR|CW2|BridgeRuntime] START spans=3 length_m=60",
    "[RoR|CW2|BridgeRuntime] ARMED actor=2026072802 heading=3.14159",
    "[RoR|CW2|BridgeRuntime] ENTER",
    "[RoR|CW2|BridgeRuntime] SEAM index=0",
    "[RoR|CW2|BridgeRuntime] CAPTURE",
    "[RoR|CW2|BridgeRuntime] SEAM index=1",
    "[RoR|CW2|BridgeRuntime] EXIT",
    "[RoR|CW2|BridgeRuntime] PASS spans=3 seams=2",
)
ENGINE_MARKERS = (
    "Parsing script rorng_city_bridge_span_20m.material",
    "Mesh: Loading rorng_city_bridge_span_20m_lod0.mesh.",
    "Mesh: Loading rorng_city_bridge_span_20m_collision_barrier_left.mesh.",
    "Mesh: Loading rorng_city_bridge_span_20m_collision_barrier_right.mesh.",
    "Mesh: Loading rorng_city_bridge_span_20m_collision_road.mesh.",
    "Pass 0 of 'rorng_city_concrete'",
    "Pass 0 of 'rorng_city_asphalt'",
    "Pass 0 of 'rorng_city_galvanized_steel'",
    "Pass 0 of 'rorng_city_dark_steel'",
    "Pass 0 of 'rorng_city_lane_white'",
    "Pass 0 of 'rorng_city_lane_yellow'",
)
FATAL_MARKERS = (
    "[RoR|CW2|BridgeRuntime] FAIL",
    "[ODEF] Could not find rorng_city_bridge",
    "Can't assign material to SubMesh of 'rorng_city_bridge",
    "GL_INVALID_",
    "RenderingAPIException",
    "OGRE EXCEPTION",
    "EXC_BAD_ACCESS",
    "Segmentation fault",
)
PASS_PATTERN = re.compile(
    r"\[RoR\|CW2\|BridgeRuntime\] PASS spans=3 seams=2 "
    r"distance_m=(?P<distance>-?[0-9.eE+]+) "
    r"min_y=(?P<min_y>-?[0-9.eE+]+) "
    r"max_y=(?P<max_y>-?[0-9.eE+]+) "
    r"lateral_error=(?P<lateral>-?[0-9.eE+]+) "
    r"speed=(?P<speed>-?[0-9.eE+]+) "
    r"physics_steps=(?P<steps>[0-9]+)"
)


class BridgeSceneFailure(RuntimeError):
    """Fail-closed diagnostic for an invalid runtime gate."""


def canonical_json(document: Mapping[str, object]) -> str:
    return json.dumps(
        document,
        ensure_ascii=True,
        sort_keys=True,
        separators=(",", ":"),
    )


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def sha256_file(path: Path) -> str:
    if not path.is_file() or path.is_symlink():
        raise BridgeSceneFailure(f"required regular file is missing: {path}")
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def decode_output(payload: bytes | str | None) -> str:
    if payload is None:
        return ""
    if isinstance(payload, str):
        return payload
    return payload.decode("utf-8", errors="replace")


def decode_process_output(
    completed: subprocess.CompletedProcess[bytes],
) -> str:
    """Decode both native output channels for semantic failure checks."""

    stdout = decode_output(completed.stdout)
    stderr = decode_output(completed.stderr)
    if stdout and stderr and not stdout.endswith(("\n", "\r")):
        return stdout + "\n" + stderr
    return stdout + stderr


def run_command(
    command: Sequence[str],
    timeout: int,
    *,
    cwd: Path | None = None,
    environment: Mapping[str, str] | None = None,
) -> subprocess.CompletedProcess[bytes]:
    try:
        return subprocess.run(
            list(command),
            cwd=None if cwd is None else str(cwd),
            env=None if environment is None else dict(environment),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired as error:
        raise BridgeSceneFailure(
            f"command exceeded {timeout} seconds: {' '.join(command)}"
        ) from error


def git_output(repository: Path, arguments: Sequence[str]) -> str:
    result = run_command(("git", "-C", str(repository), *arguments), 30)
    output = decode_process_output(result)
    if result.returncode != 0:
        raise BridgeSceneFailure(
            f"git {' '.join(arguments)} failed in {repository}: {output}"
        )
    return output.strip()


def load_json(path: Path) -> dict[str, object]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise BridgeSceneFailure(f"cannot read JSON {path}: {error}") from error
    if not isinstance(document, dict):
        raise BridgeSceneFailure(f"JSON root is not an object: {path}")
    return document


def resolve_repository_path(repository: Path, value: object) -> Path:
    if not isinstance(value, str) or not value or "\\" in value:
        raise BridgeSceneFailure("compile output has a non-portable path")
    pure = PurePosixPath(value)
    if pure.is_absolute() or any(part in ("", ".", "..") for part in pure.parts):
        raise BridgeSceneFailure(f"compile output has an unsafe path: {value}")
    path = (repository / value).resolve()
    try:
        path.relative_to(repository)
    except ValueError as error:
        raise BridgeSceneFailure(
            f"compile output escapes the repository: {value}"
        ) from error
    return path


def validate_cityworld_package(
    repository: Path,
    timeout: int,
) -> tuple[dict[str, object], tuple[Path, ...]]:
    packages = (
        (ASSET_MANIFEST, COMPILE_REPORT),
        *ADDITIONAL_ASSET_PACKAGES,
    )
    reports: list[dict[str, object]] = []
    outputs: list[Path] = []
    names: set[str] = set()
    for asset_manifest, compile_report in packages:
        commands = (
            (
                sys.executable,
                str(repository / "tools/validate_cityworld_asset.py"),
                str(repository / asset_manifest),
                "--repo-root",
                str(repository),
            ),
            (
                sys.executable,
                str(repository / "tools/compile_cityworld_asset.py"),
                str(repository / asset_manifest),
                "--repo-root",
                str(repository),
                "--validate-checked",
            ),
        )
        for command in commands:
            completed = run_command(command, timeout, cwd=repository)
            if completed.returncode != 0:
                raise BridgeSceneFailure(
                    "CityWorld package validation failed: "
                    + decode_process_output(completed)
                )

        report = load_json(repository / compile_report)
        if report.get("format") != "ror-cityworld-scene-compile-report-v1":
            raise BridgeSceneFailure("unsupported CityWorld compile report")
        raw_outputs = report.get("outputs")
        if not isinstance(raw_outputs, list) or not raw_outputs:
            raise BridgeSceneFailure("CityWorld compile report has no outputs")
        reports.append(report)
        for raw_output in raw_outputs:
            if not isinstance(raw_output, dict):
                raise BridgeSceneFailure(
                    "CityWorld compile output is not an object"
                )
            path = resolve_repository_path(repository, raw_output.get("path"))
            if path.name in names:
                raise BridgeSceneFailure(
                    "CityWorld runtime output basename is duplicated: "
                    f"{path.name}"
                )
            names.add(path.name)
            if path.stat().st_size != raw_output.get("size"):
                raise BridgeSceneFailure(
                    f"CityWorld output size drift: {path.name}"
                )
            if sha256_file(path) != raw_output.get("sha256"):
                raise BridgeSceneFailure(
                    f"CityWorld output hash drift: {path.name}"
                )
            outputs.append(path)

    provenance = run_command(
        (
            sys.executable,
            str(repository / "tools/build_cityworld_next_provenance.py"),
            "--repo-root",
            str(repository),
            "--check",
        ),
        timeout,
        cwd=repository,
    )
    if provenance.returncode != 0:
        raise BridgeSceneFailure(
            "CityWorld package validation failed: "
            + decode_process_output(provenance)
        )
    if not reports:
        raise BridgeSceneFailure("CityWorld package set is empty")
    required_suffixes = {".material", ".odef", ".mesh"}
    if not required_suffixes.issubset({path.suffix for path in outputs}):
        raise BridgeSceneFailure("CityWorld runtime output types are incomplete")
    return reports[0], tuple(sorted(outputs, key=lambda path: path.name))


def verify_pinned_content(repository: Path) -> Path:
    content = repository / "content"
    commit = git_output(content, ("rev-parse", "HEAD"))
    if commit != CONTENT_COMMIT:
        raise BridgeSceneFailure(
            f"content commit drift: expected {CONTENT_COMMIT}, got {commit}"
        )
    selected = (
        f"dafsemi/{VEHICLE_ENTRY}",
        *(f"simple2-terrain/{name}" for name in SIMPLE2_FILES),
    )
    tracked = set(git_output(content, ("ls-files",)).splitlines())
    missing = sorted(set(selected) - tracked)
    if missing:
        raise BridgeSceneFailure(f"pinned content is missing: {missing}")
    dirty = git_output(
        content,
        ("status", "--porcelain", "--untracked-files=no", "--", *selected),
    )
    if dirty:
        raise BridgeSceneFailure(
            "selected pinned content has local modifications:\n" + dirty
        )
    return content


def verify_vehicle_archive(source_content: Path, runtime_content: Path) -> Path:
    archive_path = runtime_content / VEHICLE_ARCHIVE
    if not archive_path.is_file() or archive_path.is_symlink():
        raise BridgeSceneFailure(
            f"runtime vehicle archive is missing: {archive_path}"
        )
    source = source_content / "dafsemi" / VEHICLE_ENTRY
    try:
        with zipfile.ZipFile(archive_path, "r") as archive:
            names = archive.namelist()
            if len(names) != len(set(names)):
                raise BridgeSceneFailure(
                    f"runtime vehicle archive has duplicate entries: {archive_path}"
                )
            payload = archive.read(VEHICLE_ENTRY)
    except (OSError, KeyError, zipfile.BadZipFile) as error:
        raise BridgeSceneFailure(
            f"runtime vehicle archive is invalid: {archive_path}"
        ) from error
    if sha256_bytes(payload) != sha256_file(source):
        raise BridgeSceneFailure(
            f"runtime {VEHICLE_ENTRY} differs from pinned content"
        )
    return archive_path


def deterministic_zip_info(name: str) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED
    info.create_system = 3
    info.external_attr = 0o100644 << 16
    info.flag_bits = 0x800
    return info


def build_runtime_pack(
    repository: Path,
    source_content: Path,
    compiled_outputs: Iterable[Path],
    destination: Path,
) -> tuple[dict[str, dict[str, object]], str]:
    members: dict[str, Path] = {}
    for name in SIMPLE2_FILES:
        members[name] = source_content / "simple2-terrain" / name
    fixture_root = repository / FIXTURE_DIRECTORY
    for name in FIXTURE_FILES:
        members[name] = fixture_root / name
    for path in compiled_outputs:
        if path.name in members:
            raise BridgeSceneFailure(
                f"runtime pack basename collision: {path.name}"
            )
        members[path.name] = path

    inventory: dict[str, dict[str, object]] = {}
    payloads: dict[str, bytes] = {}
    for name, path in sorted(members.items()):
        pure = PurePosixPath(name)
        if (
            pure.name != name
            or pure.is_absolute()
            or any(part in ("", ".", "..") for part in pure.parts)
        ):
            raise BridgeSceneFailure(f"unsafe runtime member name: {name}")
        if not path.is_file() or path.is_symlink():
            raise BridgeSceneFailure(f"runtime pack input is missing: {path}")
        payload = path.read_bytes()
        if len(payload) > MAX_PACK_MEMBER_BYTES:
            raise BridgeSceneFailure(f"runtime pack input is too large: {path}")
        payloads[name] = payload
        inventory[name] = {
            "sha256": sha256_bytes(payload),
            "size": len(payload),
        }

    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(
        destination,
        "w",
        compression=zipfile.ZIP_DEFLATED,
        compresslevel=9,
        strict_timestamps=True,
    ) as archive:
        for name, payload in payloads.items():
            archive.writestr(deterministic_zip_info(name), payload)

    with zipfile.ZipFile(destination, "r") as archive:
        names = archive.namelist()
        if names != sorted(members) or len(names) != len(set(names)):
            raise BridgeSceneFailure("runtime pack inventory is not canonical")
        if archive.testzip() is not None:
            raise BridgeSceneFailure("runtime pack CRC validation failed")
    return inventory, sha256_file(destination)


def infer_runtime_content(executable: Path) -> Path:
    candidates = (
        executable.resolve().parent / "content",
        executable.resolve().parent.parent / "Resources" / "content",
    )
    for candidate in candidates:
        if candidate.is_dir():
            return candidate.resolve()
    raise BridgeSceneFailure(
        "could not infer packaged content beside the executable; "
        "supply --runtime-content"
    )


def renderer_contract(target_platform: str) -> RendererContract:
    try:
        return RENDERER_CONTRACTS[target_platform]
    except KeyError as error:
        raise BridgeSceneFailure(
            f"unsupported CityWorld runtime platform: {target_platform}"
        ) from error


def runtime_layout(isolated_home: Path, target_platform: str) -> dict[str, Path]:
    renderer_contract(target_platform)
    if target_platform == "darwin":
        user = (
            isolated_home
            / "Library"
            / "Application Support"
            / "Rigs of Rods"
        )
        logs = isolated_home / "Library" / "Logs" / "Rigs of Rods"
    elif target_platform == "win32":
        user = isolated_home / "My Games" / "Rigs of Rods"
        logs = user / "logs"
    elif target_platform == "linux":
        user = isolated_home / ".rigsofrods"
        logs = user / "logs"
    else:
        raise BridgeSceneFailure(
            f"unsupported CityWorld runtime platform: {target_platform}"
        )
    return {
        "config": user / "config",
        "logs": logs,
        "mods": user / "mods",
        "screenshots": user / "screenshots",
        "user": user,
    }


def require_isolated_runtime_executable(
    executable: Path,
    target_platform: str,
) -> None:
    """Reject portable layouts that outrank the diagnostic home override."""

    renderer_contract(target_platform)
    if (
        target_platform != "darwin"
        and (executable.parent / "config").is_dir()
    ):
        raise BridgeSceneFailure(
            "portable executable config would bypass the isolated scene home"
        )


def isolated_runtime_environment(isolated_home: Path) -> dict[str, str]:
    """Build an isolated native environment without Linux Snap precedence."""

    environment = os.environ.copy()
    environment.pop("SNAP_USER_COMMON", None)
    environment["ROR_D0_SCENE_HOME"] = str(isolated_home)
    environment["ROR_D0_EXACT_WINDOW_EXTENT"] = (
        EXACT_WINDOW_EXTENT_CONTRACT
    )
    environment["ALSOFT_DRIVERS"] = "null"
    environment["ALSOFT_LOGLEVEL"] = "0"
    return environment


def write_runtime_config(
    config_directory: Path,
    shadow_mode: str = "none",
    shadow_quality: int = 2,
    *,
    postprocess_mode: str = "none",
    physics_mode: str = "async",
    target_platform: str = sys.platform,
) -> tuple[Path, Path]:
    if shadow_mode not in ("none", "pssm"):
        raise BridgeSceneFailure(f"unsupported shadow mode: {shadow_mode}")
    if not 0 <= shadow_quality <= 3:
        raise BridgeSceneFailure(
            f"shadow quality is outside 0..3: {shadow_quality}"
        )
    if postprocess_mode not in POSTPROCESS_MODES:
        raise BridgeSceneFailure(
            f"unsupported post-processing mode: {postprocess_mode}"
        )
    if physics_mode not in PHYSICS_MODES:
        raise BridgeSceneFailure(
            f"unsupported physics mode: {physics_mode}"
        )
    contract = renderer_contract(target_platform)
    config_directory.mkdir(parents=True, exist_ok=True)
    ror_config = config_directory / "RoR.cfg"
    ror_config.write_text(
        "\n".join(
            (
                "; Generated by tools/run_cityworld_bridge_scene.py",
                "app_config_long_names=false",
                "app_num_workers=1",
                "app_async_physics="
                + str(PHYSICS_MODES[physics_mode]).lower(),
                "app_disable_online_api=true",
                "app_force_cache_update=true",
                "audio_master_volume=0",
                "gfx_fps_limit=0",
                "gfx_postprocess_mode="
                + str(POSTPROCESS_MODES[postprocess_mode]),
                "gfx_shadow_type="
                + (
                    "Parallel-split Shadow Maps"
                    if shadow_mode == "pssm"
                    else "No shadows (fastest)"
                ),
                "gfx_shadow_quality=" + str(shadow_quality),
                "gfx_sky_mode=0",
                "gfx_water_mode=1",
                "",
            )
        ),
        encoding="utf-8",
    )
    ogre_config = config_directory / "ogre.cfg"
    ogre_config.write_text(
        "\n".join(contract.config_lines),
        encoding="utf-8",
    )
    return ror_config, ogre_config


def build_scene_command(executable: Path) -> tuple[str, ...]:
    command = [str(executable)]
    if sys.platform == "darwin":
        command.extend(("-ApplePersistenceIgnoreState", "YES"))
    command.extend(("-map", TERRAIN))
    return tuple(command)


def read_required(path: Path, label: str) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except FileNotFoundError as error:
        raise BridgeSceneFailure(f"{label} was not created: {path}") from error


def read_required_config(path: Path, label: str) -> bytes:
    if not path.is_file() or path.is_symlink():
        raise BridgeSceneFailure(f"{label} was not created: {path}")
    size = path.stat().st_size
    if size <= 0 or size > MAX_CONFIG_BYTES:
        raise BridgeSceneFailure(f"{label} has an invalid size: {size}")
    return path.read_bytes()


def validate_physics_config(
    payload: bytes,
    physics_mode: str,
) -> dict[str, object]:
    """Prove the requested single-worker physics mode stayed effective."""

    if physics_mode not in PHYSICS_MODES:
        raise BridgeSceneFailure(
            f"unsupported physics mode: {physics_mode}"
        )
    try:
        text = payload.decode("utf-8")
    except UnicodeDecodeError as error:
        raise BridgeSceneFailure(
            "RoR physics configuration is not UTF-8"
        ) from error
    settings: dict[str, list[str]] = {
        "app_async_physics": [],
        "app_num_workers": [],
    }
    for raw_line in text.splitlines():
        line = raw_line.strip()
        for name in settings:
            prefix = name + "="
            if line.startswith(prefix):
                settings[name].append(line[len(prefix) :].strip())
    async_values = settings["app_async_physics"]
    if len(async_values) != 1:
        raise BridgeSceneFailure(
            "RoR configuration does not prove the requested physics mode"
        )
    # The generated input uses true/false, while RoR's CVar serializer rewrites
    # the effective saved configuration to its canonical Yes/No spelling.
    # Admit only those two exact representations and continue rejecting
    # missing, duplicate, ambiguous, or otherwise permissive boolean text.
    normalized_booleans = {
        "true": True,
        "false": False,
        "Yes": True,
        "No": False,
    }
    async_physics = normalized_booleans.get(async_values[0])
    if async_physics is None or async_physics != PHYSICS_MODES[physics_mode]:
        raise BridgeSceneFailure(
            "RoR configuration does not prove the requested physics mode"
        )
    if settings["app_num_workers"] != ["1"]:
        raise BridgeSceneFailure(
            "RoR configuration does not prove single-worker physics"
        )
    return {
        "async_physics": async_physics,
        "num_workers": 1,
    }


def process_termination_record(
    returncode: int,
    target_platform: str,
) -> dict[str, object]:
    """Describe a native process exit without losing Windows NTSTATUS bits."""

    renderer_contract(target_platform)
    record: dict[str, object] = {"returncode": returncode}
    if returncode == 0:
        record["kind"] = "success"
        return record

    unsigned = returncode & 0xFFFFFFFF
    if target_platform == "win32" and unsigned >= 0x80000000:
        record.update(
            {
                "kind": "windows_ntstatus",
                "ntstatus_hex": f"0x{unsigned:08X}",
                "unsigned_returncode": unsigned,
            }
        )
        if unsigned == 0xC0000005:
            record["meaning"] = "access_violation"
        return record

    if returncode < 0:
        record.update(
            {
                "kind": "signal",
                "signal": -returncode,
            }
        )
        return record

    record["kind"] = "exit_code"
    return record


def persist_runtime_process_diagnostics(
    artifact_dir: Path,
    completed: subprocess.CompletedProcess[bytes],
    engine_log_path: Path,
    script_log_path: Path,
    requested_configs: Mapping[str, bytes],
    effective_config_paths: Mapping[str, Path],
    target_platform: str,
) -> dict[str, object]:
    """Persist child-process evidence before semantic validation can fail."""

    diagnostics = artifact_dir / "diagnostics"
    diagnostics.mkdir(exist_ok=True)
    files: dict[str, dict[str, object]] = {}
    last_lines: dict[str, str] = {}

    def capture(name: str, payload: bytes) -> None:
        destination = diagnostics / name
        destination.write_bytes(payload)
        files[name] = {
            "artifact": "diagnostics/" + name,
            "sha256": sha256_bytes(payload),
            "size": len(payload),
            "status": "captured",
        }
        if name.endswith(".log") or name.startswith("runtime.std"):
            lines = [
                line
                for line in decode_output(payload).splitlines()
                if line.strip()
            ]
            if lines:
                last_lines[name] = lines[-1][:512]

    raw_stdout = completed.stdout
    if raw_stdout is None:
        raw_stdout = b""
    if not isinstance(raw_stdout, bytes):
        raise BridgeSceneFailure(
            "native runtime stdout was not captured as bytes"
        )
    capture("runtime.stdout", raw_stdout)

    raw_stderr = completed.stderr
    if raw_stderr is None:
        raw_stderr = b""
    if not isinstance(raw_stderr, bytes):
        raise BridgeSceneFailure(
            "native runtime stderr was not captured as bytes"
        )
    capture("runtime.stderr", raw_stderr)

    for name, source in (
        ("RoR.log", engine_log_path),
        ("Angelscript.log", script_log_path),
    ):
        entry: dict[str, object] = {
            "artifact": "diagnostics/" + name,
        }
        if not source.exists():
            entry["status"] = "missing"
        elif not source.is_file() or source.is_symlink():
            entry["status"] = "unsafe"
        else:
            capture(name, source.read_bytes())
            continue
        files[name] = entry

    for name in sorted(requested_configs):
        requested_name = "requested-" + name
        capture(requested_name, requested_configs[name])

        effective_name = "effective-" + name
        effective_source = effective_config_paths[name]
        effective_entry: dict[str, object] = {
            "artifact": "diagnostics/" + effective_name,
        }
        if not effective_source.exists():
            effective_entry["status"] = "missing"
        elif (
            not effective_source.is_file()
            or effective_source.is_symlink()
        ):
            effective_entry["status"] = "unsafe"
        else:
            capture(effective_name, effective_source.read_bytes())
            continue
        files[effective_name] = effective_entry

    document: dict[str, object] = {
        "files": files,
        "format": PROCESS_DIAGNOSTIC_FORMAT,
        "termination": process_termination_record(
            completed.returncode,
            target_platform,
        ),
        "last_lines": last_lines,
        "target_platform": target_platform,
    }
    path = diagnostics / "runtime-process.json"
    temporary = diagnostics / "runtime-process.json.tmp"
    temporary.write_text(
        json.dumps(document, indent=2, ensure_ascii=True, sort_keys=True)
        + "\n",
        encoding="utf-8",
    )
    os.replace(temporary, path)
    return document


def validate_pssm_log(
    engine_log: str,
    shadow_mode: str,
    shadow_quality: int,
) -> dict[str, object] | None:
    marker_count = engine_log.count(PSSM_PREFIX)
    matches = list(PSSM_PATTERN.finditer(engine_log))
    if shadow_mode == "none":
        if marker_count != 0 or matches:
            raise BridgeSceneFailure(
                "shadow-disabled run unexpectedly enabled PSSM"
            )
        return None
    if shadow_mode != "pssm":
        raise BridgeSceneFailure(f"unsupported shadow mode: {shadow_mode}")
    if marker_count != 1 or len(matches) != 1:
        raise BridgeSceneFailure(
            "PSSM run must emit exactly one complete renderer marker"
        )

    fields = matches[0].groupdict()
    sizes = tuple(
        (
            int(fields[f"w{index}"]),
            int(fields[f"h{index}"]),
        )
        for index in range(3)
    )
    split_points = tuple(float(fields[f"s{index}"]) for index in range(4))
    near = float(fields["near"])
    far = float(fields["far"])
    split_lambda = float(fields["lambda"])
    numeric_values = (*split_points, near, far, split_lambda)
    if not all(math.isfinite(value) for value in numeric_values):
        raise BridgeSceneFailure("PSSM marker contains non-finite values")
    if shadow_quality not in PSSM_QUALITY_PROFILES:
        raise BridgeSceneFailure(
            f"shadow quality is outside 0..3: {shadow_quality}"
        )
    expected_sizes, expected_lambda = PSSM_QUALITY_PROFILES[shadow_quality]
    if int(fields["quality"]) != shadow_quality:
        raise BridgeSceneFailure("PSSM marker quality differs from requested")
    if int(fields["cascades"]) != 3:
        raise BridgeSceneFailure("PSSM marker has the wrong cascade count")
    if int(fields["receiver"]) != 1:
        raise BridgeSceneFailure("PSSM RTSS receiver is not active")
    if fields["format"] != "PF_DEPTH16":
        raise BridgeSceneFailure("PSSM depth texture format is not PF_DEPTH16")
    if sizes != expected_sizes:
        raise BridgeSceneFailure("PSSM texture sizes differ from quality profile")
    if abs(split_lambda - expected_lambda) > 1.0e-6:
        raise BridgeSceneFailure("PSSM split lambda differs from quality profile")
    if not (
        near > 0.0
        and abs(split_points[0] - near) <= 1.0e-6
        and split_points[0] < split_points[1] < split_points[2] < split_points[3]
        and abs(split_points[3] - far) <= 1.0e-6
        and abs(far - 350.0) <= 1.0e-3
    ):
        raise BridgeSceneFailure("PSSM split points are inconsistent")
    return {
        "cascades": 3,
        "far": far,
        "format": fields["format"],
        "lambda": split_lambda,
        "near": near,
        "rtss_receiver": True,
        "sizes": [list(size) for size in sizes],
        "split_points": list(split_points),
    }


def validate_postprocess_log(
    engine_log: str,
    postprocess_mode: str,
    target_platform: str,
) -> dict[str, object]:
    if postprocess_mode not in POSTPROCESS_MODES:
        raise BridgeSceneFailure(
            f"unsupported post-processing mode: {postprocess_mode}"
        )
    expected_backend = POSTPROCESS_BACKENDS.get(target_platform)
    if expected_backend is None:
        raise BridgeSceneFailure(
            "unsupported CityWorld runtime platform: "
            f"{target_platform}"
        )
    marker_count = engine_log.count(POSTPROCESS_PREFIX)
    all_matches = list(POSTPROCESS_PATTERN.finditer(engine_log))
    matches = [
        match
        for match in all_matches
        if match.group("event") == "scene_ready"
    ]
    if (
        marker_count < 1
        or len(all_matches) != marker_count
        or len(matches) != 1
    ):
        raise BridgeSceneFailure(
            "post-processing run must emit exactly one complete "
            "scene_ready marker"
        )
    if any(
        match.group("status") == "program_unavailable"
        or match.group("stage") == "failed"
        for match in all_matches
    ):
        raise BridgeSceneFailure(
            "post-processing lifecycle failed after scene attachment"
        )
    fields = matches[0].groupdict()
    requested = int(fields["requested"])
    effective = int(fields["effective"])
    width = int(fields["width"])
    height = int(fields["height"])
    expected_mode = POSTPROCESS_MODES[postprocess_mode]
    if requested != expected_mode:
        raise BridgeSceneFailure(
            "post-processing marker differs from the requested mode"
        )
    if fields["backend"] != expected_backend:
        raise BridgeSceneFailure(
            "post-processing backend differs from the platform contract"
        )
    if width != EXPECTED_WIDTH or height != EXPECTED_HEIGHT:
        raise BridgeSceneFailure(
            "post-processing backing extent differs from the fixed "
            "render target"
        )
    expected = (
        (0, "requested_none", "bypassed")
        if postprocess_mode == "none"
        else (1, "enabled", "attached")
    )
    if (
        effective != expected[0]
        or fields["status"] != expected[1]
        or fields["stage"] != expected[2]
    ):
        raise BridgeSceneFailure(
            "post-processing marker does not prove the requested "
            "effective lifecycle"
        )
    expected_renderer = renderer_contract(target_platform).render_system
    if fields["renderer"] != expected_renderer:
        raise BridgeSceneFailure(
            "post-processing marker renderer differs from the "
            "platform contract"
        )
    return {
        "backend": fields["backend"],
        "backing_height": height,
        "backing_width": width,
        "detail": fields["detail"],
        "effective_mode": effective,
        "lifecycle_stage": fields["stage"],
        "requested_mode": requested,
        "status": fields["status"],
    }


def parse_renderer_identity(
    engine_log: str,
    target_platform: str,
) -> dict[str, str]:
    contract = renderer_contract(target_platform)
    identity: dict[str, str] = {}
    patterns = {
        "api_version": contract.api_version_pattern,
        **RENDERER_IDENTITY_PATTERNS,
    }
    for field, pattern in patterns.items():
        values = [
            match.group("value").strip()
            for match in pattern.finditer(engine_log)
        ]
        if len(values) != 1 or not values[0]:
            raise BridgeSceneFailure(
                f"expected exactly one renderer identity value: {field}"
            )
        identity[field] = values[0]
    if identity["render_system"] != contract.render_system:
        raise BridgeSceneFailure(
            "renderer identity differs from the platform contract: "
            f"expected {contract.render_system}, "
            f"got {identity['render_system']}"
        )
    return identity


def normalize_shadow_config(payload: bytes) -> bytes:
    text = payload.decode("utf-8")
    lines = text.splitlines(keepends=True)
    matches = 0
    normalized: list[str] = []
    for line in lines:
        if line.startswith("gfx_shadow_type="):
            ending = "\n" if line.endswith("\n") else ""
            normalized.append("gfx_shadow_type=<paired-shadow-mode>" + ending)
            matches += 1
        else:
            normalized.append(line)
    if matches != 1:
        raise BridgeSceneFailure(
            "RoR configuration must contain exactly one shadow-mode setting"
        )
    return "".join(normalized).encode("utf-8")


def normalize_postprocess_config(payload: bytes) -> bytes:
    text = payload.decode("utf-8")
    lines = text.splitlines(keepends=True)
    matches = 0
    normalized: list[str] = []
    for line in lines:
        if line.startswith("gfx_postprocess_mode="):
            ending = "\n" if line.endswith("\n") else ""
            normalized.append(
                "gfx_postprocess_mode=<paired-postprocess-mode>"
                + ending
            )
            matches += 1
        else:
            normalized.append(line)
    if matches != 1:
        raise BridgeSceneFailure(
            "RoR configuration must contain exactly one "
            "post-processing-mode setting"
        )
    return "".join(normalized).encode("utf-8")


def validate_runtime_logs(
    returncode: int,
    stdout: str,
    engine_log: str,
    script_log: str,
) -> dict[str, float | int]:
    if returncode != 0:
        if returncode < 0:
            raise BridgeSceneFailure(
                f"RoR bridge scene terminated by signal {-returncode}"
            )
        raise BridgeSceneFailure(f"RoR bridge scene exited with {returncode}")
    for marker in SCRIPT_MARKERS:
        if marker not in script_log:
            raise BridgeSceneFailure(f"AngelScript log missed marker: {marker}")
    for marker in ENGINE_MARKERS:
        if marker not in engine_log:
            raise BridgeSceneFailure(f"engine log missed marker: {marker}")
    combined = "\n".join((stdout, engine_log, script_log))
    for marker in FATAL_MARKERS:
        if marker in combined:
            raise BridgeSceneFailure(
                f"bridge runtime logged a fatal marker: {marker}"
            )

    matches = list(PASS_PATTERN.finditer(script_log))
    if len(matches) != 1:
        raise BridgeSceneFailure(
            f"expected exactly one bridge PASS record, found {len(matches)}"
        )
    record = matches[0].groupdict()
    metrics: dict[str, float | int] = {
        "distance_m": float(record["distance"]),
        "min_y": float(record["min_y"]),
        "max_y": float(record["max_y"]),
        "lateral_error_m": float(record["lateral"]),
        "speed_mps": float(record["speed"]),
        "physics_steps": int(record["steps"]),
    }
    floats = [value for value in metrics.values() if isinstance(value, float)]
    if not all(math.isfinite(value) for value in floats):
        raise BridgeSceneFailure("bridge PASS metrics contain non-finite values")
    if not 85.0 <= metrics["distance_m"] <= 100.0:
        raise BridgeSceneFailure("bridge traversal distance is outside its gate")
    if not -1.0 <= metrics["min_y"] <= metrics["max_y"] <= 5.0:
        raise BridgeSceneFailure("bridge traversal vertical envelope is invalid")
    if metrics["max_y"] - metrics["min_y"] > 2.5:
        raise BridgeSceneFailure("bridge traversal vertical travel is excessive")
    if not 0.0 <= metrics["lateral_error_m"] <= 2.0:
        raise BridgeSceneFailure("bridge traversal lateral drift is excessive")
    if not 0.0 < metrics["speed_mps"] < 40.0:
        raise BridgeSceneFailure("bridge exit speed is outside its gate")
    if not 1 <= metrics["physics_steps"] <= 30000:
        raise BridgeSceneFailure("bridge physics-step count is outside its gate")
    return metrics


def paeth_predictor(left: int, above: int, upper_left: int) -> int:
    prediction = left + above - upper_left
    left_distance = abs(prediction - left)
    above_distance = abs(prediction - above)
    diagonal_distance = abs(prediction - upper_left)
    if left_distance <= above_distance and left_distance <= diagonal_distance:
        return left
    if above_distance <= diagonal_distance:
        return above
    return upper_left


def decode_rgb_png(path: Path) -> tuple[dict[str, object], bytes]:
    if not path.is_file() or path.is_symlink():
        raise BridgeSceneFailure(f"RGB screenshot is missing: {path}")
    payload = path.read_bytes()
    if not 8 <= len(payload) <= MAX_SCREENSHOT_BYTES:
        raise BridgeSceneFailure("RGB screenshot size is invalid")
    if payload[:8] != b"\x89PNG\r\n\x1a\n":
        raise BridgeSceneFailure("RGB screenshot has no PNG signature")

    offset = 8
    chunks: list[tuple[bytes, bytes]] = []
    while offset < len(payload):
        if len(payload) - offset < 12:
            raise BridgeSceneFailure("RGB screenshot has a truncated PNG chunk")
        length = struct.unpack(">I", payload[offset : offset + 4])[0]
        if length > MAX_SCREENSHOT_BYTES or offset + 12 + length > len(payload):
            raise BridgeSceneFailure("RGB screenshot PNG chunk is invalid")
        chunk_type = payload[offset + 4 : offset + 8]
        chunk_data = payload[offset + 8 : offset + 8 + length]
        expected_crc = struct.unpack(
            ">I", payload[offset + 8 + length : offset + 12 + length]
        )[0]
        actual_crc = binascii.crc32(chunk_type)
        actual_crc = binascii.crc32(chunk_data, actual_crc) & 0xFFFFFFFF
        if actual_crc != expected_crc:
            raise BridgeSceneFailure("RGB screenshot PNG CRC is invalid")
        chunks.append((chunk_type, chunk_data))
        offset += 12 + length
        if chunk_type == b"IEND":
            break
    if offset != len(payload) or not chunks or chunks[-1][0] != b"IEND":
        raise BridgeSceneFailure("RGB screenshot PNG has invalid termination")
    if chunks[0][0] != b"IHDR" or len(chunks[0][1]) != 13:
        raise BridgeSceneFailure("RGB screenshot PNG has invalid IHDR")
    width, height, depth, color, compression, filtering, interlace = struct.unpack(
        ">IIBBBBB", chunks[0][1]
    )
    if (width, height) != (EXPECTED_WIDTH, EXPECTED_HEIGHT):
        raise BridgeSceneFailure(
            f"RGB screenshot dimensions are {width}x{height}; expected "
            f"{EXPECTED_WIDTH}x{EXPECTED_HEIGHT}"
        )
    if (depth, color, compression, filtering, interlace) != (8, 2, 0, 0, 0):
        raise BridgeSceneFailure(
            "RGB screenshot must be 8-bit non-interlaced truecolour PNG"
        )
    idat = b"".join(data for kind, data in chunks if kind == b"IDAT")
    if not idat:
        raise BridgeSceneFailure("RGB screenshot PNG has no IDAT payload")

    row_bytes = width * 3
    expected_decoded = height * (row_bytes + 1)
    inflater = zlib.decompressobj()
    try:
        decoded = inflater.decompress(idat, expected_decoded + 1)
        if len(decoded) > expected_decoded:
            raise BridgeSceneFailure(
                "RGB screenshot PNG expands beyond its declared dimensions"
            )
        decoded += inflater.flush(expected_decoded + 1 - len(decoded))
    except zlib.error as error:
        raise BridgeSceneFailure(
            f"RGB screenshot PNG cannot be decompressed: {error}"
        ) from error
    if (
        len(decoded) != expected_decoded
        or not inflater.eof
        or inflater.unused_data
        or inflater.unconsumed_tail
    ):
        raise BridgeSceneFailure("RGB screenshot PNG decoded size is invalid")

    previous = bytearray(row_bytes)
    pixels = bytearray()
    cursor = 0
    for _ in range(height):
        filter_type = decoded[cursor]
        cursor += 1
        if filter_type > 4:
            raise BridgeSceneFailure("RGB screenshot PNG uses invalid filtering")
        raw = decoded[cursor : cursor + row_bytes]
        cursor += row_bytes
        row = bytearray(row_bytes)
        for index, value in enumerate(raw):
            left = row[index - 3] if index >= 3 else 0
            above = previous[index]
            upper_left = previous[index - 3] if index >= 3 else 0
            if filter_type == 0:
                predictor = 0
            elif filter_type == 1:
                predictor = left
            elif filter_type == 2:
                predictor = above
            elif filter_type == 3:
                predictor = (left + above) // 2
            else:
                predictor = paeth_predictor(left, above, upper_left)
            row[index] = (value + predictor) & 0xFF
        pixels.extend(row)
        previous = row

    minimum = min(pixels)
    maximum = max(pixels)
    mean = sum(pixels) / len(pixels)
    sampled_colours = {
        bytes(pixels[index : index + 3])
        for index in range(0, len(pixels) - 2, 3 * 997)
    }
    if maximum - minimum < 32 or len(sampled_colours) < 16:
        raise BridgeSceneFailure("RGB screenshot is visually degenerate")
    if not 5.0 < mean < 250.0:
        raise BridgeSceneFailure("RGB screenshot luminance is degenerate")
    record = {
        "channel_max": maximum,
        "channel_mean": round(mean, 6),
        "channel_min": minimum,
        "height": height,
        "sampled_colours": len(sampled_colours),
        "sha256": sha256_bytes(payload),
        "size": len(payload),
        "width": width,
    }
    return record, bytes(pixels)


def validate_rgb_png(path: Path) -> dict[str, object]:
    record, _ = decode_rgb_png(path)
    return record


def find_single_screenshot(directory: Path) -> Path:
    screenshots = sorted(directory.glob("*.png"))
    if len(screenshots) != 1:
        raise BridgeSceneFailure(
            f"expected exactly one RGB screenshot in {directory}, "
            f"found {len(screenshots)}"
        )
    return screenshots[0]


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument(
        "--repository",
        type=Path,
        default=Path(__file__).resolve().parents[1],
    )
    parser.add_argument("--runtime-content", type=Path)
    parser.add_argument("--artifact-dir", required=True, type=Path)
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument(
        "--shadow-mode",
        choices=("none", "pssm"),
        default="none",
    )
    parser.add_argument(
        "--shadow-quality",
        type=int,
        choices=range(4),
        default=2,
    )
    parser.add_argument(
        "--postprocess-mode",
        choices=tuple(POSTPROCESS_MODES),
        default="none",
    )
    parser.add_argument(
        "--physics-mode",
        choices=tuple(PHYSICS_MODES),
        default="async",
    )
    args = parser.parse_args(argv)
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    return args


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    target_platform = sys.platform
    renderer_contract(target_platform)
    repository = args.repository.resolve()
    executable = args.executable.resolve()
    artifact_dir = args.artifact_dir.resolve()
    if not executable.is_file():
        raise BridgeSceneFailure(f"RoR executable does not exist: {executable}")
    if artifact_dir.exists():
        raise BridgeSceneFailure(
            f"artifact directory already exists; choose a fresh path: "
            f"{artifact_dir}"
        )
    artifact_dir.mkdir(parents=True)
    require_isolated_runtime_executable(executable, target_platform)

    compile_report, compiled_outputs = validate_cityworld_package(
        repository, args.timeout
    )
    source_content = verify_pinned_content(repository)
    runtime_content = (
        infer_runtime_content(executable)
        if args.runtime_content is None
        else args.runtime_content.resolve()
    )
    if not runtime_content.is_dir():
        raise BridgeSceneFailure(
            f"runtime content directory does not exist: {runtime_content}"
        )
    vehicle_archive = verify_vehicle_archive(source_content, runtime_content)

    isolated_home = artifact_dir / "work" / "scene-home"
    layout = runtime_layout(isolated_home, target_platform)
    for key in ("logs", "mods", "screenshots"):
        layout[key].mkdir(parents=True, exist_ok=True)
    ror_config_path, ogre_config_path = write_runtime_config(
        layout["config"],
        args.shadow_mode,
        args.shadow_quality,
        postprocess_mode=args.postprocess_mode,
        physics_mode=args.physics_mode,
        target_platform=target_platform,
    )
    requested_configs = {
        "RoR.cfg": read_required_config(
            ror_config_path,
            "requested RoR configuration",
        ),
        "ogre.cfg": read_required_config(
            ogre_config_path,
            "requested OGRE configuration",
        ),
    }
    pack_path = layout["mods"] / RUNTIME_PACK
    pack_inventory, pack_sha = build_runtime_pack(
        repository,
        source_content,
        compiled_outputs,
        pack_path,
    )

    environment = isolated_runtime_environment(isolated_home)
    command = build_scene_command(executable)
    completed = run_command(
        command,
        args.timeout,
        cwd=executable.parent,
        environment=environment,
    )
    process_output = decode_process_output(completed)
    engine_log_path = layout["logs"] / "RoR.log"
    script_log_path = layout["logs"] / "Angelscript.log"
    persist_runtime_process_diagnostics(
        artifact_dir,
        completed,
        engine_log_path,
        script_log_path,
        requested_configs,
        {
            "RoR.cfg": ror_config_path,
            "ogre.cfg": ogre_config_path,
        },
        target_platform,
    )
    engine_log = read_required(engine_log_path, "RoR engine log")
    script_log = read_required(script_log_path, "AngelScript log")
    metrics = validate_runtime_logs(
        completed.returncode,
        process_output,
        engine_log,
        script_log,
    )
    pssm_record = validate_pssm_log(
        engine_log,
        args.shadow_mode,
        args.shadow_quality,
    )
    postprocess_record = validate_postprocess_log(
        engine_log,
        args.postprocess_mode,
        target_platform,
    )
    renderer_identity = parse_renderer_identity(engine_log, target_platform)
    effective_configs = {
        "RoR.cfg": read_required_config(
            ror_config_path,
            "effective RoR configuration",
        ),
        "ogre.cfg": read_required_config(
            ogre_config_path,
            "effective OGRE configuration",
        ),
    }
    physics_record = {
        "effective": validate_physics_config(
            effective_configs["RoR.cfg"],
            args.physics_mode,
        ),
        "mode": args.physics_mode,
        "requested": validate_physics_config(
            requested_configs["RoR.cfg"],
            args.physics_mode,
        ),
    }
    screenshot = find_single_screenshot(layout["screenshots"])
    image_record = validate_rgb_png(screenshot)

    diagnostics = artifact_dir / "diagnostics"
    rgb_directory = artifact_dir / "rgb"
    diagnostics.mkdir(exist_ok=True)
    rgb_directory.mkdir()
    stdout_path = diagnostics / "runtime.stdout"
    stderr_path = diagnostics / "runtime.stderr"
    copied_engine_log = diagnostics / "RoR.log"
    copied_script_log = diagnostics / "Angelscript.log"
    copied_screenshot = rgb_directory / RGB_ARTIFACT_NAME
    copied_configs: dict[str, dict[str, object]] = {}
    if not stdout_path.is_file() or not stderr_path.is_file():
        raise BridgeSceneFailure(
            "native runtime output diagnostics were not preserved"
        )
    copied_engine_log.write_text(engine_log, encoding="utf-8")
    copied_script_log.write_text(script_log, encoding="utf-8")
    shutil.copy2(screenshot, copied_screenshot)
    for name in sorted(requested_configs):
        copied_configs[name] = {}
        for phase, payload in (
            ("requested", requested_configs[name]),
            ("effective", effective_configs[name]),
        ):
            artifact_name = phase + "-" + name
            copied = diagnostics / artifact_name
            copied.write_bytes(payload)
            copied_configs[name][phase] = {
                "artifact": "diagnostics/" + artifact_name,
                "sha256": sha256_bytes(payload),
                "size": len(payload),
            }
        if name == "RoR.cfg":
            copied_configs[name][
                "requested_shadow_normalized_sha256"
            ] = sha256_bytes(
                normalize_shadow_config(requested_configs[name])
            )
            copied_configs[name][
                "effective_shadow_normalized_sha256"
            ] = sha256_bytes(
                normalize_shadow_config(effective_configs[name])
            )
            copied_configs[name][
                "requested_postprocess_normalized_sha256"
            ] = sha256_bytes(
                normalize_postprocess_config(requested_configs[name])
            )
            copied_configs[name][
                "effective_postprocess_normalized_sha256"
            ] = sha256_bytes(
                normalize_postprocess_config(effective_configs[name])
            )

    repository_commit = git_output(repository, ("rev-parse", "HEAD"))
    report: dict[str, object] = {
        "artifacts": {
            "effective_ogre_config": "diagnostics/effective-ogre.cfg",
            "effective_ror_config": "diagnostics/effective-RoR.cfg",
            "engine_log": "diagnostics/RoR.log",
            "process_diagnostic": "diagnostics/runtime-process.json",
            "requested_ogre_config": "diagnostics/requested-ogre.cfg",
            "requested_ror_config": "diagnostics/requested-RoR.cfg",
            "rgb": f"rgb/{RGB_ARTIFACT_NAME}",
            "script_log": "diagnostics/Angelscript.log",
            "stderr": "diagnostics/runtime.stderr",
            "stdout": "diagnostics/runtime.stdout",
        },
        "cityworld_compile_report_sha256": sha256_file(
            repository / COMPILE_REPORT
        ),
        "cityworld_compile_schema": compile_report.get("format"),
        "content_commit": CONTENT_COMMIT,
        "executable": str(executable),
        "executable_sha256": sha256_file(executable),
        "format": REPORT_FORMAT,
        "machine": platform.machine(),
        "metrics": metrics,
        "platform": platform.platform(),
        "physics": physics_record,
        "rendering": {
            "configs": copied_configs,
            "device": renderer_identity,
            "height": EXPECTED_HEIGHT,
            "postprocess": postprocess_record,
            "postprocess_mode": args.postprocess_mode,
            "pssm": pssm_record,
            "shadow_mode": args.shadow_mode,
            "shadow_quality": args.shadow_quality,
            "width": EXPECTED_WIDTH,
        },
        "repository_commit": repository_commit,
        "rgb": image_record,
        "runtime_content": str(runtime_content),
        "runtime_pack": {
            "members": pack_inventory,
            "sha256": pack_sha,
            "size": pack_path.stat().st_size,
        },
        "runners": {
            relative: {
                "path": relative,
                "sha256": sha256_file(repository / relative),
            }
            for relative in RUNNER_PATHS
        },
        "vehicle_archive": {
            "path": str(vehicle_archive),
            "sha256": sha256_file(vehicle_archive),
        },
    }
    overlapping_report_fields = set(report).intersection(EXTRA_REPORT_FIELDS)
    if overlapping_report_fields:
        raise BridgeSceneFailure(
            "runtime report extension collides with core fields: "
            + ", ".join(sorted(overlapping_report_fields))
        )
    report.update(EXTRA_REPORT_FIELDS)
    report_path = artifact_dir / "report.json"
    temporary = artifact_dir / "report.json.tmp"
    temporary.write_text(
        json.dumps(report, indent=2, ensure_ascii=True, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    os.replace(temporary, report_path)
    print(
        SUCCESS_PREFIX
        + ": "
        f"distance={metrics['distance_m']:.3f}m "
        f"{DEVIATION_LABEL}={metrics[DEVIATION_METRIC_KEY]:.3f}m "
        f"steps={metrics['physics_steps']} "
        f"physics={args.physics_mode} "
        f"rgb={image_record['sha256']} report={report_path}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except BridgeSceneFailure as error:
        print(f"CityWorld bridge scene gate failed: {error}", file=sys.stderr)
        raise SystemExit(1)
