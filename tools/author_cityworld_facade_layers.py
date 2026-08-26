#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Add five-layer depth materials to the CityWorld Next facade brick.

Idempotent archive rewrite, same contract as tools/apply_alexis_saber_paint.py:
a dated backup is written beside the archive before the first change, every
member this tool does not own is copied byte-identically, and a rerun over an
already-authored archive is a no-op.

The showcase is one base material plus the pinned four detail layers:

  base    brickwall_darkred_1024.png (already shipped)
  layer 0 mortar joint depth      multiply, repeat 1, + detail normal
  layer 1 broad weathering        overlay,  repeat 1
  layer 2 stain settling in joints multiply, repeat 1, high contrast
  layer 3 fine surface grain      overlay,  repeat 8, + detail normal

Layers 0 and 2 are generated from the SAME 8x32 running-bond grid with the
same 4px joint as the base brick, and are bound at repeat 1, so their recesses
land exactly on the base's mortar lines. Because the engine multiplies a detail
albedo's alpha into that layer's per-texel weight, the stain is admitted only
where the joint is: it sits IN the mortar line instead of washing across the
brick face. That is the whole point of the height-in-alpha contract and it is
what the grazing-incidence screenshot is meant to show.

No byte of any CityWorld.zip member is decoded, upscaled, or copied here.
"""

from __future__ import annotations

import argparse
import datetime
import hashlib
import importlib.util
from pathlib import Path
import shutil
import sys
import zipfile

_SPEC = importlib.util.spec_from_file_location(
    "material_detail_layers",
    Path(__file__).resolve().parent / "material_detail_layers.py",
)
mdl = importlib.util.module_from_spec(_SPEC)
sys.modules.setdefault(_SPEC.name, mdl)
_SPEC.loader.exec_module(mdl)

NAMESPACE = "cityworld_next_replacements/"
MATERIAL_MEMBER = "cityworld_next_local_overlay.material"
MARKER = "// ror-material-detail-layers-v1"

#: The base brick's recorded grid, from tools/cityworld_replacement_textures.py.
BRICK_COLUMNS = 8
BRICK_ROWS = 32
BRICK_MORTAR_PX = 4

TEXTURE_SIZE = 512
GRAIN_SIZE = 256

#: Facade materials that receive the layer set. Both name the same brick art.
TARGET_MATERIALS = (
    "modularbuildings/TEXFACE/brickwall_darkred.dds",
    "modularbuildings/SOLID/TEX/brickwall_darkred.dds",
)

MASK_MEMBER = NAMESPACE + "facade_layer_mask_256.png"
JOINT_MEMBER = NAMESPACE + "facade_joint_512.png"
JOINT_NRM_MEMBER = NAMESPACE + "facade_joint_nrm_512.png"
WEATHER_MEMBER = NAMESPACE + "facade_weathering_512.png"
STAIN_MEMBER = NAMESPACE + "facade_stain_512.png"
GRAIN_MEMBER = NAMESPACE + "facade_grain_256.png"
GRAIN_NRM_MEMBER = NAMESPACE + "facade_grain_nrm_256.png"


def build_payloads() -> dict[str, bytes]:
    """Every generated member, keyed by archive member name."""

    payloads: dict[str, bytes] = {}
    # Broad placement only. R/G/A select layers 0/1/3 everywhere; B ramps the
    # stain heavier towards street level, which is where run-off collects.
    payloads[MASK_MEMBER] = mdl.build_weight_mask_png(
        256,
        [
            lambda u, v: 1.0,
            lambda u, v: 1.0,
            lambda u, v: 0.35 + 0.65 * v,
            lambda u, v: 1.0,
        ],
    )
    payloads[JOINT_MEMBER] = mdl.build_brick_joint_layer_png(
        TEXTURE_SIZE, columns=BRICK_COLUMNS, rows=BRICK_ROWS,
        mortar_px=BRICK_MORTAR_PX, tint=(96, 88, 82),
        pivot=0.42, contrast=3.2, salt=2311, streak=0.0)
    payloads[JOINT_NRM_MEMBER] = mdl.build_brick_joint_normal_png(
        TEXTURE_SIZE, columns=BRICK_COLUMNS, rows=BRICK_ROWS,
        mortar_px=BRICK_MORTAR_PX, amplitude=5.5)
    payloads[WEATHER_MEMBER] = mdl.build_grime_albedo_png(
        TEXTURE_SIZE, salt=8123, tint=(122, 116, 106),
        pivot=0.55, contrast=1.35)
    # High contrast: the stain is present only in the deepest part of the
    # joint, then bleeds a short way down the brick beneath it.
    payloads[STAIN_MEMBER] = mdl.build_brick_joint_layer_png(
        TEXTURE_SIZE, columns=BRICK_COLUMNS, rows=BRICK_ROWS,
        mortar_px=BRICK_MORTAR_PX, tint=(48, 43, 38),
        pivot=0.5, contrast=4.5, salt=5507, streak=0.45)
    payloads[GRAIN_MEMBER] = mdl.build_fine_grain_png(
        GRAIN_SIZE, salt=907, tint=(158, 152, 146), pivot=0.5, contrast=1.5)
    payloads[GRAIN_NRM_MEMBER] = mdl.build_relief_normal_png(
        GRAIN_SIZE, salt=4409, amplitude=2.5, cell_divisor=64)
    return payloads


def build_layer_set(base_material: str) -> mdl.DetailLayerSet:
    return mdl.DetailLayerSet(
        base_material=base_material,
        weight_mask_member=MASK_MEMBER,
        layers=[
            mdl.DetailLayerSpec(
                index=0, albedo_member=JOINT_MEMBER,
                normal_member=JOINT_NRM_MEMBER, blend="multiply",
                uv_repeats=1.0, weight=0.75, normal_weight=1.0,
                height_pivot=0.42, height_contrast=3.2),
            mdl.DetailLayerSpec(
                index=1, albedo_member=WEATHER_MEMBER, normal_member=None,
                blend="overlay", uv_repeats=1.0, weight=0.5,
                height_pivot=0.55, height_contrast=1.35),
            mdl.DetailLayerSpec(
                index=2, albedo_member=STAIN_MEMBER, normal_member=None,
                blend="multiply", uv_repeats=1.0, weight=0.8,
                height_pivot=0.5, height_contrast=4.5),
            mdl.DetailLayerSpec(
                index=3, albedo_member=GRAIN_MEMBER,
                normal_member=GRAIN_NRM_MEMBER, blend="overlay",
                uv_repeats=8.0, weight=0.3, normal_weight=0.6,
                height_pivot=0.5, height_contrast=1.5),
        ],
    )


def build_material_appendix() -> str:
    blocks = [MARKER,
              "// Generated by tools/author_cityworld_facade_layers.py.",
              "// Companion materials only; nothing draws these passes."]
    for base in TARGET_MATERIALS:
        blocks.append("")
        blocks.append(mdl.emit_companion_material(build_layer_set(base)).rstrip("\n"))
    return "\n".join(blocks) + "\n"


def author(archive: Path, *, backup_dir: Path | None, dry_run: bool) -> bool:
    if not archive.is_file():
        raise SystemExit(f"archive not found: {archive}")
    with zipfile.ZipFile(archive) as source:
        names = source.namelist()
        if MATERIAL_MEMBER not in names:
            raise SystemExit(f"{archive} has no {MATERIAL_MEMBER}")
        existing = {name: source.read(name) for name in names}
        infos = {info.filename: info for info in source.infolist()}

    payloads = build_payloads()
    appendix = build_material_appendix()
    material = existing[MATERIAL_MEMBER].decode("utf-8")
    if MARKER in material:
        head, _, _ = material.partition(MARKER)
        material_out = head.rstrip("\n") + "\n\n" + appendix
    else:
        material_out = material.rstrip("\n") + "\n\n" + appendix

    desired = dict(existing)
    desired[MATERIAL_MEMBER] = material_out.encode("utf-8")
    desired.update(payloads)
    if desired == existing:
        print("archive already authored; nothing to do")
        return False
    if dry_run:
        changed = [n for n in sorted(desired) if existing.get(n) != desired[n]]
        print("would change:", ", ".join(changed))
        return True

    if backup_dir is not None:
        backup_dir.mkdir(parents=True, exist_ok=True)
        stamp = datetime.date.today().isoformat()
        backup = backup_dir / f"{archive.stem}.pre-facade-layers-{stamp}.zip"
        if backup.exists():
            print(f"backup already exists, leaving it untouched: {backup}")
        else:
            shutil.copy2(archive, backup)
            print(f"backup written: {backup}")

    temporary = archive.with_suffix(".zip.authoring")
    with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED) as out:
        for name in existing:  # preserve original member order
            info = infos[name]
            new = zipfile.ZipInfo(name, date_time=info.date_time)
            new.compress_type = info.compress_type
            new.external_attr = info.external_attr
            out.writestr(new, desired[name])
        for name in sorted(set(desired) - set(existing)):
            new = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            new.compress_type = zipfile.ZIP_DEFLATED
            out.writestr(new, desired[name])
    temporary.replace(archive)
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archives", nargs="+", type=Path)
    parser.add_argument("--backup-dir", type=Path, default=None)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    for archive in args.archives:
        print(f"== {archive}")
        author(archive, backup_dir=args.backup_dir, dry_run=args.dry_run)
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        print(f"   sha256 {digest}")
        print(f"   bytes  {archive.stat().st_size}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
