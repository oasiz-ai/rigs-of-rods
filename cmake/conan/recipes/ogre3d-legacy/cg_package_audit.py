"""Fail-closed Cg artifact audit shared by the recipe and hostile tests."""

from __future__ import annotations

import os
from pathlib import Path
import re


FORBIDDEN_CG_TOKEN = re.compile(
    r"(?:^|[^a-z0-9])(?:lib)?cg(?:gl|programmanager)?(?:$|[^a-z0-9])",
    re.IGNORECASE,
)
FORBIDDEN_CG_SOURCE_REFERENCE = re.compile(
    r"\.cg(?:inc)?(?:$|[^a-z0-9_])",
    re.IGNORECASE,
)
FORBIDDEN_CG_SUFFIXES = {".cg", ".cginc"}
RESOURCE_SCRIPT_SUFFIXES = {
    ".cfg",
    ".compositor",
    ".material",
    ".os",
    ".particle",
    ".program",
}
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
FORBIDDEN_LEGACY_DIRECTX_FRAGMENTS = (
    "direct3d9",
    "d3d9",
    "d3dx9",
    "d3dx11",
    "dxerr",
)
WINDOWS_KITS_INCLUDE_PATTERN = re.compile(
    r"^[a-z]:/program files(?: \(x86\))?/windows kits/"
    r"(?:10/include/[^/]+/um|8\.(?:0|1)/include/um)$"
)
WINDOWS_KITS_LIBRARY_PATTERN = re.compile(
    r"^[a-z]:/program files(?: \(x86\))?/windows kits/"
    r"(?:10/lib/[^/]+/um/x64|8\.1/lib/winv6\.3/um/x64|"
    r"8\.0/lib/win8/um/x64)/([^/]+\.lib)$"
)
PACKAGE_TEXT_EXTENSIONS = (
    ".cfg",
    ".cmake",
    ".json",
    ".la",
    ".pc",
    ".txt",
    ".xml",
)
FORBIDDEN_LEGACY_DIRECTX_TEXT = (
    "microsoft directx sdk",
    "dxsdk_dir",
    "directx_home",
    "directx_root",
    "directx_base",
)
WINDOWS_IMPORT_LIBRARY_PATH = re.compile(
    r"[a-z]:[/\\][^\"';\r\n]*?(?:d3d11|dxgi|dxguid)\.lib",
    re.IGNORECASE,
)


def contains_forbidden_cg_token(value: str) -> bool:
    return (
        "cgprogrammanager" in value.lower()
        or FORBIDDEN_CG_TOKEN.search(value) is not None
        or FORBIDDEN_CG_SOURCE_REFERENCE.search(value) is not None
    )


def strip_resource_comments(source: str) -> str:
    def preserve_newlines(match: re.Match[str]) -> str:
        return "\n" * match.group(0).count("\n")

    without_blocks = re.sub(
        r"/\*.*?\*/", preserve_newlines, source, flags=re.DOTALL
    )
    without_cpp_lines = re.sub(r"//[^\r\n]*", "", without_blocks)
    return re.sub(r"(?m)^[ \t]*#[^\r\n]*$", "", without_cpp_lines)


def active_cg_route_lines(source: str) -> list[tuple[int, str]]:
    active = strip_resource_comments(source)
    findings: list[tuple[int, str]] = []
    for line_number, line in enumerate(active.splitlines(), start=1):
        if (
            LEGACY_PROGRAM_DECLARATION.search(line)
            or LEGACY_SHADER_REFERENCE.search(line)
            or LEGACY_PLUGIN_DIRECTIVE.search(line)
        ):
            findings.append((line_number, line.strip()))
    return findings


def contains_forbidden_legacy_directx_token(value: str) -> bool:
    lowered = value.lower()
    return any(
        fragment in lowered
        for fragment in FORBIDDEN_LEGACY_DIRECTX_FRAGMENTS
    )


def is_trusted_windows_kits_include_path(value: str) -> bool:
    normalized = value.replace("\\", "/").lower()
    return WINDOWS_KITS_INCLUDE_PATTERN.fullmatch(normalized) is not None


def is_trusted_windows_kits_library_path(
    value: str, expected_filename: str | None = None
) -> bool:
    normalized = value.replace("\\", "/").lower()
    match = WINDOWS_KITS_LIBRARY_PATTERN.fullmatch(normalized)
    if match is None:
        return False
    return expected_filename is None or match.group(1) == expected_filename.lower()


def find_forbidden_cg_package_entries(package_folder: str) -> list[str]:
    """Return every path/config/symlink that could reactivate Cg."""
    if not os.path.isdir(package_folder):
        raise FileNotFoundError(
            "The Ogre 1.11 package folder does not exist for the Cg audit"
        )

    findings: list[str] = []

    def raise_walk_error(error: OSError) -> None:
        raise error

    for current_root, directories, filenames in os.walk(
        package_folder,
        topdown=True,
        followlinks=False,
        onerror=raise_walk_error,
    ):
        for entry_name in (*directories, *filenames):
            entry_path = os.path.join(current_root, entry_name)
            relative_path = os.path.relpath(entry_path, package_folder)
            if (
                Path(entry_name).suffix.lower() in FORBIDDEN_CG_SUFFIXES
                or contains_forbidden_cg_token(entry_name)
            ):
                findings.append(relative_path)
            if os.path.islink(entry_path):
                link_target = os.readlink(entry_path)
                if contains_forbidden_cg_token(link_target):
                    findings.append(f"{relative_path} -> {link_target}")

        for filename in filenames:
            if Path(filename).suffix.lower() not in RESOURCE_SCRIPT_SUFFIXES:
                continue
            config_path = os.path.join(current_root, filename)
            relative_config = os.path.relpath(config_path, package_folder)
            with open(config_path, encoding="utf-8") as config_file:
                active_routes = active_cg_route_lines(config_file.read())
            findings.extend(
                f"{relative_config}:{line_number}:{active_line}"
                for line_number, active_line in active_routes
            )

    return sorted(set(findings))


def find_forbidden_legacy_directx_package_entries(
    package_folder: str,
) -> list[str]:
    """Return packaged paths/config/symlinks from the retired DirectX SDK."""
    if not os.path.isdir(package_folder):
        raise FileNotFoundError(
            "The Ogre 1.11 package folder does not exist for the legacy "
            "DirectX audit"
        )

    findings: list[str] = []

    def raise_walk_error(error: OSError) -> None:
        raise error

    for current_root, directories, filenames in os.walk(
        package_folder,
        topdown=True,
        followlinks=False,
        onerror=raise_walk_error,
    ):
        for entry_name in (*directories, *filenames):
            entry_path = os.path.join(current_root, entry_name)
            relative_path = os.path.relpath(entry_path, package_folder)
            if contains_forbidden_legacy_directx_token(entry_name):
                findings.append(relative_path)
            if os.path.islink(entry_path):
                link_target = os.readlink(entry_path)
                if contains_forbidden_legacy_directx_token(link_target):
                    findings.append(f"{relative_path} -> {link_target}")

        for filename in filenames:
            lowered_filename = filename.lower()
            if not lowered_filename.endswith(PACKAGE_TEXT_EXTENSIONS):
                continue
            config_path = os.path.join(current_root, filename)
            relative_config = os.path.relpath(config_path, package_folder)
            with open(config_path, encoding="utf-8") as config_file:
                for line_number, line in enumerate(config_file, start=1):
                    active_line = line.strip()
                    if not active_line or active_line.startswith("#"):
                        continue
                    lowered_line = active_line.lower()
                    forbidden_text = (
                        contains_forbidden_legacy_directx_token(active_line)
                        or any(
                            fragment in lowered_line
                            for fragment in FORBIDDEN_LEGACY_DIRECTX_TEXT
                        )
                    )
                    untrusted_import_path = any(
                        not is_trusted_windows_kits_library_path(match.group(0))
                        for match in WINDOWS_IMPORT_LIBRARY_PATH.finditer(
                            active_line
                        )
                    )
                    if forbidden_text or untrusted_import_path:
                        findings.append(
                            f"{relative_config}:{line_number}:{active_line}"
                        )

    return sorted(set(findings))
