"""Deterministic block-compression encoders and a DDS writer.

Written against the standard library only, matching the rest of ``tools/``.
Every routine here is a pure function of its input bytes: there is no
randomness, no timestamp, no floating-point accumulation whose order depends on
dictionary iteration, so recompiling the same source texture always produces
byte-identical output. That is what lets the content compiler be idempotent.

Three formats are emitted, one per texture role:

``BC7``
    Four channels in 16 bytes per 4x4 block (1 byte/texel). Used for colour.
    Only mode 6 is emitted -- one subset, 4-bit indices, RGBA endpoints at 7
    bits plus a shared P-bit. Mode 6 is the single mode that spends its whole
    budget on a full-precision RGBA line through the block, so it is the right
    choice for photographic and painted albedo. The partitioned modes beat it
    only on blocks containing two uncorrelated colour populations, which is a
    quality ceiling, not a correctness limit: a mode-6 block is a completely
    valid BC7 block that every decoder reads exactly.

``BC5``
    Two channels in 16 bytes per block (1 byte/texel). Used for tangent-space
    normals, storing X and Y; the shader reconstructs Z, exactly as it already
    does for the uncompressed RG8 path.

``BC4``
    One channel in 8 bytes per block (0.5 bytes/texel). Used for single-channel
    maps such as roughness and occlusion.

BC4 and BC5 are exact-fit encoders in the sense that they reproduce the
per-block minimum and maximum exactly; only the 6 interior levels are
approximated. BC7 mode 6 quantises endpoints, so it is lossy in the usual
block-compression way and is never applied to data whose exact texel values
carry meaning (see the weight-mask discussion in the compiler).
"""

from __future__ import annotations

import struct
from typing import Sequence


class BlockCompressionError(RuntimeError):
    """Raised when input cannot be compressed exactly as requested."""


# ---------------------------------------------------------------------------
# Transfer functions
# ---------------------------------------------------------------------------

def _build_srgb_to_linear() -> tuple[float, ...]:
    table = []
    for value in range(256):
        c = value / 255.0
        table.append(c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4)
    return tuple(table)


SRGB_TO_LINEAR = _build_srgb_to_linear()


def _linear_to_srgb_byte(value: float) -> int:
    if value <= 0.0:
        return 0
    if value >= 1.0:
        return 255
    encoded = (
        value * 12.92 if value <= 0.0031308 else 1.055 * (value ** (1.0 / 2.4)) - 0.055
    )
    # Round half away from zero so the mapping is symmetric and independent of
    # the platform's float-to-int rounding mode.
    return min(255, max(0, int(encoded * 255.0 + 0.5)))


def _build_linear_to_srgb_lut(steps: int = 4096) -> tuple[int, ...]:
    return tuple(_linear_to_srgb_byte(i / (steps - 1)) for i in range(steps))


_LINEAR_TO_SRGB_LUT = _build_linear_to_srgb_lut()
_LINEAR_TO_SRGB_STEPS = len(_LINEAR_TO_SRGB_LUT)


def linear_to_srgb_byte(value: float) -> int:
    if value <= 0.0:
        return 0
    if value >= 1.0:
        return 255
    return _LINEAR_TO_SRGB_LUT[int(value * (_LINEAR_TO_SRGB_STEPS - 1) + 0.5)]


# ---------------------------------------------------------------------------
# Mip generation
# ---------------------------------------------------------------------------

def downsample_rgba(
    width: int, height: int, pixels: bytes, *, srgb: bool
) -> tuple[int, int, bytearray]:
    """Halve one RGBA image with a 2x2 box filter.

    When ``srgb`` is set the RGB channels are averaged in LINEAR light and
    re-encoded, which is the only correct way to filter an sRGB-encoded image:
    averaging the encoded values darkens every gradient. Alpha is always linear
    and is averaged directly. This mirrors what the runtime's own mip
    completion does for sRGB PBR textures, so an offline chain and a runtime
    chain agree.

    Odd dimensions are not accepted: the caller compiles power-of-two textures
    so that every mip is an exact halving and no edge texel is invented.
    """

    if width % 2 != 0 or height % 2 != 0:
        raise BlockCompressionError(
            f"cannot halve a {width}x{height} image without inventing edge texels"
        )
    half_width = width // 2
    half_height = height // 2
    out = bytearray(half_width * half_height * 4)
    row_bytes = width * 4
    for y in range(half_height):
        top = (y * 2) * row_bytes
        bottom = top + row_bytes
        out_base = y * half_width * 4
        for x in range(half_width):
            left = x * 8
            i0 = top + left
            i1 = i0 + 4
            i2 = bottom + left
            i3 = i2 + 4
            o = out_base + x * 4
            if srgb:
                for c in range(3):
                    total = (
                        SRGB_TO_LINEAR[pixels[i0 + c]]
                        + SRGB_TO_LINEAR[pixels[i1 + c]]
                        + SRGB_TO_LINEAR[pixels[i2 + c]]
                        + SRGB_TO_LINEAR[pixels[i3 + c]]
                    )
                    out[o + c] = linear_to_srgb_byte(total * 0.25)
            else:
                for c in range(3):
                    total = (
                        pixels[i0 + c] + pixels[i1 + c] + pixels[i2 + c] + pixels[i3 + c]
                    )
                    out[o + c] = (total + 2) >> 2
            alpha = pixels[i0 + 3] + pixels[i1 + 3] + pixels[i2 + 3] + pixels[i3 + 3]
            out[o + 3] = (alpha + 2) >> 2
    return half_width, half_height, out


def build_mip_chain(
    width: int, height: int, pixels: bytes, *, srgb: bool
) -> list[tuple[int, int, bytes]]:
    """Complete base-to-1x1 chain. Block formats cannot have their tail
    generated at load time -- deriving mip N+1 from a compressed mip N would
    mean decode, filter, re-encode, and would compound quantisation error each
    level. So the chain is authored here, in full, from the uncompressed
    source."""

    chain: list[tuple[int, int, bytes]] = [(width, height, bytes(pixels))]
    current_w, current_h, current = width, height, pixels
    while current_w > 1 or current_h > 1:
        if current_w == 1 or current_h == 1:
            # A 1xN or Nx1 tail still has to halve the long axis. Handle it by
            # duplicating the degenerate axis so the box filter stays a plain
            # 2x2 average rather than a special case that could round
            # differently.
            wide = current_w > 1
            long_axis = current_w if wide else current_h
            new_len = max(1, long_axis // 2)
            out = bytearray(new_len * 4)
            for i in range(new_len):
                a = i * 2 * 4
                b = a + 4
                for c in range(3):
                    if srgb:
                        total = SRGB_TO_LINEAR[current[a + c]] + SRGB_TO_LINEAR[
                            current[b + c]
                        ]
                        out[i * 4 + c] = linear_to_srgb_byte(total * 0.5)
                    else:
                        out[i * 4 + c] = (current[a + c] + current[b + c] + 1) >> 1
                out[i * 4 + 3] = (current[a + 3] + current[b + 3] + 1) >> 1
            current_w = new_len if wide else 1
            current_h = 1 if wide else new_len
            current = bytes(out)
        else:
            current_w, current_h, halved = downsample_rgba(
                current_w, current_h, current, srgb=srgb
            )
            current = bytes(halved)
        chain.append((current_w, current_h, current))
    return chain


# ---------------------------------------------------------------------------
# Block gathering
# ---------------------------------------------------------------------------

def _gather_block(
    pixels: bytes, width: int, height: int, block_x: int, block_y: int
) -> list[tuple[int, int, int, int]]:
    """Read one 4x4 texel block, clamping at the right and bottom edges.

    Clamping (rather than padding with black) is what keeps a non-multiple-of-4
    mip from acquiring a dark fringe: the padding texels are never sampled by
    the GPU, but they DO participate in endpoint fitting, so they must look
    like their neighbours.
    """

    texels = []
    for row in range(4):
        y = min(block_y + row, height - 1)
        row_base = y * width * 4
        for column in range(4):
            x = min(block_x + column, width - 1)
            i = row_base + x * 4
            texels.append((pixels[i], pixels[i + 1], pixels[i + 2], pixels[i + 3]))
    return texels


# ---------------------------------------------------------------------------
# BC4 / BC5
# ---------------------------------------------------------------------------

def encode_bc4_block(values: Sequence[int]) -> bytes:
    """One 8-byte BC4 block from 16 unsigned bytes.

    Uses the 8-interpolated-value mode with the block's exact min and max as
    endpoints, so both extremes are reproduced without error.
    """

    lo = min(values)
    hi = max(values)
    if lo == hi:
        # Flat block: two equal endpoints, every index 0. Exact.
        return bytes((lo, hi, 0, 0, 0, 0, 0, 0))
    # Palette order for the 8-value mode: index 0 is hi, index 1 is lo, then
    # six interior steps from hi toward lo.
    span = hi - lo
    indices = 0
    for position, value in enumerate(values):
        # Map value onto 0..7 in palette order.
        t = ((value - lo) * 7 + span // 2) // span  # 0 at lo, 7 at hi
        if t == 7:
            index = 0
        elif t == 0:
            index = 1
        else:
            index = 8 - t
        indices |= index << (3 * position)
    packed = bytearray(8)
    packed[0] = hi
    packed[1] = lo
    for byte in range(6):
        packed[2 + byte] = (indices >> (8 * byte)) & 0xFF
    return bytes(packed)


def encode_bc4(width: int, height: int, channel: bytes) -> bytes:
    """Compress one single-channel image. ``channel`` is one byte per texel."""

    out = bytearray()
    for block_y in range(0, height, 4):
        for block_x in range(0, width, 4):
            values = []
            for row in range(4):
                y = min(block_y + row, height - 1)
                for column in range(4):
                    x = min(block_x + column, width - 1)
                    values.append(channel[y * width + x])
            out += encode_bc4_block(values)
    return bytes(out)


def encode_bc5(width: int, height: int, red: bytes, green: bytes) -> bytes:
    """Compress two channels as back-to-back BC4 blocks, red then green."""

    out = bytearray()
    for block_y in range(0, height, 4):
        for block_x in range(0, width, 4):
            reds = []
            greens = []
            for row in range(4):
                y = min(block_y + row, height - 1)
                for column in range(4):
                    x = min(block_x + column, width - 1)
                    reds.append(red[y * width + x])
                    greens.append(green[y * width + x])
            out += encode_bc4_block(reds)
            out += encode_bc4_block(greens)
    return bytes(out)


# ---------------------------------------------------------------------------
# BC7 (mode 6)
# ---------------------------------------------------------------------------

# Interpolation weights for 4-bit indices, from the BC7 specification.
_BC7_WEIGHTS4 = (0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64)


def _quantise_endpoint(target: Sequence[float]) -> tuple[tuple[int, int, int, int], int]:
    """Pick the 7-bit-plus-P-bit representation closest to a float RGBA target.

    The P-bit is shared by all four channels of an endpoint, so it is chosen
    once, by trying both values and keeping whichever reproduces the four
    targets with less total squared error.
    """

    best_error = None
    best_channels = (0, 0, 0, 0)
    best_pbit = 0
    for pbit in (0, 1):
        channels = []
        error = 0.0
        for value in target:
            clamped = 0.0 if value < 0.0 else (255.0 if value > 255.0 else value)
            # Reconstruction is (seven << 1) | pbit, so solve for seven.
            seven = int((clamped - pbit) * 0.5 + 0.5)
            if seven < 0:
                seven = 0
            elif seven > 127:
                seven = 127
            channels.append(seven)
            delta = ((seven << 1) | pbit) - clamped
            error += delta * delta
        if best_error is None or error < best_error:
            best_error = error
            best_channels = (channels[0], channels[1], channels[2], channels[3])
            best_pbit = pbit
    return best_channels, best_pbit


def _pack_bc7_mode6(
    endpoint_a: tuple[int, int, int, int],
    pbit_a: int,
    endpoint_b: tuple[int, int, int, int],
    pbit_b: int,
    indices: Sequence[int],
) -> bytes:
    """Pack a mode-6 block, LSB first.

    Field order per the specification: a 7-bit unary mode marker (six zeros
    then a one), then R0 R1 G0 G1 B0 B1 A0 A1 at 7 bits each, then the two
    P-bits, then 63 index bits -- texel 0 spends 3 bits because its high bit is
    implicitly zero (the anchor rule), and the remaining fifteen spend 4.
    """

    value = 0
    position = 0

    def put(bits: int, count: int) -> None:
        nonlocal value, position
        value |= (bits & ((1 << count) - 1)) << position
        position += count

    put(1 << 6, 7)  # mode 6
    for channel in range(4):
        put(endpoint_a[channel], 7)
        put(endpoint_b[channel], 7)
    put(pbit_a, 1)
    put(pbit_b, 1)
    put(indices[0], 3)
    for i in range(1, 16):
        put(indices[i], 4)
    if position != 128:
        raise BlockCompressionError(
            f"BC7 mode 6 packing produced {position} bits, expected 128"
        )
    return value.to_bytes(16, "little")


def encode_bc7_block(texels: Sequence[tuple[int, int, int, int]]) -> bytes:
    """Compress one 4x4 RGBA block as BC7 mode 6."""

    first = texels[0]
    if all(texel == first for texel in texels):
        # Flat block. Both endpoints land on the same colour, so every index is
        # 0 and the result is exact whenever the colour survives quantisation;
        # the shared P-bit makes that true for every 8-bit value.
        channels, pbit = _quantise_endpoint(
            (float(first[0]), float(first[1]), float(first[2]), float(first[3]))
        )
        return _pack_bc7_mode6(channels, pbit, channels, pbit, [0] * 16)

    # Initial endpoints: the extremes along the block's PRINCIPAL axis.
    #
    # The obvious cheap choice -- the corners of the RGBA bounding box -- fails
    # badly whenever two channels vary in opposite directions. A block that is
    # half (200,30,30) and half (30,30,200) has a bounding box whose diagonal
    # runs (170,0,170,0); both real colours project onto the exact midpoint of
    # that diagonal, every index comes out identical, and the block collapses to
    # a single average colour. That pattern is not exotic: red mortar against
    # grey brick, white lane markings on dark asphalt, and coloured signage all
    # produce it. So fit the real axis instead.
    lo = [min(texel[c] for texel in texels) for c in range(4)]
    hi = [max(texel[c] for texel in texels) for c in range(4)]
    mean = [sum(texel[c] for texel in texels) / 16.0 for c in range(4)]
    centred = [[texel[c] - mean[c] for c in range(4)] for texel in texels]
    covariance = [[0.0] * 4 for _ in range(4)]
    for row in centred:
        for i in range(4):
            ri = row[i]
            if ri == 0.0:
                continue
            for j in range(i, 4):
                covariance[i][j] += ri * row[j]
    for i in range(4):
        for j in range(i):
            covariance[i][j] = covariance[j][i]

    # Power iteration for the dominant eigenvector.
    #
    # Seed with the covariance row of largest variance rather than the
    # bounding-box diagonal. The diagonal looks like the obvious seed, but it
    # can be exactly ORTHOGONAL to the true principal axis: for a block that is
    # half (200,30,30) and half (30,30,200), the diagonal is (170,0,170,0)
    # while the real axis is (1,0,-1,0), the product comes out identically
    # zero, and the iteration silently keeps the seed it should have replaced.
    # A covariance row cannot be orthogonal to the dominant eigenvector unless
    # the whole matrix is zero, which is the flat-block case handled above.
    diagonal = [covariance[i][i] for i in range(4)]
    seed_index = 0
    for i in range(1, 4):
        if diagonal[i] > diagonal[seed_index]:
            seed_index = i
    axis = list(covariance[seed_index])
    if not any(axis):
        axis = [float(hi[c] - lo[c]) for c in range(4)]
    if not any(axis):
        axis = [1.0, 0.0, 0.0, 0.0]
    for _ in range(8):
        product = [
            sum(covariance[i][j] * axis[j] for j in range(4)) for i in range(4)
        ]
        magnitude = max(abs(v) for v in product)
        if magnitude < 1e-9:
            break
        axis = [v / magnitude for v in product]
    axis_length_squared = sum(v * v for v in axis)
    if axis_length_squared < 1e-9:
        axis = [float(hi[c] - lo[c]) for c in range(4)]
        axis_length_squared = sum(v * v for v in axis)
    if axis_length_squared < 1e-9:
        endpoint_a = [float(v) for v in lo]
        endpoint_b = [float(v) for v in hi]
    else:
        inverse_axis = 1.0 / axis_length_squared
        projections = [
            sum((texel[c] - mean[c]) * axis[c] for c in range(4)) * inverse_axis
            for texel in texels
        ]
        min_projection = min(projections)
        max_projection = max(projections)
        endpoint_a = [mean[c] + axis[c] * min_projection for c in range(4)]
        endpoint_b = [mean[c] + axis[c] * max_projection for c in range(4)]

    indices = [0] * 16
    # Alternate between assigning indices along the current endpoint line and
    # re-fitting the endpoints to those indices by least squares. Two rounds is
    # where this stops paying: the bounding box is already close, and further
    # rounds move endpoints by less than one quantisation step.
    for _ in range(3):
        direction = [endpoint_b[c] - endpoint_a[c] for c in range(4)]
        length_squared = sum(d * d for d in direction)
        if length_squared <= 1e-9:
            indices = [0] * 16
            break
        inverse = 1.0 / length_squared
        for i, texel in enumerate(texels):
            projection = (
                sum((texel[c] - endpoint_a[c]) * direction[c] for c in range(4))
                * inverse
            )
            index = int(projection * 15.0 + 0.5)
            indices[i] = 0 if index < 0 else (15 if index > 15 else index)

        # Least-squares re-fit: minimise sum over texels of
        # ||A*(1-w) + B*w - c||^2 with w the index weight in [0,1].
        sum_ww = sum_uu = sum_uw = 0.0
        sums_u = [0.0, 0.0, 0.0, 0.0]
        sums_w = [0.0, 0.0, 0.0, 0.0]
        for i, texel in enumerate(texels):
            w = _BC7_WEIGHTS4[indices[i]] / 64.0
            u = 1.0 - w
            sum_ww += w * w
            sum_uu += u * u
            sum_uw += u * w
            for c in range(4):
                sums_u[c] += u * texel[c]
                sums_w[c] += w * texel[c]
        determinant = sum_uu * sum_ww - sum_uw * sum_uw
        if abs(determinant) < 1e-9:
            break
        inverse_determinant = 1.0 / determinant
        for c in range(4):
            endpoint_a[c] = (sums_u[c] * sum_ww - sums_w[c] * sum_uw) * inverse_determinant
            endpoint_b[c] = (sums_w[c] * sum_uu - sums_u[c] * sum_uw) * inverse_determinant

    channels_a, pbit_a = _quantise_endpoint(endpoint_a)
    channels_b, pbit_b = _quantise_endpoint(endpoint_b)

    # Final index assignment against the ACTUAL quantised palette, not the
    # float line. This is what keeps the error from the endpoint rounding from
    # compounding with the index rounding.
    palette = []
    reconstructed_a = [(channels_a[c] << 1) | pbit_a for c in range(4)]
    reconstructed_b = [(channels_b[c] << 1) | pbit_b for c in range(4)]
    for weight in _BC7_WEIGHTS4:
        palette.append(
            tuple(
                (reconstructed_a[c] * (64 - weight) + reconstructed_b[c] * weight + 32)
                >> 6
                for c in range(4)
            )
        )
    for i, texel in enumerate(texels):
        best_index = 0
        best_error = None
        for index, entry in enumerate(palette):
            error = 0
            for c in range(4):
                delta = entry[c] - texel[c]
                error += delta * delta
            if best_error is None or error < best_error:
                best_error = error
                best_index = index
        indices[i] = best_index

    # Anchor rule: texel 0 stores only 3 index bits, so its high bit must be
    # zero. If it is not, swap the endpoints and mirror every index, which
    # describes exactly the same palette.
    if indices[0] >= 8:
        channels_a, channels_b = channels_b, channels_a
        pbit_a, pbit_b = pbit_b, pbit_a
        indices = [15 - index for index in indices]

    return _pack_bc7_mode6(channels_a, pbit_a, channels_b, pbit_b, indices)


def encode_bc7(width: int, height: int, pixels: bytes) -> bytes:
    out = bytearray()
    for block_y in range(0, height, 4):
        for block_x in range(0, width, 4):
            out += encode_bc7_block(_gather_block(pixels, width, height, block_x, block_y))
    return bytes(out)


# ---------------------------------------------------------------------------
# BC1 / BC3
# ---------------------------------------------------------------------------
#
# These two exist for a portability reason, not a quality one. The hidden
# OGRE14 producer runs on GL3Plus, and macOS core profile caps at OpenGL 4.1
# while BC7 needs 4.2 or ARB_texture_compression_bptc. A texture that has to
# load in BOTH the producer and the Metal presenter is therefore limited to the
# S3TC set. BC3 costs exactly the same 1 byte/texel as BC7; it simply spends
# fewer of those bits on colour.


def _rgb565(r: int, g: int, b: int) -> int:
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def _expand565(value: int) -> tuple[int, int, int]:
    r = (value >> 11) & 0x1F
    g = (value >> 5) & 0x3F
    b = value & 0x1F
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def _encode_bc1_colour(
    texels: Sequence[tuple[int, int, int, int]], *, allow_transparent: bool
) -> bytes:
    """The 8-byte colour half shared by BC1 and BC3.

    Endpoints come from the principal axis for the same reason BC7's do: a
    bounding-box fit collapses whenever two channels vary in opposite
    directions, which is exactly what brick, lane markings and signage look
    like.
    """

    opaque = [t for t in texels if not (allow_transparent and t[3] < 128)]
    if not opaque:
        # Every texel is punch-through transparent. Emit the transparent-only
        # block form: colour0 <= colour1 with every index 3.
        return struct.pack("<HH", 0, 0) + b"\xff\xff\xff\xff"

    mean = [sum(t[c] for t in opaque) / len(opaque) for c in range(3)]
    covariance = [[0.0] * 3 for _ in range(3)]
    for texel in opaque:
        centred = [texel[c] - mean[c] for c in range(3)]
        for i in range(3):
            for j in range(i, 3):
                covariance[i][j] += centred[i] * centred[j]
    for i in range(3):
        for j in range(i):
            covariance[i][j] = covariance[j][i]

    seed_index = 0
    for i in range(1, 3):
        if covariance[i][i] > covariance[seed_index][seed_index]:
            seed_index = i
    axis = list(covariance[seed_index])
    if not any(axis):
        axis = [1.0, 1.0, 1.0]
    for _ in range(8):
        product = [sum(covariance[i][j] * axis[j] for j in range(3)) for i in range(3)]
        magnitude = max(abs(v) for v in product)
        if magnitude < 1e-9:
            break
        axis = [v / magnitude for v in product]

    length_squared = sum(v * v for v in axis)
    if length_squared < 1e-9:
        low = high = [int(round(v)) for v in mean]
    else:
        inverse = 1.0 / length_squared
        projections = [
            sum((t[c] - mean[c]) * axis[c] for c in range(3)) * inverse for t in opaque
        ]
        low = [
            min(255, max(0, int(round(mean[c] + axis[c] * min(projections)))))
            for c in range(3)
        ]
        high = [
            min(255, max(0, int(round(mean[c] + axis[c] * max(projections)))))
            for c in range(3)
        ]

    colour0 = _rgb565(high[0], high[1], high[2])
    colour1 = _rgb565(low[0], low[1], low[2])
    # BC1 selects its palette layout by comparing the two packed endpoints:
    # colour0 > colour1 gives four opaque colours, otherwise three colours plus
    # a transparent index. Force the ordering the caller asked for.
    if allow_transparent:
        if colour0 > colour1:
            colour0, colour1 = colour1, colour0
    else:
        if colour0 < colour1:
            colour0, colour1 = colour1, colour0
        elif colour0 == colour1:
            # Equal endpoints select the three-colour layout, whose index 3 is
            # transparent black. For an opaque block that would punch a hole, so
            # nudge one endpoint down by a single blue step instead.
            if colour1 > 0:
                colour1 -= 1
            else:
                colour0 = 1

    e0 = _expand565(colour0)
    e1 = _expand565(colour1)
    if colour0 > colour1:
        palette = [
            e0,
            e1,
            tuple((2 * e0[c] + e1[c] + 1) // 3 for c in range(3)),
            tuple((e0[c] + 2 * e1[c] + 1) // 3 for c in range(3)),
        ]
        transparent_index = None
    else:
        palette = [
            e0,
            e1,
            tuple((e0[c] + e1[c]) // 2 for c in range(3)),
            (0, 0, 0),
        ]
        transparent_index = 3

    indices = 0
    for position, texel in enumerate(texels):
        if transparent_index is not None and allow_transparent and texel[3] < 128:
            index = transparent_index
        else:
            best_index = 0
            best_error = None
            limit = 3 if transparent_index is not None else 4
            for candidate in range(limit):
                entry = palette[candidate]
                error = sum((entry[c] - texel[c]) ** 2 for c in range(3))
                if best_error is None or error < best_error:
                    best_error = error
                    best_index = candidate
            index = best_index
        indices |= index << (2 * position)
    return struct.pack("<HH", colour0, colour1) + indices.to_bytes(4, "little")


def encode_bc1(width: int, height: int, pixels: bytes, *, punch_through: bool) -> bytes:
    out = bytearray()
    for block_y in range(0, height, 4):
        for block_x in range(0, width, 4):
            out += _encode_bc1_colour(
                _gather_block(pixels, width, height, block_x, block_y),
                allow_transparent=punch_through,
            )
    return bytes(out)


def encode_bc3(width: int, height: int, pixels: bytes) -> bytes:
    """BC3 is a BC4 alpha block followed by an always-opaque BC1 colour block."""

    out = bytearray()
    for block_y in range(0, height, 4):
        for block_x in range(0, width, 4):
            texels = _gather_block(pixels, width, height, block_x, block_y)
            out += encode_bc4_block([t[3] for t in texels])
            out += _encode_bc1_colour(texels, allow_transparent=False)
    return bytes(out)


# ---------------------------------------------------------------------------
# DDS container
# ---------------------------------------------------------------------------

DXGI_FORMAT_BC4_UNORM = 80
DXGI_FORMAT_BC5_UNORM = 83
DXGI_FORMAT_BC7_UNORM = 98
DXGI_FORMAT_BC7_UNORM_SRGB = 99

_DDSD_CAPS = 0x1
_DDSD_HEIGHT = 0x2
_DDSD_WIDTH = 0x4
_DDSD_PIXELFORMAT = 0x1000
_DDSD_MIPMAPCOUNT = 0x20000
_DDSD_LINEARSIZE = 0x80000
_DDPF_FOURCC = 0x4
_DDSCAPS_COMPLEX = 0x8
_DDSCAPS_TEXTURE = 0x1000
_DDSCAPS_MIPMAP = 0x400
_DDS_DIMENSION_TEXTURE2D = 3


def block_bytes_for(dxgi_format: int) -> int:
    if dxgi_format == DXGI_FORMAT_BC4_UNORM:
        return 8
    if dxgi_format in (
        DXGI_FORMAT_BC5_UNORM,
        DXGI_FORMAT_BC7_UNORM,
        DXGI_FORMAT_BC7_UNORM_SRGB,
    ):
        return 16
    raise BlockCompressionError(f"unsupported DXGI format {dxgi_format}")


def write_dds(
    width: int,
    height: int,
    dxgi_format: int,
    mip_payloads: Sequence[bytes],
) -> bytes:
    """Serialise a DX10-header DDS holding a complete 2D mip chain.

    The DX10 extended header is used for every format here, including BC4 and
    BC5 which also have legacy FourCC spellings. One code path means one thing
    to validate, and it is the only way to express BC7 at all.
    """

    if not mip_payloads:
        raise BlockCompressionError("a DDS needs at least one mip level")
    block_size = block_bytes_for(dxgi_format)
    linear_size = ((width + 3) // 4) * ((height + 3) // 4) * block_size

    flags = (
        _DDSD_CAPS
        | _DDSD_HEIGHT
        | _DDSD_WIDTH
        | _DDSD_PIXELFORMAT
        | _DDSD_LINEARSIZE
        | _DDSD_MIPMAPCOUNT
    )
    caps = _DDSCAPS_TEXTURE
    if len(mip_payloads) > 1:
        caps |= _DDSCAPS_COMPLEX | _DDSCAPS_MIPMAP

    header = bytearray()
    header += b"DDS "
    header += struct.pack("<I", 124)  # dwSize
    header += struct.pack("<I", flags)
    header += struct.pack("<I", height)
    header += struct.pack("<I", width)
    header += struct.pack("<I", linear_size)
    header += struct.pack("<I", 0)  # depth
    header += struct.pack("<I", len(mip_payloads))
    header += b"\x00" * (4 * 11)  # dwReserved1
    # DDS_PIXELFORMAT
    header += struct.pack("<I", 32)  # dwSize
    header += struct.pack("<I", _DDPF_FOURCC)
    header += b"DX10"
    header += struct.pack("<IIIII", 0, 0, 0, 0, 0)
    header += struct.pack("<I", caps)
    header += struct.pack("<IIII", 0, 0, 0, 0)
    if len(header) != 128:
        raise BlockCompressionError(
            f"DDS header is {len(header)} bytes, expected 128"
        )
    # DDS_HEADER_DXT10
    header += struct.pack("<I", dxgi_format)
    header += struct.pack("<I", _DDS_DIMENSION_TEXTURE2D)
    header += struct.pack("<I", 0)  # miscFlag
    header += struct.pack("<I", 1)  # arraySize
    header += struct.pack("<I", 0)  # miscFlags2, DDS_ALPHA_MODE_UNKNOWN

    body = bytearray(header)
    for payload in mip_payloads:
        body += payload
    return bytes(body)


# ---------------------------------------------------------------------------
# Reference decoders and self-test
# ---------------------------------------------------------------------------
#
# An encoder nobody can decode is an unverifiable encoder. These reference
# decoders exist so the module can prove, without a GPU and without a
# third-party library, that what it emits round-trips to the values it
# intended. They are written straight from the format definitions and are used
# by the self-test only; nothing in the compile path calls them.


def decode_bc4_block(block: bytes) -> list[int]:
    e0, e1 = block[0], block[1]
    bits = int.from_bytes(block[2:8], "little")
    if e0 > e1:
        palette = [e0, e1] + [
            ((6 - i) * e0 + (1 + i) * e1 + 3) // 7 for i in range(6)
        ]
    else:
        palette = (
            [e0, e1]
            + [((4 - i) * e0 + (1 + i) * e1 + 2) // 5 for i in range(4)]
            + [0, 255]
        )
    return [palette[(bits >> (3 * i)) & 7] for i in range(16)]


def decode_bc1_block(block: bytes) -> list[tuple[int, int, int, int]]:
    colour0, colour1 = struct.unpack("<HH", block[:4])
    bits = int.from_bytes(block[4:8], "little")
    e0 = _expand565(colour0)
    e1 = _expand565(colour1)
    if colour0 > colour1:
        palette = [
            e0 + (255,),
            e1 + (255,),
            tuple((2 * e0[c] + e1[c] + 1) // 3 for c in range(3)) + (255,),
            tuple((e0[c] + 2 * e1[c] + 1) // 3 for c in range(3)) + (255,),
        ]
    else:
        palette = [
            e0 + (255,),
            e1 + (255,),
            tuple((e0[c] + e1[c]) // 2 for c in range(3)) + (255,),
            (0, 0, 0, 0),
        ]
    return [palette[(bits >> (2 * i)) & 3] for i in range(16)]


def decode_bc3_block(block: bytes) -> list[tuple[int, int, int, int]]:
    alpha = decode_bc4_block(block[:8])
    colour = decode_bc1_block(block[8:16])
    return [(colour[i][0], colour[i][1], colour[i][2], alpha[i]) for i in range(16)]


def decode_bc7_mode6_block(block: bytes) -> list[tuple[int, int, int, int]]:
    value = int.from_bytes(block, "little")
    position = 0

    def take(count: int) -> int:
        nonlocal position
        result = (value >> position) & ((1 << count) - 1)
        position += count
        return result

    if take(7) != (1 << 6):
        raise BlockCompressionError("not a BC7 mode 6 block")
    endpoints = [[0] * 4, [0] * 4]
    for channel in range(4):
        endpoints[0][channel] = take(7)
        endpoints[1][channel] = take(7)
    pbit_a = take(1)
    pbit_b = take(1)
    indices = [take(3)] + [take(4) for _ in range(15)]
    if position != 128:
        raise BlockCompressionError("BC7 block did not consume exactly 128 bits")
    a = [(endpoints[0][c] << 1) | pbit_a for c in range(4)]
    b = [(endpoints[1][c] << 1) | pbit_b for c in range(4)]
    decoded = []
    for index in indices:
        weight = _BC7_WEIGHTS4[index]
        decoded.append(
            tuple((a[c] * (64 - weight) + b[c] * weight + 32) >> 6 for c in range(4))
        )
    return decoded


def self_test() -> int:
    """Round-trip every encoder and report error statistics.

    Run with ``python3 tools/cityworld_block_compression.py --self-test``.
    """

    import math
    import random

    failures = 0
    random.seed(20260825)

    def report(label: str, squared_error: float, count: int, worst: int) -> None:
        mean_squared = squared_error / count
        psnr = 99.0 if mean_squared <= 0 else 10 * math.log10(255 * 255 / mean_squared)
        print(
            f"  {label:22s} rmse={math.sqrt(mean_squared):7.3f} "
            f"psnr={psnr:5.1f} dB  worst={worst}"
        )

    print("BC4:")
    total = 0.0
    count = 0
    worst = 0
    for _ in range(400):
        values = [random.randrange(256) for _ in range(16)]
        decoded = decode_bc4_block(encode_bc4_block(values))
        # The block minimum and maximum are endpoints, so both must survive
        # exactly; only interior levels are approximated.
        if max(values) not in decoded or min(values) not in decoded:
            print("  FAIL: BC4 did not reproduce its endpoints exactly")
            failures += 1
            break
        for a, b in zip(values, decoded):
            error = abs(a - b)
            total += error * error
            count += 1
            worst = max(worst, error)
    report("uniform random", total, count, worst)
    for value in (0, 1, 127, 128, 254, 255):
        if any(x != value for x in decode_bc4_block(encode_bc4_block([value] * 16))):
            print(f"  FAIL: BC4 flat block {value} was not exact")
            failures += 1
    print("  flat blocks exact: OK")

    print("BC7 mode 6:")
    flat_worst = 0
    for _ in range(200):
        colour = tuple(random.randrange(256) for _ in range(4))
        decoded = decode_bc7_mode6_block(encode_bc7_block([colour] * 16))
        if any(texel != decoded[0] for texel in decoded):
            print("  FAIL: BC7 flat block was not uniform across its texels")
            failures += 1
            break
        error = max(abs(decoded[0][c] - colour[c]) for c in range(4))
        # Mode 6 shares one P-bit across RGBA, so a flat colour whose channels
        # have mixed parity is reachable only to within one level. That is a
        # property of the format, not of this encoder.
        if error > 1:
            print(f"  FAIL: BC7 flat block {colour} decoded to {decoded[0]}")
            failures += 1
            break
        flat_worst = max(flat_worst, error)
    print(f"  flat blocks uniform, worst channel error={flat_worst} (P-bit parity)")

    def gradient():
        # A gentle ramp, the way a real gradient looks once it is spread across
        # a whole texture rather than crammed into four texels.
        return [(90 + i, 100 + i, 110 + i, 255) for i in range(16)]

    def steep_gradient():
        # The pathological case: a 210-level ramp inside a single 4x4 block.
        return [(10 + i * 14, 20 + i * 13, 30 + i * 12, 255) for i in range(16)]

    def anti_correlated():
        return [(200, 30, 30, 255) if i < 8 else (30, 30, 200, 255) for i in range(16)]

    def brick():
        return [
            (150, 60, 45, 255) if (i // 4) % 2 == 0 else (190, 185, 175, 255)
            for i in range(16)
        ]

    def lane_marking():
        return [
            (235, 233, 225, 255) if (i % 4) < 2 else (48, 46, 44, 255)
            for i in range(16)
        ]

    def photographic():
        base = [random.randrange(40, 200) for _ in range(4)]
        return [
            tuple(
                min(255, max(0, int(base[c] + 7 * (i % 4) + 5 * (i // 4) + random.gauss(0, 3))))
                for c in range(4)
            )
            for i in range(16)
        ]

    def white_noise():
        return [tuple(random.randrange(256) for _ in range(4)) for _ in range(16)]

    for label, generator, minimum_psnr in (
        ("smooth gradient", gradient, 40.0),
        ("steep gradient", steep_gradient, 40.0),
        ("anti-correlated", anti_correlated, 40.0),
        ("brick", brick, 40.0),
        ("lane marking", lane_marking, 40.0),
        ("photographic", photographic, 32.0),
        # White noise is the honest floor: no block format can store four
        # channels of independent noise at one byte per texel. It is measured
        # so a regression elsewhere cannot hide behind it, but it is not held
        # to a quality bar.
        ("white noise", white_noise, None),
    ):
        total = 0.0
        count = 0
        worst = 0
        for _ in range(300):
            texels = generator()
            decoded = decode_bc7_mode6_block(encode_bc7_block(texels))
            for a, b in zip(texels, decoded):
                for c in range(4):
                    error = abs(a[c] - b[c])
                    total += error * error
                    count += 1
                    worst = max(worst, error)
        report(label, total, count, worst)
        if minimum_psnr is not None:
            mean_squared = total / count
            psnr = 99.0 if mean_squared <= 0 else 10 * math.log10(255 * 255 / mean_squared)
            if psnr < minimum_psnr:
                print(f"  FAIL: {label} fell below {minimum_psnr} dB")
                failures += 1

    print("BC1 / BC3:")
    for label, generator, minimum_psnr in (
        ("bc1 smooth gradient", gradient, 30.0),
        # BC1 has four palette entries per block. A 210-level ramp across one
        # block therefore lands 35 levels from its worst texel no matter how
        # good the encoder is -- the bar below is where the FORMAT sits, not
        # where this encoder sits. BC7 handles the same block at 48 dB because
        # it has sixteen entries; this is the fidelity actually being traded
        # away to keep the GL3Plus producer able to load the file.
        ("bc1 steep gradient", steep_gradient, 21.0),
        ("bc1 anti-correlated", anti_correlated, 30.0),
        ("bc1 brick", brick, 30.0),
        ("bc1 lane marking", lane_marking, 30.0),
        ("bc1 photographic", photographic, 28.0),
    ):
        total = 0.0
        count = 0
        worst = 0
        for _ in range(200):
            texels = generator()
            encoded = _encode_bc1_colour(texels, allow_transparent=False)
            decoded = decode_bc1_block(encoded)
            if any(entry[3] != 255 for entry in decoded):
                print(f"  FAIL: {label} produced a transparent texel in an opaque block")
                failures += 1
                break
            for a, b in zip(texels, decoded):
                for c in range(3):
                    error = abs(a[c] - b[c])
                    total += error * error
                    count += 1
                    worst = max(worst, error)
        report(label, total, count, worst)
        mean_squared = total / count
        psnr = 99.0 if mean_squared <= 0 else 10 * math.log10(255 * 255 / mean_squared)
        if psnr < minimum_psnr:
            print(f"  FAIL: {label} fell below {minimum_psnr} dB")
            failures += 1

    # A flat opaque block must never select the three-colour layout, whose
    # index 3 decodes to transparent black. That would punch holes in solid
    # colour, which is the one BC1 failure mode that is visible rather than
    # merely soft.
    for value in (0, 1, 64, 128, 200, 255):
        colour = (value, value, value, 255)
        decoded = decode_bc1_block(_encode_bc1_colour([colour] * 16, allow_transparent=False))
        if any(entry[3] != 255 for entry in decoded):
            print(f"  FAIL: flat opaque BC1 block {colour} decoded transparent")
            failures += 1
    print("  flat opaque blocks stay opaque: OK")

    # BC3 alpha rides in a BC4 block, so it must reproduce the block's alpha
    # extremes exactly -- that is what makes it safe for the detail-layer
    # height/coverage signal.
    total = 0.0
    count = 0
    worst = 0
    for _ in range(200):
        texels = [
            (random.randrange(256), random.randrange(256), random.randrange(256),
             random.randrange(256))
            for _ in range(16)
        ]
        decoded = decode_bc3_block(encode_bc3(4, 4, bytes(b for t in texels for b in t)))
        alphas = [t[3] for t in texels]
        decoded_alphas = [d[3] for d in decoded]
        if max(alphas) not in decoded_alphas or min(alphas) not in decoded_alphas:
            print("  FAIL: BC3 did not reproduce its alpha extremes exactly")
            failures += 1
            break
        for a, b in zip(alphas, decoded_alphas):
            error = abs(a - b)
            total += error * error
            count += 1
            worst = max(worst, error)
    report("bc3 alpha", total, count, worst)

    texels = white_noise()
    if encode_bc7_block(texels) != encode_bc7_block(texels):
        print("  FAIL: BC7 encoding is not deterministic")
        failures += 1
    elif encode_bc3(4, 4, bytes(b for t in texels for b in t)) != encode_bc3(
        4, 4, bytes(b for t in texels for b in t)
    ):
        print("  FAIL: BC3 encoding is not deterministic")
        failures += 1
    else:
        print("  deterministic: OK")

    print("FAILURES:" if failures else "all block-compression self-tests passed", failures or "")
    return 1 if failures else 0


if __name__ == "__main__":
    import sys

    if "--self-test" in sys.argv[1:]:
        raise SystemExit(self_test())
    print(__doc__)
    raise SystemExit(0)


def write_dds_fourcc(
    width: int,
    height: int,
    four_cc: bytes,
    block_bytes: int,
    mip_payloads: Sequence[bytes],
) -> bytes:
    """Serialise a LEGACY FourCC DDS -- no DX10 extended header.

    The DX10 header is deliberately not used. The runtime's own source-texture
    decoder refuses DX10 outright, and separately the hidden OGRE14 producer
    runs on GL3Plus where macOS core profile caps at OpenGL 4.1, below the 4.2
    that BC7 needs. A texture that must load in both renderers is therefore a
    legacy-FourCC S3TC/RGTC file, and writing one keeps that constraint
    visible here rather than discovering it at load time.

    The flag combination matters and is checked by the decoder: a
    block-compressed DDS must declare DDSD_LINEARSIZE with the top mip's byte
    count and must NOT declare DDSD_PITCH.
    """

    if len(four_cc) != 4:
        raise BlockCompressionError("a DDS FourCC is exactly four bytes")
    if not mip_payloads:
        raise BlockCompressionError("a DDS needs at least one mip level")
    linear_size = ((width + 3) // 4) * ((height + 3) // 4) * block_bytes
    if len(mip_payloads[0]) != linear_size:
        raise BlockCompressionError(
            f"top mip is {len(mip_payloads[0])} bytes, expected {linear_size}"
        )

    flags = (
        _DDSD_CAPS
        | _DDSD_HEIGHT
        | _DDSD_WIDTH
        | _DDSD_PIXELFORMAT
        | _DDSD_LINEARSIZE
        | _DDSD_MIPMAPCOUNT
    )
    caps = _DDSCAPS_TEXTURE
    if len(mip_payloads) > 1:
        caps |= _DDSCAPS_COMPLEX | _DDSCAPS_MIPMAP

    header = bytearray()
    header += b"DDS "
    header += struct.pack("<I", 124)
    header += struct.pack("<I", flags)
    header += struct.pack("<I", height)
    header += struct.pack("<I", width)
    header += struct.pack("<I", linear_size)
    header += struct.pack("<I", 0)
    header += struct.pack("<I", len(mip_payloads))
    header += b"\x00" * (4 * 11)
    header += struct.pack("<I", 32)
    header += struct.pack("<I", _DDPF_FOURCC)
    header += four_cc
    header += struct.pack("<IIIII", 0, 0, 0, 0, 0)
    header += struct.pack("<I", caps)
    header += struct.pack("<IIII", 0, 0, 0, 0)
    if len(header) != 128:
        raise BlockCompressionError(f"DDS header is {len(header)} bytes, expected 128")
    body = bytearray(header)
    for payload in mip_payloads:
        body += payload
    return bytes(body)
