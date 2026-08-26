"""HlmsPbs detail-layer authoring: base + up to 4 detail layers.

THE ENGINE CEILING IS FIVE. HlmsPbsDatablock exposes PBSM_DETAIL0..3 plus
PBSM_DETAIL0_NM..3_NM and one PBSM_DETAIL_WEIGHT mask; with the base colour
that is exactly five layers and there is no sixth.

THERE IS NO HEIGHT / DISPLACEMENT / PARALLAX SLOT. The pinned datablock's
texture types are diffuse, normal, specular, roughness, emissive, reflection,
detail 0..3, detail normal 0..3 and the detail weight mask. A height map bound
"for depth" would be silently ignored, so rorsmith never ships one as if it
were bindable. Depth is instead produced by HEIGHT-BLENDED LAYERING, which the
pinned PBS shader already supports without any new slot:

    SampleDetailMaps (800.PixelShader_piece_ps.any):
        detailWeights.<c> *= detailCol<n>.w;

so the effective per-texel weight of layer i is

    mask_channel_i  *  cDetailWeights_i  *  detailAlbedo_i.a

with detailAlbedo_i sampled at THAT LAYER'S own UV rate and the mask sampled
unscaled. The alpha channel of each detail albedo therefore carries the
layer's height/coverage with its contrast curve already baked in - that is
where "grime sits down inside the mortar line" comes from, and the engine
cannot add the contrast later.

CONTRACT (agreed with the layered-materials agent, who owns transport):
  * detail albedo  - RGBA8, sRGB. RGB = albedo, A = saturate((h - t) * k + 0.5).
  * detail normal  - RGBA, LINEAR, tangent space, same UV transform as its albedo.
  * weight mask    - one per material, RGBA8 LINEAR, R/G/B/A = layers 0..3,
                     sampled unscaled; the cross-layer "topmost wins"
                     comparison is baked here because it is the only place the
                     layers share a common rate.
"""

from __future__ import annotations

import hashlib
import io
import json
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np
from PIL import Image

from . import procedural
from .paths import Layout, RorsmithError

F32 = np.float32

MAX_DETAIL_LAYERS = 4

#: Ogre PBSM_BLEND_* modes. Only NormalNonPremul is applied by the pinned
#: frontend today (OgreNextN1Frontend.cpp hardcodes
#: PBSM_BLEND_NORMAL_NON_PREMUL); everything else is accepted as an authoring
#: declaration and reported as pending transport rather than faked.
PBSM_BLEND_MODES = (
    "PBSM_BLEND_NORMAL_NON_PREMUL",
    "PBSM_BLEND_NORMAL_PREMUL",
    "PBSM_BLEND_ADD",
    "PBSM_BLEND_SUBTRACT",
    "PBSM_BLEND_MULTIPLY",
    "PBSM_BLEND_MULTIPLY2X",
    "PBSM_BLEND_SCREEN",
    "PBSM_BLEND_OVERLAY",
    "PBSM_BLEND_LIGHTEN",
    "PBSM_BLEND_DARKEN",
    "PBSM_BLEND_GRAIN_EXTRACT",
    "PBSM_BLEND_GRAIN_MERGE",
    "PBSM_BLEND_DIFFERENCE",
)
APPLIED_BLEND_MODE = "PBSM_BLEND_NORMAL_NON_PREMUL"

AUTHORING_FORMAT = "ror-rorsmith-detail-layer-authoring-v1"


def _sha(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _u8(x: np.ndarray) -> np.ndarray:
    return np.clip(np.rint(np.asarray(x, dtype=F32) * 255.0), 0, 255).astype(np.uint8)


def _png(array: np.ndarray, mode: str) -> bytes:
    buffer = io.BytesIO()
    Image.fromarray(array, mode=mode).save(buffer, format="PNG", optimize=True)
    return buffer.getvalue()


def height_to_alpha(height: np.ndarray, threshold: float, contrast: float) -> np.ndarray:
    """alpha = saturate((h - t) * k + 0.5) - the agreed coverage curve.

    `h` is the layer's height NORMALISED to 0..1 first. Each generator's raw
    height has its own range (a noise layer spans 0..0.12, a brick base 0..0.9),
    so a shared `t` applied to raw values would clamp whole layers to alpha 0
    and the layer would be invisible while every manifest still claimed it was
    there. The normalisation is recorded with the literals.
    """
    field = np.asarray(height, dtype=F32)
    low, high = float(field.min()), float(field.max())
    normalised = (
        np.full_like(field, F32(0.5))
        if high - low < 1e-6
        else (field - F32(low)) / F32(high - low)
    )
    return np.clip(
        (normalised - F32(threshold)) * F32(contrast) + F32(0.5),
        F32(0.0),
        F32(1.0),
    )


@dataclass
class LayerRequest:
    generator: str
    params: dict = field(default_factory=dict)
    uv_scale: tuple[float, float] = (1.0, 1.0)
    uv_offset: tuple[float, float] = (0.0, 0.0)
    weight: float = 1.0
    blend_mode: str = APPLIED_BLEND_MODE
    coverage_threshold: float = 0.5
    coverage_contrast: float = 6.0
    #: Where this layer's surface sits in the material's common 0..1 height
    #: space. 1.0 is the proudest point of the base (a brick face), 0.0 the
    #: deepest (the mortar bed). A grime layer at 0.35 therefore wins inside
    #: the mortar line and loses on the brick face - which is the whole point.
    elevation: float = 0.5
    #: How much the layer's own height modulates that elevation.
    elevation_spread: float = 0.25
    #: Sharpness of the cross-layer height comparison baked into the mask.
    elevation_contrast: float = 4.0
    placement_cells: int = 4
    placement_coverage: float = 0.6
    detail_normal: bool = True
    name: str | None = None


def _as_layer(raw: dict) -> LayerRequest:
    if not isinstance(raw, dict) or "generator" not in raw:
        raise RorsmithError("layer_spec_invalid", "each layer needs a 'generator'")
    blend = str(raw.get("blend_mode", APPLIED_BLEND_MODE))
    if blend not in PBSM_BLEND_MODES:
        raise RorsmithError(
            "unknown_blend_mode", f"'{blend}' is not a PBSM_BLEND_* mode"
        )
    scale = raw.get("uv_scale", [1.0, 1.0])
    offset = raw.get("uv_offset", [0.0, 0.0])
    for label, value in (("uv_scale", scale), ("uv_offset", offset)):
        if not (isinstance(value, (list, tuple)) and len(value) == 2):
            raise RorsmithError("layer_spec_invalid", f"{label} must be [u, v]")
    return LayerRequest(
        generator=str(raw["generator"]),
        params=dict(raw.get("params") or {}),
        uv_scale=(float(scale[0]), float(scale[1])),
        uv_offset=(float(offset[0]), float(offset[1])),
        weight=float(raw.get("weight", 1.0)),
        blend_mode=blend,
        coverage_threshold=float(raw.get("coverage_threshold", 0.5)),
        coverage_contrast=float(raw.get("coverage_contrast", 6.0)),
        elevation=float(raw.get("elevation", 0.5)),
        elevation_spread=float(raw.get("elevation_spread", 0.25)),
        elevation_contrast=float(raw.get("elevation_contrast", 4.0)),
        placement_cells=int(raw.get("placement_cells", 4)),
        placement_coverage=float(raw.get("placement_coverage", 0.6)),
        detail_normal=bool(raw.get("detail_normal", True)),
        name=raw.get("name"),
    )


def author_layers(
    layout: Layout,
    material: str,
    layers: list[dict],
    out_dir: str,
    base: dict | None = None,
    resolution: int = 1024,
    mask_resolution: int = 512,
    dry_run: bool = True,
) -> dict[str, object]:
    """Configure a base + up to 4 HlmsPbs detail layers for one material.

    With `dry_run` (the default) nothing is written: the declaration, the
    per-layer contrast literals and the map manifest are returned so the plan
    can be reviewed before any bytes exist.
    """
    if not layers:
        raise RorsmithError("no_layers", "author_layers needs at least one detail layer")
    if len(layers) > MAX_DETAIL_LAYERS:
        raise RorsmithError(
            "detail_layer_ceiling",
            f"{len(layers)} detail layers requested; HlmsPbsDatablock exposes "
            f"PBSM_DETAIL0..3 - exactly {MAX_DETAIL_LAYERS} on top of the base "
            "(5 total). rorsmith refuses rather than silently dropping one.",
        )
    requests = [_as_layer(raw) for raw in layers]
    for request in requests:
        if request.generator not in procedural.GENERATORS:
            raise RorsmithError(
                "unknown_generator",
                f"'{request.generator}' is not one of {sorted(procedural.GENERATORS)}",
            )
        if not (0.0 <= request.weight <= 1.0):
            raise RorsmithError(
                "detail_weight_range",
                f"detail weight {request.weight} is outside the unit range the "
                "descriptor validator requires",
            )

    base_spec = dict(base or {"generator": "bricks", "params": {}})
    base_request = _as_layer({**base_spec, "blend_mode": APPLIED_BLEND_MODE})

    target = Path(out_dir).expanduser()
    stem = "".join(c if c.isalnum() or c in "-_" else "_" for c in material)

    # ---- evaluate ----------------------------------------------------
    base_result = procedural.evaluate(
        base_request.generator, base_request.params, int(resolution)
    )
    layer_results = [
        procedural.evaluate(r.generator, r.params, int(resolution)) for r in requests
    ]

    # ---- weight mask: cross-layer topmost-wins at a common rate --------
    mask_size = int(mask_resolution)
    if mask_size <= 0 or mask_size & (mask_size - 1):
        raise RorsmithError("mask_resolution_not_power_of_two", str(mask_size))
    # Every generator's height field has its own natural range, so the
    # cross-layer comparison is done in a COMMON normalised 0..1 space. Raw
    # ranges would make a 0..0.12 noise layer lose to a 0..0.9 brick base
    # everywhere and every mask channel would come out zero.
    running = _normalise(_resample(base_result.height, mask_size))
    channels: list[np.ndarray] = []
    surfaces: list[dict[str, object]] = []
    for index, (request, result) in enumerate(zip(requests, layer_results)):
        placement_noise = procedural.fbm(
            mask_size,
            cells=max(1, request.placement_cells),
            octaves=4,
            seed=17.0 + index * 3.7,
        )
        placement = np.clip(
            (placement_noise - (F32(1.0) - F32(request.placement_coverage)))
            * F32(3.0)
            + F32(0.5),
            F32(0.0),
            F32(1.0),
        )
        layer_height = _normalise(_resample(result.height, mask_size))
        surface = np.clip(
            F32(request.elevation)
            + F32(request.elevation_spread) * (layer_height - F32(0.5)),
            F32(0.0),
            F32(1.0),
        )
        weight = (
            np.clip(
                (surface - running) * F32(request.elevation_contrast) + F32(0.5),
                F32(0.0),
                F32(1.0),
            )
            * placement
        )
        channels.append(weight)
        surfaces.append(
            {
                "index": index,
                "mean_mask_weight": round(float(weight.mean()), 4),
                "mask_weight_p95": round(float(np.percentile(weight, 95)), 4),
            }
        )
        running = running * (F32(1.0) - weight) + surface * weight
    while len(channels) < MAX_DETAIL_LAYERS:
        channels.append(np.zeros((mask_size, mask_size), dtype=F32))
    mask_rgba = np.stack([_u8(c) for c in channels], axis=-1)

    # ---- assemble the map manifest -----------------------------------
    manifest: list[dict[str, object]] = []

    def record(slot: str, filename: str, payload: bytes, storage: str, note: str = "") -> None:
        manifest.append(
            {
                "slot": slot,
                "file": filename,
                "bytes": len(payload),
                "sha256": _sha(payload),
                "storage": storage,
                **({"note": note} if note else {}),
            }
        )

    files: dict[str, bytes] = {}

    base_albedo = _png(_u8(base_result.albedo), "RGB")
    files[f"{stem}_base_albedo.png"] = base_albedo
    record("PBSM_DIFFUSE", f"{stem}_base_albedo.png", base_albedo, "RGB8 sRGB")

    base_normal = _png(
        _u8(procedural.normal_from_height(base_result.height) * F32(0.5) + F32(0.5)), "RGB"
    )
    files[f"{stem}_base_normal.png"] = base_normal
    record("PBSM_NORMAL", f"{stem}_base_normal.png", base_normal, "RGB8 linear")

    base_roughness = _png(_u8(base_result.roughness), "L")
    files[f"{stem}_base_roughness.png"] = base_roughness
    record("PBSM_ROUGHNESS", f"{stem}_base_roughness.png", base_roughness, "L8 linear")

    mask_png = _png(mask_rgba, "RGBA")
    files[f"{stem}_detail_weight.png"] = mask_png
    record(
        "PBSM_DETAIL_WEIGHT",
        f"{stem}_detail_weight.png",
        mask_png,
        "RGBA8 linear",
        "R/G/B/A = detail layers 0..3; sampled unscaled; identity UV transform is "
        "required by ValidateMaterialDescriptor",
    )

    layer_records: list[dict[str, object]] = []
    for index, (request, result) in enumerate(zip(requests, layer_results)):
        alpha = height_to_alpha(
            result.height, request.coverage_threshold, request.coverage_contrast
        )
        rgba = np.concatenate(
            [_u8(result.albedo), _u8(alpha)[..., None]], axis=-1
        )
        surfaces[index]["mean_albedo_alpha"] = round(float(alpha.mean()), 4)
        albedo_name = f"{stem}_detail{index}_albedo.png"
        payload = _png(rgba, "RGBA")
        files[albedo_name] = payload
        record(
            f"PBSM_DETAIL{index}",
            albedo_name,
            payload,
            "RGBA8 sRGB",
            f"alpha = saturate((normalise(height) - "
            f"{request.coverage_threshold:g}) * "
            f"{request.coverage_contrast:g} + 0.5)",
        )
        entry: dict[str, object] = {
            "index": index,
            "name": request.name or request.generator,
            "generator": request.generator,
            "provenance": procedural.GENERATORS[request.generator].provenance,
            "parameters": request.params,
            "uv_scale": list(request.uv_scale),
            "uv_offset": list(request.uv_offset),
            "detail_weight": request.weight,
            "blend_mode_requested": request.blend_mode,
            "blend_mode_applied": APPLIED_BLEND_MODE,
            "coverage_threshold_t": request.coverage_threshold,
            "coverage_contrast_k": request.coverage_contrast,
            "elevation": request.elevation,
            "elevation_spread": request.elevation_spread,
            "elevation_contrast": request.elevation_contrast,
            "placement_cells": request.placement_cells,
            "placement_coverage": request.placement_coverage,
            "mask_statistics": surfaces[index],
            "albedo": albedo_name,
            "notes": result.notes,
        }
        if request.blend_mode != APPLIED_BLEND_MODE:
            entry["blend_mode_status"] = (
                "DECLARED_NOT_APPLIED: OgreNextN1Frontend.cpp pins every detail "
                "layer to PBSM_BLEND_NORMAL_NON_PREMUL. The requested mode is "
                "recorded in this declaration and needs the layered-materials "
                "agent's transport before it changes a pixel."
            )
        if request.detail_normal:
            normal_name = f"{stem}_detail{index}_normal.png"
            normal = procedural.normal_from_height(result.height)
            normal_rgba = np.concatenate(
                [
                    _u8(normal * F32(0.5) + F32(0.5)),
                    np.full(normal.shape[:2] + (1,), 255, dtype=np.uint8),
                ],
                axis=-1,
            )
            payload = _png(normal_rgba, "RGBA")
            files[normal_name] = payload
            record(
                f"PBSM_DETAIL{index}_NM",
                normal_name,
                payload,
                "RGBA8 linear",
                "shares mDetailsOffsetScale[%d] with its albedo" % index,
            )
            entry["detail_normal"] = normal_name
        layer_records.append(entry)

    declaration = {
        "format": AUTHORING_FORMAT,
        "material": material,
        "resolution": int(resolution),
        "mask_resolution": mask_size,
        "base": {
            "generator": base_request.generator,
            "provenance": procedural.GENERATORS[base_request.generator].provenance,
            "parameters": base_request.params,
        },
        "layers": layer_records,
        "maps": manifest,
        "engine_facts": {
            "detail_layer_ceiling": MAX_DETAIL_LAYERS,
            "total_layers": len(requests) + 1,
            "height_slot": "none - HlmsPbsDatablock has no height/displacement/parallax slot",
            "effective_weight": "mask_channel_i * cDetailWeights_i * detailAlbedo_i.a",
            "mask_uv": "identity transform required; the mask does placement, not density",
        },
        "runtime_status": {
            "detail_slots": "bound by OgreNextN1Frontend for MODERN_PBR_RT4_V1",
            "non_terrain_transport": (
                "PENDING: the scene transport that carries detail bindings for "
                "legacy/non-terrain materials is the layered-materials agent's "
                "work. rorsmith writes the authoring declaration and the maps; "
                "it does not claim the runtime is applying them."
            ),
            "verify_with": "verify_live, then the census, not by assertion",
        },
        "handoff": {
            "compression": "BC7 (albedo, detail albedo), BC5 (normals), BC4 (mask channels) - texture agent",
            "packaging": "content compiler - texture agent",
        },
    }

    declaration_bytes = json.dumps(declaration, indent=1, sort_keys=True).encode() + b"\n"
    declaration_name = f"{stem}.layers.json"

    if dry_run:
        return {
            "dry_run": True,
            "material": material,
            "out_dir": str(target),
            "would_write": [
                {"file": declaration_name, "bytes": len(declaration_bytes)},
                *[
                    {"file": name, "bytes": len(payload), "sha256": _sha(payload)}
                    for name, payload in sorted(files.items())
                ],
            ],
            "declaration": declaration,
        }

    target.mkdir(parents=True, exist_ok=True)
    written = []
    for name, payload in sorted(files.items()):
        (target / name).write_bytes(payload)
        written.append({"file": str(target / name), "bytes": len(payload), "sha256": _sha(payload)})
    (target / declaration_name).write_bytes(declaration_bytes)
    written.append(
        {
            "file": str(target / declaration_name),
            "bytes": len(declaration_bytes),
            "sha256": _sha(declaration_bytes),
        }
    )
    return {
        "dry_run": False,
        "material": material,
        "out_dir": str(target),
        "written": written,
        "declaration": declaration,
    }


def _normalise(field: np.ndarray) -> np.ndarray:
    """Rescale a height field to 0..1 so layers can be compared at all."""
    low, high = float(field.min()), float(field.max())
    if high - low < 1e-6:
        return np.full_like(field, F32(0.5))
    return (field - F32(low)) / F32(high - low)


def _resample(field: np.ndarray, size: int) -> np.ndarray:
    source = np.asarray(field, dtype=F32)
    if source.shape[0] == size:
        return source
    image = Image.fromarray(_u8(source), mode="L").resize((size, size), Image.BILINEAR)
    return np.asarray(image, dtype=F32) / F32(255.0)


#: A reviewed five-layer facade stack. Base brick carries the mortar recess;
#: each detail layer's own height then decides, per texel, whether it sits on
#: top of what is already there. Grime settles into the mortar because its
#: effective height there is below the brick face, not because it was painted
#: across the wall.
FACADE_PRESET: dict[str, object] = {
    "base": {
        "generator": "bricks",
        "params": {
            "pattern": "running_bond",
            "rows": 16.0,
            "columns": 6.0,
            "mortar": 0.06,
            "bevel": 0.05,
            "color_variation": 0.45,
            "grit": 0.4,
        },
    },
    "layers": [
        {
            # Mortar bed variation: sits just BELOW the brick face, so it only
            # shows where the base is already recessed.
            "name": "mortar_variation",
            "generator": "surface_noise",
            "params": {"cells": 24, "octaves": 4, "amplitude": 0.2, "color": [0.58, 0.56, 0.52]},
            "uv_scale": [4.0, 4.0],
            "weight": 0.55,
            "coverage_threshold": 0.45,
            "coverage_contrast": 4.0,
            "elevation": 0.30,
            "elevation_spread": 0.30,
            "elevation_contrast": 5.0,
            "placement_cells": 3,
            "placement_coverage": 0.85,
        },
        {
            # Weathering: low elevation and high alpha contrast, so grime runs
            # down into the joints rather than washing across the brick faces.
            "name": "weathering_grime",
            "generator": "grime",
            "params": {"cells": 5, "coverage": 0.6, "contrast": 1.8},
            "uv_scale": [2.0, 2.0],
            "weight": 0.8,
            "coverage_threshold": 0.5,
            "coverage_contrast": 9.0,
            "elevation": 0.38,
            "elevation_spread": 0.34,
            "elevation_contrast": 4.0,
            "placement_cells": 2,
            "placement_coverage": 0.7,
        },
        {
            # Moss: the deepest layer. It wins only inside what is still the
            # lowest part of the surface after grime has filled some of it.
            "name": "recess_moss",
            "generator": "moss",
            "params": {"cells": 12, "coverage": 0.35, "contrast": 2.6},
            "uv_scale": [3.0, 3.0],
            "weight": 0.7,
            "coverage_threshold": 0.35,
            "coverage_contrast": 8.0,
            "elevation": 0.52,
            "elevation_spread": 0.36,
            "elevation_contrast": 6.0,
            "placement_cells": 2,
            "placement_coverage": 0.72,
        },
        {
            # Fine break-up sits AT the running surface, so it modulates
            # everything below it instead of choosing a side.
            "name": "fine_surface_noise",
            "generator": "surface_noise",
            "params": {"cells": 96, "octaves": 3, "amplitude": 0.08},
            "uv_scale": [8.0, 8.0],
            "weight": 0.28,
            "coverage_threshold": 0.5,
            "coverage_contrast": 3.0,
            "elevation": 0.55,
            "elevation_spread": 0.5,
            "elevation_contrast": 1.5,
            "placement_cells": 6,
            "placement_coverage": 0.95,
        },
    ],
}

PRESETS = {"facade_5layer": FACADE_PRESET}
