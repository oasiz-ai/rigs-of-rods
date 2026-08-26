#!/usr/bin/env python3
"""Prove NiceMetal shader support in an isolated packaged combined runtime.

This gate deliberately proves only the hidden OGRE14 resource host.  Each
required placeholder marker is emitted after ActorSpawner has checked, loaded,
and compiled every GPU program referenced by that managed-material template.
The visible window must still belong to Ogre-Next, but this receipt is not
evidence that Ogre-Next drew a NiceMetal surface.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import sys
import time
from typing import Mapping, Sequence
import zipfile


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_FIXTURE_DIR = ROOT / "tests/fixtures/nicemetal_runtime"

RECEIPT_SCHEMA = "ror.nicemetal_resource_host_shader_smoke.v1"
EVIDENCE_CLASS = "resource-host-shader-evidence"
QUALIFICATION_SCOPE = (
    "ogre14-hidden-resource-host-managed-template-shader-compile-load"
)
ARCHIVE_NAME = "OgreNextNiceMetalShaderProbe.zip"
ARCHIVE_SHA256 = "28ac6c99ea01c4187d27b3905de34b6646f82da6d25270038232093898bf939e"
ACTOR_FILE = "OgreNextNiceMetalShaderProbe.truck"
ACTOR_NAME = "Ogre-Next NiceMetal shader qualification fixture"
TERRAIN = "simple2_a.terrn2"
WIDTH = 1280
HEIGHT = 720
MAX_LOG_BYTES = 256 * 1024 * 1024

FIXTURE_SHA256 = {
    ACTOR_FILE: "19c241bf692c8b40f8eb8bbd4b5d9d0f6cc05654f76ddd6c9df425dcda34ad78",
    "nicemetal_base.ppm": "abc6dd957b281982170b83396a98e256de2680c7a1d27c3b8e53d0725724b29e",
    "nicemetal_damage.ppm": "48aef2e6d0b63409f849b810ddd8bb4fcff2c87047b64339823f1e4b72c3e996",
    "nicemetal_specular.ppm": "25bd0415e6e5474e286eed230556ccfec4c151b8d4ef5c2296b7b4e50b76ba80",
}

SHADER_SOURCE_PATHS = (
    "materials/nicemetal.program",
    "materials/nicemetal_gl3plus.glsl",
    "materials/nicemetal_d3d11.hlsl",
    "managed_materials/nicemetal_mm.program",
    "managed_materials/nicemetal_mm_gl3plus.glsl",
    "managed_materials/nicemetal_mm_d3d11.hlsl",
)
REMOVED_CG_PATHS = (
    "materials/nicemetal.cg",
    "managed_materials/nicemetal_mm.cg",
)

MATERIAL_TEMPLATES = {
    "NiceMetalFlexNoDamage": (
        "managed/flexmesh_standard/specularonly_nicemetal",
        (
            "NiceMetal_VS_mm",
            "NiceMetal_PS_nodmg_mm",
            "NiceMetal_Reflect_VS_mm",
            "NiceMetal_Reflect_PS_mm",
        ),
    ),
    "NiceMetalFlexDamage": (
        "managed/flexmesh_standard/speculardamage_nicemetal",
        (
            "NiceMetal_VS_mm",
            "NiceMetal_PS_mm",
            "NiceMetal_Reflect_VS_mm",
            "NiceMetal_Reflect_PS_mm",
        ),
    ),
    "NiceMetalMesh": (
        "managed/mesh_standard/specular_nicemetal",
        (
            "NiceMetal_VS_mm",
            "SimpleMetal_PS_mm",
            "NiceMetal_Reflect_VS_mm",
            "NiceMetal_Reflect_nocolor_PS_mm",
        ),
    ),
    "NiceMetalFlexTransparentNoDamage": (
        "managed/flexmesh_transparent/specularonly_nicemetal",
        (
            "NiceMetal_VS_mm",
            "NiceMetal_transp_PS_nodmg_mm",
            "NiceMetal_Reflect_VS_mm",
            "NiceMetal_Reflect_PS_mm",
        ),
    ),
    "NiceMetalFlexTransparentDamage": (
        "managed/flexmesh_transparent/speculardamage_nicemetal",
        (
            "NiceMetal_VS_mm",
            "NiceMetal_transp_PS_mm",
            "NiceMetal_Reflect_VS_mm",
            "NiceMetal_Reflect_PS_mm",
        ),
    ),
    "NiceMetalMeshTransparent": (
        "managed/mesh_transparent/specular_nicemetal",
        (
            "NiceMetal_VS_mm",
            "SimpleMetal_transp_PS_mm",
            "NiceMetal_Reflect_VS_mm",
            "NiceMetal_Reflect_nocolor_PS_mm",
        ),
    ),
}

EXPECTED_MANAGED_LINES = (
    "NiceMetalFlexNoDamage flexmesh_standard nicemetal_base.ppm - nicemetal_specular.ppm",
    "NiceMetalFlexDamage flexmesh_standard nicemetal_base.ppm nicemetal_damage.ppm nicemetal_specular.ppm",
    "NiceMetalMesh mesh_standard nicemetal_base.ppm nicemetal_specular.ppm",
    "NiceMetalFlexTransparentNoDamage flexmesh_transparent nicemetal_base.ppm - nicemetal_specular.ppm",
    "NiceMetalFlexTransparentDamage flexmesh_transparent nicemetal_base.ppm nicemetal_damage.ppm nicemetal_specular.ppm",
    "NiceMetalMeshTransparent mesh_transparent nicemetal_base.ppm nicemetal_specular.ppm",
)

PRESENTATION_OWNER_PREFIX = (
    "[RoR|RendererCombined|Startup] presentation_owner=ogre-next "
    "visible_window=true legacy_visible_fallback=false"
)
RESOURCE_HOST_RECEIPT = (
    "[RoR|RendererCombined|Startup] resource_host=ogre14 "
    "visible_window=false protected=true"
)
SPAWN_MARKER = f"== Spawning vehicle: {ACTOR_NAME}"
RESOURCE_GROUP = f"{{bundle USER:/mods/{ARCHIVE_NAME}}}"
PLACEHOLDER_PREFIX = (
    "[RoR] DBG ActorSpawner::ProcessManagedMaterial(): Creating placeholder "
    "for material"
)
PLACEHOLDER_PATTERN = re.compile(
    re.escape(PLACEHOLDER_PREFIX)
    + r" '(?P<material>[^']+)' in group '(?P<group>[^']+)'"
)
RENDER_SYSTEM_PATTERN = re.compile(r"RenderSystem Name:\s*([^\r\n]+)")

PLATFORM_POLICY = {
    "linux": {
        "resource_host_render_system": "OpenGL 3+ Rendering Subsystem",
        "visible_render_system": "ogre-next-vulkan",
        "backend_child_suffix": "/GL3Plus",
    },
    "darwin": {
        "resource_host_render_system": "OpenGL 3+ Rendering Subsystem",
        "visible_render_system": "ogre-next-metal",
        "backend_child_suffix": "/GL3Plus",
    },
    "win32": {
        "resource_host_render_system": "Direct3D11 Rendering Subsystem",
        "visible_render_system": "ogre-next-d3d11",
        "backend_child_suffix": "/D3D11",
    },
}

FATAL_MARKERS = (
    "Validation Failed: Sampler error:",
    "GL_INVALID_",
    "RenderingAPIException",
    "OGRE EXCEPTION",
    "EXC_BAD_ACCESS",
    "Segmentation fault",
    "Access violation",
)

REJECTED_DIAGNOSTICS = {
    "unsupported_nicemetal_program": re.compile(
        r"Program '(?:NiceMetal|SimpleMetal)[^']*' is not supported",
        re.IGNORECASE,
    ),
    "nicemetal_compile_or_load_error": re.compile(
        r"^(?=[^\r\n]*(?:NiceMetal_|SimpleMetal_))"
        r"(?=[^\r\n]*(?:compil|load))"
        r"(?=[^\r\n]*(?:fail|error))[^\r\n]+$",
        re.IGNORECASE | re.MULTILINE,
    ),
    "nicemetal_rtss_fallback": re.compile(
        r"RTSS:[^\r\n]*nicemetal|Using the RTSS-compatible material",
        re.IGNORECASE,
    ),
    "nicemetal_script_error": re.compile(
        r"Error:\s*ScriptCompiler[^\r\n]*"
        r"(?:nicemetal|managed_mats_vehicles_(?:transparent_)?nicemetal)"
        r"[^\r\n]*\.(?:program|material)",
        re.IGNORECASE,
    ),
    "duplicate_placeholder": re.compile(
        r"ActorSpawner::ProcessManagedMaterial\(\): Placeholder already exists:",
        re.IGNORECASE,
    ),
}


class NiceMetalSmokeFailure(RuntimeError):
    """Fail-closed diagnostic for invalid smoke inputs or runtime evidence."""


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def host_platform() -> str:
    if sys.platform.startswith("linux"):
        return "linux"
    if sys.platform == "darwin":
        return "darwin"
    if sys.platform in ("win32", "cygwin"):
        return "win32"
    raise NiceMetalSmokeFailure(f"unsupported runtime platform: {sys.platform}")


def is_macos_app_bundle_executable(executable: Path) -> bool:
    process_dir = executable.parent
    return (
        process_dir.name == "MacOS"
        and process_dir.parent.name == "Contents"
        and process_dir.parent.parent.name.endswith(".app")
    )


def runtime_layout(
    isolated_home: Path, target_platform: str, executable: Path
) -> dict[str, Path]:
    if target_platform == "darwin":
        if not is_macos_app_bundle_executable(executable):
            raise NiceMetalSmokeFailure(
                "the macOS shader smoke requires a packaged .app executable"
            )
        user = isolated_home / "Library/Application Support/Rigs of Rods"
        logs = isolated_home / "Library/Logs/Rigs of Rods"
    elif target_platform == "win32":
        user = isolated_home / "My Games/Rigs of Rods"
        logs = user / "logs"
    elif target_platform == "linux":
        user = isolated_home / ".rigsofrods"
        logs = user / "logs"
    else:
        raise NiceMetalSmokeFailure(
            f"unsupported runtime platform: {target_platform}"
        )
    return {
        "config": user / "config",
        "logs": logs,
        "mods": user / "mods",
        "user": user,
    }


def expected_runtime_content(executable: Path, target_platform: str) -> Path:
    if target_platform in ("linux", "win32"):
        return executable.parent / "content"
    if target_platform == "darwin" and is_macos_app_bundle_executable(executable):
        return executable.parent.parent / "Resources/content"
    raise NiceMetalSmokeFailure(
        f"cannot derive packaged content for {target_platform}: {executable}"
    )


def packaged_resource_root(executable: Path, target_platform: str) -> Path:
    if target_platform in ("linux", "win32"):
        return executable.parent / "resources"
    if target_platform == "darwin" and is_macos_app_bundle_executable(executable):
        return executable.parent.parent / "Resources/resources"
    raise NiceMetalSmokeFailure(
        f"cannot derive packaged resources for {target_platform}: {executable}"
    )


def validate_packaged_shader_sources(
    executable: Path, target_platform: str
) -> dict[str, object]:
    unresolved_packaged_root = packaged_resource_root(executable, target_platform)
    if unresolved_packaged_root.is_symlink():
        raise NiceMetalSmokeFailure(
            f"the packaged resource root is indirect: {unresolved_packaged_root}"
        )
    packaged_root = unresolved_packaged_root.resolve()
    source_root = ROOT / "resources"
    if not packaged_root.is_dir() or packaged_root.is_symlink():
        raise NiceMetalSmokeFailure(
            f"the packaged resource root is unavailable: {packaged_root}"
        )
    archive_names = ("materials.zip", "managed_materials.zip")
    archive_members: dict[str, dict[str, bytes]] = {}
    archive_receipts: list[dict[str, object]] = []
    removed_cg_names = {Path(relative).name.lower() for relative in REMOVED_CG_PATHS}
    for archive_name in archive_names:
        archive_path = packaged_root / archive_name
        if not archive_path.is_file() or archive_path.is_symlink():
            raise NiceMetalSmokeFailure(
                f"the packaged NiceMetal source archive is unavailable: {archive_path}"
            )
        with zipfile.ZipFile(archive_path, mode="r") as archive:
            infos = archive.infolist()
            names = [info.filename for info in infos]
            duplicates = sorted({name for name in names if names.count(name) != 1})
            if duplicates:
                raise NiceMetalSmokeFailure(
                    f"the packaged source archive has duplicate members: "
                    f"{archive_name}: {duplicates!r}"
                )
            for info in infos:
                member = PurePosixPath(info.filename)
                if (
                    member.is_absolute()
                    or ".." in member.parts
                    or "\\" in info.filename
                    or stat.S_ISLNK((info.external_attr >> 16) & 0xFFFF)
                ):
                    raise NiceMetalSmokeFailure(
                        f"the packaged source archive has an unsafe member: "
                        f"{archive_name}!{info.filename}"
                    )
                if member.name.lower() in removed_cg_names:
                    raise NiceMetalSmokeFailure(
                        f"the packaged NiceMetal closure retained Cg: "
                        f"{archive_name}!{info.filename}"
                    )
            archive_members[archive_name] = {
                info.filename: archive.read(info.filename)
                for info in infos
                if not info.is_dir()
            }
        archive_receipts.append(
            {
                "path": f"resources/{archive_name}",
                "sha256": sha256_file(archive_path),
                "size": archive_path.stat().st_size,
                "member_names_unique": True,
                "unsafe_members_absent": True,
                "nicemetal_cg_members_absent": True,
            }
        )

    for relative in REMOVED_CG_PATHS:
        legacy = packaged_root / relative
        if legacy.exists() or legacy.is_symlink():
            raise NiceMetalSmokeFailure(
                f"the packaged NiceMetal closure retained loose Cg: {relative}"
            )

    gl3plus = target_platform in ("linux", "darwin")
    entries: list[dict[str, object]] = []
    for relative in SHADER_SOURCE_PATHS:
        source = source_root / relative
        if not source.is_file() or source.is_symlink():
            raise NiceMetalSmokeFailure(
                f"the NiceMetal source closure is incomplete: {relative}"
            )
        family, member_name = relative.split("/", maxsplit=1)
        archive_name = f"{family}.zip"
        locations = [
            candidate
            for candidate in archive_names
            if member_name in archive_members[candidate]
        ]
        if locations != [archive_name]:
            raise NiceMetalSmokeFailure(
                "the packaged NiceMetal source member location changed: "
                f"{member_name}: {locations!r}"
            )
        source_bytes = source.read_bytes()
        packaged_bytes = archive_members[archive_name][member_name]
        if packaged_bytes != source_bytes:
            raise NiceMetalSmokeFailure(
                f"the packaged NiceMetal source differs from the checkout: {relative}"
            )
        loose = packaged_root / relative
        loose_present = loose.exists() or loose.is_symlink()
        loose_required = family == "managed_materials"
        if loose_required and not loose_present:
            raise NiceMetalSmokeFailure(
                "the runtime-selected loose managed-material source is missing: "
                f"{relative}"
            )
        if loose_present and (
            not loose.is_file()
            or loose.is_symlink()
            or loose.read_bytes() != source_bytes
        ):
            raise NiceMetalSmokeFailure(
                f"the loose packaged NiceMetal source differs: {relative}"
            )
        if relative.endswith(".program"):
            selected = True
            role = "unified-program-script"
        elif relative.endswith("_gl3plus.glsl"):
            selected = gl3plus
            role = "gl3plus-shader-source"
        elif relative.endswith("_d3d11.hlsl"):
            selected = not gl3plus
            role = "d3d11-shader-source"
        else:
            raise NiceMetalSmokeFailure(
                f"unclassified NiceMetal source manifest entry: {relative}"
            )
        entries.append(
            {
                "source_path": f"resources/{relative}",
                "container_path": f"resources/{archive_name}",
                "member_path": member_name,
                "sha256": sha256_bytes(packaged_bytes),
                "size": len(packaged_bytes),
                "role": role,
                "selected_for_resource_host": selected,
                "packaged_bytes_match_checkout": True,
                "loose_copy_required": loose_required,
                "loose_copy_present": loose_present,
                "loose_copy_matches_archive": True if loose_present else None,
            }
        )
    packaged_by_path = {
        str(entry["source_path"]): entry for entry in entries
    }
    mirror_pairs = (
        (
            "resources/materials/nicemetal_gl3plus.glsl",
            "resources/managed_materials/nicemetal_mm_gl3plus.glsl",
        ),
        (
            "resources/materials/nicemetal_d3d11.hlsl",
            "resources/managed_materials/nicemetal_mm_d3d11.hlsl",
        ),
    )
    for ordinary, managed in mirror_pairs:
        if packaged_by_path[ordinary]["sha256"] != packaged_by_path[managed]["sha256"]:
            raise NiceMetalSmokeFailure(
                f"the packaged NiceMetal source mirrors differ: {ordinary}, {managed}"
            )
    return {
        "schema": "ror.nicemetal_shader_source_manifest.v1",
        "scope": "packaged-program-and-backend-source-closure",
        "archives": archive_receipts,
        "files": entries,
        "file_count": len(entries),
        "removed_cg_absent": True,
        "managed_loose_source_closure_complete": True,
        "backend_source_mirrors_byte_identical": True,
    }


def validate_packaged_runtime(
    executable: Path, runtime_content: Path, target_platform: str
) -> dict[str, object]:
    if executable.is_symlink():
        raise NiceMetalSmokeFailure(
            f"the packaged runtime entrypoint is indirect: {executable}"
        )
    if runtime_content.is_symlink():
        raise NiceMetalSmokeFailure(
            f"the packaged runtime content is indirect: {runtime_content}"
        )
    executable = executable.resolve()
    runtime_content = runtime_content.resolve()
    if not executable.is_file() or executable.is_symlink():
        raise NiceMetalSmokeFailure(
            f"the packaged runtime entrypoint is unavailable: {executable}"
        )
    if target_platform != "win32" and not os.access(executable, os.X_OK):
        raise NiceMetalSmokeFailure(
            f"the packaged runtime entrypoint is not executable: {executable}"
        )
    expected_names = {
        "linux": "RunRoR",
        "darwin": "RoR-Combined",
        "win32": "RoR-Combined.exe",
    }
    if executable.name != expected_names[target_platform]:
        raise NiceMetalSmokeFailure(
            "the packaged entrypoint name changed: "
            f"observed={executable.name!r}, "
            f"expected={expected_names[target_platform]!r}"
        )
    expected_content = expected_runtime_content(executable, target_platform).resolve()
    if runtime_content != expected_content:
        raise NiceMetalSmokeFailure(
            "the runtime content is not colocated with the packaged entrypoint: "
            f"observed={runtime_content}, expected={expected_content}"
        )
    terrain_archive = runtime_content / "simple2-terrain.zip"
    if (
        not runtime_content.is_dir()
        or runtime_content.is_symlink()
        or not terrain_archive.is_file()
        or terrain_archive.is_symlink()
    ):
        raise NiceMetalSmokeFailure(
            f"the packaged Simple2 content is unavailable: {terrain_archive}"
        )

    runtime_binary = executable
    if target_platform == "linux":
        runtime_binary = executable.parent / "RoR-Combined"
        if (
            not runtime_binary.is_file()
            or runtime_binary.is_symlink()
            or not os.access(runtime_binary, os.X_OK)
        ):
            raise NiceMetalSmokeFailure(
                f"the packaged Linux runtime binary is unavailable: {runtime_binary}"
            )
    return {
        "entrypoint": executable.name,
        "entrypoint_sha256": sha256_file(executable),
        "runtime_binary": runtime_binary.name,
        "runtime_binary_sha256": sha256_file(runtime_binary),
        "terrain_archive": terrain_archive.name,
        "terrain_archive_sha256": sha256_file(terrain_archive),
    }


def validate_fixture(fixture_dir: Path) -> list[dict[str, object]]:
    if fixture_dir.is_symlink():
        raise NiceMetalSmokeFailure(
            f"the NiceMetal fixture directory is indirect: {fixture_dir}"
        )
    fixture_dir = fixture_dir.resolve()
    if not fixture_dir.is_dir() or fixture_dir.is_symlink():
        raise NiceMetalSmokeFailure(
            f"the NiceMetal fixture directory is unavailable: {fixture_dir}"
        )
    children = sorted(fixture_dir.iterdir(), key=lambda path: path.name)
    names = [path.name for path in children]
    expected = sorted(FIXTURE_SHA256)
    if names != expected:
        raise NiceMetalSmokeFailure(
            f"the NiceMetal fixture inventory changed: {names!r}"
        )

    inventory: list[dict[str, object]] = []
    for path in children:
        if not path.is_file() or path.is_symlink():
            raise NiceMetalSmokeFailure(
                f"the NiceMetal fixture member is not a regular file: {path}"
            )
        data = path.read_bytes()
        digest = sha256_bytes(data)
        if digest != FIXTURE_SHA256[path.name]:
            raise NiceMetalSmokeFailure(
                f"the NiceMetal fixture digest changed for {path.name}: {digest}"
            )
        inventory.append(
            {"path": path.name, "sha256": digest, "size": len(data)}
        )

    actor = (fixture_dir / ACTOR_FILE).read_text(encoding="utf-8")
    nonempty = [line.strip() for line in actor.splitlines() if line.strip()]
    if not nonempty or nonempty[0] != ACTOR_NAME:
        raise NiceMetalSmokeFailure("the NiceMetal fixture actor title changed")
    try:
        section = nonempty.index("managedmaterials")
        cameras = nonempty.index("cameras")
    except ValueError as error:
        raise NiceMetalSmokeFailure(
            "the NiceMetal fixture managed-material section is missing"
        ) from error
    if tuple(nonempty[section + 1 : cameras]) != EXPECTED_MANAGED_LINES:
        raise NiceMetalSmokeFailure(
            "the NiceMetal fixture managed-material declarations changed"
        )
    return inventory


def create_deterministic_fixture_archive(
    fixture_dir: Path, archive_path: Path
) -> dict[str, object]:
    inventory = validate_fixture(fixture_dir)
    if archive_path.name != ARCHIVE_NAME:
        raise NiceMetalSmokeFailure(
            "the deterministic fixture archive name changed: "
            f"observed={archive_path.name!r}, expected={ARCHIVE_NAME!r}"
        )
    if archive_path.exists() or archive_path.is_symlink():
        raise NiceMetalSmokeFailure(
            f"the deterministic fixture archive already exists: {archive_path}"
        )
    archive_path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(
        archive_path, mode="x", compression=zipfile.ZIP_STORED
    ) as archive:
        for member in inventory:
            name = str(member["path"])
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_STORED
            info.create_system = 3
            info.external_attr = (stat.S_IFREG | 0o644) << 16
            archive.writestr(info, (fixture_dir / name).read_bytes())

    with zipfile.ZipFile(archive_path, mode="r") as archive:
        infos = archive.infolist()
        if [info.filename for info in infos] != [
            str(item["path"]) for item in inventory
        ]:
            raise NiceMetalSmokeFailure(
                "the deterministic fixture archive member order changed"
            )
        for info, member in zip(infos, inventory):
            if (
                info.date_time != (1980, 1, 1, 0, 0, 0)
                or info.compress_type != zipfile.ZIP_STORED
                or info.create_system != 3
                or (info.external_attr >> 16) != (stat.S_IFREG | 0o644)
                or archive.read(info.filename)
                != (fixture_dir / str(member["path"])).read_bytes()
            ):
                raise NiceMetalSmokeFailure(
                    f"the deterministic ZIP metadata changed for {info.filename}"
                )
    archive_digest = sha256_file(archive_path)
    if archive_digest != ARCHIVE_SHA256:
        raise NiceMetalSmokeFailure(
            "the deterministic fixture archive digest changed: "
            f"observed={archive_digest}, expected={ARCHIVE_SHA256}"
        )
    return {
        "archive_name": archive_path.name,
        "archive_sha256": archive_digest,
        "members": inventory,
    }


def build_ror_config() -> str:
    return "\n".join(
        (
            "; Generated by tools/run_nicemetal_shader_smoke.py",
            "app_config_long_names=false",
            "app_disable_online_api=true",
            "app_force_cache_update=true",
            "audio_master_volume=0",
            "gfx_alt_actor_materials=false",
            "gfx_fps_limit=0",
            "",
        )
    )


def build_ogre_config(target_platform: str) -> str:
    system = PLATFORM_POLICY[target_platform]["resource_host_render_system"]
    if system == "OpenGL 3+ Rendering Subsystem":
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
            f"Video Mode={WIDTH} x {HEIGHT}",
            "sRGB Gamma Conversion=No",
        )
    else:
        body = (
            "Allow NVPerfHUD=No",
            "Debug Layer=Off",
            "Driver type=Hardware",
            "FSAA=1",
            "Full Screen=No",
            "Information Queue Exceptions Bottom Level=No information queue exceptions",
            "Max Requested Feature Levels=11.0",
            "Min Requested Feature Levels=9.1",
            "Rendering Device=(default)",
            "Reversed Z-Buffer=No",
            "VSync=No",
            "VSync Interval=1",
            f"Video Mode={WIDTH} x {HEIGHT} @ 32-bit colour",
            "sRGB Gamma Conversion=No",
        )
    return "\n".join((f"Render System={system}", "", f"[{system}]", *body, ""))


def build_command(executable: Path, target_platform: str) -> tuple[str, ...]:
    command = [str(executable)]
    if target_platform == "darwin":
        command.extend(("-ApplePersistenceIgnoreState", "YES"))
    command.extend(
        (
            "-checkcache",
            "-map",
            TERRAIN,
            "-truck",
            ACTOR_FILE,
            "-enter",
        )
    )
    return tuple(command)


def build_environment(isolated_home: Path) -> dict[str, str]:
    environment = os.environ.copy()
    environment.pop("SNAP_USER_COMMON", None)
    environment["ROR_D0_SCENE_HOME"] = str(isolated_home)
    environment["ROR_D0_EXACT_WINDOW_EXTENT"] = f"{WIDTH}x{HEIGHT}"
    environment["ALSOFT_DRIVERS"] = "null"
    environment.setdefault("ALSOFT_LOGLEVEL", "0")
    return environment


def read_text_limited(path: Path, *, required: bool) -> str:
    if not path.is_file():
        if required:
            raise NiceMetalSmokeFailure(f"runtime evidence is missing: {path}")
        return ""
    size = path.stat().st_size
    if size <= 0:
        if required:
            raise NiceMetalSmokeFailure(f"runtime evidence is empty: {path}")
        return ""
    if size > MAX_LOG_BYTES:
        raise NiceMetalSmokeFailure(
            f"runtime evidence exceeds {MAX_LOG_BYTES} bytes: {path}"
        )
    return path.read_text(encoding="utf-8", errors="replace")


def rejected_diagnostics(text: str) -> list[str]:
    found = [marker for marker in FATAL_MARKERS if marker in text]
    found.extend(
        name for name, pattern in REJECTED_DIAGNOSTICS.items() if pattern.search(text)
    )
    return sorted(found)


def validate_runtime_evidence(
    runtime_log: str,
    console: str,
    target_platform: str,
    *,
    require_complete: bool = True,
) -> dict[str, object] | None:
    try:
        policy = PLATFORM_POLICY[target_platform]
    except KeyError as error:
        raise NiceMetalSmokeFailure(
            f"unsupported runtime platform: {target_platform}"
        ) from error

    diagnostics = rejected_diagnostics(runtime_log + "\n" + console)
    if diagnostics:
        raise NiceMetalSmokeFailure(
            "the runtime emitted rejected shader/fallback diagnostics: "
            + ", ".join(diagnostics)
        )

    visible_marker = (
        f"{PRESENTATION_OWNER_PREFIX} "
        f"backend={policy['visible_render_system']}"
    )
    visible_count = runtime_log.count(visible_marker)
    owner_prefix_count = runtime_log.count(PRESENTATION_OWNER_PREFIX)
    resource_host_count = runtime_log.count(RESOURCE_HOST_RECEIPT)
    spawn_count = runtime_log.count(SPAWN_MARKER)
    if visible_count > 1 or owner_prefix_count > 1:
        raise NiceMetalSmokeFailure(
            "the visible Ogre-Next ownership receipt was duplicated"
        )
    if resource_host_count > 1:
        raise NiceMetalSmokeFailure("the hidden resource-host receipt was duplicated")
    if spawn_count > 1:
        raise NiceMetalSmokeFailure("the NiceMetal fixture actor spawned more than once")
    if owner_prefix_count == 1 and visible_count != 1:
        raise NiceMetalSmokeFailure(
            "the visible Ogre-Next backend does not match the host platform"
        )

    render_systems = [
        value.strip() for value in RENDER_SYSTEM_PATTERN.findall(runtime_log)
    ]
    expected_render_system = str(policy["resource_host_render_system"])
    if render_systems and render_systems != [expected_render_system]:
        raise NiceMetalSmokeFailure(
            "the hidden resource-host render system changed: "
            f"observed={render_systems!r}, expected={[expected_render_system]!r}"
        )

    placeholder_matches = list(PLACEHOLDER_PATTERN.finditer(runtime_log))
    observed_pairs = [
        (match.group("material"), match.group("group"))
        for match in placeholder_matches
    ]
    expected_pairs = [(name, RESOURCE_GROUP) for name in MATERIAL_TEMPLATES]
    unexpected_pairs = sorted(set(observed_pairs) - set(expected_pairs))
    if unexpected_pairs:
        raise NiceMetalSmokeFailure(
            f"the runtime emitted unexpected placeholder markers: {unexpected_pairs!r}"
        )
    placeholder_counts = {
        name: observed_pairs.count((name, RESOURCE_GROUP))
        for name in MATERIAL_TEMPLATES
    }
    duplicated = sorted(
        name for name, count in placeholder_counts.items() if count > 1
    )
    if duplicated:
        raise NiceMetalSmokeFailure(
            "the runtime duplicated NiceMetal placeholder markers: "
            + ", ".join(duplicated)
        )

    missing: list[str] = []
    if visible_count != 1:
        missing.append("visible Ogre-Next ownership")
    if resource_host_count != 1:
        missing.append("hidden OGRE14 resource host")
    if render_systems != [expected_render_system]:
        missing.append(expected_render_system)
    if spawn_count != 1:
        missing.append(SPAWN_MARKER)
    missing.extend(
        f"placeholder:{name}"
        for name, count in placeholder_counts.items()
        if count != 1
    )
    if missing:
        if not require_complete:
            return None
        raise NiceMetalSmokeFailure(
            "the runtime did not emit complete NiceMetal resource-host evidence: "
            + ", ".join(missing)
        )

    public_programs = sorted(
        {
            program
            for _, programs in MATERIAL_TEMPLATES.values()
            for program in programs
        }
    )
    suffix = str(policy["backend_child_suffix"])
    return {
        "spawn_marker": SPAWN_MARKER,
        "spawn_marker_count": spawn_count,
        "placeholder_marker_counts": placeholder_counts,
        "managed_templates": [
            {
                "fixture_material": material,
                "template": template,
                "public_programs": list(programs),
                "post_support_check_placeholder_marker": True,
            }
            for material, (template, programs) in MATERIAL_TEMPLATES.items()
        ],
        "managed_public_programs_derived_from_template_contract": public_programs,
        "backend_children_derived_from_static_contract_and_observed_backend": [
            f"{program}{suffix}" for program in public_programs
        ],
        "backend_child_names_directly_logged": False,
        "rejected_diagnostic_count": 0,
    }


def wait_for_evidence(
    runtime_log_path: Path,
    console_path: Path,
    process: subprocess.Popen[bytes],
    target_platform: str,
    timeout_seconds: float,
    poll_seconds: float = 0.1,
) -> None:
    if not 0.0 < poll_seconds <= 1.0:
        raise NiceMetalSmokeFailure(
            "the runtime evidence poll interval must be within (0, 1] seconds"
        )
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        runtime_log = read_text_limited(runtime_log_path, required=False)
        console = read_text_limited(console_path, required=False)
        if (
            validate_runtime_evidence(
                runtime_log,
                console,
                target_platform,
                require_complete=False,
            )
            is not None
        ):
            return
        returncode = process.poll()
        if returncode is not None:
            raise NiceMetalSmokeFailure(
                "the runtime exited before complete NiceMetal resource-host "
                f"evidence: returncode={returncode}"
            )
        time.sleep(poll_seconds)
    raise NiceMetalSmokeFailure(
        "the runtime did not emit complete NiceMetal resource-host evidence "
        f"within {timeout_seconds:.0f}s"
    )


def terminate_child(
    process: subprocess.Popen[bytes], timeout_seconds: float = 10.0
) -> dict[str, object]:
    if process.poll() is not None:
        raise NiceMetalSmokeFailure(
            "the runtime exited before the driver could terminate its child"
        )
    process.terminate()
    method = "terminate"
    try:
        returncode = process.wait(timeout=timeout_seconds)
    except subprocess.TimeoutExpired:
        process.kill()
        method = "terminate-then-kill"
        returncode = process.wait(timeout=timeout_seconds)
    return {
        "requested_by_driver": True,
        "scope": "direct-child-only",
        "method": method,
        "returncode": returncode,
    }


def write_json_atomic(path: Path, document: Mapping[str, object]) -> None:
    if path.exists() or path.is_symlink():
        raise NiceMetalSmokeFailure(f"the receipt already exists: {path}")
    temporary = path.with_name(path.name + ".tmp")
    if temporary.exists() or temporary.is_symlink():
        raise NiceMetalSmokeFailure(
            f"the receipt temporary path already exists: {temporary}"
        )
    data = json.dumps(document, indent=2, sort_keys=True) + "\n"
    with temporary.open("x", encoding="utf-8", newline="\n") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def preserve_runtime_log(runtime_log_path: Path, destination: Path) -> None:
    if not runtime_log_path.is_file() or destination.exists():
        return
    shutil.copyfile(runtime_log_path, destination)


def execute(args: argparse.Namespace) -> dict[str, object]:
    target_platform = host_platform()
    if not re.fullmatch(r"[0-9a-f]{40}", args.repository_commit):
        raise NiceMetalSmokeFailure(
            "--repository-commit must be an exact lowercase 40-hex commit"
        )
    runtime_build_commit = (
        args.runtime_build_commit
        if args.runtime_build_commit is not None
        else args.repository_commit
    )
    if not re.fullmatch(r"[0-9a-f]{40}", runtime_build_commit):
        raise NiceMetalSmokeFailure(
            "--runtime-build-commit must be an exact lowercase 40-hex commit"
        )
    if not 1 <= args.timeout <= 600:
        raise NiceMetalSmokeFailure("--timeout must be between 1 and 600 seconds")

    if args.executable.is_symlink() or args.runtime_content.is_symlink():
        raise NiceMetalSmokeFailure(
            "the packaged entrypoint and content must be direct paths"
        )
    executable = args.executable.resolve()
    runtime_content = args.runtime_content.resolve()
    packaged_runtime = validate_packaged_runtime(
        executable, runtime_content, target_platform
    )
    source_manifest = validate_packaged_shader_sources(
        executable, target_platform
    )
    fixture_inventory = validate_fixture(args.fixture_dir)

    artifact_dir = args.artifact_dir.resolve()
    if artifact_dir.exists() or artifact_dir.is_symlink():
        raise NiceMetalSmokeFailure(
            f"the artifact directory already exists: {artifact_dir}"
        )
    artifact_dir.mkdir(parents=True)
    isolated_home = artifact_dir / "home"
    layout = runtime_layout(isolated_home, target_platform, executable)
    for directory in layout.values():
        directory.mkdir(parents=True, exist_ok=True)

    archive_path = artifact_dir / ARCHIVE_NAME
    fixture = create_deterministic_fixture_archive(
        args.fixture_dir.resolve(), archive_path
    )
    if fixture["members"] != fixture_inventory:
        raise NiceMetalSmokeFailure(
            "the fixture changed between validation and deterministic packaging"
        )
    staged_archive = layout["mods"] / ARCHIVE_NAME
    shutil.copyfile(archive_path, staged_archive)
    if sha256_file(staged_archive) != fixture["archive_sha256"]:
        raise NiceMetalSmokeFailure("the staged fixture archive digest changed")

    ror_config = build_ror_config()
    ogre_config = build_ogre_config(target_platform)
    (layout["config"] / "RoR.cfg").write_text(ror_config, encoding="utf-8")
    (layout["config"] / "ogre.cfg").write_text(ogre_config, encoding="utf-8")

    runtime_log_path = layout["logs"] / "RoR.log"
    console_path = artifact_dir / "console.txt"
    process: subprocess.Popen[bytes] | None = None
    termination: dict[str, object] | None = None
    try:
        with console_path.open("xb") as console_stream:
            process = subprocess.Popen(
                list(build_command(executable, target_platform)),
                cwd=executable.parent,
                env=build_environment(isolated_home),
                stdout=console_stream,
                stderr=subprocess.STDOUT,
            )
            wait_for_evidence(
                runtime_log_path,
                console_path,
                process,
                target_platform,
                float(args.timeout),
            )
            if process.poll() is not None:
                raise NiceMetalSmokeFailure(
                    "the runtime exited after readiness and before child termination"
                )
            termination = terminate_child(process)
    except BaseException:
        if process is not None and process.poll() is None:
            try:
                terminate_child(process)
            except BaseException:
                process.kill()
                process.wait()
        preserve_runtime_log(runtime_log_path, artifact_dir / "RoR.log")
        raise

    preserve_runtime_log(runtime_log_path, artifact_dir / "RoR.log")
    runtime_log = read_text_limited(artifact_dir / "RoR.log", required=True)
    if not console_path.is_file():
        raise NiceMetalSmokeFailure("the runtime console capture is missing")
    console = read_text_limited(console_path, required=False)
    evidence = validate_runtime_evidence(
        runtime_log, console, target_platform, require_complete=True
    )
    if evidence is None or termination is None:
        raise NiceMetalSmokeFailure("internal smoke evidence was not retained")

    policy = PLATFORM_POLICY[target_platform]
    receipt: dict[str, object] = {
        "schema": RECEIPT_SCHEMA,
        "passed": True,
        "evidence_class": EVIDENCE_CLASS,
        "qualification_scope": QUALIFICATION_SCOPE,
        "resource_host_shader_compile_load_proven": True,
        "visible_nicemetal_draw_proven": False,
        "repository_commit": args.repository_commit,
        "source_manifest": source_manifest,
        "fixture": {
            **fixture,
            "actor_file": ACTOR_FILE,
            "actor_name": ACTOR_NAME,
            "resource_group": RESOURCE_GROUP,
        },
        "runtime": {
            "platform": target_platform,
            "runtime_build_commit": runtime_build_commit,
            **packaged_runtime,
            "terrain": TERRAIN,
            "resource_host": "ogre14",
            "resource_host_render_system": policy[
                "resource_host_render_system"
            ],
            "resource_host_visible": False,
            "resource_host_protected": True,
            "presentation_owner": "ogre-next",
            "visible_render_system": policy["visible_render_system"],
            "visible_window": True,
            "legacy_visible_fallback": False,
            "working_directory_policy": "packaged-entrypoint-directory",
            "ror_config_sha256": sha256_bytes(ror_config.encode("utf-8")),
            "ogre_config_sha256": sha256_bytes(ogre_config.encode("utf-8")),
        },
        "evidence": evidence,
        "termination": termination,
        "limitations": [
            "resource-host shader evidence only",
            "visible NiceMetal draw is not proven",
            "playability is not proven",
        ],
    }
    write_json_atomic(artifact_dir / "receipt.json", receipt)
    return receipt


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--runtime-content", required=True, type=Path)
    parser.add_argument("--artifact-dir", required=True, type=Path)
    parser.add_argument("--repository-commit", required=True)
    parser.add_argument(
        "--runtime-build-commit",
        help=(
            "exact commit embedded in the runtime binary; defaults to "
            "--repository-commit for immutable CI builds"
        ),
    )
    parser.add_argument(
        "--fixture-dir", type=Path, default=DEFAULT_FIXTURE_DIR
    )
    parser.add_argument("--timeout", type=int, default=240)
    return parser.parse_args(list(argv))


def main(argv: Sequence[str] | None = None) -> int:
    try:
        receipt = execute(parse_args(sys.argv[1:] if argv is None else argv))
    except NiceMetalSmokeFailure as error:
        print(f"NiceMetal resource-host shader smoke failed: {error}", file=sys.stderr)
        return 1
    print(json.dumps(receipt, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
