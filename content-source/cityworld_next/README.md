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

The v2 transition module closes a bridge corridor against terrain with a
continuous drivable slab, anchored modular expansion joint, framed drain
grates and underside scuppers, bearing pads and fasteners, panelled backwall,
truly flared wing walls, retaining toes, and material-factor weathering.
Guardrail posts expose their base plates, anchor heads, and reflectors while
the unchanged road and barrier collision proxies remain low-poly and
watertight. The deterministic 20,392 / 324 / 156 triangle render ladder keeps
all three LODs below their declared limits, and a wider studio camera shows the
whole transition without changing runtime lighting:

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

The first standalone CW1 fixture is the original, project-owned
`rorng_city_led_streetlight`. Its 4,548 / 396 / 132 triangle render ladder
provides three distinct LODs under the street-fixture budget. A separate,
simplified, welded watertight proxy encloses the foundation and pole for
collision. Texture-free PBR factors distinguish precast concrete, galvanized
steel, graphite powder coat, the lens gasket, and the warm emissive LED lens.
The export contains no runtime lights: the lens emits visually through its
material factor but does not synthesize a point light or change global
lighting.

The generator canonicalizes the exported GLB into stable primitive, accessor,
vertex, and triangle order, making the GLB byte-deterministic for the pinned
generator and Blender version. Compiler profile v1 supports one static factor
set per material only. Day, dusk, and night instance-material variants are
therefore unsupported; use the single checked warm emissive factor and never
invent runtime material switching:

- editable source:
  `fixtures/led_streetlight/rorng_city_led_streetlight.blend`;
- authoring preview:
  `fixtures/led_streetlight/rorng_city_led_streetlight_preview.png`;
- runtime-neutral interchange and contract:
  `../../resources/nextgen/cityworld/fixtures/led_streetlight/`.

The raised corridor uses a separate
`rorng_city_led_streetlight_bridge` variant. Its 0.4 m bridge flange fits the
0.45 m native parapet, while the render ladder and inward-facing luminaire
retain the checked streetlight proportions. The `static-visual-v1` profile
requires an explicit empty collision list: the native procedural parapet
remains the only collision authority. Each compiled ODEF carries one bounded
24 m warm point light at the lens, so sixteen alternating 40 m placements
brighten the flat raised deck without changing road physics:

- editable source:
  `fixtures/led_streetlight_bridge/rorng_city_led_streetlight_bridge.blend`;
- authoring preview:
  `fixtures/led_streetlight_bridge/rorng_city_led_streetlight_bridge_preview.png`;
- runtime-neutral interchange and contract:
  `../../resources/nextgen/cityworld/fixtures/led_streetlight_bridge/`.

The first project-authored building family covers the 40 highest-reuse
Penguinville storefront placements (`store02`, `store03`, `store05`, `store06`,
and `store08`) without placing them yet. Five exact-footprint variants provide
contemporary, heritage, market-hall, industrial-arcade, and gabled-townhouse
silhouettes. Each includes real-scale recessed glazing, frames and mullions,
doors, canopy, abstract project-owned signage, facade articulation, roof/HVAC
detail, three LODs, and a separate watertight collision envelope. Every render
and collision LOD begins at Z=0; the audited legacy one-metre subgrade envelope
is not reproduced. Exactly one selected-occupied-window material is emissive,
with no runtime point lights:

- family and read-only compatibility audit:
  `buildings/storefront_family/rorng_city_storefront_family.v1.json`;
- editable sources and previews:
  `buildings/storefront_family/rorng_city_storefront_*/`;
- runtime-neutral interchange, contracts, and compiled OGRE packages:
  `../../resources/nextgen/cityworld/buildings/storefront_family/`;
- detailed workflow and future placement gate:
  `../../doc/nextgen/CITYWORLD_STOREFRONT_FAMILY.md`.

Regenerate with the pinned Blender 5.2 LTS authoring version:

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
blender --background --factory-startup \
  --python tools/blender/cityworld_next/generate_led_streetlight.py -- \
  --output-root "$PWD"
blender --background --factory-startup \
  --python tools/blender/cityworld_next/generate_bridge_streetlight.py -- \
  --output-root "$PWD"
blender --background --factory-startup \
  --python tools/blender/cityworld_next/generate_cityworld_storefront_family.py -- \
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
python3 tools/validate_cityworld_asset.py \
  resources/nextgen/cityworld/fixtures/led_streetlight/rorng_city_led_streetlight.asset.json \
  --repo-root .
python3 tools/compile_cityworld_asset.py \
  resources/nextgen/cityworld/fixtures/led_streetlight/rorng_city_led_streetlight.asset.json \
  --repo-root . \
  --converter /absolute/path/to/OgreXMLConverter
python3 tools/compile_cityworld_asset.py \
  resources/nextgen/cityworld/fixtures/led_streetlight/rorng_city_led_streetlight.asset.json \
  --repo-root . \
  --validate-checked
python3 tools/validate_cityworld_asset.py \
  resources/nextgen/cityworld/fixtures/led_streetlight_bridge/rorng_city_led_streetlight_bridge.asset.json \
  --repo-root .
python3 tools/compile_cityworld_asset.py \
  resources/nextgen/cityworld/fixtures/led_streetlight_bridge/rorng_city_led_streetlight_bridge.asset.json \
  --repo-root . \
  --converter /absolute/path/to/OgreXMLConverter
python3 tools/compile_cityworld_asset.py \
  resources/nextgen/cityworld/fixtures/led_streetlight_bridge/rorng_city_led_streetlight_bridge.asset.json \
  --repo-root . \
  --validate-checked
python3 tools/validate_cityworld_storefront_family.py \
  content-source/cityworld_next/buildings/storefront_family/rorng_city_storefront_family.v1.json \
  --repo-root .
for manifest in \
  resources/nextgen/cityworld/buildings/storefront_family/*/*.asset.json
do
  python3 tools/validate_cityworld_asset.py "$manifest" --repo-root .
  python3 tools/compile_cityworld_asset.py "$manifest" \
    --repo-root . --validate-checked
done
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

Production compilation requires the pinned `OgreXMLConverter` executable at
the explicit absolute path; the compiler never searches `PATH`. The
`--validate-checked` command revalidates the committed portable package without
executing a host converter.

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
The current eight-entry deterministic ZIP references the separately installed
original `CityWorld.otc` and `CityWorld.tobj`; it does not copy either file or
any original map asset. Overlay v3 authenticates the two source road-object
placements and replaces the incomplete 192 m prototype with a continuous
1,075.448 m native construction alignment. It starts 14.8491 m inside
Penguinville's east carriageway, rises from the decoded 0.198 m road surface
to 0.31 m over 10 m, clears the decoded 0.30 m curb by 1 cm across the full
8.9 m driven mouth, and ends on NeoQueretaro's west carriageway. It closes at
the destination road height and heading, eases the surface offset over 40 m,
uses two 160 m ramps, raises the central deck 8 m, enables continuous
collision, and requests 47 terrain-reaching pillar stations. The overlap
functionally removes the curb from the connection without copying or modifying
the original private city mesh.

Its inventory is the generated descriptor, merged material script, procedural
placement, canonical local-only provenance report, and four collisionless
runtime resources for the placed bridge streetlight. The four Blender-authored
module families remain validated and reported but are unplaced and excluded
from the runtime payload because their existing ODEFs own collision. The v3
route continues to use RoR's native procedural road, barriers and
terrain-reaching pillars as the sole corridor collision authority. The
building-overlapping gateway is not placed. Sixteen alternating bridge
fixtures are mounted from station 234.8491 m through 834.8491 m, with exact
inward transforms and one checked warm point light per instance. The source
archive stays byte-identical and the generated package is explicitly not for
redistribution or shipping.

The generated descriptor mounts the original archive through:

```ini
[ResourceBundles]
Dependency = CityWorld.zip:CityWorld.terrn2:ebeac2f0204f25ca1955f29ca1583b2afa4517a3a848feb1db203814acac2ef3
```

This is an authenticated, direct, read-only runtime mount. Every dependency
must include a pinned 64-character lowercase SHA-256; unhashed dependencies are
rejected. The exact root-level terrain member selects one installed ZIP; it is
not parsed recursively. RoR streams and verifies the resolved archive
immediately before mounting it, and a read error or hash mismatch aborts before
terrain initialization. RoR also rejects missing, ambiguous, duplicate,
self-referential, unsafe, or non-ZIP dependencies, with limits of eight
entries, 512 bytes per entry, and 2,048 bytes total. The overlay's own resource
location remains first, so its project-owned resources win name collisions
while `CityWorld.zip` stays separate and unchanged.

The GLB is the canonical geometry interchange output and is expected to remain
byte-deterministic for the pinned Blender generator. `.blend` files and
rendered previews are editable/evidence artifacts whose exact bytes may change
between Blender sessions or versions; their current hashes are always pinned
by the asset and provenance manifests.
