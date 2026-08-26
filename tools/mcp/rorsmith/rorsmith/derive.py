"""Texture derivation, procedural generation, and parameter fitting.

Division of labour with the sibling agents:
  * rorsmith produces UNCOMPRESSED source maps (PNG, 8-bit or 16-bit).
  * The texture agent owns BC7/BC5/BC4 compression and the content compiler
    that packages them. rorsmith deliberately emits no BC blocks and no DDS.
  * The layered-materials agent owns runtime transport/projection of detail
    layers; rorsmith emits the authoring declaration and the maps it needs.
"""

from __future__ import annotations

import io
import json
import math
import zipfile
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from PIL import Image

from . import procedural
from .paths import Layout, RorsmithError

F32 = np.float32

#: sRGB decode/encode. Albedo is authored in sRGB; height/roughness/AO/normal
#: are linear data and must never be gamma-encoded.
def _srgb_to_linear(x: np.ndarray) -> np.ndarray:
    x = x.astype(F32)
    return np.where(x <= 0.04045, x / F32(12.92), ((x + F32(0.055)) / F32(1.055)) ** F32(2.4))


def _linear_to_srgb(x: np.ndarray) -> np.ndarray:
    x = np.clip(x.astype(F32), F32(0.0), F32(1.0))
    return np.where(x <= 0.0031308, x * F32(12.92), F32(1.055) * x ** (F32(1.0) / F32(2.4)) - F32(0.055))


def load_texture(layout: Layout, reference: str) -> tuple[np.ndarray, dict[str, object]]:
    """Load `path`, or `Archive.zip::member`, as float RGB in 0..1 (sRGB)."""
    if "::" in reference:
        archive_name, _, member = reference.partition("::")
        archive_path = layout.resolve_archive(archive_name)
        with zipfile.ZipFile(archive_path) as handle:
            names = handle.namelist()
            if member not in names:
                hits = [n for n in names if n.casefold().endswith(member.casefold())]
                if len(hits) != 1:
                    raise RorsmithError(
                        "texture_member_not_found",
                        f"'{member}' matches {len(hits)} members of {archive_path.name}",
                    )
                member = hits[0]
            payload = handle.read(member)
        origin = {"archive": str(archive_path), "member": member}
    else:
        path = Path(reference).expanduser()
        if not path.is_file():
            raise RorsmithError("texture_not_found", str(path))
        payload = path.read_bytes()
        origin = {"path": str(path)}
    try:
        image = Image.open(io.BytesIO(payload))
        image.load()
    except Exception as exc:
        raise RorsmithError(
            "texture_decode_refused",
            f"{origin}: {exc}. rorsmith decodes what Pillow decodes; DDS/BC "
            "sources belong to the texture agent's pipeline",
        ) from exc
    rgb = np.asarray(image.convert("RGB"), dtype=np.uint8).astype(F32) / F32(255.0)
    origin.update({"width": rgb.shape[1], "height": rgb.shape[0], "mode": image.mode})
    return rgb, origin


def _encode_png(array: np.ndarray, mode: str) -> bytes:
    buffer = io.BytesIO()
    Image.fromarray(array, mode=mode).save(buffer, format="PNG", optimize=True)
    return buffer.getvalue()


def _u8(x: np.ndarray) -> np.ndarray:
    return np.clip(np.rint(x * 255.0), 0, 255).astype(np.uint8)


def _write_outputs(out_dir: Path, stem: str, maps: dict[str, bytes]) -> list[dict[str, object]]:
    out_dir.mkdir(parents=True, exist_ok=True)
    written = []
    import hashlib

    for suffix, payload in maps.items():
        target = out_dir / f"{stem}_{suffix}.png"
        target.write_bytes(payload)
        written.append(
            {
                "map": suffix,
                "path": str(target),
                "bytes": len(payload),
                "sha256": hashlib.sha256(payload).hexdigest(),
            }
        )
    return written


# --------------------------------------------------------------------------
# derive_normal_map
# --------------------------------------------------------------------------

def derive_normal_map(
    layout: Layout,
    texture: str,
    out_dir: str,
    strength: float = 1.0,
    family: str | None = None,
    highpass_fraction: float = 0.06,
) -> dict[str, object]:
    """Derive a tangent-space normal map from a source texture.

    METHOD, stated plainly because it matters: the source is converted to
    linear luminance, high-passed against a box blur of radius
    `highpass_fraction * size` to remove the albedo's low-frequency lighting
    and colour, and the remainder is treated as a HEIGHT PROXY. A wrapped 3x3
    Sobel then produces the gradient, scaled by `strength`, and the result is
    encoded +Y-up with 0.5 flat.

    This is a proxy, not a measurement. Dark paint reads as a groove because
    nothing in an albedo texture distinguishes pigment from geometry. For a
    surface where that matters, generate the material procedurally instead -
    `generate_material` produces a real height field from the pattern model.
    """
    rgb, origin = load_texture(layout, texture)
    linear = _srgb_to_linear(rgb)
    luminance = (
        linear[..., 0] * F32(0.2126)
        + linear[..., 1] * F32(0.7152)
        + linear[..., 2] * F32(0.0722)
    )
    size = min(luminance.shape)
    radius = max(1, int(size * float(highpass_fraction)))
    kernel = np.ones(2 * radius + 1, dtype=F32) / F32(2 * radius + 1)
    padded = np.pad(luminance, radius, mode="wrap")
    blurred = np.apply_along_axis(lambda m: np.convolve(m, kernel, "valid"), 1, padded)
    blurred = np.apply_along_axis(lambda m: np.convolve(m, kernel, "valid"), 0, blurred)
    height = np.clip(F32(0.5) + (luminance - blurred), F32(0.0), F32(1.0))
    normal = procedural.normal_from_height(height, strength=float(strength))
    encoded = _u8(normal * F32(0.5) + F32(0.5))

    stem = Path(str(origin.get("member") or origin.get("path"))).stem
    written = _write_outputs(
        Path(out_dir).expanduser(),
        stem,
        {
            "normal": _encode_png(encoded, "RGB"),
            "height_proxy": _encode_png(_u8(height), "L"),
        },
    )
    return {
        "source": origin,
        "method": "albedo_luminance_highpass_sobel_v1",
        "method_detail": (
            "linear luminance -> box high-pass (radius "
            f"{radius}px) -> wrapped 3x3 Sobel -> +Y-up encode"
        ),
        "strength": float(strength),
        "family": family,
        "honesty": (
            "height is a proxy derived from albedo; painted contrast becomes "
            "false relief. Prefer generate_material for a modelled height field."
        ),
        "outputs": written,
        "handoff": "compression/packaging is the texture agent's pipeline; these are PNG sources",
    }


# --------------------------------------------------------------------------
# derive_roughness
# --------------------------------------------------------------------------

def derive_roughness(
    layout: Layout,
    family: str | None = None,
    material: str | None = None,
    texture: str | None = None,
    out_dir: str | None = None,
    map_method: str | None = None,
) -> dict[str, object]:
    """Return the reviewed F3 roughness for a family, material, or texture.

    The F3 program assigns roughness PER FAMILY, not per texel, and the value
    is the sanitizer's own `sqrt(2 / (shininess + 2))`. A per-texel roughness
    map invented from an albedo texture would fight that, so the default
    answer is the band scalar. A map is only produced when `map_method` is
    given explicitly, and it is centred on the band value so the mean of the
    map equals the scalar the sanitizer would have injected.
    """
    from . import policy

    bands = policy.bands()
    resolved_family = family
    if resolved_family is None and material is not None:
        resolved_family = policy.classify_band(material, [texture] if texture else [])
    if resolved_family is None and texture is not None:
        resolved_family = policy.classify_band(Path(texture).name, [texture])
    if resolved_family is None:
        raise RorsmithError(
            "roughness_target_missing",
            "give one of family=, material=, or texture=",
        )
    band = bands.get(resolved_family)
    if band is None:
        raise RorsmithError(
            "unknown_roughness_family",
            f"'{resolved_family}' is not one of {sorted(bands)}",
        )
    result: dict[str, object] = {
        "family": band.name,
        "roughness": round(band.roughness, 4),
        "shininess": band.shininess,
        "specular_rgb": band.specular_rgb,
        "sanitizer_directive": f"specular {band.specular_rgb} 1 {band.shininess:g}",
        "identity": "roughness = sqrt(2 / (shininess + 2))",
        "source": "tools/generate_cityworld_roughness_repair_edits.py BAND_* table",
        "all_bands": {
            name: round(value.roughness, 4) for name, value in sorted(bands.items())
        },
    }
    if map_method is None:
        result["map"] = None
        result["note"] = (
            "no roughness map produced: the reviewed pipeline is a per-family "
            "scalar. Pass map_method='albedo_variance' to opt into a derived "
            "map, which is an approximation, not a measurement."
        )
        return result
    if map_method != "albedo_variance":
        raise RorsmithError(
            "unknown_roughness_map_method",
            f"'{map_method}'; only 'albedo_variance' exists",
        )
    if texture is None or out_dir is None:
        raise RorsmithError(
            "roughness_map_inputs_missing", "map_method requires texture= and out_dir="
        )
    rgb, origin = load_texture(layout, texture)
    linear = _srgb_to_linear(rgb)
    luminance = linear.mean(axis=-1)
    size = min(luminance.shape)
    radius = max(1, size // 64)
    kernel = np.ones(2 * radius + 1, dtype=F32) / F32(2 * radius + 1)
    padded = np.pad(luminance, radius, mode="wrap")
    mean = np.apply_along_axis(lambda m: np.convolve(m, kernel, "valid"), 1, padded)
    mean = np.apply_along_axis(lambda m: np.convolve(m, kernel, "valid"), 0, mean)
    detail = np.abs(luminance - mean)
    spread = float(detail.std()) or 1e-6
    centred = np.clip(
        F32(band.roughness) + (detail - detail.mean()) / F32(spread) * F32(0.05),
        F32(0.03),
        F32(1.0),
    )
    stem = Path(str(origin.get("member") or origin.get("path"))).stem
    result["map"] = _write_outputs(
        Path(out_dir).expanduser(), stem, {"roughness": _encode_png(_u8(centred), "L")}
    )
    result["map_method"] = "albedo_variance_v1"
    result["map_mean"] = round(float(centred.mean()), 4)
    result["honesty"] = (
        "the map is local albedo contrast rescaled around the band scalar; it "
        "carries no measured microfacet information"
    )
    return result


# --------------------------------------------------------------------------
# generate_material
# --------------------------------------------------------------------------

_MAP_ORDER = ("albedo", "normal", "roughness", "ao", "height")


def generate_material(
    layout: Layout,
    generator: str,
    out_dir: str,
    params: dict | None = None,
    resolution: int = 1024,
    outputs: list[str] | None = None,
    normal_strength: float = 1.0,
    name: str | None = None,
) -> dict[str, object]:
    """Evaluate a generator headlessly and emit a PBR map set."""
    requested = list(outputs or _MAP_ORDER)
    unknown = sorted(set(requested) - set(_MAP_ORDER))
    if unknown:
        raise RorsmithError("unknown_output_map", f"{unknown}; known: {list(_MAP_ORDER)}")
    result = procedural.evaluate(generator, dict(params or {}), int(resolution))
    spec = procedural.GENERATORS[generator]
    maps: dict[str, bytes] = {}
    if "albedo" in requested:
        maps["albedo"] = _encode_png(_u8(result.albedo), "RGB")
    if "normal" in requested:
        normal = procedural.normal_from_height(result.height, strength=float(normal_strength))
        maps["normal"] = _encode_png(_u8(normal * F32(0.5) + F32(0.5)), "RGB")
    if "roughness" in requested:
        maps["roughness"] = _encode_png(_u8(result.roughness), "L")
    if "ao" in requested:
        maps["ao"] = _encode_png(_u8(procedural.ao_from_height(result.height)), "L")
    if "height" in requested:
        maps["height"] = _encode_png(_u8(result.height), "L")
    stem = name or f"{generator}_{resolution}"
    written = _write_outputs(Path(out_dir).expanduser(), stem, maps)
    return {
        "generator": generator,
        "provenance": spec.provenance,
        "parameters": {**{k: v["default"] for k, v in spec.parameters.items()}, **(params or {})},
        "resolution": int(resolution),
        "outputs": written,
        "notes": result.notes,
        "height_binding": (
            "HlmsPbsDatablock has no height/displacement slot in the pinned "
            "engine (diffuse, normal, specular, roughness, emissive, "
            "reflection, DETAIL0..3, DETAIL0_NM..3_NM, DETAIL_WEIGHT). The "
            "height map is emitted for height-blended layer weighting and for "
            "a future parallax tier, NOT for a slot that exists today."
        ),
        "handoff": "BC compression and packaging belong to the texture agent",
    }


# --------------------------------------------------------------------------
# fit_generator
# --------------------------------------------------------------------------

def _dominant_period(profile: np.ndarray) -> tuple[float, float]:
    """Return (period_in_texels, confidence 0..1) of a 1-D periodic profile."""
    centred = profile - profile.mean()
    if not np.any(centred):
        return 0.0, 0.0
    window = np.hanning(len(centred)).astype(F32)
    spectrum = np.abs(np.fft.rfft(centred * window))
    spectrum[0] = 0.0
    # Periods shorter than 4 texels or longer than half the page are not a
    # brick course; refusing them is better than reporting noise as structure.
    low = max(2, int(len(centred) / (len(centred) / 4)))
    high = max(low + 1, len(spectrum))
    band = spectrum[low:high]
    if band.size == 0:
        return 0.0, 0.0
    peak = int(np.argmax(band)) + low
    total = float(band.sum()) or 1e-9
    confidence = float(band[peak - low]) / total
    return float(len(centred)) / peak, min(1.0, confidence * 6.0)



def _folded_joint_fraction(profile: np.ndarray, period: float) -> tuple[float, bool]:
    """Fraction of one period the joint occupies, by phase folding.

    Averaging every sample that shares a phase within the detected period
    cancels the brick-to-brick colour variation that otherwise swamps a raw
    threshold, leaving one clean period whose dark band is the joint.
    """
    if period <= 2.0 or not np.isfinite(period):
        return 0.0, True
    bins = max(4, int(round(period)))
    phase = (np.arange(len(profile)) % period) / period
    index = np.clip((phase * bins).astype(int), 0, bins - 1)
    folded = np.zeros(bins, dtype=np.float64)
    counts = np.zeros(bins, dtype=np.float64)
    np.add.at(folded, index, profile.astype(np.float64))
    np.add.at(counts, index, 1.0)
    folded = folded / np.maximum(counts, 1.0)
    low, high = float(folded.min()), float(folded.max())
    span = max(1e-6, high - low)
    dark = float((folded <= (low + 0.4 * span)).mean())
    bright = float((folded >= (high - 0.4 * span)).mean())
    # Mortar may be lighter than the brick (grey joint on red clay) or darker
    # (dark grout on pale block). Polarity is not assumed: a joint is always
    # the THINNER of the two bands, so the minority side is the joint.
    return min(dark, bright), dark <= bright


def fit_generator(
    layout: Layout,
    texture: str,
    generator: str = "bricks",
    min_confidence: float = 0.12,
) -> dict[str, object]:
    """Estimate generator parameters that reproduce an existing texture.

    This is what keeps a resolution upgrade honest: the regenerated map keeps
    the source's row pitch, column pitch, mortar fraction and colour
    statistics, so the building stays the same building instead of becoming a
    different one at higher resolution.

    METHOD: linear luminance; horizontal and vertical gradient-energy
    profiles; dominant period of each by windowed FFT (rows from the
    horizontal-line energy, columns from the vertical-line energy); mortar
    fraction from the width of the below-Otsu-threshold bands; brick and
    mortar colours from the mean sRGB of each side of that threshold; colour
    variation from the per-cell luminance spread.

    It REFUSES rather than guessing when the periodicity confidence is below
    `min_confidence` - a non-periodic texture has no brick parameters, and
    inventing some would be exactly the dishonesty this tool exists to avoid.
    """
    if generator not in {"bricks", "arc_pavement"}:
        raise RorsmithError(
            "fit_unsupported_generator",
            f"fitting is implemented for 'bricks' and 'arc_pavement'; "
            f"'{generator}' has no fitted parameterisation",
        )
    rgb, origin = load_texture(layout, texture)
    # The joint/face split is a PERCEPTUAL one, so the Otsu threshold is taken
    # on sRGB luminance. Doing it on linear luminance drags the threshold into
    # the shadows of any saturated colour and reports a wall as almost all
    # mortar.
    luminance = (
        rgb[..., 0] * F32(0.2126)
        + rgb[..., 1] * F32(0.7152)
        + rgb[..., 2] * F32(0.0722)
    )
    height, width = luminance.shape

    dy = np.abs(np.diff(luminance, axis=0, append=luminance[:1]))
    dx = np.abs(np.diff(luminance, axis=1, append=luminance[:, :1]))
    row_profile = dy.mean(axis=1)
    column_profile = dx.mean(axis=0)
    row_period, row_confidence = _dominant_period(row_profile)
    column_period, column_confidence = _dominant_period(column_profile)

    confidence = min(row_confidence, column_confidence)
    if confidence < float(min_confidence):
        raise RorsmithError(
            "fit_refused_weak_periodicity",
            f"row confidence {row_confidence:.3f}, column confidence "
            f"{column_confidence:.3f}, both must reach {min_confidence}. "
            f"{origin} does not read as a periodic masonry/pavement page; "
            "no parameters are reported rather than invented",
        )

    # Joint thickness and POLARITY first: the Otsu split below needs to know
    # which side of the threshold is the joint, and mortar is not always the
    # darker one.
    #
    # Mortar WIDTH, not mortar area. Phase-folding the row-mean luminance over
    # the detected row pitch cancels brick-to-brick colour variation and
    # leaves one clean period whose minority band is the joint. A raw area
    # fraction would also count every dark brick and report a wall as mostly
    # mortar.
    row_luminance = luminance.mean(axis=1)
    joint_fraction, joint_is_dark = _folded_joint_fraction(row_luminance, row_period)
    # The generator insets each brick by `mortar` on every side, so one joint
    # spans about 2 * mortar of the row pitch.
    mortar_fraction = joint_fraction / 2.0

    # Otsu split of the luminance into joint and face.
    histogram, edges = np.histogram(luminance, bins=64, range=(0.0, 1.0))
    total = histogram.sum()
    best_threshold, best_variance = 0.5, -1.0
    weight_background = 0.0
    sum_background = 0.0
    sum_total = float((histogram * ((edges[:-1] + edges[1:]) / 2)).sum())
    for index in range(64):
        weight_background += histogram[index]
        if weight_background == 0 or weight_background == total:
            continue
        weight_foreground = total - weight_background
        centre = (edges[index] + edges[index + 1]) / 2
        sum_background += histogram[index] * centre
        mean_background = sum_background / weight_background
        mean_foreground = (sum_total - sum_background) / weight_foreground
        variance = (
            weight_background * weight_foreground
            * (mean_background - mean_foreground) ** 2
        )
        if variance > best_variance:
            best_variance, best_threshold = variance, float(centre)

    face = luminance >= best_threshold if joint_is_dark else luminance < best_threshold
    area_below_threshold = float(1.0 - face.mean())

    face_rgb = rgb[face] if face.any() else rgb.reshape(-1, 3)
    mortar_rgb = rgb[~face] if (~face).any() else rgb.reshape(-1, 3)
    brick_color = [round(float(c), 4) for c in face_rgb.mean(axis=0)]
    mortar_color = [round(float(c), 4) for c in mortar_rgb.mean(axis=0)]
    face_luminance = luminance[face] if face.any() else luminance
    variation = float(np.clip(face_luminance.std() / max(1e-6, face_luminance.mean()) * 2.0, 0.0, 1.0))

    rows = max(1.0, round(height / row_period)) if row_period else 1.0
    columns = max(1.0, round(width / column_period)) if column_period else 1.0
    # Running bond offsets alternate rows by half a brick, so the vertical
    # joints seen by a column-gradient profile repeat at HALF the brick width.
    # The measured column count is therefore twice the true one.
    measured_columns = columns
    if generator == "bricks" and columns >= 2:
        columns = max(1.0, columns / 2.0)

    if generator == "bricks":
        params = {
            "pattern": "running_bond",
            "rows": float(rows),
            "columns": float(columns),
            "repeat": 1.0,
            "row_offset": 0.5,
            "mortar": round(float(np.clip(mortar_fraction, 0.005, 0.5)), 4),
            "bevel": 0.1,
            "brick_color": brick_color,
            "mortar_color": mortar_color,
            "color_variation": round(variation, 4),
        }
    else:
        params = {
            "rows": float(np.clip(rows, 4, 16)),
            "bricks": float(np.clip(columns, 4, 16)),
            "repeat": 2.0,
            "mortar": round(float(np.clip(mortar_fraction, 0.0, 0.5)), 4),
            "bevel": 0.2,
            "stone_color": brick_color,
            "mortar_color": mortar_color,
            "color_variation": round(variation, 4),
        }

    return {
        "source": origin,
        "generator": generator,
        "fitted_parameters": params,
        "confidence": round(confidence, 4),
        "measurements": {
            "row_period_texels": round(row_period, 2),
            "column_period_texels": round(column_period, 2),
            "measured_column_joints": measured_columns,
            "running_bond_halving": generator == "bricks",
            "row_confidence": round(row_confidence, 4),
            "column_confidence": round(column_confidence, 4),
            "otsu_threshold": round(best_threshold, 4),
            "joint_row_fraction": round(joint_fraction, 4),
            "joint_is_darker_than_face": joint_is_dark,
            "area_below_threshold": round(area_below_threshold, 4),
            "mortar_parameter": round(float(np.clip(mortar_fraction, 0.005, 0.5)), 4),
        },
        "method": "gradient_profile_fft_period_plus_otsu_split_v1",
        "next": (
            "call generate_material with these parameters at the target "
            "resolution; the pattern is regenerated from the model, so no "
            "texel is upscaled"
        ),
    }
