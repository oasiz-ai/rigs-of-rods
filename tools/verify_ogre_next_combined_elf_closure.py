#!/usr/bin/env python3
"""Fail closed on the Linux ELF closure of the visible Ogre-Next game."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

import verify_ogre_next_combined_binary_closure as common


SCHEMA = "ror.ogre_next_combined_elf_closure.v1"
PLATFORM_POLICY = "linux-x86_64-vulkan"


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


def _nm(
    nm: Path,
    target: Path,
    *,
    demangle: bool = False,
    external_only: bool = False,
) -> tuple[str, set[str]]:
    argv = [str(nm), "--defined-only"]
    if external_only:
        argv.append("--extern-only")
    if demangle:
        argv.append("--demangle")
    argv.append(str(target))
    payload = _run(argv, f"nm scan for {target}")
    return payload, common._nm_symbol_names(payload)


def _dynamic_section(readelf: Path, target: Path) -> str:
    return _run([str(readelf), "--dynamic", str(target)], f"readelf for {target}")


def _needed_names(payload: str) -> list[str]:
    names = re.findall(
        r"[(]NEEDED[)].*Shared library: \[([^\]]+)\]", payload
    )
    if len(names) != len(set(names)):
        raise ValueError("ELF dynamic section contains duplicate NEEDED entries")
    return names


def _soname(readelf: Path, target: Path) -> str:
    payload = _dynamic_section(readelf, target)
    matches = re.findall(
        r"[(]SONAME[)].*Library soname: \[([^\]]+)\]", payload
    )
    if len(matches) > 1:
        raise ValueError(f"ELF library has multiple SONAME records: {target}")
    return matches[0] if matches else target.name


def _required_archive_evidence(
    payload: str, required_archives: list[Path], build_root: Path
) -> dict[str, int]:
    counts: dict[str, int] = {}
    for path in required_archives:
        try:
            relative_path = path.relative_to(build_root).as_posix()
        except ValueError as error:
            raise ValueError(
                f"required Ogre-Next archive escaped the build root: {path}"
            ) from error
        # GNU ld writes build-tree inputs relative to the link invocation's
        # build root. Requiring the opening member delimiter proves that the
        # archive contributed an object, rather than merely appearing in a
        # command echo or LOAD record.
        counts[str(path)] = payload.count(relative_path + "(")
    missing = [path for path, count in counts.items() if count < 1]
    if missing:
        raise ValueError(
            "GNU link map lacks required Ogre-Next archive evidence: "
            + ", ".join(missing)
        )
    return counts


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--nm", required=True)
    parser.add_argument("--readelf", required=True)
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
            raise ValueError("ELF receipt path must be an absolute file path")
        if receipt.exists() or receipt.is_symlink():
            receipt.unlink()

        nm = common._regular_absolute(arguments.nm, "nm executable")
        readelf = common._regular_absolute(
            arguments.readelf, "readelf executable"
        )
        binary = common._regular_absolute(arguments.binary, "combined ELF binary")
        build_root = common._directory_absolute(
            arguments.build_root, "combined build root"
        )
        link_map = common._regular_absolute(arguments.link_map, "GNU link map")
        executable_contract = common._regular_absolute(
            arguments.executable_contract, "combined executable contract"
        )
        provider_contract = common._regular_absolute(
            arguments.provider_contract, "combined provider contract"
        )
        namespace_report = common._regular_absolute(
            arguments.namespace_audit_report, "combined namespace audit"
        )

        executable = common._json_object(
            executable_contract, "combined executable contract"
        )
        if (
            executable.get("schema")
            != "ror.ogre_next_combined_executable.v1"
            or executable.get("target") != "RoR-Combined"
            or executable.get("transport_or_bridge_sources_linked") is not False
            or executable.get("provider_contract") != str(provider_contract)
            or executable.get("namespace_audit_report") != str(namespace_report)
        ):
            raise ValueError("combined executable contract is not fail-closed")

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
            or provider.get("rapidjson_namespace")
            != "RoROgreNextRapidJson"
            or provider.get("rapidjson_namespace_private") is not True
            or provider.get("sdl_target") != "SDL2::SDL2"
            or provider.get("ror_source_root") != str(provider_source_root)
            or provider.get("ror_source_manifest") != str(provider_manifest)
        ):
            raise ValueError("Linux combined provider contract changed")
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
            or namespace.get("rapidjson_namespace")
            != "RoROgreNextRapidJson"
            or namespace.get("ror_source_commit") != provider.get("ror_commit")
        ):
            raise ValueError("Linux namespace audit is not an exact passing proof")
        namespace_scope = namespace.get("evidence_scope")
        if (
            not isinstance(namespace_scope, dict)
            or namespace_scope.get("rapidjson_private_namespace_link") is not True
        ):
            raise ValueError(
                "Linux namespace audit lacks private RapidJSON link evidence"
            )
        source_checkout = common._verify_source_checkout(namespace)
        strict_fp = common._verify_strict_fp_receipts(provider, namespace)

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
        missing_audits = [
            str(path)
            for path in required_archives
            if str(path) not in audited_archive_paths
        ]
        if missing_audits:
            raise ValueError(
                "required archives lack namespace audit evidence: "
                + ", ".join(missing_audits)
            )

        audited_legacy_payload = namespace.get("legacy_runtime_libraries")
        if not isinstance(audited_legacy_payload, list):
            raise ValueError("namespace audit OGRE14 closure is invalid")
        audited_legacy = [
            common._verified_file_record(record, "namespace-audited OGRE14 library")
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
            raise ValueError("Linux host runtime arguments changed")

        demangled_payload, demangled_symbols = _nm(nm, binary, demangle=True)
        raw_payload, raw_symbols = _nm(nm, binary, external_only=True)
        missing_symbols = common._missing_required_demangled_symbols(
            demangled_payload
        )
        forbidden_symbols = sorted(
            token
            for token in common.FORBIDDEN_SYMBOL_TOKENS
            if any(token in symbol for symbol in demangled_symbols)
        )
        image_codec_symbols = sorted(
            symbol
            for symbol in raw_symbols
            if symbol.startswith(("png_", "jpeg_", "stbi_"))
        )
        host_rapidjson_symbols = sorted(
            symbol for symbol in demangled_symbols if "rapidjson::" in symbol
        )
        ogre_next_rapidjson_symbols = sorted(
            symbol
            for symbol in demangled_symbols
            if "RoROgreNextRapidJson::" in symbol
        )
        if not host_rapidjson_symbols or not ogre_next_rapidjson_symbols:
            raise ValueError(
                "Linux combined ELF lacks isolated host and OgreNext RapidJSON owners"
            )
        executable_sdl_symbols = sorted(
            symbol for symbol in raw_symbols if symbol.startswith("SDL_")
        )
        if missing_symbols or forbidden_symbols or image_codec_symbols:
            raise ValueError(
                "Linux combined symbol closure failed: "
                + ", ".join(
                    missing_symbols + forbidden_symbols + image_codec_symbols
                )
            )
        if executable_sdl_symbols:
            raise ValueError("root SDL archive was extracted into RoR-Combined")

        link_map_text = link_map.read_text(encoding="utf-8", errors="strict")
        archive_counts = _required_archive_evidence(
            link_map_text, required_archives, build_root
        )
        forbidden_objects = sorted(
            token
            for token in common.FORBIDDEN_LINK_MAP_OBJECT_TOKENS
            if token in link_map_text
        )
        if forbidden_objects:
            raise ValueError(
                "GNU link map contains retired renderer objects: "
                + ", ".join(forbidden_objects)
            )
        private_stbi = sorted(
            symbol
            for symbol in demangled_symbols
            if "stbi_" in symbol
        )
        if (
            not private_stbi
            or "Ogre14SourceTextureDecoder.cpp.o" not in link_map_text
        ):
            raise ValueError("Linux binary lacks private stb_image owner evidence")

        needed = _needed_names(_dynamic_section(readelf, binary))
        audited_sonames = {
            _soname(readelf, Path(record["path"]))
            for record in audited_legacy
        }
        required_sonames = {_soname(readelf, path) for path in required_legacy}
        missing_needed = sorted(required_sonames - set(needed))
        unexpected_ogre = sorted(
            name
            for name in needed
            if re.match(
                r"^(?:libOgre|Plugin_|Codec_|RenderSystem_)", name
            )
            and name not in audited_sonames
        )
        if missing_needed or unexpected_ogre:
            raise ValueError(
                "Linux dynamic OGRE closure failed: "
                + ", ".join(missing_needed + unexpected_ogre)
            )

        _sdl_payload, sdl_symbols = _nm(
            nm, sdl_provider, external_only=True
        )
        missing_sdl = sorted(
            name
            for name in common.REQUIRED_SDL_PROVIDER_SYMBOL_TOKENS
            if name not in sdl_symbols
        )
        if missing_sdl:
            raise ValueError(
                "OGRE14 SDL provider lacks required symbols: "
                + ", ".join(missing_sdl)
            )

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
                    "required_archive_member_counts": archive_counts,
                    "forbidden_renderer_objects_present": False,
                },
                "provider_source_manifest": provider_manifest_report,
                "selected_game_source_manifest": selected_manifest_report,
                "authenticated_source_image_decoder": authenticated_decoder,
                "namespace_audit_report": {
                    "path": str(namespace_report),
                    "sha256": common._sha256(namespace_report),
                    "audited_archives": audited_archives,
                    "audited_ogre14_libraries": audited_legacy,
                },
                "ogre_next_upstream_strict_fp": strict_fp,
                "ogre14_runtime_manifest": ogre14_manifest,
                "elf_dynamic_section": {
                    "needed": needed,
                    "required_ogre14_sonames": sorted(required_sonames),
                    "unexpected_ogre_sonames": [],
                },
                "required_symbol_tokens": list(common.REQUIRED_SYMBOL_TOKENS),
                "missing_required_symbols": [],
                "bridge_or_transport_symbols_present": False,
                "bridge_or_transport_objects_present": False,
                "external_image_codec_symbols_present": False,
                "root_sdl_symbols_present": False,
                "private_stb_image_symbol_count": len(private_stbi),
                "rapidjson_namespace_isolation": {
                    "host_namespace": "rapidjson",
                    "host_defined_symbol_count": len(host_rapidjson_symbols),
                    "ogre_next_namespace": "RoROgreNextRapidJson",
                    "ogre_next_defined_symbol_count": len(
                        ogre_next_rapidjson_symbols
                    ),
                    "dual_owner_linked": True,
                },
                "ogre_next_runtime_contributors_present": True,
                "ogre14_host_load_commands_present": True,
            },
        )
    except (OSError, UnicodeError, ValueError) as error:
        print(
            f"combined ELF closure verification failed: {error}",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
