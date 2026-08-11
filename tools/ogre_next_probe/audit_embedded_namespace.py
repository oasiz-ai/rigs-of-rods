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
DEFINED_GLOBAL_INTERSECTION_ALLOWLIST: frozenset[str] = frozenset(
    {
        # libc++17 inline variable, emitted weakly by both runtime closures.
        "__ZNSt3__119piecewise_constructE",
    }
)
LEGACY_RUNTIME_DYLIB_PATTERN = re.compile(
    r"^(?:libOgre|Plugin_|Codec_|RenderSystem_).*[.]dylib$"
)
FORBIDDEN_DIRECT_SOURCE_PATTERN = re.compile(
    r"(?:Bridge|Transport)", re.IGNORECASE
)
UNREMAPPED_OGRE_MANGLED_PATTERN = re.compile(
    r"_Z(?:N[KVRrO]*|T(?:I|S|V)N)4Ogre"
)
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


def global_definition_linkages(
    path: Path, definitions: set[str]
) -> tuple[set[str], set[str]]:
    """Partition Mach-O global definitions into weak and strong linkage."""
    raw = output("nm", "-gm", str(path))
    weak: set[str] = set()
    strong: set[str] = set()
    for line in raw.splitlines():
        fields = line.rsplit(maxsplit=1)
        if not fields or fields[-1] not in definitions:
            continue
        destination = weak if " weak " in f" {line} " else strong
        destination.add(fields[-1])
    require(
        weak | strong == definitions,
        f"could not classify every global definition in {path}",
    )
    return weak, strong


def command_text(entry: dict[str, object]) -> str:
    if isinstance(entry.get("command"), str):
        return str(entry["command"])
    arguments = entry.get("arguments")
    require(isinstance(arguments, list), "compile entry has no command")
    return " ".join(str(value) for value in arguments)


def one_compile_entry(
    entries: list[dict[str, object]], source: Path
) -> dict[str, object]:
    matches = [
        entry for entry in entries
        if Path(str(entry.get("file", ""))).resolve() == source.resolve()
    ]
    require(
        len(matches) == 1,
        f"expected one compile entry for {source}, got {len(matches)}",
    )
    return matches[0]


def one_target_compile_entry(
    entries: list[dict[str, object]], source: Path, target_name: str
) -> dict[str, object]:
    target_token = f"CMakeFiles/{target_name}.dir/"
    matches = [
        entry
        for entry in entries
        if Path(str(entry.get("file", ""))).resolve() == source.resolve()
        and target_token in command_text(entry)
    ]
    require(
        len(matches) == 1,
        f"expected one {target_name} compile entry for {source}, got {len(matches)}",
    )
    return matches[0]


def exact_regular_file(value: str, label: str) -> Path:
    candidate = Path(value)
    require(candidate.is_absolute(), f"{label} is not absolute: {candidate}")
    require(
        candidate.exists() and candidate.is_file() and not candidate.is_symlink(),
        f"{label} is missing, indirect, or not a file: {candidate}",
    )
    return candidate.resolve()


def exact_directory(value: str, label: str) -> Path:
    candidate = Path(value)
    require(candidate.is_absolute(), f"{label} is not absolute: {candidate}")
    require(
        candidate.exists() and candidate.is_dir() and not candidate.is_symlink(),
        f"{label} is missing, indirect, or not a directory: {candidate}",
    )
    return candidate.resolve()


def is_relative_to(path: Path, directory: Path) -> bool:
    try:
        path.relative_to(directory)
        return True
    except ValueError:
        return False


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--next-archive", action="append", required=True)
    parser.add_argument("--embedded-runtime-archive", required=True)
    parser.add_argument("--direct-contract-archive", required=True)
    parser.add_argument("--plugin-object", required=True)
    parser.add_argument("--legacy-main-library", required=True)
    parser.add_argument("--legacy-library", action="append", required=True)
    parser.add_argument("--executable", required=True)
    parser.add_argument("--compile-commands", required=True)
    parser.add_argument("--next-source-root", required=True)
    parser.add_argument("--next-adapter", required=True)
    parser.add_argument("--legacy-adapter", required=True)
    parser.add_argument("--main-source", required=True)
    parser.add_argument("--session-adapter", required=True)
    parser.add_argument("--embedded-target-name", required=True)
    parser.add_argument("--embedded-source", action="append", required=True)
    parser.add_argument("--direct-target-name", required=True)
    parser.add_argument("--direct-source", action="append", required=True)
    parser.add_argument("--remap-header", required=True)
    parser.add_argument("--legacy-include", required=True)
    parser.add_argument("--report", required=True)
    args = parser.parse_args()

    next_archives = [
        exact_regular_file(value, "OgreNext archive")
        for value in args.next_archive
    ]
    embedded_runtime_archive = exact_regular_file(
        args.embedded_runtime_archive, "embedded N1 runtime archive"
    )
    direct_contract_archive = exact_regular_file(
        args.direct_contract_archive, "direct contract archive"
    )
    modern_archives = [
        *next_archives,
        embedded_runtime_archive,
        direct_contract_archive,
    ]
    legacy_libraries = [
        exact_regular_file(value, "OGRE14 runtime library")
        for value in args.legacy_library
    ]
    plugin_object = exact_regular_file(args.plugin_object, "plugin object")
    legacy_main_library = exact_regular_file(
        args.legacy_main_library, "OGRE14 main library"
    )
    executable = exact_regular_file(args.executable, "dual-runtime executable")
    compile_commands = exact_regular_file(
        args.compile_commands, "compile commands"
    )
    next_source_root = exact_directory(args.next_source_root, "OgreNext source root")
    next_adapter = exact_regular_file(args.next_adapter, "OgreNext adapter source")
    legacy_adapter = exact_regular_file(args.legacy_adapter, "OGRE14 adapter source")
    main_source = exact_regular_file(args.main_source, "dual-runtime main source")
    session_adapter = exact_regular_file(
        args.session_adapter, "direct session adapter source"
    )
    embedded_sources = [
        exact_regular_file(value, "embedded N1 source")
        for value in args.embedded_source
    ]
    direct_sources = [
        exact_regular_file(value, "direct contract source")
        for value in args.direct_source
    ]
    remap_header = exact_regular_file(args.remap_header, "namespace remap header")
    legacy_include = exact_directory(args.legacy_include, "OGRE14 include root")
    report = Path(args.report)
    require(report.is_absolute(), "audit report path must be absolute")

    require(
        len(modern_archives) == len(set(modern_archives)),
        "modern archive audit inputs contain duplicates",
    )
    require(
        len(legacy_libraries) == len(set(legacy_libraries)),
        "OGRE14 runtime audit inputs contain duplicates",
    )
    require(
        legacy_main_library in legacy_libraries,
        "OGRE14 runtime list does not contain its main library",
    )
    legacy_runtime_directories = {path.parent for path in legacy_libraries}
    require(
        len(legacy_runtime_directories) == 1,
        "OGRE14 runtime libraries do not share one exact directory",
    )
    legacy_runtime_directory = next(iter(legacy_runtime_directories))
    discovered_legacy_libraries = {
        candidate.resolve()
        for candidate in legacy_runtime_directory.iterdir()
        if candidate.is_file()
        and not candidate.is_symlink()
        and LEGACY_RUNTIME_DYLIB_PATTERN.fullmatch(candidate.name)
    }
    require(
        set(legacy_libraries) == discovered_legacy_libraries,
        "OGRE14 runtime collision set is not the complete versioned dylib directory closure",
    )
    require(
        all(source.suffix.lower() in CPP_SUFFIXES for source in embedded_sources),
        "embedded N1 source list contains a non-translation-unit entry",
    )
    require(
        all(source.suffix.lower() in CPP_SUFFIXES for source in direct_sources),
        "direct contract source list contains a non-translation-unit entry",
    )
    forbidden_direct_sources = [
        source.name
        for source in direct_sources
        if FORBIDDEN_DIRECT_SOURCE_PATTERN.search(source.name)
    ]
    require(
        not forbidden_direct_sources,
        "direct contract imported Bridge/Transport source names: "
        + ", ".join(sorted(forbidden_direct_sources)),
    )

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
    require(not UNREMAPPED_OGRE_MANGLED_PATTERN.search(next_raw),
            "an Itanium Ogre namespace symbol remains in the OgreNext archives")

    embedded_raw, embedded_demangled = nm(embedded_runtime_archive)
    require(
        "RoR::Render::OgreNextN1Frontend::OgreNextN1Frontend" in
        embedded_demangled,
        "the embedded runtime archive lacks the production N1 frontend",
    )
    require(
        "Ogre::" not in embedded_demangled
        and not UNREMAPPED_OGRE_MANGLED_PATTERN.search(embedded_raw),
        "an unremapped Ogre owner remains in the embedded N1 runtime archive",
    )

    _, direct_demangled = nm(direct_contract_archive)
    require(
        "RoR::RendererInProcessSession::RendererInProcessSession" in
        direct_demangled,
        "the direct contract archive lacks RendererInProcessSession",
    )
    require(
        "Ogre::" not in direct_demangled
        and "RoROgreNext::" not in direct_demangled,
        "the renderer-neutral direct contract archive imports an Ogre ABI owner",
    )
    require(
        not any(
            FORBIDDEN_DIRECT_SOURCE_PATTERN.search(line)
            for line in direct_demangled.splitlines()
        ),
        "the direct contract archive imports a Bridge/Transport symbol",
    )

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

    _, legacy_main_demangled = nm(legacy_main_library)
    require("Ogre::Root::getSingletonPtr()" in legacy_main_demangled,
            "the explicit legacy runtime lacks Ogre::Root")
    legacy_definitions: set[str] = set()
    legacy_strong_definitions: set[str] = set()
    legacy_library_reports: list[dict[str, object]] = []
    for legacy_library in legacy_libraries:
        _, legacy_demangled = nm(legacy_library)
        require(
            "RoROgreNext::" not in legacy_demangled,
            f"legacy runtime was modified by the namespace fork: {legacy_library}",
        )
        definitions = defined_global_symbols(legacy_library)
        require(
            definitions,
            f"legacy runtime has no global definitions to audit: {legacy_library}",
        )
        _, strong_definitions = global_definition_linkages(
            legacy_library, definitions
        )
        legacy_definitions.update(definitions)
        legacy_strong_definitions.update(strong_definitions)
        legacy_library_reports.append(
            {
                "path": str(legacy_library),
                "sha256": digest(legacy_library),
                "defined_globals": len(definitions),
            }
        )
    require(legacy_definitions, "OGRE14 runtime closure has no global definitions")
    global_intersections: list[dict[str, object]] = []
    for archive in modern_archives:
        modern_definitions = defined_global_symbols(archive)
        require(modern_definitions,
                f"the modern archive has no global definitions to audit: {archive}")
        intersection = modern_definitions & legacy_definitions
        unexpected = intersection - DEFINED_GLOBAL_INTERSECTION_ALLOWLIST
        require(not unexpected,
                f"global definitions collide with OGRE14 in {archive}: " +
                ", ".join(sorted(unexpected)))
        reviewed_weak = intersection & DEFINED_GLOBAL_INTERSECTION_ALLOWLIST
        modern_weak, modern_strong = global_definition_linkages(
            archive, modern_definitions
        )
        require(
            reviewed_weak <= modern_weak
            and not (
                reviewed_weak
                & (modern_strong | legacy_strong_definitions)
            ),
            f"reviewed weak collision became strong in {archive}: "
            + ", ".join(sorted(reviewed_weak)),
        )
        global_intersections.append({
            "modern_archive": str(archive),
            "modern_defined_globals": len(modern_definitions),
            "legacy_closure_defined_globals": len(legacy_definitions),
            "intersection": sorted(intersection),
            "reviewed_allowlist": sorted(
                DEFINED_GLOBAL_INTERSECTION_ALLOWLIST),
        })

    # OgreNext is linked statically with hidden visibility, so its resolved
    # symbols are local in the final Mach-O.  Inspect the complete final symbol
    # table here; the archive/plugin rejection gates above remain global-only.
    _, executable_demangled = nm(executable, global_only=False)
    require("Ogre::Root::getSingletonPtr()" in executable_demangled and
            "RoROgreNext::Root::getSingletonPtr()" in executable_demangled,
            "dual-runtime executable does not resolve both Root ABI owners")
    require(
        "RoR::Render::OgreNextN1Frontend::OgreNextN1Frontend" in
        executable_demangled
        and "RoR::RendererInProcessSession::RendererInProcessSession" in
        executable_demangled,
        "dual-runtime executable does not contain the production N1/direct session lifecycle",
    )
    require(
        "RoR::Render::OgreNextN1Frontend::~OgreNextN1Frontend" in
        executable_demangled
        and "RoR::RendererInProcessSession::~RendererInProcessSession" in
        executable_demangled,
        "dual-runtime executable does not contain both production destructors",
    )
    linked_libraries = output("otool", "-L", str(executable))
    require(legacy_main_library.name in linked_libraries,
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
    session_command = command_text(
        one_target_compile_entry(
            entries, session_adapter, "ror_ogre_next_dual_runtime_link_smoke"
        )
    )
    require(str(remap_header) in next_command,
            "the OgreNext adapter does not receive the fork remap")
    require(str(remap_header) not in legacy_command and
            "Ogre=RoROgreNext" not in legacy_command,
            "the fork remap leaked into the OGRE14 adapter")
    require(str(remap_header) not in main_command,
            "the fork remap leaked into the dual-runtime main TU")
    require(
        str(remap_header) not in session_command
        and "Ogre=RoROgreNext" not in session_command,
        "the fork remap leaked into the renderer-neutral session adapter",
    )
    require(str(legacy_include) not in next_command,
            "the OgreNext adapter sees OGRE14 headers")
    require(str(next_source_root) not in legacy_command,
            "the OGRE14 adapter sees OgreNext headers")

    embedded_commands = [
        command_text(
            one_target_compile_entry(
                entries, source, args.embedded_target_name
            )
        )
        for source in embedded_sources
    ]
    require(
        all(str(remap_header) in command for command in embedded_commands),
        "a production embedded N1 translation unit lacks the namespace remap",
    )
    require(
        all(
            "ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM" not in command
            for command in embedded_commands
        ),
        "the production embedded N1 target imported a test seam",
    )
    direct_commands = [
        command_text(
            one_target_compile_entry(entries, source, args.direct_target_name)
        )
        for source in direct_sources
    ]
    require(
        all(
            str(remap_header) not in command
            and "Ogre=RoROgreNext" not in command
            for command in direct_commands
        ),
        "the OgreNext namespace remap leaked into the direct contract closure",
    )

    result = {
        "schema": "ror.ogre_next.embedded_namespace_audit.v2",
        "status": "passed",
        "namespace": "RoROgreNext",
        "upstream_compile_entries": len(upstream_entries),
        "next_archives": [
            {"path": str(path), "sha256": digest(path)} for path in next_archives
        ],
        "embedded_runtime_archive": {
            "path": str(embedded_runtime_archive),
            "sha256": digest(embedded_runtime_archive),
        },
        "direct_contract_archive": {
            "path": str(direct_contract_archive),
            "sha256": digest(direct_contract_archive),
            "bridge_transport_symbols": 0,
        },
        "plugin_object": {"path": str(plugin_object), "sha256": digest(plugin_object)},
        "legacy_main_library": {
            "path": str(legacy_main_library),
            "sha256": digest(legacy_main_library),
        },
        "legacy_runtime_libraries": legacy_library_reports,
        "legacy_runtime_directory": str(legacy_runtime_directory),
        "executable": {"path": str(executable), "sha256": digest(executable)},
        "compile_commands_sha256": digest(compile_commands),
        "defined_global_intersections": global_intersections,
        "translation_unit_isolation": {
            "next_adapter_forced_remap": True,
            "ogre14_adapter_forced_remap": False,
            "main_forced_remap": False,
            "session_adapter_forced_remap": False,
            "embedded_n1_sources_forced_remap": len(embedded_commands),
            "direct_contract_sources_forced_remap": 0,
            "direct_contract_sources": len(direct_commands),
        },
    }
    report.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n",
                      encoding="utf-8")
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
