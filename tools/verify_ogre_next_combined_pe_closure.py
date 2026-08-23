#!/usr/bin/env python3
"""Fail closed on the Windows PE closure of the visible Ogre-Next game."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

import verify_ogre_next_combined_binary_closure as common


SCHEMA = "ror.ogre_next_combined_pe_closure.v1"
PLATFORM_POLICY = "windows-x64-d3d11"

REQUIRED_PE_SYMBOL_TOKENS = (
    "RendererOgreNextInProcessPresenter",
    "RendererInProcessSession",
    "RendererFrontendDirectDispatcher",
    "RendererOgreNextSdlWindowRuntime",
    "OgreNextN1Frontend",
    "OgreNextDisplayDomainUnlit",
    "DecodeOgre14SourceTexture",
    "OgreNextDemoMaterialSource",
)

FORBIDDEN_PE_OBJECT_TOKENS = tuple(
    token.removesuffix(".cpp.o") + ".cpp.obj"
    for token in common.FORBIDDEN_LINK_MAP_OBJECT_TOKENS
)

FORBIDDEN_ROOT_CODEC_ARCHIVES = (
    "libpng.lib",
    "libpng16.lib",
    "png.lib",
    "png16.lib",
    "jpeg.lib",
    "libjpeg.lib",
    "jpeg-static.lib",
)

REVIEWED_ROOT_PNG_ARCHIVE = "libpng16_static.lib"

REQUIRED_STATIC_SDL_SYMBOL_TOKENS = (
    "SDL_InitSubSystem",
    "SDL_PollEvent",
    "SDL_CreateWindow",
)


def _run(argv: list[str], label: str) -> str:
    completed = subprocess.run(
        argv,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="strict",
    )
    if completed.returncode != 0:
        raise ValueError(
            f"{label} failed ({completed.returncode}): "
            f"{completed.stderr.strip()}"
        )
    return completed.stdout


def _dumpbin(dumpbin: Path, option: str, target: Path) -> str:
    return _run(
        [str(dumpbin), "/nologo", option, str(target)],
        f"dumpbin {option} scan for {target}",
    )


def _dependent_names(payload: str) -> list[str]:
    names = [
        match.group(1)
        for line in payload.splitlines()
        if (
            match := re.fullmatch(
                r"\s*([A-Za-z0-9_.+\-]+[.]dll)\s*",
                line,
                flags=re.IGNORECASE,
            )
        )
    ]
    folded = [name.casefold() for name in names]
    if len(folded) != len(set(folded)):
        raise ValueError("PE import table contains duplicate DLL names")
    return names


def _decode_link_map(path: Path) -> str:
    payload = path.read_bytes()
    if payload.startswith((b"\xff\xfe", b"\xfe\xff")):
        return payload.decode("utf-16", errors="strict")
    return payload.decode("utf-8", errors="strict")


def _public_symbol_rows(payload: str, binary: Path) -> list[tuple[str, str]]:
    leading = [line.strip() for line in payload.splitlines() if line.strip()]
    if not leading or binary.stem not in leading[:8]:
        raise ValueError("MSVC link map does not name RoR-Combined")

    rows: list[tuple[str, str]] = []
    in_publics = False
    header_seen = False
    for line in payload.splitlines():
        if "Publics by Value" in line and "Rva+Base" in line:
            if header_seen:
                raise ValueError("MSVC link map has duplicate public-symbol headers")
            header_seen = True
            in_publics = True
            continue
        if not in_publics:
            continue
        if re.match(r"\s*entry point at\s+", line, flags=re.IGNORECASE):
            break
        match = re.match(
            r"^\s*[0-9A-Fa-f]+:[0-9A-Fa-f]+\s+"
            r"(\S+)\s+[0-9A-Fa-f]+(?:\s+[fi]){0,2}\s+(\S+)\s*$",
            line,
        )
        if match is not None:
            rows.append((match.group(1), match.group(2)))
    if not header_seen or not rows:
        raise ValueError("MSVC link map lacks public symbol ownership records")
    return rows


def _owner_mentions_archive(owner: str, archive_name: str) -> bool:
    normalized = owner.replace("\\", "/")
    name = Path(archive_name).name
    aliases = (
        (name, Path(name).stem)
        if name.casefold().endswith(".lib")
        else (name,)
    )
    return any(
        re.search(
            rf"(?:^|/){re.escape(alias)}(?::|\()",
            normalized,
            flags=re.IGNORECASE,
        )
        is not None
        for alias in aliases
    )


def _link_map_evidence(
    payload: str,
    binary: Path,
    required_archives: list[Path],
    sdl_archive: Path,
) -> dict[str, object]:
    rows = _public_symbol_rows(payload, binary)
    archive_counts = {
        str(archive): sum(
            1
            for _symbol, owner in rows
            if _owner_mentions_archive(owner, archive.name)
        )
        for archive in required_archives
    }
    missing_archives = sorted(
        path for path, count in archive_counts.items() if count < 1
    )
    if missing_archives:
        raise ValueError(
            "MSVC link map lacks required Ogre-Next archive evidence: "
            + ", ".join(missing_archives)
        )

    symbols = {symbol for symbol, _owner in rows}
    missing_symbols = sorted(
        token
        for token in REQUIRED_PE_SYMBOL_TOKENS
        if not any(token in symbol for symbol in symbols)
    )
    forbidden_symbols = sorted(
        token
        for token in common.FORBIDDEN_SYMBOL_TOKENS
        if any(token in symbol for symbol in symbols)
    )
    forbidden_objects = sorted(
        token
        for token in FORBIDDEN_PE_OBJECT_TOKENS
        if any(token.casefold() in owner.casefold() for _symbol, owner in rows)
    )
    if missing_symbols or forbidden_symbols or forbidden_objects:
        raise ValueError(
            "Windows combined symbol/object closure failed: "
            + ", ".join(
                missing_symbols + forbidden_symbols + forbidden_objects
            )
        )

    owners = [owner for _symbol, owner in rows]
    sdl_symbol_owners = {
        token: sorted(
            {
                owner
                for symbol, owner in rows
                if symbol.removeprefix("_") == token
                and _owner_mentions_archive(owner, sdl_archive.name)
            }
        )
        for token in REQUIRED_STATIC_SDL_SYMBOL_TOKENS
    }
    missing_sdl_symbols = sorted(
        token for token, owners_for_token in sdl_symbol_owners.items()
        if not owners_for_token
    )
    foreign_sdl_owners = sorted(
        {
            owner
            for symbol, owner in rows
            if symbol.removeprefix("_") in REQUIRED_STATIC_SDL_SYMBOL_TOKENS
            and not _owner_mentions_archive(owner, sdl_archive.name)
        }
    )
    if missing_sdl_symbols or foreign_sdl_owners:
        raise ValueError(
            "Windows static SDL ownership changed: "
            + ", ".join(missing_sdl_symbols + foreign_sdl_owners)
        )
    codec_archives = sorted(
        archive
        for archive in FORBIDDEN_ROOT_CODEC_ARCHIVES
        if any(_owner_mentions_archive(owner, archive) for owner in owners)
    )
    if codec_archives:
        raise ValueError(
            "Windows root static dependency closure changed: "
            + ", ".join(codec_archives)
        )

    decoder_owners = sorted(
        {
            owner
            for _symbol, owner in rows
            if "Ogre14SourceTextureDecoder.cpp.obj" in owner
        }
    )
    if not decoder_owners:
        raise ValueError("Windows binary lacks private stb_image owner evidence")

    reviewed_png_symbols = sorted(
        (symbol, owner)
        for symbol, owner in rows
        if re.search(r"(?:^|[?@_])(?:png|jpeg|stbi)_", symbol)
        and "Ogre14SourceTextureDecoder.cpp.obj" not in owner
        and re.search(r"(?:^|[?@_])png_", symbol)
        and _owner_mentions_archive(owner, REVIEWED_ROOT_PNG_ARCHIVE)
    )
    codec_symbol_violations = sorted(
        (symbol, owner)
        for symbol, owner in rows
        if re.search(r"(?:^|[?@_])(?:png|jpeg|stbi)_", symbol)
        and "Ogre14SourceTextureDecoder.cpp.obj" not in owner
        and (symbol, owner) not in reviewed_png_symbols
    )
    if not reviewed_png_symbols or codec_symbol_violations:
        raise ValueError(
            "Windows binary image-codec ownership changed: "
            + ", ".join(
                f"{symbol} ({owner})"
                for symbol, owner in codec_symbol_violations
            )
        )

    host_rapidjson = sorted(
        symbol for symbol in symbols if "rapidjson" in symbol
    )
    next_rapidjson = sorted(
        symbol for symbol in symbols if "RoROgreNextRapidJson" in symbol
    )
    if not host_rapidjson or not next_rapidjson:
        raise ValueError(
            "Windows combined PE lacks isolated host and OgreNext RapidJSON owners"
        )

    return {
        "required_archive_member_counts": archive_counts,
        "forbidden_renderer_objects_present": False,
        "root_sdl_static_archive_members_extracted": True,
        "root_sdl_archive": {
            "path": str(sdl_archive),
            "sha256": common._sha256(sdl_archive),
            "required_symbol_owners": sdl_symbol_owners,
        },
        "reviewed_root_image_codec_archive": {
            "owner": Path(REVIEWED_ROOT_PNG_ARCHIVE).stem,
            "defined_symbol_count": len(reviewed_png_symbols),
        },
        "private_stb_image_owner_objects": decoder_owners,
        "host_rapidjson_symbol_count": len(host_rapidjson),
        "ogre_next_rapidjson_symbol_count": len(next_rapidjson),
    }


def _verify_contracts(
    executable_contract: Path,
    provider_contract: Path,
    namespace_report: Path,
) -> dict[str, object]:
    executable = common._json_object(
        executable_contract, "combined executable contract"
    )
    if (
        executable.get("schema")
        != "ror.ogre_next_combined_executable.v2"
        or executable.get("target") != "RoR-Combined"
        or executable.get("visible_presentation_owner") != "ogre-next"
        or executable.get("legacy_visible_presentation") is not False
        or executable.get("legacy_visible_fallback") is not False
        or executable.get("legacy_host_role")
        != "hidden-transitional-resource-and-simulation-host"
        or executable.get("presentation_failure_policy") != "fail-closed"
        or executable.get("transport_or_bridge_sources_linked") is not False
    ):
        raise ValueError("combined executable contract is not fail-closed")
    executable_provider_contract = common._regular_absolute(
        str(executable.get("provider_contract", "")),
        "executable-recorded provider contract",
    )
    executable_namespace_report = common._regular_absolute(
        str(executable.get("namespace_audit_report", "")),
        "executable-recorded namespace audit",
    )
    if (
        executable_provider_contract != provider_contract
        or executable_namespace_report != namespace_report
    ):
        raise ValueError("combined executable contract paths changed")

    provider_source_root = common._directory_absolute(
        str(executable.get("provider_source_root", "")),
        "provider source root",
    )
    provider_manifest = common._regular_absolute(
        str(executable.get("provider_source_manifest", "")),
        "provider source manifest",
    )
    provider_manifest_report = common._verify_source_manifest(
        provider_manifest,
        provider_source_root,
        executable.get("provider_source_manifest_sha256"),
        executable.get("provider_source_manifest_file_count"),
        "provider source manifest",
    )
    selected_source_root = common._directory_absolute(
        str(executable.get("selected_game_source_root", "")),
        "selected game source root",
    )
    selected_manifest = common._regular_absolute(
        str(executable.get("selected_game_source_manifest", "")),
        "selected game source manifest",
    )
    selected_manifest_report = common._verify_source_manifest(
        selected_manifest,
        selected_source_root,
        executable.get("selected_game_source_manifest_sha256"),
        executable.get("remaining_game_source_count"),
        "selected game source manifest",
    )

    provider = common._json_object(
        provider_contract, "combined provider contract"
    )
    if (
        provider.get("schema") != "ror.ogre_next_combined_provider.v1"
        or provider.get("platform_policy") != PLATFORM_POLICY
        or provider.get("bridge_sources_linked") is not False
        or provider.get("transport_sources_linked") is not False
        or provider.get("rapidjson_namespace") != "RoROgreNextRapidJson"
        or provider.get("rapidjson_namespace_private") is not True
        or provider.get("sdl_target") != "SDL2::SDL2"
    ):
        raise ValueError("Windows combined provider contract changed")
    provider_recorded_source_root = common._directory_absolute(
        str(provider.get("ror_source_root", "")),
        "provider-recorded source root",
    )
    provider_recorded_source_manifest = common._regular_absolute(
        str(provider.get("ror_source_manifest", "")),
        "provider-recorded source manifest",
    )
    if (
        provider_recorded_source_root != provider_source_root
        or provider_recorded_source_manifest != provider_manifest
    ):
        raise ValueError("Windows combined provider source paths changed")
    authenticated_decoder = common._verify_authenticated_source_image_decoder(
        provider, provider_manifest, provider_source_root
    )

    namespace = common._json_object(
        namespace_report, "combined namespace audit"
    )
    if (
        namespace.get("schema")
        != "ror.ogre_next.embedded_namespace_audit.v2"
        or namespace.get("status") != "passed"
        or namespace.get("platform_policy") != PLATFORM_POLICY
        or namespace.get("namespace") != "RoROgreNext"
        or namespace.get("rapidjson_namespace") != "RoROgreNextRapidJson"
        or namespace.get("ror_source_commit") != provider.get("ror_commit")
    ):
        raise ValueError("Windows namespace audit is not an exact passing proof")
    namespace_scope = namespace.get("evidence_scope")
    if (
        not isinstance(namespace_scope, dict)
        or namespace_scope.get("rapidjson_private_namespace_link") is not True
    ):
        raise ValueError(
            "Windows namespace audit lacks private RapidJSON link evidence"
        )

    return {
        "provider": provider,
        "namespace": namespace,
        "provider_manifest": provider_manifest_report,
        "selected_manifest": selected_manifest_report,
        "authenticated_decoder": authenticated_decoder,
        "source_checkout": common._verify_source_checkout(namespace),
        "strict_fp": common._verify_strict_fp_receipts(provider, namespace),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dumpbin", required=True)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--build-root", required=True)
    parser.add_argument("--link-map", required=True)
    parser.add_argument("--executable-contract", required=True)
    parser.add_argument("--provider-contract", required=True)
    parser.add_argument("--namespace-audit-report", required=True)
    parser.add_argument("--required-archive", action="append", required=True)
    parser.add_argument(
        "--required-ogre14-library", action="append", required=True
    )
    parser.add_argument("--sdl-provider-library", required=True)
    parser.add_argument("--receipt", required=True)
    arguments = parser.parse_args()

    receipt = Path(arguments.receipt)
    try:
        if not receipt.is_absolute() or receipt.is_dir():
            raise ValueError("PE receipt path must be an absolute file path")
        if receipt.exists() or receipt.is_symlink():
            receipt.unlink()

        dumpbin = common._regular_absolute(
            arguments.dumpbin, "dumpbin executable"
        )
        binary = common._regular_absolute(
            arguments.binary, "combined PE binary"
        )
        if binary.suffix.casefold() != ".exe":
            raise ValueError("combined PE binary must have an .exe suffix")
        build_root = common._directory_absolute(
            arguments.build_root, "combined build root"
        )
        link_map = common._regular_absolute(arguments.link_map, "MSVC link map")
        executable_contract = common._regular_absolute(
            arguments.executable_contract, "combined executable contract"
        )
        provider_contract = common._regular_absolute(
            arguments.provider_contract, "combined provider contract"
        )
        namespace_report = common._regular_absolute(
            arguments.namespace_audit_report, "combined namespace audit"
        )

        contracts = _verify_contracts(
            executable_contract, provider_contract, namespace_report
        )
        provider = contracts["provider"]
        namespace = contracts["namespace"]

        sdl_archive = common._regular_absolute(
            str(provider.get("sdl_imported_artifact", "")),
            "provider-recorded static SDL archive",
        )
        sdl_artifact_target = provider.get("sdl_imported_artifact_target")
        if (
            sdl_archive.suffix.casefold() != ".lib"
            or not isinstance(sdl_artifact_target, str)
            or "SDL2-static" not in sdl_artifact_target
            or provider.get("sdl_imported_artifact_size")
            != sdl_archive.stat().st_size
            or provider.get("sdl_imported_artifact_sha256")
            != common._sha256(sdl_archive)
        ):
            raise ValueError("Windows static SDL provider contract changed")

        required_archives = [
            common._regular_absolute(value, "required combined archive")
            for value in arguments.required_archive
        ]
        if len(required_archives) != len(set(required_archives)):
            raise ValueError("required combined archive inputs contain duplicates")
        audited_archive_records = namespace.get("next_archives")
        if not isinstance(audited_archive_records, list):
            raise ValueError("namespace audit archive closure is invalid")
        audited_archives = [
            common._verified_file_record(record, "namespace-audited archive")
            for record in audited_archive_records
        ]
        for field, label in (
            ("embedded_runtime_archive", "namespace-audited N1 runtime"),
            ("direct_contract_archive", "namespace-audited direct contract"),
        ):
            audited_archives.append(
                common._verified_file_record(namespace.get(field), label)
            )
        audited_archive_paths = {record["path"] for record in audited_archives}
        missing_audits = sorted(
            str(path)
            for path in required_archives
            if str(path) not in audited_archive_paths
        )
        if missing_audits:
            raise ValueError(
                "required archives lack namespace audit evidence: "
                + ", ".join(missing_audits)
            )

        audited_legacy_payload = namespace.get("legacy_runtime_libraries")
        if not isinstance(audited_legacy_payload, list):
            raise ValueError("namespace audit OGRE14 closure is invalid")
        audited_legacy = [
            common._verified_file_record(
                record, "namespace-audited OGRE14 library"
            )
            for record in audited_legacy_payload
        ]
        ogre14_manifest = common._verify_ogre14_runtime_manifest(
            provider, audited_legacy, PLATFORM_POLICY
        )
        required_legacy = [
            common._regular_absolute(value, "required OGRE14 library")
            for value in arguments.required_ogre14_library
        ]
        sdl_provider = common._regular_absolute(
            arguments.sdl_provider_library, "OGRE14 SDL provider library"
        )
        expected_host = {
            ogre14_manifest["main_runtime"],
            ogre14_manifest["sdl_provider_runtime"],
        }
        if (
            {str(path) for path in required_legacy} != expected_host
            or str(sdl_provider) != ogre14_manifest["sdl_provider_runtime"]
        ):
            raise ValueError("Windows host runtime arguments changed")

        headers = _dumpbin(dumpbin, "/headers", binary)
        if re.search(
            r"\b8664\s+machine\s+[(]x64[)]",
            headers,
            flags=re.IGNORECASE,
        ) is None:
            raise ValueError("combined PE is not an x64 image")

        dependents = _dependent_names(
            _dumpbin(dumpbin, "/dependents", binary)
        )
        dependent_folded = {name.casefold() for name in dependents}
        required_names = {path.name.casefold() for path in required_legacy}
        missing_imports = sorted(required_names - dependent_folded)
        audited_names = {
            Path(record["path"]).name.casefold() for record in audited_legacy
        }
        unexpected_ogre = sorted(
            name
            for name in dependents
            if re.match(
                r"^(?:Ogre|Plugin_|Codec_|RenderSystem_)",
                name,
                flags=re.IGNORECASE,
            )
            and name.casefold() not in audited_names
        )
        if missing_imports or unexpected_ogre:
            raise ValueError(
                "Windows dynamic OGRE closure failed: "
                + ", ".join(missing_imports + unexpected_ogre)
            )

        link_map_evidence = _link_map_evidence(
            _decode_link_map(link_map), binary, required_archives, sdl_archive
        )

        source_checkout = contracts["source_checkout"]
        common._write_receipt(
            receipt,
            {
                "schema": SCHEMA,
                "platform_policy": PLATFORM_POLICY,
                "binary": str(binary),
                "binary_sha256": common._sha256(binary),
                "qualification_eligible": source_checkout[
                    "qualification_eligible"
                ],
                "source_checkout": source_checkout,
                "link_map": {
                    "path": str(link_map),
                    "sha256": common._sha256(link_map),
                    **link_map_evidence,
                },
                "provider_source_manifest": contracts["provider_manifest"],
                "selected_game_source_manifest": contracts[
                    "selected_manifest"
                ],
                "authenticated_source_image_decoder": contracts[
                    "authenticated_decoder"
                ],
                "namespace_audit_report": {
                    "path": str(namespace_report),
                    "sha256": common._sha256(namespace_report),
                    "audited_archives": audited_archives,
                    "audited_ogre14_libraries": audited_legacy,
                },
                "ogre_next_upstream_strict_fp": contracts["strict_fp"],
                "ogre14_runtime_manifest": ogre14_manifest,
                "pe_import_table": {
                    "dependents": dependents,
                    "required_ogre14_dlls": sorted(required_names),
                    "unexpected_ogre_dlls": [],
                },
                "required_symbol_tokens": list(REQUIRED_PE_SYMBOL_TOKENS),
                "missing_required_symbols": [],
                "bridge_or_transport_symbols_present": False,
                "bridge_or_transport_objects_present": False,
                "external_image_codec_symbols_present": True,
                "unreviewed_external_image_codec_symbols_present": False,
                "root_sdl_symbols_present": True,
                "rapidjson_namespace_isolation": {
                    "host_namespace": "rapidjson",
                    "host_defined_symbol_count": link_map_evidence[
                        "host_rapidjson_symbol_count"
                    ],
                    "ogre_next_namespace": "RoROgreNextRapidJson",
                    "ogre_next_defined_symbol_count": link_map_evidence[
                        "ogre_next_rapidjson_symbol_count"
                    ],
                    "dual_owner_linked": True,
                },
                "ogre_next_runtime_contributors_present": True,
                "ogre14_host_imports_present": True,
            },
        )
    except (OSError, UnicodeError, ValueError) as error:
        print(
            f"combined PE closure verification failed: {error}",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
