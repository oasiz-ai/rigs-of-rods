"""The rorsmith MCP server: stdio transport, official MCP Python SDK."""

from __future__ import annotations

import json
import traceback
from typing import Any, Callable

import mcp.types as types
from mcp.server import Server
from mcp.server.stdio import stdio_server

from . import apply as _apply
from . import derive, inventory, layers, live, policy, procedural
from .paths import RorsmithError, build_layout

SERVER_NAME = "rorsmith"


def _json(payload: Any) -> list[types.ContentBlock]:
    return [types.TextContent(type="text", text=json.dumps(payload, indent=1, default=str))]


def _refusal(reason: str, detail: str, hint: str = "") -> list[types.ContentBlock]:
    body: dict[str, Any] = {"refused": True, "reason": reason, "detail": detail}
    if hint:
        body["hint"] = hint
    return _json(body)


# --------------------------------------------------------------------------
# Tool implementations
# --------------------------------------------------------------------------

def _t_list_materials(args: dict) -> Any:
    layout = build_layout()
    return inventory.list_materials(
        layout,
        archive=args["archive"],
        name_filter=args.get("filter"),
        band_filter=args.get("band"),
        limit=int(args.get("limit", 200)),
    )


def _t_inspect_material(args: dict) -> Any:
    layout = build_layout()
    return inventory.inspect_material(layout, args["archive"], args["name"])


def _t_derive_normal_map(args: dict) -> Any:
    layout = build_layout()
    return derive.derive_normal_map(
        layout,
        texture=args["texture"],
        out_dir=args["out_dir"],
        strength=float(args.get("strength", 1.0)),
        family=args.get("family"),
        highpass_fraction=float(args.get("highpass_fraction", 0.06)),
    )


def _t_derive_roughness(args: dict) -> Any:
    layout = build_layout()
    return derive.derive_roughness(
        layout,
        family=args.get("family"),
        material=args.get("material"),
        texture=args.get("texture"),
        out_dir=args.get("out_dir"),
        map_method=args.get("map_method"),
    )


def _t_list_generators(args: dict) -> Any:
    return {
        "generators": [
            {
                "name": spec.name,
                "summary": spec.summary,
                "provenance": spec.provenance,
                "parameters": spec.parameters,
            }
            for spec in procedural.GENERATORS.values()
        ],
        "outputs": list(derive._MAP_ORDER),
        "evaluation": (
            "CPU numpy port of the Material Maker (MIT) node functions; no GPU "
            "context, no shader compile, resolution-independent"
        ),
        "presets": sorted(layers.PRESETS),
    }


def _t_generate_material(args: dict) -> Any:
    layout = build_layout()
    return derive.generate_material(
        layout,
        generator=args["generator"],
        out_dir=args["out_dir"],
        params=args.get("params"),
        resolution=int(args.get("resolution", 1024)),
        outputs=args.get("outputs"),
        normal_strength=float(args.get("normal_strength", 1.0)),
        name=args.get("name"),
    )


def _t_fit_generator(args: dict) -> Any:
    layout = build_layout()
    return derive.fit_generator(
        layout,
        texture=args["texture"],
        generator=args.get("generator", "bricks"),
        min_confidence=float(args.get("min_confidence", 0.12)),
    )


def _t_author_layers(args: dict) -> Any:
    layout = build_layout()
    preset = args.get("preset")
    spec_layers = args.get("layers")
    base = args.get("base")
    if preset:
        if preset not in layers.PRESETS:
            raise RorsmithError("unknown_preset", f"{preset}; known: {sorted(layers.PRESETS)}")
        chosen = layers.PRESETS[preset]
        spec_layers = spec_layers or chosen["layers"]
        base = base or chosen["base"]
    if not spec_layers:
        raise RorsmithError("no_layers", "give layers=[...] or preset=")
    return layers.author_layers(
        layout,
        material=args["material"],
        layers=spec_layers,
        out_dir=args["out_dir"],
        base=base,
        resolution=int(args.get("resolution", 1024)),
        mask_resolution=int(args.get("mask_resolution", 512)),
        dry_run=bool(args.get("dry_run", True)),
    )


def _t_apply_to_archive(args: dict) -> Any:
    layout = build_layout()
    return _apply.apply_to_archive(
        layout,
        archive=args["archive"],
        changes=args.get("changes") or {},
        dry_run=bool(args.get("dry_run", True)),
        backup_label=str(args.get("backup_label", "rorsmith")),
        install=bool(args.get("install", True)),
        repin=bool(args.get("repin", True)),
    )


def _t_verify_live(args: dict) -> Any:
    layout = build_layout()
    return live.verify_live(
        layout,
        terrain=args.get("map", "CityWorldNextLocalOverlay.terrn2"),
        truck=args.get("truck"),
        binary=args.get("binary"),
        timeout_seconds=float(args.get("timeout_seconds", 900.0)),
        width=int(args.get("width", live.DEFAULT_EXTENT[0])),
        height=int(args.get("height", live.DEFAULT_EXTENT[1])),
        keep_home=bool(args.get("keep_home", False)),
        material_filter=args.get("material_filter"),
        home_dir=args.get("home_dir"),
    )


def _t_renderer_policy(args: dict) -> Any:
    layout = build_layout()
    limits = policy.structural_limits(str(layout.private_policy_h))
    return {
        "repo_root": str(layout.repo_root),
        "mods_dirs": [str(d) for d in layout.mods_dirs],
        "backup_dir": str(layout.backup_dir),
        "refusal_tokens": list(policy.refusal_tokens(str(layout.private_policy_cpp))),
        "roughness_bands": {
            name: {
                "roughness": round(band.roughness, 4),
                "shininess": band.shininess,
                "specular_rgb": band.specular_rgb,
            }
            for name, band in sorted(policy.bands().items())
        },
        "structural_limits": {
            "max_texture_units_per_pass": limits.max_texture_units_per_pass,
            "max_passes_per_material": limits.max_passes_per_material,
            "max_detail_layers": limits.max_detail_layers,
            "max_total_layers": limits.max_detail_layers + 1,
        },
        "compatibility_pins": policy.compatibility_pins(layout),
        "identity": "roughness = sqrt(2 / (shininess + 2))",
    }


_ARCHIVE = {
    "type": "string",
    "description": "Archive name in a mods dir (e.g. CityWorldNextLocalOverlay.zip, "
    "CityWorld.zip, AlexisSaber.zip) or an absolute path.",
}

TOOLS: dict[str, tuple[types.Tool, Callable[[dict], Any]]] = {}


def _register(tool: types.Tool, handler: Callable[[dict], Any]) -> None:
    TOOLS[tool.name] = (tool, handler)


_register(
    types.Tool(
        name="list_materials",
        description=(
            "Inventory an archive's Ogre materials with current state: textures "
            "bound, assigned F3 roughness band, pass/texture-unit counts, and a "
            "STATIC admission prediction with the renderer's own refusal tokens. "
            "The prediction is structure-only; verify_live is ground truth."
        ),
        inputSchema={
            "type": "object",
            "properties": {
                "archive": _ARCHIVE,
                "filter": {"type": "string", "description": "substring of material name or texture"},
                "band": {"type": "string", "description": "only this roughness band"},
                "limit": {"type": "integer", "default": 200},
            },
            "required": ["archive"],
        },
    ),
    _t_list_materials,
)

_register(
    types.Tool(
        name="inspect_material",
        description=(
            "Full detail on one material: passes, texture units, layer equations, "
            "structural anomalies, the exact specular/shininess the sanitizer "
            "would inject and under which reviewed rule, and whether structure "
            "alone refuses projection (with the refusal token) or leaves it a "
            "projection candidate."
        ),
        inputSchema={
            "type": "object",
            "properties": {"archive": _ARCHIVE, "name": {"type": "string"}},
            "required": ["archive", "name"],
        },
    ),
    _t_inspect_material,
)

_register(
    types.Tool(
        name="renderer_policy",
        description=(
            "The live policy facts rorsmith obeys, read from the engine sources: "
            "every matte/refusal token, the F3 roughness band table, the legacy "
            "pass/texture-unit caps, the detail-layer ceiling, and the archive "
            "digest pins."
        ),
        inputSchema={"type": "object", "properties": {}},
    ),
    _t_renderer_policy,
)

_register(
    types.Tool(
        name="derive_normal_map",
        description=(
            "Derive a tangent-space normal map from a source texture. Method: "
            "linear luminance, box high-pass, wrapped Sobel, +Y-up encode. The "
            "height is an ALBEDO PROXY, not a measurement - painted contrast "
            "becomes false relief. Prefer generate_material where a real modelled "
            "height field is available."
        ),
        inputSchema={
            "type": "object",
            "properties": {
                "texture": {
                    "type": "string",
                    "description": "path, or 'Archive.zip::member'",
                },
                "out_dir": {"type": "string"},
                "strength": {"type": "number", "default": 1.0},
                "family": {"type": "string"},
                "highpass_fraction": {"type": "number", "default": 0.06},
            },
            "required": ["texture", "out_dir"],
        },
    ),
    _t_derive_normal_map,
)

_register(
    types.Tool(
        name="derive_roughness",
        description=(
            "Return the reviewed F3 roughness for a family, material name, or "
            "texture, consistent with the band table the sanitizer injects. "
            "Answers with a SCALAR by default because the reviewed pipeline is "
            "per-family; a per-texel map is opt-in via map_method and is labelled "
            "an approximation."
        ),
        inputSchema={
            "type": "object",
            "properties": {
                "family": {"type": "string"},
                "material": {"type": "string"},
                "texture": {"type": "string"},
                "out_dir": {"type": "string"},
                "map_method": {"type": "string", "enum": ["albedo_variance"]},
            },
        },
    ),
    _t_derive_roughness,
)

_register(
    types.Tool(
        name="list_generators",
        description=(
            "The procedural generators available, with parameters, defaults and "
            "provenance. Pattern generators are CPU ports of Material Maker (MIT) "
            "node functions; noise generators are named as rorsmith originals."
        ),
        inputSchema={"type": "object", "properties": {}},
    ),
    _t_list_generators,
)

_register(
    types.Tool(
        name="generate_material",
        description=(
            "Evaluate a generator headlessly at any power-of-two resolution and "
            "emit a PBR map set (albedo / normal / roughness / ao / height) as "
            "PNG. Resolution-independent: every texel comes from the parametric "
            "model, none is upscaled. BC compression and packaging are the "
            "texture agent's pipeline."
        ),
        inputSchema={
            "type": "object",
            "properties": {
                "generator": {"type": "string"},
                "out_dir": {"type": "string"},
                "params": {"type": "object"},
                "resolution": {"type": "integer", "default": 1024},
                "outputs": {"type": "array", "items": {"type": "string"}},
                "normal_strength": {"type": "number", "default": 1.0},
                "name": {"type": "string"},
            },
            "required": ["generator", "out_dir"],
        },
    ),
    _t_generate_material,
)

_register(
    types.Tool(
        name="fit_generator",
        description=(
            "Estimate generator parameters that approximate an existing legacy "
            "texture - row/column pitch, mortar fraction, brick and mortar "
            "colour, colour variation - so a regenerated 2K map stays the same "
            "building. REFUSES with a truthful reason when the source is not "
            "periodic enough to fit."
        ),
        inputSchema={
            "type": "object",
            "properties": {
                "texture": {"type": "string", "description": "path, or 'Archive.zip::member'"},
                "generator": {"type": "string", "enum": ["bricks", "arc_pavement"], "default": "bricks"},
                "min_confidence": {"type": "number", "default": 0.12},
            },
            "required": ["texture"],
        },
    ),
    _t_fit_generator,
)

_register(
    types.Tool(
        name="author_layers",
        description=(
            "Configure HlmsPbs detail layers: base plus up to 4 (the engine "
            "ceiling - PBSM_DETAIL0..3, five layers total). Emits per-layer "
            "detail albedo RGBA8 sRGB with the height/coverage curve baked into "
            "alpha, matching detail normals at the same UV rate, and one RGBA8 "
            "linear PBSM_DETAIL_WEIGHT mask carrying the cross-layer "
            "topmost-wins comparison. dry_run is the default. Blend modes other "
            "than PBSM_BLEND_NORMAL_NON_PREMUL are recorded as declared-not-"
            "applied because the frontend pins that mode today."
        ),
        inputSchema={
            "type": "object",
            "properties": {
                "material": {"type": "string"},
                "out_dir": {"type": "string"},
                "preset": {"type": "string", "description": "e.g. facade_5layer"},
                "base": {"type": "object"},
                "layers": {"type": "array", "items": {"type": "object"}},
                "resolution": {"type": "integer", "default": 1024},
                "mask_resolution": {"type": "integer", "default": 512},
                "dry_run": {"type": "boolean", "default": True},
            },
            "required": ["material", "out_dir"],
        },
    ),
    _t_author_layers,
)

_register(
    types.Tool(
        name="apply_to_archive",
        description=(
            "Rewrite archive members on the established procedure: only the "
            "intended members change, every other member stays byte-identical, "
            "a dated backup is made alongside mods-originals/ (never overwriting "
            "one), the result installs into BOTH mods directories, and "
            "kCityWorld*ArchiveSha256 is repinned in "
            "LegacyMaterialCompatibilityPlan.h when an authenticated archive "
            "changes. dry_run is the DEFAULT and returns the member diff with "
            "sha256 before/after. Re-running an applied change reports zero "
            "changes."
        ),
        inputSchema={
            "type": "object",
            "properties": {
                "archive": _ARCHIVE,
                "changes": {
                    "type": "object",
                    "description": "member name -> text, 'file:<path>', or 'base64:<data>'",
                },
                "dry_run": {"type": "boolean", "default": True},
                "backup_label": {"type": "string", "default": "rorsmith"},
                "install": {"type": "boolean", "default": True},
                "repin": {"type": "boolean", "default": True},
            },
            "required": ["archive", "changes"],
        },
    ),
    _t_apply_to_archive,
)

_register(
    types.Tool(
        name="verify_live",
        description=(
            "Launch the combined runtime in an ISOLATED ROR_D0_SCENE_HOME with "
            "ROR_SCENE_CENSUS on, wait for the material census, and return the "
            "numbers: candidate/projected/matte sections, the matte_by_reason "
            "histogram, distinct texture keys, per-material projected/matte/"
            "reason rows, native roughness bins, and capture_rejected. Never "
            "touches the user's own session or log."
        ),
        inputSchema={
            "type": "object",
            "properties": {
                "map": {"type": "string", "default": "CityWorldNextLocalOverlay.terrn2"},
                "truck": {"type": "string"},
                "binary": {"type": "string"},
                "timeout_seconds": {"type": "number", "default": 900},
                "home_dir": {
                    "type": "string",
                    "description": "reuse this isolated home (warm cache). Only "
                    "ever reuse one built by the same binary.",
                },
                "width": {"type": "integer", "default": 1280},
                "height": {"type": "integer", "default": 720},
                "keep_home": {"type": "boolean", "default": False},
                "material_filter": {"type": "string"},
            },
        },
    ),
    _t_verify_live,
)


def build_server() -> Server:
    server: Server = Server(SERVER_NAME)

    @server.list_tools()
    async def list_tools() -> list[types.Tool]:
        return [tool for tool, _ in TOOLS.values()]

    @server.call_tool()
    async def call_tool(name: str, arguments: dict | None) -> list[types.ContentBlock]:
        entry = TOOLS.get(name)
        if entry is None:
            return _refusal("unknown_tool", name, f"known: {sorted(TOOLS)}")
        _, handler = entry
        try:
            return _json(handler(arguments or {}))
        except RorsmithError as exc:
            return _refusal(exc.reason, exc.detail)
        except KeyError as exc:
            return _refusal("missing_argument", str(exc))
        except Exception as exc:  # pragma: no cover - surfaced, never swallowed
            return _refusal(
                "tool_failed",
                f"{type(exc).__name__}: {exc}",
                traceback.format_exc(limit=4),
            )

    return server


async def run() -> None:
    server = build_server()
    async with stdio_server() as (read_stream, write_stream):
        await server.run(
            read_stream, write_stream, server.create_initialization_options()
        )
