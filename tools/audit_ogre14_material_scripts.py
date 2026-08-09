#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Audit legacy OGRE material scripts without assigning guessed PBR roles.

The input ZIP remains user supplied.  This tool reads bounded ``.material``
members directly from the archive, records authored structure and aliases, and
reports conservative structural blockers for the version-one RoR translator.
It is an inventory, not a runtime material-admission decision: OGRE inheritance,
listeners, RTShaderSystem, and native pass state must still be captured exactly.
"""

from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import sys
from typing import cast
import zipfile

TOOLS_ROOT = str(Path(__file__).resolve().parent)
if TOOLS_ROOT not in sys.path:
    sys.path.insert(0, TOOLS_ROOT)

from audit_cityworld_visuals import (
    AuditFailure,
    MAX_TEXT_BYTES,
    archive_sha256,
    decode_text_payload,
    validate_archive_members,
)


REPORT_FORMAT = "ror-ogre14-material-script-audit-v1"
MAX_MATERIAL_DEFINITIONS = 65_536
MAX_DIRECTIVES_PER_MATERIAL = 65_536

MATERIAL_PATTERN = re.compile(
    r"^material\s+([^\s:{]+)(?:\s*:\s*([^\s{]+))?",
    re.IGNORECASE,
)

PROGRAM_DIRECTIVES = (
    "vertex_program_ref",
    "fragment_program_ref",
    "geometry_program_ref",
    "tessellation_hull_program_ref",
    "tessellation_domain_program_ref",
    "compute_program_ref",
)
FIXED_FUNCTION_DIRECTIVES = (
    "ambient",
    "diffuse",
    "specular",
    "emissive",
    "shininess",
)
NON_IDENTITY_TEXTURE_DIRECTIVES = (
    "anim_texture",
    "cubic_texture",
    "rotate",
    "rotate_anim",
    "scale",
    "scroll",
    "scroll_anim",
    "transform",
    "wave_xform",
)


def canonical_json(report: object, *, pretty: bool = False) -> str:
    if pretty:
        return json.dumps(
            report, ensure_ascii=True, indent=2, sort_keys=True
        ) + "\n"
    return json.dumps(
        report,
        ensure_ascii=True,
        sort_keys=True,
        separators=(",", ":"),
    ) + "\n"


def _new_material(
    *, name: str, inherited_from: str | None, script: str, line: int
) -> dict[str, object]:
    return {
        "alpha_rejection_directives": 0,
        "authored_texture_aliases": [],
        "environment_map_directives": 0,
        "exact_name": name,
        "fixed_function_directives": {
            directive: 0 for directive in FIXED_FUNCTION_DIRECTIVES
        },
        "inherited_from": inherited_from,
        "lighting_directives": 0,
        "line": line,
        "non_identity_texture_directives": 0,
        "passes": 0,
        "program_references": {
            directive.removesuffix("_ref"): 0
            for directive in PROGRAM_DIRECTIVES
        },
        "scene_blend_directives": 0,
        "script": script,
        "techniques": 0,
        "texture_directives": 0,
        "texture_units": 0,
    }


def _record_directive(
    material: dict[str, object], directive: str, arguments: list[str]
) -> None:
    if directive == "technique":
        material["techniques"] = int(material["techniques"]) + 1
    elif directive == "pass":
        material["passes"] = int(material["passes"]) + 1
    elif directive == "texture_unit":
        material["texture_units"] = int(material["texture_units"]) + 1
    elif directive == "texture":
        material["texture_directives"] = (
            int(material["texture_directives"]) + 1
        )
    elif directive == "texture_alias" and arguments:
        aliases = cast(list[str], material["authored_texture_aliases"])
        aliases.append(arguments[0])
    elif directive == "env_map":
        material["environment_map_directives"] = (
            int(material["environment_map_directives"]) + 1
        )
    elif directive in PROGRAM_DIRECTIVES:
        programs = cast(dict[str, int], material["program_references"])
        key = directive.removesuffix("_ref")
        programs[key] = int(programs[key]) + 1
    elif directive in FIXED_FUNCTION_DIRECTIVES:
        fixed = cast(dict[str, int], material["fixed_function_directives"])
        fixed[directive] = int(fixed[directive]) + 1
    elif directive in NON_IDENTITY_TEXTURE_DIRECTIVES:
        material["non_identity_texture_directives"] = (
            int(material["non_identity_texture_directives"]) + 1
        )
    elif directive == "scene_blend":
        material["scene_blend_directives"] = (
            int(material["scene_blend_directives"]) + 1
        )
    elif directive == "alpha_rejection":
        material["alpha_rejection_directives"] = (
            int(material["alpha_rejection_directives"]) + 1
        )
    elif directive == "lighting":
        material["lighting_directives"] = (
            int(material["lighting_directives"]) + 1
        )


def _finalize_material(material: dict[str, object]) -> dict[str, object]:
    aliases = cast(list[str], material["authored_texture_aliases"])
    material["authored_texture_aliases"] = sorted(aliases)
    programs = cast(dict[str, int], material["program_references"])
    fixed = cast(dict[str, int], material["fixed_function_directives"])

    blockers: list[str] = []
    if material["inherited_from"] is not None:
        blockers.append("SCRIPT_INHERITANCE_REQUIRES_NATIVE_RESOLUTION")
    if int(material["techniques"]) != 1:
        blockers.append("TECHNIQUE_COUNT_NOT_ONE")
    if int(material["passes"]) != 1:
        blockers.append("PASS_COUNT_NOT_ONE")
    if any(int(value) != 0 for value in programs.values()):
        blockers.append("AUTHORED_GPU_PROGRAM")
    if int(material["texture_units"]) > 1:
        blockers.append("MULTIPLE_TEXTURE_UNITS")
    if int(material["environment_map_directives"]) != 0:
        blockers.append("ENVIRONMENT_MAPPING")
    if int(material["non_identity_texture_directives"]) != 0:
        blockers.append("TEXTURE_TRANSFORM_OR_ANIMATION")

    material["requires_explicit_semantic_declaration"] = True
    material["v1_structural_blockers"] = blockers
    material["v1_structural_candidate"] = not blockers
    material["requires_native_fixed_function_value_check"] = any(
        int(fixed[name]) != 0
        for name in ("ambient", "specular", "emissive", "shininess")
    )
    return material


def parse_material_script(text: str, script: str) -> list[dict[str, object]]:
    if "\x00" in text:
        raise AuditFailure(f"material script contains NUL: {script!r}")

    records: list[dict[str, object]] = []
    current: dict[str, object] | None = None
    directive_count = 0
    material_brace_depth = 0
    material_block_opened = False
    for line_number, raw_line in enumerate(text.splitlines(), start=1):
        authored = raw_line.split("//", 1)[0].strip()
        if not authored:
            continue
        match = MATERIAL_PATTERN.match(authored)
        if match is not None:
            if current is not None:
                records.append(_finalize_material(current))
            if len(records) >= MAX_MATERIAL_DEFINITIONS:
                raise AuditFailure("material definition count exceeds fixed cap")
            current = _new_material(
                name=match.group(1),
                inherited_from=match.group(2),
                script=script,
                line=line_number,
            )
            directive_count = 0
            material_brace_depth = 0
            material_block_opened = False
            authored = authored[match.end() :].strip()
        if current is None:
            continue

        material_closed = False
        for segment in re.split(r"([{}])", authored):
            if segment == "{":
                material_block_opened = True
                material_brace_depth += 1
                continue
            if segment == "}":
                if material_block_opened:
                    material_brace_depth -= 1
                    if material_brace_depth <= 0:
                        material_closed = True
                continue
            if material_closed:
                continue
            tokens = segment.split()
            if not tokens:
                continue
            directive_count += 1
            if directive_count > MAX_DIRECTIVES_PER_MATERIAL:
                raise AuditFailure(
                    f"material directive count exceeds fixed cap: {script!r}"
                )
            _record_directive(current, tokens[0].casefold(), tokens[1:])

        if material_closed:
            records.append(_finalize_material(current))
            current = None
            material_brace_depth = 0
            material_block_opened = False

    if current is not None:
        records.append(_finalize_material(current))
    return records


def audit_archive(
    path: Path, *, expected_sha256: str | None = None
) -> dict[str, object]:
    if not path.is_file():
        raise AuditFailure("input archive does not exist or is not a file")
    digest = archive_sha256(path)
    if expected_sha256 is not None and digest != expected_sha256.casefold():
        raise AuditFailure(
            f"archive SHA-256 mismatch: expected {expected_sha256.casefold()}, "
            f"got {digest}"
        )

    records: list[dict[str, object]] = []
    script_records: list[dict[str, object]] = []
    try:
        with zipfile.ZipFile(path) as archive:
            infos = archive.infolist()
            expanded_bytes, _ = validate_archive_members(infos)
            material_infos = sorted(
                (
                    info
                    for info in infos
                    if not info.is_dir()
                    and PurePosixPath(info.filename).suffix.casefold()
                    == ".material"
                ),
                key=lambda info: info.filename,
            )
            for info in material_infos:
                if info.file_size > MAX_TEXT_BYTES:
                    raise AuditFailure(
                        "material script exceeds bounded text limit: "
                        f"{info.filename!r}"
                    )
                payload = archive.read(info)
                if len(payload) != info.file_size:
                    raise AuditFailure(
                        f"short read for material script: {info.filename!r}"
                    )
                parsed = parse_material_script(
                    decode_text_payload(payload, info.filename), info.filename
                )
                if len(records) + len(parsed) > MAX_MATERIAL_DEFINITIONS:
                    raise AuditFailure(
                        "archive material definition count exceeds fixed cap"
                    )
                records.extend(parsed)
                script_records.append(
                    {
                        "bytes": info.file_size,
                        "definitions": len(parsed),
                        "name": info.filename,
                        "sha256": hashlib.sha256(payload).hexdigest(),
                    }
                )
    except zipfile.BadZipFile as exc:
        raise AuditFailure("input is not a valid ZIP archive") from exc

    name_locations: dict[str, list[str]] = {}
    blocker_counts: Counter[str] = Counter()
    alias_counts: Counter[str] = Counter()
    texture_unit_histogram: Counter[str] = Counter()
    structural_candidates = 0
    for record in records:
        location = f"{record['script']}:{record['line']}"
        name_locations.setdefault(str(record["exact_name"]), []).append(
            location
        )
        blockers = cast(list[str], record["v1_structural_blockers"])
        aliases = cast(list[str], record["authored_texture_aliases"])
        blocker_counts.update(str(blocker) for blocker in blockers)
        alias_counts.update(str(alias) for alias in aliases)
        texture_unit_histogram[str(record["texture_units"])] += 1
        structural_candidates += int(bool(record["v1_structural_candidate"]))

    duplicates = [
        {"exact_name": name, "locations": locations}
        for name, locations in sorted(name_locations.items())
        if len(locations) > 1
    ]
    return {
        "archive": {
            "bytes": path.stat().st_size,
            "declared_expanded_bytes": expanded_bytes,
            "entries": len(infos),
            "name": path.name,
            "sha256": digest,
        },
        "format": REPORT_FORMAT,
        "materials": records,
        "ok": True,
        "scripts": script_records,
        "summary": {
            "authored_texture_alias_counts": dict(sorted(alias_counts.items())),
            "duplicate_exact_names": duplicates,
            "material_definitions": len(records),
            "script_files": len(script_records),
            "texture_unit_histogram": dict(
                sorted(texture_unit_histogram.items(), key=lambda item: int(item[0]))
            ),
            "v1_structural_blocker_counts": dict(
                sorted(blocker_counts.items())
            ),
            "v1_structural_blocked": len(records) - structural_candidates,
            "v1_structural_candidates": structural_candidates,
        },
    }


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--expect-sha256")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--pretty", action="store_true")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    arguments = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        report = audit_archive(
            arguments.archive,
            expected_sha256=arguments.expect_sha256,
        )
        rendered = canonical_json(report, pretty=arguments.pretty)
        if arguments.output is None:
            sys.stdout.write(rendered)
        else:
            arguments.output.write_text(rendered, encoding="utf-8")
        return 0
    except (AuditFailure, OSError, ValueError) as error:
        print(f"OGRE material-script audit failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
