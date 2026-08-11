#!/usr/bin/env python3
"""Fail closed if RoR-Combined links the retired renderer bridge closure."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys


SCHEMA = "ror.ogre_next_combined_binary_closure.v1"

FORBIDDEN_SYMBOL_TOKENS = (
    "InputEventTransport",
    "RenderAssetDeltaTransport",
    "RenderBridge",
    "RendererBridge",
    "RendererChild",
    "RendererOgre14GameBridge",
    "RendererOgre14GameHostSession",
    "RendererOgre14InputAdapter",
    "RendererOgre14ProductSession",
    "RendererOgreNextChild",
    "RendererOgreNextLiveSession",
    "RendererOgreNextProductionSession",
    "RendererPublicLauncher",
    "RendererPackageRuntimeProbe",
    "RendererPackagedMediaPath",
    "RendererSiblingPath",
    "RendererStartupHandoff",
    "RendererStartupPlan",
    "SceneGenerationBoundaryTransport",
    "SceneSnapshotTransport",
    "RenderTransport",
    "RendererFrontendTransportDispatcher",
)

FORBIDDEN_LINK_MAP_OBJECT_TOKENS = tuple(
    f"{name}.cpp.o"
    for name in (
        "InputEventTransport",
        "RenderAssetDeltaTransport",
        "RenderBridgeControlTransport",
        "RenderBridgeSessionIdentity",
        "RendererBackendPolicy",
        "RendererBridgeChannel",
        "RendererBridgeEndpoint",
        "RendererBridgeLaunchPlan",
        "RendererBridgeProcessSupervisor",
        "RendererChildIntent",
        "RendererChildLauncher",
        "RendererLauncherMain",
        "RendererFrontendTransportDispatcher",
        "RendererOgre14GameBridge",
        "RendererOgre14GameHostSession",
        "RendererOgre14InputAdapter",
        "RendererOgre14ProductSession",
        "RendererOgreNextLiveSession",
        "RendererOgreNextChild",
        "RendererOgreNextChildMain",
        "RendererOgreNextProductionSession",
        "RendererPackageRuntimeProbe",
        "RendererPackagedMediaPath",
        "RendererSiblingPath",
        "RendererStartupHandoff",
        "RendererStartupPlan",
        "RendererPublicLauncher",
        "RenderTransportEnvelope",
        "RenderTransportStream",
        "SceneGenerationBoundaryTransport",
        "SceneSnapshotTransport",
    )
)

REQUIRED_SYMBOL_TOKENS = (
    "RoR::RendererOgreNextInProcessPresenter::",
    "RoR::RendererInProcessSession::",
    "RoR::Render::RendererFrontendDirectDispatcher::",
    "RoR::RendererOgreNextSdlWindowRuntime::",
    "RoR::Render::OgreNextN1Frontend::",
    "RoR::Render::OgreNextDisplayDomainUnlit::",
    "RoR::Render::DecodeOgre14SourceTexture(",
    "RoR::Gfx::Detail::OgreNextDemoMaterialSource::",
)

FORBIDDEN_ROOT_IMAGE_CODEC_ARCHIVE_TOKENS = (
    "libpng.a(",
    "libpng.a[",
    "libpng16.a(",
    "libpng16.a[",
    "libjpeg.a(",
    "libjpeg.a[",
    "libjpeg-static.a(",
    "libjpeg-static.a[",
)

FORBIDDEN_EXTERNAL_IMAGE_CODEC_SYMBOL_PREFIXES = (
    "_png_",
    "_jpeg_",
    "_stbi_",
)

# These are the exact pre-existing zlib/libc++ definitions/imports shared by
# the reviewed RoR-Combined closure and OGRE14 Codec_FreeImage. PNG/JPEG/stb
# symbols, including libjpeg's non-jpeg_ helper globals, are intentionally not
# admitted. The complete Codec_FreeImage symbol set is intersected below, so a
# renamed archive/object cannot evade this boundary by changing its filename.
REVIEWED_CODEC_FREEIMAGE_DEFINED_INTERSECTION_ALLOWLIST = (
    "__ZNSt3__119piecewise_constructE",
    "_compress",
    "_compress2",
    "_compress2_z",
    "_compressBound",
    "_compressBound_z",
    "_compress_z",
    "_uncompress",
    "_uncompress2",
    "_uncompress2_z",
    "_uncompress_z",
)
REVIEWED_CODEC_FREEIMAGE_UNDEFINED_INTERSECTION_ALLOWLIST = (
    "_deflate",
    "_deflateEnd",
    "_deflateInit_",
    "_inflate",
    "_inflateEnd",
    "_inflateInit2_",
    "_inflateInit_",
    "_zError",
    "_zlibVersion",
)

COMBINED_STB_DECODER_OBJECT = (
    "source/main/CMakeFiles/RoR-Combined.dir/"
    "gfx/render/Ogre14SourceTextureDecoder.cpp.o"
)

EXPECTED_OGRE14_DIRECT_LOAD_PREFIXES = (
    "libOgreBites.",
    "libOgreMain.",
    "libOgreMeshLodGenerator.",
    "libOgreOverlay.",
    "libOgrePaging.",
    "libOgreProperty.",
    "libOgreRTShaderSystem.",
    "libOgreTerrain.",
    "libOgreVolume.",
)

REQUIRED_SDL_PROVIDER_SYMBOL_TOKENS = (
    "SDL_Init",
    "SDL_PollEvent",
    "SDL_CreateWindow",
)

REVIEWED_GLOBAL_INTERSECTION_ALLOWLIST = (
    "__ZNSt3__119piecewise_constructE",
)

STB_IMAGE_SOURCE_SCHEMA = "ror.ogre14_source_image_codec.v1"
STB_IMAGE_UPSTREAM_COMMIT = "2c980bb59875b0d32144a71867fbdebb2f77cd20"
STB_IMAGE_SOURCE_FILES = {
    "source_lock": {
        "relative_path": (
            "source/main/gfx/render/third_party/stb/"
            "stb-image-source.lock.json"
        ),
        "sha256": (
            "9902dd2891f8d8733d24cc06316ec98e23eac5b108ccca6c1a519cc94ddf61b6"
        ),
    },
    "header": {
        "relative_path": "source/main/gfx/render/third_party/stb/stb_image.h",
        "size": 283010,
        "sha256": (
            "594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3"
        ),
    },
    "license": {
        "relative_path": "source/main/gfx/render/third_party/stb/LICENSE.txt",
        "size": 2362,
        "sha256": (
            "771d43eb5017cb859978ad3ddb027fb80ea6119681f286950053404d95b21707"
        ),
    },
}


def _regular_absolute(path: str, description: str) -> Path:
    candidate = Path(path)
    if not candidate.is_absolute():
        raise ValueError(f"{description} must be absolute: {candidate}")
    if candidate.is_symlink() or not candidate.is_file():
        raise ValueError(f"{description} is missing, indirect, or not a file: {candidate}")
    return candidate.resolve(strict=True)


def _directory_absolute(path: str, description: str) -> Path:
    candidate = Path(path)
    if not candidate.is_absolute():
        raise ValueError(f"{description} must be absolute: {candidate}")
    if candidate.is_symlink() or not candidate.is_dir():
        raise ValueError(
            f"{description} is missing, indirect, or not a directory: {candidate}"
        )
    return candidate.resolve(strict=True)


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _link_map_contains(payload: bytes, token: str) -> bool:
    """Search Apple link-map bytes without interpreting embedded literals as text."""
    return token.encode("utf-8", errors="strict") in payload


def _external_image_codec_symbol_violations(payload: str) -> list[str]:
    """Return exact externally-defined C codec symbols from an nm listing."""
    violations: list[str] = []
    for line in payload.splitlines():
        fields = line.split()
        if not fields:
            continue
        symbol = fields[-1]
        if any(
            symbol.startswith(prefix)
            for prefix in FORBIDDEN_EXTERNAL_IMAGE_CODEC_SYMBOL_PREFIXES
        ):
            violations.append(symbol)
    return sorted(set(violations))


def _nm_symbol_names(payload: str) -> set[str]:
    """Parse exact nm rows without substring or last-word ambiguity."""
    symbols: set[str] = set()
    for line in payload.splitlines():
        if not line.strip() or line.endswith(":"):
            continue
        fields = line.split(maxsplit=2)
        if (
            len(fields) == 3
            and re.fullmatch(r"[0-9A-Fa-f]+", fields[0])
            and re.fullmatch(r"[A-Za-z?]", fields[1])
        ):
            symbols.add(fields[2])
        elif len(fields) == 2 and re.fullmatch(r"[A-Za-z?]", fields[0]):
            symbols.add(fields[1])
    return symbols


def _nm_undefined_symbol_names(payload: str) -> set[str]:
    """Parse Apple/GNU undefined-only nm output without dropping one-field rows."""
    symbols: set[str] = set()
    for line in payload.splitlines():
        stripped = line.strip()
        if not stripped or stripped.endswith(":"):
            continue
        fields = stripped.split()
        if len(fields) == 1:
            symbols.add(fields[0])
        elif len(fields) == 2 and re.fullmatch(r"[UuWw?]", fields[0]):
            symbols.add(fields[1])
        elif (
            len(fields) == 3
            and re.fullmatch(r"[0-9A-Fa-f]+", fields[0])
            and re.fullmatch(r"[UuWw?]", fields[1])
        ):
            symbols.add(fields[2])
        else:
            raise ValueError(f"undefined nm emitted an unrecognized row: {line}")
    return symbols


def _sdl_definition_symbols(symbols: set[str]) -> list[str]:
    return sorted(symbol for symbol in symbols if symbol.startswith("_SDL_"))


def _missing_required_demangled_symbols(payload: str) -> list[str]:
    symbols = _nm_symbol_names(payload)
    return sorted(
        required
        for required in REQUIRED_SYMBOL_TOKENS
        if not any(symbol.startswith(required) for symbol in symbols)
    )


def _unexpected_symbol_intersection(
    left: set[str], right: set[str], allowlist: tuple[str, ...]
) -> tuple[list[str], list[str]]:
    intersection = sorted(left & right)
    unexpected = sorted(set(intersection) - set(allowlist))
    return intersection, unexpected


def _parse_apple_link_map(
    payload: bytes, binary: Path, build_root: Path
) -> tuple[dict[int, str], list[tuple[int, bytes, bool]]]:
    """Parse only Apple's path/object/symbol records; ignore raw literals."""
    lines = payload.splitlines()
    map_binary_text = None
    object_rows: dict[int, str] = {}
    symbol_rows: list[tuple[int, bytes, bool]] = []
    section = "header"
    path_seen = False
    object_header_seen = False
    sections_header_seen = False
    symbols_header_seen = False
    dead_header_seen = False
    for raw_line in lines:
        if raw_line.startswith(b"# Path: "):
            if path_seen or section != "header":
                raise ValueError("Apple link map has a duplicate or misplaced Path")
            map_binary_text = raw_line[len(b"# Path: ") :].decode(
                "utf-8", errors="strict"
            )
            path_seen = True
        elif raw_line == b"# Object files:":
            if not path_seen or object_header_seen or section != "header":
                raise ValueError(
                    "Apple link map has duplicate or out-of-order Object files"
                )
            object_header_seen = True
            section = "objects"
            continue
        elif raw_line == b"# Sections:":
            if not object_header_seen or sections_header_seen or section != "objects":
                raise ValueError(
                    "Apple link map has duplicate or out-of-order Sections"
                )
            sections_header_seen = True
            section = "sections"
            continue
        elif raw_line == b"# Symbols:":
            if not sections_header_seen or symbols_header_seen or section != "sections":
                raise ValueError(
                    "Apple link map has duplicate or out-of-order Symbols"
                )
            symbols_header_seen = True
            section = "symbols"
            continue
        elif raw_line == b"# Dead Stripped Symbols:":
            if not symbols_header_seen or dead_header_seen or section != "symbols":
                raise ValueError(
                    "Apple link map has duplicate or out-of-order dead symbols"
                )
            dead_header_seen = True
            section = "dead_symbols"
            continue

        if section == "objects":
            match = re.fullmatch(rb"\[\s*([0-9]+)\]\s+(.+)", raw_line)
            if match:
                index = int(match.group(1))
                if index in object_rows:
                    raise ValueError("Apple link map contains duplicate object index")
                object_rows[index] = match.group(2).decode(
                    "utf-8", errors="strict"
                )
        elif section in ("symbols", "dead_symbols"):
            match = re.fullmatch(
                rb"(?:0x[0-9A-Fa-f]+|<<dead>>)\s+0x[0-9A-Fa-f]+\s+"
                rb"\[\s*([0-9]+)\]\s+(.+)",
                raw_line,
            )
            if match:
                symbol_rows.append(
                    (
                        int(match.group(1)),
                        match.group(2),
                        section == "dead_symbols",
                    )
                )

    if (
        not map_binary_text
        or not object_rows
        or not symbol_rows
        or not symbols_header_seen
    ):
        raise ValueError("Apple link map lacks path, object, or symbol records")
    map_binary = PurePosixPath(map_binary_text)
    if map_binary.is_absolute():
        if Path(map_binary_text).resolve(strict=True) != binary:
            raise ValueError("Apple link map names a different absolute binary")
    else:
        if any(part in ("", ".", "..") for part in map_binary.parts):
            raise ValueError("Apple link map binary path is unsafe")
        if build_root.joinpath(*map_binary.parts).resolve(strict=True) != binary:
            raise ValueError("Apple link map names a different binary")
    return object_rows, symbol_rows


def _link_map_archive_path(build_root: Path, object_row: str):
    match = re.fullmatch(r"(.+[.]a)(?:[(].+[)]|\[.+\])", object_row)
    if not match:
        return None
    candidate = Path(match.group(1))
    if not candidate.is_absolute():
        candidate = build_root / candidate
    return candidate.resolve(strict=False)


def _link_map_object_basename(object_row: str) -> str:
    archive_member = re.fullmatch(
        r".+[.]a(?:[(](.+)[)]|\[(.+)\])", object_row
    )
    if archive_member:
        member = archive_member.group(1) or archive_member.group(2)
        return Path(member).name
    return Path(object_row).name


def _is_private_stbi_link_map_symbol(symbol: bytes) -> bool:
    return re.fullmatch(
        rb"(?:_stbi_.+|__Z(?:Z)?L.+stbi_.+)",
        symbol,
    ) is not None


def _structural_link_map_evidence(
    payload: bytes,
    binary: Path,
    build_root: Path,
    required_archives: list[Path],
    raw_defined_symbols: set[str],
) -> dict[str, object]:
    binary = binary.resolve(strict=True)
    build_root = build_root.resolve(strict=True)
    required_archives = [path.resolve(strict=True) for path in required_archives]
    object_rows, symbol_rows = _parse_apple_link_map(
        payload, binary, build_root
    )
    archive_members: dict[Path, int] = {}
    object_violations: set[str] = set()
    root_image_codec_archives: set[str] = set()
    extracted_sdl = False
    for object_row in object_rows.values():
        archive = _link_map_archive_path(build_root, object_row)
        if archive is not None:
            archive_members[archive] = archive_members.get(archive, 0) + 1
            basename = archive.name
            if basename == "libSDL2.a":
                extracted_sdl = True
            if basename in {
                "libpng.a",
                "libpng16.a",
                "libjpeg.a",
                "libjpeg-static.a",
            }:
                root_image_codec_archives.add(str(archive))
        object_basename = _link_map_object_basename(object_row)
        if object_basename in FORBIDDEN_LINK_MAP_OBJECT_TOKENS:
            object_violations.add(object_basename)

    missing_required = sorted(
        str(path) for path in required_archives if archive_members.get(path, 0) < 1
    )
    stbi_symbol_rows = [
        (index, symbol, dead) for index, symbol, dead in symbol_rows
        if _is_private_stbi_link_map_symbol(symbol)
    ]
    live_stbi_symbol_names = {
        symbol.decode("ascii", errors="strict")
        for _index, symbol, dead in stbi_symbol_rows
        if not dead
    }
    unverified_live_stbi_symbols = sorted(
        live_stbi_symbol_names - raw_defined_symbols
    )
    if unverified_live_stbi_symbols:
        raise ValueError(
            "Apple link map stb_image symbols are absent from defined nm: "
            + ", ".join(unverified_live_stbi_symbols)
        )
    verified_live_stbi_symbols = sorted(
        live_stbi_symbol_names & raw_defined_symbols
    )
    if not verified_live_stbi_symbols:
        raise ValueError(
            "Apple link map lacks an nm-confirmed live private stb_image symbol"
        )
    root_sdl_symbol_rows: list[str] = []
    for index, symbol, _dead in symbol_rows:
        if not re.fullmatch(rb"_SDL_.+", symbol) or symbol.endswith(b".stub"):
            continue
        object_row = object_rows.get(index, "")
        if object_row == "linker synthesized" or object_row.endswith(
            (".dylib", ".tbd")
        ):
            continue
        root_sdl_symbol_rows.append(symbol.decode("ascii", errors="strict"))
    root_sdl_symbol_rows.sort()
    stbi_owner_indices = {
        index for index, _symbol, _dead in stbi_symbol_rows
    }
    if not stbi_owner_indices:
        raise ValueError("Apple link map lacks positive private stb_image symbols")
    stbi_owner_objects: list[str] = []
    for index in sorted(stbi_owner_indices):
        object_row = object_rows.get(index)
        if object_row is None or _link_map_archive_path(build_root, object_row):
            stbi_owner_objects.append(f"<invalid-object-{index}>")
            continue
        candidate = Path(object_row)
        if not candidate.is_absolute():
            candidate = build_root / candidate
        stbi_owner_objects.append(str(candidate.resolve(strict=False)))
    expected_stbi_owner = str(
        (build_root / COMBINED_STB_DECODER_OBJECT).resolve(strict=False)
    )
    if stbi_owner_objects != [expected_stbi_owner]:
        raise ValueError(
            "private stb_image symbols have an unreviewed object owner: "
            + ", ".join(stbi_owner_objects)
        )

    return {
        "build_root": str(build_root),
        "required_archive_member_counts": {
            str(path): archive_members.get(path, 0) for path in required_archives
        },
        "missing_required_archives": missing_required,
        "forbidden_objects": sorted(object_violations),
        "root_sdl_archive_members_extracted": extracted_sdl,
        "root_image_codec_archives": sorted(root_image_codec_archives),
        "stb_image_symbol_count": len(stbi_symbol_rows),
        "stb_image_nm_confirmed_live_symbols": verified_live_stbi_symbols,
        "stb_image_owner_objects": stbi_owner_objects,
        "expected_stb_image_owner_object": expected_stbi_owner,
        "root_sdl_defined_symbols": root_sdl_symbol_rows,
    }


def _otool_load_commands(payload: str) -> list[str]:
    commands: list[str] = []
    for line in payload.splitlines()[1:]:
        if not line.startswith(("\t", " ")):
            continue
        fields = line.split()
        if fields:
            commands.append(fields[0])
    return commands


def _otool_rpaths(payload: str) -> list[str]:
    rpaths: list[str] = []
    lines = payload.splitlines()
    for index, line in enumerate(lines):
        if line.strip() != "cmd LC_RPATH":
            continue
        if index + 2 >= len(lines):
            raise ValueError("otool LC_RPATH record is truncated")
        match = re.match(r"\s*path\s+(.+?)\s+\(offset\s+[0-9]+\)\s*$", lines[index + 2])
        if not match:
            raise ValueError("otool LC_RPATH path record is invalid")
        rpaths.append(match.group(1))
    return rpaths


def _expand_rpath(path: str, binary: Path) -> Path:
    if path.startswith("@loader_path/"):
        return (binary.parent / path[len("@loader_path/") :]).resolve(
            strict=False
        )
    if path.startswith("@executable_path/"):
        return (binary.parent / path[len("@executable_path/") :]).resolve(
            strict=False
        )
    candidate = Path(path)
    if not candidate.is_absolute():
        raise ValueError(f"unsupported or relative LC_RPATH: {path}")
    return candidate.resolve(strict=False)


def _direct_dynamic_load_evidence(
    load_payload: str,
    command_payload: str,
    binary: Path,
    runtime_manifest: dict[str, object],
) -> dict[str, object]:
    load_commands = _otool_load_commands(load_payload)
    non_system = [
        command for command in load_commands
        if not command.startswith(("/System/Library/", "/usr/lib/"))
    ]
    basenames = [Path(command).name for command in non_system]
    runtime_entries = runtime_manifest.get("libraries")
    if not isinstance(runtime_entries, list):
        raise ValueError("OGRE14 runtime manifest library records are invalid")
    runtime_by_basename = {
        Path(str(entry.get("path", ""))).name:
            Path(str(entry.get("path", ""))).resolve(strict=True)
        for entry in runtime_entries if isinstance(entry, dict)
    }
    if len(runtime_by_basename) != len(runtime_entries):
        raise ValueError("OGRE14 runtime manifest basenames are ambiguous")
    expected_direct_basenames: set[str] = set()
    for prefix in EXPECTED_OGRE14_DIRECT_LOAD_PREFIXES:
        matches = sorted(
            basename for basename in runtime_by_basename
            if basename.startswith(prefix)
        )
        if len(matches) != 1:
            raise ValueError(
                f"OGRE14 runtime manifest lacks one direct-load {prefix} library"
            )
        expected_direct_basenames.add(matches[0])

    rpaths = [_expand_rpath(path, binary) for path in _otool_rpaths(command_payload)]
    resolved_commands: list[dict[str, object]] = []
    resolution_failures: list[str] = []
    for command in non_system:
        basename = Path(command).name
        expected_path = runtime_by_basename.get(basename)
        candidates: list[Path] = []
        if command.startswith("@rpath/"):
            for rpath in rpaths:
                candidate = rpath / command[len("@rpath/") :]
                if candidate.is_file() and not candidate.is_symlink():
                    candidates.append(candidate.resolve(strict=True))
        elif command.startswith("@loader_path/"):
            candidate = binary.parent / command[len("@loader_path/") :]
            if candidate.is_file() and not candidate.is_symlink():
                candidates.append(candidate.resolve(strict=True))
        elif command.startswith("@executable_path/"):
            candidate = binary.parent / command[len("@executable_path/") :]
            if candidate.is_file() and not candidate.is_symlink():
                candidates.append(candidate.resolve(strict=True))
        elif Path(command).is_absolute():
            candidate = Path(command)
            if candidate.is_file() and not candidate.is_symlink():
                candidates.append(candidate.resolve(strict=True))
        else:
            resolution_failures.append(command)
        candidates = list(dict.fromkeys(candidates))
        if expected_path is None or candidates != [expected_path]:
            resolution_failures.append(command)
        resolved_commands.append(
            {
                "install_name": command,
                "basename": basename,
                "candidates": [str(path) for path in candidates],
                "authenticated_path": (
                    str(expected_path) if expected_path is not None else None
                ),
            }
        )

    return {
        "all": load_commands,
        "non_system": non_system,
        "unexpected_non_system": sorted(
            command for command in non_system
            if Path(command).name not in expected_direct_basenames
        ),
        "duplicate_non_system_basenames": sorted(
            basename for basename in set(basenames)
            if basenames.count(basename) != 1
        ),
        "missing_required_basenames": sorted(
            expected_direct_basenames - set(basenames)
        ),
        "rpaths": [str(path) for path in rpaths],
        "resolved_non_system": resolved_commands,
        "resolution_failures": sorted(set(resolution_failures)),
    }


def _verify_ogre14_runtime_manifest(
    provider_contract: dict[str, object],
    audited_records: list[dict[str, str]],
) -> dict[str, object]:
    package_root = _directory_absolute(
        str(provider_contract.get("ogre14_runtime_package_root", "")),
        "OGRE14 runtime package root",
    )
    expected_count = provider_contract.get("ogre14_runtime_library_count")
    expected_manifest_sha256 = provider_contract.get(
        "ogre14_runtime_manifest_sha256"
    )
    if (
        type(expected_count) is not int
        or expected_count < 3
        or len(audited_records) != expected_count
        or not isinstance(expected_manifest_sha256, str)
        or not re.fullmatch(r"[0-9a-f]{64}", expected_manifest_sha256)
    ):
        raise ValueError("provider OGRE14 runtime manifest authority is invalid")

    paths = [Path(record["path"]).resolve(strict=True) for record in audited_records]
    if len(paths) != len(set(paths)):
        raise ValueError("provider OGRE14 runtime manifest contains duplicates")
    serialized = ""
    entries: list[dict[str, object]] = []
    for path in sorted(paths):
        try:
            relative = path.relative_to(package_root)
        except ValueError as error:
            raise ValueError(
                "namespace-audited OGRE14 dylib escaped its configured package"
            ) from error
        relative_text = relative.as_posix()
        if (
            any(part in ("", ".", "..") for part in relative.parts)
            or not re.fullmatch(
                r"(?:lib|lib/OGRE)/(?:libOgre|Plugin_|Codec_|RenderSystem_).*[.]dylib",
                relative_text,
            )
        ):
            raise ValueError("provider OGRE14 runtime relative path is invalid")
        size = path.stat().st_size
        sha256 = _sha256(path)
        audited_sha256 = next(
            record["sha256"] for record in audited_records
            if Path(record["path"]).resolve(strict=True) == path
        )
        if sha256 != audited_sha256:
            raise ValueError("provider OGRE14 runtime changed after namespace audit")
        serialized += f"{relative_text}|{size}|{sha256}\n"
        entries.append(
            {
                "relative_path": relative_text,
                "path": str(path),
                "size": size,
                "sha256": sha256,
            }
        )
    observed_manifest_sha256 = hashlib.sha256(
        serialized.encode("utf-8")
    ).hexdigest()
    if observed_manifest_sha256 != expected_manifest_sha256:
        raise ValueError(
            "current OGRE14 runtime closure differs from its configured manifest"
        )

    main_runtime = _regular_absolute(
        str(provider_contract.get("ogre14_main_runtime", "")),
        "provider OGRE14 main runtime",
    )
    sdl_runtime = _regular_absolute(
        str(provider_contract.get("ogre14_sdl_provider_runtime", "")),
        "provider OGRE14 SDL runtime",
    )
    if main_runtime not in paths or sdl_runtime not in paths:
        raise ValueError("provider OGRE14 main/SDL runtimes escaped its manifest")
    codec_freeimage = [
        path for path in paths
        if re.fullmatch(r"Codec_FreeImage(?:[.][0-9]+)+[.]dylib", path.name)
    ]
    if len(codec_freeimage) != 1:
        raise ValueError("configured OGRE14 closure lacks one exact Codec_FreeImage")
    return {
        "package_root": str(package_root),
        "manifest_sha256": observed_manifest_sha256,
        "library_count": len(entries),
        "libraries": entries,
        "main_runtime": str(main_runtime),
        "sdl_provider_runtime": str(sdl_runtime),
        "codec_freeimage": str(codec_freeimage[0]),
    }


def _reject_duplicate_pairs(
    pairs: list[tuple[str, object]], description: str
) -> dict[str, object]:
    document: dict[str, object] = {}
    for key, value in pairs:
        if key in document:
            raise ValueError(f"{description} contains duplicate key: {key}")
        document[key] = value
    return document


def _json_object(path: Path, description: str) -> dict[str, object]:
    try:
        document = json.loads(
            path.read_text(encoding="utf-8", errors="strict"),
            object_pairs_hook=lambda pairs: _reject_duplicate_pairs(
                pairs, description
            ),
        )
    except json.JSONDecodeError as error:
        raise ValueError(f"{description} is invalid JSON: {error}") from error
    if not isinstance(document, dict):
        raise ValueError(f"{description} must be a JSON object")
    return document


def _verified_file_record(record: object, description: str) -> dict[str, str]:
    if not isinstance(record, dict):
        raise ValueError(f"{description} record is invalid")
    path = _regular_absolute(str(record.get("path", "")), description)
    expected_sha256 = record.get("sha256")
    if not isinstance(expected_sha256, str) or not re.fullmatch(
        r"[0-9a-f]{64}", expected_sha256
    ):
        raise ValueError(f"{description} digest is invalid")
    observed_sha256 = _sha256(path)
    if observed_sha256 != expected_sha256:
        raise ValueError(f"{description} bytes changed after the namespace audit")
    return {"path": str(path), "sha256": observed_sha256}


def _verify_source_manifest(
    manifest: Path,
    source_root: Path,
    expected_sha256: object,
    expected_count: object,
    description: str,
) -> dict[str, object]:
    source_root = source_root.resolve(strict=True)
    if not isinstance(expected_sha256, str) or not re.fullmatch(
        r"[0-9a-f]{64}", expected_sha256
    ):
        raise ValueError(f"{description} contract digest is invalid")
    if not isinstance(expected_count, int) or expected_count < 1:
        raise ValueError(f"{description} contract count is invalid")
    observed_manifest_sha256 = _sha256(manifest)
    if observed_manifest_sha256 != expected_sha256:
        raise ValueError(f"{description} bytes differ from the executable contract")
    lines = manifest.read_text(encoding="utf-8", errors="strict").splitlines()
    if len(lines) != expected_count:
        raise ValueError(f"{description} entry count changed")
    previous_path = ""
    for line in lines:
        fields = line.split("|")
        if len(fields) != 3:
            raise ValueError(f"{description} entry shape changed: {line}")
        relative_text, size_text, expected_file_sha256 = fields
        relative = PurePosixPath(relative_text)
        if (
            relative.is_absolute()
            or not relative.parts
            or any(part in ("", ".", "..") for part in relative.parts)
            or relative.as_posix() != relative_text
            or relative_text <= previous_path
            or not size_text.isdecimal()
            or not re.fullmatch(r"[0-9a-f]{64}", expected_file_sha256)
        ):
            raise ValueError(f"{description} contains unsafe or unsorted entry: {line}")
        previous_path = relative_text
        candidate = source_root.joinpath(*relative.parts)
        cursor = source_root
        for part in relative.parts:
            cursor = cursor / part
            if cursor.is_symlink():
                raise ValueError(f"{description} contains indirect path: {relative_text}")
        if not candidate.is_file():
            raise ValueError(f"{description} source is missing: {relative_text}")
        try:
            candidate.resolve(strict=True).relative_to(source_root)
        except ValueError as error:
            raise ValueError(
                f"{description} source escaped its root: {relative_text}"
            ) from error
        if candidate.stat().st_size != int(size_text):
            raise ValueError(f"{description} source size changed: {relative_text}")
        if _sha256(candidate) != expected_file_sha256:
            raise ValueError(f"{description} source digest changed: {relative_text}")
    return {
        "path": str(manifest),
        "sha256": observed_manifest_sha256,
        "file_count": len(lines),
        "source_root": str(source_root),
        "build_time_rehash": True,
    }


def _verify_strict_fp_receipts(
    provider_contract: dict[str, object],
    namespace_audit: dict[str, object],
) -> dict[str, int | bool]:
    if provider_contract.get("ogre_next_upstream_strict_fp") is not True:
        raise ValueError(
            "combined provider contract does not require OgreNext upstream strict FP"
        )
    target_count = provider_contract.get(
        "ogre_next_upstream_strict_fp_target_count"
    )
    if type(target_count) is not int or target_count < 1:
        raise ValueError(
            "combined provider contract strict-FP target count is not positive"
        )

    if namespace_audit.get("upstream_strict_fp_required") is not True:
        raise ValueError("namespace audit did not require upstream strict FP")
    upstream_compile_entries = namespace_audit.get("upstream_compile_entries")
    if type(upstream_compile_entries) is not int or upstream_compile_entries < 1:
        raise ValueError("namespace audit upstream compile-entry count is not positive")
    strict_fp_compile_entries = namespace_audit.get(
        "upstream_strict_fp_compile_entries"
    )
    if (
        type(strict_fp_compile_entries) is not int
        or strict_fp_compile_entries != upstream_compile_entries
    ):
        raise ValueError(
            "namespace audit strict-FP compile entries do not cover every upstream entry"
        )

    return {
        "provider_required": True,
        "provider_target_count": target_count,
        "upstream_compile_entries": upstream_compile_entries,
        "strict_fp_compile_entries": strict_fp_compile_entries,
    }


def _source_manifest_file_record(
    manifest: Path,
    relative_path: str,
    description: str,
) -> tuple[int, str]:
    matches: list[tuple[int, str]] = []
    for line in manifest.read_text(encoding="utf-8", errors="strict").splitlines():
        fields = line.split("|")
        if len(fields) == 3 and fields[0] == relative_path:
            size_text, sha256 = fields[1:]
            if not size_text.isdecimal() or not re.fullmatch(r"[0-9a-f]{64}", sha256):
                raise ValueError(f"{description} has an invalid source-manifest entry")
            matches.append((int(size_text), sha256))
    if len(matches) != 1:
        raise ValueError(
            f"{description} is not attested exactly once by the provider source manifest"
        )
    return matches[0]


def _verify_authenticated_source_image_decoder(
    provider_contract: dict[str, object],
    provider_manifest: Path,
    provider_source_root: Path,
) -> dict[str, object]:
    decoder = provider_contract.get("authenticated_source_image_decoder")
    if not isinstance(decoder, dict):
        raise ValueError(
            "combined provider contract lacks authenticated source-image decoder proof"
        )
    if (
        decoder.get("schema") != STB_IMAGE_SOURCE_SCHEMA
        or decoder.get("upstream_commit") != STB_IMAGE_UPSTREAM_COMMIT
        or decoder.get("macro_contract_verified") is not True
    ):
        raise ValueError(
            "combined provider authenticated source-image decoder identity changed"
        )

    verified_files: dict[str, dict[str, object]] = {}
    for name, expected in STB_IMAGE_SOURCE_FILES.items():
        record = decoder.get(name)
        if not isinstance(record, dict):
            raise ValueError(f"stb_image {name} proof is invalid")
        relative_path = expected["relative_path"]
        if record.get("relative_path") != relative_path:
            raise ValueError(f"stb_image {name} relative path changed")
        source_path = _regular_absolute(
            str(record.get("path", "")), f"stb_image {name}"
        )
        expected_source_path = provider_source_root.joinpath(
            *PurePosixPath(relative_path).parts
        ).resolve(strict=True)
        if source_path != expected_source_path:
            raise ValueError(f"stb_image {name} escaped the provider source root")

        expected_sha256 = expected["sha256"]
        if record.get("sha256") != expected_sha256:
            raise ValueError(f"stb_image {name} contract digest changed")
        observed_size = source_path.stat().st_size
        observed_sha256 = _sha256(source_path)
        if observed_sha256 != expected_sha256:
            raise ValueError(f"stb_image {name} source bytes changed")
        if "size" in expected and (
            type(record.get("size")) is not int
            or record.get("size") != expected["size"]
            or observed_size != expected["size"]
        ):
            raise ValueError(f"stb_image {name} byte count changed")

        manifest_size, manifest_sha256 = _source_manifest_file_record(
            provider_manifest,
            relative_path,
            f"stb_image {name}",
        )
        if manifest_size != observed_size or manifest_sha256 != observed_sha256:
            raise ValueError(
                f"stb_image {name} differs from the provider source manifest"
            )
        verified_files[name] = {
            "relative_path": relative_path,
            "path": str(source_path),
            "size": observed_size,
            "sha256": observed_sha256,
            "provider_source_manifest_attested": True,
        }

    return {
        "schema": STB_IMAGE_SOURCE_SCHEMA,
        "upstream_commit": STB_IMAGE_UPSTREAM_COMMIT,
        "macro_contract_verified": True,
        "files": verified_files,
    }


def _write_receipt(path: Path, document: dict[str, object]) -> None:
    if not path.is_absolute() or path.parent.is_symlink():
        raise ValueError(f"receipt path must be direct and absolute: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    if temporary.exists() or temporary.is_symlink():
        raise ValueError(f"temporary receipt path already exists: {temporary}")
    payload = json.dumps(document, indent=2, sort_keys=True) + "\n"
    try:
        with temporary.open("x", encoding="utf-8", newline="\n") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if temporary.exists() and not temporary.is_symlink():
            temporary.unlink()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--nm", required=True)
    parser.add_argument("--otool", required=True)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--build-root", required=True)
    parser.add_argument("--link-map", required=True)
    parser.add_argument("--executable-contract", required=True)
    parser.add_argument("--provider-contract", required=True)
    parser.add_argument("--namespace-audit-report", required=True)
    parser.add_argument("--required-archive", action="append", required=True)
    parser.add_argument("--required-ogre14-dylib", action="append", required=True)
    parser.add_argument("--sdl-provider-dylib", required=True)
    parser.add_argument("--receipt", required=True)
    arguments = parser.parse_args()

    try:
        receipt = Path(arguments.receipt)
        if not receipt.is_absolute() or receipt.is_dir():
            raise ValueError(f"receipt path must be an absolute file path: {receipt}")
        # A previous successful proof must never survive a later failed gate.
        if receipt.exists() or receipt.is_symlink():
            receipt.unlink()

        nm = _regular_absolute(arguments.nm, "nm executable")
        otool = _regular_absolute(arguments.otool, "otool executable")
        binary = _regular_absolute(arguments.binary, "combined binary")
        build_root = _directory_absolute(arguments.build_root, "combined build root")
        link_map = _regular_absolute(arguments.link_map, "combined link map")
        executable_contract = _regular_absolute(
            arguments.executable_contract, "combined executable contract"
        )
        provider_contract = _regular_absolute(
            arguments.provider_contract, "combined provider contract"
        )
        namespace_audit_report = _regular_absolute(
            arguments.namespace_audit_report, "combined namespace audit report"
        )
        contract_document = _json_object(
            executable_contract, "combined executable contract"
        )
        if contract_document.get("schema") != (
            "ror.ogre_next_combined_executable.v1"
        ):
            raise ValueError("combined executable contract schema changed")
        if (
            contract_document.get("provider_contract") != str(provider_contract)
            or contract_document.get("namespace_audit_report")
            != str(namespace_audit_report)
        ):
            raise ValueError(
                "combined executable contract names a different provider proof"
            )
        provider_manifest = _regular_absolute(
            str(contract_document.get("provider_source_manifest", "")),
            "provider source manifest",
        )
        provider_source_root = _directory_absolute(
            str(contract_document.get("provider_source_root", "")),
            "provider source root",
        )
        provider_manifest_report = _verify_source_manifest(
            provider_manifest,
            provider_source_root,
            contract_document.get("provider_source_manifest_sha256"),
            contract_document.get("provider_source_manifest_file_count"),
            "provider source manifest",
        )
        selected_manifest = _regular_absolute(
            str(contract_document.get("selected_game_source_manifest", "")),
            "selected game source manifest",
        )
        selected_source_root = _directory_absolute(
            str(contract_document.get("selected_game_source_root", "")),
            "selected game source root",
        )
        selected_manifest_report = _verify_source_manifest(
            selected_manifest,
            selected_source_root,
            contract_document.get("selected_game_source_manifest_sha256"),
            contract_document.get("remaining_game_source_count"),
            "selected game source manifest",
        )

        provider_contract_document = _json_object(
            provider_contract, "combined provider contract"
        )
        if (
            provider_contract_document.get("schema")
            != "ror.ogre_next_combined_provider.v1"
            or provider_contract_document.get("bridge_sources_linked") is not False
            or provider_contract_document.get("transport_sources_linked") is not False
            or provider_contract_document.get("sdl_target") != "SDL2::SDL2"
            or provider_contract_document.get("ror_source_root")
            != str(provider_source_root)
            or provider_contract_document.get("ror_source_manifest")
            != str(provider_manifest)
            or provider_contract_document.get("ror_source_manifest_file_count")
            != contract_document.get("provider_source_manifest_file_count")
            or provider_contract_document.get("ror_source_manifest_sha256")
            != contract_document.get("provider_source_manifest_sha256")
        ):
            raise ValueError("combined provider contract differs from the executable")
        authenticated_source_image_decoder = (
            _verify_authenticated_source_image_decoder(
                provider_contract_document,
                provider_manifest,
                provider_source_root,
            )
        )

        namespace_audit_document = _json_object(
            namespace_audit_report, "combined namespace audit report"
        )
        if (
            namespace_audit_document.get("schema")
            != "ror.ogre_next.embedded_namespace_audit.v2"
            or namespace_audit_document.get("status") != "passed"
            or namespace_audit_document.get("namespace") != "RoROgreNext"
            or namespace_audit_document.get("ror_source_commit")
            != provider_contract_document.get("ror_commit")
        ):
            raise ValueError("combined namespace audit is not an exact passing proof")
        strict_fp_report = _verify_strict_fp_receipts(
            provider_contract_document, namespace_audit_document
        )
        namespace_build_contract_record = namespace_audit_document.get(
            "build_contract"
        )
        verified_namespace_build_contract = _verified_file_record(
            namespace_build_contract_record,
            "combined namespace audit build contract",
        )
        namespace_build_contract_document = _json_object(
            Path(verified_namespace_build_contract["path"]),
            "combined namespace audit build contract",
        )
        namespace_ror_source = namespace_build_contract_document.get("ror_source")
        if (
            namespace_build_contract_document.get("schema_version") != 7
            or not isinstance(namespace_ror_source, dict)
            or namespace_ror_source.get("commit")
            != provider_contract_document.get("ror_commit")
            or namespace_ror_source.get("relevant_manifest_sha256")
            != contract_document.get("provider_source_manifest_sha256")
            or namespace_ror_source.get("relevant_manifest_file_count")
            != contract_document.get("provider_source_manifest_file_count")
        ):
            raise ValueError("namespace audit build contract differs from provider bytes")
        namespace_isolation = namespace_audit_document.get(
            "translation_unit_isolation"
        )
        isolated_consumers = (
            namespace_isolation.get("isolated_consumers")
            if isinstance(namespace_isolation, dict)
            else None
        )
        expected_isolated_consumer = {
            "path": str(provider_source_root / "source/main/main.cpp"),
            "target": "RoR-Combined",
            "ogre_next_usage_leaked": False,
        }
        if isolated_consumers != [expected_isolated_consumer]:
            raise ValueError("namespace audit lacks exact RoR-Combined compile isolation")
        stb_implementation = namespace_audit_document.get(
            "stb_image_implementation"
        )
        expected_stb_source = str(
            provider_source_root
            / "source/main/gfx/render/Ogre14SourceTextureDecoder.cpp"
        )
        if (
            not isinstance(stb_implementation, dict)
            or stb_implementation.get("source") != expected_stb_source
            or stb_implementation.get("target") != "RoR-Combined"
            or type(stb_implementation.get("target_compile_entries")) is not int
            or stb_implementation.get("target_compile_entries", 0) < 1
            or stb_implementation.get("implementation_compile_entries") != 1
            or stb_implementation.get("external_configuration_tokens") != []
            or stb_implementation.get("indirect_input_tokens") != []
            or stb_implementation.get("strict_fp") is not True
            or not isinstance(
                stb_implementation.get("compile_command_sha256"), str
            )
            or not re.fullmatch(
                r"[0-9a-f]{64}",
                str(stb_implementation.get("compile_command_sha256", "")),
            )
        ):
            raise ValueError(
                "namespace audit lacks exact effective stb_image owner evidence"
            )
        namespace_evidence_scope = namespace_audit_document.get("evidence_scope")
        if not isinstance(namespace_evidence_scope, dict) or any(
            namespace_evidence_scope.get(field) is not True
            for field in (
                "namespace_and_dual_root_link",
                "full_n1_runtime_link",
                "renderer_neutral_in_process_session_link",
                "concrete_in_process_presenter_link",
            )
        ):
            raise ValueError("namespace audit evidence scope is incomplete")
        if (
            contract_document.get("target") != "RoR-Combined"
            or contract_document.get("transport_or_bridge_sources_linked") is not False
        ):
            raise ValueError("combined executable contract admits a retired renderer closure")
        required_archives = [
            _regular_absolute(value, "required combined archive")
            for value in arguments.required_archive
        ]
        required_ogre14_dylibs = [
            _regular_absolute(value, "required OGRE14 dylib")
            for value in arguments.required_ogre14_dylib
        ]
        sdl_provider_dylib = _regular_absolute(
            arguments.sdl_provider_dylib, "OGRE14 SDL provider dylib"
        )
        if len(required_archives) != len(set(required_archives)):
            raise ValueError("required combined archive inputs contain duplicates")
        if len(required_ogre14_dylibs) != len(set(required_ogre14_dylibs)):
            raise ValueError("required OGRE14 dylib inputs contain duplicates")
        if sdl_provider_dylib not in required_ogre14_dylibs:
            raise ValueError("SDL provider is absent from the required OGRE14 closure")

        audited_archive_records = namespace_audit_document.get("next_archives")
        if not isinstance(audited_archive_records, list):
            raise ValueError("namespace audit archive closure is invalid")
        verified_audited_archives = [
            _verified_file_record(record, "namespace-audited archive")
            for record in audited_archive_records
        ]
        for field, description in (
            ("embedded_runtime_archive", "namespace-audited N1 runtime"),
            ("direct_contract_archive", "namespace-audited direct contract"),
        ):
            verified_audited_archives.append(
                _verified_file_record(
                    namespace_audit_document.get(field), description
                )
            )
        audited_archive_paths = {
            record["path"] for record in verified_audited_archives
        }
        missing_audited_archives = sorted(
            str(path)
            for path in required_archives
            if str(path) not in audited_archive_paths
        )
        if missing_audited_archives:
            raise ValueError(
                "required combined archives lack namespace audit evidence: "
                + ", ".join(missing_audited_archives)
            )
        global_intersections = namespace_audit_document.get(
            "defined_global_intersections"
        )
        if not isinstance(global_intersections, list) or not global_intersections:
            raise ValueError("namespace audit global collision evidence is missing")
        collision_archive_paths: set[str] = set()
        for collision_record in global_intersections:
            if not isinstance(collision_record, dict):
                raise ValueError("namespace audit collision record is invalid")
            intersection = collision_record.get("intersection")
            reviewed_allowlist = collision_record.get("reviewed_allowlist")
            archive_path = collision_record.get("modern_archive")
            if (
                not isinstance(archive_path, str)
                or archive_path in collision_archive_paths
                or not isinstance(intersection, list)
                or intersection != sorted(set(intersection))
                or not all(isinstance(symbol, str) for symbol in intersection)
                or any(
                    symbol not in REVIEWED_GLOBAL_INTERSECTION_ALLOWLIST
                    for symbol in intersection
                )
                or reviewed_allowlist
                != list(REVIEWED_GLOBAL_INTERSECTION_ALLOWLIST)
                or not isinstance(
                    collision_record.get("modern_defined_globals"), int
                )
                or collision_record.get("modern_defined_globals", 0) < 1
                or not isinstance(
                    collision_record.get("legacy_closure_defined_globals"), int
                )
                or collision_record.get("legacy_closure_defined_globals", 0) < 1
            ):
                raise ValueError("namespace audit collision record is fail-open")
            collision_archive_paths.add(archive_path)
        if collision_archive_paths != audited_archive_paths:
            raise ValueError(
                "namespace audit collision records differ from audited archives"
            )

        audited_legacy_records = namespace_audit_document.get(
            "legacy_runtime_libraries"
        )
        if not isinstance(audited_legacy_records, list):
            raise ValueError("namespace audit OGRE14 closure is invalid")
        verified_audited_legacy = [
            _verified_file_record(record, "namespace-audited OGRE14 dylib")
            for record in audited_legacy_records
        ]
        audited_legacy_paths = {
            record["path"] for record in verified_audited_legacy
        }
        if (
            len(verified_audited_legacy)
            != provider_contract_document.get("ogre14_runtime_library_count")
            or any(str(path) not in audited_legacy_paths for path in required_ogre14_dylibs)
        ):
            raise ValueError("required OGRE14 dylibs lack full collision-audit evidence")
        ogre14_runtime_manifest = _verify_ogre14_runtime_manifest(
            provider_contract_document, verified_audited_legacy
        )
        expected_host_runtimes = {
            ogre14_runtime_manifest["main_runtime"],
            ogre14_runtime_manifest["sdl_provider_runtime"],
        }
        if (
            {str(path) for path in required_ogre14_dylibs}
            != expected_host_runtimes
            or str(sdl_provider_dylib)
            != ogre14_runtime_manifest["sdl_provider_runtime"]
        ):
            raise ValueError(
                "final host runtime arguments differ from the configured OGRE14 manifest"
            )

        demangled_external = subprocess.run(
            [
                str(nm),
                "--defined-only",
                "--extern-only",
                "--demangle",
                str(binary),
            ],
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="strict",
        )
        if demangled_external.returncode != 0:
            raise ValueError(
                "demangled external nm failed "
                f"({demangled_external.returncode}): "
                f"{demangled_external.stderr.strip()}"
            )
        raw_external = subprocess.run(
            [str(nm), "--defined-only", "--extern-only", str(binary)],
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="strict",
        )
        if raw_external.returncode != 0:
            raise ValueError(
                f"raw external nm failed ({raw_external.returncode}): "
                f"{raw_external.stderr.strip()}"
            )
        demangled_external_symbols = _nm_symbol_names(
            demangled_external.stdout
        )
        raw_external_symbols = _nm_symbol_names(raw_external.stdout)
        symbol_violations = sorted(
            token for token in FORBIDDEN_SYMBOL_TOKENS
            if any(token in symbol for symbol in demangled_external_symbols)
        )
        external_image_codec_symbol_violations = (
            _external_image_codec_symbol_violations(raw_external.stdout)
        )
        all_defined = subprocess.run(
            [
                str(nm),
                "--defined-only",
                "--demangle",
                str(binary),
            ],
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="strict",
        )
        if all_defined.returncode != 0:
            raise ValueError(
                f"all-defined nm failed ({all_defined.returncode}): "
                f"{all_defined.stderr.strip()}"
            )
        missing_symbol_evidence = _missing_required_demangled_symbols(
            all_defined.stdout
        )
        raw_all_defined = subprocess.run(
            [str(nm), "--defined-only", str(binary)],
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="strict",
        )
        if raw_all_defined.returncode != 0:
            raise ValueError(
                f"raw all-defined nm failed ({raw_all_defined.returncode}): "
                f"{raw_all_defined.stderr.strip()}"
            )
        raw_all_defined_symbols = _nm_symbol_names(raw_all_defined.stdout)

        undefined_symbols = subprocess.run(
            [str(nm), "-u", str(binary)],
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="strict",
        )
        if undefined_symbols.returncode != 0:
            raise ValueError(
                f"undefined nm failed ({undefined_symbols.returncode}): "
                f"{undefined_symbols.stderr.strip()}"
            )
        codec_freeimage = Path(str(ogre14_runtime_manifest["codec_freeimage"]))
        codec_symbols = subprocess.run(
            [
                str(nm),
                "--defined-only",
                "--extern-only",
                str(codec_freeimage),
            ],
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="strict",
        )
        if codec_symbols.returncode != 0:
            raise ValueError(
                f"Codec_FreeImage nm failed ({codec_symbols.returncode}): "
                f"{codec_symbols.stderr.strip()}"
            )
        codec_defined_symbols = _nm_symbol_names(codec_symbols.stdout)
        defined_codec_intersection, unexpected_defined_codec_intersection = (
            _unexpected_symbol_intersection(
                raw_external_symbols,
                codec_defined_symbols,
                REVIEWED_CODEC_FREEIMAGE_DEFINED_INTERSECTION_ALLOWLIST,
            )
        )
        undefined_codec_intersection, unexpected_undefined_codec_intersection = (
            _unexpected_symbol_intersection(
                _nm_undefined_symbol_names(undefined_symbols.stdout),
                codec_defined_symbols,
                REVIEWED_CODEC_FREEIMAGE_UNDEFINED_INTERSECTION_ALLOWLIST,
            )
        )

        # Parse only Apple's structured object and symbol tables. Literal
        # payloads later in the map may contain arbitrary bytes and must never
        # satisfy positive archive/object evidence.
        link_map_payload = link_map.read_bytes()
        structural_link_map = _structural_link_map_evidence(
            link_map_payload,
            binary,
            build_root,
            required_archives,
            raw_all_defined_symbols,
        )
        object_violations = structural_link_map["forbidden_objects"]
        extracted_sdl_members = structural_link_map[
            "root_sdl_archive_members_extracted"
        ]
        extracted_root_image_codec_members = structural_link_map[
            "root_image_codec_archives"
        ]
        structural_root_sdl_symbols = structural_link_map[
            "root_sdl_defined_symbols"
        ]
        missing_archive_evidence = structural_link_map[
            "missing_required_archives"
        ]

        linked_dylibs = subprocess.run(
            [str(otool), "-L", str(binary)],
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="strict",
        )
        if linked_dylibs.returncode != 0:
            raise ValueError(
                f"otool failed ({linked_dylibs.returncode}): "
                f"{linked_dylibs.stderr.strip()}"
            )
        binary_load_commands = subprocess.run(
            [str(otool), "-l", str(binary)],
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="strict",
        )
        if binary_load_commands.returncode != 0:
            raise ValueError(
                f"otool load-command scan failed ({binary_load_commands.returncode}): "
                f"{binary_load_commands.stderr.strip()}"
            )
        direct_dynamic_loads = _direct_dynamic_load_evidence(
            linked_dylibs.stdout,
            binary_load_commands.stdout,
            binary,
            ogre14_runtime_manifest,
        )
        unexpected_non_system_load_commands = direct_dynamic_loads[
            "unexpected_non_system"
        ]
        duplicate_non_system_load_basenames = direct_dynamic_loads[
            "duplicate_non_system_basenames"
        ]
        unresolved_non_system_load_commands = direct_dynamic_loads[
            "resolution_failures"
        ]
        missing_direct_load_basenames = direct_dynamic_loads[
            "missing_required_basenames"
        ]
        missing_ogre14_dylib_evidence = sorted(
            str(dylib)
            for dylib in required_ogre14_dylibs
            if dylib.name in direct_dynamic_loads["missing_required_basenames"]
        )

        sdl_provider_symbols = subprocess.run(
            [
                str(nm),
                "--defined-only",
                "--extern-only",
                str(sdl_provider_dylib),
            ],
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="strict",
        )
        if sdl_provider_symbols.returncode != 0:
            raise ValueError(
                f"nm failed for SDL provider ({sdl_provider_symbols.returncode}): "
                f"{sdl_provider_symbols.stderr.strip()}"
            )
        sdl_provider_defined_symbols = _nm_symbol_names(
            sdl_provider_symbols.stdout
        )
        sdl_provider_api_symbols = _sdl_definition_symbols(
            sdl_provider_defined_symbols
        )
        missing_sdl_provider_symbols = sorted(
            token
            for token in REQUIRED_SDL_PROVIDER_SYMBOL_TOKENS
            if f"_{token}" not in sdl_provider_defined_symbols
        )
        executable_sdl_definitions = _sdl_definition_symbols(
            raw_external_symbols
        )
        executable_sdl_provider_intersection = sorted(
            set(executable_sdl_definitions) & set(sdl_provider_api_symbols)
        )

        if (
            symbol_violations
            or external_image_codec_symbol_violations
            or unexpected_defined_codec_intersection
            or unexpected_undefined_codec_intersection
            or object_violations
            or extracted_sdl_members
            or structural_root_sdl_symbols
            or executable_sdl_definitions
            or extracted_root_image_codec_members
            or missing_symbol_evidence
            or missing_archive_evidence
            or missing_ogre14_dylib_evidence
            or missing_direct_load_basenames
            or unexpected_non_system_load_commands
            or duplicate_non_system_load_basenames
            or unresolved_non_system_load_commands
            or missing_sdl_provider_symbols
        ):
            details = ", ".join(
                symbol_violations
                + external_image_codec_symbol_violations
                + unexpected_defined_codec_intersection
                + unexpected_undefined_codec_intersection
                + object_violations
                + structural_root_sdl_symbols
                + executable_sdl_definitions
                + extracted_root_image_codec_members
                + missing_symbol_evidence
                + missing_archive_evidence
                + missing_ogre14_dylib_evidence
                + missing_direct_load_basenames
                + unexpected_non_system_load_commands
                + duplicate_non_system_load_basenames
                + unresolved_non_system_load_commands
                + missing_sdl_provider_symbols
            )
            if extracted_sdl_members:
                details = f"{details}, extracted libSDL2.a member".lstrip(", ")
            raise ValueError(f"RoR-Combined closure evidence failed: {details}")

        symbol_lines = [
            line for line in raw_external.stdout.splitlines() if line.strip()
        ]
        all_defined_symbol_lines = [
            line for line in all_defined.stdout.splitlines() if line.strip()
        ]
        _write_receipt(
            receipt,
            {
                "schema": SCHEMA,
                "binary": str(binary),
                "binary_sha256": _sha256(binary),
                "link_map": str(link_map),
                "link_map_sha256": _sha256(link_map),
                "executable_contract": {
                    "path": str(executable_contract),
                    "sha256": _sha256(executable_contract),
                },
                "provider_contract": {
                    "path": str(provider_contract),
                    "sha256": _sha256(provider_contract),
                },
                "namespace_audit_report": {
                    "path": str(namespace_audit_report),
                    "sha256": _sha256(namespace_audit_report),
                    "build_contract": verified_namespace_build_contract,
                    "audited_archives": verified_audited_archives,
                    "audited_ogre14_dylibs": verified_audited_legacy,
                    "stb_image_implementation": stb_implementation,
                },
                "ogre_next_upstream_strict_fp": strict_fp_report,
                "ogre14_runtime_manifest": ogre14_runtime_manifest,
                "authenticated_source_image_decoder": (
                    authenticated_source_image_decoder
                ),
                "codec_freeimage_global_collision_audit": {
                    "path": str(codec_freeimage),
                    "sha256": _sha256(codec_freeimage),
                    "defined_global_count": len(codec_defined_symbols),
                    "defined_intersection": defined_codec_intersection,
                    "defined_intersection_allowlist": list(
                        REVIEWED_CODEC_FREEIMAGE_DEFINED_INTERSECTION_ALLOWLIST
                    ),
                    "unexpected_defined_intersection": [],
                    "undefined_intersection": undefined_codec_intersection,
                    "undefined_intersection_allowlist": list(
                        REVIEWED_CODEC_FREEIMAGE_UNDEFINED_INTERSECTION_ALLOWLIST
                    ),
                    "unexpected_undefined_intersection": [],
                },
                "direct_dynamic_load_commands": direct_dynamic_loads,
                "structural_link_map": structural_link_map,
                "provider_source_manifest": provider_manifest_report,
                "selected_game_source_manifest": selected_manifest_report,
                "defined_external_symbol_count": len(symbol_lines),
                "all_defined_symbol_count": len(all_defined_symbol_lines),
                "forbidden_symbol_tokens": list(FORBIDDEN_SYMBOL_TOKENS),
                "forbidden_external_image_codec_symbol_prefixes": list(
                    FORBIDDEN_EXTERNAL_IMAGE_CODEC_SYMBOL_PREFIXES
                ),
                "external_image_codec_symbols_present": False,
                "forbidden_link_map_object_tokens": list(
                    FORBIDDEN_LINK_MAP_OBJECT_TOKENS
                ),
                "forbidden_root_image_codec_archive_tokens": list(
                    FORBIDDEN_ROOT_IMAGE_CODEC_ARCHIVE_TOKENS
                ),
                "bridge_or_transport_symbols_present": False,
                "bridge_or_transport_objects_present": False,
                "required_symbol_tokens": list(REQUIRED_SYMBOL_TOKENS),
                "required_symbols_parsed_with_exact_prefixes": True,
                "required_archives": [
                    {"path": str(path), "sha256": _sha256(path)}
                    for path in required_archives
                ],
                "required_ogre14_dylibs": [
                    {"path": str(path), "sha256": _sha256(path)}
                    for path in required_ogre14_dylibs
                ],
                "sdl_provider_dylib": {
                    "path": str(sdl_provider_dylib),
                    "sha256": _sha256(sdl_provider_dylib),
                    "required_symbols": list(REQUIRED_SDL_PROVIDER_SYMBOL_TOKENS),
                    "defined_sdl_symbol_count": len(sdl_provider_api_symbols),
                    "executable_defined_intersection": (
                        executable_sdl_provider_intersection
                    ),
                },
                "root_sdl_static_archive_members_extracted": False,
                "root_sdl_symbols_present": False,
                "root_image_codec_static_archive_members_extracted": False,
                "extra_image_codec_dynamic_libraries_loaded": False,
                "codec_freeimage_unreviewed_global_intersections_present": False,
                "authenticated_source_texture_decoder_present": True,
                "ogre_next_runtime_contributors_present": True,
                "ogre14_host_load_commands_present": True,
            },
        )
    except (OSError, UnicodeError, ValueError) as error:
        print(f"combined binary closure verification failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
