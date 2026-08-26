#!/usr/bin/env python3
"""Fail-closed relocation audit for the non-product Ogre 1.11 compatibility lane."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import ntpath
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
from typing import Mapping, Sequence
import zipfile


REPORT_SCHEMA = "ror.ogre11_compatibility_audit.v1"
SHA1_RE = re.compile(r"[0-9a-f]{40}")
WINDOWS_DLL_RE = re.compile(r"^[A-Za-z0-9_.+-]+\.dll$", re.IGNORECASE)
WINDOWS_SYSTEM_DLLS = frozenset(
    {
        "advapi32.dll",
        "bcrypt.dll",
        "cfgmgr32.dll",
        "comctl32.dll",
        "comdlg32.dll",
        "crypt32.dll",
        "d3d11.dll",
        "d3dcompiler_47.dll",
        "dbghelp.dll",
        "dinput8.dll",
        "dwmapi.dll",
        "dxgi.dll",
        "gdi32.dll",
        "imm32.dll",
        "iphlpapi.dll",
        "kernel32.dll",
        "msvcp140.dll",
        "msvcp140_1.dll",
        "normaliz.dll",
        "ntdll.dll",
        "ole32.dll",
        "oleaut32.dll",
        "opengl32.dll",
        "powrprof.dll",
        "psapi.dll",
        "rpcrt4.dll",
        "secur32.dll",
        "setupapi.dll",
        "shell32.dll",
        "shlwapi.dll",
        "ucrtbase.dll",
        "user32.dll",
        "vcruntime140.dll",
        "vcruntime140_1.dll",
        "version.dll",
        "winhttp.dll",
        "winmm.dll",
        "wintrust.dll",
        "ws2_32.dll",
        "wtsapi32.dll",
    }
)
FORBIDDEN_RUNTIME_NAME_FRAGMENTS = (
    "cgprogrammanager",
    "rendersystem_direct3d9",
    "directx9",
    "d3dx9",
    "dxerr",
)
FORBIDDEN_SHADER_SUFFIXES = frozenset({".cg", ".cginc"})
RESOURCE_SCRIPT_SUFFIXES = frozenset(
    {".cfg", ".compositor", ".material", ".os", ".particle", ".program"}
)
LEGACY_PROGRAM_DECLARATION = re.compile(
    r"(?im)^[ \t]*(?:[A-Za-z_]+)_program[ \t]+"
    r"[^\s{}]+[ \t]+(?:asm|cg)\b"
)
LEGACY_SHADER_REFERENCE = re.compile(
    r"(?im)(?<![A-Za-z0-9_])"
    r"(?:source[ \t]+)?(?:\"[^\"\r\n]+|[^\s{}\"']+)"
    r"\.(?:cg|cginc)\b"
)
LEGACY_PLUGIN_DIRECTIVE = re.compile(
    r"(?im)^[ \t]*(?:load[ \t]+)?plugin[ \t]*=[ \t]*"
    r"[^\r\n]*cgprogrammanager\b"
)
MAX_RESOURCE_SCRIPT_BYTES = 16 * 1024 * 1024


class AuditError(RuntimeError):
    """The relocated compatibility artifact violated its bounded contract."""


@dataclass(frozen=True)
class PlatformContract:
    executable: str
    launcher: str | None
    plugin_folder: str
    plugins: tuple[str, ...]
    required_runtime_paths: tuple[str, ...]


PLATFORM_CONTRACTS = {
    "linux-x86_64": PlatformContract(
        executable="RoR",
        launcher="RunRoR",
        plugin_folder="lib",
        plugins=(
            "Codec_FreeImage",
            "RenderSystem_GL",
            "Plugin_ParticleFX",
            "Plugin_OctreeSceneManager",
            "libCaelum.so",
        ),
        required_runtime_paths=(
            "lib/libOgreMain.so",
            "lib/Codec_FreeImage.so",
            "lib/RenderSystem_GL.so",
            "lib/Plugin_ParticleFX.so",
            "lib/Plugin_OctreeSceneManager.so",
            "lib/libCaelum.so",
        ),
    ),
    "windows-x86_64": PlatformContract(
        executable="RoR.exe",
        launcher=None,
        plugin_folder=".",
        plugins=(
            "Codec_FreeImage",
            "RenderSystem_Direct3D11",
            "Plugin_ParticleFX",
            "Plugin_OctreeSceneManager",
            "Caelum",
        ),
        required_runtime_paths=(
            "OgreMain.dll",
            "Codec_FreeImage.dll",
            "RenderSystem_Direct3D11.dll",
            "Plugin_ParticleFX.dll",
            "Plugin_OctreeSceneManager.dll",
            "Caelum.dll",
        ),
    ),
}


def normalized_text(value: str) -> str:
    return value.replace("\\", "/").casefold()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def is_within(root: Path, candidate: Path) -> bool:
    resolved_root = root.resolve(strict=True)
    resolved_candidate = candidate.resolve(strict=True)
    return (
        resolved_candidate == resolved_root
        or resolved_root in resolved_candidate.parents
    )


def require_tool(name: str) -> str:
    resolved = shutil.which(name)
    if resolved is None:
        raise AuditError(f"required audit tool is unavailable: {name}")
    return resolved


def run(
    command: Sequence[str],
    *,
    environment: Mapping[str, str] | None = None,
) -> str:
    try:
        result = subprocess.run(
            list(command),
            check=False,
            env=None if environment is None else dict(environment),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=60,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise AuditError(f"audit command could not run: {command!r}: {error}") from error
    if result.returncode != 0:
        raise AuditError(
            f"audit command failed ({result.returncode}): {command!r}\n{result.stdout}"
        )
    return result.stdout


def assert_clean_metadata(
    text: str,
    *,
    context: str,
    forbidden_prefixes: Sequence[str],
) -> None:
    if "\x00" in text:
        raise AuditError(f"{context} contains a NUL byte")
    normalized = normalized_text(text)
    for prefix in forbidden_prefixes:
        if not prefix:
            continue
        candidates = {
            normalized_text(os.path.expanduser(prefix)).rstrip("/"),
            normalized_text(os.path.abspath(os.path.expanduser(prefix))).rstrip("/"),
        }
        if any(candidate and candidate in normalized for candidate in candidates):
            raise AuditError(f"{context} retains forbidden prefix {prefix!r}")


def validate_symlinks(root: Path) -> int:
    count = 0
    for path in sorted(root.rglob("*")):
        if not path.is_symlink():
            continue
        count += 1
        target = os.readlink(path)
        if os.path.isabs(target) or ntpath.isabs(target):
            raise AuditError(f"absolute artifact symlink: {path} -> {target}")
        try:
            resolved = path.resolve(strict=True)
        except OSError as error:
            raise AuditError(f"broken artifact symlink: {path}: {error}") from error
        if not is_within(root, resolved):
            raise AuditError(f"artifact symlink escapes root: {path} -> {target}")
    return count


def parse_plugins_config(
    text: str,
    contract: PlatformContract,
    *,
    forbidden_prefixes: Sequence[str],
) -> tuple[str, ...]:
    assert_clean_metadata(
        text,
        context="plugins.cfg",
        forbidden_prefixes=forbidden_prefixes,
    )
    folders: list[str] = []
    plugins: list[str] = []
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        key, separator, value = line.partition("=")
        if not separator or not value.strip():
            raise AuditError(f"plugins.cfg has malformed directive {line!r}")
        if key.strip() == "PluginFolder":
            folders.append(value.strip())
        elif key.strip() == "Plugin":
            plugins.append(value.strip())
        else:
            raise AuditError(f"plugins.cfg has unknown directive {key.strip()!r}")
    if folders != [contract.plugin_folder]:
        raise AuditError(
            "plugins.cfg relocation folder changed: "
            f"expected {[contract.plugin_folder]!r}, observed={folders!r}"
        )
    if tuple(plugins) != contract.plugins:
        raise AuditError(
            "plugins.cfg compatibility set changed: "
            f"expected={contract.plugins!r}, observed={tuple(plugins)!r}"
        )
    lowered = normalized_text("\n".join(plugins))
    if "cg" in lowered or "direct3d9" in lowered or "d3d9" in lowered:
        raise AuditError("plugins.cfg reactivated Cg or Direct3D9")
    return tuple(plugins)


def strip_resource_comments(source: str) -> str:
    def preserve_newlines(match: re.Match[str]) -> str:
        return "\n" * match.group(0).count("\n")

    without_blocks = re.sub(
        r"/\*.*?\*/", preserve_newlines, source, flags=re.DOTALL
    )
    without_cpp_lines = re.sub(r"//[^\r\n]*", "", without_blocks)
    return re.sub(r"(?m)^[ \t]*#[^\r\n]*$", "", without_cpp_lines)


def validate_resource_script(data: bytes, *, context: str) -> None:
    if len(data) > MAX_RESOURCE_SCRIPT_BYTES:
        raise AuditError(
            f"packaged resource script exceeds the audit bound: {context}"
        )
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as error:
        raise AuditError(
            f"packaged resource script is not UTF-8: {context}"
        ) from error
    active = strip_resource_comments(text)
    if (
        LEGACY_PROGRAM_DECLARATION.search(active)
        or LEGACY_SHADER_REFERENCE.search(active)
        or LEGACY_PLUGIN_DIRECTIVE.search(active)
    ):
        raise AuditError(f"packaged resource retains an active Cg route: {context}")


def normalized_archive_member(
    name: str, *, archive: Path, is_directory: bool
) -> str:
    normalized = name.replace("\\", "/")
    if is_directory and normalized.endswith("/"):
        normalized = normalized[:-1]
    if (
        not normalized
        or normalized.startswith("/")
        or ntpath.isabs(name)
        or any(part in {"", ".", ".."} for part in normalized.split("/"))
    ):
        raise AuditError(f"unsafe ZIP member in {archive}: {name!r}")
    return normalized


def scan_packaged_cg_routes(root: Path) -> dict[str, int]:
    loose_scripts = 0
    archives = 0
    archive_scripts = 0
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.is_symlink():
            continue
        suffix = path.suffix.casefold()
        relative = path.relative_to(root).as_posix()
        if suffix in FORBIDDEN_SHADER_SUFFIXES:
            raise AuditError(f"artifact contains a legacy shader source: {relative}")
        if suffix in RESOURCE_SCRIPT_SUFFIXES:
            validate_resource_script(path.read_bytes(), context=relative)
            loose_scripts += 1
        if suffix != ".zip":
            continue
        archives += 1
        try:
            with zipfile.ZipFile(path) as archive:
                observed: set[str] = set()
                for member in archive.infolist():
                    normalized = normalized_archive_member(
                        member.filename,
                        archive=path,
                        is_directory=member.is_dir(),
                    )
                    folded = normalized.casefold()
                    if folded in observed:
                        raise AuditError(
                            f"ZIP contains duplicate normalized member: {relative}:{normalized}"
                        )
                    observed.add(folded)
                    if member.flag_bits & 0x1:
                        raise AuditError(
                            f"ZIP contains encrypted member: {relative}:{normalized}"
                        )
                    unix_mode = (member.external_attr >> 16) & 0xFFFF
                    if unix_mode and stat.S_ISLNK(unix_mode):
                        raise AuditError(
                            f"ZIP contains symbolic-link member: {relative}:{normalized}"
                        )
                    member_suffix = Path(normalized).suffix.casefold()
                    if member.is_dir():
                        continue
                    if member_suffix in FORBIDDEN_SHADER_SUFFIXES:
                        raise AuditError(
                            "artifact archive contains a legacy shader source: "
                            f"{relative}:{normalized}"
                        )
                    if member_suffix not in RESOURCE_SCRIPT_SUFFIXES:
                        continue
                    if member.file_size > MAX_RESOURCE_SCRIPT_BYTES:
                        raise AuditError(
                            f"archived resource script exceeds audit bound: "
                            f"{relative}:{normalized}"
                        )
                    validate_resource_script(
                        archive.read(member), context=f"{relative}:{normalized}"
                    )
                    archive_scripts += 1
        except (OSError, zipfile.BadZipFile, RuntimeError) as error:
            raise AuditError(f"cannot audit resource archive {relative}: {error}") from error
    return {
        "loose_resource_scripts": loose_scripts,
        "resource_archives": archives,
        "archived_resource_scripts": archive_scripts,
    }


def require_runtime_files(root: Path, contract: PlatformContract) -> list[Path]:
    executable = root / contract.executable
    if not executable.is_file() or executable.is_symlink():
        raise AuditError(f"compatibility executable is absent: {executable}")
    if os.name != "nt" and not os.access(executable, os.X_OK):
        raise AuditError(f"compatibility executable is not executable: {executable}")
    required = [executable]
    if contract.launcher is not None:
        launcher = root / contract.launcher
        if not launcher.is_file() or launcher.is_symlink():
            raise AuditError(f"compatibility launcher is absent: {launcher}")
        if not os.access(launcher, os.X_OK):
            raise AuditError(f"compatibility launcher is not executable: {launcher}")
        required.append(launcher)

    for relative in contract.required_runtime_paths:
        exact = root / relative
        if not exact.is_file():
            raise AuditError(f"required compatibility runtime is absent: {relative}")
        required.append(exact)

    forbidden_siblings = (
        "RoR-Ogre14",
        "RoR-Ogre14.exe",
        "RoR-OgreNext",
        "RoR-OgreNext.exe",
        "RoR-Combined",
        "RoR-Combined.exe",
    )
    for sibling in forbidden_siblings:
        if (root / sibling).exists():
            raise AuditError(
                f"compatibility artifact contains product renderer sibling: {sibling}"
            )
    for relative in sorted(path.relative_to(root) for path in root.rglob("*")):
        lowered = normalized_text(relative.as_posix())
        if any(fragment in lowered for fragment in FORBIDDEN_RUNTIME_NAME_FRAGMENTS):
            raise AuditError(f"artifact retains forbidden runtime name: {relative}")
    return required


def parse_ldd_paths(output: str) -> tuple[Path, ...]:
    paths: list[Path] = []
    for raw_line in output.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("linux-vdso"):
            continue
        if "=>" in line:
            _, resolved = line.split("=>", 1)
            value = resolved.split("(", 1)[0].strip()
            if value == "not found":
                raise AuditError(f"relocated ELF dependency is unresolved: {line}")
        else:
            value = line.split("(", 1)[0].strip()
        if value.startswith("/"):
            paths.append(Path(value))
    return tuple(paths)


def linux_binary_audit(
    root: Path,
    forbidden_prefixes: Sequence[str],
) -> list[dict[str, object]]:
    file_tool = require_tool("file")
    readelf = require_tool("readelf")
    ldd = require_tool("ldd")
    candidates = [root / "RoR"] + [
        path
        for path in sorted((root / "lib").rglob("*"))
        if path.is_file() and not path.is_symlink()
    ]
    binaries: list[Path] = []
    for candidate in candidates:
        identity = run([file_tool, "-b", str(candidate)])
        if identity.startswith("ELF "):
            binaries.append(candidate)
    if root / "RoR" not in binaries:
        raise AuditError("relocated RoR is not an ELF binary")

    environment = os.environ.copy()
    environment["LD_LIBRARY_PATH"] = str(root / "lib")
    records: list[dict[str, object]] = []
    for binary in binaries:
        header = run([readelf, "-h", str(binary)])
        dynamic = run([readelf, "-d", str(binary)])
        dependencies = run([ldd, str(binary)], environment=environment)
        if not re.search(
            r"Machine:\s+Advanced Micro Devices X86-64", header
        ):
            raise AuditError(f"ELF is not x86_64: {binary}")
        assert_clean_metadata(
            dynamic + dependencies,
            context=str(binary.relative_to(root)),
            forbidden_prefixes=forbidden_prefixes,
        )
        for dependency in parse_ldd_paths(dependencies):
            if is_within(root, dependency):
                continue
            if dependency.as_posix().startswith(
                ("/lib/", "/lib64/", "/usr/lib/", "/usr/lib64/")
            ):
                continue
            raise AuditError(
                f"ELF dependency escapes artifact and system roots: "
                f"{binary.name} -> {dependency}"
            )
        records.append(
            {
                "path": binary.relative_to(root).as_posix(),
                "sha256": sha256_file(binary),
            }
        )
    return records


def parse_dumpbin_dependencies(output: str) -> tuple[str, ...]:
    dependencies = []
    for line in output.splitlines():
        candidate = line.strip()
        if WINDOWS_DLL_RE.fullmatch(candidate):
            dependencies.append(candidate)
    return tuple(dependencies)


def dumpbin_metadata_payload(output: str) -> str:
    return "\n".join(
        line
        for line in output.splitlines()
        if not line.strip().casefold().startswith("dump of file ")
    )


def is_windows_system_dependency(name: str) -> bool:
    lowered = name.casefold()
    if (
        lowered in WINDOWS_SYSTEM_DLLS
        or lowered.startswith("api-ms-win-")
        or lowered.startswith("ext-ms-win-")
    ):
        return True
    system_root_value = os.environ.get("SystemRoot")
    if not system_root_value:
        return False
    system_root = Path(system_root_value)
    candidate = system_root / "System32" / name
    return (
        system_root.is_dir()
        and candidate.is_file()
        and is_within(system_root, candidate)
    )


def windows_binary_audit(
    root: Path,
    forbidden_prefixes: Sequence[str],
) -> list[dict[str, object]]:
    dumpbin = require_tool("dumpbin")
    binaries = sorted(
        path
        for path in root.iterdir()
        if path.is_file()
        and not path.is_symlink()
        and path.suffix.casefold() in {".exe", ".dll"}
    )
    if root / "RoR.exe" not in binaries:
        raise AuditError("relocated RoR.exe is not in the PE closure")
    packaged_dlls = {
        path.name.casefold()
        for path in binaries
        if path.suffix.casefold() == ".dll"
    }
    records: list[dict[str, object]] = []
    for binary in binaries:
        headers = run([dumpbin, "/nologo", "/headers", str(binary)])
        dependents = run([dumpbin, "/nologo", "/dependents", str(binary)])
        if not re.search(r"\b8664 machine \(x64\)", headers, re.IGNORECASE):
            raise AuditError(f"PE is not x64: {binary}")
        assert_clean_metadata(
            dumpbin_metadata_payload(headers),
            context=str(binary.relative_to(root)),
            forbidden_prefixes=forbidden_prefixes,
        )
        for dependency in parse_dumpbin_dependencies(dependents):
            lowered = dependency.casefold()
            if (
                lowered in packaged_dlls
                or is_windows_system_dependency(dependency)
            ):
                continue
            raise AuditError(
                f"PE dependency is neither packaged nor an approved system DLL: "
                f"{binary.name} -> {dependency}"
            )
        records.append(
            {
                "path": binary.relative_to(root).as_posix(),
                "sha256": sha256_file(binary),
            }
        )
    return records


def audit(
    root: Path,
    platform: str,
    forbidden_prefixes: Sequence[str],
    source_commit: str,
    report_path: Path,
) -> dict[str, object]:
    if not root.is_absolute() or not root.is_dir() or root.is_symlink():
        raise AuditError(f"artifact root must be an absolute real directory: {root}")
    root = root.resolve(strict=True)
    if not report_path.is_absolute():
        raise AuditError("audit report path must be absolute")
    if report_path.exists():
        raise AuditError(f"audit report already exists: {report_path}")
    report_parent = report_path.parent
    report_parent.mkdir(parents=True, exist_ok=True)
    if not is_within(root, report_parent):
        raise AuditError("audit report must stay inside the compatibility artifact")
    if not SHA1_RE.fullmatch(source_commit):
        raise AuditError("source commit must be a lowercase 40-character SHA-1")

    contract = PLATFORM_CONTRACTS[platform]
    symlink_count = validate_symlinks(root)
    required_files = require_runtime_files(root, contract)
    plugins_path = root / "plugins.cfg"
    if not plugins_path.is_file() or plugins_path.is_symlink():
        raise AuditError("relocated plugins.cfg is absent")
    plugins = parse_plugins_config(
        plugins_path.read_text(encoding="utf-8"),
        contract,
        forbidden_prefixes=forbidden_prefixes,
    )
    resource_audit = scan_packaged_cg_routes(root)
    if contract.launcher is not None:
        launcher_path = root / contract.launcher
        assert_clean_metadata(
            launcher_path.read_text(encoding="utf-8"),
            context=contract.launcher,
            forbidden_prefixes=forbidden_prefixes,
        )

    if platform == "linux-x86_64":
        binaries = linux_binary_audit(root, forbidden_prefixes)
    else:
        binaries = windows_binary_audit(root, forbidden_prefixes)
    if not binaries:
        raise AuditError("compatibility artifact has no audited native binaries")

    document: dict[str, object] = {
        "schema": REPORT_SCHEMA,
        "status": "passed",
        "source_commit": source_commit,
        "platform": platform,
        "qualification_scope": "ogre11-cgfree-build-install-relocation-closure",
        "entrypoint": contract.launcher or contract.executable,
        "application_executable": contract.executable,
        "renderer_dependency": "ogre3d/1.11.6.1",
        "caelum_dependency": "ogre3d-caelum/0.6.3.1",
        "compatibility_artifact": True,
        "product_runtime": False,
        "ogre_next_visible_frames_proven": False,
        "renderer_runtime_smoke_proven": False,
        "playability_proven": False,
        "cg_source_files_present": False,
        "active_cg_routes_present": False,
        "resource_audit": resource_audit,
        "plugins": list(plugins),
        "symlink_count": symlink_count,
        "required_runtime_files": sorted(
            {path.relative_to(root).as_posix() for path in required_files}
        ),
        "audited_binaries": binaries,
    }
    with report_path.open("x", encoding="utf-8") as stream:
        json.dump(document, stream, indent=2, sort_keys=True)
        stream.write("\n")
    return document


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument(
        "--platform", required=True, choices=tuple(PLATFORM_CONTRACTS)
    )
    parser.add_argument("--forbidden-prefix", action="append", default=[])
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--report", required=True, type=Path)
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    arguments = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        report = audit(
            arguments.root,
            arguments.platform,
            arguments.forbidden_prefix,
            arguments.source_commit,
            arguments.report,
        )
    except (AuditError, OSError, UnicodeError, ValueError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    print(
        "PASS "
        f"platform={report['platform']} "
        f"binaries={len(report['audited_binaries'])} "
        "scope=compatibility-only"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
