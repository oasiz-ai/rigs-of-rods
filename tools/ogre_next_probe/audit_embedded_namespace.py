#!/usr/bin/env python3
"""Fail-closed symbol and compile-boundary audit for embedded OgreNext."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path


CPP_SUFFIXES = {".cc", ".cpp", ".cxx", ".mm"}
DEFINED_GLOBAL_INTERSECTION_ALLOWLIST: frozenset[str] = frozenset()
LEGACY_OBJC_CLASSES = (
    "OgreConfigWindowDelegate",
    "OgreMetalView",
    "MetalWinListener",
    "CocoaWindowDelegate",
    "OgreGL3PlusView",
    "OgreGL3PlusWindow",
    "EAGL2View",
    "EAGL2ViewController",
    "OgreView",
    "OgreWindow",
    "AppDelegate",
    "GameViewController",
    "RestartViewController",
    "TutorialViewController",
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def output(*argv: str) -> str:
    completed = subprocess.run(
        argv, check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True
    )
    return completed.stdout


def digest(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def nm(path: Path, *, global_only: bool = True) -> tuple[str, str]:
    argv = ["nm"]
    if global_only:
        argv.append("-g")
    argv.append(str(path))
    raw = output(*argv)
    symbols = "\n".join(
        line.rsplit(maxsplit=1)[-1]
        for line in raw.splitlines()
        if line.strip() and not line.endswith(":")
    )
    completed = subprocess.run(
        ["c++filt"], input=symbols, check=True, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, text=True
    )
    demangled = completed.stdout
    return raw, demangled


def defined_global_symbols(path: Path) -> set[str]:
    """Return exact Mach-O global definitions, excluding undefined imports."""
    raw = output("nm", "-gU", str(path))
    symbols: set[str] = set()
    for line in raw.splitlines():
        fields = line.split()
        if (len(fields) >= 3 and
                re.fullmatch(r"[0-9A-Fa-f]+", fields[0]) and
                re.fullmatch(r"[A-Za-z]", fields[1])):
            symbols.add(fields[-1])
    return symbols


def command_text(entry: dict[str, object]) -> str:
    if isinstance(entry.get("command"), str):
        return str(entry["command"])
    arguments = entry.get("arguments")
    require(isinstance(arguments, list), "compile entry has no command")
    return " ".join(str(value) for value in arguments)


def one_compile_entry(entries: list[dict[str, object]], source: Path) -> dict[str, object]:
    matches = [
        entry for entry in entries
        if Path(str(entry.get("file", ""))).resolve() == source.resolve()
    ]
    require(len(matches) == 1, f"expected one compile entry for {source}, got {len(matches)}")
    return matches[0]


def is_relative_to(path: Path, directory: Path) -> bool:
    try:
        path.relative_to(directory)
        return True
    except ValueError:
        return False


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--next-archive", action="append", required=True)
    parser.add_argument("--plugin-object", required=True)
    parser.add_argument("--legacy-library", required=True)
    parser.add_argument("--executable", required=True)
    parser.add_argument("--compile-commands", required=True)
    parser.add_argument("--next-source-root", required=True)
    parser.add_argument("--next-adapter", required=True)
    parser.add_argument("--legacy-adapter", required=True)
    parser.add_argument("--main-source", required=True)
    parser.add_argument("--remap-header", required=True)
    parser.add_argument("--legacy-include", required=True)
    parser.add_argument("--report", required=True)
    args = parser.parse_args()

    next_archives = [Path(value).resolve() for value in args.next_archive]
    plugin_object = Path(args.plugin_object).resolve()
    legacy_library = Path(args.legacy_library).resolve()
    executable = Path(args.executable).resolve()
    compile_commands = Path(args.compile_commands).resolve()
    next_source_root = Path(args.next_source_root).resolve()
    next_adapter = Path(args.next_adapter).resolve()
    legacy_adapter = Path(args.legacy_adapter).resolve()
    main_source = Path(args.main_source).resolve()
    remap_header = Path(args.remap_header).resolve()
    legacy_include = Path(args.legacy_include).resolve()
    report = Path(args.report).resolve()

    for path in [*next_archives, plugin_object, legacy_library, executable,
                 compile_commands, next_adapter, legacy_adapter, main_source,
                 remap_header]:
        require(path.exists() and not path.is_symlink(), f"missing or indirect audit input: {path}")

    next_raw_parts: list[str] = []
    next_demangled_parts: list[str] = []
    for archive in next_archives:
        raw, demangled = nm(archive)
        next_raw_parts.append(raw)
        next_demangled_parts.append(demangled)
    next_raw = "\n".join(next_raw_parts)
    next_demangled = "\n".join(next_demangled_parts)
    require("RoROgreNext::" in next_demangled,
            "OgreNext archives contain no RoROgreNext C++ owner")
    require("Ogre::" not in next_demangled,
            "an Ogre namespace symbol remains in the namespaced OgreNext archives")
    require(not re.search(r"_Z\S*4Ogre", next_raw),
            "an Itanium Ogre namespace symbol remains in the OgreNext archives")

    for old_name in LEGACY_OBJC_CLASSES:
        require(f"$_{old_name}" not in next_raw,
                f"unprefixed Objective-C runtime class remains: {old_name}")
    for new_name in (
        "RoROgreNextConfigWindowDelegate",
        "RoROgreNextMetalView",
        "RoROgreNextMetalWinListener",
    ):
        require(f"$_{new_name}" in next_raw,
                f"expected prefixed Objective-C runtime class is absent: {new_name}")

    plugin_raw, plugin_demangled = nm(plugin_object)
    require("RoROgreNext_dllStartPlugin" in plugin_raw and
            "RoROgreNext_dllStopPlugin" in plugin_raw,
            "prefixed plugin C exports are absent")
    require(not re.search(r"\b_?dll(?:Start|Stop)Plugin\b", plugin_raw),
            "unprefixed plugin C export remains")
    require("Ogre::" not in plugin_demangled,
            "plugin export probe references the legacy C++ namespace")

    archive_strings = "\n".join(output("strings", str(path)) for path in next_archives)
    require("RoROgreNext_dllStartPlugin" in archive_strings and
            "RoROgreNext_dllStopPlugin" in archive_strings,
            "OgreNext Root did not compile the prefixed dynamic lookup names")

    legacy_raw, legacy_demangled = nm(legacy_library)
    require("Ogre::Root::getSingletonPtr()" in legacy_demangled,
            "the explicit legacy runtime lacks Ogre::Root")
    require("RoROgreNext::" not in legacy_demangled,
            "the explicit legacy runtime was modified by the namespace fork")

    legacy_definitions = defined_global_symbols(legacy_library)
    require(legacy_definitions,
            "the explicit legacy runtime has no global definitions to audit")
    global_intersections: list[dict[str, object]] = []
    for archive in next_archives:
        next_definitions = defined_global_symbols(archive)
        require(next_definitions,
                f"the OgreNext archive has no global definitions to audit: {archive}")
        intersection = next_definitions & legacy_definitions
        unexpected = intersection - DEFINED_GLOBAL_INTERSECTION_ALLOWLIST
        require(not unexpected,
                f"global definitions collide with OGRE14 in {archive}: " +
                ", ".join(sorted(unexpected)))
        global_intersections.append({
            "next_archive": str(archive),
            "next_defined_globals": len(next_definitions),
            "legacy_defined_globals": len(legacy_definitions),
            "intersection": sorted(intersection),
            "reviewed_allowlist": sorted(
                DEFINED_GLOBAL_INTERSECTION_ALLOWLIST),
        })

    # OgreNext is linked statically with hidden visibility, so its resolved
    # symbols are local in the final Mach-O.  Inspect the complete final symbol
    # table here; the archive/plugin rejection gates above remain global-only.
    executable_raw, executable_demangled = nm(executable, global_only=False)
    require("Ogre::Root::getSingletonPtr()" in executable_demangled and
            "RoROgreNext::Root::getSingletonPtr()" in executable_demangled,
            "dual-runtime executable does not resolve both Root ABI owners")
    linked_libraries = output("otool", "-L", str(executable))
    require(legacy_library.name in linked_libraries,
            "dual-runtime executable has no load command for OGRE14")

    payload = json.loads(compile_commands.read_text(encoding="utf-8"))
    require(isinstance(payload, list), "compile_commands.json is not an array")
    entries = [entry for entry in payload if isinstance(entry, dict)]
    upstream_entries = [
        entry for entry in entries
        if Path(str(entry.get("file", ""))).suffix.lower() in CPP_SUFFIXES
        and is_relative_to(
            Path(str(entry.get("file", ""))).resolve(), next_source_root
        )
    ]
    require(upstream_entries, "no OgreNext C++/Objective-C++ compile entries found")
    for entry in upstream_entries:
        require(str(remap_header) in command_text(entry),
                f"OgreNext source lacks forced namespace remap: {entry.get('file')}")

    next_command = command_text(one_compile_entry(entries, next_adapter))
    legacy_command = command_text(one_compile_entry(entries, legacy_adapter))
    main_command = command_text(one_compile_entry(entries, main_source))
    require(str(remap_header) in next_command,
            "the OgreNext adapter does not receive the fork remap")
    require(str(remap_header) not in legacy_command and
            "Ogre=RoROgreNext" not in legacy_command,
            "the fork remap leaked into the OGRE14 adapter")
    require(str(remap_header) not in main_command,
            "the fork remap leaked into the dual-runtime main TU")
    require(str(legacy_include) not in next_command,
            "the OgreNext adapter sees OGRE14 headers")
    require(str(next_source_root) not in legacy_command,
            "the OGRE14 adapter sees OgreNext headers")

    result = {
        "schema": "ror.ogre_next.embedded_namespace_audit.v1",
        "status": "passed",
        "namespace": "RoROgreNext",
        "upstream_compile_entries": len(upstream_entries),
        "next_archives": [
            {"path": str(path), "sha256": digest(path)} for path in next_archives
        ],
        "plugin_object": {"path": str(plugin_object), "sha256": digest(plugin_object)},
        "legacy_library": {"path": str(legacy_library), "sha256": digest(legacy_library)},
        "executable": {"path": str(executable), "sha256": digest(executable)},
        "compile_commands_sha256": digest(compile_commands),
        "defined_global_intersections": global_intersections,
        "translation_unit_isolation": {
            "next_adapter_forced_remap": True,
            "ogre14_adapter_forced_remap": False,
            "main_forced_remap": False,
        },
    }
    report.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n",
                      encoding="utf-8")
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
