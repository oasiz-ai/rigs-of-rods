#!/usr/bin/env python3
"""Fail closed unless Conan produced the reviewed TSan OpenAL Soft archive."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
from typing import Any, Iterable, Mapping, Optional, Sequence


SCHEMA = "ror.openal-soft-tsan-package-audit@1"
PACKAGE_NAME = "openal-soft"
PACKAGE_VERSION = "1.24.3"
RECIPE_REVISION = "47d7f9d8acb249fbdab9d93428361ce0"
PACKAGE_ID = "f51c6d09f85236f2cfdcd402c3d5a0cdae6158da"
RECIPE_REFERENCE = f"{PACKAGE_NAME}/{PACKAGE_VERSION}#{RECIPE_REVISION}"
PACKAGE_REFERENCE = f"{RECIPE_REFERENCE}:{PACKAGE_ID}"
SOURCE_ARCHIVE_SHA256 = (
    "cb5e6197a1c0da0edcf2a81024953cc8fa8545c3b9474e48c852af709d587892"
)
RECIPE_SHA256 = "14d760cc0e2ea750d2144ae63d2bdacfbc644ac8da0c0ad6a1b13a11a8aeb573"
CONANDATA_SHA256 = "d803c3fbda374ee555c121a14c981a8c065c504dc65a35a5531e3d42e1b4d1f1"
LOCK_SHA256 = "7c01ab8fce221f221001d36ba886593e4f2231a923683935e8939790d9c4395d"
NORMAL_PACKAGE_ID = "7b08ab0814dd037bac5a06c5ba689c48d12f5422"
EXPECTED_SETTINGS = {
    "arch": "x86_64",
    "build_type": "Release",
    "compiler": "gcc",
    "compiler.libcxx": "libstdc++11",
    "compiler.version": "11",
    "os": "Linux",
}
EXPECTED_OPTIONS = {
    "fPIC": "True",
    "shared": "False",
    "thread_sanitizer": "True",
}
REQUIRED_OBJECT_CONTRACTS = (
    (
        "panning-and-filters",
        "alu.cpp.o",
        r"^\(anonymous namespace\)::CalcPanningAndFilters\(.*DeviceBase\*\)$",
    ),
    (
        "context-voice-allocation",
        "context.cpp.o",
        r"^ContextBase::allocVoices\(unsigned long\)$",
    ),
    (
        "source-play",
        "source.cpp.o",
        r"^alSourcePlayDirect$",
    ),
    (
        "voice-mix",
        "voice.cpp.o",
        r"^Voice::mix\(.*unsigned int\)$",
    ),
    (
        "null-backend-mixer",
        "null.cpp.o",
        r"^\(anonymous namespace\)::NullBackend::mixerProc\(\)$",
    ),
)
MEMORY_SYMBOL_PREFIXES = (
    "__tsan_read",
    "__tsan_write",
    "__tsan_unaligned",
    "__tsan_atomic",
)


class AuditError(RuntimeError):
    """The package does not satisfy the reviewed full-runtime TSan contract."""


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _run_checked(
    command: Sequence[str],
    *,
    environment: Mapping[str, str],
    cwd: Optional[Path] = None,
) -> str:
    result = subprocess.run(
        list(command),
        check=False,
        capture_output=True,
        text=True,
        env=dict(environment),
        cwd=str(cwd) if cwd is not None else None,
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip() or "no output"
        raise AuditError(
            f"command failed ({result.returncode}): {' '.join(command)}: {detail}"
        )
    return result.stdout


def _require_hash(path: Path, expected: str, label: str) -> None:
    if not path.is_file():
        raise AuditError(f"{label} is missing: {path}")
    if path.is_symlink():
        raise AuditError(f"{label} must not be a symlink: {path}")
    actual = _sha256_file(path)
    if actual != expected:
        raise AuditError(
            f"{label} bytes changed: expected SHA-256 {expected}, got {actual}"
        )


def _validate_repository_inputs(repository_root: Path) -> None:
    recipe_root = repository_root / "cmake/conan/recipes/openal-soft"
    _require_hash(recipe_root / "conanfile.py", RECIPE_SHA256, "OpenAL recipe")
    _require_hash(
        recipe_root / "conandata.yml", CONANDATA_SHA256, "OpenAL conandata"
    )
    _require_hash(
        repository_root / "cmake/conan/locks/ror-ogre14-linux-x86_64-tsan.lock",
        LOCK_SHA256,
        "OpenAL TSan dependency lock",
    )
    conandata = (recipe_root / "conandata.yml").read_text(encoding="utf-8")
    if SOURCE_ARCHIVE_SHA256 not in conandata:
        raise AuditError("OpenAL conandata lost the reviewed source archive digest")


def _conan_package(document: Mapping[str, Any]) -> Mapping[str, Any]:
    try:
        recipe = document["Local Cache"][f"{PACKAGE_NAME}/{PACKAGE_VERSION}"]
        revision = recipe["revisions"][RECIPE_REVISION]
        package = revision["packages"][PACKAGE_ID]
    except (KeyError, TypeError) as error:
        raise AuditError(
            "Conan cache does not contain the exact reviewed OpenAL recipe/package"
        ) from error
    if not isinstance(package, Mapping):
        raise AuditError("invalid Conan package listing")
    return package


def _parse_conan_package_revision(document: Mapping[str, Any]) -> str:
    package = _conan_package(document)
    revisions = package.get("revisions")
    if not isinstance(revisions, Mapping) or len(revisions) != 1:
        raise AuditError("expected exactly one OpenAL package revision in the cache")
    package_revision, revision_document = next(iter(revisions.items()))
    if not re.fullmatch(r"[0-9a-f]{32}", str(package_revision)):
        raise AuditError(f"invalid Conan package revision: {package_revision!r}")
    if not isinstance(revision_document, Mapping):
        raise AuditError("invalid Conan package revision document")
    return str(package_revision)


def _parse_conan_package_info(document: Mapping[str, Any]) -> Mapping[str, Any]:
    package = _conan_package(document)
    info = package.get("info")
    if not isinstance(info, Mapping):
        raise AuditError("Conan package listing omitted settings/options information")
    return info


def _normalise_mapping(value: Any, label: str) -> dict[str, str]:
    if not isinstance(value, Mapping):
        raise AuditError(f"Conan package {label} are missing")
    return {str(key): str(item) for key, item in value.items()}


def _validate_package_info(info: Mapping[str, Any]) -> None:
    settings = _normalise_mapping(info.get("settings"), "settings")
    options = _normalise_mapping(info.get("options"), "options")
    if settings != EXPECTED_SETTINGS:
        raise AuditError(
            f"OpenAL package settings changed: expected {EXPECTED_SETTINGS}, got {settings}"
        )
    if options != EXPECTED_OPTIONS:
        raise AuditError(
            f"OpenAL package options changed: expected {EXPECTED_OPTIONS}, got {options}"
        )
    if PACKAGE_ID == NORMAL_PACKAGE_ID:
        raise AuditError("TSan OpenAL unexpectedly reused the normal package identity")


def _audit_conan_graph(
    repository_root: Path,
    environment: Mapping[str, str],
    package_revision: str,
) -> tuple[dict[str, Any], str]:
    profile = repository_root / "cmake/conan/profiles/linux-x86_64-release"
    lock = repository_root / "cmake/conan/locks/ror-ogre14-linux-x86_64-tsan.lock"
    graph_text = _run_checked(
        [
            "conan",
            "graph",
            "info",
            ".",
            f"--profile:host={profile}",
            f"--profile:build={profile}",
            "-o=&:ogre14=True",
            "-o=openal-soft/*:thread_sanitizer=True",
            f"--lockfile={lock}",
            '-c:h=tools.cmake:configure_args=["-DCMAKE_POLICY_VERSION_MINIMUM=3.5"]',
            "--format=json",
        ],
        environment=environment,
        cwd=repository_root,
    )
    try:
        document = json.loads(graph_text)
        nodes = document["graph"]["nodes"]
    except (json.JSONDecodeError, KeyError, TypeError) as error:
        raise AuditError("Conan returned malformed TSan graph JSON") from error
    if not isinstance(nodes, Mapping):
        raise AuditError("Conan TSan graph nodes are not an object")
    matches = [
        node
        for node in nodes.values()
        if isinstance(node, Mapping) and node.get("name") == PACKAGE_NAME
    ]
    if len(matches) != 1:
        raise AuditError(
            f"TSan graph must contain exactly one OpenAL node, found {len(matches)}"
        )
    node = matches[0]
    expected_scalars = {
        "ref": RECIPE_REFERENCE,
        "rrev": RECIPE_REVISION,
        "package_id": PACKAGE_ID,
        "prev": package_revision,
        "binary": "Cache",
        "context": "host",
    }
    for field, expected in expected_scalars.items():
        if node.get(field) != expected:
            raise AuditError(
                f"TSan graph OpenAL {field} changed: expected {expected!r}, "
                f"got {node.get(field)!r}"
            )
    _validate_package_info(node)
    return (
        {
            "verified": True,
            "binary": "Cache",
            "context": "host",
            "openal_node_count": 1,
        },
        graph_text,
    )


def _ensure_within(path: Path, parent: Path, label: str) -> None:
    try:
        common = Path(os.path.commonpath((str(path), str(parent))))
    except ValueError as error:
        raise AuditError(f"{label} is outside the isolated Conan home") from error
    if common != parent:
        raise AuditError(f"{label} is outside the isolated Conan home: {path}")


def _parse_members(output: str) -> list[str]:
    members = [line.strip() for line in output.splitlines() if line.strip()]
    if not members:
        raise AuditError("OpenAL archive contains no object members")
    return members


def _undefined_symbol_set(output: str) -> set[str]:
    symbols = set(
        match.group(1)
        for line in output.splitlines()
        if (match := re.search(r"(?:^|\s)U\s+(\S+)(?:\s|$)", line))
    )
    if not symbols:
        raise AuditError("nm reported no undefined symbols for an extracted object")
    return symbols


def _defined_symbols(output: str) -> tuple[str, ...]:
    symbols: list[str] = []
    pattern = re.compile(r"^(?:.*?):[0-9a-fA-F]+\s+([Tt])\s+(.+)$")
    for line in output.splitlines():
        match = pattern.match(line.strip())
        if match:
            symbols.append(match.group(2))
    if not symbols:
        raise AuditError("nm reported no defined text symbols for an extracted object")
    return tuple(symbols)


def _audit_selected_object(
    ar: str,
    nm: str,
    archive: Path,
    members: Iterable[str],
    environment: Mapping[str, str],
    *,
    member_name: str,
    entrypoint_pattern: str,
) -> dict[str, Any]:
    occurrences = sum(
        1 for member in members if Path(member).name == member_name
    )
    if occurrences < 1:
        raise AuditError(f"{member_name} is missing from libopenal.a")
    candidates: list[dict[str, Any]] = []
    with tempfile.TemporaryDirectory(prefix="ror-openal-tsan-object-") as temp:
        root = Path(temp)
        for ordinal in range(1, occurrences + 1):
            extract_root = root / str(ordinal)
            extract_root.mkdir()
            _run_checked(
                [ar, "xN", str(ordinal), str(archive), member_name],
                environment=environment,
                cwd=extract_root,
            )
            extracted = extract_root / member_name
            if not extracted.is_file():
                raise AuditError(
                    f"GNU ar did not extract {member_name} occurrence {ordinal}"
                )
            symbols_text = _run_checked(
                [nm, "--demangle", "--print-file-name", str(extracted)],
                environment=environment,
            )
            matching_entrypoints = sorted(
                symbol
                for symbol in _defined_symbols(symbols_text)
                if re.search(entrypoint_pattern, symbol)
            )
            if not matching_entrypoints:
                continue
            undefined_text = _run_checked(
                [nm, "--print-file-name", "--undefined-only", str(extracted)],
                environment=environment,
            )
            undefined = _undefined_symbol_set(undefined_text)
            memory_symbols = sorted(
                symbol
                for symbol in undefined
                if symbol.startswith(MEMORY_SYMBOL_PREFIXES)
            )
            candidates.append(
                {
                    "archive_member": member_name,
                    "archive_member_occurrences": occurrences,
                    "archive_member_ordinal": ordinal,
                    "entrypoint_pattern": entrypoint_pattern,
                    "matching_defined_symbol_count": len(matching_entrypoints),
                    "function_entry": "__tsan_func_entry" in undefined,
                    "memory_symbols": memory_symbols,
                }
            )
    if len(candidates) != 1:
        raise AuditError(
            f"expected exactly one {member_name} object to define a symbol matching "
            f"{entrypoint_pattern!r}, found {len(candidates)}"
        )
    evidence = candidates[0]
    if not evidence["function_entry"]:
        raise AuditError(
            f"the exact {member_name} object matching {entrypoint_pattern!r} "
            "lacks TSan entry hooks"
        )
    if not evidence["memory_symbols"]:
        raise AuditError(
            f"the exact {member_name} object matching {entrypoint_pattern!r} "
            "lacks TSan memory-access hooks"
        )
    return evidence


def _audit_build_linkage(
    build_dir: Path,
    package_root: Path,
    archive: Path,
    environment: Mapping[str, str],
) -> tuple[dict[str, Any], str]:
    build_dir = build_dir.resolve(strict=True)
    if not build_dir.is_dir():
        raise AuditError(f"RoR build directory is not a directory: {build_dir}")
    generated = (
        build_dir / "conan/generators/OpenAL-release-x86_64-data.cmake"
    )
    if not generated.is_file() or generated.is_symlink():
        raise AuditError(
            "the exact Linux OpenAL Conan generator data file is missing or symlinked"
        )
    generated_text = generated.read_text(encoding="utf-8")
    package_matches = re.findall(
        r'^set\(openal-soft_PACKAGE_FOLDER_RELEASE "([^"]+)"\)$',
        generated_text,
        re.MULTILINE,
    )
    if len(package_matches) != 1:
        raise AuditError("OpenAL Conan generator has no unique release package folder")
    generated_package = Path(package_matches[0]).resolve(strict=True)
    if generated_package != package_root:
        raise AuditError(
            "RoR's OpenAL generator did not resolve the audited Conan package"
        )
    ninja = shutil.which("ninja")
    if ninja is None:
        raise AuditError("Ninja is required to audit the RoR-Combined link closure")
    commands_text = _run_checked(
        [ninja, "-C", str(build_dir), "-t", "commands", "RoR-Combined"],
        environment=environment,
    )
    archive_reference_count = commands_text.count(str(archive))
    if archive_reference_count != 1:
        raise AuditError(
            "the RoR-Combined build commands must reference the exact audited "
            f"OpenAL archive once, found {archive_reference_count} references"
        )
    executable = build_dir / "bin/RoR-Combined"
    if not executable.is_file() or executable.is_symlink():
        raise AuditError("the linked RoR-Combined executable is missing or symlinked")
    return (
        {
            "target": "RoR-Combined",
            "conan_generator": (
                "conan/generators/OpenAL-release-x86_64-data.cmake"
            ),
            "generator_package_matches_audited_package": True,
            "link_command_archive_reference_count": archive_reference_count,
            "linked_executable": "bin/RoR-Combined",
        },
        commands_text,
    )


def _write_text_exclusive(path: Path, content: str) -> None:
    with path.open("x", encoding="utf-8", newline="\n") as stream:
        stream.write(content)


def audit(
    conan_home: Path,
    build_dir: Path,
    artifact_dir: Path,
    repository_root: Path,
) -> Path:
    conan_home = conan_home.resolve(strict=True)
    if not conan_home.is_dir():
        raise AuditError(f"Conan home is not a directory: {conan_home}")
    repository_root = repository_root.resolve(strict=True)
    _validate_repository_inputs(repository_root)

    artifact_dir = artifact_dir.resolve()
    if artifact_dir.exists():
        if not artifact_dir.is_dir() or any(artifact_dir.iterdir()):
            raise AuditError(f"audit artifact directory must be absent or empty: {artifact_dir}")
    else:
        artifact_dir.mkdir(parents=True)

    environment = os.environ.copy()
    environment["CONAN_HOME"] = str(conan_home)
    revisions_text = _run_checked(
        ["conan", "list", f"{PACKAGE_REFERENCE}#*", "--format=json"],
        environment=environment,
    )
    info_text = _run_checked(
        ["conan", "list", f"{RECIPE_REFERENCE}:*", "--format=json"],
        environment=environment,
    )
    try:
        revisions_listing = json.loads(revisions_text)
        info_listing = json.loads(info_text)
    except json.JSONDecodeError as error:
        raise AuditError("Conan returned malformed package JSON") from error
    package_revision = _parse_conan_package_revision(revisions_listing)
    package_info = _parse_conan_package_info(info_listing)
    _validate_package_info(package_info)
    graph_evidence, graph_text = _audit_conan_graph(
        repository_root, environment, package_revision
    )

    package_path_text = _run_checked(
        ["conan", "cache", "path", PACKAGE_REFERENCE], environment=environment
    ).strip()
    package_root = Path(package_path_text).resolve(strict=True)
    _ensure_within(package_root, conan_home, "OpenAL package")
    archive_link = package_root / "lib/libopenal.a"
    if archive_link.is_symlink():
        raise AuditError(f"OpenAL static archive must not be a symlink: {archive_link}")
    archive = archive_link.resolve(strict=True)
    _ensure_within(archive, package_root, "OpenAL static archive")
    if not archive.is_file():
        raise AuditError(f"OpenAL static archive is missing: {archive}")

    ar = shutil.which("ar")
    nm = shutil.which("nm")
    if ar is None or nm is None:
        raise AuditError("GNU ar and nm are required for the OpenAL TSan audit")
    members_text = _run_checked([ar, "t", str(archive)], environment=environment)
    nm_text = _run_checked(
        [nm, "--print-file-name", "--undefined-only", str(archive)],
        environment=environment,
    )
    members = _parse_members(members_text)
    instrumentation = {
        receipt_key: _audit_selected_object(
            ar,
            nm,
            archive,
            members,
            environment,
            member_name=member_name,
            entrypoint_pattern=entrypoint_pattern,
        )
        for receipt_key, member_name, entrypoint_pattern in REQUIRED_OBJECT_CONTRACTS
    }
    linkage, link_commands = _audit_build_linkage(
        build_dir, package_root, archive, environment
    )

    archive_sha256 = _sha256_file(archive)
    receipt = {
        "schema": SCHEMA,
        "source": {
            "name": PACKAGE_NAME,
            "version": PACKAGE_VERSION,
            "archive_sha256": SOURCE_ARCHIVE_SHA256,
            "recipe_sha256": RECIPE_SHA256,
            "conandata_sha256": CONANDATA_SHA256,
            "dependency_lock_sha256": LOCK_SHA256,
        },
        "conan": {
            "recipe_reference": RECIPE_REFERENCE,
            "recipe_revision": RECIPE_REVISION,
            "package_id": PACKAGE_ID,
            "package_revision": package_revision,
            "normal_package_id_reused": False,
            "settings": EXPECTED_SETTINGS,
            "options": EXPECTED_OPTIONS,
            "graph": graph_evidence,
        },
        "archive": {
            "relative_path": "lib/libopenal.a",
            "sha256": archive_sha256,
            "size_bytes": archive.stat().st_size,
            "member_count": len(members),
            "required_instrumentation": instrumentation,
        },
        "ror_combined_linkage": linkage,
        "qualification": "sanitizer-dependency-evidence-only",
    }
    _write_text_exclusive(
        artifact_dir / "conan-package-revisions.json", revisions_text
    )
    _write_text_exclusive(artifact_dir / "conan-package-info.json", info_text)
    _write_text_exclusive(artifact_dir / "ror-tsan-conan-graph.json", graph_text)
    _write_text_exclusive(
        artifact_dir / "libopenal.a.members.txt", members_text
    )
    _write_text_exclusive(
        artifact_dir / "libopenal.a.nm-undefined.txt", nm_text
    )
    _write_text_exclusive(
        artifact_dir / "RoR-Combined.ninja-commands.txt", link_commands
    )
    receipt_path = artifact_dir / "receipt.json"
    _write_text_exclusive(
        receipt_path, json.dumps(receipt, indent=2, sort_keys=True) + "\n"
    )
    return receipt_path


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--conan-home", required=True, type=Path, help="isolated Conan cache root"
    )
    parser.add_argument(
        "--build-dir",
        required=True,
        type=Path,
        help="configured and linked RoR-Combined Ninja build directory",
    )
    parser.add_argument(
        "--artifact-dir",
        required=True,
        type=Path,
        help="empty directory for the canonical audit receipt and command evidence",
    )
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = _build_parser().parse_args(argv)
    repository_root = Path(__file__).resolve().parents[1]
    try:
        receipt = audit(
            args.conan_home, args.build_dir, args.artifact_dir, repository_root
        )
    except (AuditError, OSError) as error:
        print(f"OpenAL TSan package audit failed: {error}", file=sys.stderr)
        return 1
    print(receipt)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
