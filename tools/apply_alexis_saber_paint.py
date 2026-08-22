#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Apply the authored Alexis Saber paint set to the vehicle archive.

``USER:/mods/AlexisSaber.zip`` ships real art for most of the car and 5x5
flat-colour placeholders where the body paint belongs.  This rewrites the
archive in place:

* the seven placeholder members are replaced with the authored paint from
  ``tools/alexis_saber_paint.py``;
* five new ``body_<colour>spec.png`` members are added, one per colour skin,
  because the skin system swaps texture units by name and the body's specular
  slot needs a per-skin target to swap to;
* ``AlexisSaber.truck`` has its commented-out ``SaberBody`` managedmaterial
  restored, so the body binds the same ``flexmesh_standard`` template the
  chassis already binds successfully instead of falling through to the
  Cg-program material ``SaberBody : AurigaPaint``;
* ``AlexisSaber.skin`` gains the paired specular replacement per skin;
* ``AlexisSaberWinds2.png`` (the glass tint both window materials sample) is
  re-derived from the archive's own full-range mask ``AlexisSaberWinds.png``
  so the two-pane windshield transmits ~68 per cent instead of the shipped
  ~0.3 per cent (see ``build_glass_member`` for the composition math).

Every other member is copied byte for byte: the local header, the extra
field and the deflate stream are reproduced verbatim, so untouched members
keep their exact stored bytes and not merely their content.  Both text
patches are idempotent, so the tool is safe to re-run against its own output.

The archive digest is deliberately *not* pinned in source/ or tools/.  Only
CityWorld.zip carries a compatibility digest
(``kCityWorldLegacyMaterialCompatibilityArchiveSha256`` in
source/main/resources/LegacyMaterialCompatibilityPlan.h, re-verified at
mount); AlexisSaber.zip has no equivalent pin, so replacing its members does
not invalidate any mount-time verification.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util as _importlib_util
from pathlib import Path
import shutil
import struct
import sys
import zlib

_PAINT_SPEC = _importlib_util.spec_from_file_location(
    "alexis_saber_paint",
    Path(__file__).resolve().parent / "alexis_saber_paint.py",
)
if _PAINT_SPEC is None or _PAINT_SPEC.loader is None:
    raise RuntimeError("could not load the Alexis Saber paint generator")
alexis_saber_paint = _importlib_util.module_from_spec(_PAINT_SPEC)
sys.modules.setdefault(_PAINT_SPEC.name, alexis_saber_paint)
_PAINT_SPEC.loader.exec_module(alexis_saber_paint)


class ArchivePatchError(RuntimeError):
    """Raised when the archive cannot be rewritten exactly."""


TRUCK_MEMBER = "AlexisSaber.truck"
SKIN_MEMBER = "AlexisSaber.skin"
MATERIAL_MEMBER = "Auriga327.material"

#: The dead Cg material the body meshes used to resolve to.
#:
#: Every SaberBody submesh names the material "SaberBody". The spawner wants to
#: install a supportable placeholder under that name before the meshes load
#: (ActorSpawner::ProcessManagedMaterial), but it only does so when nothing
#: owns the name yet - and this script block owns it, so the spawner logs
#: "Placeholder already exists: 'SaberBody'" and skips. OGRE then loads this
#: block for every body submesh, finds AurigaMatPaint_VP unusable because the
#: Cg plugin is disabled in this build, and warns that the material "has no
#: supportable Techniques and will be blank".
#:
#: The managed material is unaffected - it is created under the spawner's
#: composed name and swapped in afterwards - so this block is pure dead weight
#: that can never compile here. Removing it lets the spawner install its own
#: placeholder and the warning disappears.
MATERIAL_DEAD_BODY_HEADER = b"material SaberBody : AurigaPaint"

#: The archive uses CRLF for its text members; both patches preserve that.
_EOL = b"\r\n"

#: The body's managedmaterial, commented out in the shipped truck file. It is
#: the same shape as the SaberChassis line directly beneath it, which the
#: presenter already admits: flexmesh_standard, no damaged diffuse, one
#: specular map.
TRUCK_COMMENTED_BODY = (
    b";SaberBody flexmesh_standard bodytemp.png - bodytempspec.png")
TRUCK_RESTORED_BODY = (
    b"SaberBody flexmesh_standard bodytemp.png - bodytempspec.png")

#: A fixed timestamp so the rewritten archive is byte-reproducible; the value
#: itself carries no meaning.
AUTHORED_MEMBER_TIMESTAMP = (2026, 1, 1, 0, 0, 0)

#: The shipped members all record create_system 0 with the DOS archive bit;
#: authored members match so the listing stays homogeneous.
_AUTHORED_VERSION_MADE_BY = 20
_AUTHORED_EXTERNAL_ATTR = 0x20


def _dos_timestamp(moment: tuple[int, int, int, int, int, int]
                   ) -> tuple[int, int]:
    year, month, day, hour, minute, second = moment
    if year < 1980:
        raise ArchivePatchError("DOS timestamps start at 1980")
    date = ((year - 1980) << 9) | (month << 5) | day
    time = (hour << 11) | (minute << 5) | (second // 2)
    return time, date


class _Entry:
    """One central-directory record plus the local bytes it points at."""

    __slots__ = ("name", "central", "local_bytes")

    def __init__(self, name: str, central: bytes, local_bytes: bytes) -> None:
        self.name = name
        self.central = central
        self.local_bytes = local_bytes


def _read_entries(archive: bytes) -> list[_Entry]:
    """Parses the central directory and slices each member's local record."""

    end = archive.rfind(b"PK\x05\x06")
    if end < 0:
        raise ArchivePatchError("no end-of-central-directory record")
    if archive[end + 20:end + 22] != b"\x00\x00":
        raise ArchivePatchError("archive comments are not supported")
    count, size, offset = struct.unpack_from("<HII", archive, end + 10)
    if offset + size > len(archive):
        raise ArchivePatchError("central directory runs past the archive")

    entries: list[_Entry] = []
    cursor = offset
    for _ in range(count):
        if archive[cursor:cursor + 4] != b"PK\x01\x02":
            raise ArchivePatchError("malformed central-directory record")
        flags, = struct.unpack_from("<H", archive, cursor + 8)
        compressed, = struct.unpack_from("<I", archive, cursor + 20)
        name_len, extra_len, comment_len = struct.unpack_from(
            "<HHH", archive, cursor + 28)
        local_offset, = struct.unpack_from("<I", archive, cursor + 42)
        central_end = cursor + 46 + name_len + extra_len + comment_len
        name = archive[cursor + 46:cursor + 46 + name_len].decode(
            "utf-8" if flags & 0x800 else "cp437")
        if flags & 0x8:
            raise ArchivePatchError(
                f"member '{name}' uses a trailing data descriptor")
        if compressed == 0xFFFFFFFF or local_offset == 0xFFFFFFFF:
            raise ArchivePatchError(f"member '{name}' needs Zip64")

        if archive[local_offset:local_offset + 4] != b"PK\x03\x04":
            raise ArchivePatchError(f"member '{name}' has no local header")
        local_name_len, local_extra_len = struct.unpack_from(
            "<HH", archive, local_offset + 26)
        data_start = local_offset + 30 + local_name_len + local_extra_len
        entries.append(_Entry(
            name,
            archive[cursor:central_end],
            archive[local_offset:data_start + compressed]))
        cursor = central_end
    return entries


def _member_payload(entry: _Entry) -> bytes:
    """Decompresses one member, for verification and for text patching."""

    method, = struct.unpack_from("<H", entry.local_bytes, 8)
    name_len, extra_len = struct.unpack_from("<HH", entry.local_bytes, 26)
    data = entry.local_bytes[30 + name_len + extra_len:]
    if method == 0:
        return data
    if method == 8:
        return zlib.decompress(data, -15)
    raise ArchivePatchError(
        f"member '{entry.name}' uses compression method {method}")


#: The window-glass tint member both glass managed materials sample, and the
#: full-range alpha mask it was authored down from.  ``AlexisSaber.truck``
#: declares
#:
#:   SaberWinds     mesh_transparent AlexisSaberWinds2.png AlexisSaberWindss.png
#:   SaberWinds_int mesh_transparent AlexisSaberWinds2.png AlexisSaberWindss.png
#:
#: so every pane of glass on the car - and the windshield renders as TWO
#: stacked panes, the exterior ``SaberWinds`` skin plus the interior
#: ``SaberWinds_int`` shell - reads its colour and alpha from this one member.
#: ``SaberLens`` samples ``AlexisSaberLens.png`` instead and is not touched.
GLASS_MEMBER = "AlexisSaberWinds2.png"
GLASS_MASK_MEMBER = "AlexisSaberWinds.png"

#: Per-pane alpha authored over the glass core, replacing the shipped 241.
#:
#: The transparent managed-material path alpha-blends each pane over what is
#: behind it: ``out = glass_rgb*a + behind*(1-a)``.  The glass RGB is black
#: everywhere in this member, so a single pane scales the scene behind it by
#: ``1 - a/255``, and the two stacked windshield panes compose to a net
#: transmission of
#:
#:   T = (1 - a/255)^2.
#:
#: The shipped member carried a=241 over the glass:
#: T = (14/255)^2 = 0.30 per cent - a windshield that reads as near-black.
#: For reference, a road-legal windshield transmits >= 70 per cent and even
#: dark privacy tint passes 15-25.  Inverting the composition for the 60-75
#: per cent target band gives a = 255*(1 - sqrt(T)), i.e. a in [34, 57];
#: 45 sits mid-band:
#:
#:   two panes  (windshield):  (210/255)^2 = 67.8 per cent transmission,
#:   one pane   (any single):   210/255    = 82.4 per cent transmission.
#:
#: Because the RGB is black, the tint contribution shrinks with the same
#: alpha, so at a=45 the glass keeps a subtle dark cast instead of a wall of
#: black - correct behaviour for tinted glass.  The specular sheen comes from
#: the separate ``AlexisSaberWindss.png`` unit and is untouched.
GLASS_PANE_ALPHA = 45


def _decode_png_rgba(name: str, payload: bytes) -> tuple[int, int, bytearray]:
    """Decodes a non-interlaced 8-bit RGBA PNG member, strictly."""

    if payload[:8] != b"\x89PNG\r\n\x1a\n":
        raise ArchivePatchError(f"member '{name}' is not a PNG")
    width = height = None
    idat = bytearray()
    cursor = 8
    while cursor + 8 <= len(payload):
        length, tag = struct.unpack_from(">I4s", payload, cursor)
        chunk = payload[cursor + 8:cursor + 8 + length]
        if len(chunk) != length:
            raise ArchivePatchError(f"member '{name}' has a truncated chunk")
        if tag == b"IHDR":
            width, height, depth, colour, _, _, interlace = struct.unpack(
                ">IIBBBBB", chunk)
            if (depth, colour, interlace) != (8, 6, 0):
                raise ArchivePatchError(
                    f"member '{name}' is not non-interlaced 8-bit RGBA "
                    f"(depth={depth} colour={colour} interlace={interlace})")
        elif tag == b"IDAT":
            idat.extend(chunk)
        elif tag == b"IEND":
            break
        cursor += 12 + length
    if width is None or height is None or not idat:
        raise ArchivePatchError(f"member '{name}' has no image data")

    raw = zlib.decompress(bytes(idat))
    stride = width * 4
    if len(raw) != (stride + 1) * height:
        raise ArchivePatchError(f"member '{name}' has a malformed scanline "
                                f"payload ({len(raw)} bytes)")
    rgba = bytearray(stride * height)
    previous = bytearray(stride)
    for y in range(height):
        filter_type = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        if filter_type == 1:
            for i in range(4, stride):
                line[i] = (line[i] + line[i - 4]) & 0xFF
        elif filter_type == 2:
            for i in range(stride):
                line[i] = (line[i] + previous[i]) & 0xFF
        elif filter_type == 3:
            for i in range(stride):
                left = line[i - 4] if i >= 4 else 0
                line[i] = (line[i] + (left + previous[i]) // 2) & 0xFF
        elif filter_type == 4:
            for i in range(stride):
                a = line[i - 4] if i >= 4 else 0
                b = previous[i]
                c = previous[i - 4] if i >= 4 else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                predictor = a if pa <= pb and pa <= pc else (
                    b if pb <= pc else c)
                line[i] = (line[i] + predictor) & 0xFF
        elif filter_type != 0:
            raise ArchivePatchError(
                f"member '{name}' uses PNG filter {filter_type}")
        rgba[y * stride:(y + 1) * stride] = line
        previous = line
    return width, height, rgba


def build_glass_member(mask_payload: bytes) -> bytes:
    """Re-authors the glass tint from the full-range mask.  Deterministic.

    The archive still ships the original the author compressed the tint down
    from: ``AlexisSaberWinds.png`` holds alpha 0 over the glass, 255 over the
    frame and unused UV space, and a nine-step antialiased ramp between, while
    the shipped ``AlexisSaberWinds2.png`` is exactly
    ``255 - (255-mask)*14/255`` of it.  This re-runs that same compression
    with span ``255 - GLASS_PANE_ALPHA`` in place of 14: the glass core lands
    on ``GLASS_PANE_ALPHA``, fully opaque texels stay 255, and the edge ramp
    keeps its proportions.  The input member is never itself rewritten, so
    re-running the tool reproduces the identical output.
    """

    width, height, rgba = _decode_png_rgba(GLASS_MASK_MEMBER, mask_payload)
    span = 255 - GLASS_PANE_ALPHA
    for i in range(0, len(rgba), 4):
        if rgba[i] or rgba[i + 1] or rgba[i + 2]:
            raise ArchivePatchError(
                f"'{GLASS_MASK_MEMBER}' is not black at texel {i // 4}; the "
                f"transmission derivation assumes black glass")
        mask = rgba[i + 3]
        rgba[i + 3] = 255 - ((255 - mask) * span + 127) // 255
    return alexis_saber_paint.encode_png_rgba(width, height, bytes(rgba))


def _authored_entry(name: str, payload: bytes) -> _Entry:
    """Builds a fresh deflate member with reproducible metadata."""

    raw = name.encode("utf-8")
    if raw.decode("utf-8", "strict") != name:
        raise ArchivePatchError(f"member name '{name}' is not round-trippable")
    compressor = zlib.compressobj(9, zlib.DEFLATED, -15)
    body = compressor.compress(payload) + compressor.flush()
    if len(body) >= len(payload):
        method, body = 0, payload
    else:
        method = 8
    crc = zlib.crc32(payload) & 0xFFFFFFFF
    time, date = _dos_timestamp(AUTHORED_MEMBER_TIMESTAMP)
    local = (struct.pack("<4s5H3I2H", b"PK\x03\x04", 20, 0, method, time,
                         date, crc, len(body), len(payload), len(raw), 0) +
             raw + body)
    central = (struct.pack("<4s6H3I5H2I", b"PK\x01\x02",
                           _AUTHORED_VERSION_MADE_BY, 20, 0, method, time,
                           date, crc, len(body), len(payload), len(raw), 0, 0,
                           0, 0, _AUTHORED_EXTERNAL_ATTR, 0) + raw)
    return _Entry(name, central, local)


def _rebuild(entries: list[_Entry]) -> bytes:
    """Serialises entries, patching each central record's local offset."""

    out = bytearray()
    offsets: list[int] = []
    for entry in entries:
        offsets.append(len(out))
        out.extend(entry.local_bytes)
    directory_offset = len(out)
    for entry, offset in zip(entries, offsets):
        central = bytearray(entry.central)
        struct.pack_into("<I", central, 42, offset)
        out.extend(central)
    directory_size = len(out) - directory_offset
    out.extend(struct.pack("<4s4H2IH", b"PK\x05\x06", 0, 0, len(entries),
                           len(entries), directory_size, directory_offset, 0))
    return bytes(out)


def patch_truck(payload: bytes) -> bytes:
    """Restores the SaberBody managedmaterial declaration. Idempotent."""

    lines = payload.split(_EOL)
    restored = 0
    already = 0
    for index, line in enumerate(lines):
        stripped = line.strip()
        if stripped == TRUCK_COMMENTED_BODY:
            lines[index] = TRUCK_RESTORED_BODY
            restored += 1
        elif stripped == TRUCK_RESTORED_BODY:
            already += 1
    if restored + already != 1:
        raise ArchivePatchError(
            f"expected exactly one SaberBody managedmaterial line, found "
            f"{restored} commented and {already} restored")
    return _EOL.join(lines)


def _replace_texture_arguments(line: bytes) -> tuple[bytes, bytes] | None:
    """Splits a ``replaceTexture = source, target`` line, or returns None."""

    stripped = line.strip()
    if not stripped.lower().startswith(b"replacetexture"):
        return None
    _, separator, arguments = stripped.partition(b"=")
    if not separator:
        raise ArchivePatchError(f"unparsable replaceTexture line: {line!r}")
    parts = [part.strip() for part in arguments.split(b",")]
    if len(parts) != 2:
        raise ArchivePatchError(f"unparsable replaceTexture line: {line!r}")
    return parts[0], parts[1]


def patch_skin(payload: bytes) -> bytes:
    """Adds the paired specular replacement to every colour skin. Idempotent.

    ActorSpawner walks every texture unit of the substituted material and
    applies `replace_textures` by texture name, so the specular unit needs its
    own entry; without one a colour skin would keep the default paint's
    specular map.  Any specular replacement already present is dropped and
    reauthored, so re-running never accumulates duplicates.
    """

    by_base = {skin.base_color_member: skin
               for skin in alexis_saber_paint.PAINT_SKINS}
    default_specular = alexis_saber_paint.PAINT_SKINS[0].specular_member
    out: list[bytes] = []
    added = 0
    for line in payload.split(_EOL):
        arguments = _replace_texture_arguments(line)
        if arguments is not None and arguments[0].decode(
                "ascii") == default_specular:
            continue
        out.append(line)
        if arguments is None or arguments[0] != b"bodytemp.png":
            continue
        target = arguments[1].decode("ascii")
        skin = by_base.get(target)
        if skin is None:
            raise ArchivePatchError(
                f"skin replaces '{target}', which the paint generator does "
                f"not author")
        indent = line[:len(line) - len(line.lstrip())]
        out.append(indent + b"replaceTexture   = " +
                   default_specular.encode("ascii") + b", " +
                   skin.specular_member.encode("ascii"))
        added += 1
    expected = len(alexis_saber_paint.PAINT_SKINS) - 1
    if added != expected:
        raise ArchivePatchError(
            f"paired {added} skins, expected {expected}")
    return _EOL.join(out)


def patch_material_script(payload: bytes) -> bytes:
    """Removes the dead Cg ``SaberBody`` material block. Idempotent.

    Only that one block: the ``Auriga*`` base materials it derived from are
    left alone, because nothing resolves to them by name and removing unused
    definitions is not this tool's business.
    """

    lines = payload.split(_EOL)
    header = None
    for index, line in enumerate(lines):
        if line.strip() == MATERIAL_DEAD_BODY_HEADER:
            if header is not None:
                raise ArchivePatchError(
                    "material script declares 'SaberBody' more than once")
            header = index
    if header is None:
        # Already patched. Nothing may still own the name, or the placeholder
        # the spawner installs would be skipped again.
        for line in lines:
            if line.strip().startswith(b"material SaberBody"):
                raise ArchivePatchError(
                    f"material script still declares SaberBody: {line!r}")
        return payload

    cursor = header + 1
    while cursor < len(lines) and not lines[cursor].strip():
        cursor += 1
    if cursor >= len(lines) or lines[cursor].strip() != b"{":
        raise ArchivePatchError(
            "the SaberBody material block does not open with a brace")
    depth = 0
    while cursor < len(lines):
        stripped = lines[cursor].strip()
        depth += stripped.count(b"{") - stripped.count(b"}")
        if depth == 0:
            break
        cursor += 1
    if depth != 0:
        raise ArchivePatchError("the SaberBody material block never closes")

    start = header
    while start > 0 and not lines[start - 1].strip():
        start -= 1
    return _EOL.join(lines[:start] + lines[cursor + 1:])


def patch_archive(archive: bytes) -> tuple[bytes, dict[str, str]]:
    """Returns the rewritten archive and a name -> action report."""

    entries = _read_entries(archive)
    authored = alexis_saber_paint.build_paint_members()
    if GLASS_MEMBER in authored or GLASS_MASK_MEMBER in authored:
        raise ArchivePatchError(
            "the paint generator must not author the glass members")
    masks = [entry for entry in entries if entry.name == GLASS_MASK_MEMBER]
    if len(masks) != 1:
        raise ArchivePatchError(
            f"expected exactly one '{GLASS_MASK_MEMBER}', found {len(masks)}")
    authored[GLASS_MEMBER] = build_glass_member(_member_payload(masks[0]))
    text_patches = {
        TRUCK_MEMBER: patch_truck,
        SKIN_MEMBER: patch_skin,
        MATERIAL_MEMBER: patch_material_script,
    }
    report: dict[str, str] = {}
    seen = set()

    rebuilt: list[_Entry] = []
    for entry in entries:
        if entry.name in seen:
            raise ArchivePatchError(f"duplicate member '{entry.name}'")
        seen.add(entry.name)
        if entry.name in authored:
            payload = authored[entry.name]
            report[entry.name] = (
                "unchanged" if _member_payload(entry) == payload
                else "replaced")
            rebuilt.append(_authored_entry(entry.name, payload))
        elif entry.name in text_patches:
            original = _member_payload(entry)
            payload = text_patches[entry.name](original)
            if payload == original:
                report[entry.name] = "unchanged"
                rebuilt.append(entry)
            else:
                report[entry.name] = "patched"
                rebuilt.append(_authored_entry(entry.name, payload))
        else:
            report[entry.name] = "verbatim"
            rebuilt.append(entry)

    for name, payload in authored.items():
        if name not in seen:
            report[name] = "added"
            rebuilt.append(_authored_entry(name, payload))

    missing = [name for name in text_patches if name not in seen]
    if missing:
        raise ArchivePatchError(f"archive is missing {missing}")
    return _rebuild(rebuilt), report


def compare_archives(before: bytes, after: bytes) -> dict[str, str]:
    """Member-by-member content comparison, for post-write verification."""

    old = {entry.name: _member_payload(entry)
           for entry in _read_entries(before)}
    new = {entry.name: _member_payload(entry)
           for entry in _read_entries(after)}
    result: dict[str, str] = {}
    for name in sorted(set(old) | set(new)):
        if name not in old:
            result[name] = "added"
        elif name not in new:
            result[name] = "removed"
        elif old[name] != new[name]:
            result[name] = "changed"
        else:
            result[name] = "identical"
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Apply the authored Alexis Saber paint to the archive.")
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
        "--dry-run", action="store_true",
        help="report what would change without writing anything")
    args = parser.parse_args(argv)

    original = args.archive.read_bytes()
    patched, report = patch_archive(original)
    verification = compare_archives(original, patched)

    for name in sorted(report):
        action = report[name]
        if action != "verbatim":
            print(f"{action:>9}  {name}")
    changed = sorted(name for name, state in verification.items()
                     if state != "identical")
    print(f"members: {len(verification)} "
          f"({sum(1 for s in verification.values() if s == 'added')} added, "
          f"{sum(1 for s in verification.values() if s == 'changed')} changed)")
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
