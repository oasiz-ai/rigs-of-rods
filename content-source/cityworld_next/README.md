# CityWorld Next editable sources

This directory contains project-authored source and evidence for the
rights-cleared CityWorld visual upgrade. It does not contain or derive geometry
from the user-supplied `CityWorld.zip`.

The first bridge family contains a 20 m tangent span and a 20 m-centreline,
15-degree left curve:

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

Regenerate with Blender 4.0 or newer:

```sh
blender --background --factory-startup \
  --python tools/blender/cityworld_next/generate_bridge_kit.py -- \
  --output-root "$PWD"
blender --background --factory-startup \
  --python tools/blender/cityworld_next/generate_curved_bridge.py -- \
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
python3 tools/solve_cityworld_bridge_corridor.py \
  --asset resources/nextgen/cityworld/bridge/curve_left_15deg/rorng_city_bridge_curve_left_15deg_20m.asset.json \
  --asset resources/nextgen/cityworld/bridge/curve_left_15deg/rorng_city_bridge_curve_left_15deg_20m.asset.json \
  --asset resources/nextgen/cityworld/bridge/curve_left_15deg/rorng_city_bridge_curve_left_15deg_20m.asset.json \
  --entry-x 512 --entry-z 482 --heading-degrees 0 --format tobj
python3 tools/build_cityworld_next_provenance.py --repo-root .
python3 tools/build_cityworld_next_provenance.py --repo-root . --check
python3 tools/content_provenance_audit.py \
  --manifest content-source/cityworld_next/provenance/cityworld_next.manifest.json \
  --inventory content-source/cityworld_next/provenance/cityworld_next.inventory.json \
  --release-gate \
  --package-root resources/nextgen/cityworld \
  --editable-root .
```

The GLB is the canonical geometry interchange output and is expected to remain
byte-deterministic for the pinned Blender generator. `.blend` files and
rendered previews are editable/evidence artifacts whose exact bytes may change
between Blender sessions or versions; their current hashes are always pinned
by the asset and provenance manifests.
