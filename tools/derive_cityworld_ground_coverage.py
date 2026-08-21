#!/usr/bin/env python3
"""Derive CityWorld's terrain ground coverage from its own placed geometry.

CityWorld paints its streets, plazas and parking as *objects*: `CityWorld.tobj`
places ~1900 meshes whose submesh materials name the surface they represent
(`calleunsolosentido`, `pavimento`, `adocretos`, `prado`, `piedra`, ...). The
terrain underneath carries no such information, so the whole 12 km page renders
as the base grass layer.

This tool recovers the missing coverage rather than inventing it:

  1. resolve every `.tobj` placement through its `.odef` to a mesh and scale;
  2. convert each mesh with the pinned OgreXMLConverter and read its triangles
     together with the material of the submesh they belong to;
  3. place the triangles exactly as the runtime does -- `TerrainObjectManager`
     applies `Quaternion(rx,X)*Quaternion(ry,Y)*Quaternion(rz,Z)` and then
     `pitch(-90)` unless the odef asks for `standard` mode;
  4. keep the near-horizontal triangles that sit in a band around the terrain
     plane, so facades, roofs and the elevated districts contribute nothing;
  5. classify each material into a terrain layer and rasterize in ascending
     height, so the topmost surface at each point is the one that shows.

The result is a blend map that lines up with the geometry it was derived from,
which a hand-painted mask cannot do.

Requires numpy and the pinned OgreXMLConverter; run offline, then commit the
PNG it writes. `cityworld_terrain_layers.py` consumes that PNG.
"""
from __future__ import annotations

import argparse
import collections
import json
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

try:
    import numpy as np
except ImportError:  # pragma: no cover - offline tool
    sys.exit("derive_cityworld_ground_coverage requires numpy")

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cityworld_terrain_layers as terrain_layers  # noqa: E402


class CoverageError(RuntimeError):
    """Raised when the derivation cannot proceed."""


WORLD_SIZE_M = terrain_layers.WORLD_SIZE_M

#: Layer indices matching the page OTC: 0 grass (base), 1 asphalt, 2 concrete,
#: 3 rock. ``IGNORE`` marks geometry that says nothing about the ground.
GRASS, ASPHALT, CONCRETE, ROCK, IGNORE = 0, 1, 2, 3, -1
LAYER_NAMES = {GRASS: "grass", ASPHALT: "asphalt",
               CONCRETE: "concrete", ROCK: "rock", IGNORE: "ignore"}

#: A surface is ground evidence when it faces roughly upwards and sits near the
#: terrain plane. CityWorld's terrain is flat at y=0; the band admits kerbs and
#: ramps while rejecting roofs and the elevated Neo Q 2.0 slab at y~50.
NORMAL_MIN = 0.5
GROUND_Y_LO = -3.0
GROUND_Y_HI = 6.0

#: Ordered classification of OGRE material names, first match wins. The names
#: are the authors' own, mostly Spanish: `calle` street, `prado` meadow,
#: `adocreto` paving block, `piedra` stone, `arcilla`/`arsilla` clay.
CLASSIFIER_RULES = (
    (IGNORE, r"ventana|window|cristal|glass|vidrio|fachada|facade|brickwall|"
             r"brickwindow|brickentrance|brickdoor|cornice|roof|techo|"
             r"semaforo|trafficlight|luminaria|streetlamp|alumbrado|"
             r"telefono|gente|people|arbol|tree|arbust|hoja|"
             r"agua|water|ibeam|steelbeam|cercado|fence|malla|"
             r"bomba|anuncio|poster|cartel|logo|letrero|signo|sign|"
             r"metal|acero|lamina|reja|puerta|door|escalera|"
             r"busstop|parada|toldo|awning|solar|celda|selda|"
             r"wall_|_wall|muro|columna|column|decowhite|panels"),
    (GRASS, r"prado|cesped|pasto|grass|hierba|lawn|campo|jardin|garden|"
            r"park(?!ing)"),
    (ROCK, r"piedra|rock|roca|stone|arsilla|arcilla|clay|tierra|dirt|"
           r"gravel|grava|sand|arena|montana|cliff"),
    (ASPHALT, r"asfalto|asphalt|calle|carretera|autovia|road|street|"
              r"pavimento|pavement|estacionamiento|parking|parkingspace|"
              r"pista|runway|track|crucepeatonal|crosswalk|"
              r"reductordevelocidad|tope|autodromo|circuito|highway"),
    (CONCRETE, r"concreto|concrete|crete|sidewalk|banqueta|acera|"
               r"adocreto|adoquin|cobble|pavestone|baldosa|"
               r"marmol|marble|granito|granite|loseta|azulejo|tile|"
               r"plaza|patio|piso|floor|prefabricado|cemento|cement|"
               r"cancha|court|helipad|explanada"),
)
_COMPILED = tuple((cls, re.compile(pat, re.I)) for cls, pat in CLASSIFIER_RULES)


def classify_material(material: str) -> int:
    """Map an OGRE material name onto a terrain layer index."""
    if not material:
        return IGNORE
    name = material.strip()
    # `dneroads/TEXFACE/sidewalk01.dds` is a sidewalk, not a road: when a group
    # prefix is present the texture basename alone carries the surface meaning.
    parts = re.split(r"[\\/]", name)
    tail = re.sub(r"\.(dds|png|jpg|jpeg|tga|psd)$", "", parts[-1], flags=re.I)
    subject = tail if len(parts) > 1 else name
    for cls, rx in _COMPILED:
        if rx.search(subject):
            return cls
    return IGNORE


# --------------------------------------------------------------------------
# placement resolution
# --------------------------------------------------------------------------

def index_archive(archive: Path, workdir: Path) -> dict[str, Path]:
    """Extract the odef/mesh members and index them by lowercased filename."""
    out = workdir / "content"
    out.mkdir(parents=True, exist_ok=True)
    index: dict[str, Path] = {}
    with zipfile.ZipFile(archive) as zf:
        for info in zf.infolist():
            if info.is_dir():
                continue
            name = Path(info.filename).name
            if not name.lower().endswith((".odef", ".mesh")):
                continue
            target = out / name
            if not target.exists():
                with zf.open(info) as src, open(target, "wb") as dst:
                    shutil.copyfileobj(src, dst)
            index.setdefault(name.lower(), target)
    return index


def parse_odef(path: Path) -> tuple[str | None, tuple[float, float, float], bool]:
    lines = [l.strip() for l in path.read_text(errors="replace").splitlines()]
    lines = [l for l in lines if l and not l.startswith(("//", ";"))]
    i = 0
    if lines and lines[0].upper() == "LOD":
        i = 1
    if i >= len(lines):
        return None, (1.0, 1.0, 1.0), False
    mesh = lines[i]
    scale = (1.0, 1.0, 1.0)
    if i + 1 < len(lines):
        m = re.match(r"^\s*(-?[\d.eE+-]+)\s*,\s*(-?[\d.eE+-]+)\s*,"
                     r"\s*(-?[\d.eE+-]+)\s*$", lines[i + 1])
        if m:
            scale = tuple(float(x) for x in m.groups())  # type: ignore[assignment]
    if not mesh.lower().endswith(".mesh"):
        return None, scale, False
    standard = any(l.lower() == "standard" for l in lines)
    return mesh, scale, standard


def read_placements(tobj_text: str, index: dict[str, Path]) -> list[dict]:
    placements = []
    for line in tobj_text.splitlines():
        s = line.strip()
        if not s or s.startswith(("//", ";")):
            continue
        parts = [p.strip() for p in s.split(",")]
        if len(parts) < 7:
            continue
        try:
            pos = tuple(float(parts[k]) for k in (0, 1, 2))
            rot = tuple(float(parts[k]) for k in (3, 4, 5))
        except ValueError:
            continue
        name = parts[6].strip()
        # trailing fields separate spawn-zone/vehicle entries by whitespace
        if not name or re.search(r"\s", name):
            continue
        odef = index.get(name.lower() + ".odef")
        if odef is None:
            continue
        mesh, scale, standard = parse_odef(odef)
        if mesh is None:
            continue
        mesh_path = index.get(mesh.lower())
        if mesh_path is None:
            continue
        placements.append(dict(name=name, mesh=mesh_path.name, scale=scale,
                               standard=standard, pos=pos, rot=rot))
    return placements


# --------------------------------------------------------------------------
# mesh geometry
# --------------------------------------------------------------------------

def convert_meshes(meshes: set[str], index: dict[str, Path],
                   converter: Path, xml_dir: Path) -> None:
    xml_dir.mkdir(parents=True, exist_ok=True)
    for i, mesh in enumerate(sorted(meshes)):
        dest = xml_dir / (mesh + ".xml")
        if dest.exists():
            continue
        src = index[mesh.lower()]
        proc = subprocess.run([str(converter), str(src), str(dest)],
                              capture_output=True, text=True)
        if proc.returncode != 0 or not dest.exists():
            raise CoverageError(f"OgreXMLConverter failed on {mesh}: {proc.stderr[-400:]}")
        if (i + 1) % 25 == 0:
            print(f"  converted {i + 1}/{len(meshes)}", flush=True)


def _read_geometry(geom) -> np.ndarray:
    pos = []
    for vb in geom.findall("vertexbuffer"):
        if vb.get("positions") != "true":
            continue
        for v in vb.findall("vertex"):
            p = v.find("position")
            if p is not None:
                pos.append((float(p.get("x")), float(p.get("y")), float(p.get("z"))))
    return np.asarray(pos, dtype=np.float32).reshape(-1, 3)


def parse_mesh_xml(path: Path) -> list[tuple[str, np.ndarray]]:
    root = ET.parse(path).getroot()
    sg = root.find("sharedgeometry")
    shared = _read_geometry(sg) if sg is not None else None
    out: dict[str, list[np.ndarray]] = collections.defaultdict(list)
    subs = root.find("submeshes")
    if subs is None:
        return []
    for sm in subs.findall("submesh"):
        material = (sm.get("material") or "").strip()
        if sm.get("usesharedvertices") == "true":
            verts = shared
        else:
            g = sm.find("geometry")
            verts = _read_geometry(g) if g is not None else None
        if verts is None or len(verts) == 0:
            continue
        faces = sm.find("faces")
        if faces is None:
            continue
        idx = []
        for f in faces.findall("face"):
            idx.append((int(f.get("v1")), int(f.get("v2")), int(f.get("v3"))))
            v4 = f.get("v4")
            if v4 is not None:
                idx.append((int(f.get("v1")), int(f.get("v3")), int(v4)))
        if not idx:
            continue
        arr = np.asarray(idx, dtype=np.int64)
        arr = arr[(arr < len(verts)).all(axis=1)]
        if len(arr) == 0:
            continue
        out[material].append(verts[arr])
    return [(m, np.concatenate(v, axis=0)) for m, v in out.items()]


def rotation_matrix(rx: float, ry: float, rz: float) -> np.ndarray:
    ax, ay, az = math.radians(rx), math.radians(ry), math.radians(rz)
    cx, sx = math.cos(ax), math.sin(ax)
    cy, sy = math.cos(ay), math.sin(ay)
    cz, sz = math.cos(az), math.sin(az)
    return (np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]])
            @ np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
            @ np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]]))


PITCH_MINUS_90 = rotation_matrix(-90.0, 0.0, 0.0)


def collect_ground_triangles(placements, xml_dir: Path):
    """World-space near-ground triangles and their layer classes."""
    cache: dict[str, list[tuple[str, np.ndarray]]] = {}
    tris, classes = [], []
    for p in placements:
        mesh = p["mesh"]
        if mesh not in cache:
            cache[mesh] = parse_mesh_xml(xml_dir / (mesh + ".xml"))
        rot = rotation_matrix(*p["rot"])
        orientation = rot if p["standard"] else rot @ PITCH_MINUS_90
        matrix = orientation * np.asarray(p["scale"], dtype=np.float64)[None, :]
        origin = np.asarray(p["pos"], dtype=np.float64)
        for material, local in cache[mesh]:
            cls = classify_material(material)
            if cls == IGNORE:
                continue
            world = (local.astype(np.float64).reshape(-1, 3) @ matrix.T
                     + origin[None, :]).reshape(-1, 3, 3)
            normals = np.cross(world[:, 1] - world[:, 0], world[:, 2] - world[:, 0])
            lengths = np.linalg.norm(normals, axis=1)
            lengths[lengths == 0] = 1e-12
            upness = np.abs(normals[:, 1] / lengths)
            height = world[:, :, 1].mean(axis=1)
            keep = (upness > NORMAL_MIN) & (height > GROUND_Y_LO) & (height < GROUND_Y_HI)
            if not keep.any():
                continue
            kept = world[keep]
            tris.append(kept.astype(np.float32))
            classes.append(np.full(len(kept), cls, dtype=np.uint8))
    if not tris:
        raise CoverageError("no ground triangles were derived")
    return np.concatenate(tris, 0), np.concatenate(classes, 0)


def procedural_road_triangles(tobj_text: str):
    """Ground-level procedural road spans as quads of the authored width."""
    chains, current = [], None
    for line in tobj_text.splitlines():
        s = line.strip()
        if s.startswith("begin_procedural_roads"):
            current = []
            continue
        if s.startswith("end_procedural_roads"):
            if current:
                chains.append(current)
            current = None
            continue
        if current is None or not s or s.startswith(("//", ";")):
            continue
        parts = [p.strip() for p in s.split(",")]
        if len(parts) < 10:
            continue
        try:
            x, y, z = float(parts[0]), float(parts[1]), float(parts[2])
            width = float(parts[6])
        except ValueError:
            continue
        current.append((x, y, z, width, parts[9].lower()))
    out = []
    for chain in chains:
        for a, b in zip(chain, chain[1:]):
            # elevated spans ride on pillars and never touch the terrain
            if "bridge" in a[4] or "bridge" in b[4]:
                continue
            if max(a[1], b[1]) > GROUND_Y_HI:
                continue
            dx, dz = b[0] - a[0], b[2] - a[2]
            length = math.hypot(dx, dz)
            if length < 1e-6:
                continue
            nx, nz = -dz / length, dx / length
            ha, hb = a[3] / 2.0, b[3] / 2.0
            p0 = (a[0] + nx * ha, 0.0, a[2] + nz * ha)
            p1 = (a[0] - nx * ha, 0.0, a[2] - nz * ha)
            p2 = (b[0] - nx * hb, 0.0, b[2] - nz * hb)
            p3 = (b[0] + nx * hb, 0.0, b[2] + nz * hb)
            out.extend(([p0, p1, p2], [p0, p2, p3]))
    if not out:
        return np.zeros((0, 3, 3), np.float32), np.zeros(0, np.uint8)
    arr = np.asarray(out, dtype=np.float32)
    return arr, np.full(len(arr), ASPHALT, dtype=np.uint8)


# --------------------------------------------------------------------------
# rasterization
# --------------------------------------------------------------------------

def rasterize(tris: np.ndarray, classes: np.ndarray, size: int,
              supersample: int, band_rows: int = 4096) -> np.ndarray:
    """Painter's-order rasterization into per-class coverage fractions."""
    order = np.argsort(tris[:, :, 1].mean(axis=1), kind="stable")
    tris, classes = tris[order], classes[order]

    ss_n = size * supersample
    scale = ss_n / WORLD_SIZE_M
    gx = tris[:, :, 0] * scale
    gz = tris[:, :, 2] * scale
    zmin = np.floor(gz.min(axis=1)).astype(np.int64)
    zmax = np.ceil(gz.max(axis=1)).astype(np.int64)

    coverage = np.zeros((4, size, size), dtype=np.float32)
    unpainted = 255

    for band0 in range(0, ss_n, band_rows):
        band1 = min(band0 + band_rows, ss_n)
        selected = np.nonzero((zmax >= band0) & (zmin < band1))[0]
        buf = np.full((band1 - band0, ss_n), unpainted, dtype=np.uint8)
        for i in selected:
            xs_t, zs_t = gx[i], gz[i]
            c0 = max(int(math.floor(xs_t.min())), 0)
            c1 = min(int(math.ceil(xs_t.max())) + 1, ss_n)
            r0 = max(int(math.floor(zs_t.min())), band0)
            r1 = min(int(math.ceil(zs_t.max())) + 1, band1)
            if c1 <= c0 or r1 <= r0:
                continue
            ax, ay = xs_t[0], zs_t[0]
            bx, by = xs_t[1], zs_t[1]
            cx, cy = xs_t[2], zs_t[2]
            det = (by - ay) * (cx - ax) - (bx - ax) * (cy - ay)
            if abs(det) < 1e-9:
                continue
            px = (np.arange(c0, c1, dtype=np.float32) + 0.5)[None, :] - ax
            pz = (np.arange(r0, r1, dtype=np.float32) + 0.5)[:, None] - ay
            w1 = ((cx - ax) * pz - (cy - ay) * px) / det
            w2 = ((bx - ax) * pz - (by - ay) * px) / -det
            inside = (w1 >= 0) & (w2 >= 0) & (w1 + w2 <= 1)
            if inside.any():
                buf[r0 - band0:r1 - band0, c0:c1][inside] = classes[i]
        rows = (band1 - band0) // supersample
        blocks = buf.reshape(rows, supersample, size, supersample)
        base = band0 // supersample
        for cls in range(4):
            coverage[cls, base:base + rows] = (
                (blocks == cls).sum(axis=(1, 3)) / float(supersample ** 2))
        print(f"  band {band1 // band_rows}/{math.ceil(ss_n / band_rows)}", flush=True)
    return coverage


def dilate(channel: np.ndarray, radius: int = 1) -> np.ndarray:
    out = channel.copy()
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            if dx or dy:
                out = np.maximum(out, np.roll(np.roll(channel, dy, 0), dx, 1))
    return out


def coverage_to_channels(coverage: np.ndarray) -> tuple[bytearray, bytearray, bytearray]:
    """Invert OGRE's sequential layer compositing into blend channel bytes.

    OGRE laps each layer over the ones beneath with its own blend map as alpha,
    so the visible weights are
        rock = B, concrete = (1-B)G, asphalt = (1-B)(1-G)R, grass = the rest,
    which target fractions (g, a, c, r) invert to
        B = r, G = c/(1-r), R = a/(a+g).
    """
    grass_ev, asphalt, concrete, rock = coverage
    # Abutting tiles leave texel-wide seams that would otherwise show base
    # grass; paved classes lap one texel outward. Grass is not dilated, so
    # parkland never creeps over pavement.
    asphalt, concrete, rock = dilate(asphalt), dilate(concrete), dilate(rock)
    total = grass_ev + asphalt + concrete + rock
    scale = np.maximum(total, 1.0)
    grass_ev, asphalt, concrete, rock = (grass_ev / scale, asphalt / scale,
                                         concrete / scale, rock / scale)
    grass = grass_ev + np.clip(1.0 - (grass_ev + asphalt + concrete + rock), 0.0, 1.0)

    blue = rock
    green = np.divide(concrete, 1.0 - rock, out=np.zeros_like(concrete),
                      where=(1.0 - rock) > 1e-6)
    red = np.divide(asphalt, asphalt + grass, out=np.zeros_like(asphalt),
                    where=(asphalt + grass) > 1e-6)
    return tuple(bytearray(np.rint(np.clip(ch, 0.0, 1.0) * 255.0)
                           .astype(np.uint8).reshape(-1).tolist())
                 for ch in (red, green, blue))


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--archive", required=True, type=Path,
                    help="the pinned CityWorld.zip")
    ap.add_argument("--overlay-tobj", type=Path, default=None,
                    help="overlay tobj contributing procedural roads")
    ap.add_argument("--xml-converter", required=True, type=Path)
    ap.add_argument("--output", required=True, type=Path)
    ap.add_argument("--size", type=int, default=terrain_layers.BLEND_MAP_SIZE)
    ap.add_argument("--supersample", type=int, default=4)
    ap.add_argument("--workdir", type=Path, default=None)
    args = ap.parse_args(argv)

    if args.size <= 0 or (args.size & (args.size - 1)):
        raise CoverageError(f"size must be a positive power of two: {args.size}")

    tmp = args.workdir or Path(tempfile.mkdtemp(prefix="cityworld-coverage-"))
    tmp.mkdir(parents=True, exist_ok=True)
    print(f"working in {tmp}")

    index = index_archive(args.archive, tmp)
    with zipfile.ZipFile(args.archive) as zf:
        tobj_name = next(n for n in zf.namelist() if n.lower().endswith(".tobj"))
        tobj_text = zf.read(tobj_name).decode("utf-8", "replace")
    placements = read_placements(tobj_text, index)
    if not placements:
        raise CoverageError("no placements resolved from the archive tobj")
    meshes = {p["mesh"] for p in placements}
    print(f"resolved {len(placements)} placements over {len(meshes)} meshes")

    convert_meshes(meshes, index, args.xml_converter, tmp / "xml")
    tris, classes = collect_ground_triangles(placements, tmp / "xml")
    print(f"kept {len(tris)} near-ground triangles")

    if args.overlay_tobj is not None:
        road_tris, road_cls = procedural_road_triangles(
            args.overlay_tobj.read_text(errors="replace"))
        if len(road_tris):
            tris = np.concatenate([tris, road_tris], 0)
            classes = np.concatenate([classes, road_cls], 0)
            print(f"added {len(road_tris)} procedural-road triangles")

    print(f"rasterizing at {args.size}^2 "
          f"({WORLD_SIZE_M / args.size:.4f} m/texel, ss={args.supersample})")
    coverage = rasterize(tris, classes, args.size, args.supersample)

    for i, name in enumerate(("grass", "asphalt", "concrete", "rock")):
        print(f"  {name:9s} {100 * coverage[i].mean():7.4f}% of page")

    red, green, blue = coverage_to_channels(coverage)
    png = terrain_layers.encode_png_rgba(args.size, red, green, blue)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(png)
    print(f"wrote {args.output} ({len(png)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
