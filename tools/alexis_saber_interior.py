#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Deterministic cabin-interior shell for the Alexis Saber.

Through every window of ``AlexisSaber.zip`` the cabin reads as a black void.
That is not a lighting failure: the only geometry inside the glasshouse is the
chassis tub (the ``SaberChassis`` submesh of ``AlexisSaber.mesh``), whose
authored texture is near-black almost everywhere (mean sRGB (35,32,30), a
1.6 per cent linear albedo), and the seats, dash and steering wheel the author
intended are commented-out prop lines referencing ``AlexisProxim*.mesh``
members that were never shipped in this archive.  Transparent glass over a
1.6 per cent tub is indistinguishable from opaque black glass.

This module authors the smallest honest interior: a static shell (parcel
shelf, rear bulkhead, carpet, transmission tunnel, dash, door cards, two seats
with headrests) built in the exact local coordinate frame of
``AlexisSaber.mesh`` so it can ride the very same flexbody placement line the
body already uses.

Material budget.  The combined presenter admits AlexisSaber materials through
an exact reviewed set (``OgreNextDemoAllowsAlexisTUS0Approximation`` +
``IsExactAlexisDiffuseProjection`` in
``source/main/gfx/ogre14/detail/OgreNextDemoPrivatePolicy.cpp`` /
``OgreNextDemoMaterialSource.cpp``); a brand-new managed material name would
need a new review.  The shell therefore binds the already-reviewed
``SaberWheels`` managed material: its instanced projection is admitted for the
wheel meshes today, and this mesh simply becomes one more section of that
projection.  Every other reviewed sheet is fully claimed - the body paint
deliberately unwraps every panel across the whole square
(``alexis_saber_paint.py``) and the chassis sheet is rasterized 100 per cent
by the tub - while the wheel sheet's authored art (tread strip + spoke star)
covers only 46 per cent of its 600x400 texels.  The shell's UV islands live in
three rectangles of the remaining flat-fill background, re-authored below as
seat fabric, carpet and trim swatches.  The wheel art texels themselves are
copied byte-for-byte; a 4+ texel guard ring separates the islands from every
rasterized wheel texel, so the wheels keep their exact presented colour
through the first two mip levels, and beyond that the swatches average
against a background whose luminance they straddle.

Everything is stdlib-only and bit-deterministic; the PNG encoder and the
coordinate hash are the reviewed ones from ``tools/cityworld_road_texture.py``
(via ``alexis_saber_paint``).
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import importlib.util as _importlib_util
import math
from pathlib import Path
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

_hash32 = alexis_saber_paint._hash32
encode_png_rgba = alexis_saber_paint.encode_png_rgba


class InteriorError(RuntimeError):
    """Raised when the interior cannot be authored deterministically."""


INTERIOR_FORMAT = "ror-alexis-saber-interior-v1"

#: The compiled member this module authors and the flexbody line that places
#: it.  The placement parameters are copied verbatim from the
#: ``AlexisSaber.mesh`` flexbody, so shell vertices authored in that mesh's
#: local frame land at the same world positions as equivalent body vertices.
MESH_MEMBER = "AlexisSaberInterior.mesh"
TRUCK_FLEXBODY_LINE = (
    b"20, 94, 5, 0.1, 0.5, -0.4, 180, 90, 0, AlexisSaberInterior.mesh")
TRUCK_FORSET_LINE = (
    b"forset 58-68, 78-81, 87-90, 0, 5, 20, 25, 93, 94, 6, 8-10, 15-17, 19, "
    b"41-48, 11-14, 49-50, 52-55, 109-116, 56, 57, 69, 0-4, 21, 23-25, "
    b"72-78, 81, 101-108")

#: The reviewed managed material the shell binds and the members it samples.
MATERIAL_NAME = "SaberWheels"
WHEEL_MEMBER = "AlexisSaberWheel.png"
WHEEL_SPEC_MEMBER = "AlexisSaberWheelSpec.png"

#: Wheel sheet dimensions; the swatch rectangles below are meaningless against
#: any other size, so both are asserted at patch time.
WHEEL_SHEET_SIZE = (600, 400)

#: Swatch rectangles (x0, y0, x1, y1), inclusive, in wheel-sheet texels.
#: All three sit in the flat background fill with at least a 4-texel guard to
#: the nearest texel rasterized by any ``SaberWheels`` face (measured by
#: wrap-folded rasterization of every submesh in the archive; the wheel art
#: keeps a >40 texel margin in practice).  UV islands inset a further 8 texels
#: so bilinear taps and the first two mip levels never leave their swatch.
SEAT_RECT = (445, 280, 595, 395)
CARPET_RECT = (245, 280, 385, 395)
TRIM_RECT = (245, 4, 595, 46)

_ISLAND_INSET = 8

#: Swatch base colours, sRGB.  Chosen against the presenter's seated SH-9 sky
#: ambient: a charcoal cabin that stays clearly above the void threshold
#: through the ~68 per cent two-way glass without reading as painted primer.
#: The wheel sheet's own background fill is sRGB ~(150,150,150), so all three
#: swatches darken the sheet locally rather than brightening it.
SEAT_BASE = (122, 119, 115)
CARPET_BASE = (66, 64, 62)
TRIM_BASE = (100, 97, 94)

#: Per-texel hash noise amplitude, per swatch, so the surfaces read as
#: material rather than flat fill.  The salts keep the three fields
#: decorrelated; values are small enough that no texel leaves its family.
SEAT_NOISE = 7
CARPET_NOISE = 6
TRIM_NOISE = 3
_SEAT_SALT = 0x53
_CARPET_SALT = 0x6B
_TRIM_SALT = 0x7F

#: Specular level written under every swatch in the ``*Spec`` member, in the
#: shipped linear 0..255 encoding.  Cabin fabric and trim are matte; the
#: sheet's own background is ~(60) and the swatch level sits well under it.
SWATCH_SPECULAR_LEVEL = 12


# --------------------------------------------------------------------------
# Cabin geometry, in AlexisSaber.mesh local coordinates (+z nose, +y roof).
#
# Measured cabin envelope (rasterized from the shipped meshes):
#   tub floor top          y -0.469, cabin z -1.00..+0.40
#   rear deck (shipped)    y <= 0.190, z -1.75..-1.05
#   cowl deck (shipped)    y <= 0.190, z +0.40..+0.80
#   rear glass             z -1.75 (base, y 0.26) rising to z -0.75 (y 0.66)
#   windshield             z +0.75 (base, y 0.19) rising to z +0.50 (y 0.33)
#   beltline glass base    y 0.19..0.29, side glass |x| <= 0.71
#
# Every shell surface floats 5-15 mm inside the shipped geometry so nothing
# z-fights the tub or pokes through glass.
# --------------------------------------------------------------------------

_FLOOR_Y = -0.44
_BELT_Y = 0.20
_SHELF_Y = 0.20
_SHELF_REAR_Z = -1.70
_SHELF_REAR_HALF_X = 0.42
_SHELF_FRONT_Z = -1.02
_SHELF_FRONT_HALF_X = 0.66
_BULKHEAD_Z = -1.02
_BULKHEAD_HALF_X = 0.64
_CARPET_HALF_X = 0.62
_CARPET_REAR_Z = -1.00
_CARPET_FRONT_Z = 0.40
_TUNNEL_HALF_X = 0.13
_TUNNEL_TOP_Y = -0.22
_DASH_TOP_Y = 0.205
_DASH_REAR_Z = 0.36
_DASH_FRONT_Z = 0.66
_DASH_REAR_HALF_X = 0.66
_DASH_FRONT_HALF_X = 0.62
_DASH_FASCIA_BOTTOM_Y = -0.10
_DOOR_CARD_X = 0.62
_DOOR_CARD_REAR_Z = -0.88
_DOOR_CARD_FRONT_Z = 0.36
_SEAT_CENTER_X = 0.34
_SEAT_HALF_WIDTH = 0.26
_CUSHION_TOP_Y = -0.16
_CUSHION_FRONT_Z = -0.10
_CUSHION_REAR_Z = -0.52
_BACK_BOTTOM_Z = -0.55
_BACK_BOTTOM_Y = -0.20
_BACK_TOP_Z = -0.72
_BACK_TOP_Y = 0.28
_BACK_THICKNESS = 0.08
_HEADREST_HALF_WIDTH = 0.11
_HEADREST_BOTTOM_Y = 0.32
_HEADREST_TOP_Y = 0.44
_HEADREST_FRONT_Z = -0.76
_HEADREST_THICKNESS = 0.07


@dataclass(frozen=True)
class _Vertex:
    position: tuple[float, float, float]
    normal: tuple[float, float, float]
    uv: tuple[float, float]


def _normalize(vector: tuple[float, float, float]) -> tuple[float, float, float]:
    length = math.sqrt(sum(component * component for component in vector))
    if length < 1e-9:
        raise InteriorError(f"degenerate normal {vector}")
    return (vector[0] / length, vector[1] / length, vector[2] / length)


class _MeshBuilder:
    """Accumulates quads as indexed triangles with per-face normals."""

    def __init__(self) -> None:
        self.vertices: list[_Vertex] = []
        self.faces: list[tuple[int, int, int]] = []

    def quad(self, corners: list[tuple[float, float, float]],
             normal: tuple[float, float, float],
             uv_rect: tuple[float, float, float, float]) -> None:
        """One quad; corners counter-clockwise when viewed from `normal`.

        The corner order maps to the UV rect as (u0,v0) (u1,v0) (u1,v1)
        (u0,v1).  OGRE renders counter-clockwise faces as front faces under
        the default clockwise culling, and the capture preserves that
        winding, so the quad is visible exactly from the half-space the
        normal points into.
        """

        if len(corners) != 4:
            raise InteriorError("a quad needs exactly four corners")
        unit = _normalize(normal)
        a = tuple(corners[1][i] - corners[0][i] for i in range(3))
        b = tuple(corners[2][i] - corners[0][i] for i in range(3))
        cross = (a[1] * b[2] - a[2] * b[1],
                 a[2] * b[0] - a[0] * b[2],
                 a[0] * b[1] - a[1] * b[0])
        alignment = sum(cross[i] * unit[i] for i in range(3))
        if alignment <= 1e-9:
            raise InteriorError(
                f"quad winding disagrees with its normal {normal} at "
                f"{corners[0]}")
        u0, v0, u1, v1 = uv_rect
        if not (u1 > u0 and v1 > v0):
            raise InteriorError(f"empty UV rect {uv_rect}")
        base = len(self.vertices)
        uvs = [(u0, v0), (u1, v0), (u1, v1), (u0, v1)]
        for corner, uv in zip(corners, uvs):
            self.vertices.append(_Vertex(corner, unit, uv))
        self.faces.append((base + 0, base + 1, base + 2))
        self.faces.append((base + 0, base + 2, base + 3))


def _island(rect: tuple[int, int, int, int],
            fx0: float, fy0: float, fx1: float, fy1: float
            ) -> tuple[float, float, float, float]:
    """A UV sub-rectangle of a swatch, in fractions of its inset island."""

    width, height = WHEEL_SHEET_SIZE
    x0, y0, x1, y1 = rect
    ix0, iy0 = x0 + _ISLAND_INSET, y0 + _ISLAND_INSET
    ix1, iy1 = x1 - _ISLAND_INSET, y1 - _ISLAND_INSET
    if ix1 <= ix0 or iy1 <= iy0:
        raise InteriorError(f"swatch {rect} is too small for its inset")
    u0 = (ix0 + fx0 * (ix1 - ix0)) / width
    u1 = (ix0 + fx1 * (ix1 - ix0)) / width
    v0 = (iy0 + fy0 * (iy1 - iy0)) / height
    v1 = (iy0 + fy1 * (iy1 - iy0)) / height
    return (u0, v0, u1, v1)


def build_interior_mesh() -> _MeshBuilder:
    """The full cabin shell.  Deterministic vertex and face order."""

    mesh = _MeshBuilder()
    seat_uv = lambda *f: _island(SEAT_RECT, *f)      # noqa: E731
    carpet_uv = lambda *f: _island(CARPET_RECT, *f)  # noqa: E731
    trim_uv = lambda *f: _island(TRIM_RECT, *f)      # noqa: E731

    # Parcel shelf: trapezoid following the rear-glass planform, 10 mm above
    # the shipped rear deck.
    mesh.quad(
        [(-_SHELF_FRONT_HALF_X, _SHELF_Y, _SHELF_FRONT_Z),
         (_SHELF_FRONT_HALF_X, _SHELF_Y, _SHELF_FRONT_Z),
         (_SHELF_REAR_HALF_X, _SHELF_Y, _SHELF_REAR_Z),
         (-_SHELF_REAR_HALF_X, _SHELF_Y, _SHELF_REAR_Z)],
        (0.0, 1.0, 0.0), carpet_uv(0.0, 0.0, 1.0, 0.55))

    # Rear bulkhead under the shelf's front edge, facing the cabin.
    mesh.quad(
        [(-_BULKHEAD_HALF_X, _FLOOR_Y, _BULKHEAD_Z),
         (_BULKHEAD_HALF_X, _FLOOR_Y, _BULKHEAD_Z),
         (_BULKHEAD_HALF_X, _SHELF_Y, _BULKHEAD_Z),
         (-_BULKHEAD_HALF_X, _SHELF_Y, _BULKHEAD_Z)],
        (0.0, 0.0, 1.0), trim_uv(0.0, 0.0, 0.48, 1.0))

    # Carpet, split around the transmission tunnel.
    for side in (1.0, -1.0):
        inner = side * _TUNNEL_HALF_X
        outer = side * _CARPET_HALF_X
        lo_x, hi_x = sorted((inner, outer))
        mesh.quad(
            [(lo_x, _FLOOR_Y, _CARPET_FRONT_Z),
             (hi_x, _FLOOR_Y, _CARPET_FRONT_Z),
             (hi_x, _FLOOR_Y, _CARPET_REAR_Z),
             (lo_x, _FLOOR_Y, _CARPET_REAR_Z)],
            (0.0, 1.0, 0.0), carpet_uv(0.0, 0.6, 0.45, 1.0))

    # Transmission tunnel: top and both sides.
    mesh.quad(
        [(-_TUNNEL_HALF_X, _TUNNEL_TOP_Y, _CARPET_FRONT_Z),
         (_TUNNEL_HALF_X, _TUNNEL_TOP_Y, _CARPET_FRONT_Z),
         (_TUNNEL_HALF_X, _TUNNEL_TOP_Y, _CARPET_REAR_Z),
         (-_TUNNEL_HALF_X, _TUNNEL_TOP_Y, _CARPET_REAR_Z)],
        (0.0, 1.0, 0.0), carpet_uv(0.5, 0.6, 0.75, 1.0))
    for side in (1.0, -1.0):
        x = side * _TUNNEL_HALF_X
        rear, front = _CARPET_REAR_Z, _CARPET_FRONT_Z
        corners = [(x, _FLOOR_Y, front), (x, _FLOOR_Y, rear),
                   (x, _TUNNEL_TOP_Y, rear), (x, _TUNNEL_TOP_Y, front)]
        if side < 0.0:
            corners.reverse()
        mesh.quad(corners, (side, 0.0, 0.0),
                  carpet_uv(0.8, 0.6, 1.0, 1.0))

    # Dash: top pad and a fascia facing the seats.
    mesh.quad(
        [(-_DASH_FRONT_HALF_X, _DASH_TOP_Y, _DASH_FRONT_Z),
         (_DASH_FRONT_HALF_X, _DASH_TOP_Y, _DASH_FRONT_Z),
         (_DASH_REAR_HALF_X, _DASH_TOP_Y, _DASH_REAR_Z),
         (-_DASH_REAR_HALF_X, _DASH_TOP_Y, _DASH_REAR_Z)],
        (0.0, 1.0, 0.0), trim_uv(0.52, 0.0, 1.0, 0.45))
    mesh.quad(
        [(_DASH_REAR_HALF_X, _DASH_FASCIA_BOTTOM_Y, _DASH_REAR_Z),
         (-_DASH_REAR_HALF_X, _DASH_FASCIA_BOTTOM_Y, _DASH_REAR_Z),
         (-_DASH_REAR_HALF_X, _DASH_TOP_Y, _DASH_REAR_Z),
         (_DASH_REAR_HALF_X, _DASH_TOP_Y, _DASH_REAR_Z)],
        (0.0, 0.0, -1.0), trim_uv(0.52, 0.5, 1.0, 1.0))

    # Door cards.
    for side in (1.0, -1.0):
        x = side * _DOOR_CARD_X
        rear, front = _DOOR_CARD_REAR_Z, _DOOR_CARD_FRONT_Z
        corners = [(x, _FLOOR_Y, rear), (x, _FLOOR_Y, front),
                   (x, _BELT_Y, front), (x, _BELT_Y, rear)]
        if side < 0.0:
            corners.reverse()
        mesh.quad(corners, (-side, 0.0, 0.0),
                  trim_uv(0.0, 0.0, 0.48, 1.0))

    # Seats: cushion, reclined back, headrest.  The back leans rearward, so
    # its rear surface faces rear-and-slightly-down and its front surface
    # faces front-and-up; both normals are exact.
    back_slope = _normalize((0.0, _BACK_TOP_Y - _BACK_BOTTOM_Y,
                             _BACK_TOP_Z - _BACK_BOTTOM_Z))
    back_front_normal = (0.0, -back_slope[2], back_slope[1])
    back_rear_normal = (0.0, back_slope[2], -back_slope[1])
    for side in (1.0, -1.0):
        lo_x = side * _SEAT_CENTER_X - _SEAT_HALF_WIDTH
        hi_x = side * _SEAT_CENTER_X + _SEAT_HALF_WIDTH
        # Cushion top.
        mesh.quad(
            [(lo_x, _CUSHION_TOP_Y, _CUSHION_FRONT_Z),
             (hi_x, _CUSHION_TOP_Y, _CUSHION_FRONT_Z),
             (hi_x, _CUSHION_TOP_Y, _CUSHION_REAR_Z),
             (lo_x, _CUSHION_TOP_Y, _CUSHION_REAR_Z)],
            (0.0, 1.0, 0.0), seat_uv(0.0, 0.0, 0.45, 0.4))
        # Cushion front face.
        mesh.quad(
            [(lo_x, _FLOOR_Y, _CUSHION_FRONT_Z),
             (hi_x, _FLOOR_Y, _CUSHION_FRONT_Z),
             (hi_x, _CUSHION_TOP_Y, _CUSHION_FRONT_Z),
             (lo_x, _CUSHION_TOP_Y, _CUSHION_FRONT_Z)],
            (0.0, 0.0, 1.0), seat_uv(0.0, 0.45, 0.45, 0.7))
        # Cushion side faces.
        for face_x, direction in ((hi_x, 1.0), (lo_x, -1.0)):
            corners = [(face_x, _FLOOR_Y, _CUSHION_FRONT_Z),
                       (face_x, _FLOOR_Y, _CUSHION_REAR_Z),
                       (face_x, _CUSHION_TOP_Y, _CUSHION_REAR_Z),
                       (face_x, _CUSHION_TOP_Y, _CUSHION_FRONT_Z)]
            if direction < 0.0:
                corners.reverse()
            mesh.quad(corners, (direction, 0.0, 0.0),
                      seat_uv(0.5, 0.45, 0.95, 0.7))
        # Seat back: front, rear, top cap, side caps.
        rear_bottom_z = _BACK_BOTTOM_Z - _BACK_THICKNESS
        rear_top_z = _BACK_TOP_Z - _BACK_THICKNESS
        mesh.quad(
            [(lo_x, _BACK_BOTTOM_Y, _BACK_BOTTOM_Z),
             (hi_x, _BACK_BOTTOM_Y, _BACK_BOTTOM_Z),
             (hi_x, _BACK_TOP_Y, _BACK_TOP_Z),
             (lo_x, _BACK_TOP_Y, _BACK_TOP_Z)],
            back_front_normal, seat_uv(0.0, 0.0, 0.45, 0.95))
        mesh.quad(
            [(hi_x, _BACK_BOTTOM_Y, rear_bottom_z),
             (lo_x, _BACK_BOTTOM_Y, rear_bottom_z),
             (lo_x, _BACK_TOP_Y, rear_top_z),
             (hi_x, _BACK_TOP_Y, rear_top_z)],
            back_rear_normal, seat_uv(0.5, 0.0, 0.95, 0.95))
        mesh.quad(
            [(lo_x, _BACK_TOP_Y, _BACK_TOP_Z),
             (hi_x, _BACK_TOP_Y, _BACK_TOP_Z),
             (hi_x, _BACK_TOP_Y, rear_top_z),
             (lo_x, _BACK_TOP_Y, rear_top_z)],
            (0.0, 1.0, 0.0), seat_uv(0.0, 0.95, 0.45, 1.0))
        for face_x, direction in ((hi_x, 1.0), (lo_x, -1.0)):
            corners = [(face_x, _BACK_BOTTOM_Y, _BACK_BOTTOM_Z),
                       (face_x, _BACK_BOTTOM_Y, rear_bottom_z),
                       (face_x, _BACK_TOP_Y, rear_top_z),
                       (face_x, _BACK_TOP_Y, _BACK_TOP_Z)]
            if direction < 0.0:
                corners.reverse()
            mesh.quad(corners, (direction, 0.0, 0.0),
                      seat_uv(0.5, 0.95, 0.95, 1.0))
        # Headrest: front, rear, top, sides.
        head_lo_x = side * _SEAT_CENTER_X - _HEADREST_HALF_WIDTH
        head_hi_x = side * _SEAT_CENTER_X + _HEADREST_HALF_WIDTH
        head_rear_z = _HEADREST_FRONT_Z - _HEADREST_THICKNESS
        mesh.quad(
            [(head_lo_x, _HEADREST_BOTTOM_Y, _HEADREST_FRONT_Z),
             (head_hi_x, _HEADREST_BOTTOM_Y, _HEADREST_FRONT_Z),
             (head_hi_x, _HEADREST_TOP_Y, _HEADREST_FRONT_Z),
             (head_lo_x, _HEADREST_TOP_Y, _HEADREST_FRONT_Z)],
            (0.0, 0.0, 1.0), seat_uv(0.0, 0.0, 0.45, 0.3))
        mesh.quad(
            [(head_hi_x, _HEADREST_BOTTOM_Y, head_rear_z),
             (head_lo_x, _HEADREST_BOTTOM_Y, head_rear_z),
             (head_lo_x, _HEADREST_TOP_Y, head_rear_z),
             (head_hi_x, _HEADREST_TOP_Y, head_rear_z)],
            (0.0, 0.0, -1.0), seat_uv(0.5, 0.0, 0.95, 0.3))
        mesh.quad(
            [(head_lo_x, _HEADREST_TOP_Y, _HEADREST_FRONT_Z),
             (head_hi_x, _HEADREST_TOP_Y, _HEADREST_FRONT_Z),
             (head_hi_x, _HEADREST_TOP_Y, head_rear_z),
             (head_lo_x, _HEADREST_TOP_Y, head_rear_z)],
            (0.0, 1.0, 0.0), seat_uv(0.0, 0.3, 0.45, 0.4))
        for face_x, direction in ((head_hi_x, 1.0), (head_lo_x, -1.0)):
            corners = [(face_x, _HEADREST_BOTTOM_Y, _HEADREST_FRONT_Z),
                       (face_x, _HEADREST_BOTTOM_Y, head_rear_z),
                       (face_x, _HEADREST_TOP_Y, head_rear_z),
                       (face_x, _HEADREST_TOP_Y, _HEADREST_FRONT_Z)]
            if direction < 0.0:
                corners.reverse()
            mesh.quad(corners, (direction, 0.0, 0.0),
                      seat_uv(0.5, 0.3, 0.95, 0.4))
    return mesh


def build_mesh_xml() -> bytes:
    """The OGRE mesh XML for the shell, formatted deterministically."""

    mesh = build_interior_mesh()
    lines = ['<?xml version="1.0"?>', "<mesh>"]
    lines.append(f'\t<sharedgeometry vertexcount="{len(mesh.vertices)}">')
    lines.append('\t\t<vertexbuffer positions="true" normals="true" '
                 'texture_coord_dimensions_0="float2" texture_coords="1">')
    for vertex in mesh.vertices:
        px, py, pz = vertex.position
        nx, ny, nz = vertex.normal
        u, v = vertex.uv
        lines.append("\t\t\t<vertex>")
        lines.append(f'\t\t\t\t<position x="{px:.6f}" y="{py:.6f}" '
                     f'z="{pz:.6f}" />')
        lines.append(f'\t\t\t\t<normal x="{nx:.6f}" y="{ny:.6f}" '
                     f'z="{nz:.6f}" />')
        lines.append(f'\t\t\t\t<texcoord u="{u:.6f}" v="{v:.6f}" />')
        lines.append("\t\t\t</vertex>")
    lines.append("\t\t</vertexbuffer>")
    lines.append("\t</sharedgeometry>")
    lines.append("\t<submeshes>")
    lines.append(f'\t\t<submesh material="{MATERIAL_NAME}" '
                 'usesharedvertices="true" use32bitindexes="false" '
                 'operationtype="triangle_list">')
    lines.append(f'\t\t\t<faces count="{len(mesh.faces)}">')
    for v1, v2, v3 in mesh.faces:
        lines.append(f'\t\t\t\t<face v1="{v1}" v2="{v2}" v3="{v3}" />')
    lines.append("\t\t\t</faces>")
    lines.append("\t\t</submesh>")
    lines.append("\t</submeshes>")
    lines.append("</mesh>")
    lines.append("")
    return "\n".join(lines).encode("ascii")


# --------------------------------------------------------------------------
# Wheel-sheet swatch authoring.
# --------------------------------------------------------------------------


def _decode_png_rgb_like(name: str, payload: bytes
                         ) -> tuple[int, int, int, bytearray]:
    """Decodes a non-interlaced 8-bit RGB or RGBA PNG member, strictly."""

    if payload[:8] != b"\x89PNG\r\n\x1a\n":
        raise InteriorError(f"member '{name}' is not a PNG")
    width = height = channels = None
    idat = bytearray()
    cursor = 8
    while cursor + 8 <= len(payload):
        length, tag = struct.unpack_from(">I4s", payload, cursor)
        chunk = payload[cursor + 8:cursor + 8 + length]
        if len(chunk) != length:
            raise InteriorError(f"member '{name}' has a truncated chunk")
        if tag == b"IHDR":
            width, height, depth, colour, _, _, interlace = struct.unpack(
                ">IIBBBBB", chunk)
            if depth != 8 or interlace != 0 or colour not in (2, 6):
                raise InteriorError(
                    f"member '{name}' is not non-interlaced 8-bit RGB(A) "
                    f"(depth={depth} colour={colour} interlace={interlace})")
            channels = 3 if colour == 2 else 4
        elif tag == b"IDAT":
            idat.extend(chunk)
        elif tag == b"IEND":
            break
        cursor += 12 + length
    if width is None or height is None or channels is None or not idat:
        raise InteriorError(f"member '{name}' has no image data")

    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    if len(raw) != (stride + 1) * height:
        raise InteriorError(
            f"member '{name}' has a malformed scanline payload")
    pixels = bytearray(stride * height)
    previous = bytearray(stride)
    for y in range(height):
        filter_type = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        if filter_type == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif filter_type == 2:
            for i in range(stride):
                line[i] = (line[i] + previous[i]) & 0xFF
        elif filter_type == 3:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + (left + previous[i]) // 2) & 0xFF
        elif filter_type == 4:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                b = previous[i]
                c = previous[i - channels] if i >= channels else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                predictor = a if pa <= pb and pa <= pc else (
                    b if pb <= pc else c)
                line[i] = (line[i] + predictor) & 0xFF
        elif filter_type != 0:
            raise InteriorError(
                f"member '{name}' uses PNG filter {filter_type}")
        pixels[y * stride:(y + 1) * stride] = line
        previous = line
    return width, height, channels, pixels


def _rgba_from(width: int, height: int, channels: int,
               pixels: bytearray) -> bytearray:
    if channels == 4:
        return bytearray(pixels)
    rgba = bytearray(width * height * 4)
    for i in range(width * height):
        rgba[i * 4:i * 4 + 3] = pixels[i * 3:i * 3 + 3]
        rgba[i * 4 + 3] = 255
    return rgba


def _clamp_byte(value: int) -> int:
    return 0 if value < 0 else (255 if value > 255 else value)


def _paint_swatch(rgba: bytearray, width: int,
                  rect: tuple[int, int, int, int],
                  base: tuple[int, int, int], noise: int, salt: int) -> None:
    x0, y0, x1, y1 = rect
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            wobble = (_hash32(x, y, salt) % (2 * noise + 1)) - noise
            index = (y * width + x) * 4
            rgba[index + 0] = _clamp_byte(base[0] + wobble)
            rgba[index + 1] = _clamp_byte(base[1] + wobble)
            rgba[index + 2] = _clamp_byte(base[2] + wobble)
            rgba[index + 3] = 255


def build_wheel_members(wheel_payload: bytes,
                        wheel_spec_payload: bytes) -> dict[str, bytes]:
    """Both wheel members with the three swatches authored in.

    Deterministic and idempotent: every texel inside a swatch rectangle is
    overwritten from constants and the coordinate hash, and every texel
    outside keeps its decoded value, so re-running against the output
    reproduces it byte for byte.
    """

    members: dict[str, bytes] = {}
    for name, payload, swatches in (
            (WHEEL_MEMBER, wheel_payload,
             ((SEAT_RECT, SEAT_BASE, SEAT_NOISE, _SEAT_SALT),
              (CARPET_RECT, CARPET_BASE, CARPET_NOISE, _CARPET_SALT),
              (TRIM_RECT, TRIM_BASE, TRIM_NOISE, _TRIM_SALT))),
            (WHEEL_SPEC_MEMBER, wheel_spec_payload,
             tuple((rect,
                    (SWATCH_SPECULAR_LEVEL,) * 3, 0, 0)
                   for rect in (SEAT_RECT, CARPET_RECT, TRIM_RECT)))):
        width, height, channels, pixels = _decode_png_rgb_like(name, payload)
        if (width, height) != WHEEL_SHEET_SIZE:
            raise InteriorError(
                f"member '{name}' is {width}x{height}, expected "
                f"{WHEEL_SHEET_SIZE[0]}x{WHEEL_SHEET_SIZE[1]}; the swatch "
                f"rectangles are meaningless against it")
        rgba = _rgba_from(width, height, channels, pixels)
        for rect, base, noise, salt in swatches:
            x0, y0, x1, y1 = rect
            if not (0 <= x0 < x1 < width and 0 <= y0 < y1 < height):
                raise InteriorError(f"swatch {rect} leaves the sheet")
            _paint_swatch(rgba, width, rect, base, noise, salt)
        members[name] = encode_png_rgba(width, height, bytes(rgba))
    return members


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Dump the authored Alexis Saber interior mesh XML.")
    parser.add_argument(
        "--output-dir", type=Path, required=True,
        help="directory that receives AlexisSaberInterior.mesh.xml")
    args = parser.parse_args(argv)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    xml_path = args.output_dir / "AlexisSaberInterior.mesh.xml"
    xml_path.write_bytes(build_mesh_xml())
    mesh = build_interior_mesh()
    print(f"{INTERIOR_FORMAT}: {len(mesh.vertices)} vertices, "
          f"{len(mesh.faces)} faces -> {xml_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
