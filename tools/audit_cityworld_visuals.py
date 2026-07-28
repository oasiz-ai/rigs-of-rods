#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Create a deterministic, read-only CityWorld visual-content inventory.

The input archive is user-supplied compatibility content. This tool never
extracts, modifies, or republishes it. The report is intended to drive a
rights-cleared replacement/overlay workflow for Blender, glTF, and RoR.
"""

from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path, PurePosixPath
import re
import sys
import unicodedata
import zipfile


REPORT_FORMAT = "ror-cityworld-visual-audit-v1"
READ_CHUNK_BYTES = 1024 * 1024
MAX_ENTRIES = 50_000
MAX_ENTRY_BYTES = 2 * 1024 * 1024 * 1024
MAX_TOTAL_EXPANDED_BYTES = 16 * 1024 * 1024 * 1024
MAX_TEXT_BYTES = 32 * 1024 * 1024
MAX_COMPRESSION_RATIO = 1024.0

TEXTURE_SUFFIXES = frozenset(
    {".bmp", ".dds", ".gif", ".jpeg", ".jpg", ".png", ".tga", ".webp"}
)
MODEL_SUFFIXES = frozenset({".mesh", ".mesh.xml", ".gltf", ".glb"})

CATEGORY_PATTERNS = (
    (
        "bridge",
        re.compile(
            r"bridge|elevatedhighway|overpass|viaduct|avenueover",
            re.IGNORECASE,
        ),
    ),
    (
        "vegetation",
        re.compile(
            r"arbol|(?<!s)trees?|bush|veget|forest|palm|grass",
            re.IGNORECASE,
        ),
    ),
    (
        "fixture",
        re.compile(
            r"semaforo|trafficlight|lamp|lightpole|streetlight|busstop|"
            r"parabus|telefon|sign|hydrant|bench|billboard|pantalla|"
            r"precaucion|tope",
            re.IGNORECASE,
        ),
    ),
    (
        "building",
        re.compile(
            r"building|skyscraper|highrise|haus|house|hotel|hospital|"
            r"station|school|colegio|university|universidad|garage|"
            r"hangar|firehouse|insurance|airport|aeropuerto|prison|cereso|"
            r"estadio|stadium|bank|banco|tower|torre|factory|warehouse|"
            r"store|shop",
            re.IGNORECASE,
        ),
    ),
    (
        "road",
        re.compile(
            r"road|avenue|carretera|autopista|cruce|calle|street|"
            r"sidewalk|asphalt|distribuidor",
            re.IGNORECASE,
        ),
    ),
)


class AuditFailure(Exception):
    """A stable failure caused by an unsafe or unsupported input archive."""


def classify_name(name: str) -> str:
    for category, pattern in CATEGORY_PATTERNS:
        if pattern.search(name):
            return category
    return "other"


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


def archive_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(READ_CHUNK_BYTES):
            digest.update(chunk)
    return digest.hexdigest()


def validate_member_name(name: str) -> str:
    if "\x00" in name or "\\" in name:
        raise AuditFailure(f"unsafe ZIP member path: {name!r}")
    normalized = unicodedata.normalize("NFC", name)
    if normalized != name:
        raise AuditFailure(f"non-canonical ZIP member path: {name!r}")
    path = PurePosixPath(name)
    if path.is_absolute() or not path.parts:
        raise AuditFailure(f"unsafe ZIP member path: {name!r}")
    if any(part in {"", ".", ".."} for part in path.parts):
        raise AuditFailure(f"unsafe ZIP member path: {name!r}")
    if ":" in path.parts[0]:
        raise AuditFailure(f"unsafe ZIP member path: {name!r}")
    return name.casefold()


def validate_archive_members(
    infos: list[zipfile.ZipInfo],
) -> tuple[int, dict[str, zipfile.ZipInfo]]:
    if len(infos) > MAX_ENTRIES:
        raise AuditFailure(
            f"archive has {len(infos)} entries; limit is {MAX_ENTRIES}"
        )

    total_expanded = 0
    names: dict[str, zipfile.ZipInfo] = {}
    for info in infos:
        folded_name = validate_member_name(info.filename)
        if folded_name in names:
            raise AuditFailure(
                "duplicate or case-colliding ZIP member path: "
                f"{info.filename!r}"
            )
        names[folded_name] = info
        if info.flag_bits & 0x1:
            raise AuditFailure(
                f"encrypted ZIP member is unsupported: {info.filename!r}"
            )
        if info.file_size > MAX_ENTRY_BYTES:
            raise AuditFailure(
                f"ZIP member exceeds expanded-size limit: {info.filename!r}"
            )
        total_expanded += info.file_size
        if total_expanded > MAX_TOTAL_EXPANDED_BYTES:
            raise AuditFailure("archive exceeds total expanded-size limit")
        if info.file_size:
            ratio = info.file_size / max(1, info.compress_size)
            if ratio > MAX_COMPRESSION_RATIO:
                raise AuditFailure(
                    "ZIP member exceeds compression-ratio limit: "
                    f"{info.filename!r}"
                )
    return total_expanded, names


def read_text_member(
    archive: zipfile.ZipFile, info: zipfile.ZipInfo
) -> str:
    if info.file_size > MAX_TEXT_BYTES:
        raise AuditFailure(
            f"text member exceeds read limit: {info.filename!r}"
        )
    payload = archive.read(info)
    if len(payload) != info.file_size:
        raise AuditFailure(
            f"short read for ZIP member: {info.filename!r}"
        )
    try:
        return payload.decode("utf-8-sig")
    except UnicodeDecodeError:
        try:
            return payload.decode("cp1252")
        except UnicodeDecodeError as exc:
            raise AuditFailure(
                f"text member is not UTF-8 or CP1252: {info.filename!r}"
            ) from exc


def find_required_member(
    names: dict[str, zipfile.ZipInfo], suffix: str
) -> zipfile.ZipInfo:
    matches = [
        info
        for folded, info in names.items()
        if folded.endswith(suffix.casefold())
    ]
    if len(matches) != 1:
        raise AuditFailure(
            f"expected exactly one {suffix} member; found {len(matches)}"
        )
    return matches[0]


def parse_vector(value: str, expected: int = 3) -> list[float] | None:
    pieces = [piece.strip() for piece in value.replace(",", " ").split()]
    if len(pieces) != expected:
        return None
    try:
        vector = [float(piece) for piece in pieces]
    except ValueError:
        return None
    return vector if all(math.isfinite(item) for item in vector) else None


def parse_terrain(text: str, filename: str) -> dict[str, object]:
    section = ""
    values: dict[str, dict[str, str]] = {}
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line or line.startswith(("#", ";")):
            continue
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1].strip().casefold()
            continue
        if "=" in line:
            key, value = line.split("=", 1)
            values.setdefault(section, {})[key.strip()] = value.strip()

    general = values.get("general", {})
    teleport = values.get("teleport", {})
    telepoints: list[dict[str, object]] = []
    number = 1
    while True:
        name = teleport.get(f"Telepoint{number}/Name")
        position_text = teleport.get(f"Telepoint{number}/Position")
        if name is None and position_text is None:
            break
        position = (
            parse_vector(position_text)
            if position_text is not None
            else None
        )
        if name is not None and position is not None:
            telepoints.append({"name": name, "position": position})
        number += 1

    return {
        "ambient_color": parse_vector(
            general.get("AmbientColor", "")
        ),
        "file": filename,
        "guid": general.get("GUID", ""),
        "name": general.get("Name", ""),
        "start_position": parse_vector(
            general.get("StartPosition", "")
        ),
        "telepoints": telepoints,
    }


def parse_placements(text: str) -> dict[str, object]:
    objects: Counter[str] = Counter()
    records: list[dict[str, object]] = []
    commented_placements = 0
    directives = 0
    malformed = 0

    for line_number, raw_line in enumerate(text.splitlines(), start=1):
        line = raw_line.strip()
        if not line:
            continue
        if line.startswith(("//", ";", "#")):
            candidate = line[2:].strip() if line.startswith("//") else ""
            if candidate.count(",") >= 6:
                commented_placements += 1
            continue
        pieces = [piece.strip() for piece in line.split(",", 6)]
        if len(pieces) != 7:
            directives += 1
            continue
        try:
            transform = [float(piece) for piece in pieces[:6]]
        except ValueError:
            malformed += 1
            continue
        if not all(math.isfinite(item) for item in transform):
            malformed += 1
            continue
        object_name = pieces[6].strip()
        if not object_name:
            malformed += 1
            continue
        objects[object_name] += 1
        records.append(
            {
                "category": classify_name(object_name),
                "line": line_number,
                "object": object_name,
                "position": transform[:3],
                "rotation": transform[3:],
            }
        )

    category_counts = Counter(
        str(record["category"]) for record in records
    )
    bounds = None
    if records:
        positions = [record["position"] for record in records]
        bounds = {
            "max": [
                max(float(position[index]) for position in positions)
                for index in range(3)
            ],
            "min": [
                min(float(position[index]) for position in positions)
                for index in range(3)
            ],
        }

    return {
        "bounds": bounds,
        "category_counts": dict(sorted(category_counts.items())),
        "commented_placements": commented_placements,
        "directives": directives,
        "malformed": malformed,
        "object_counts": objects,
        "records": records,
        "total": len(records),
    }


def object_definition_index(
    infos: list[zipfile.ZipInfo],
) -> dict[str, str]:
    index: dict[str, str] = {}
    for info in infos:
        path = PurePosixPath(info.filename)
        if path.suffix.casefold() == ".odef":
            index[path.stem.casefold()] = info.filename
    return index


def canonical_placement_object(name: str) -> str | None:
    tokens = name.casefold().split()
    if not tokens:
        return None
    if tokens[0] in {"truck", "load", "machine", "boat"}:
        return None
    if len(tokens) > 1 and tokens[0] in {
        "hangar",
        "load-spawner",
        "truckshop",
    }:
        return None
    path = PurePosixPath(name)
    return path.stem if path.suffix.casefold() == ".odef" else name


def build_object_report(
    object_counts: Counter[str], odefs: dict[str, str]
) -> tuple[list[dict[str, object]], list[str]]:
    report: list[dict[str, object]] = []
    unresolved: list[str] = []
    for object_name in sorted(object_counts, key=str.casefold):
        key = canonical_placement_object(object_name)
        definition = odefs.get(key.casefold()) if key is not None else None
        if key is not None and definition is None:
            unresolved.append(object_name)
        report.append(
            {
                "category": classify_name(object_name),
                "count": object_counts[object_name],
                "definition": definition,
                "name": object_name,
            }
        )
    return report, unresolved


def nearest_telepoint_clusters(
    records: list[dict[str, object]],
    telepoints: list[dict[str, object]],
) -> list[dict[str, object]]:
    clusters = [
        {
            "category_counts": Counter(),
            "name": str(point["name"]),
            "placement_count": 0,
            "position": point["position"],
        }
        for point in telepoints
    ]
    if not clusters:
        return []

    for record in records:
        position = record["position"]
        nearest = min(
            range(len(clusters)),
            key=lambda index:
                (float(position[0]) - float(clusters[index]["position"][0]))
                ** 2
                + (
                    float(position[2])
                    - float(clusters[index]["position"][2])
                )
                ** 2,
        )
        clusters[nearest]["placement_count"] += 1
        clusters[nearest]["category_counts"][record["category"]] += 1

    result: list[dict[str, object]] = []
    for cluster in clusters:
        result.append(
            {
                "category_counts": dict(
                    sorted(cluster["category_counts"].items())
                ),
                "name": cluster["name"],
                "placement_count": cluster["placement_count"],
                "position": cluster["position"],
            }
        )
    return result


def intercity_links(
    telepoints: list[dict[str, object]],
) -> list[dict[str, object]]:
    links: list[dict[str, object]] = []
    for left in range(len(telepoints)):
        for right in range(left + 1, len(telepoints)):
            left_position = telepoints[left]["position"]
            right_position = telepoints[right]["position"]
            distance = math.hypot(
                float(left_position[0]) - float(right_position[0]),
                float(left_position[2]) - float(right_position[2]),
            )
            names = sorted(
                [
                    str(telepoints[left]["name"]),
                    str(telepoints[right]["name"]),
                ],
                key=str.casefold,
            )
            links.append(
                {
                    "distance_m": round(distance, 3),
                    "from": names[0],
                    "to": names[1],
                }
            )
    return sorted(
        links,
        key=lambda link: (
            float(link["distance_m"]),
            str(link["from"]).casefold(),
            str(link["to"]).casefold(),
        ),
    )


def asset_inventory(infos: list[zipfile.ZipInfo]) -> dict[str, object]:
    suffix_counts: Counter[str] = Counter()
    model_categories: Counter[str] = Counter()
    model_count = 0
    texture_count = 0
    for info in infos:
        if info.is_dir():
            continue
        lowered = info.filename.casefold()
        suffix = "".join(PurePosixPath(lowered).suffixes[-2:])
        if suffix != ".mesh.xml":
            suffix = PurePosixPath(lowered).suffix
        suffix_counts[suffix or "<none>"] += 1
        if suffix in MODEL_SUFFIXES:
            model_count += 1
            model_categories[classify_name(info.filename)] += 1
        if suffix in TEXTURE_SUFFIXES:
            texture_count += 1
    return {
        "material_files": suffix_counts[".material"],
        "model_category_counts": dict(sorted(model_categories.items())),
        "model_files": model_count,
        "object_definition_files": suffix_counts[".odef"],
        "suffix_counts": dict(sorted(suffix_counts.items())),
        "texture_files": texture_count,
    }


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

    try:
        with zipfile.ZipFile(path) as archive:
            infos = archive.infolist()
            expanded_bytes, names = validate_archive_members(infos)
            terrain_info = find_required_member(names, ".terrn2")
            placement_info = find_required_member(names, ".tobj")
            terrain = parse_terrain(
                read_text_member(archive, terrain_info),
                terrain_info.filename,
            )
            placements = parse_placements(
                read_text_member(archive, placement_info)
            )
    except zipfile.BadZipFile as exc:
        raise AuditFailure("input is not a valid ZIP archive") from exc

    odefs = object_definition_index(infos)
    objects, unresolved = build_object_report(
        placements["object_counts"], odefs
    )
    telepoints = terrain["telepoints"]
    warnings: list[dict[str, object]] = []
    if placements["malformed"]:
        warnings.append(
            {
                "code": "PLACEMENT_MALFORMED",
                "count": placements["malformed"],
            }
        )
    if unresolved:
        warnings.append(
            {
                "code": "PLACEMENT_DEFINITION_UNRESOLVED",
                "count": len(unresolved),
                "objects": unresolved,
            }
        )

    return {
        "archive": {
            "bytes": path.stat().st_size,
            "declared_expanded_bytes": expanded_bytes,
            "entries": len(infos),
            "name": path.name,
            "sha256": digest,
        },
        "assets": asset_inventory(infos),
        "city_clusters": nearest_telepoint_clusters(
            placements["records"], telepoints
        ),
        "format": REPORT_FORMAT,
        "intercity_links": intercity_links(telepoints),
        "ok": True,
        "placements": {
            "bounds": placements["bounds"],
            "category_counts": placements["category_counts"],
            "commented_placements": placements["commented_placements"],
            "directives": placements["directives"],
            "file": placement_info.filename,
            "malformed": placements["malformed"],
            "objects": objects,
            "total": placements["total"],
            "unique_objects": len(objects),
        },
        "terrain": terrain,
        "warnings": warnings,
    }


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--expect-sha256")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--pretty", action="store_true")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        report = audit_archive(
            args.archive, expected_sha256=args.expect_sha256
        )
    except (AuditFailure, OSError) as exc:
        print(f"CityWorld visual audit failed: {exc}", file=sys.stderr)
        return 2

    payload = canonical_json(report, pretty=args.pretty)
    if args.output is None:
        sys.stdout.write(payload)
    else:
        args.output.write_text(payload, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
