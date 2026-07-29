# CityWorld Next editable sources

This directory contains project-authored source and evidence for the
rights-cleared CityWorld visual upgrade. It does not contain or derive geometry
from the user-supplied `CityWorld.zip`.

The first bridge family contains a 20 m tangent span, a 20 m-centreline
15-degree left curve, and a 12 m road-to-bridge transition:

- editable source:
  `bridge/rorng_city_bridge_span_20m.blend`;
- authoring preview:
  `bridge/rorng_city_bridge_span_20m_preview.png`;
- runtime-neutral interchange:
  `../../resources/nextgen/cityworld/bridge/rorng_city_bridge_span_20m.glb`;
- asset contract:
  `../../resources/nextgen/cityworld/bridge/rorng_city_bridge_span_20m.asset.json`;
- release provenance:
  `provenance/cityworld_next.manifest.json` and
  `provenance/cityworld_next.inventory.json`.

The curved module adds a 76.394 m radius, exact tangent-aware connectors, an
integrated pier and bearings, four LED fixtures, three render LODs, and
separate swept road/barrier collision shells:

- editable source:
  `bridge/curve_left_15deg/rorng_city_bridge_curve_left_15deg_20m.blend`;
- authoring preview:
  `bridge/curve_left_15deg/rorng_city_bridge_curve_left_15deg_20m_preview.png`;
- runtime-neutral interchange and contract:
  `../../resources/nextgen/cityworld/bridge/curve_left_15deg/`.

The transition module closes a bridge corridor against terrain with a
continuous drivable slab, expansion joint, drains, bearing shelf, backwall,
flared wing walls, retaining toes, three render LODs, and separate road and
barrier collision meshes:

- editable source:
  `bridge/transition_12m/rorng_city_bridge_transition_12m.blend`;
- authoring preview:
  `bridge/transition_12m/rorng_city_bridge_transition_12m_preview.png`;
- runtime-neutral interchange and contract:
  `../../resources/nextgen/cityworld/bridge/transition_12m/`.

The v2 city-side gateway block is a connector-compatible 40 m streetscape
with four project-authored mid-rises. Recessed glazing, exterior frames,
storefront doors and mullions, balconies, façade bands, pilasters, parapets,
roof penthouses, and HVAC silhouettes provide close-range depth without
entering the collision meshes. Eight deterministic three-shape trees replace
the original cylindrical crowns with tapered two-part trunks, visible branch
structure, and varied six-lobe close / three-lobe medium canopies. The
32,092 / 3,596 / 276 triangle render ladder consumes 53.5% of the 60,000
triangle LOD0 ceiling while retaining the skyline in all three LODs.

Eight emissive streetlights and their eight bounded dynamic point lights keep
the original identifiers, positions, colours, ranges, and ODEF contract.
Road, connector, and three conservative collision objects are unchanged.
The generator imports no geometry or textures. Its canonical GLB rebuild gives
every primitive independent accessors, preserves all source vertices and
triangle winding, and rejects unreferenced vertices; consecutive Blender 5.2
generations are byte-identical. The asset contract pins both the v2 entrypoint
and its shared bridge-kit helper, and validation fails if either source changes
without regeneration. The brighter authoring preview uses
preview-only fill and exposure and does not alter RoR's runtime PSSM or local
light behavior:

- editable source:
  `streetscape/gateway_block_40m/rorng_city_gateway_block_40m.blend`;
- authoring preview:
  `streetscape/gateway_block_40m/rorng_city_gateway_block_40m_preview.png`;
- runtime-neutral interchange and contract:
  `../../resources/nextgen/cityworld/streetscape/gateway_block_40m/`.

Regenerate with Blender 4.0 or newer:

```sh
blender --background --factory-startup \
  --python tools/blender/cityworld_next/generate_bridge_kit.py -- \
  --output-root "$PWD"
blender --background --factory-startup \
  --python tools/blender/cityworld_next/generate_curved_bridge.py -- \
  --output-root "$PWD"
blender --background --factory-startup \
  --python tools/blender/cityworld_next/generate_bridge_transition.py -- \
  --output-root "$PWD"
blender --background --factory-startup \
  --python tools/blender/cityworld_next/generate_gateway_block.py -- \
  --output-root "$PWD"
```

Validate the authored output and rebuild provenance:

```sh
python3 tools/validate_cityworld_asset.py \
  resources/nextgen/cityworld/bridge/rorng_city_bridge_span_20m.asset.json \
  --repo-root .
python3 tools/validate_cityworld_asset.py \
  resources/nextgen/cityworld/bridge/curve_left_15deg/rorng_city_bridge_curve_left_15deg_20m.asset.json \
  --repo-root .
python3 tools/validate_cityworld_asset.py \
  resources/nextgen/cityworld/bridge/transition_12m/rorng_city_bridge_transition_12m.asset.json \
  --repo-root .
python3 tools/validate_cityworld_asset.py \
  resources/nextgen/cityworld/streetscape/gateway_block_40m/rorng_city_gateway_block_40m.asset.json \
  --repo-root .
python3 tools/solve_cityworld_bridge_corridor.py \
  --asset resources/nextgen/cityworld/bridge/curve_left_15deg/rorng_city_bridge_curve_left_15deg_20m.asset.json \
  --asset resources/nextgen/cityworld/bridge/curve_left_15deg/rorng_city_bridge_curve_left_15deg_20m.asset.json \
  --asset resources/nextgen/cityworld/bridge/curve_left_15deg/rorng_city_bridge_curve_left_15deg_20m.asset.json \
  --asset resources/nextgen/cityworld/bridge/transition_12m/rorng_city_bridge_transition_12m.asset.json \
  --entry-x 512 --entry-z 482 --heading-degrees 0 --format tobj
python3 tools/run_cityworld_bridge_transition_scene.py \
  --executable /Applications/RoR.app/Contents/MacOS/RoR \
  --runtime-content build-macos-ogre14-roadmap/bin/content \
  --artifact-dir /tmp/cityworld-bridge-transition-runtime
python3 tools/run_cityworld_gateway_scene.py \
  --executable /Applications/RoR.app/Contents/MacOS/RoR \
  --runtime-content build-macos-ogre14-roadmap/bin/content \
  --artifact-dir /tmp/cityworld-gateway-runtime
python3 tools/build_cityworld_next_provenance.py --repo-root .
python3 tools/build_cityworld_next_provenance.py --repo-root . --check
python3 tools/content_provenance_audit.py \
  --manifest content-source/cityworld_next/provenance/cityworld_next.manifest.json \
  --inventory content-source/cityworld_next/provenance/cityworld_next.inventory.json \
  --release-gate \
  --package-root resources/nextgen/cityworld \
  --editable-root .
```

Build the local-only Penguinville-to-NeoQueretaro overlay after those checks:

```sh
mkdir -p /tmp/ror-cityworld-local
python3 tools/build_cityworld_local_overlay.py \
  --archive "$HOME/Library/Application Support/Rigs of Rods/mods/CityWorld.zip" \
  --repo-root "$PWD" \
  --output /tmp/ror-cityworld-local/CityWorldNextLocalOverlay.zip \
  --surface-offset-m 0.08
```

The output path must not already exist and must be outside this repository.
The 35-entry deterministic ZIP references the separately installed original
`CityWorld.otc` and `CityWorld.tobj`; it does not copy either file or any
original map asset. It packages only the generated descriptor and placement,
the checked project-owned render/collision meshes, materials, ODEFs (including
the gateway lights), and a canonical local-only provenance report. The source
archive stays byte-identical and the generated package is explicitly not for
redistribution or shipping.

The GLB is the canonical geometry interchange output and is expected to remain
byte-deterministic for the pinned Blender generator. `.blend` files and
rendered previews are editable/evidence artifacts whose exact bytes may change
between Blender sessions or versions; their current hashes are always pinned
by the asset and provenance manifests.
