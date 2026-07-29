# CityWorld Visual Upgrade

This workstream raises CityWorld's visual quality without treating the
user-supplied map as redistributable project content. The original
`CityWorld.zip` stays unchanged. Project-authored models, editable Blender
sources, conversion metadata, tests, and an overlay builder can be published
only when their own provenance passes the repository content audit. A local
derived terrain package may reference the exact user archive during
development, but it must not be committed or shipped.

## Measured baseline

The pinned local archive is 158,845,395 bytes with SHA-256
`ebeac2f0204f25ca1955f29ca1583b2afa4517a3a848feb1db203814acac2ef3`.
`tools/audit_cityworld_visuals.py` validates ZIP paths, encryption, expanded
sizes, compression ratios, case collisions, and the expected digest before it
reads the terrain and placement metadata. Its JSON has no timestamp or host
path and is byte-deterministic for the same input.

The 2026-07-28 baseline contains:

- 1,411 archive entries, 499 model files, 266 object definitions, 20 material
  scripts, and 618 textures;
- 1,922 active placements using 211 unique placement records;
- 39 bridge/elevated-road, 397 fixture, 31 vegetation, 106 building, and 405
  road placements under the first version of the explicit name classifier;
- 22 bridge, 86 fixture, 16 vegetation, and 93 building model files;
- one unresolved placed object definition, `pantallaQr`, which is content debt
  rather than a renderer failure.

The three authored teleport anchors give two useful staged transport links:

1. Penguinville to NeoQueretaro: 2,067.758 metres.
2. NeoQueretaro to NeoQ2.0: 5,401.543 metres.

The direct Penguinville-to-NeoQ2.0 distance is 7,374.342 metres and is not the
first construction target. A link is a road corridor with bridge or elevated
spans where the terrain requires them, not a single multi-kilometre mesh.

Run the local audit with:

```bash
python3 tools/audit_cityworld_visuals.py \
  "$HOME/Library/Application Support/Rigs of Rods/mods/CityWorld.zip" \
  --expect-sha256 \
  ebeac2f0204f25ca1955f29ca1583b2afa4517a3a848feb1db203814acac2ef3 \
  --pretty --output /tmp/cityworld-visual-audit.json
```

## Delivery order

### CW0 — Stable light and capture baseline

Complete. An unavailable saved Caelum selection now resolves to the
dependency-free sky, directional sun, ambient light, and fog path. The signed
arm64 rolling app loads CityWorld, captures after 120 warmup frames, and exits
cleanly after 180 frames. The visual capture deliberately leaves content
diagnostics visible.

### CW1 — Original fixture kit

Author a compact, project-owned kit before changing placement density:

- one modular LED streetlight with emissive lens, pole, base, and simplified
  collision;
- one traffic-signal family with shared pole and signal-head materials;
- one bus shelter, bench, bollard, hydrant, and wayfinding-sign family;
- day, dusk, and night material variants without embedded runtime scripts.

The existing map has 71 `parabusQr`, 39 `fancytrafficlight5`, 31
`fancytrafficlight`, and 26 `busstopNJT` placements, so replacements for those
families have high visual leverage. Until PSSM-compatible local light casting
is implemented, fixtures use physically plausible emissive surfaces and the
directional sun; they must not disable global shadows merely to cast a point
light.

### CW2 — Intercity corridor and bridge kit

Build the Penguinville-to-NeoQueretaro corridor first. Use short modular pieces
with deterministic placement transforms:

- tangent and curved deck spans, expansion joints, barriers, drains, signs,
  lamp mounts, and underside service detail;
- pier, abutment, retaining-wall, and transition pieces;
- separate simplified continuous collision surfaces with outward normals and
  no intersecting faces;
- LODs and far silhouettes that preserve lane alignment and bridge profile.

The local map already has 20 `elevatedhighway` placements, nine standard
pillars, three wide pillars, two ramps, one curve, and one on-ramp. Those are
compatibility references, not sources for a redistributable replacement. The
new kit uses project-owned names and geometry. The longer
NeoQueretaro-to-NeoQ2.0 corridor starts only after the first link passes
collision, navigation, visual, and performance gates.

The first project-owned tangent module is checked in as
`rorng_city_bridge_span_20m`, and the first curve as
`rorng_city_bridge_curve_left_15deg_20m`. Their Blender 5.2 generators produce:

- an editable, metre-scale Blender source and a 1280x720 authoring preview;
- one standard Y-up glTF 2.0 GLB with applied transforms and no imported
  scripts, shaders, textures, cameras, lights, animation, or extensions;
- LOD0/LOD1/LOD2 render objects at 4,636, 300, and 48 triangles;
- a continuous watertight road collision box and separate watertight left and
  right barrier collision boxes, all with outward winding and non-overlapping
  bounds;
- exact start/end connector metadata, an 8.9 m road width, and two 3.5 m
  lanes; the curve additionally pins a 20 m centreline, 15-degree heading
  change, 76.394372684 m radius, and 19.942933147 m chord;
- an integrated reinforced-concrete pier, hammerhead, bearings, expansion
  joints, four LED fixtures, and an emissive material on the curved span; and
- a canonical asset manifest plus A0 release-gate provenance for the GLB and
  manifest.

`tools/validate_cityworld_asset.py` reads the GLB container and accessors
directly. It checks finite data, required normal/UV/tangent streams, exact PBR
material coverage, LOD ratios, connector continuity, welded collision
manifoldness, winding, connectedness, and artifact hashes. The GLB was
byte-identical across consecutive Blender 5.2 arm64 generations. Blender
sources and rendered previews remain pinned artifacts rather than
cross-version byte-canonical formats.

![First project-owned CityWorld bridge span](../../content-source/cityworld_next/bridge/rorng_city_bridge_span_20m_preview.png)

![First project-owned CityWorld curved bridge span](../../content-source/cityworld_next/bridge/curve_left_15deg/rorng_city_bridge_curve_left_15deg_20m_preview.png)

These are the first CW2 modules, not completion of the intercity corridor.
The checked connector solver now assembles three curve modules with exact
position/tangent continuity, and the signed macOS arm64 app physically drove
the DAF through the 45-degree corridor with 1.41593 m maximum path error.
Abutments, retaining walls, transitions, mixed-module map placement,
Windows/Linux physical execution, production fixed-camera captures, and the
declared frame-time gates remain required.

Both spans pass the production offline scene-compiler boundary. Each checked
runtime package contains three OGRE render LOD meshes, three separate collision
meshes, an ODEF, deterministic material fallback, and a canonical conversion
report. The curve's LED lens is carried through core glTF `emissiveFactor` into
the generated OGRE `emissive` pass. The compiler pins OGRE 14.5.2, little-endian
`MeshSerializer_v1.100`, stable submesh/material identifiers, explicit
80 m/180 m manual LOD distances, and the tested Blender-to-glTF-to-OGRE basis.
Cross-platform CI regenerates and hashes the deterministic XML lowering,
validates the checked binary/package records without executing a host converter,
and fails on stale or unknown files. See
[CityWorld Next offline scene compiler](CITYWORLD_SCENE_COMPILER.md).

### CW3 — Vegetation

Replace the repeated billboard-era trees with a small bioclimatically coherent
library. Each species has:

- a wind-ready trunk/canopy hierarchy and authored normals;
- at least three deterministic shape variants;
- close, medium, far, and impostor LODs with stable silhouettes;
- alpha-tested foliage with mip-safe edge treatment;
- a trunk-only or capsule-like collision proxy where vehicle contact matters;
- seasonal/tint variation driven by instance data rather than copied textures.

The first replacement target is the 18 individually placed `arbol1Qr` objects,
followed by the larger grouped tree meshes. Vegetation instancing and temporal
stability must be measured before placement density rises.

### CW4 — Buildings

Start with one low-rise modular facade set and one skyline landmark set. Preserve
real scale, door/window rhythm, roof silhouettes, and street-level parallax.
Every building provides:

- reusable facade modules and trim instead of one giant baked texture;
- albedo, normal, occlusion/roughness/metalness, and emissive source maps;
- authored tangents, weighted normals, and at least three LODs;
- simple, continuous collision geometry distinct from the render mesh;
- optional interiors only for approved, bounded entry zones.

The first high-reuse targets are the store and townhouse families. High-rise
and skyscraper replacements follow once the material pipeline can render their
glass, metal, emissive windows, and reflections consistently.

## Blender and interchange contract

Blender is an authoring tool, not a runtime dependency. The Blender MCP may
create and inspect assets when connected, but all outputs must be reproducible
headlessly from checked-in scripts and manifests.

- Author in metres and apply location, rotation, and scale before export.
- Keep render, collision, and each LOD as separate named objects.
- Use stable lowercase ASCII asset identifiers prefixed with `rorng_city_`.
- Preserve the editable `.blend` source and export glTF 2.0 as the neutral
  interchange artifact. The offline scene compiler owns glTF validation,
  coordinate conversion, texture transcoding, OGRE mesh generation, material
  fallback generation, and canonical hashes.
- RoR is Y-up while Blender is Z-up. The legacy OGRE export path requires the
  documented `xz-y` axis swap; the offline compiler must encode and test the
  equivalent transform rather than relying on an artist checkbox.
- Never import executable scripts or third-party shaders from a scene package.
- Record Blender version, generator revision, source hash, export settings,
  compiler revision, output hashes, author, license, and redistribution
  evidence for every asset.

The current compiler profile intentionally requires every object transform to
be applied and rejects hierarchy, extensions, animation, morph targets, unknown
attributes, malformed accessors, and any unowned output. The glTF export already
performs Blender's `(x, y, z) -> (x, z, -y)` rotation; glTF and OGRE are both
Y-up, so the compiler asserts an identity interchange-to-runtime transform
instead of applying the axis swap twice.

The current official Blender mesh guide is written around Blender 2.79 and
points newer Blender users to `blender2ogre`; this project instead treats glTF
as the stable source interchange and OGRE mesh as a compiled runtime artifact.
That keeps authoring modern and prevents platform-specific Blender add-ons from
becoming part of the game runtime.

## Initial budgets and gates

Budgets are per authored asset family and are tightened from measured captures;
they are not permission to spend the entire budget on every instance.

| Asset | LOD0 triangle ceiling | Required reduction |
| --- | ---: | --- |
| Street fixture | 12,000 | LOD1 <= 35%, LOD2 <= 10% |
| Hero tree | 35,000 | LOD1 <= 40%, LOD2 <= 12%, impostor |
| Modular low-rise | 80,000 | LOD1 <= 35%, LOD2 <= 10% |
| Landmark building | 180,000 | LOD1 <= 30%, LOD2 <= 8% |
| 20 m bridge span | 60,000 | LOD1 <= 35%, LOD2 <= 10% |

No CityWorld visual milestone is complete until:

- the original archive remains byte-identical and the local overlay is
  reproducible from a pinned manifest;
- every new asset passes provenance, glTF validation, finite-transform,
  material-completeness, texture-colour-space, tangent, LOD, and collision
  checks;
- no white, pink, or black missing-material fallback is visible and the log has
  no new missing-material, renderer API, or shader diagnostic;
- bridge lanes are continuous, collision proxies do not snag or tunnel a
  vehicle, and all intended clearance envelopes pass;
- fixed cameras at each city anchor, both corridor approaches, a bridge deck,
  a tree-lined street, and a building canyon pass perceptual and temporal
  regression gates;
- the macOS arm64 high preset sustains 60 FPS at 1920x1080 with p95 frame time
  at or below 18.3 ms on the declared reference machine, followed by native
  Windows and Linux validation before the feature becomes a shared default.

Hardware ray tracing remains optional backend work after this raster baseline.
The portable fidelity path is dynamic sun/ambient lighting, PSSM shadows,
HDR/PBR materials, reflection probes, instancing, LODs, atmosphere, and
post-processing. Metal, DirectX 12, and Vulkan ray-tracing implementations may
later accelerate selected shadows/reflections, but CityWorld content must not
depend on one platform's ray-tracing API.

## References

- [RoR Blender mesh editing](https://docs.rigsofrods.org/tools-tutorials/blender-mesh-editing/)
- [RoR terrain object placement](https://docs.rigsofrods.org/terrain-creation/editing-terrain-objects/)
- [RoR collision meshes](https://docs.rigsofrods.org/terrain-creation/collision-meshes/)
- [RoR object definitions](https://docs.rigsofrods.org/terrain-creation/object-format/)
- [Blender glTF 2.0 exporter](https://docs.blender.org/manual/en/3.3/addons/import_export/scene_gltf2.html)
