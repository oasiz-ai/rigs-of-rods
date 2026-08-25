#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Restage the terrain members of an existing CityWorld Next overlay archive.

A full `build_cityworld_local_overlay.py` run regenerates every member, which
drags in the compiled-asset provenance checks even when nothing but the terrain
changed. This tool rewrites only the terrain configuration and blend map,
copying every other member through untouched, and it reproduces exactly what the
full builder would have written: the blend map comes from the same
`cityworld_terrain_layers.rasterize_blend_channels` call, fed with the routes
and sites read back out of the manifest the archive already ships. An optional
archive-derived coverage PNG must be supplied explicitly and kept outside the
repository.

The archive is deterministic ZIP_STORED with a fixed timestamp and mode, so
rewriting it from its own members is byte-exact and re-running this tool on its
own output reports nothing changed.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import sys
import zipfile
from pathlib import Path

_TOOLS = Path(__file__).resolve().parent


def _load(name: str):
    spec = importlib.util.spec_from_file_location(name, _TOOLS / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


terrain_layers = _load("cityworld_terrain_layers")

#: Members this tool owns. Everything else is copied through verbatim.
GLOBAL_OTC = "CityWorldNextEnhanced.otc"
PAGE_OTC = "CityWorldNextEnhanced-page-0-0.otc"
BLEND_MAP = "cityworld_next_terrain_blend.png"
INFILL_MANIFEST = "cityworld_next_infill_manifest.v2.json"
REPORT = "cityworld_next_local_overlay.report.json"

ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)
ZIP_MODE = 0o100644


class RestageError(RuntimeError):
    pass


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def canonical_json_bytes(value: dict) -> bytes:
    return (
        json.dumps(
            value,
            ensure_ascii=True,
            sort_keys=True,
            separators=(",", ":"),
        )
        + "\n"
    ).encode("utf-8")


def read_members(path: Path) -> dict[str, bytes]:
    with zipfile.ZipFile(path) as archive:
        for info in archive.infolist():
            if info.compress_type != zipfile.ZIP_STORED:
                raise RestageError(
                    f"{info.filename} is not stored uncompressed; this archive "
                    "was not written by the overlay builder")
        return {info.filename: archive.read(info) for info in archive.infolist()}


def write_deterministic_zip(path: Path, payloads: dict[str, bytes]) -> None:
    """Byte-for-byte the same writer the overlay builder uses."""
    with zipfile.ZipFile(path, mode="w",
                         compression=zipfile.ZIP_STORED, allowZip64=True) as archive:
        for name in sorted(payloads):
            info = zipfile.ZipInfo(name, date_time=ZIP_TIMESTAMP)
            info.compress_type = zipfile.ZIP_STORED
            info.create_system = 3
            info.create_version = 20
            info.extract_version = 20
            info.external_attr = ZIP_MODE << 16
            info.extra = b""
            info.comment = b""
            archive.writestr(info, payloads[name])


def terrain_inputs(manifest: dict) -> tuple[list[dict], list[dict]]:
    """The route and site views the overlay builder hands the layer generator."""
    routes = []
    for route in manifest.get("access_routes", ()):
        points = route.get("points") or ()
        if len(points) < 2:
            continue
        routes.append({
            "route_id": route.get("route_id"),
            "xz_points": tuple((float(p["position_m"][0]), float(p["position_m"][2]))
                               for p in points),
            "width_m": max(float(p["width_m"]) for p in points),
        })
    sites = [{
        "site_id": site.get("site_id"),
        "category": site.get("category"),
        "polygon_xz_m": site.get("polygon_xz_m"),
    } for site in manifest.get("sites", ())]
    return routes, sites


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--overlay", required=True, type=Path)
    ap.add_argument("--repo-root", required=True, type=Path)
    ap.add_argument("--output", required=True, type=Path)
    ap.add_argument("--derived-coverage", type=Path)
    args = ap.parse_args(argv)

    repository = args.repo_root.resolve()
    derived_coverage = None
    if args.derived_coverage is not None:
        if args.derived_coverage.is_symlink():
            raise RestageError(
                "derived coverage input cannot be a symbolic link")
        derived_coverage = args.derived_coverage.resolve()
        if not derived_coverage.is_file():
            raise RestageError(
                "derived coverage input does not exist or is not a regular file")
        try:
            derived_coverage.relative_to(repository)
        except ValueError:
            pass
        else:
            raise RestageError(
                "derived coverage input must stay outside the repository")

    members = read_members(args.overlay)
    for required in (INFILL_MANIFEST, REPORT):
        if required not in members:
            raise RestageError(f"the archive has no {required}")

    manifest = json.loads(members[INFILL_MANIFEST])
    report = json.loads(members[REPORT])
    routes, sites = terrain_inputs(manifest)
    derived_record = None
    if derived_coverage is not None:
        derived_payload = derived_coverage.read_bytes()
        derived_record = {
            "bytes": len(derived_payload),
            "name": derived_coverage.name,
            "sha256": sha256_bytes(derived_payload),
        }
    derived = (
        terrain_layers.load_derived_coverage(
            derived_coverage,
            size=terrain_layers.BLEND_MAP_SIZE,
        )
        if derived_coverage is not None
        else None
    )
    channels = terrain_layers.rasterize_blend_channels(
        routes,
        sites,
        size=terrain_layers.BLEND_MAP_SIZE,
        derived=derived,
    )
    if (
        derived_coverage is not None
        and derived_record is not None
        and sha256_bytes(derived_coverage.read_bytes())
        != derived_record["sha256"]
    ):
        raise RestageError("derived coverage input changed while it was read")

    staged = {
        GLOBAL_OTC: terrain_layers.build_global_otc(PAGE_OTC).encode("utf-8"),
        PAGE_OTC: terrain_layers.build_page_otc().encode("utf-8"),
        BLEND_MAP: terrain_layers.encode_png_rgba(
            terrain_layers.BLEND_MAP_SIZE, *channels),
    }
    package = report.get("package")
    records = package.get("files") if isinstance(package, dict) else None
    if not isinstance(records, list):
        raise RestageError("the archive report has no package file inventory")
    by_path = {
        record.get("path"): record
        for record in records
        if isinstance(record, dict)
    }
    for name, payload in staged.items():
        record = by_path.get(name)
        if not isinstance(record, dict):
            raise RestageError(
                f"the archive report has no package record for {name}")
        record["sha256"] = sha256_bytes(payload)
        record["size"] = len(payload)
    source = report.get("source")
    archive = source.get("archive") if isinstance(source, dict) else None
    source_archive_sha256 = (
        archive.get("expected_sha256") if isinstance(archive, dict) else None
    )
    if not isinstance(source_archive_sha256, str):
        raise RestageError("the archive report has no source archive digest")
    report["terrain_coverage"] = {
        "cityworld_archive_derived_input": derived_record,
        "local_only": True,
        "mode": (
            "explicit-local-cityworld-derived-plus-project-authored"
            if derived_record is not None
            else "project-authored-routes-and-sites-only"
        ),
        "redistribution_allowed": False,
        "source_archive_sha256": source_archive_sha256,
    }
    staged[REPORT] = canonical_json_bytes(report)

    added = [n for n in staged if n not in members]
    changed = [n for n in staged if n in members and members[n] != staged[n]]
    updated = dict(members)
    updated.update(staged)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    write_deterministic_zip(args.output, updated)

    # every member this tool does not own must survive unchanged
    result = read_members(args.output)
    for name, payload in members.items():
        if name in staged:
            continue
        if result.get(name) != payload:
            raise RestageError(f"member {name} was not preserved byte-for-byte")

    print(f"members: {len(updated)} ({len(added)} added, {len(changed)} changed, "
          f"{len(updated) - len(added) - len(changed)} unchanged)")
    for name in sorted(added):
        print(f"  added   {name}")
    for name in sorted(changed):
        print(f"  changed {name}  {len(members[name])} -> {len(staged[name])} bytes")
    digest = hashlib.sha256(args.output.read_bytes()).hexdigest()
    print(f"wrote {args.output}\nsha256 {digest}\nbytes  {args.output.stat().st_size}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
