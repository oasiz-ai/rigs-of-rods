#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Apply the authored Alexis Saber cabin interior to the vehicle archive.

``USER:/mods/AlexisSaber.zip`` has no cabin: the only geometry behind the
glass is the near-black chassis tub, and the seats/dash the original author
intended are commented-out props referencing ``AlexisProxim*.mesh`` members
another mod owns.  Through the ~68 per cent glass the cabin therefore reads
as a pure black void.  This rewrites the archive in place:

* ``AlexisSaberInterior.mesh`` is added - the static interior shell from
  ``tools/alexis_saber_interior.py``, compiled with the pinned
  OgreXMLConverter (the same binary the CityWorld asset pipeline pins);
* ``AlexisSaber.truck`` gains one flexbody entry placing the shell with the
  exact placement line and forset the body mesh already uses;
* ``AlexisSaberWheel.png`` / ``AlexisSaberWheelSpec.png`` gain the three
  interior swatches (seat fabric, carpet, trim) in background texels no
  ``SaberWheels`` face samples; the wheel-art texels keep their decoded
  values.  The shell binds the already-reviewed ``SaberWheels`` managed
  material, so the combined presenter's exact Alexis material review set is
  untouched.

Every other member is copied byte for byte, exactly as
``apply_alexis_saber_paint.py`` does (this tool reuses its archive
machinery).  All patches are idempotent, so the tool is safe to re-run
against its own output, and the compiled mesh is proven deterministic by
converting twice per invocation and requiring identical bytes.

The archive digest is deliberately not pinned anywhere in source/ or tools/
(see the paint tool's module docstring), so replacing members does not
invalidate any mount-time verification.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util as _importlib_util
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def _load_sibling(name: str):
    spec = _importlib_util.spec_from_file_location(
        name, Path(__file__).resolve().parent / f"{name}.py")
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load tools/{name}.py")
    module = _importlib_util.module_from_spec(spec)
    sys.modules.setdefault(name, module)
    spec.loader.exec_module(module)
    return module


paint_tool = _load_sibling("apply_alexis_saber_paint")
interior = _load_sibling("alexis_saber_interior")

ArchivePatchError = paint_tool.ArchivePatchError

TRUCK_MEMBER = "AlexisSaber.truck"

#: The archive uses CRLF for its text members; the truck patch preserves it.
_EOL = b"\r\n"

#: The shell is inserted directly after this flexbody's forset line - the
#: interior window shell, the last cabin flexbody in the shipped file - so
#: the section keeps reading top-down as body, glass, interior.
_ANCHOR_MESH_LINE = (
    b"20, 94, 5, 0.1, 0.5, -0.4, 180, 90, 0, AlexisSaberWinds_int.mesh")

#: The identity pin for the mesh compiler, matching the CityWorld pipeline
#: (``tools/compile_cityworld_asset.py``): the version string is checked via
#: ``-v`` and the binary digest is recorded in the run report.
OGRE_CONVERTER_VERSION = "OgreXMLConverter Tsathoggua (14.5.2)"
DEFAULT_CONVERTER = Path.home() / (
    ".conan2/p/b/ogre3637f0f0e53424/p/bin/macosx/OgreXMLConverter")
OGRE_MESH_HEADER = bytes.fromhex("00105b4d65736853657269616c697a6572")


def _converter_version(converter: Path) -> str:
    if not converter.is_file() or converter.is_symlink():
        raise ArchivePatchError(
            f"OgreXMLConverter is missing or unsafe: {converter}")
    try:
        result = subprocess.run(
            [str(converter.resolve()), "-v"], check=False,
            capture_output=True, text=True, timeout=60)
    except (OSError, subprocess.SubprocessError) as error:
        raise ArchivePatchError(
            f"cannot execute OgreXMLConverter: {error}") from error
    version = (result.stdout or "").strip().splitlines()
    return version[0].strip() if version else ""


def _convert_once(converter: Path, xml_payload: bytes, workdir: Path,
                  tag: str) -> bytes:
    xml_path = workdir / f"AlexisSaberInterior.{tag}.mesh.xml"
    mesh_path = workdir / f"AlexisSaberInterior.{tag}.mesh"
    log_path = workdir / f"AlexisSaberInterior.{tag}.log"
    xml_path.write_bytes(xml_payload)
    command = [str(converter), "-q", "-gl", "-E", "little",
               "-log", str(log_path), str(xml_path), str(mesh_path)]
    try:
        result = subprocess.run(command, check=False, capture_output=True,
                                text=True, timeout=120)
    except (OSError, subprocess.SubprocessError) as error:
        raise ArchivePatchError(
            f"OgreXMLConverter failed to execute: {error}") from error
    if result.returncode != 0 or not mesh_path.is_file():
        message = (result.stderr or result.stdout or "").strip()
        raise ArchivePatchError(
            f"OgreXMLConverter rejected the interior mesh: {message[:500]}")
    payload = mesh_path.read_bytes()
    if not payload.startswith(OGRE_MESH_HEADER):
        raise ArchivePatchError(
            "the compiled interior mesh does not use the pinned OGRE format")
    return payload


def compile_interior_mesh(converter: Path) -> tuple[bytes, dict[str, str]]:
    """Compiles the authored XML twice and requires identical bytes."""

    version = _converter_version(converter)
    if version != OGRE_CONVERTER_VERSION:
        raise ArchivePatchError(
            f"OgreXMLConverter version is not pinned (expected "
            f"{OGRE_CONVERTER_VERSION!r}, received {version!r})")
    xml_payload = interior.build_mesh_xml()
    with tempfile.TemporaryDirectory(
            prefix="ror-alexis-interior-") as tempdir:
        workdir = Path(tempdir)
        first = _convert_once(converter, xml_payload, workdir, "a")
        second = _convert_once(converter, xml_payload, workdir, "b")
    if first != second:
        raise ArchivePatchError(
            "OgreXMLConverter output is not deterministic; refusing to "
            "author an unreproducible member")
    identity = {
        "converter": str(converter),
        "converter_sha256": hashlib.sha256(
            converter.read_bytes()).hexdigest(),
        "mesh_sha256": hashlib.sha256(first).hexdigest(),
    }
    return first, identity


def patch_truck(payload: bytes) -> bytes:
    """Adds the interior flexbody after the interior glass shell. Idempotent."""

    lines = payload.split(_EOL)
    mesh_line = interior.TRUCK_FLEXBODY_LINE
    forset_line = interior.TRUCK_FORSET_LINE
    present = [index for index, line in enumerate(lines)
               if line.strip() == mesh_line]
    if len(present) > 1:
        raise ArchivePatchError(
            "the truck file declares the interior flexbody more than once")
    if len(present) == 1:
        follow = lines[present[0] + 1].strip() if (
            present[0] + 1 < len(lines)) else b""
        if follow != forset_line:
            raise ArchivePatchError(
                "the interior flexbody is not followed by its forset line")
        return payload

    anchors = [index for index, line in enumerate(lines)
               if line.strip() == _ANCHOR_MESH_LINE]
    if len(anchors) != 1:
        raise ArchivePatchError(
            f"expected exactly one interior-glass flexbody line, found "
            f"{len(anchors)}")
    anchor = anchors[0]
    if anchor + 1 >= len(lines) or not lines[anchor + 1].strip().startswith(
            b"forset"):
        raise ArchivePatchError(
            "the interior-glass flexbody is not followed by a forset line")
    insertion = anchor + 2
    return _EOL.join(lines[:insertion] +
                     [b"", mesh_line, forset_line] +
                     lines[insertion:])


def patch_archive(archive: bytes,
                  converter: Path) -> tuple[bytes, dict[str, str]]:
    """Returns the rewritten archive and a name -> action report."""

    entries = paint_tool._read_entries(archive)
    by_name = {}
    for entry in entries:
        if entry.name in by_name:
            raise ArchivePatchError(f"duplicate member '{entry.name}'")
        by_name[entry.name] = entry
    for required in (TRUCK_MEMBER, interior.WHEEL_MEMBER,
                     interior.WHEEL_SPEC_MEMBER):
        if required not in by_name:
            raise ArchivePatchError(f"archive is missing '{required}'")

    mesh_payload, identity = compile_interior_mesh(converter)
    authored = interior.build_wheel_members(
        paint_tool._member_payload(by_name[interior.WHEEL_MEMBER]),
        paint_tool._member_payload(by_name[interior.WHEEL_SPEC_MEMBER]))
    authored[interior.MESH_MEMBER] = mesh_payload

    report: dict[str, str] = {}
    rebuilt = []
    for entry in entries:
        if entry.name in authored:
            payload = authored.pop(entry.name)
            report[entry.name] = (
                "unchanged" if paint_tool._member_payload(entry) == payload
                else "replaced")
            rebuilt.append(paint_tool._authored_entry(entry.name, payload))
        elif entry.name == TRUCK_MEMBER:
            original = paint_tool._member_payload(entry)
            payload = patch_truck(original)
            if payload == original:
                report[entry.name] = "unchanged"
                rebuilt.append(entry)
            else:
                report[entry.name] = "patched"
                rebuilt.append(paint_tool._authored_entry(entry.name, payload))
        else:
            report[entry.name] = "verbatim"
            rebuilt.append(entry)
    for name, payload in authored.items():
        report[name] = "added"
        rebuilt.append(paint_tool._authored_entry(name, payload))
    return paint_tool._rebuild(rebuilt), report, identity


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Apply the authored Alexis Saber interior to the "
                    "archive.")
    parser.add_argument(
        "--archive", type=Path, required=True,
        help="path to AlexisSaber.zip")
    parser.add_argument(
        "--output", type=Path,
        help="write here instead of rewriting --archive in place")
    parser.add_argument(
        "--backup", type=Path,
        help="copy the untouched archive here before rewriting it")
    parser.add_argument(
        "--converter", type=Path, default=DEFAULT_CONVERTER,
        help="path to the pinned OgreXMLConverter")
    parser.add_argument(
        "--dry-run", action="store_true",
        help="report what would change without writing anything")
    args = parser.parse_args(argv)

    original = args.archive.read_bytes()
    patched, report, identity = patch_archive(original, args.converter)
    verification = paint_tool.compare_archives(original, patched)

    for key in ("converter", "converter_sha256", "mesh_sha256"):
        print(f"{key}: {identity[key]}")
    for name in sorted(report):
        action = report[name]
        if action != "verbatim":
            print(f"{action:>9}  {name}")
    changed = sorted(name for name, state in verification.items()
                     if state != "identical")
    print(f"members: {len(verification)} "
          f"({sum(1 for s in verification.values() if s == 'added')} added, "
          f"{sum(1 for s in verification.values() if s == 'changed')} "
          f"changed)")
    for name in changed:
        print(f"  {verification[name]}: {name}")

    if args.dry_run:
        return 0

    destination = args.output or args.archive
    if args.backup is not None and not args.backup.exists():
        args.backup.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(args.archive, args.backup)
        print(f"backed up to {args.backup}")
    destination.write_bytes(patched)
    print(f"wrote {destination} "
          f"({len(patched)} bytes, "
          f"sha256 {hashlib.sha256(patched).hexdigest()})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
