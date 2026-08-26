#!/usr/bin/env python3
"""Compile the CityWorld Next overlay's colour textures to block-compressed DDS.

WHY THIS EXISTS

The renderer had no block-compressed storage, so every shipped texture was
decoded to RGBA8 before upload and then given a full mip chain. The overlay's
eleven textures are 25.3 MiB of PNG on disk and roughly 145 MiB resident once
that inflation is applied. The art itself is already at a good resolution --
the road base colour is 2048x4096 -- so the limiting factor was not the source
art but the fact that the engine could only hold it uncompressed.

This tool closes that gap. It does NOT resample, sharpen, or otherwise invent
detail: every compiled texture keeps its authored dimensions exactly. What
changes is storage. That is a deliberate choice; see "WHAT IS NOT DONE" below.

WHAT IS COMPILED

Colour textures become BC3 (DXT5) with a complete authored mip chain, at one
byte per texel instead of four, so resident cost falls by 4x. BC3 rather than
BC7 for a portability reason recorded in cityworld_block_compression.py: the
hidden OGRE14 producer runs on GL3Plus, macOS core profile caps at OpenGL 4.1,
and BC7 needs 4.2. BC3 costs exactly the same byte per texel as BC7 would.

Mip chains are authored here rather than generated at load time, because a
compressed mip cannot be derived from a compressed mip without decoding and
re-encoding, which would compound quantisation error at every level. They are
filtered in linear light, matching the runtime's own sRGB mip rule.

WHAT IS DELIBERATELY NOT COMPILED

cityworld_next_terrain_blend.png is a layer weight mask, not artwork. Block
compression fits two endpoints per 4x4 block, so a compressed weight mask
bleeds layer selection across block boundaries at precisely the layer edges
the mask exists to define. It stays uncompressed, and the engine's
DETAIL_WEIGHT slot refuses block formats for the same reason.

WHAT IS NOT DONE HERE

No upscaling. The sources are already at their authored resolution and no
pixel-space upscaler can add detail that is not present; running one would
produce mush and call it an upgrade. Where a family is genuinely procedural
(brick, pavement, concrete, asphalt, plaster, grime), higher-frequency detail
should come from a fitted parametric generator whose parameters are matched to
the legacy source, which is a separate pipeline that feeds raw map sets into
this one.

No normal or roughness maps yet. This tool is the packaging and compression
half; it compiles whatever raw maps it is given. Roughness in particular must
stay consistent with the per-material F3 family bands already assigned in
source/main/resources/LegacyMaterialScriptSanitizer.cpp, so a per-texel map
must be authored against those bands rather than against nothing.

DETERMINISM

Every step is a pure function of the input bytes. The archive is rewritten with
sorted member order, ZIP_STORED, a fixed 1980-01-01 timestamp and fixed 0644
permissions, matching tools/build_cityworld_local_overlay.py. Members this tool
does not own are copied byte-for-byte. Re-running on its own output finds
nothing to do and reports zero changes.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
import sys
import zipfile
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import cityworld_block_compression as bcpack  # noqa: E402


class TextureCompileError(RuntimeError):
    """Raised when a texture cannot be compiled exactly as specified."""


# Deterministic ZIP record fields, identical to the overlay builder's.
ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)
ZIP_MODE = 0o100644

# Roles are listed explicitly rather than inferred from filenames. An explicit
# table is auditable: adding a texture to the compile set is a reviewable diff,
# and a texture nobody classified is refused rather than silently guessed at.
ROLE_COLOUR_SRGB = "colour_srgb"
ROLE_WEIGHT_MASK = "weight_mask"
# Present in the archive but owned by another workstream: copied verbatim.
ROLE_EXTERNALLY_OWNED = "externally_owned"

COMPILE_PLAN: dict[str, tuple[str, str]] = {
    # member -> (role, why)
    "cityworld_road2_basecolor.png": (
        ROLE_COLOUR_SRGB,
        "road base colour; the single largest texture and the surface that "
        "dominates the frame at street level",
    ),
    "cityworld_parcel_asphalt.png": (
        ROLE_COLOUR_SRGB,
        "parcel asphalt pad; second road-tier surface",
    ),
    "cityworld_next_replacements/asiaconcrete_1024.png": (
        ROLE_COLOUR_SRGB,
        "building facade",
    ),
    "cityworld_next_replacements/betterbrickdiffuse_1024.png": (
        ROLE_COLOUR_SRGB,
        "building facade",
    ),
    "cityworld_next_replacements/brickwall_darkred_1024.png": (
        ROLE_COLOUR_SRGB,
        "building facade",
    ),
    "cityworld_next_replacements/concretelightgrey_1024.png": (
        ROLE_COLOUR_SRGB,
        "building facade",
    ),
    "cityworld_next_replacements/concretetan_1024.png": (
        ROLE_COLOUR_SRGB,
        "building facade",
    ),
    "cityworld_next_replacements/darkcrete_1024.png": (
        ROLE_COLOUR_SRGB,
        "building facade",
    ),
    "cityworld_next_replacements/lightgreybrick_1024.png": (
        ROLE_COLOUR_SRGB,
        "building facade",
    ),
    "cityworld_next_replacements/redcrete_1024.png": (
        ROLE_COLOUR_SRGB,
        "building facade",
    ),
    "cityworld_next_terrain_blend.png": (
        ROLE_WEIGHT_MASK,
        "layer weight mask; block compression would bleed layer selection "
        "across 4x4 boundaries at exactly the layer edges it defines",
    ),
    # Detail-layer members owned by the layered-materials workstream. They are
    # copied verbatim, not compiled. Their layer set was verified live shortly
    # before this tool ran, and compressing it in the same change would put
    # this compression and those freshly-proven layers at risk together.
    #
    # When they are compiled: the four albedo layers take BC3, whose BC4 alpha
    # block reproduces the block's alpha extremes exactly, which matters
    # because their alpha is not transparency -- it carries the layer's
    # height/coverage curve and multiplies into the per-texel layer weight.
    # The two normal maps take BC5. facade_layer_mask_256.png must stay
    # uncompressed for the same reason as the terrain blend map above.
    "cityworld_next_replacements/facade_grain_256.png": (ROLE_EXTERNALLY_OWNED, "detail albedo"),
    "cityworld_next_replacements/facade_joint_512.png": (ROLE_EXTERNALLY_OWNED, "detail albedo"),
    "cityworld_next_replacements/facade_stain_512.png": (ROLE_EXTERNALLY_OWNED, "detail albedo"),
    "cityworld_next_replacements/facade_weathering_512.png": (ROLE_EXTERNALLY_OWNED, "detail albedo"),
    "cityworld_next_replacements/facade_grain_nrm_256.png": (ROLE_EXTERNALLY_OWNED, "detail normal"),
    "cityworld_next_replacements/facade_joint_nrm_512.png": (ROLE_EXTERNALLY_OWNED, "detail normal"),
    "cityworld_next_replacements/facade_layer_mask_256.png": (
        ROLE_EXTERNALLY_OWNED,
        "detail layer weight mask; must never be block-compressed",
    ),
}

# Material scripts inside the archive that reference a compiled member by name.
# The extension changes, so these are rewritten in the same pass; a stale
# reference would fail closed at load rather than fall back.
MATERIAL_MEMBERS = (
    "cityworld_next_local_overlay.material",
    "cityworld_next_parcel_surfaces.material",
)


def decode_png_rgba(data: bytes) -> tuple[int, int, bytes]:
    """Decode an 8-bit RGBA, non-interlaced, filter-0 PNG.

    Deliberately narrow, matching tools/cityworld_terrain_layers.py: anything
    outside that form is a build error rather than a guess. Every texture in
    the overlay is written by this repository's own tools and is in that form;
    if that ever changes, this refuses by name instead of mis-decoding.
    """

    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise TextureCompileError("not a PNG")
    offset = 8
    header = None
    compressed = bytearray()
    while offset + 8 <= len(data):
        length = struct.unpack(">I", data[offset : offset + 4])[0]
        tag = data[offset + 4 : offset + 8]
        payload = data[offset + 8 : offset + 8 + length]
        if tag == b"IHDR":
            header = struct.unpack(">IIBBBBB", payload)
        elif tag == b"IDAT":
            compressed += payload
        elif tag == b"IEND":
            break
        offset += 12 + length
    if header is None:
        raise TextureCompileError("PNG has no IHDR")
    width, height, depth, colour_type, compression, filtering, interlace = header
    if depth != 8 or colour_type != 6 or compression != 0 or filtering != 0 or interlace != 0:
        raise TextureCompileError(
            f"PNG is {width}x{height} depth={depth} colour_type={colour_type} "
            f"interlace={interlace}; only 8-bit RGBA non-interlaced is accepted"
        )
    raw = zlib.decompress(bytes(compressed))
    stride = width * 4
    expected = (stride + 1) * height
    if len(raw) != expected:
        raise TextureCompileError(
            f"PNG scanline payload is {len(raw)} bytes, expected {expected}"
        )
    pixels = bytearray(stride * height)
    for y in range(height):
        base = y * (stride + 1)
        if raw[base] != 0:
            raise TextureCompileError(
                f"PNG row {y} uses filter {raw[base]}; only filter 0 is accepted"
            )
        pixels[y * stride : (y + 1) * stride] = raw[base + 1 : base + 1 + stride]
    return width, height, bytes(pixels)


def resident_bytes_rgba8(width: int, height: int) -> int:
    """GPU bytes for an RGBA8 texture with a complete mip chain."""

    total = 0
    w, h = width, height
    while True:
        total += w * h * 4
        if w == 1 and h == 1:
            break
        w = max(1, w // 2)
        h = max(1, h // 2)
    return total


def resident_bytes_block(width: int, height: int, block_bytes: int) -> int:
    total = 0
    w, h = width, height
    while True:
        total += ((w + 3) // 4) * ((h + 3) // 4) * block_bytes
        if w == 1 and h == 1:
            break
        w = max(1, w // 2)
        h = max(1, h // 2)
    return total


# Formats that may be written into a terrain archive.
#
# BC7 is deliberately absent and this is the guard, not a comment. Archive
# textures load through the OGRE-14 producer, whose isLoaded() gate runs before
# the presenter's decoder ever sees the bytes, and macOS core profile caps at
# OpenGL 4.1 while BPTC needs 4.2. Measured directly from a producer session:
# GL_VERSION = 4.1.0.0, GL_EXT_texture_compression_s3tc present, no BPTC
# extension at all. A BC7 archive member would compile cleanly, pass material
# validation, and then project zero layers at runtime.
#
# The material validator still admits BC7, correctly: it is a platform-neutral
# contract about what a format can express, and BC7 loads fine on D3D12 and
# Vulkan. The platform truth belongs here instead, in the one place that
# chooses what to write.
ARCHIVE_ADMITTED_FOURCC = {b"DXT5": 16}


def compile_colour_texture(width: int, height: int, pixels: bytes) -> bytes:
    four_cc = b"DXT5"
    if four_cc not in ARCHIVE_ADMITTED_FOURCC:
        raise TextureCompileError(
            f"{four_cc!r} may not be written into a terrain archive: it must "
            "load in the OGRE-14 producer, which runs GL 4.1 on macOS"
        )
    block_bytes = ARCHIVE_ADMITTED_FOURCC[four_cc]
    chain = bcpack.build_mip_chain(width, height, pixels, srgb=True)
    payloads = [bcpack.encode_bc3(w, h, data) for w, h, data in chain]
    return bcpack.write_dds_fourcc(width, height, four_cc, block_bytes, payloads)


class Entry:
    """One archive member, kept as raw bytes so untouched members round-trip."""

    __slots__ = ("name", "data")

    def __init__(self, name: str, data: bytes) -> None:
        self.name = name
        self.data = data


def write_deterministic_zip(entries: list[Entry]) -> bytes:
    import io

    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w", zipfile.ZIP_STORED, allowZip64=True) as archive:
        for entry in sorted(entries, key=lambda e: e.name):
            info = zipfile.ZipInfo(entry.name, date_time=ZIP_TIMESTAMP)
            info.compress_type = zipfile.ZIP_STORED
            info.external_attr = ZIP_MODE << 16
            info.create_system = 3
            info.create_version = 20
            info.extract_version = 20
            info.extra = b""
            info.comment = b""
            archive.writestr(info, entry.data)
    return buffer.getvalue()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=None)
    parser.add_argument("--backup", type=Path, default=None)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args(argv)

    source = args.archive
    if not source.is_file():
        raise TextureCompileError(f"archive not found: {source}")
    original = source.read_bytes()
    print(f"source  {source} ({len(original)} bytes, sha256 {hashlib.sha256(original).hexdigest()})")

    entries: list[Entry] = []
    with zipfile.ZipFile(source) as archive:
        names = archive.namelist()
        for name in names:
            entries.append(Entry(name, archive.read(name)))
    by_name = {entry.name: entry for entry in entries}

    # Refuse an unclassified texture rather than guessing at its role.
    unclassified = [
        e.name
        for e in entries
        if e.name.lower().endswith((".png", ".dds", ".tga", ".jpg"))
        and e.name not in COMPILE_PLAN
        and not e.name.lower().endswith(".dds")
    ]
    if unclassified:
        raise TextureCompileError(
            "these texture members have no entry in COMPILE_PLAN and would be "
            "shipped unreviewed: " + ", ".join(sorted(unclassified))
        )

    renames: dict[str, str] = {}
    changed: list[str] = []
    before_resident = 0
    after_resident = 0
    before_disk = 0
    after_disk = 0
    skipped: list[tuple[str, str]] = []

    targets = [
        (name, plan) for name, plan in sorted(COMPILE_PLAN.items()) if name in by_name
    ]
    if not targets:
        print("nothing to compile: no planned member is present (already compiled?)")

    for name, (role, why) in targets:
        entry = by_name[name]
        width, height, pixels = decode_png_rgba(entry.data)
        rgba8_resident = resident_bytes_rgba8(width, height)
        if role in (ROLE_WEIGHT_MASK, ROLE_EXTERNALLY_OWNED):
            skipped.append((name, f"[{role}] {why}"))
            before_resident += rgba8_resident
            after_resident += rgba8_resident
            before_disk += len(entry.data)
            after_disk += len(entry.data)
            continue

        compiled_name = name[: -len(".png")] + ".dds"
        print(f"  compiling {name} ({width}x{height}) -> {compiled_name} ...", flush=True)
        dds = compile_colour_texture(width, height, pixels)
        block_resident = resident_bytes_block(width, height, 16)
        before_resident += rgba8_resident
        after_resident += block_resident
        before_disk += len(entry.data)
        after_disk += len(dds)
        renames[name] = compiled_name
        entry.name = compiled_name
        entry.data = dds
        changed.append(compiled_name)
        print(
            f"    disk {len(entry.data):>10} <- {len(pixels):>10} raw   "
            f"resident {block_resident:>10} <- {rgba8_resident:>10}"
            f"  ({rgba8_resident / block_resident:.2f}x)"
        )

    # Rewrite in-archive material references so no script points at a member
    # that no longer exists.
    for material_name in MATERIAL_MEMBERS:
        entry = by_name.get(material_name)
        if entry is None:
            continue
        text = entry.data.decode("utf-8")
        rewritten = text
        for old, new in renames.items():
            rewritten = rewritten.replace(old, new)
        if rewritten != text:
            entry.data = rewritten.encode("utf-8")
            changed.append(material_name)

    print()
    for name, why in skipped:
        print(f"  SKIPPED {name}\n          {why}")

    if not changed:
        print("\n0 members changed; archive is already compiled (idempotent re-run)")
        return 0

    payload = write_deterministic_zip(entries)
    digest = hashlib.sha256(payload).hexdigest()

    print(f"\n{len(changed)} members changed, {len(entries)} total")
    print(f"disk      {before_disk:>12} -> {after_disk:>12} bytes over compiled members")
    print(
        f"resident  {before_resident:>12} -> {after_resident:>12} bytes "
        f"({before_resident / after_resident:.2f}x less GPU memory)"
    )
    print(f"archive   {len(payload)} bytes, sha256 {digest}")
    print()
    print("PIN THESE IN source/main/resources/LegacyMaterialCompatibilityPlan.h:")
    print(f'  kCityWorldNextLocalOverlayArchiveSha256 = "{digest}"')
    print(f"  kCityWorldNextLocalOverlayArchiveBytes  = {len(payload)}ULL")

    if args.dry_run:
        print("\n--dry-run: nothing written")
        return 0

    destination = args.output or source
    if args.backup is not None:
        if args.backup.exists():
            # Never modify an existing backup: it is the only record of a state
            # this tool cannot reconstruct.
            print(f"backup already exists, left untouched: {args.backup}")
        else:
            args.backup.parent.mkdir(parents=True, exist_ok=True)
            args.backup.write_bytes(original)
            print(f"backup  {args.backup}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(payload)
    print(f"wrote   {destination} ({len(payload)} bytes, sha256 {digest})")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except TextureCompileError as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(2)
