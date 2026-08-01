#!/usr/bin/env python3
"""Build, run, and attest the Windows Ogre-Next D3D11On12/DXR RT7 proof."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PureWindowsPath
import platform
import re
import secrets
import shutil
import struct
import subprocess
import sys
import tempfile
from typing import Any


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
PROBE_SOURCE = Path(__file__).resolve().parent
MAIN_RUNNER_PATH = REPOSITORY_ROOT / "tools" / "run_ogre_next_probe.py"
MAIN_RUNNER_SPEC = importlib.util.spec_from_file_location(
    "run_ogre_next_probe_for_windows_dxr7", MAIN_RUNNER_PATH
)
if MAIN_RUNNER_SPEC is None or MAIN_RUNNER_SPEC.loader is None:
    raise RuntimeError("could not load the pinned Ogre-Next runner")
MAIN_RUNNER = importlib.util.module_from_spec(MAIN_RUNNER_SPEC)
MAIN_RUNNER_SPEC.loader.exec_module(MAIN_RUNNER)

REPORT_NAME = "ror-ogre-next-windows-dxr7-report.json"
ATTESTATION_NAME = "ror-ogre-next-windows-dxr7-attestation.json"
EXECUTION_RECEIPT_NAME = "ror-ogre-next-windows-dxr7-execution-receipt.json"
DSSE_BUNDLE_NAME = "ror-ogre-next-windows-dxr7-execution-receipt.sigstore.jsonl"
OGRE_FRAME_NAME = "ror-ogre-next-windows-dxr7-ogre-frame.ppm"
DXIL_RELATIVE = "generated/ror_ogre_next_windows_dxr7_probe.dxil"
SCHEMA = "ror.ogre_next_windows_dxr_rt7.v3"
ATTESTATION_SCHEMA = "ror.ogre_next_windows_dxr_rt7.attestation.v3"
EXECUTION_RECEIPT_SCHEMA = (
    "ror.ogre_next_windows_dxr_rt7.execution_receipt.v2"
)
VERIFICATION_SCHEMA = "ror.ogre_next_windows_dxr_rt7.verification.v1"
TRUSTED_REPOSITORY = "oasiz-ai/rigs-of-rods"
TRUSTED_SIGNER_WORKFLOW = (
    "oasiz-ai/rigs-of-rods/.github/workflows/ogre-next-probe.yml"
)
TRUSTED_WORKFLOW_PATH = ".github/workflows/ogre-next-probe.yml"
LOCK_NAME = "windows-dxr7.lock.json"
LOCK_SHA256 = (
    "c7092a523109a08111173acc6c30e9d838ca56e997764be38454db2f4b8d5359"
)
UNSUPPORTED_EXIT_CODE = 77
REQUIRED_CONFIG = "Release"
UINT32_MAX = (1 << 32) - 1
UINT64_MAX = (1 << 64) - 1
SCOPE_LIMITATION = (
    "one hardware DXR primary-ray closest-hit readback plus exact D3D11On12 "
    "Ogre device adoption and a separate UI-free Ogre PBS raster readback; "
    "no hybrid ray/raster composite, GI, reflection, denoising, multi-bounce, "
    "or production material-parity claim"
)
OFFLINE_EXECUTION_LIMITATION = (
    "hashes, binary structure, report semantics, and a fresh nonce can be "
    "verified offline, but an offline bundle cannot cryptographically prove "
    "that its executable ran; require the GitHub artifact attestation for "
    "that execution receipt"
)


class Dxr7Error(RuntimeError):
    """Raised when DXR7 code or evidence fails closed."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def is_sha256(value: object) -> bool:
    return (
        isinstance(value, str)
        and re.fullmatch(r"[0-9a-f]{64}", value) is not None
    )


def is_uint32(value: object) -> bool:
    return type(value) is int and 0 <= value <= UINT32_MAX


def is_uint64(value: object) -> bool:
    return type(value) is int and 0 <= value <= UINT64_MAX


def require_exact_keys(
    value: object, expected: set[str], label: str
) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != expected:
        raise Dxr7Error(f"{label} keys changed")
    return value


def exact_json_equal(actual: object, expected: object) -> bool:
    if type(actual) is not type(expected):
        return False
    if isinstance(expected, dict):
        return set(actual) == set(expected) and all(
            exact_json_equal(actual[key], expected[key]) for key in expected
        )
    if isinstance(expected, list):
        return len(actual) == len(expected) and all(
            exact_json_equal(lhs, rhs)
            for lhs, rhs in zip(actual, expected, strict=True)
        )
    return actual == expected


def fsync_parent_directory(path: Path) -> None:
    if os.name == "nt":
        # Python's os.replace maps to a replacing MoveFile operation on
        # Windows. The C++ producer uses MOVEFILE_WRITE_THROUGH; Python has no
        # portable directory-fsync primitive there, so the replacement below
        # uses the same Win32 write-through API instead.
        return
    flags = os.O_RDONLY
    if hasattr(os, "O_DIRECTORY"):
        flags |= os.O_DIRECTORY
    directory_fd = os.open(path.parent, flags)
    try:
        os.fsync(directory_fd)
    finally:
        os.close(directory_fd)


def replace_atomically(temporary_path: Path, destination: Path) -> None:
    if os.name != "nt":
        os.replace(temporary_path, destination)
        fsync_parent_directory(destination)
        return
    import ctypes

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    move_file_ex = kernel32.MoveFileExW
    move_file_ex.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_ulong]
    move_file_ex.restype = ctypes.c_int
    movefile_replace_existing = 0x1
    movefile_write_through = 0x8
    if not move_file_ex(
        str(temporary_path),
        str(destination),
        movefile_replace_existing | movefile_write_through,
    ):
        raise OSError(ctypes.get_last_error(), "MoveFileExW failed")


def read_json(path: Path, label: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise Dxr7Error(f"could not read {label}: {error}") from error
    if not isinstance(value, dict):
        raise Dxr7Error(f"{label} root is not an object")
    return value


def write_json_atomically(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary_path: Path | None = None
    try:
        payload = (json.dumps(value, indent=2, sort_keys=True) + "\n").encode(
            "utf-8"
        )
        with tempfile.NamedTemporaryFile(
            mode="wb",
            prefix=path.name + ".tmp-",
            dir=path.parent,
            delete=False,
        ) as temporary:
            temporary_path = Path(temporary.name)
            temporary.write(payload)
            temporary.flush()
            os.fsync(temporary.fileno())
        replace_atomically(temporary_path, path)
        temporary_path = None
    except OSError as error:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)
        raise Dxr7Error(f"could not publish DXR7 attestation: {error}") from error


def require_direct_file(path: Path, label: str) -> Path:
    try:
        if not path.is_file() or path.is_symlink() or path.stat().st_size <= 0:
            raise Dxr7Error(f"{label} is missing, empty, or indirect")
    except OSError as error:
        raise Dxr7Error(f"could not inspect {label}: {error}") from error
    return path


def artifact_record(path: Path, name: str | None = None) -> dict[str, Any]:
    require_direct_file(path, name or path.name)
    return {
        "path": name or path.name,
        "bytes": path.stat().st_size,
        "sha256": sha256_file(path),
    }


def load_dxr7_lock() -> dict[str, Any]:
    path = PROBE_SOURCE / LOCK_NAME
    try:
        source = path.read_text(encoding="utf-8")
        lock = json.loads(source)
    except (OSError, json.JSONDecodeError) as error:
        raise Dxr7Error(f"could not read Windows DXR7 lock: {error}") from error
    if source != json.dumps(lock, indent=2) + "\n":
        raise Dxr7Error("Windows DXR7 lock is not canonical JSON")
    if sha256_file(path) != LOCK_SHA256:
        raise Dxr7Error("Windows DXR7 lock moved without review")
    require_exact_keys(
        lock,
        {
            "schema",
            "platform_policy",
            "ogre_next_commit",
            "adaptation_patch",
            "shader",
            "runtime",
        },
        "Windows DXR7 lock",
    )
    patch = require_exact_keys(
        lock["adaptation_patch"], {"path", "sha256", "scope"},
        "Windows DXR7 patch lock",
    )
    shader = require_exact_keys(
        lock["shader"],
        {
            "path",
            "sha256",
            "target",
            "entry_exports",
            "compiler_provider",
            "compiler_path_policy",
            "compiler_closure",
            "compiler_version_command",
            "compiler_arguments",
        },
        "Windows DXR7 shader lock",
    )
    runtime = require_exact_keys(
        lock["runtime"],
        {
            "minimum_dxr_tier",
            "software_adapter_pass_allowed",
            "required_dispatch_dimensions",
            "closest_hit_readback",
        },
        "Windows DXR7 runtime lock",
    )
    expected = {
        "schema": lock.get("schema")
        == "ror.ogre_next_windows_dxr7_toolchain.v2",
        "platform": lock.get("platform_policy")
        == "windows-x64-d3d11on12-dxr",
        "ogre_commit": lock.get("ogre_next_commit")
        == "37149a802de747f6806996fa3067b0748ecc1084",
        "patch_path": patch.get("path")
        == "patches/0004-d3d11-adopt-external-device.patch",
        "patch_hash": patch.get("sha256")
        == "a9adf369ab03b8b14b1d81aa7a3f3303663606ce60c61bd6f5418048cd137834",
        "patch_scope": patch.get("scope")
        == (
            "D3D11 plugin external-device adoption with shared post-device "
            "initialization"
        ),
        "shader_path": shader.get("path")
        == "shaders/windows_dxr7_probe.hlsl",
        "shader_hash": shader.get("sha256")
        == "3246fcd8c91b0dffa149c25223455ca136a2e1813cd75af6d5dd49368d897df3",
        "shader_target": shader.get("target") == "lib_6_5",
        "exports": shader.get("entry_exports")
        == ["RayGen", "Miss", "ClosestHit"],
        "compiler": shader.get("compiler_provider")
        == "Windows SDK versioned x64 closure",
        "compiler_path": shader.get("compiler_path_policy")
        == "Windows Kits/10/bin/<version>/x64",
        "compiler_closure": shader.get("compiler_closure")
        == ["dxc.exe", "dxcompiler.dll", "dxil.dll"],
        "compiler_version": shader.get("compiler_version_command")
        == ["--version"],
        "compiler_arguments": shader.get("compiler_arguments")
        == ["-HV", "2021", "-O3", "-Qstrip_debug", "-Qstrip_reflect"],
        "tier": runtime.get("minimum_dxr_tier") == "1.1",
        "no_software": runtime.get("software_adapter_pass_allowed") is False,
        "dispatch": runtime.get("required_dispatch_dimensions") == [1, 1, 1],
        "readback": runtime.get("closest_hit_readback") == 0xD1CEB00B,
    }
    failed = sorted(name for name, passed in expected.items() if not passed)
    if failed:
        raise Dxr7Error("Windows DXR7 lock failed closed: " + ", ".join(failed))
    for record, label in ((patch, "patch"), (shader, "shader")):
        if not is_sha256(record.get("sha256")):
            raise Dxr7Error(f"Windows DXR7 {label} hash is invalid")
        artifact = PROBE_SOURCE / str(record["path"])
        if not artifact.is_file() or artifact.is_symlink():
            raise Dxr7Error(f"Windows DXR7 {label} source is missing or indirect")
        if sha256_file(artifact) != record["sha256"]:
            raise Dxr7Error(f"Windows DXR7 {label} source hash mismatch")
    return lock


def executable_path(build_dir: Path) -> Path:
    names = (
        "bin/ror_ogre_next_windows_dxr7_smoke.exe",
        f"bin/{REQUIRED_CONFIG}/ror_ogre_next_windows_dxr7_smoke.exe",
    )
    existing = [build_dir / name for name in names if (build_dir / name).is_file()]
    if len(existing) != 1:
        raise Dxr7Error(
            f"expected exactly one DXR7 executable, found {len(existing)}"
        )
    return existing[0]


def _read_u16(contents: bytes, offset: int, label: str) -> int:
    if offset < 0 or offset + 2 > len(contents):
        raise Dxr7Error(f"{label} is truncated")
    return struct.unpack_from("<H", contents, offset)[0]


def _read_u32(contents: bytes, offset: int, label: str) -> int:
    if offset < 0 or offset + 4 > len(contents):
        raise Dxr7Error(f"{label} is truncated")
    return struct.unpack_from("<I", contents, offset)[0]


def validate_pe_executable(
    path: Path, expected_binary_marker: str
) -> dict[str, Any]:
    """Validate the structural identity of the exact Windows x64 probe PE.

    This is intentionally stricter than checking an ``MZ`` prefix.  It walks
    the DOS, COFF, PE32+, and section tables, requires an executable x64 image
    with a populated executable ``.text`` section, and binds the source marker
    emitted by the compiled smoke target.  It is an integrity/semantic check,
    not a cryptographic proof that Windows executed the file.
    """

    require_direct_file(path, "DXR7 PE executable")
    contents = path.read_bytes()
    if len(contents) < 1024 or contents[:2] != b"MZ":
        raise Dxr7Error("DXR7 executable is not a nontrivial DOS/PE image")
    pe_offset = _read_u32(contents, 0x3C, "DXR7 DOS header")
    if pe_offset < 0x40 or pe_offset + 24 > len(contents):
        raise Dxr7Error("DXR7 PE header offset is invalid")
    if contents[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise Dxr7Error("DXR7 executable has no PE signature")
    coff = pe_offset + 4
    machine = _read_u16(contents, coff, "DXR7 COFF header")
    section_count = _read_u16(contents, coff + 2, "DXR7 COFF header")
    optional_size = _read_u16(contents, coff + 16, "DXR7 COFF header")
    characteristics = _read_u16(contents, coff + 18, "DXR7 COFF header")
    if machine != 0x8664 or not 1 <= section_count <= 96:
        raise Dxr7Error("DXR7 executable is not a bounded x64 PE image")
    if (characteristics & 0x0002) == 0 or (characteristics & 0x2000) != 0:
        raise Dxr7Error("DXR7 PE is not an executable non-DLL image")
    optional = coff + 20
    if optional_size < 112 or optional + optional_size > len(contents):
        raise Dxr7Error("DXR7 PE optional header is invalid")
    if _read_u16(contents, optional, "DXR7 PE optional header") != 0x20B:
        raise Dxr7Error("DXR7 executable is not PE32+")
    entry_point = _read_u32(contents, optional + 16, "DXR7 PE optional header")
    image_size = _read_u32(contents, optional + 56, "DXR7 PE optional header")
    header_size = _read_u32(contents, optional + 60, "DXR7 PE optional header")
    subsystem = _read_u16(contents, optional + 68, "DXR7 PE optional header")
    if entry_point == 0 or image_size <= header_size or header_size > len(contents):
        raise Dxr7Error("DXR7 PE image sizing or entry point is invalid")
    if subsystem not in {2, 3}:
        raise Dxr7Error("DXR7 PE has an unexpected Windows subsystem")
    section_table = optional + optional_size
    if section_table + section_count * 40 > len(contents):
        raise Dxr7Error("DXR7 PE section table is truncated")
    text_section: dict[str, int] | None = None
    for index in range(section_count):
        section = section_table + index * 40
        raw_name = contents[section : section + 8].split(b"\0", 1)[0]
        try:
            name = raw_name.decode("ascii")
        except UnicodeDecodeError as error:
            raise Dxr7Error("DXR7 PE section name is not ASCII") from error
        virtual_size = _read_u32(contents, section + 8, "DXR7 PE section")
        raw_size = _read_u32(contents, section + 16, "DXR7 PE section")
        raw_offset = _read_u32(contents, section + 20, "DXR7 PE section")
        section_flags = _read_u32(contents, section + 36, "DXR7 PE section")
        if raw_size and (
            raw_offset < header_size or raw_offset + raw_size > len(contents)
        ):
            raise Dxr7Error("DXR7 PE section payload is out of bounds")
        if name == ".text":
            text_section = {
                "virtual_size": virtual_size,
                "raw_size": raw_size,
                "raw_offset": raw_offset,
                "characteristics": section_flags,
            }
    if text_section is None:
        raise Dxr7Error("DXR7 PE has no .text section")
    text_flags = text_section["characteristics"]
    text_payload = contents[
        text_section["raw_offset"] :
        text_section["raw_offset"] + text_section["raw_size"]
    ]
    if (
        text_section["virtual_size"] == 0
        or text_section["raw_size"] == 0
        or (text_flags & 0x00000020) == 0
        or (text_flags & 0x20000000) == 0
        or not any(text_payload)
    ):
        raise Dxr7Error("DXR7 PE .text section is not executable code")
    marker = expected_binary_marker.encode("utf-8")
    if not marker or contents.count(marker) != 1:
        raise Dxr7Error("DXR7 PE does not contain its exact source marker")
    return {
        "format": "PE32+",
        "machine": "x86_64",
        "section_count": section_count,
        "entry_point_rva": entry_point,
        "image_size": image_size,
        "subsystem": subsystem,
        "text_bytes": text_section["raw_size"],
        "source_marker": expected_binary_marker,
    }


def validate_dxil_container(
    path: Path, required_exports: list[str]
) -> dict[str, Any]:
    """Parse the DXBC container and require real DXIL program semantics."""

    require_direct_file(path, "DXR7 DXIL library")
    contents = path.read_bytes()
    if len(contents) < 64 or contents[:4] != b"DXBC":
        raise Dxr7Error("DXR7 shader is not a DXBC/DXIL container")
    if not any(contents[4:20]):
        raise Dxr7Error("DXR7 shader container digest is empty")
    container_size = _read_u32(contents, 24, "DXIL container header")
    part_count = _read_u32(contents, 28, "DXIL container header")
    if container_size != len(contents) or not 1 <= part_count <= 64:
        raise Dxr7Error("DXIL container size or part count is invalid")
    offsets_end = 32 + part_count * 4
    if offsets_end > len(contents):
        raise Dxr7Error("DXIL part-offset table is truncated")
    offsets = [
        _read_u32(contents, 32 + index * 4, "DXIL part-offset table")
        for index in range(part_count)
    ]
    if len(set(offsets)) != len(offsets):
        raise Dxr7Error("DXIL part offsets are not unique")
    parts: dict[str, bytes] = {}
    for offset in offsets:
        if offset % 4 != 0 or offset < offsets_end or offset + 8 > len(contents):
            raise Dxr7Error("DXIL part offset is invalid")
        fourcc_bytes = contents[offset : offset + 4]
        try:
            fourcc = fourcc_bytes.decode("ascii")
        except UnicodeDecodeError as error:
            raise Dxr7Error("DXIL part FourCC is not ASCII") from error
        size = _read_u32(contents, offset + 4, "DXIL part header")
        end = offset + 8 + size
        if size == 0 or end > len(contents) or fourcc in parts:
            raise Dxr7Error("DXIL part is empty, duplicated, or out of bounds")
        parts[fourcc] = contents[offset + 8 : end]
    if "DXIL" not in parts or "SFI0" not in parts:
        raise Dxr7Error("DXIL container lacks required DXIL/SFI0 parts")
    program = parts["DXIL"]
    if len(program) < 24 or program[8:12] != b"DXIL":
        raise Dxr7Error("DXIL program header is invalid")
    program_words = _read_u32(program, 4, "DXIL program header")
    bitcode_offset = _read_u32(program, 16, "DXIL bitcode header")
    bitcode_size = _read_u32(program, 20, "DXIL bitcode header")
    bitcode_start = 8 + bitcode_offset
    bitcode_end = bitcode_start + bitcode_size
    if (
        program_words * 4 > len(program)
        or bitcode_size < 4
        or bitcode_start < 24
        or bitcode_end > len(program)
        or program[bitcode_start : bitcode_start + 4] != b"BC\xc0\xde"
    ):
        raise Dxr7Error("DXIL LLVM bitcode payload is invalid")
    missing_exports = [
        export
        for export in required_exports
        if export.encode("utf-8") not in contents
    ]
    if missing_exports:
        raise Dxr7Error(
            "DXIL library is missing locked exports: "
            + ", ".join(missing_exports)
        )
    return {
        "format": "DXBC/DXIL",
        "container_bytes": container_size,
        "part_count": part_count,
        "part_fourccs": sorted(parts),
        "llvm_bitcode_bytes": bitcode_size,
        "exports": required_exports,
    }


def _normalized_path(path: Path) -> str:
    return str(path.resolve()).replace("\\", "/").rstrip("/").lower()


def validate_dxc_closure(
    dxc: Path,
    sdk_bin_root: Path,
    *,
    execute_version: bool,
) -> dict[str, Any]:
    dxc = require_direct_file(dxc.resolve(), "Windows SDK dxc.exe")
    sdk_bin_root = sdk_bin_root.resolve()
    if platform.system().lower() == "windows":
        program_files_x86 = os.environ.get("ProgramFiles(x86)")
        if not program_files_x86:
            raise Dxr7Error(
                "ProgramFiles(x86) is unavailable for Windows SDK validation"
            )
        reviewed_sdk_root = (
            Path(program_files_x86) / "Windows Kits/10/bin"
        ).resolve()
        if _normalized_path(sdk_bin_root) != _normalized_path(
            reviewed_sdk_root
        ):
            raise Dxr7Error(
                "DXC is outside Program Files (x86)/Windows Kits/10/bin"
            )
    try:
        relative = dxc.relative_to(sdk_bin_root)
    except ValueError as error:
        raise Dxr7Error("dxc.exe is outside the reviewed Windows SDK bin root") from error
    if (
        len(relative.parts) != 3
        or re.fullmatch(r"10\.0\.\d+\.\d+", relative.parts[0]) is None
        or relative.parts[1].lower() != "x64"
        or relative.parts[2].lower() != "dxc.exe"
    ):
        raise Dxr7Error(
            "dxc.exe must be Windows Kits/10/bin/<version>/x64/dxc.exe"
        )
    x64_dir = dxc.parent
    components: dict[str, dict[str, Any]] = {}
    for filename in ("dxc.exe", "dxcompiler.dll", "dxil.dll"):
        component = require_direct_file(x64_dir / filename, filename)
        if component.parent.resolve() != x64_dir:
            raise Dxr7Error("DXC closure escaped the reviewed SDK x64 directory")
        components[filename] = artifact_record(component, filename)
    version = ""
    if execute_version:
        try:
            result = subprocess.run(
                [str(dxc), "--version"],
                check=False,
                capture_output=True,
                text=True,
                timeout=30,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            raise Dxr7Error(f"could not query Windows SDK dxc.exe: {error}") from error
        version = " ".join((result.stdout or result.stderr).split())
        if result.returncode != 0 or not version or len(version) > 512:
            raise Dxr7Error("Windows SDK dxc.exe did not return a bounded version")
    return {
        "provider": "Windows SDK",
        "sdk_version": relative.parts[0],
        "sdk_bin_root": _normalized_path(sdk_bin_root),
        "x64_directory": _normalized_path(x64_dir),
        "dxc_path": _normalized_path(dxc),
        "dxc_version": version,
        "components": components,
    }


def expected_binary_marker(source_identity: dict[str, Any]) -> str:
    return (
        "ror-ogre-next-dxr7-pe-v2:"
        + source_identity["commit"]
        + ":"
        + source_identity["relevant_manifest_sha256"]
    )


def read_cmake_cache_value(build_dir: Path, key: str) -> str:
    cache = require_direct_file(build_dir / "CMakeCache.txt", "CMake cache")
    matches: list[str] = []
    for line in cache.read_text(encoding="utf-8", errors="strict").splitlines():
        if line.startswith(key + ":") and "=" in line:
            matches.append(line.split("=", 1)[1])
    if len(matches) != 1 or not matches[0]:
        raise Dxr7Error(f"CMake cache has no unique {key}")
    return matches[0]


def configured_dxc_closure(build_dir: Path) -> dict[str, Any]:
    dxc = Path(read_cmake_cache_value(build_dir, "ROR_OGRE_NEXT_DXC_EXECUTABLE"))
    sdk_root = Path(
        read_cmake_cache_value(build_dir, "ROR_OGRE_NEXT_DXC_SDK_BIN_ROOT")
    )
    return validate_dxc_closure(dxc, sdk_root, execute_version=True)


def recorded_dxc_closure(
    build_dir: Path, value: object
) -> dict[str, Any]:
    closure = require_exact_keys(
        value,
        {
            "provider",
            "sdk_version",
            "sdk_bin_root",
            "x64_directory",
            "dxc_path",
            "dxc_version",
            "components",
        },
        "recorded DXC closure",
    )
    components = require_exact_keys(
        closure["components"],
        {"dxc.exe", "dxcompiler.dll", "dxil.dll"},
        "recorded DXC components",
    )

    def windows_path(value: object) -> str:
        if not isinstance(value, str) or not value:
            return ""
        return (
            str(PureWindowsPath(value))
            .replace("\\", "/")
            .rstrip("/")
            .lower()
        )

    sdk_version = closure.get("sdk_version")
    sdk_root = windows_path(closure.get("sdk_bin_root"))
    x64_directory = windows_path(closure.get("x64_directory"))
    dxc_path = windows_path(closure.get("dxc_path"))
    cache_dxc = windows_path(
        read_cmake_cache_value(build_dir, "ROR_OGRE_NEXT_DXC_EXECUTABLE")
    )
    cache_root = windows_path(
        read_cmake_cache_value(build_dir, "ROR_OGRE_NEXT_DXC_SDK_BIN_ROOT")
    )
    checks = {
        "provider": closure.get("provider") == "Windows SDK",
        "sdk_version": isinstance(sdk_version, str)
        and re.fullmatch(r"[0-9]+(?:\.[0-9]+){3}", sdk_version) is not None,
        "sdk_root": bool(sdk_root),
        "x64_directory": x64_directory
        == f"{sdk_root}/{sdk_version}/x64",
        "dxc_path": dxc_path == f"{x64_directory}/dxc.exe",
        "cache_dxc": cache_dxc == dxc_path,
        "cache_root": cache_root == sdk_root,
        "dxc_version": isinstance(closure.get("dxc_version"), str)
        and 0 < len(closure["dxc_version"]) <= 512,
    }
    for filename, component in components.items():
        record = require_exact_keys(
            component,
            {"path", "bytes", "sha256"},
            f"recorded {filename}",
        )
        checks[f"{filename}_record"] = (
            record.get("path") == filename
            and is_uint64(record.get("bytes"))
            and record["bytes"] > 0
            and is_sha256(record.get("sha256"))
        )
    failed = sorted(name for name, passed in checks.items() if not passed)
    if failed:
        raise Dxr7Error(
            "recorded DXC closure failed closed: " + ", ".join(failed)
        )
    return closure


def require_dxc_closure_unchanged(
    build_dir: Path, expected: dict[str, Any]
) -> None:
    if configured_dxc_closure(build_dir) != expected:
        raise Dxr7Error("Windows SDK DXC closure changed during the proof")


def validate_build_context(
    build_dir: Path,
    ogre_lock: dict[str, Any],
    source_identity: dict[str, Any],
) -> dict[str, Any]:
    sentinel = require_direct_file(
        build_dir / MAIN_RUNNER.BUILD_SENTINEL_NAME,
        "Ogre-Next build sentinel",
    )
    try:
        sentinel_contents = sentinel.read_text(encoding="utf-8")
    except OSError as error:
        raise Dxr7Error(f"could not read Ogre-Next build sentinel: {error}") from error
    if sentinel_contents != MAIN_RUNNER.BUILD_SENTINEL_CONTENT:
        raise Dxr7Error("Ogre-Next build sentinel changed")
    contract_path = build_dir / MAIN_RUNNER.BUILD_CONTRACT_NAME
    contract = read_json(contract_path, "Ogre-Next build contract")
    policy = MAIN_RUNNER.detect_policy("Windows", "AMD64")
    MAIN_RUNNER.validate_build_contract(
        contract, ogre_lock, policy, source_identity
    )
    cache_path = require_direct_file(build_dir / "CMakeCache.txt", "CMake cache")
    return {
        "sentinel": artifact_record(
            sentinel, MAIN_RUNNER.BUILD_SENTINEL_NAME
        ),
        "build_contract": artifact_record(
            contract_path, MAIN_RUNNER.BUILD_CONTRACT_NAME
        ),
        "cmake_cache": artifact_record(cache_path, "CMakeCache.txt"),
    }


def _fnv1a64(payload: bytes) -> int:
    value = 14695981039346656037
    for byte in payload:
        value ^= byte
        value = (value * 1099511628211) & UINT64_MAX
    return value


def validate_ogre_frame_ppm(path: Path) -> dict[str, Any]:
    require_direct_file(path, "DXR7 Ogre frame")
    contents = path.read_bytes()
    lines = contents.split(b"\n", 3)
    if len(lines) != 4 or lines[0] != b"P6" or lines[2] != b"255":
        raise Dxr7Error("DXR7 Ogre frame is not canonical binary PPM")
    try:
        dimensions = lines[1].decode("ascii").split()
        width, height = (int(value, 10) for value in dimensions)
    except (UnicodeDecodeError, ValueError) as error:
        raise Dxr7Error("DXR7 Ogre frame dimensions are invalid") from error
    if dimensions != ["192", "128"] or width != 192 or height != 128:
        raise Dxr7Error("DXR7 Ogre frame dimensions changed")
    pixels = lines[3]
    if len(pixels) != width * height * 3:
        raise Dxr7Error("DXR7 Ogre frame payload size is invalid")
    colours = [
        pixels[offset : offset + 3]
        for offset in range(0, len(pixels), 3)
    ]
    counts: dict[bytes, int] = {}
    for colour in colours:
        counts[colour] = counts.get(colour, 0) + 1
    distinct = len(counts)
    non_background = len(colours) - max(counts.values())
    luminances = [
        0.2126 * colour[0] / 255.0
        + 0.7152 * colour[1] / 255.0
        + 0.0722 * colour[2] / 255.0
        for colour in colours
    ]
    if (
        distinct < 8
        or non_background < 512
        or max(luminances) - min(luminances) < 0.05
    ):
        raise Dxr7Error("DXR7 Ogre frame is blank or lacks PBS geometry")
    return {
        "width": width,
        "height": height,
        "distinct_rgb8_values": distinct,
        "non_background_pixels": non_background,
        "rgb8_fnv1a64": f"{_fnv1a64(pixels):016x}",
    }


def unsupported_reason_consistent(
    reason: object, adapter: dict[str, Any], ownership: dict[str, Any]
) -> bool:
    decision = adapter.get("candidate_decision")
    if reason != f"no attested DXR7 adapter: {decision}":
        return False
    no_identity = (
        adapter.get("name") == ""
        and adapter.get("luid") == ""
        and adapter.get("vendor_id") == 0
        and adapter.get("device_id") == 0
        and adapter.get("d3d12_feature_level") == 0
        and adapter.get("d3d11_feature_level") == 0
        and adapter.get("raytracing_tier") == 0
        and adapter.get("software_adapter") is False
    )
    if decision == "no_hardware_adapter":
        return no_identity
    hardware_identity = (
        isinstance(adapter.get("name"), str)
        and bool(adapter["name"])
        and isinstance(adapter.get("luid"), str)
        and re.fullmatch(r"[0-9a-f]{16}", adapter["luid"]) is not None
        and is_uint32(adapter.get("vendor_id"))
        and adapter["vendor_id"] > 0
        and is_uint32(adapter.get("device_id"))
        and adapter.get("software_adapter") is False
    )
    if decision == "d3d12_unavailable":
        return (
            hardware_identity
            and adapter.get("d3d12_feature_level") == 0
            and adapter.get("raytracing_tier") == 0
        )
    if decision == "dxr_tier_below_1_1":
        return (
            hardware_identity
            and adapter.get("d3d12_feature_level") == 0xC000
            and is_uint32(adapter.get("raytracing_tier"))
            and adapter["raytracing_tier"] < 11
        )
    return False


def validate_report(
    report: dict[str, Any],
    process_exit_code: int,
    ogre_lock: dict[str, Any],
    dxr7_lock: dict[str, Any],
    source_identity: dict[str, Any],
    dxc_closure: dict[str, Any],
    expected_nonce: str,
    frame_path: Path,
) -> None:
    require_exact_keys(
        report,
        {
            "schema",
            "status",
            "reason",
            "execution",
            "scope",
            "provenance",
            "build",
            "adapter",
            "ownership",
            "ray_tracing",
            "ogre_frame",
            "synchronization",
            "lifecycle",
        },
        "DXR7 report",
    )
    execution = require_exact_keys(
        report["execution"], {"challenge_nonce", "probe_binary_marker"},
        "DXR7 execution",
    )
    scope = require_exact_keys(
        report["scope"],
        {
            "external_d3d11on12_foundation",
            "hardware_dxr_pass",
            "native_ray_tracing",
            "acceleration_structure_built",
            "ray_traced_probe_readback",
            "ray_traced_image_produced",
            "ogre_raster_image_produced",
            "hybrid_ogre_image_composite",
            "limitation",
        },
        "DXR7 scope",
    )
    provenance = require_exact_keys(
        report["provenance"],
        {
            "ror_repository",
            "ror_ref",
            "ror_commit",
            "ror_relevant_source_manifest_sha256",
            "ror_relevant_source_manifest_file_count",
            "ogre_next_repository",
            "ogre_next_branch",
            "ogre_next_commit",
            "ogre_next_archive_sha256",
            "ogre_next_license_spdx",
            "ogre_next_license_sha256",
            "dxr7_toolchain_lock_sha256",
            "ogre_adaptation_patch_path",
            "ogre_adaptation_patch_sha256",
            "hlsl_source_sha256",
            "dxc_executable_sha256",
            "dxcompiler_dll_sha256",
            "dxil_dll_sha256",
            "dxc_sdk_version",
            "dxc_version",
            "dxc_path",
            "dxc_x64_directory",
        },
        "DXR7 provenance",
    )
    build = require_exact_keys(
        report["build"],
        {
            "platform_policy",
            "system",
            "processor",
            "compiler_id",
            "compiler_version",
            "ogre_version",
            "pointer_bits",
        },
        "DXR7 build",
    )
    adapter = require_exact_keys(
        report["adapter"],
        {
            "name",
            "luid",
            "vendor_id",
            "device_id",
            "software_adapter",
            "d3d12_feature_level",
            "d3d11_feature_level",
            "raytracing_tier",
            "candidate_decision",
        },
        "DXR7 adapter",
    )
    ownership = require_exact_keys(
        report["ownership"],
        {
            "app_owned_d3d12_device",
            "app_owned_direct_queue",
            "app_owned_fence",
            "d3d11on12_device_created",
            "d3d11on12_created_with_exact_direct_queue",
            "d3d11on12_underlying_d3d12_device_exact",
            "d3d11on12_adapter_luid_exact",
            "ogre_plugin_option",
            "ogre_external_device_option_used",
            "ogre_d3d11_device_exact",
            "ogre_external_device_active",
        },
        "DXR7 ownership",
    )
    ray_tracing = require_exact_keys(
        report["ray_tracing"],
        {
            "blas_built",
            "tlas_built",
            "state_object_created",
            "shader_identifiers_resolved",
            "dispatch_rays_called",
            "dispatch_width",
            "dispatch_height",
            "dispatch_depth",
            "readback_value",
            "closest_hit_readback_exact",
        },
        "DXR7 ray tracing",
    )
    ogre_frame = require_exact_keys(
        report["ogre_frame"],
        {
            "native_hidden_window_created",
            "pbs_material_created",
            "compositor_workspace_created",
            "frame_submitted",
            "gpu_readback_completed",
            "nonblank",
            "ui_included",
            "resources_destroyed_before_ogre_shutdown",
            "workspace_removed",
            "workspace_definition_removed",
            "render_target_destroyed",
            "scene_destroyed",
            "pbs_datablock_destroyed",
            "pbs_hlms_unregistered",
            "native_window_destroyed",
            "root_shutdown_completed",
            "width",
            "height",
            "distinct_rgb8_values",
            "non_background_pixels",
            "rgb8_fnv1a64",
        },
        "DXR7 Ogre frame",
    )
    synchronization = require_exact_keys(
        report["synchronization"],
        {"fence_before_dispatch", "fence_after_dispatch", "fence_after_ogre"},
        "DXR7 synchronization",
    )
    lifecycle = require_exact_keys(
        report["lifecycle"],
        {
            "ogre_shutdown_before_d3d11_release",
            "d3d11_context_flushed_before_release",
            "d3d11_released_before_d3d12_queue",
            "d3d12_queue_released_before_device",
            "shutdown_completed",
        },
        "DXR7 lifecycle",
    )
    status = report.get("status")
    expected_status = {0: "pass", UNSUPPORTED_EXIT_CODE: "unsupported"}.get(
        process_exit_code
    )
    boolean_fields = (
        [
            scope[field]
            for field in (
                "external_d3d11on12_foundation",
                "hardware_dxr_pass",
                "acceleration_structure_built",
                "ray_traced_probe_readback",
                "ray_traced_image_produced",
                "ogre_raster_image_produced",
                "hybrid_ogre_image_composite",
            )
        ]
        + [value for key, value in ownership.items() if key != "ogre_plugin_option"]
        + [
            value
            for key, value in ray_tracing.items()
            if key
            not in {
                "dispatch_width",
                "dispatch_height",
                "dispatch_depth",
                "readback_value",
            }
        ]
        + [
            ogre_frame[field]
            for field in (
                "native_hidden_window_created",
                "pbs_material_created",
                "compositor_workspace_created",
                "frame_submitted",
                "gpu_readback_completed",
                "nonblank",
                "ui_included",
                "resources_destroyed_before_ogre_shutdown",
                "workspace_removed",
                "workspace_definition_removed",
                "render_target_destroyed",
                "scene_destroyed",
                "pbs_datablock_destroyed",
                "pbs_hlms_unregistered",
                "native_window_destroyed",
                "root_shutdown_completed",
            )
        ]
        + list(lifecycle.values())
    )
    patch_lock = dxr7_lock["adaptation_patch"]
    shader_lock = dxr7_lock["shader"]
    components = dxc_closure["components"]
    checks = {
        "schema": report.get("schema") == SCHEMA,
        "status": status == expected_status,
        "status_domain": status in {"pass", "unsupported"},
        "boolean_types": all(type(value) is bool for value in boolean_fields),
        "scope_no_image": scope.get("ray_traced_image_produced") is False,
        "scope_no_hybrid": scope.get("hybrid_ogre_image_composite") is False,
        "scope_limitation": scope.get("limitation") == SCOPE_LIMITATION,
        "ror_repository": provenance.get("ror_repository")
        == source_identity["repository"],
        "ror_ref": provenance.get("ror_ref") == source_identity["ref"],
        "ror_commit": provenance.get("ror_commit")
        == source_identity["commit"],
        "ror_manifest": provenance.get("ror_relevant_source_manifest_sha256")
        == source_identity["relevant_manifest_sha256"],
        "ror_manifest_count": provenance.get(
            "ror_relevant_source_manifest_file_count"
        )
        == source_identity["relevant_manifest_file_count"],
        "ogre_repository": provenance.get("ogre_next_repository")
        == ogre_lock["repository"],
        "ogre_branch": provenance.get("ogre_next_branch")
        == ogre_lock["branch"],
        "ogre_commit": provenance.get("ogre_next_commit")
        == ogre_lock["commit"],
        "ogre_archive": provenance.get("ogre_next_archive_sha256")
        == ogre_lock["archive_sha256"],
        "ogre_license": provenance.get("ogre_next_license_spdx")
        == ogre_lock["license"]["spdx"],
        "ogre_license_hash": provenance.get("ogre_next_license_sha256")
        == ogre_lock["license"]["sha256"],
        "dxr7_lock": provenance.get("dxr7_toolchain_lock_sha256")
        == LOCK_SHA256,
        "patch_path": provenance.get("ogre_adaptation_patch_path")
        == patch_lock["path"],
        "patch_hash": provenance.get("ogre_adaptation_patch_sha256")
        == patch_lock["sha256"],
        "hlsl_hash": provenance.get("hlsl_source_sha256")
        == shader_lock["sha256"],
        "dxc_hash": is_sha256(provenance.get("dxc_executable_sha256")),
        "dxc_exact_hash": provenance.get("dxc_executable_sha256")
        == components["dxc.exe"]["sha256"],
        "dxcompiler_hash": provenance.get("dxcompiler_dll_sha256")
        == components["dxcompiler.dll"]["sha256"],
        "dxil_dll_hash": provenance.get("dxil_dll_sha256")
        == components["dxil.dll"]["sha256"],
        "dxc_sdk_version": provenance.get("dxc_sdk_version")
        == dxc_closure["sdk_version"],
        "dxc_version": provenance.get("dxc_version")
        == dxc_closure["dxc_version"],
        "dxc_path": isinstance(provenance.get("dxc_path"), str)
        and provenance["dxc_path"].replace("\\", "/").rstrip("/").lower()
        == dxc_closure["dxc_path"],
        "dxc_x64_directory": isinstance(
            provenance.get("dxc_x64_directory"), str
        )
        and provenance["dxc_x64_directory"]
        .replace("\\", "/")
        .rstrip("/")
        .lower()
        == dxc_closure["x64_directory"],
        "nonce": execution.get("challenge_nonce") == expected_nonce
        and isinstance(expected_nonce, str)
        and re.fullmatch(r"[0-9a-f]{64}", expected_nonce) is not None,
        "binary_marker": execution.get("probe_binary_marker")
        == expected_binary_marker(source_identity),
        "platform": build.get("platform_policy")
        == "windows-x64-d3d11on12-dxr",
        "system": build.get("system") == "Windows",
        "processor": str(build.get("processor", "")).lower()
        in {"amd64", "x86_64"},
        "compiler": build.get("compiler_id") == "MSVC"
        and isinstance(build.get("compiler_version"), str)
        and bool(build["compiler_version"]),
        "ogre_version": build.get("ogre_version") == "3.0.0",
        "pointer_bits": build.get("pointer_bits") == 64,
        "adapter_types": isinstance(adapter.get("name"), str)
        and isinstance(adapter.get("luid"), str)
        and is_uint32(adapter.get("vendor_id"))
        and is_uint32(adapter.get("device_id"))
        and type(adapter.get("software_adapter")) is bool
        and is_uint32(adapter.get("d3d12_feature_level"))
        and is_uint32(adapter.get("d3d11_feature_level"))
        and is_uint32(adapter.get("raytracing_tier")),
        "decision_domain": adapter.get("candidate_decision")
        in {
            "accept",
            "no_hardware_adapter",
            "d3d12_unavailable",
            "dxr_tier_below_1_1",
        },
        "plugin_option": ownership.get("ogre_plugin_option")
        == "external_device",
        "dispatch_integer_types": all(
            is_uint32(ray_tracing.get(field))
            for field in (
                "dispatch_width",
                "dispatch_height",
                "dispatch_depth",
                "readback_value",
            )
        ),
        "fence_integer_types": all(
            is_uint64(synchronization.get(field))
            for field in (
                "fence_before_dispatch",
                "fence_after_dispatch",
                "fence_after_ogre",
            )
        ),
        "shutdown": lifecycle.get("shutdown_completed") is True,
        "frame_integer_types": all(
            is_uint32(ogre_frame.get(field))
            for field in (
                "width",
                "height",
                "distinct_rgb8_values",
                "non_background_pixels",
            )
        ),
        "frame_hash_type": isinstance(ogre_frame.get("rgb8_fnv1a64"), str)
        and re.fullmatch(r"[0-9a-f]{16}", ogre_frame["rgb8_fnv1a64"])
        is not None,
    }
    if status == "pass":
        frame_metrics = validate_ogre_frame_ppm(frame_path)
        checks.update(
            {
                "no_reason": report.get("reason") == "",
                "scope_pass": scope.get("external_d3d11on12_foundation") is True
                and scope.get("hardware_dxr_pass") is True
                and scope.get("native_ray_tracing") == "dispatch_rays"
                and scope.get("acceleration_structure_built") is True
                and scope.get("ray_traced_probe_readback") is True
                and scope.get("ogre_raster_image_produced") is True,
                "hardware_identity": bool(adapter.get("name"))
                and re.fullmatch(r"[0-9a-f]{16}", adapter.get("luid", ""))
                is not None
                and adapter.get("vendor_id", 0) > 0
                and adapter.get("software_adapter") is False,
                "feature_levels": adapter.get("d3d12_feature_level") == 0xC000
                and adapter.get("d3d11_feature_level") in {0xB000, 0xB100},
                "tier": adapter.get("raytracing_tier") == 11,
                "accepted": adapter.get("candidate_decision") == "accept",
                "ownership": all(
                    ownership.get(field) is True
                    for field in ownership
                    if field != "ogre_plugin_option"
                ),
                "real_dispatch": all(
                    ray_tracing.get(field) is True
                    for field in (
                        "blas_built",
                        "tlas_built",
                        "state_object_created",
                        "shader_identifiers_resolved",
                        "dispatch_rays_called",
                        "closest_hit_readback_exact",
                    )
                ),
                "dispatch_dimensions": [
                    ray_tracing.get("dispatch_width"),
                    ray_tracing.get("dispatch_height"),
                    ray_tracing.get("dispatch_depth"),
                ]
                == dxr7_lock["runtime"]["required_dispatch_dimensions"],
                "readback": ray_tracing.get("readback_value")
                == dxr7_lock["runtime"]["closest_hit_readback"],
                "fence_sequence": exact_json_equal(
                    synchronization,
                    {
                        "fence_before_dispatch": 1,
                        "fence_after_dispatch": 2,
                        "fence_after_ogre": 3,
                    },
                ),
                "lifecycle": all(value is True for value in lifecycle.values()),
                "ogre_frame_flags": all(
                    ogre_frame.get(field) is True
                    for field in (
                        "native_hidden_window_created",
                        "pbs_material_created",
                        "compositor_workspace_created",
                        "frame_submitted",
                        "gpu_readback_completed",
                        "nonblank",
                        "resources_destroyed_before_ogre_shutdown",
                        "workspace_removed",
                        "workspace_definition_removed",
                        "render_target_destroyed",
                        "scene_destroyed",
                        "pbs_datablock_destroyed",
                        "pbs_hlms_unregistered",
                        "native_window_destroyed",
                        "root_shutdown_completed",
                    )
                )
                and ogre_frame.get("ui_included") is False,
                "ogre_frame_metrics": exact_json_equal(
                    {key: ogre_frame.get(key) for key in frame_metrics},
                    frame_metrics,
                ),
            }
        )
    else:
        if frame_path.exists() or frame_path.is_symlink():
            checks["unsupported_no_frame_artifact"] = False
        checks.update(
            {
                "explicit_reason": isinstance(report.get("reason"), str)
                and bool(report["reason"]),
                "reason_consistent": unsupported_reason_consistent(
                    report.get("reason"), adapter, ownership
                ),
                "scope_unsupported": scope.get(
                    "external_d3d11on12_foundation"
                )
                is False
                and scope.get("hardware_dxr_pass") is False
                and scope.get("native_ray_tracing") == "unsupported"
                and scope.get("acceleration_structure_built") is False
                and scope.get("ray_traced_probe_readback") is False
                and scope.get("ogre_raster_image_produced") is False,
                "no_ownership_claim": all(
                    ownership.get(field) is False
                    for field in ownership
                    if field != "ogre_plugin_option"
                ),
                "no_dispatch_claim": all(
                    ray_tracing.get(field) is False
                    for field in (
                        "blas_built",
                        "tlas_built",
                        "state_object_created",
                        "shader_identifiers_resolved",
                        "dispatch_rays_called",
                        "closest_hit_readback_exact",
                    )
                )
                and all(
                    ray_tracing.get(field) == 0
                    for field in (
                        "dispatch_width",
                        "dispatch_height",
                        "dispatch_depth",
                        "readback_value",
                    )
                ),
                "no_fence_claim": all(value == 0 for value in synchronization.values()),
                "only_shutdown_claim": exact_json_equal(
                    lifecycle,
                    {
                        "ogre_shutdown_before_d3d11_release": False,
                        "d3d11_context_flushed_before_release": False,
                        "d3d11_released_before_d3d12_queue": False,
                        "d3d12_queue_released_before_device": False,
                        "shutdown_completed": True,
                    },
                ),
                "no_ogre_frame_claim": exact_json_equal(
                    ogre_frame,
                    {
                        "native_hidden_window_created": False,
                        "pbs_material_created": False,
                        "compositor_workspace_created": False,
                        "frame_submitted": False,
                        "gpu_readback_completed": False,
                        "nonblank": False,
                        "ui_included": False,
                        "resources_destroyed_before_ogre_shutdown": False,
                        "workspace_removed": False,
                        "workspace_definition_removed": False,
                        "render_target_destroyed": False,
                        "scene_destroyed": False,
                        "pbs_datablock_destroyed": False,
                        "pbs_hlms_unregistered": False,
                        "native_window_destroyed": False,
                        "root_shutdown_completed": False,
                        "width": 0,
                        "height": 0,
                        "distinct_rgb8_values": 0,
                        "non_background_pixels": 0,
                        "rgb8_fnv1a64": "0000000000000000",
                    },
                ),
            }
        )
    failed = sorted(name for name, passed in checks.items() if not passed)
    if failed:
        raise Dxr7Error("DXR7 report failed closed: " + ", ".join(failed))


def optional_frame_record(frame_path: Path, passed: bool) -> dict[str, Any]:
    if passed:
        record = artifact_record(frame_path, OGRE_FRAME_NAME)
        return {"present": True, **record}
    if frame_path.exists() or frame_path.is_symlink():
        raise Dxr7Error("unsupported DXR7 evidence unexpectedly contains a frame")
    return {
        "present": False,
        "path": OGRE_FRAME_NAME,
        "bytes": 0,
        "sha256": "",
    }


def github_ci_context(require: bool) -> dict[str, Any]:
    if os.environ.get("GITHUB_ACTIONS") != "true":
        if require:
            raise Dxr7Error("GitHub Actions execution context is required")
        return {
            "provider": "local",
            "repository": "",
            "workflow_ref": "",
            "run_id": "",
            "run_attempt": "",
            "sha": "",
            "ref": "",
            "job": "",
            "external_dsse_required": True,
        }
    values = {
        "repository": os.environ.get("GITHUB_REPOSITORY", ""),
        "workflow_ref": os.environ.get("GITHUB_WORKFLOW_REF", ""),
        "run_id": os.environ.get("GITHUB_RUN_ID", ""),
        "run_attempt": os.environ.get("GITHUB_RUN_ATTEMPT", ""),
        "sha": os.environ.get("GITHUB_SHA", ""),
        "ref": os.environ.get("GITHUB_REF", ""),
        "job": os.environ.get("GITHUB_JOB", ""),
    }
    if any(not value for value in values.values()):
        raise Dxr7Error("GitHub Actions execution identity is incomplete")
    if re.fullmatch(r"[0-9a-f]{40}", values["sha"]) is None:
        raise Dxr7Error("GitHub Actions SHA is not canonical")
    return {
        "provider": "github-actions",
        **values,
        "external_dsse_required": True,
    }


def make_execution_receipt(
    report_path: Path,
    executable: Path,
    dxil: Path,
    frame_path: Path,
    report: dict[str, Any],
    observed_process_exit_code: int,
    source_identity: dict[str, Any],
    build_context: dict[str, Any],
    dxc_closure: dict[str, Any],
    require_ci_context: bool,
) -> dict[str, Any]:
    passed = report.get("status") == "pass"
    return {
        "schema": EXECUTION_RECEIPT_SCHEMA,
        "status": report["status"],
        "observation": {
            "mode": "fresh_child_process_challenge",
            "challenge_nonce": report["execution"]["challenge_nonce"],
            "observed_process_exit_code": observed_process_exit_code,
            "offline_cryptographic_execution_proof": False,
            "limitation": OFFLINE_EXECUTION_LIMITATION,
        },
        "subjects": {
            "report": artifact_record(report_path, REPORT_NAME),
            "executable": artifact_record(executable, executable.name),
            "dxil": artifact_record(dxil, DXIL_RELATIVE),
            "ogre_frame": optional_frame_record(frame_path, passed),
        },
        "build_context": build_context,
        "toolchain": dxc_closure,
        "ror_source": source_identity,
        "ci": github_ci_context(require_ci_context),
        "complete": True,
    }


def validate_execution_receipt(
    receipt: dict[str, Any],
    report_path: Path,
    executable: Path,
    dxil: Path,
    frame_path: Path,
    report: dict[str, Any],
    source_identity: dict[str, Any],
    build_context: dict[str, Any],
    dxc_closure: dict[str, Any],
) -> None:
    require_exact_keys(
        receipt,
        {
            "schema",
            "status",
            "observation",
            "subjects",
            "build_context",
            "toolchain",
            "ror_source",
            "ci",
            "complete",
        },
        "DXR7 execution receipt",
    )
    observation = require_exact_keys(
        receipt["observation"],
        {
            "mode",
            "challenge_nonce",
            "observed_process_exit_code",
            "offline_cryptographic_execution_proof",
            "limitation",
        },
        "DXR7 execution observation",
    )
    subjects = require_exact_keys(
        receipt["subjects"],
        {"report", "executable", "dxil", "ogre_frame"},
        "DXR7 execution subjects",
    )
    ci = require_exact_keys(
        receipt["ci"],
        {
            "provider",
            "repository",
            "workflow_ref",
            "run_id",
            "run_attempt",
            "sha",
            "ref",
            "job",
            "external_dsse_required",
        },
        "DXR7 CI receipt identity",
    )
    passed = report.get("status") == "pass"
    observed_exit = observation.get("observed_process_exit_code")
    expected_exit = 0 if passed else UNSUPPORTED_EXIT_CODE
    checks = {
        "schema": receipt.get("schema") == EXECUTION_RECEIPT_SCHEMA,
        "status": receipt.get("status") == report.get("status"),
        "mode": observation.get("mode") == "fresh_child_process_challenge",
        "nonce": observation.get("challenge_nonce")
        == report["execution"]["challenge_nonce"],
        "exit_type": type(observed_exit) is int,
        "exit": observed_exit == expected_exit,
        "offline_limit": observation.get(
            "offline_cryptographic_execution_proof"
        )
        is False
        and observation.get("limitation") == OFFLINE_EXECUTION_LIMITATION,
        "report": exact_json_equal(
            subjects.get("report"), artifact_record(report_path, REPORT_NAME)
        ),
        "executable": exact_json_equal(
            subjects.get("executable"),
            artifact_record(executable, executable.name),
        ),
        "dxil": exact_json_equal(
            subjects.get("dxil"), artifact_record(dxil, DXIL_RELATIVE)
        ),
        "frame": exact_json_equal(
            subjects.get("ogre_frame"),
            optional_frame_record(frame_path, passed),
        ),
        "build_context": exact_json_equal(
            receipt.get("build_context"), build_context
        ),
        "toolchain": exact_json_equal(
            receipt.get("toolchain"), dxc_closure
        ),
        "source": exact_json_equal(
            receipt.get("ror_source"), source_identity
        ),
        "ci_provider": ci.get("provider") in {"local", "github-actions"},
        "ci_dsse": ci.get("external_dsse_required") is True,
        "complete": receipt.get("complete") is True,
    }
    if ci.get("provider") == "github-actions":
        checks["ci_identity"] = (
            ci.get("repository") == TRUSTED_REPOSITORY
            and isinstance(ci.get("workflow_ref"), str)
            and ci["workflow_ref"]
            == (
                TRUSTED_REPOSITORY
                + "/"
                + TRUSTED_WORKFLOW_PATH
                + "@"
                + str(ci.get("ref", ""))
            )
            and isinstance(ci.get("run_id"), str)
            and ci["run_id"].isdigit()
            and isinstance(ci.get("run_attempt"), str)
            and ci["run_attempt"].isdigit()
            and ci.get("sha") == source_identity["commit"]
            and isinstance(ci.get("ref"), str)
            and re.fullmatch(
                r"refs/(?:heads|tags)/[A-Za-z0-9._/-]+|"
                r"refs/pull/[0-9]+/merge",
                ci["ref"],
            )
            is not None
            and isinstance(ci.get("job"), str)
            and bool(ci["job"])
        )
    else:
        checks["local_identity_empty"] = all(
            ci.get(field) == ""
            for field in (
                "repository",
                "workflow_ref",
                "run_id",
                "run_attempt",
                "sha",
                "ref",
                "job",
            )
        )
    failed = sorted(name for name, ok in checks.items() if not ok)
    if failed:
        raise Dxr7Error(
            "DXR7 execution receipt failed closed: " + ", ".join(failed)
        )


def make_attestation(
    report_path: Path,
    executable: Path,
    dxil: Path,
    frame_path: Path,
    receipt_path: Path,
    report: dict[str, Any],
    observed_process_exit_code: int,
    source_identity: dict[str, Any],
    build_context: dict[str, Any],
    dxc_closure: dict[str, Any],
    pe_semantics: dict[str, Any],
    dxil_semantics: dict[str, Any],
) -> dict[str, Any]:
    passed = report.get("status") == "pass"
    return {
        "schema": ATTESTATION_SCHEMA,
        "status": report["status"],
        "execution": {
            "challenge_nonce": report["execution"]["challenge_nonce"],
            "observed_process_exit_code": observed_process_exit_code,
            "observation": "fresh_child_process_challenge",
            "offline_artifact_proves_execution": False,
            "cryptographic_ci_receipt": "external_github_dsse_required",
            "limitation": OFFLINE_EXECUTION_LIMITATION,
        },
        "files": {
            "report": artifact_record(report_path, REPORT_NAME),
            "executable": artifact_record(executable, executable.name),
            "dxil": artifact_record(dxil, DXIL_RELATIVE),
            "ogre_frame": optional_frame_record(frame_path, passed),
            "execution_receipt": artifact_record(
                receipt_path, EXECUTION_RECEIPT_NAME
            ),
        },
        "binary_semantics": {
            "pe": pe_semantics,
            "dxil": dxil_semantics,
        },
        "build_context": build_context,
        "toolchain": dxc_closure,
        "ror_source": source_identity,
        "claims": {
            "report_declares_external_d3d11on12_foundation": passed,
            "report_declares_hardware_dxr_pass": passed,
            "report_declares_dispatch_rays": passed,
            "report_declares_ui_free_ogre_raster_frame": passed,
            "software_adapter_rt_pass": False,
            "hybrid_ray_raster_composite": False,
        },
        "complete": True,
    }


def validate_attestation(
    attestation: dict[str, Any],
    report_path: Path,
    executable: Path,
    dxil: Path,
    frame_path: Path,
    receipt_path: Path,
    report: dict[str, Any],
    source_identity: dict[str, Any],
    build_context: dict[str, Any],
    dxc_closure: dict[str, Any],
    pe_semantics: dict[str, Any],
    dxil_semantics: dict[str, Any],
) -> int:
    require_exact_keys(
        attestation,
        {
            "schema",
            "status",
            "execution",
            "files",
            "binary_semantics",
            "build_context",
            "toolchain",
            "ror_source",
            "claims",
            "complete",
        },
        "DXR7 attestation",
    )
    execution = require_exact_keys(
        attestation["execution"],
        {
            "challenge_nonce",
            "observed_process_exit_code",
            "observation",
            "offline_artifact_proves_execution",
            "cryptographic_ci_receipt",
            "limitation",
        },
        "DXR7 attested execution",
    )
    files = require_exact_keys(
        attestation["files"],
        {"report", "executable", "dxil", "ogre_frame", "execution_receipt"},
        "DXR7 attested files",
    )
    semantics = require_exact_keys(
        attestation["binary_semantics"], {"pe", "dxil"},
        "DXR7 attested binary semantics",
    )
    claims = require_exact_keys(
        attestation["claims"],
        {
            "report_declares_external_d3d11on12_foundation",
            "report_declares_hardware_dxr_pass",
            "report_declares_dispatch_rays",
            "report_declares_ui_free_ogre_raster_frame",
            "software_adapter_rt_pass",
            "hybrid_ray_raster_composite",
        },
        "DXR7 attested claims",
    )
    passed = report.get("status") == "pass"
    observed_exit = execution.get("observed_process_exit_code")
    expected_exit = 0 if passed else UNSUPPORTED_EXIT_CODE
    expected_claims = {
        "report_declares_external_d3d11on12_foundation": passed,
        "report_declares_hardware_dxr_pass": passed,
        "report_declares_dispatch_rays": passed,
        "report_declares_ui_free_ogre_raster_frame": passed,
        "software_adapter_rt_pass": False,
        "hybrid_ray_raster_composite": False,
    }
    checks = {
        "schema": attestation.get("schema") == ATTESTATION_SCHEMA,
        "status": attestation.get("status") == report.get("status"),
        "exit_type": type(observed_exit) is int,
        "exit": observed_exit == expected_exit,
        "nonce": execution.get("challenge_nonce")
        == report["execution"]["challenge_nonce"],
        "observation": execution.get("observation")
        == "fresh_child_process_challenge",
        "offline_scope": execution.get("offline_artifact_proves_execution")
        is False
        and execution.get("cryptographic_ci_receipt")
        == "external_github_dsse_required"
        and execution.get("limitation") == OFFLINE_EXECUTION_LIMITATION,
        "report": exact_json_equal(
            files.get("report"), artifact_record(report_path, REPORT_NAME)
        ),
        "executable": exact_json_equal(
            files.get("executable"),
            artifact_record(executable, executable.name),
        ),
        "dxil": exact_json_equal(
            files.get("dxil"), artifact_record(dxil, DXIL_RELATIVE)
        ),
        "frame": exact_json_equal(
            files.get("ogre_frame"),
            optional_frame_record(frame_path, passed),
        ),
        "receipt": exact_json_equal(
            files.get("execution_receipt"),
            artifact_record(receipt_path, EXECUTION_RECEIPT_NAME),
        ),
        "pe_semantics": exact_json_equal(semantics.get("pe"), pe_semantics),
        "dxil_semantics": exact_json_equal(
            semantics.get("dxil"), dxil_semantics
        ),
        "build_context": exact_json_equal(
            attestation.get("build_context"), build_context
        ),
        "toolchain": exact_json_equal(
            attestation.get("toolchain"), dxc_closure
        ),
        "ror_source": exact_json_equal(
            attestation.get("ror_source"), source_identity
        ),
        "claims": exact_json_equal(claims, expected_claims),
        "complete": attestation.get("complete") is True,
    }
    failed = sorted(name for name, ok in checks.items() if not ok)
    if failed:
        raise Dxr7Error("DXR7 attestation failed closed: " + ", ".join(failed))
    return observed_exit


def validate_static_contract() -> None:
    bootstrap = (
        REPOSITORY_ROOT
        / "source/main/gfx/render/ogrenext/OgreNextD3D12DxrBootstrap.cpp"
    ).read_text(encoding="utf-8")
    patch = (
        PROBE_SOURCE / "patches/0004-d3d11-adopt-external-device.patch"
    ).read_text(encoding="utf-8")
    smoke = (PROBE_SOURCE / "src/windows_dxr7_smoke.cpp").read_text(
        encoding="utf-8"
    )
    cmake = (PROBE_SOURCE / "CMakeLists.txt").read_text(encoding="utf-8")
    for token in (
        "EnumAdapterByGpuPreference",
        "D3D12_RAYTRACING_TIER_1_1",
        "D3D11On12CreateDevice",
        "GetD3D12Device",
        "BuildRaytracingAccelerationStructure",
        "CreateStateObject",
        "GetShaderIdentifier",
        "DispatchRays",
        "closest_hit_readback_exact",
        "WaitForFence(1U)",
        "WaitForFence(2U)",
        "WaitForFence(3U)",
        "Dxr7FenceCompletionDecision::DEVICE_REMOVED",
        "ID3D12Device::GetDeviceRemovedReason",
        "RecordOgreFrameProof",
    ):
        if token not in bootstrap:
            raise Dxr7Error(f"DXR7 bootstrap contract token is missing: {token}")
    for token in (
        'options->find( "external_device" )',
        "mExternalDevice",
        "adoptExternalDevice",
        "D3D11_EXTERNAL_DEVICE_ACTIVE",
        "owner recreation is required",
        "External and renderer-owned devices deliberately converge here",
        "GetDriverVersion",
        "selectDepthBufferFormat",
    ):
        if token not in patch:
            raise Dxr7Error(f"DXR7 Ogre patch token is missing: {token}")
    for token in (
        'plugin_options["external_device"]',
        "ValidateDxr7PassContract",
        "kUnsupportedExitCode = 77",
        'MakeReport("pass"',
        "createRenderWindow",
        "renderOneFrame",
        "convertFromTexture",
        "WritePpmAtomically",
        "MoveFileExW",
        "removeWorkspaceDefinition",
        "destroyDatablock",
        "unregisterHlms",
        "destroyRenderWindow",
        "ROOT_SHUTDOWN_COMPLETED",
    ):
        if token not in smoke:
            raise Dxr7Error(f"DXR7 smoke contract token is missing: {token}")
    for token in (
        "ROR_OGRE_NEXT_WINDOWS_DXR7",
        "ror_ogre_next_windows_dxr7_shader",
        "ror_ogre_next_windows_dxr7_smoke",
        "dxcompiler.dll",
        "dxil.dll",
        "ROR_OGRE_NEXT_DXC_SDK_BIN_ROOT",
        "Program Files (x86)/Windows Kits/10/bin",
        "ror_ogre_next_windows_dxr7_runtime_repeat",
        "SKIP_RETURN_CODE 77",
    ):
        if token not in cmake:
            raise Dxr7Error(f"DXR7 CMake contract token is missing: {token}")


def requested_dxc_closure(args: argparse.Namespace) -> dict[str, Any]:
    dxc_value = args.dxc or (
        Path(os.environ["ROR_OGRE_NEXT_DXR7_DXC"])
        if os.environ.get("ROR_OGRE_NEXT_DXR7_DXC")
        else None
    )
    sdk_root_value = args.windows_sdk_bin_root or (
        Path(os.environ["ROR_OGRE_NEXT_DXR7_SDK_BIN_ROOT"])
        if os.environ.get("ROR_OGRE_NEXT_DXR7_SDK_BIN_ROOT")
        else None
    )
    if dxc_value is None or sdk_root_value is None:
        raise Dxr7Error(
            "DXR7 configuration requires --dxc and --windows-sdk-bin-root; "
            "arbitrary PATH discovery is prohibited"
        )
    return validate_dxc_closure(
        dxc_value.expanduser(), sdk_root_value.expanduser(),
        execute_version=True,
    )


def configure_build(
    build_dir: Path,
    generator: str | None,
    dxc_closure: dict[str, Any],
) -> None:
    command = [
        "cmake",
        "-S",
        str(PROBE_SOURCE),
        "-B",
        str(build_dir),
        "-DROR_OGRE_NEXT_PROBE=ON",
        "-DROR_OGRE_NEXT_WINDOWS_DXR7=ON",
        f"-DCMAKE_BUILD_TYPE={REQUIRED_CONFIG}",
    ]
    command.extend(
        [
            "-DROR_OGRE_NEXT_DXC_EXECUTABLE=" + dxc_closure["dxc_path"],
            "-DROR_OGRE_NEXT_DXC_SDK_BIN_ROOT="
            + dxc_closure["sdk_bin_root"],
        ]
    )
    if generator:
        command.extend(["-G", generator])
    elif shutil.which("ninja"):
        command.extend(["-G", "Ninja"])
    MAIN_RUNNER.run(command)


def run_proof(args: argparse.Namespace) -> dict[str, Any]:
    if platform.system().lower() != "windows" or platform.machine().lower() not in {
        "amd64",
        "x86_64",
    }:
        raise Dxr7Error("native DXR7 execution is reviewed only for Windows x64")
    ogre_lock = MAIN_RUNNER.load_lock()
    dxr7_lock = load_dxr7_lock()
    MAIN_RUNNER.require_relevant_source_clean()
    source_identity = MAIN_RUNNER.ror_source_identity()
    build_dir = MAIN_RUNNER.prepare_build_dir(
        args.build_dir, args.clean_build_dir, args.reuse_build_dir
    )
    if not args.reuse_build_dir:
        configure_build(
            build_dir, args.generator, requested_dxc_closure(args)
        )
    dxc_closure = configured_dxc_closure(build_dir)
    validate_build_context(build_dir, ogre_lock, source_identity)
    MAIN_RUNNER.run(
        [
            "cmake",
            "--build",
            str(build_dir),
            "--target",
            "ror_ogre_next_frontend_n1_package",
            "ror_ogre_next_windows_dxr7_smoke",
            "--config",
            REQUIRED_CONFIG,
            "--parallel",
            str(args.jobs),
        ]
    )
    MAIN_RUNNER.require_source_identity_unchanged(source_identity)
    require_dxc_closure_unchanged(build_dir, dxc_closure)

    report_path = build_dir / REPORT_NAME
    attestation_path = build_dir / ATTESTATION_NAME
    receipt_path = build_dir / EXECUTION_RECEIPT_NAME
    frame_path = build_dir / OGRE_FRAME_NAME
    for stale in (report_path, attestation_path, receipt_path, frame_path):
        stale.unlink(missing_ok=True)
    executable = executable_path(build_dir)
    dxil = build_dir / DXIL_RELATIVE
    marker = expected_binary_marker(source_identity)
    pe_semantics = validate_pe_executable(executable, marker)
    dxil_semantics = validate_dxil_container(
        dxil, dxr7_lock["shader"]["entry_exports"]
    )
    executable_before = artifact_record(executable, executable.name)
    dxil_before = artifact_record(dxil, DXIL_RELATIVE)
    execution_nonce = secrets.token_hex(32)
    media_root = (
        build_dir
        / MAIN_RUNNER.N1_PACKAGE_NAME
        / "share/rigsofrods/ogre-next/Samples/Media"
    )
    require_direct_file(
        media_root / "Hlms/Pbs/Any/Main/800.PixelShader_piece_ps.any",
        "packaged Ogre-Next PBS media",
    )
    try:
        result = subprocess.run(
            [
                str(executable),
                "--report",
                str(report_path),
                "--shader",
                str(dxil),
                "--image",
                str(frame_path),
                "--media-root",
                str(media_root),
                "--execution-nonce",
                execution_nonce,
            ],
            check=False,
        )
    except OSError as error:
        raise Dxr7Error(f"could not execute DXR7 proof: {error}") from error
    if result.returncode not in (0, UNSUPPORTED_EXIT_CODE):
        raise Dxr7Error(f"DXR7 proof failed with exit code {result.returncode}")
    if (
        artifact_record(executable, executable.name) != executable_before
        or artifact_record(dxil, DXIL_RELATIVE) != dxil_before
    ):
        raise Dxr7Error("DXR7 executable or shader changed during execution")
    require_dxc_closure_unchanged(build_dir, dxc_closure)
    if not report_path.is_file():
        raise Dxr7Error("DXR7 proof did not publish its report")
    report = read_json(report_path, "DXR7 report")
    validate_report(
        report,
        result.returncode,
        ogre_lock,
        dxr7_lock,
        source_identity,
        dxc_closure,
        execution_nonce,
        frame_path,
    )
    MAIN_RUNNER.require_source_identity_unchanged(source_identity)
    build_context = validate_build_context(
        build_dir, ogre_lock, source_identity
    )
    receipt = make_execution_receipt(
        report_path,
        executable,
        dxil,
        frame_path,
        report,
        result.returncode,
        source_identity,
        build_context,
        dxc_closure,
        args.require_ci_context,
    )
    write_json_atomically(receipt_path, receipt)
    validate_execution_receipt(
        receipt,
        report_path,
        executable,
        dxil,
        frame_path,
        report,
        source_identity,
        build_context,
        dxc_closure,
    )
    attestation = make_attestation(
        report_path,
        executable,
        dxil,
        frame_path,
        receipt_path,
        report,
        result.returncode,
        source_identity,
        build_context,
        dxc_closure,
        pe_semantics,
        dxil_semantics,
    )
    write_json_atomically(attestation_path, attestation)
    validate_attestation(
        attestation,
        report_path,
        executable,
        dxil,
        frame_path,
        receipt_path,
        report,
        source_identity,
        build_context,
        dxc_closure,
        pe_semantics,
        dxil_semantics,
    )
    require_dxc_closure_unchanged(build_dir, dxc_closure)
    MAIN_RUNNER.require_source_identity_unchanged(source_identity)
    return report


def validate_trusted_dsse_bundle(
    bundle_path: Path,
    receipt_path: Path,
    receipt: dict[str, Any],
    source_identity: dict[str, Any],
    expected_source_ref: str,
) -> dict[str, Any]:
    require_direct_file(bundle_path, "GitHub Sigstore DSSE bundle")
    if not isinstance(expected_source_ref, str) or re.fullmatch(
        r"refs/(?:heads|tags)/[A-Za-z0-9._/-]+|refs/pull/[0-9]+/merge",
        expected_source_ref,
    ) is None:
        raise Dxr7Error(
            "trusted verification requires an exact GitHub source ref"
        )
    ci = receipt.get("ci")
    if not isinstance(ci, dict):
        raise Dxr7Error("trusted verification requires GitHub CI identity")
    expected_workflow_ref = (
        TRUSTED_SIGNER_WORKFLOW + "@" + expected_source_ref
    )
    if not (
        ci.get("provider") == "github-actions"
        and ci.get("repository") == TRUSTED_REPOSITORY
        and ci.get("workflow_ref") == expected_workflow_ref
        and ci.get("ref") == expected_source_ref
        and ci.get("sha") == source_identity["commit"]
    ):
        raise Dxr7Error(
            "trusted verification CI repository/workflow/ref/commit binding changed"
        )

    try:
        bundle_documents = [
            json.loads(line)
            for line in bundle_path.read_text(encoding="utf-8").splitlines()
            if line.strip()
        ]
    except (OSError, json.JSONDecodeError) as error:
        raise Dxr7Error(
            f"could not parse GitHub Sigstore DSSE bundle: {error}"
        ) from error
    if not bundle_documents or not all(
        isinstance(document, dict) for document in bundle_documents
    ):
        raise Dxr7Error("GitHub Sigstore DSSE bundle is empty or malformed")

    gh = shutil.which("gh")
    if gh is None:
        raise Dxr7Error(
            "gh is required to cryptographically verify the DSSE bundle"
        )
    command = [
        gh,
        "attestation",
        "verify",
        str(receipt_path),
        "--repo",
        TRUSTED_REPOSITORY,
        "--bundle",
        str(bundle_path),
        "--signer-workflow",
        TRUSTED_SIGNER_WORKFLOW,
        "--source-ref",
        expected_source_ref,
        "--source-digest",
        source_identity["commit"],
        "--deny-self-hosted-runners",
        "--format",
        "json",
    ]
    try:
        result = subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
            timeout=120,
        )
    except (OSError, subprocess.SubprocessError) as error:
        raise Dxr7Error(
            f"could not execute GitHub attestation verification: {error}"
        ) from error
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip() or "no detail"
        raise Dxr7Error(f"GitHub Sigstore DSSE verification failed: {detail}")
    try:
        verification = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise Dxr7Error(
            "GitHub attestation verification returned invalid JSON"
        ) from error
    if not isinstance(verification, list) or not verification or not all(
        isinstance(item, dict) for item in verification
    ):
        raise Dxr7Error(
            "GitHub attestation verification returned no attestations"
        )
    return {
        "present": True,
        **artifact_record(bundle_path, DSSE_BUNDLE_NAME),
        "verified_attestations": len(verification),
    }


def verify_existing(
    build_dir: Path,
    *,
    integrity_only: bool = False,
    trusted_attestation_bundle: Path | None = None,
    expected_source_ref: str | None = None,
) -> dict[str, Any]:
    if integrity_only and trusted_attestation_bundle is not None:
        raise Dxr7Error(
            "integrity-only and trusted-attestation-bundle verification conflict"
        )
    if (trusted_attestation_bundle is None) != (expected_source_ref is None):
        raise Dxr7Error(
            "trusted verification requires both the DSSE bundle and exact source ref"
        )
    if not integrity_only and trusted_attestation_bundle is None:
        raise Dxr7Error(
            "verification requires either --integrity-only or a trusted "
            "GitHub Sigstore DSSE bundle"
        )
    MAIN_RUNNER.require_relevant_source_clean()
    source_identity = MAIN_RUNNER.ror_source_identity()
    ogre_lock = MAIN_RUNNER.load_lock()
    dxr7_lock = load_dxr7_lock()
    build_context = validate_build_context(
        build_dir, ogre_lock, source_identity
    )
    report_path = build_dir / REPORT_NAME
    attestation_path = build_dir / ATTESTATION_NAME
    receipt_path = build_dir / EXECUTION_RECEIPT_NAME
    frame_path = build_dir / OGRE_FRAME_NAME
    executable = executable_path(build_dir)
    dxil = build_dir / DXIL_RELATIVE
    pe_semantics = validate_pe_executable(
        executable, expected_binary_marker(source_identity)
    )
    dxil_semantics = validate_dxil_container(
        dxil, dxr7_lock["shader"]["entry_exports"]
    )
    report = read_json(report_path, "DXR7 report")
    attestation = read_json(attestation_path, "DXR7 attestation")
    receipt = read_json(receipt_path, "DXR7 execution receipt")
    dxc_closure = recorded_dxc_closure(
        build_dir, receipt.get("toolchain")
    )
    execution = attestation.get("execution")
    if not isinstance(execution, dict):
        raise Dxr7Error("DXR7 attestation has no execution observation")
    observed_exit_code = execution.get("observed_process_exit_code")
    nonce = execution.get("challenge_nonce")
    if type(observed_exit_code) is not int or not isinstance(nonce, str):
        raise Dxr7Error("DXR7 observed process exit or nonce is invalid")
    validate_report(
        report,
        observed_exit_code,
        ogre_lock,
        dxr7_lock,
        source_identity,
        dxc_closure,
        nonce,
        frame_path,
    )
    validate_execution_receipt(
        receipt,
        report_path,
        executable,
        dxil,
        frame_path,
        report,
        source_identity,
        build_context,
        dxc_closure,
    )
    validate_attestation(
        attestation,
        report_path,
        executable,
        dxil,
        frame_path,
        receipt_path,
        report,
        source_identity,
        build_context,
        dxc_closure,
        pe_semantics,
        dxil_semantics,
    )
    passed = report.get("status") == "pass"
    if trusted_attestation_bundle is not None:
        dsse = validate_trusted_dsse_bundle(
            trusted_attestation_bundle,
            receipt_path,
            receipt,
            source_identity,
            expected_source_ref or "",
        )
        verification_scope = "github_sigstore_dsse"
        trusted_hardware_pass = passed
        signer = {
            "present": True,
            "repository": TRUSTED_REPOSITORY,
            "workflow": TRUSTED_SIGNER_WORKFLOW,
            "source_ref": expected_source_ref,
            "source_digest": source_identity["commit"],
        }
    else:
        dsse = {
            "present": False,
            "path": DSSE_BUNDLE_NAME,
            "bytes": 0,
            "sha256": "",
            "verified_attestations": 0,
        }
        verification_scope = "integrity_only"
        trusted_hardware_pass = False
        signer = {
            "present": False,
            "repository": "",
            "workflow": "",
            "source_ref": "",
            "source_digest": "",
        }
    if not exact_json_equal(
        recorded_dxc_closure(build_dir, receipt.get("toolchain")),
        dxc_closure,
    ):
        raise Dxr7Error("recorded DXC closure changed during verification")
    MAIN_RUNNER.require_source_identity_unchanged(source_identity)
    return {
        "schema": VERIFICATION_SCHEMA,
        "artifact_status": report["status"],
        "verification_scope": verification_scope,
        "trusted_hardware_dxr_pass": trusted_hardware_pass,
        "report": artifact_record(report_path, REPORT_NAME),
        "execution_receipt": artifact_record(
            receipt_path, EXECUTION_RECEIPT_NAME
        ),
        "dsse_bundle": dsse,
        "source": source_identity,
        "signer": signer,
        "complete": True,
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=MAIN_RUNNER.default_build_dir(),
        help="owned standalone Ogre-Next build directory",
    )
    parser.add_argument("--clean-build-dir", action="store_true")
    parser.add_argument("--reuse-build-dir", action="store_true")
    parser.add_argument("--generator")
    parser.add_argument("--dxc", type=Path)
    parser.add_argument("--windows-sdk-bin-root", type=Path)
    parser.add_argument("--require-ci-context", action="store_true")
    parser.add_argument(
        "--jobs", type=int, default=max(1, min(os.cpu_count() or 1, 8))
    )
    parser.add_argument("--validate-contract-only", action="store_true")
    parser.add_argument("--verify-existing", action="store_true")
    parser.add_argument("--integrity-only", action="store_true")
    parser.add_argument("--trusted-attestation-bundle", type=Path)
    parser.add_argument("--expected-source-ref")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.jobs <= 0:
            raise Dxr7Error("--jobs must be positive")
        if args.validate_contract_only and args.verify_existing:
            raise Dxr7Error(
                "--validate-contract-only and --verify-existing conflict"
            )
        verification_only_options = (
            args.integrity_only
            or args.trusted_attestation_bundle is not None
            or args.expected_source_ref is not None
        )
        if verification_only_options and not args.verify_existing:
            raise Dxr7Error(
                "attestation verification options require --verify-existing"
            )
        ogre_lock = MAIN_RUNNER.load_lock()
        dxr7_lock = load_dxr7_lock()
        validate_static_contract()
        if args.validate_contract_only:
            print(
                json.dumps(
                    {
                        "schema": SCHEMA + ".contract",
                        "status": "pass",
                        "ogre_next_commit": ogre_lock["commit"],
                        "dxr7_lock_sha256": LOCK_SHA256,
                        "platform_policy": dxr7_lock["platform_policy"],
                        "network_used": False,
                        "hardware_execution": "not_evaluated",
                    },
                    indent=2,
                    sort_keys=True,
                )
            )
            return 0
        if args.verify_existing:
            report = verify_existing(
                args.build_dir.resolve(),
                integrity_only=args.integrity_only,
                trusted_attestation_bundle=(
                    args.trusted_attestation_bundle.resolve()
                    if args.trusted_attestation_bundle is not None
                    else None
                ),
                expected_source_ref=args.expected_source_ref,
            )
        else:
            if args.reuse_build_dir and (
                args.generator or args.dxc or args.windows_sdk_bin_root
            ):
                raise Dxr7Error(
                    "reused builds cannot change the generator or DXC closure"
                )
            report = run_proof(args)
        print(json.dumps(report, indent=2, sort_keys=True))
        return 0
    except (Dxr7Error, MAIN_RUNNER.ProbeError) as error:
        print(f"Ogre-Next Windows DXR7 failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
