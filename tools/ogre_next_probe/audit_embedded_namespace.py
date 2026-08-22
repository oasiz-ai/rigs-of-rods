#!/usr/bin/env python3
"""Fail-closed symbol and compile-boundary audit for embedded OgreNext."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shlex
import subprocess
from pathlib import Path


CPP_SUFFIXES = {".cc", ".cpp", ".cxx", ".mm"}
STB_IMAGE_IMPLEMENTATION_PATTERN = re.compile(
    rb"^[ \t]*#[ \t]*define[ \t]+STB_IMAGE_IMPLEMENTATION(?:[ \t]|$)",
    re.MULTILINE,
)
STB_IMAGE_COMMAND_CONFIGURATION_PATTERN = re.compile(
    r"(?:STBIDEF|STB_IMAGE_[A-Z0-9_]+|STBI_[A-Z0-9_]+)"
)
STB_IMAGE_INDIRECT_INPUT_PREFIXES = (
    "-include",
    "-imacros",
    "/FI",
    "@",
)
DEFINED_GLOBAL_INTERSECTION_ALLOWLIST: frozenset[str] = frozenset(
    {
        # libc++17 inline variable, emitted weakly by both runtime closures.
        "__ZNSt3__119piecewise_constructE",
    }
)
ITANIUM_STD_SYMBOL_PATTERN = re.compile(
    r"^_+Z(?:N[KVRrO]*St|St|TISt|TSSt|ZNSt)"
)
LEGACY_RUNTIME_LIBRARY_PATTERNS = {
    "macos-arm64-metal": re.compile(
        r"^(?:libOgre|Plugin_|Codec_|RenderSystem_).*[.]dylib$"
    ),
    "linux-x86_64-vulkan": re.compile(
        r"^(?:libOgre|Plugin_|Codec_|RenderSystem_).*[.]so(?:[.][0-9]+)*$"
    ),
    "windows-x64-d3d11": re.compile(
        r"^(?:Ogre|Plugin_|Codec_|RenderSystem_).*[.]dll$",
        re.IGNORECASE,
    ),
}
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


def audit_plugin_symbol_ownership(
    plugin_raw: str,
    archive_strings: str,
    next_plugin_linkage: str,
) -> dict[str, object]:
    require(
        next_plugin_linkage in {"static", "dynamic"},
        "OgreNext plugin linkage is not explicit",
    )
    require(
        "RoROgreNext_dllStartPlugin" in plugin_raw
        and "RoROgreNext_dllStopPlugin" in plugin_raw,
        "prefixed plugin C exports are absent",
    )
    require(
        not re.search(r"\b_?dll(?:Start|Stop)Plugin\b", plugin_raw),
        "unprefixed plugin C export remains",
    )
    require(
        not re.search(
            r"(?<![A-Za-z0-9_])dll(?:Start|Stop)Plugin(?![A-Za-z0-9_])",
            archive_strings,
        ),
        "an unprefixed dynamic plugin lookup name remains",
    )
    prefixed_dynamic_lookup_names = (
        "RoROgreNext_dllStartPlugin" in archive_strings
        and "RoROgreNext_dllStopPlugin" in archive_strings
    )
    if next_plugin_linkage == "dynamic":
        require(
            prefixed_dynamic_lookup_names,
            "OgreNext Root did not compile the prefixed dynamic lookup names",
        )
    return {
        "linkage": next_plugin_linkage,
        "prefixed_c_exports": True,
        "unprefixed_exports_or_lookup_names": False,
        "dynamic_lookup_names": (
            "verified"
            if next_plugin_linkage == "dynamic"
            else "not_applicable_static"
        ),
    }


def is_toolchain_owned_global_collision(symbol: str) -> bool:
    return (
        symbol in DEFINED_GLOBAL_INTERSECTION_ALLOWLIST
        or ITANIUM_STD_SYMBOL_PATTERN.match(symbol) is not None
    )


def digest(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def classify_source_checkout(
    source_status: str, allow_dirty_source: bool
) -> dict[str, object]:
    canonical_status = source_status.strip()
    status_lines = canonical_status.splitlines() if canonical_status else []
    checkout_clean = not status_lines
    require(
        checkout_clean or allow_dirty_source,
        "exact source-commit evidence requires a clean checkout; "
        "dirty builds require the explicit development-only admission",
    )
    development_only = bool(allow_dirty_source)
    return {
        "clean": checkout_clean,
        "dirty_development_build_allowed": development_only,
        "porcelain_entry_count": len(status_lines),
        "porcelain_sha256": hashlib.sha256(
            canonical_status.encode("utf-8")
        ).hexdigest(),
        "qualification_eligible": checkout_clean and not development_only,
    }


def json_object(path: Path, label: str) -> dict[str, object]:
    def reject_duplicate_keys(
        pairs: list[tuple[str, object]],
    ) -> dict[str, object]:
        result: dict[str, object] = {}
        for key, value in pairs:
            require(key not in result, f"{label} contains duplicate key: {key}")
            result[key] = value
        return result

    try:
        payload = json.loads(
            path.read_text(encoding="utf-8"),
            object_pairs_hook=reject_duplicate_keys,
        )
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise RuntimeError(f"could not read {label}: {error}") from error
    require(isinstance(payload, dict), f"{label} is not a JSON object")
    return payload


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


def _gnu_defined_symbol_types(path: Path) -> dict[str, str]:
    raw = output("nm", "--defined-only", "--extern-only", str(path))
    symbols: dict[str, str] = {}
    for line in raw.splitlines():
        fields = line.split()
        if len(fields) < 2 or line.rstrip().endswith(":"):
            continue
        symbol_type = fields[-2]
        if re.fullmatch(r"[A-Za-z?]", symbol_type):
            symbols[fields[-1]] = symbol_type
    return symbols


def defined_global_symbols(path: Path, platform_policy: str) -> set[str]:
    """Return exact global definitions, excluding undefined imports."""
    if platform_policy != "macos-arm64-metal":
        return set(_gnu_defined_symbol_types(path))
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
    path: Path, definitions: set[str], platform_policy: str
) -> tuple[set[str], set[str]]:
    """Partition global definitions into weak and strong linkage."""
    if platform_policy != "macos-arm64-metal":
        symbol_types = _gnu_defined_symbol_types(path)
        weak_types = {"W", "V", "w", "v"}
        weak = {
            symbol for symbol in definitions
            if symbol_types.get(symbol) in weak_types
        }
        strong = definitions - weak
        return weak, strong
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


def require_strict_fp_compile_command(command: str, label: str) -> None:
    tokens = shlex.split(command)
    msvc_fp = [
        (index, token.lower())
        for index, token in enumerate(tokens)
        if token.lower().startswith("/fp:")
    ]
    if msvc_fp:
        require(
            msvc_fp[-1][1] == "/fp:strict",
            f"strict FP does not end with /fp:strict: {label}",
        )
        return
    no_fast_math = [
        index for index, token in enumerate(tokens)
        if token == "-fno-fast-math"
    ]
    fast_math = [
        index for index, token in enumerate(tokens)
        if token == "-ffast-math"
    ]
    require(no_fast_math, f"strict FP is missing -fno-fast-math: {label}")
    require(
        not fast_math or no_fast_math[-1] > fast_math[-1],
        f"strict FP does not override the final -ffast-math: {label}",
    )
    fp_contract = [
        (index, token)
        for index, token in enumerate(tokens)
        if token.startswith("-ffp-contract=")
    ]
    require(
        fp_contract and fp_contract[-1][1] == "-ffp-contract=off",
        f"strict FP does not end with -ffp-contract=off: {label}",
    )


def compile_entries_for_source(
    entries: list[dict[str, object]], source: Path
) -> list[dict[str, object]]:
    return [
        entry for entry in entries
        if Path(str(entry.get("file", ""))).resolve() == source.resolve()
    ]


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


def audit_compile_sources(
    entries: list[dict[str, object]],
    sources: list[Path],
    remap_header: Path,
    *,
    expect_remap: bool,
    require_entries: bool,
) -> list[dict[str, object]]:
    audited: list[dict[str, object]] = []
    for source in sources:
        matches = compile_entries_for_source(entries, source)
        if require_entries:
            require(matches, f"no compile entry for required source: {source}")
        if not matches:
            continue
        for entry in matches:
            command = command_text(entry)
            if expect_remap:
                require(
                    str(remap_header) in command,
                    f"OgreNext consumer lacks forced namespace remap: {source}",
                )
            else:
                require(
                    str(remap_header) not in command
                    and remap_header.name not in command
                    and "Ogre=RoROgreNext" not in command,
                    f"namespace remap leaked into neutral/OGRE14 source: {source}",
                )
        audited.append(
            {
                "path": str(source),
                "compile_entries": len(matches),
                "forced_remap": expect_remap,
            }
        )
    return audited


def validate_embedded_build_contract(
    contract: dict[str, object],
    lock: dict[str, object],
    source_root: Path,
    expected_source_commit: str,
    patch: Path,
    remap_header: Path,
) -> dict[str, object]:
    require(contract.get("schema_version") == 7,
            "embedded namespace audit requires build-contract schema 7")
    require(lock.get("schema_version") == 6,
            "embedded namespace audit requires canonical lock schema 6")
    ror_source = contract.get("ror_source")
    require(isinstance(ror_source, dict),
            "build contract has no RoR source identity")
    require(ror_source.get("commit") == expected_source_commit,
            "build-contract RoR source commit differs from audited commit")
    require(contract.get("patches") == lock.get("patches"),
            "build-contract base patch set differs from the canonical lock")

    lock_embedded = lock.get("embedded_namespace")
    require(isinstance(lock_embedded, dict),
            "canonical lock has no embedded namespace contract")
    lock_patch = lock_embedded.get("patch")
    lock_remap = lock_embedded.get("remap_header")
    require(isinstance(lock_patch, dict) and isinstance(lock_remap, dict),
            "canonical embedded namespace inputs are incomplete")
    try:
        patch_relative = patch.relative_to(source_root).as_posix()
        remap_relative = remap_header.relative_to(source_root).as_posix()
    except ValueError as error:
        raise RuntimeError(
            "embedded namespace input escaped the audited source root"
        ) from error
    require(patch_relative == f"tools/ogre_next_probe/{lock_patch.get('path')}",
            "audited embedded namespace patch path differs from the lock")
    require(remap_relative ==
            f"tools/ogre_next_probe/{lock_remap.get('path')}",
            "audited namespace remap path differs from the lock")
    require(digest(patch) == lock_patch.get("sha256"),
            "audited embedded namespace patch hash differs from the lock")
    require(digest(remap_header) == lock_remap.get("sha256"),
            "audited namespace remap hash differs from the lock")

    expected_embedded = {
        "enabled": True,
        "namespace": lock_embedded.get("namespace"),
        "cmake_option": lock_embedded.get("cmake_option"),
        "default_enabled": lock_embedded.get("default_enabled"),
        "patch": {
            **lock_patch,
            "applied": True,
        },
        "remap_header": {
            **lock_remap,
            "forced_include": True,
        },
        "full_n1_link_evidence": "not_evaluated",
    }
    require(contract.get("embedded_namespace") == expected_embedded,
            "build-contract embedded namespace identity is not canonical")
    return expected_embedded


def is_relative_to(path: Path, directory: Path) -> bool:
    try:
        path.relative_to(directory)
        return True
    except ValueError:
        return False


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--platform-policy",
        choices=tuple(LEGACY_RUNTIME_LIBRARY_PATTERNS),
        required=True,
    )
    parser.add_argument("--next-archive", action="append", required=True)
    parser.add_argument("--embedded-runtime-archive", required=True)
    parser.add_argument("--direct-contract-archive", required=True)
    parser.add_argument("--plugin-object", required=True)
    parser.add_argument(
        "--next-plugin-linkage",
        choices=("static", "dynamic"),
        required=True,
    )
    parser.add_argument("--legacy-main-library", required=True)
    parser.add_argument("--legacy-library", action="append", required=True)
    parser.add_argument(
        "--legacy-runtime-directory", action="append", default=[]
    )
    parser.add_argument("--executable", required=True)
    parser.add_argument("--compile-commands", required=True)
    parser.add_argument("--require-upstream-strict-fp", action="store_true")
    parser.add_argument("--next-source-root", required=True)
    parser.add_argument("--source-root", required=True)
    parser.add_argument("--expected-source-commit", required=True)
    parser.add_argument("--allow-dirty-source", action="store_true")
    parser.add_argument("--build-contract", required=True)
    parser.add_argument("--canonical-lock", required=True)
    parser.add_argument("--patch", required=True)
    parser.add_argument("--next-adapter", required=True)
    parser.add_argument("--legacy-adapter", required=True)
    parser.add_argument("--main-source", required=True)
    parser.add_argument("--session-adapter", required=True)
    parser.add_argument("--presenter-adapter", required=True)
    parser.add_argument(
        "--link-smoke-target-name",
        default="ror_ogre_next_dual_runtime_link_smoke",
    )
    parser.add_argument("--embedded-target-name", required=True)
    parser.add_argument("--embedded-source", action="append", required=True)
    parser.add_argument("--direct-target-name", required=True)
    parser.add_argument("--direct-source", action="append", required=True)
    parser.add_argument("--remap-header", required=True)
    parser.add_argument("--legacy-include", required=True)
    parser.add_argument("--namespaced-source", action="append", default=[])
    parser.add_argument(
        "--namespaced-source-if-present", action="append", default=[]
    )
    parser.add_argument("--neutral-source", action="append", default=[])
    parser.add_argument(
        "--neutral-source-if-present", action="append", default=[]
    )
    parser.add_argument(
        "--isolated-consumer-source", action="append", default=[]
    )
    parser.add_argument("--isolated-consumer-target-name")
    parser.add_argument("--stb-decoder-source")
    parser.add_argument("--stb-decoder-target-name")
    parser.add_argument("--report", required=True)
    args = parser.parse_args()
    legacy_runtime_library_pattern = LEGACY_RUNTIME_LIBRARY_PATTERNS[
        args.platform_policy
    ]

    report = Path(args.report)
    require(report.is_absolute(), "audit report path must be absolute")
    # A previous successful audit must not survive a later failed invocation.
    if report.exists() or report.is_symlink():
        report.unlink()

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
    presenter_adapter = exact_regular_file(
        args.presenter_adapter, "in-process presenter adapter source"
    )
    require(
        bool(args.stb_decoder_source) == bool(args.stb_decoder_target_name),
        "stb_image decoder audit arguments must be provided together",
    )
    stb_decoder_source = (
        exact_regular_file(args.stb_decoder_source, "stb_image decoder source")
        if args.stb_decoder_source else None
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
    observed_legacy_runtime_directories = {
        path.parent for path in legacy_libraries
    }
    if args.legacy_runtime_directory:
        legacy_runtime_directories = {
            exact_directory(value, "OGRE14 runtime directory")
            for value in args.legacy_runtime_directory
        }
        require(
            observed_legacy_runtime_directories == legacy_runtime_directories,
            "OGRE14 runtime inputs differ from the explicit directory closure",
        )
    else:
        require(
            len(observed_legacy_runtime_directories) == 1,
            "OGRE14 runtime libraries do not share one exact directory",
        )
        legacy_runtime_directories = observed_legacy_runtime_directories
    discovered_legacy_libraries = {
        candidate.resolve()
        for legacy_runtime_directory in legacy_runtime_directories
        for candidate in legacy_runtime_directory.iterdir()
        if candidate.is_file()
        and not candidate.is_symlink()
        and legacy_runtime_library_pattern.fullmatch(candidate.name)
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

    source_root = exact_directory(args.source_root, "RoR source root")
    build_contract = exact_regular_file(args.build_contract, "build contract")
    canonical_lock = exact_regular_file(args.canonical_lock, "canonical lock")
    patch = exact_regular_file(args.patch, "embedded namespace patch")
    namespaced_sources = [
        exact_regular_file(value, "required namespaced source")
        for value in args.namespaced_source
    ]
    neutral_sources = [
        exact_regular_file(value, "required neutral source")
        for value in args.neutral_source
    ]
    optional_namespaced_sources = [
        Path(value).resolve() for value in args.namespaced_source_if_present
    ]
    optional_neutral_sources = [
        Path(value).resolve() for value in args.neutral_source_if_present
    ]
    isolated_consumer_sources = [
        exact_regular_file(value, "isolated consumer source")
        for value in args.isolated_consumer_source
    ]
    require(
        not isolated_consumer_sources or args.isolated_consumer_target_name,
        "isolated consumer sources require an exact target name",
    )

    try:
        canonical_lock_relative = canonical_lock.relative_to(source_root)
    except ValueError as error:
        raise RuntimeError(
            "canonical lock escaped the audited source root"
        ) from error
    require(
        canonical_lock_relative.as_posix()
        == "tools/ogre_next_probe/ogre-next.lock.json",
        "canonical lock path differs from the reviewed source contract",
    )
    require(re.fullmatch(r"[0-9a-f]{40}", args.expected_source_commit) is not None,
            "expected source commit is not canonical")
    actual_source_commit = output(
        "git", "-C", str(source_root), "rev-parse", "HEAD"
    ).strip()
    require(actual_source_commit == args.expected_source_commit,
            "audited checkout differs from the expected source commit")
    source_status = output(
        "git", "-C", str(source_root), "status", "--porcelain=v1",
        "--untracked-files=all"
    ).strip()
    source_checkout = classify_source_checkout(
        source_status, args.allow_dirty_source
    )

    contract = json_object(build_contract, "build contract")
    lock = json_object(canonical_lock, "canonical lock")
    embedded_contract = validate_embedded_build_contract(
        contract,
        lock,
        source_root,
        args.expected_source_commit,
        patch,
        remap_header,
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
    require(
        "RoROgreNextRapidJson::" in next_demangled,
        "OgreNext archives contain no private RapidJSON namespace owner",
    )
    require(
        "rapidjson::" not in next_demangled,
        "an unremapped RapidJSON symbol remains in the OgreNext archives",
    )
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

    if args.platform_policy == "macos-arm64-metal":
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
    require("Ogre::" not in plugin_demangled,
            "plugin export probe references the legacy C++ namespace")

    archive_strings = "\n".join(output("strings", str(path)) for path in next_archives)
    plugin_symbol_ownership = audit_plugin_symbol_ownership(
        plugin_raw,
        archive_strings,
        args.next_plugin_linkage,
    )

    _, legacy_main_demangled = nm(legacy_main_library)
    require("Ogre::Root::getSingletonPtr()" in legacy_main_demangled,
            "the explicit legacy runtime lacks Ogre::Root")
    legacy_definitions: set[str] = set()
    legacy_weak_definitions: set[str] = set()
    legacy_strong_definitions: set[str] = set()
    legacy_library_reports: list[dict[str, object]] = []
    for legacy_library in legacy_libraries:
        _, legacy_demangled = nm(legacy_library)
        require(
            "RoROgreNext::" not in legacy_demangled,
            f"legacy runtime was modified by the namespace fork: {legacy_library}",
        )
        definitions = defined_global_symbols(
            legacy_library, args.platform_policy
        )
        require(
            definitions,
            f"legacy runtime has no global definitions to audit: {legacy_library}",
        )
        weak_definitions, strong_definitions = global_definition_linkages(
            legacy_library, definitions, args.platform_policy
        )
        legacy_definitions.update(definitions)
        legacy_weak_definitions.update(weak_definitions)
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
        modern_definitions = defined_global_symbols(
            archive, args.platform_policy
        )
        require(modern_definitions,
                f"the modern archive has no global definitions to audit: {archive}")
        intersection = modern_definitions & legacy_definitions
        toolchain_owned = {
            symbol for symbol in intersection
            if is_toolchain_owned_global_collision(symbol)
        }
        unexpected = intersection - toolchain_owned
        require(not unexpected,
                f"global definitions collide with OGRE14 in {archive}: " +
                ", ".join(sorted(unexpected)))
        modern_weak, modern_strong = global_definition_linkages(
            archive, modern_definitions, args.platform_policy
        )
        global_intersections.append({
            "modern_archive": str(archive),
            "modern_defined_globals": len(modern_definitions),
            "legacy_closure_defined_globals": len(legacy_definitions),
            "intersection": sorted(intersection),
            "toolchain_owned_intersections": sorted(toolchain_owned),
            "toolchain_owned_modern_weak": sorted(
                toolchain_owned & modern_weak
            ),
            "toolchain_owned_modern_strong": sorted(
                toolchain_owned & modern_strong
            ),
            "toolchain_owned_legacy_weak": sorted(
                toolchain_owned & legacy_weak_definitions
            ),
            "toolchain_owned_legacy_strong": sorted(
                toolchain_owned & legacy_strong_definitions
            ),
        })

    # OgreNext is linked statically with hidden visibility, so its resolved
    # symbols are local in the final executable. Inspect the complete symbol
    # table here; the archive/plugin rejection gates above remain global-only.
    _, executable_demangled = nm(executable, global_only=False)
    require("Ogre::Root::getSingletonPtr()" in executable_demangled and
            "RoROgreNext::Root::getSingletonPtr()" in executable_demangled,
            "dual-runtime executable does not resolve both Root ABI owners")
    require(
        "rapidjson::" in executable_demangled
        and "RoROgreNextRapidJson::" in executable_demangled,
        "dual-runtime executable does not resolve isolated host and OgreNext RapidJSON owners",
    )
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
    require(
        "RoR::RendererOgreNextInProcessPresenter::RendererOgreNextInProcessPresenter" in
        executable_demangled
        and "RoR::RendererOgreNextInProcessPresenter::~RendererOgreNextInProcessPresenter" in
        executable_demangled,
        "dual-runtime executable does not contain the concrete presenter lifecycle",
    )
    if args.platform_policy == "macos-arm64-metal":
        linked_libraries = output("otool", "-L", str(executable))
    elif args.platform_policy == "linux-x86_64-vulkan":
        linked_libraries = output("readelf", "-d", str(executable))
    else:
        linked_libraries = output("dumpbin", "/DEPENDENTS", str(executable))
    require(legacy_main_library.name in linked_libraries,
            "dual-runtime executable has no load command for OGRE14")

    payload = json.loads(compile_commands.read_text(encoding="utf-8"))
    require(isinstance(payload, list), "compile_commands.json is not an array")
    entries = [entry for entry in payload if isinstance(entry, dict)]
    stb_implementation_report = None
    if stb_decoder_source is not None:
        stb_target_token = f"CMakeFiles/{args.stb_decoder_target_name}.dir/"
        stb_target_entries = [
            entry for entry in entries
            if stb_target_token in command_text(entry)
            and Path(str(entry.get("file", ""))).suffix.lower()
                in {".c", ".cc", ".cpp", ".cxx", ".m", ".mm"}
        ]
        require(stb_target_entries, "stb_image decoder target has no compile entries")
        stb_implementation_entries: list[dict[str, object]] = []
        for entry in stb_target_entries:
            entry_source = Path(str(entry.get("file", "")))
            require(
                entry_source.is_absolute()
                and entry_source.is_file()
                and not entry_source.is_symlink(),
                f"stb_image target compile source is missing or indirect: {entry_source}",
            )
            command = command_text(entry)
            injected_configuration = sorted(
                token for token in shlex.split(command)
                if STB_IMAGE_COMMAND_CONFIGURATION_PATTERN.search(token)
            )
            require(
                not injected_configuration,
                "stb_image configuration leaked through a compile definition: "
                + ", ".join(injected_configuration),
            )
            if entry_source.resolve() == stb_decoder_source:
                indirect_inputs = sorted(
                    token for token in shlex.split(command)
                    if token.startswith(STB_IMAGE_INDIRECT_INPUT_PREFIXES)
                )
                require(
                    not indirect_inputs,
                    "stb_image decoder compile command uses an unaudited indirect input: "
                    + ", ".join(indirect_inputs),
                )
            if STB_IMAGE_IMPLEMENTATION_PATTERN.search(entry_source.read_bytes()):
                stb_implementation_entries.append(entry)
        require(
            len(stb_implementation_entries) == 1,
            "combined target must compile exactly one stb_image implementation owner",
        )
        stb_implementation_entry = stb_implementation_entries[0]
        require(
            Path(str(stb_implementation_entry.get("file", ""))).resolve()
            == stb_decoder_source,
            "combined target stb_image implementation owner is not the reviewed decoder",
        )
        stb_decoder_command = command_text(stb_implementation_entry)
        require_strict_fp_compile_command(
            stb_decoder_command, str(stb_decoder_source)
        )
        stb_implementation_report = {
            "source": str(stb_decoder_source),
            "target": str(args.stb_decoder_target_name),
            "target_compile_entries": len(stb_target_entries),
            "implementation_compile_entries": 1,
            "compile_command_sha256": hashlib.sha256(
                stb_decoder_command.encode("utf-8")
            ).hexdigest(),
            "external_configuration_tokens": [],
            "indirect_input_tokens": [],
            "strict_fp": True,
        }
    upstream_entries = [
        entry for entry in entries
        if Path(str(entry.get("file", ""))).suffix.lower() in CPP_SUFFIXES
        and is_relative_to(
            Path(str(entry.get("file", ""))).resolve(), next_source_root
        )
    ]
    require(upstream_entries, "no OgreNext C++/Objective-C++ compile entries found")
    for entry in upstream_entries:
        command = command_text(entry)
        require(str(remap_header) in command,
                f"OgreNext source lacks forced namespace remap: {entry.get('file')}")
        if args.require_upstream_strict_fp:
            require_strict_fp_compile_command(
                command, str(entry.get("file", ""))
            )

    namespaced_audit = audit_compile_sources(
        entries,
        namespaced_sources,
        remap_header,
        expect_remap=True,
        require_entries=True,
    )
    namespaced_audit.extend(
        audit_compile_sources(
            entries,
            optional_namespaced_sources,
            remap_header,
            expect_remap=True,
            require_entries=False,
        )
    )
    neutral_audit = audit_compile_sources(
        entries,
        neutral_sources,
        remap_header,
        expect_remap=False,
        require_entries=True,
    )
    neutral_audit.extend(
        audit_compile_sources(
            entries,
            optional_neutral_sources,
            remap_header,
            expect_remap=False,
            require_entries=False,
        )
    )

    next_command = command_text(one_compile_entry(entries, next_adapter))
    legacy_command = command_text(one_compile_entry(entries, legacy_adapter))
    main_command = command_text(one_compile_entry(entries, main_source))
    session_command = command_text(
        one_target_compile_entry(
            entries, session_adapter, args.link_smoke_target_name
        )
    )
    presenter_adapter_command = command_text(
        one_target_compile_entry(
            entries, presenter_adapter, args.link_smoke_target_name
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
    require(
        str(remap_header) not in presenter_adapter_command
        and "Ogre=RoROgreNext" not in presenter_adapter_command,
        "the fork remap leaked into the renderer-neutral presenter adapter",
    )
    require(str(legacy_include) not in next_command,
            "the OgreNext adapter sees OGRE14 headers")
    require(str(next_source_root) not in legacy_command,
            "the OGRE14 adapter sees OgreNext headers")

    isolated_consumer_audit: list[dict[str, object]] = []
    for source in isolated_consumer_sources:
        command = command_text(
            one_target_compile_entry(
                entries, source, str(args.isolated_consumer_target_name)
            )
        )
        require(
            str(next_source_root) not in command
            and str(remap_header) not in command
            and remap_header.name not in command
            and "Ogre=RoROgreNext" not in command,
            f"OgreNext compile usage leaked through the provider facade: {source}",
        )
        isolated_consumer_audit.append(
            {
                "path": str(source),
                "target": str(args.isolated_consumer_target_name),
                "ogre_next_usage_leaked": False,
            }
        )

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
        "platform_policy": args.platform_policy,
        "namespace": "RoROgreNext",
        "rapidjson_namespace": "RoROgreNextRapidJson",
        "ror_source_commit": actual_source_commit,
        "source_checkout": source_checkout,
        "canonical_lock": {
            "path": str(canonical_lock),
            "sha256": digest(canonical_lock),
        },
        "build_contract": {
            "path": str(build_contract),
            "sha256": digest(build_contract),
            "embedded_namespace": embedded_contract,
        },
        "embedded_namespace_inputs": {
            "patch": {"path": str(patch), "sha256": digest(patch)},
            "remap_header": {
                "path": str(remap_header),
                "sha256": digest(remap_header),
            },
        },
        "upstream_compile_entries": len(upstream_entries),
        "upstream_strict_fp_required": args.require_upstream_strict_fp,
        "upstream_strict_fp_compile_entries": (
            len(upstream_entries) if args.require_upstream_strict_fp else 0
        ),
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
        "plugin_symbol_ownership": plugin_symbol_ownership,
        "legacy_main_library": {
            "path": str(legacy_main_library),
            "sha256": digest(legacy_main_library),
        },
        "legacy_runtime_libraries": legacy_library_reports,
        "legacy_runtime_directory": (
            str(next(iter(legacy_runtime_directories)))
            if len(legacy_runtime_directories) == 1
            else None
        ),
        "legacy_runtime_directories": sorted(
            str(path) for path in legacy_runtime_directories
        ),
        "executable": {"path": str(executable), "sha256": digest(executable)},
        "compile_commands_sha256": digest(compile_commands),
        "stb_image_implementation": stb_implementation_report,
        "defined_global_intersections": global_intersections,
        "translation_unit_isolation": {
            "next_adapter_forced_remap": True,
            "ogre14_adapter_forced_remap": False,
            "main_forced_remap": False,
            "session_adapter_forced_remap": False,
            "presenter_adapter_forced_remap": False,
            "embedded_n1_sources_forced_remap": len(embedded_commands),
            "direct_contract_sources_forced_remap": 0,
            "direct_contract_sources": len(direct_commands),
            "namespaced_sources": namespaced_audit,
            "neutral_sources": neutral_audit,
            "isolated_consumers": isolated_consumer_audit,
        },
        "evidence_scope": {
            "namespace_and_dual_root_link": True,
            "rapidjson_namespace_and_dual_owner_link": True,
            "full_n1_runtime_link": True,
            "renderer_neutral_in_process_session_link": True,
            "concrete_in_process_presenter_link": True,
        },
    }
    report.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n",
                      encoding="utf-8")
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
