"""Fail closed if the legacy Caelum package regains a Cg runtime route."""

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
CONFIG_SUFFIXES = {".cfg", ".cmake", ".json", ".la", ".pc", ".xml"}
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


def find_forbidden_caelum_package_entries(package_folder: str) -> list[str]:
    """Return packaged files, links, or active scripts that can load Cg."""
    if not os.path.isdir(package_folder):
        raise FileNotFoundError(
            "The Caelum package folder does not exist for the Cg audit"
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
            suffix = Path(filename).suffix.lower()
            if suffix not in RESOURCE_SCRIPT_SUFFIXES | CONFIG_SUFFIXES:
                continue
            script_path = os.path.join(current_root, filename)
            relative_script = os.path.relpath(script_path, package_folder)
            with open(script_path, encoding="utf-8") as script_file:
                active_routes = active_cg_route_lines(script_file.read())
            findings.extend(
                f"{relative_script}:{line_number}:{active_line}"
                for line_number, active_line in active_routes
            )

    return sorted(set(findings))
