#!/usr/bin/env python3
"""Author UV0 texture coordinates into AlexisSaberTrunk.mesh inside AlexisSaber.zip.

Problem
-------
``AlexisSaberTrunk.mesh`` ships with positions and normals only -- no UV0
channel.  The legacy renderer logs ``FLEXBODY Warning: at least one part of
this mesh does not have texture coordinates, switching off texturing!`` and
the combined renderer's material census fails the section closed
(``missing_authored_uv0``), so the trunk lid renders as an untextured matte
band instead of body paint.

Fix
---
This tool authors a deterministic planar UV projection into every vertex
buffer of the trunk mesh that carries positions but no texture coordinates:

* The lid is a predominantly horizontal panel (x spans the car's width,
  z its length, y only 17 cm of curvature), so the projection is planar
  from above (+Y): ``u = (x - xmin) / (xmax - xmin)`` and
  ``v = (zmax - z) / (zmax - zmin)``, normalised per submesh to the full
  [0,1] x [0,1] square.
* The trunk is painted by the ``SaberBody`` managed material
  (``flexmesh_standard bodytemp.png - bodytempspec.png``).  The sibling
  panels (AlexisSaberHood.mesh, AlexisSaberDoors.mesh) each unwrap onto
  their own full copy of the unit square -- bodytemp.png is not a shared
  atlas -- and every SaberBody paint layer (bodytemp.png, bodytempspec.png,
  all body_*.png skin variants) is spatially uniform paint grain with no
  authored per-region layout, so a full-square planar projection reproduces
  exactly what the siblings show.  Vertical lid-edge faces inherit stretched
  grain, which is invisible at the paint textures' +/-2 LSB grain amplitude.

Procedure (mirrors tools/compile_cityworld_asset.py)
----------------------------------------------------
1. Verify the pinned OgreXMLConverter (sha256 + version banner).
2. Extract the member, convert mesh -> XML with the pinned converter.
3. Add ``texture_coords="1" texture_coord_dimensions_0="float2"`` and a
   ``<texcoord u v>`` per vertex to buffers lacking texture coordinates.
   Buffers that already carry texture coordinates are never modified, which
   makes the tool idempotent: a re-run reports 0 changes and leaves the
   archive untouched.
4. Convert XML -> mesh twice and require byte-identical outputs
   (determinism proof), then require the pinned mesh serializer header.
5. Replace only that member in the archive (Info-ZIP update mode, ``-X``,
   member mtime pinned to 2026-01-01 00:00:00 local like the archive's
   other normalised members), then verify per member that every other
   member's stored CRC, sizes, compression method, timestamp and
   decompressed bytes are unchanged before atomically installing the
   result over the input archive.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

PINNED_CONVERTER = Path(
    "~/.conan2/p/b/ogre3637f0f0e53424/p/bin/macosx/OgreXMLConverter"
).expanduser()
PINNED_CONVERTER_SHA256 = (
    "15dbcde2ea124f0645d0c0f2ee809effee35123b0799f542742b5d2dd42b507f"
)
PINNED_CONVERTER_VERSION = "OgreXMLConverter Tsathoggua (14.5.2)"
OGRE_MESH_HEADER = b"\x00\x10[MeshSerializer_v1.100]\n"
DEFAULT_MEMBER = "AlexisSaberTrunk.mesh"
# The archive's previously-normalised members carry this local timestamp.
MEMBER_MTIME = (2026, 1, 1, 0, 0, 0)
MIN_PROJECTION_RANGE = 1e-6


class AuthoringFailure(RuntimeError):
    pass


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify_converter(converter: Path) -> None:
    if not converter.is_file() or converter.is_symlink():
        raise AuthoringFailure(f"OgreXMLConverter is missing or unsafe: {converter}")
    actual = sha256_file(converter)
    if actual != PINNED_CONVERTER_SHA256:
        raise AuthoringFailure(
            "OgreXMLConverter sha256 is not the pinned converter "
            f"(expected {PINNED_CONVERTER_SHA256}, received {actual})"
        )
    with tempfile.TemporaryDirectory(prefix="ror-trunk-uv-identity-") as tmp:
        result = subprocess.run(
            [str(converter.resolve()), "-v"],
            check=False,
            capture_output=True,
            text=True,
            timeout=60,
            cwd=tmp,
        )
    version = (result.stdout or "").strip().splitlines()
    banner = version[0].strip() if version else ""
    if result.returncode != 0 or banner != PINNED_CONVERTER_VERSION:
        raise AuthoringFailure(
            "OgreXMLConverter version is not pinned "
            f"(expected {PINNED_CONVERTER_VERSION!r}, received {banner!r})"
        )


def run_converter(converter: Path, source: Path, dest: Path, log: Path) -> None:
    command = [
        str(converter),
        "-q",
        "-gl",
        "-E",
        "little",
        "-log",
        str(log),
        str(source),
        str(dest),
    ]
    result = subprocess.run(
        command,
        check=False,
        capture_output=True,
        text=True,
        timeout=120,
    )
    if result.returncode != 0:
        message = (result.stderr or result.stdout).strip()
        raise AuthoringFailure(
            f"OgreXMLConverter rejected {source.name}: {message[:1000]}"
        )
    if not dest.is_file():
        raise AuthoringFailure(f"OgreXMLConverter did not create {dest.name}")


def buffer_has_texcoords(buffer: ET.Element) -> bool:
    coords = buffer.get("texture_coords")
    return coords is not None and coords != "0"


def author_planar_uv0(root: ET.Element) -> list[dict[str, object]]:
    """Add planar UV0 to buffers lacking texcoords. Returns per-buffer reports."""
    reports: list[dict[str, object]] = []
    geometries: list[tuple[str, ET.Element]] = []
    shared = root.find("sharedgeometry")
    if shared is not None:
        geometries.append(("sharedgeometry", shared))
    submeshes = root.find("submeshes")
    if submeshes is not None:
        for index, submesh in enumerate(submeshes.findall("submesh")):
            geometry = submesh.find("geometry")
            if geometry is not None:
                label = (
                    f"submesh[{index}] material="
                    f"{submesh.get('material', '?')}"
                )
                geometries.append((label, geometry))
    for label, geometry in geometries:
        for buffer in geometry.findall("vertexbuffer"):
            if buffer.get("positions") != "true":
                continue
            report: dict[str, object] = {"section": label}
            if buffer_has_texcoords(buffer):
                report["action"] = "kept-existing-texcoords"
                reports.append(report)
                continue
            vertices = buffer.findall("vertex")
            positions = []
            for vertex in vertices:
                position = vertex.find("position")
                if position is None:
                    raise AuthoringFailure(
                        f"{label}: vertex without position in a positions buffer"
                    )
                positions.append(
                    (float(position.get("x")), float(position.get("z")))
                )
            if not positions:
                raise AuthoringFailure(f"{label}: empty positions buffer")
            xs = [p[0] for p in positions]
            zs = [p[1] for p in positions]
            x_min, x_max = min(xs), max(xs)
            z_min, z_max = min(zs), max(zs)
            if (x_max - x_min) < MIN_PROJECTION_RANGE or (
                z_max - z_min
            ) < MIN_PROJECTION_RANGE:
                raise AuthoringFailure(
                    f"{label}: degenerate XZ extent, planar projection unsafe"
                )
            buffer.set("texture_coords", "1")
            buffer.set("texture_coord_dimensions_0", "float2")
            for vertex, (x, z) in zip(vertices, positions):
                u = (x - x_min) / (x_max - x_min)
                v = (z_max - z) / (z_max - z_min)
                texcoord = ET.SubElement(vertex, "texcoord")
                texcoord.set("u", f"{u:.6f}")
                texcoord.set("v", f"{v:.6f}")
            report["action"] = "authored-planar-uv0"
            report["vertices"] = len(vertices)
            report["x_range"] = (x_min, x_max)
            report["z_range"] = (z_min, z_max)
            reports.append(report)
    return reports


def member_signature(info: zipfile.ZipInfo) -> tuple:
    return (
        info.filename,
        info.CRC,
        info.file_size,
        info.compress_size,
        info.compress_type,
        info.date_time,
    )


def verify_repack(
    original: Path, updated: Path, member: str, expected_mesh: bytes
) -> None:
    with zipfile.ZipFile(original) as old, zipfile.ZipFile(updated) as new:
        old_names = old.namelist()
        new_names = new.namelist()
        if sorted(old_names) != sorted(new_names):
            raise AuthoringFailure(
                "repacked archive gained or lost members: "
                f"{sorted(set(old_names) ^ set(new_names))}"
            )
        for name in old_names:
            old_info = old.getinfo(name)
            new_info = new.getinfo(name)
            if name == member:
                if new.read(name) != expected_mesh:
                    raise AuthoringFailure(
                        f"repacked member {name} does not hold the authored mesh"
                    )
                continue
            if member_signature(old_info) != member_signature(new_info):
                raise AuthoringFailure(
                    f"untouched member {name} changed metadata during repack: "
                    f"{member_signature(old_info)} -> {member_signature(new_info)}"
                )
            if old.read(name) != new.read(name):
                raise AuthoringFailure(
                    f"untouched member {name} changed bytes during repack"
                )


def repack_member(archive: Path, member: str, mesh_bytes: bytes, workdir: Path) -> Path:
    staging = workdir / "staging"
    staging.mkdir(parents=True, exist_ok=True)
    member_path = staging / member
    member_path.write_bytes(mesh_bytes)
    mtime = time.mktime(MEMBER_MTIME + (0, 0, -1))
    os.utime(member_path, (mtime, mtime))
    updated = workdir / "updated.zip"
    shutil.copyfile(archive, updated)
    result = subprocess.run(
        ["zip", "-X", "-q", str(updated), member],
        check=False,
        capture_output=True,
        text=True,
        cwd=staging,
        timeout=120,
    )
    if result.returncode != 0:
        raise AuthoringFailure(
            f"zip update failed: {(result.stderr or result.stdout).strip()[:500]}"
        )
    verify_repack(archive, updated, member, mesh_bytes)
    return updated


def install_over(archive: Path, updated: Path) -> None:
    tmp_target = archive.with_name(archive.name + ".trunk-uv-tmp")
    shutil.copyfile(updated, tmp_target)
    os.replace(tmp_target, archive)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--archive",
        required=True,
        type=Path,
        help="AlexisSaber.zip to edit in place",
    )
    parser.add_argument(
        "--converter",
        type=Path,
        default=PINNED_CONVERTER,
        help="pinned OgreXMLConverter (sha256 verified)",
    )
    parser.add_argument(
        "--member",
        default=DEFAULT_MEMBER,
        help="archive member to author UV0 into",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="report what would change without modifying the archive",
    )
    args = parser.parse_args(argv)

    archive: Path = args.archive
    if not archive.is_file():
        raise AuthoringFailure(f"archive not found: {archive}")
    verify_converter(args.converter)

    with tempfile.TemporaryDirectory(prefix="ror-trunk-uv-") as tmp:
        workdir = Path(tmp)
        with zipfile.ZipFile(archive) as bundle:
            names = [n for n in bundle.namelist() if n == args.member]
            if len(names) != 1:
                raise AuthoringFailure(
                    f"archive must hold exactly one {args.member!r}, found {len(names)}"
                )
            original_mesh = bundle.read(args.member)
        mesh_path = workdir / args.member
        mesh_path.write_bytes(original_mesh)
        xml_path = workdir / (args.member + ".xml")
        log_path = workdir / "converter.log"
        run_converter(args.converter, mesh_path, xml_path, log_path)

        tree = ET.parse(xml_path)
        reports = author_planar_uv0(tree.getroot())
        for report in reports:
            print(f"[trunk-uv] {report}")
        authored = [r for r in reports if r["action"] == "authored-planar-uv0"]
        if not any(r["action"] == "kept-existing-texcoords" for r in reports) and not authored:
            raise AuthoringFailure("no positions vertex buffers found in member")
        if not authored:
            print(
                f"[trunk-uv] 0 changes: every vertex buffer in {args.member} "
                "already has texture coordinates; archive untouched"
            )
            return 0
        if args.check:
            print(
                f"[trunk-uv] check mode: {len(authored)} vertex buffer(s) would "
                "gain planar UV0; archive untouched"
            )
            return 0

        authored_xml = workdir / (args.member + ".uv.xml")
        tree.write(authored_xml, encoding="UTF-8", xml_declaration=True)
        first_mesh = workdir / "authored-a.mesh"
        second_mesh = workdir / "authored-b.mesh"
        run_converter(args.converter, authored_xml, first_mesh, log_path)
        run_converter(args.converter, authored_xml, second_mesh, log_path)
        first_bytes = first_mesh.read_bytes()
        if first_bytes != second_mesh.read_bytes():
            raise AuthoringFailure(
                "pinned converter produced non-deterministic mesh bytes"
            )
        if not first_bytes.startswith(OGRE_MESH_HEADER):
            raise AuthoringFailure(
                "authored mesh does not use the pinned OGRE mesh serializer"
            )

        updated = repack_member(archive, args.member, first_bytes, workdir)
        install_over(archive, updated)
        print(
            f"[trunk-uv] authored planar UV0 into {len(authored)} vertex "
            f"buffer(s) of {args.member}"
        )
        print(f"[trunk-uv] member sha256 before: {sha256_bytes(original_mesh)}")
        print(f"[trunk-uv] member sha256 after:  {sha256_bytes(first_bytes)}")
        print(f"[trunk-uv] archive sha256 after: {sha256_file(archive)}")
        print("[trunk-uv] changed_members=1 (all other members verified byte-identical)")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except AuthoringFailure as failure:
        print(f"[trunk-uv] FAILURE: {failure}", file=sys.stderr)
        sys.exit(1)
