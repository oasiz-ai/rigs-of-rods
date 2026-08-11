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
        "RendererFrontendTransportDispatcher",
        "RendererOgre14GameBridge",
        "RendererOgre14GameHostSession",
        "RendererOgre14InputAdapter",
        "RendererOgre14ProductSession",
        "RendererOgreNextLiveSession",
        "RendererPackageRuntimeProbe",
        "RendererPackagedMediaPath",
        "RendererSiblingPath",
        "RendererStartupHandoff",
        "RendererStartupPlan",
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
)

REQUIRED_SDL_PROVIDER_SYMBOL_TOKENS = (
    "SDL_Init",
    "SDL_PollEvent",
    "SDL_CreateWindow",
)

REVIEWED_GLOBAL_INTERSECTION_ALLOWLIST = (
    "__ZNSt3__119piecewise_constructE",
)


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

        completed = subprocess.run(
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
        if completed.returncode != 0:
            raise ValueError(
                f"nm failed ({completed.returncode}): {completed.stderr.strip()}"
            )
        symbols = completed.stdout
        symbol_violations = sorted(
            token for token in FORBIDDEN_SYMBOL_TOKENS if token in symbols
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
        missing_symbol_evidence = sorted(
            token
            for token in REQUIRED_SYMBOL_TOKENS
            if token not in all_defined.stdout
        )

        # Apple linker maps include the raw bytes of literal strings. Those bytes
        # are not required to be UTF-8, so keep this evidence byte-exact and only
        # encode the ASCII/UTF-8 authority tokens being searched for.
        link_map_payload = link_map.read_bytes()
        object_violations = sorted(
            token
            for token in FORBIDDEN_LINK_MAP_OBJECT_TOKENS
            if _link_map_contains(link_map_payload, token)
        )
        extracted_sdl_members = (
            _link_map_contains(link_map_payload, "libSDL2.a(")
            or _link_map_contains(link_map_payload, "libSDL2.a[")
        )
        missing_archive_evidence = sorted(
            str(archive)
            for archive in required_archives
            if not _link_map_contains(link_map_payload, archive.name)
        )

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
        missing_ogre14_dylib_evidence = sorted(
            str(dylib)
            for dylib in required_ogre14_dylibs
            if dylib.name not in linked_dylibs.stdout
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
        missing_sdl_provider_symbols = sorted(
            token
            for token in REQUIRED_SDL_PROVIDER_SYMBOL_TOKENS
            if token not in sdl_provider_symbols.stdout
        )

        if (
            symbol_violations
            or object_violations
            or extracted_sdl_members
            or missing_symbol_evidence
            or missing_archive_evidence
            or missing_ogre14_dylib_evidence
            or missing_sdl_provider_symbols
        ):
            details = ", ".join(
                symbol_violations
                + object_violations
                + missing_symbol_evidence
                + missing_archive_evidence
                + missing_ogre14_dylib_evidence
                + missing_sdl_provider_symbols
            )
            if extracted_sdl_members:
                details = f"{details}, extracted libSDL2.a member".lstrip(", ")
            raise ValueError(f"RoR-Combined closure evidence failed: {details}")

        symbol_lines = [line for line in symbols.splitlines() if line.strip()]
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
                },
                "provider_source_manifest": provider_manifest_report,
                "selected_game_source_manifest": selected_manifest_report,
                "defined_external_symbol_count": len(symbol_lines),
                "all_defined_symbol_count": len(all_defined_symbol_lines),
                "forbidden_symbol_tokens": list(FORBIDDEN_SYMBOL_TOKENS),
                "forbidden_link_map_object_tokens": list(
                    FORBIDDEN_LINK_MAP_OBJECT_TOKENS
                ),
                "bridge_or_transport_symbols_present": False,
                "bridge_or_transport_objects_present": False,
                "required_symbol_tokens": list(REQUIRED_SYMBOL_TOKENS),
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
                },
                "root_sdl_static_archive_members_extracted": False,
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
