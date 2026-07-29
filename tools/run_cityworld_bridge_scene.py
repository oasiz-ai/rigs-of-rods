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
from typing import Iterable, Mapping, Sequence
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
MAX_SCREENSHOT_BYTES = 32 * 1024 * 1024
MAX_PACK_MEMBER_BYTES = 64 * 1024 * 1024

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
            stderr=subprocess.STDOUT,
            timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired as error:
        raise BridgeSceneFailure(
            f"command exceeded {timeout} seconds: {' '.join(command)}"
        ) from error


def git_output(repository: Path, arguments: Sequence[str]) -> str:
    result = run_command(("git", "-C", str(repository), *arguments), 30)
    output = decode_output(result.stdout)
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
    commands = (
        (
            sys.executable,
            str(repository / "tools/validate_cityworld_asset.py"),
            str(repository / ASSET_MANIFEST),
            "--repo-root",
            str(repository),
        ),
        (
            sys.executable,
            str(repository / "tools/compile_cityworld_asset.py"),
            str(repository / ASSET_MANIFEST),
            "--repo-root",
            str(repository),
            "--validate-checked",
        ),
        (
            sys.executable,
            str(repository / "tools/build_cityworld_next_provenance.py"),
            "--repo-root",
            str(repository),
            "--check",
        ),
    )
    for command in commands:
        completed = run_command(command, timeout, cwd=repository)
        if completed.returncode != 0:
            raise BridgeSceneFailure(
                "CityWorld package validation failed: "
                + decode_output(completed.stdout)
            )

    report_path = repository / COMPILE_REPORT
    report = load_json(report_path)
    if report.get("format") != "ror-cityworld-scene-compile-report-v1":
        raise BridgeSceneFailure("unsupported CityWorld compile report")
    raw_outputs = report.get("outputs")
    if not isinstance(raw_outputs, list) or not raw_outputs:
        raise BridgeSceneFailure("CityWorld compile report has no outputs")
    outputs: list[Path] = []
    names: set[str] = set()
    for raw_output in raw_outputs:
        if not isinstance(raw_output, dict):
            raise BridgeSceneFailure("CityWorld compile output is not an object")
        path = resolve_repository_path(repository, raw_output.get("path"))
        if path.name in names:
            raise BridgeSceneFailure(
                f"CityWorld runtime output basename is duplicated: {path.name}"
            )
        names.add(path.name)
        if path.stat().st_size != raw_output.get("size"):
            raise BridgeSceneFailure(f"CityWorld output size drift: {path.name}")
        if sha256_file(path) != raw_output.get("sha256"):
            raise BridgeSceneFailure(f"CityWorld output hash drift: {path.name}")
        outputs.append(path)
    required_suffixes = {".material", ".odef", ".mesh"}
    if not required_suffixes.issubset({path.suffix for path in outputs}):
        raise BridgeSceneFailure("CityWorld runtime output types are incomplete")
    return report, tuple(sorted(outputs, key=lambda path: path.name))


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


def runtime_layout(isolated_home: Path, target_platform: str) -> dict[str, Path]:
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
    else:
        user = isolated_home / ".rigsofrods"
        logs = user / "logs"
    return {
        "config": user / "config",
        "logs": logs,
        "mods": user / "mods",
        "screenshots": user / "screenshots",
        "user": user,
    }


def write_runtime_config(config_directory: Path) -> tuple[Path, Path]:
    config_directory.mkdir(parents=True, exist_ok=True)
    ror_config = config_directory / "RoR.cfg"
    ror_config.write_text(
        "\n".join(
            (
                "; Generated by tools/run_cityworld_bridge_scene.py",
                "app_config_long_names=false",
                "app_num_workers=1",
                "app_async_physics=true",
                "app_disable_online_api=true",
                "app_force_cache_update=true",
                "audio_master_volume=0",
                "gfx_fps_limit=0",
                "gfx_shadow_type=0",
                "gfx_sky_mode=0",
                "gfx_water_mode=1",
                "",
            )
        ),
        encoding="utf-8",
    )
    ogre_config = config_directory / "ogre.cfg"
    ogre_config.write_text(
        "\n".join(
            (
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
            )
        ),
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


def validate_rgb_png(path: Path) -> dict[str, object]:
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
    return {
        "channel_max": maximum,
        "channel_mean": round(mean, 6),
        "channel_min": minimum,
        "height": height,
        "sampled_colours": len(sampled_colours),
        "sha256": sha256_bytes(payload),
        "size": len(payload),
        "width": width,
    }


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
    args = parser.parse_args(argv)
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    return args


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
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
    if sys.platform != "darwin" and (executable.parent / "config").is_dir():
        raise BridgeSceneFailure(
            "portable executable config would bypass the isolated scene home"
        )

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
    layout = runtime_layout(isolated_home, sys.platform)
    for key in ("logs", "mods", "screenshots"):
        layout[key].mkdir(parents=True, exist_ok=True)
    write_runtime_config(layout["config"])
    pack_path = layout["mods"] / RUNTIME_PACK
    pack_inventory, pack_sha = build_runtime_pack(
        repository,
        source_content,
        compiled_outputs,
        pack_path,
    )

    environment = os.environ.copy()
    environment["ROR_D0_SCENE_HOME"] = str(isolated_home)
    environment["ALSOFT_DRIVERS"] = "null"
    environment["ALSOFT_LOGLEVEL"] = "0"
    command = build_scene_command(executable)
    completed = run_command(
        command,
        args.timeout,
        cwd=executable.parent,
        environment=environment,
    )
    stdout = decode_output(completed.stdout)
    engine_log_path = layout["logs"] / "RoR.log"
    script_log_path = layout["logs"] / "Angelscript.log"
    engine_log = read_required(engine_log_path, "RoR engine log")
    script_log = read_required(script_log_path, "AngelScript log")
    metrics = validate_runtime_logs(
        completed.returncode,
        stdout,
        engine_log,
        script_log,
    )
    screenshot = find_single_screenshot(layout["screenshots"])
    image_record = validate_rgb_png(screenshot)

    diagnostics = artifact_dir / "diagnostics"
    rgb_directory = artifact_dir / "rgb"
    diagnostics.mkdir()
    rgb_directory.mkdir()
    stdout_path = diagnostics / "runtime.stdout"
    copied_engine_log = diagnostics / "RoR.log"
    copied_script_log = diagnostics / "Angelscript.log"
    copied_screenshot = rgb_directory / RGB_ARTIFACT_NAME
    stdout_path.write_text(stdout, encoding="utf-8")
    copied_engine_log.write_text(engine_log, encoding="utf-8")
    copied_script_log.write_text(script_log, encoding="utf-8")
    shutil.copy2(screenshot, copied_screenshot)

    repository_commit = git_output(repository, ("rev-parse", "HEAD"))
    report: dict[str, object] = {
        "artifacts": {
            "engine_log": "diagnostics/RoR.log",
            "rgb": f"rgb/{RGB_ARTIFACT_NAME}",
            "script_log": "diagnostics/Angelscript.log",
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
        f"rgb={image_record['sha256']} report={report_path}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except BridgeSceneFailure as error:
        print(f"CityWorld bridge scene gate failed: {error}", file=sys.stderr)
        raise SystemExit(1)
