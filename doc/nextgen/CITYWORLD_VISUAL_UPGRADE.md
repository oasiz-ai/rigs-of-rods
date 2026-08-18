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
- 39 bridge/elevated-road, 1,176 fixture, 31 vegetation, 156 building, and 405
  road placements under the second version of the explicit name classifier;
- 22 bridge, 96 fixture, 16 vegetation, and 146 building model files;
- 213 object definitions with collision blocks, 50 with authored LOD blocks,
  and zero authored point- or spot-light directives;
- one unresolved placed object definition, `pantallaQr`, which is content debt
  rather than a renderer failure.

The three authored teleport anchors give two useful city-to-city planning
distances:

1. Penguinville to NeoQueretaro: 2,067.758 metres.
2. NeoQueretaro to NeoQ2.0: 5,401.543 metres.

The physical road links do not use those spawn positions as endpoints. The
first starts on the authenticated Penguinville east carriageway at
`(480, 0.198, 370)`, overlaps 14.8491 m of the existing road and curb, and
continues from the authenticated curb edge at `(494.8491, 0.1, 370)` to the
authenticated NeoQueretaro west carriageway seam at
`(1380.966797, 0.1, 936.098389)`. The new source and destination surface points
are 1,064.053 metres apart. The generated centreline is 1,075.448 metres
because it preserves east tangents at both cities and includes the source
overlap.

The second link starts flush at NeoQueretaro's decoded east-distributor seam
at `(3790.970703, 0.1, 3993.104004)`, reaches NeoQ2.0's west industrial
distributor seam at `(6867, 0.2, 4018)`, and terminates flush there. Its
generated centreline is 3,076.132 metres. Both endpoints have zero generated
overlap, so each seam retains exactly one authoritative collision surface. The
direct Penguinville-to-NeoQ2.0 spawn distance remains 7,374.342 metres; links
are road corridors with bridge or elevated spans where terrain requires them,
not single spawn-to-spawn meshes.

Run the local audit with:

```bash
python3 tools/audit_cityworld_visuals.py \
  "$HOME/Library/Application Support/Rigs of Rods/mods/CityWorld.zip" \
  --expect-sha256 \
  ebeac2f0204f25ca1955f29ca1583b2afa4517a3a848feb1db203814acac2ef3 \
  --pretty --output /tmp/cityworld-visual-audit.json
```

### Local overlay package

`tools/build_cityworld_local_overlay.py` turns the pinned, user-supplied
archive and the checked project modules into one deterministic local test ZIP.
The output must be a new path outside the repository:

```bash
mkdir -p /tmp/ror-cityworld-local
python3 tools/build_cityworld_local_overlay.py \
  --archive "$HOME/Library/Application Support/Rigs of Rods/mods/CityWorld.zip" \
  --repo-root "$PWD" \
  --output /tmp/ror-cityworld-local/CityWorldNextLocalOverlay.zip \
  --surface-offset-m 0.08
```

The builder requires the exact pinned archive hash, audits ZIP paths and
telepoints without extracting the archive, authenticates both routes'
road-object placements and rotations in `CityWorld.tobj`, verifies both open
intercity placement-origin windows, hashes the Neo-to-NeoQ2.0 endpoint render
meshes, collision meshes, and ODEFs, validates all five asset manifests and
checked compiler outputs, and writes through a temporary sibling before an
atomic no-overwrite publish. The 50-entry ZIP contains a derived terrain
descriptor, a project-owned overlay TOBJ, the collisionless streetlight LOD
family, the direct Penguinville road-seam family, three replacement-tree LOD
families with 18 per-instance ODEFs, two authenticated manifests, one merged
material script, and one canonical report. The four earlier
Blender-authored corridor module families remain validated and reported but
are excluded from the runtime payload while their ODEFs still own collision. It
contains no original CityWorld geometry, placement, texture, object, or
archive payload. The descriptor references `CityWorld.otc` and
`CityWorld.tobj`, so the original `CityWorld.zip` must remain installed
separately.

The generated descriptor also declares the direct runtime mount explicitly:

```ini
[ResourceBundles]
Dependency = CityWorld.zip:CityWorld.terrn2:ebeac2f0204f25ca1955f29ca1583b2afa4517a3a848feb1db203814acac2ef3
```

`Dependency` values use an exact
`bundle.zip:terrain.terrn2:<64-lowercase-hex-sha256>` identity. Unhashed,
uppercase, malformed, and mismatched digests fail closed. Each member must be
a non-deleted, root-level terrain entry in a ZIP; partial and path-based
lookups are rejected. Immediately before each read-only mount, RoR streams the
resolved archive through SHA-256 and compares it with the authored digest. A
descriptor may declare at most eight dependencies, each at most 512 bytes and
at most 2,048 bytes in aggregate. Read failures, hash mismatches, missing,
ambiguous, duplicate, self-referential, unsafe, or unsupported dependencies
fail terrain loading before `Terrain::initialize()`.

The named terrain member is a selection anchor, not a recursively loaded
descriptor. Only its containing archive is mounted, so dependencies do not
form transitive chains or cycles. The original terrain archive remains a
separately installed, read-only local source and is appended to the derived
terrain's resource group. The derived overlay location therefore keeps
precedence if both archives contain the same resource name.

Overlay v5 retains the v3 runtime corridor that replaced the incomplete 192 m
prototype placement with a continuous
1,075.448 m, 8.9 m-wide construction alignment. At Penguinville, a
14.8491 m collision-authoritative asphalt apron begins at the legacy road
surface height of 0.198 m, rises to 0.31 m over 10 m, and crosses the decoded
0.30 m curb top with 1 cm clearance. The apron therefore removes the curb from
the driven bridge mouth without modifying or redistributing the private source
mesh; the original sidewalk and curb remain visible and collidable on either
side of the 8.9 m opening. A later rights-cleared direct-city pass may bake this
cut into an editable city mesh.

The destination closes at the exact existing-road height and heading with zero
reported three-dimensional gap. The 0.08 m anti-z-fighting surface offset
eases out over 40 m rather than creating a lip. Two 160 m smoothstep ramps hold
the analytic grade to 7.5 percent, the central deck is raised 8 m, and 47
bridge stations request RoR's terrain-reaching native pillars at no more than
20 m spacing. Collision generation is enabled for the complete procedural
road.

The former gateway is not placed: its audited footprint intersected
`officeblock04`. The route leaves the Penguinville edge road, crosses the
source archive's empty intercity placement-origin window, and joins
NeoQueretaro's western T-junction carriageway. The checked Blender bridge
family remains provenance-validated and reported but is unplaced and excluded
from the runtime payload; it is the candidate visual kit for the next pass,
not evidence that the construction alignment already uses high-detail deck
meshes.

The first route-safe Blender visual pass places sixteen
`rorng_city_led_streetlight_bridge` instances at 40 m spacing from station
234.8491 m through 834.8491 m, alternating sides and rotating each local `-Z`
arm toward the carriageway. Their 0.4 m flange fits the 0.45 m native parapet. The
`static-visual-v1` contract requires zero collision meshes, so the procedural
road and parapet remain the sole collision authority. Each ODEF carries one
validated warm point light with a 24 m range; the generated report records
every placement transform, light count, lateral offset, and collision
authority.

### NeoQueretaro-to-NeoQ2.0 road link

Overlay v5 authenticates `distribuidorQr` at source line 366 and
`NeoQ2-0industrial-zone-distributor-road` at destination line 1230, together
with their exact render mesh, collision mesh, and ODEF bytes. It also
authenticates line-378 `autopistaQr` at `(0,-0.4,0)` with rotation
`(90,0,90)` plus its exact mesh and ODEF. A 128 m-wide swept placement-origin
strip between the decoded road seams must remain empty. This is intentionally
stricter than selecting nearby city spawn points: endpoint, ground road, or
resource drift aborts the local build before a route is emitted.

Endpoint elevation comes from decoded collision surfaces, not raw TOBJ origin
height. `distribuidorQr` composes its 0.3 m runtime origin with a -0.2 m local
surface to produce the 0.1 m source seam. The NeoQ2.0 placement is authored at
50 m but the pinned compatibility transform grounds its runtime origin at
0 m; its decoded +0.2 m local surface therefore produces the 0.2 m
destination seam. The report records all four values and requires the route
surface to match the composed collision elevation exactly.

The v4 route is one 3,076.132 m continuous native procedural collision surface
with 80 waypoints. It begins exactly at NeoQueretaro's decoded east mesh edge
and terminates exactly at NeoQ2.0's decoded west mesh edge. Generated overlap
is zero at both ends, preserving the source surface plus the destination
median and both carriageways without coplanar double-contact strips. The main
deck is 24 m wide, then tapers over 160 m to the destination road's decoded
15.1 m inner-barrier span. Its final surface is exactly level at 0.2 m.
Position, vertical step, grade, yaw, and width-edge errors are all zero, and
the open-end collision contract omits all six transverse start/finish cap
triangles.

Two 160 m smoothstep ramps raise the central deck by 8 m. The sampled maximum
grade is 0.07039, below the 0.075 contract. An exact offline decode of
`autopistaQr.mesh` identifies 9,599 upward-facing live-road triangles in the
`calleunsolosentido` and `pavimento` submeshes. Prospective column footprints,
expanded by the 2.5 m heavy-truck clearance, intersect that surface at all 18
stations from 80 through 760 m; each entire pair is therefore authored
`bridge_no_pillars`, with no runtime skipping. The remaining 56
`bridge_side_pillars` stations use paired columns plus hammerheads at least
5 cm below the road slab. The build enumerates 168 support collision AABBs and
rejects any intersection with the swept bridge-road prism.

Thirty-three collisionless bridge fixtures alternate sides at 80 m spacing.
They share the checked `rorng_city_led_streetlight_bridge` resource, point
inward, and add one bounded 24 m warm point light each. Together with the first
route's fifteen fixtures, overlay v5 requests 48 project-owned local lights,
which remains below the runtime budget of 64.

The v4 native macOS arm64 gate validates this contract end to end. Six
byte-distinct 1280x720 UI-free captures show the live `autopistaQr` surface
without columns, paired piers beginning only after the excluded span, and the
wheel-height NeoQ2.0 handoff with no generated barrier or median coverage.
Native accounting reports the exact combined multiset
`requested=46 built=46 skipped=0` and
`requested=56 built=56 skipped=0`. A packaged DAF
crosses both city seams eastbound, then a separately spawned westbound DAF
crosses from the preserved positive-local-z carriageway back onto the generated
deck. The combined trace covers 3,161.36 m in 424,240 physics steps with
0.0822754 m maximum path error, 0.808374 m vertical error, and 0.00537109 m
maximum regression. Windows and Linux still require their own native runs.

### NeoQueretaro core relighting gate

Overlay v4 adds the first deterministic full-map relighting content slice
without changing runtime lighting yet. The v2 archive audit identifies all
779 explicit NeoQueretaro pole placements: 528 `luminariaLQr`, 239
`luminariaQr`, and 12 `luminariaYQr`. Exactly 67 poles lie within 400 m of the
authenticated `NeoQueretaro Spawn`: 42 single-arm and 25 dual-arm poles. The
three source ODEFs contain collision meshes, no authored LOD, and no point or
spot lights, so adding replacement pole objects would duplicate both visual
geometry and collision.

The local-only package therefore contains
`cityworld_next_neoq_core_lights.candidates.json`. It records one bounded warm
point-light candidate per existing pole, preserves the exact source transform,
uses a hard 24 m range ceiling, requests no shadow casting, and specifies a
future legacy-Z-up ODEF adapter whose mesh header is `none`. No adapter ODEF,
candidate placement, source mesh, or source texture is emitted. The report
records 67 derived placement records and continues to mark the package
nonredistributable and non-shippable.

Activation fails closed. Overlay v5 still emits zero NeoQueretaro
core-candidate point lights until that independently derived content is
promoted. The renderer now exposes the
`ror-cityworld-local-light-budget-v1` bounded-light policy used by the 49
project-owned bridge fixtures. The shared terrain-object path explicitly
disables shadow casting on every point and spot light and reports
`local_shadow_casters=0`; the candidate manifest records that zero-local-shadow
contract as satisfied. The candidate-family and whole-map family counts are
authenticated during every build; moving one pole across the 400 m boundary,
changing a family count, or changing any of the three exact collision-bearing
source ODEFs aborts publication. Promotion also requires a UI-free fixed-camera
RGB comparison, frame-time measurements, and native macOS, Windows, and Linux
loading. Until those gates close, this is a reproducible activation-ready
content contract, not evidence of completed relighting or ray tracing.
The corridor diagnostic accepts overlay report v5, independently validates all
67 candidate records and three pinned source-ODEF hashes, proves that no core
candidate adapter or placement entered the runtime payload, validates both
procedural corridors and all 48 bridge fixtures, and rebuilds the complete
50-entry ZIP byte for byte before launching the unchanged v3 Penguinville
corridor traversal.

Fixed ZIP order, timestamps, permissions, and stored payloads make repeated
builds byte-identical. The embedded report marks redistribution and shipping
false and records source/member hashes, authenticated anchor evidence,
tool/generator hashes, asset/compile provenance, measured endpoint errors,
grade, waypoints, support stations, target distance, and covered length.
Normal and optimized Python tests cover deterministic builds, hostile anchor
drift, occupied-gap rejection, seam closure, OGRE yaw orthogonality, ramps,
pillars, inward fixture orientation, collisionless ODEFs, Windows-reserved
output names, and no-overwrite publication. Those overlay tests now run in the
Linux, Windows, and macOS provenance matrix.

On the macOS arm64 rolling app, the earlier installed v5 runtime package reaches
`TERRAIN LOADING DONE`, passes the 10-frame bundle smoke, and shuts OGRE down
cleanly. A UI-free 1280x720 capture verifies a continuous asphalt mouth across
the original sidewalk and curb, with the curb retained only beside the road.
The prior Neo-to-NeoQ2.0 five-view run exercised the superseded centered-pillar
and destination-overlap prototype and is not acceptance evidence for v2. The
replacement gate now passes on macOS arm64 with six UI-free views, including
driver-height and underside views, exact native side-pier accounting, a full
DAF traversal into the preserved destination lane, and no endpoint-cap snag.
Native Windows and Linux repetitions remain open.

The v2 exploratory private-content diagnostic armed the packaged DAF at the
former report-declared Penguinville endpoint tangent and followed all 57
procedural centreline samples. That macOS arm64 run covered 1,086.34 m in
170,960 fixed-batch physics steps and held maximum centreline error to
0.912104 m.

The current combined corridor gate supersedes that v3 evidence. On macOS
arm64 it rebuilds the 50-entry overlay byte for byte, requires the exact
`46/46/0` and `56/56/0` native support summaries, validates all 48 bridge
lights, and captures four byte-distinct UI-free 1280x720 seam views. Forward
and reverse collision-enabled DAF traversals cover 2,146.23 m in 377,000 fixed
physics steps with 1.1988 m maximum path error, 0.808091 m vertical error, and
0.00591469 m regression. The route crosses the direct project-owned
Penguinville road seam without a curb obstruction and reaches the independent
NeoQueretaro road in both directions.

The optional Blender transition, curve, span, and gateway visual families
remain validated but unplaced; the accepted route uses the native procedural
deck and direct road seam. Whole-route building-clearance and visual/performance
review, rights-cleared distribution, and native Windows and Linux repetitions
remain promotion gates.

Run the rights-preserving diagnostic only with the explicit incomplete-content
acknowledgement:

```bash
python3 tools/run_cityworld_corridor_scene.py \
  --executable /Applications/RoR.app/Contents/MacOS/RoR \
  --cityworld-archive \
    "$HOME/Library/Application Support/Rigs of Rods/mods/CityWorld.zip" \
  --overlay-archive \
    "$HOME/Library/Application Support/Rigs of Rods/mods/CityWorldNextLocalOverlay.zip" \
  --artifact-dir /tmp/cityworld-corridor-runtime \
  --diagnostic-allow-incomplete-overlay
```

The diagnostic validates every overlay payload, independently rebuilds the
overlay byte for byte with the current generator, proves the 59 report
waypoints match the script, authenticates the packaged DAF entry, and uses an
ephemeral RoR home. It re-hashes every staged input before launch and publishes
the artifact directory atomically only after its report is complete. Its report
permanently records all four missing acceptance properties as false. Neither
private ZIP is retained in the artifact directory. Its standard-library
contract tests run under normal and optimized Python on Linux, Windows, and
macOS.

The complete NeoQueretaro-to-NeoQ2.0 bridge has a separate native acceptance
runner. It captures six UI-free views and then drives the packaged DAF from the
independently authored NeoQueretaro road, across both flush zero-overlap seams,
and ten metres into an authenticated live NeoQ2.0 lane. It fails on incomplete
or skipped paired supports, disabled collision, a median/lane departure,
missing seam markers, degenerate RGB, shader/renderer errors, or a
non-byte-identical overlay rebuild:

```bash
python3 tools/run_cityworld_neoq_bridge_scene.py \
  --executable /Applications/RoR.app/Contents/MacOS/RoR \
  --runtime-content /Applications/RoR.app/Contents/Resources/content \
  --cityworld-archive \
    "$HOME/Library/Application Support/Rigs of Rods/mods/CityWorld.zip" \
  --overlay-archive /tmp/ror-cityworld-local/CityWorldNextLocalOverlay.zip \
  --artifact-dir /tmp/cityworld-neoq-bridge-runtime
```

The runner uses isolated profiles for the static and drive scenes and
atomically publishes the six RGB files, both log sets, renderer/PSSM identity,
exact SidePiers counts, executable and input hashes, repository commit, and
acceptance report only after both native runs pass.

### Authenticated legacy material compatibility

The original archive remains byte-identical. Runtime compatibility is limited
to reviewed identities and fails closed:

- both the CityWorld ZIP SHA-256 and the individual material-script SHA-256
  must match before an in-memory edit is allowed;
- the opened OGRE script stream is byte-compared with the corresponding member
  of the authenticated package, avoiding basename or resource-group guesses;
- mesh requests follow OGRE's exact-case-before-case-insensitive archive
  precedence and are opened from the selected SHA-256-authenticated ZIP before
  a reviewed material alias or fallback can be applied;
- material aliases require the target to have been defined by the same
  authenticated archive SHA, and generated material fallbacks use stable names;
- procedural texture data is limited to seven exact archive-, script-, and
  directive-bound `.dds` replacements with collision-resistant generated
  resource names. Once authorized, those names are always served from the
  in-memory procedural payload and never delegated to a later resource
  location; stale or changed authorization aborts the load. The missing
  `parabusimagenlateral.jpg` reference is repaired into a texture-free lit
  pass so a JPEG request can never receive DDS bytes;
- the compatibility path performs no fuzzy matching, disk rewrite, or global
  diagnostic suppression.

The current macOS arm64 runtime gate applies exact edit counts of 1, 1, 2, 4,
2, 47, and 5 to seven reviewed scripts, for 62 edits total. The
`NeoQueretaro.material` plan clears the authenticated redundant
`concretorojo` block at original lines 1772-1784 while preserving its first
definition at lines 1698-1710. It also converts the two authenticated legacy
environment-map pairs to OGRE 14 syntax, removing the blank environment layers
from `parabus` and `semaforogris3` and eliminating both associated deprecation
warnings. The replacement declarations retain the cube type, zero mipmaps,
and `PF_R8G8B8` format of RoR's manually created `EnvironmentTexture` render
target, so they do not redeclare that shared texture with conflicting
parameters. It then resolves
23 reviewed aliases, creates 11 reviewed lit fallbacks, loads the one generated
4x4 DDS resource demanded by this terrain path, and uses GL3Plus RTSS programs.
The local overlay reaches `TERRAIN LOADING DONE` with zero CityWorld script
errors, zero missing-material warnings in its authenticated resource group, no
request for the absent JPEG, and a clean OGRE shutdown. Two pre-existing
missing-material warnings remain in the unrelated `MeshesRG` group. Native
Linux and Windows runtime confirmation is still required before this becomes a
shared cross-platform acceptance gate.

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
- future day, dusk, and night material variants without embedded runtime
  scripts, after a versioned compiler/runtime contract supports them.

The existing map has 71 `parabusQr`, 39 `fancytrafficlight5`, 31
`fancytrafficlight`, and 26 `busstopNJT` placements, so replacements for those
families have high visual leverage. Until PSSM-compatible local light casting
is implemented, fixtures use physically plausible emissive surfaces and the
directional sun; they must not disable global shadows merely to cast a point
light.

The first bounded CW1 asset is implemented as the original, project-owned
`rorng_city_led_streetlight`. Its metre-scale Blender source and deterministic
GLB contain LOD0/LOD1/LOD2 render objects at 4,548, 396, and 132 triangles,
plus one separate simplified, welded watertight collision proxy. Texture-free
PBR factors cover precast concrete, galvanized steel, graphite powder coat, a
lens gasket, and a warm emissive LED lens. The export declares no runtime
lights; the emissive lens is a surface material, not an implicit point light.
The generator canonicalizes primitive, accessor, vertex, and triangle ordering,
and consecutive runs with the pinned generator and Blender version produce the
same GLB bytes.

Compiler profile v1 accepts exactly one static factor set per material.
Day/dusk/night instance variants and runtime material switching are not
supported by that contract. This fixture therefore uses one checked warm
emissive factor in every context; loaders and placement code must never invent
a variant toggle or synthesize runtime switching. Adding variants requires a
new versioned compiler and runtime contract. Reproduction, validation, and
offline compilation commands are recorded in the
[editable-source README](../../content-source/cityworld_next/README.md).

The bridge-mounted derivative is implemented as
`rorng_city_led_streetlight_bridge`. It retains the three render LODs and warm
emissive lens, narrows the base to a parapet-safe flange, removes the collision
proxy entirely, and adds one checked 24 m point light. This is a distinct
`static-visual-v1` asset because the native bridge parapet owns collision.

The first bounded local-light slice is attached to the project-owned gateway
block: eight versioned warm point lights share the eight emissive luminaires.
The offline validator rejects unknown types, duplicate identifiers, non-finite
positions or colours, out-of-range colours, unsafe ranges, and more than 32
lights per asset. The compiler records the Blender-to-OGRE transform and emits
stable ODEF point-light records. The dependency-free sky applies a bounded
ambient contribution equal to 0.35 times the terrain ambient tint. ODEF point
and spot lights are explicitly unshadowed, leaving the directional sun as the
sole PSSM shadow caster. Runtime markers report both the fallback lighting
policy and the observed local shadow-caster count so native GL3Plus and D3D11
scene gates fail if that contract regresses.

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
new kit uses project-owned names and geometry. Overlay v5 now gives the longer
NeoQueretaro-to-NeoQ2.0 corridor an authenticated native procedural
construction alignment; authored high-detail deck and abutment replacement
remains a later visual pass.

The first-link topology is now complete as the v3 native procedural
construction alignment described above. That deliberately separates two
risks: exact road-to-road connectivity, elevation, collision and support
placement can be driven and reviewed now, while Blender-authored ramp, pier,
deck-detail, fixture and building-adjacency meshes replace the generic
construction visuals without changing the authenticated route. The visual pass
must preserve the v3 centreline, lane width, curb-free source overlap,
destination seam, maximum grade and continuous collision surface.
The same rule applies to the v5 second link: future authored visuals must
preserve its exact zero-overlap city seams, 24 m driven width, grade ceiling,
and single native collision surface.

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
The checked connector solver now assembles three curves, the 12 m transition,
and the 40 m gateway with exact position/tangent continuity. The installed
macOS arm64 app physically drove the DAF through all five modules over
137.569 m with 1.43999 m maximum path error. The building-canyon capture proves
the truck, façades, windows, trees, fixtures, emissive lenses, and all eight
dynamic point lights render together. Full retaining-wall families,
rights-cleared map-overlay placement, Windows/Linux physical execution, the
remaining production camera anchors, and declared frame-time gates are still
required.

The gateway's bounded v2 art pass raises close-range depth without changing
that runtime contract. Its four façades now include exterior window frames,
storefront doors and mullions, balconies, pilasters, bands, parapets, and
stepped rooftop penthouses. The eight deterministic tree instances use three
shape variants, tapered trunk sections, radial branches, and varied lobed
canopies instead of stacked cylinders. LOD0/LOD1/LOD2 contain 32,092, 3,596,
and 276 triangles: 53.5% of the declared close-detail ceiling, then 11.2% and
0.86% of LOD0. The three collision meshes, exact connectors, and eight point
lights remain unchanged. A balanced preview-only fill makes the branch and
street-level depth legible without changing runtime PSSM. No external geometry
or textures are used, and this is a reproducible CityWorld milestone rather
than a claim of AirSim parity.

The Blender 5.2 exporter can share accessors and vary same-material component
order, so the v2 generator closes that authoring nondeterminism before hashing
the GLB. It rebuilds independent accessors per primitive, retains a one-to-one
mapping for every referenced vertex, preserves raw positions and accessor
bounds, and only cyclically rotates triangles before stable sorting, which
preserves winding. Two consecutive full generations produced byte-identical
GLB output; the asset validator then rechecked LOD budgets, render attributes,
collision manifoldness/winding, connectors, materials, and runtime lights.

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
which overlay v4 now replaces atomically in place with the checked round,
columnar, and windswept family. Exact archive and TOBJ authentication plus an
all-wrapper runtime preflight preserve the 18 source positions and prevent
legacy/replacement duplicates. Each deterministic selector scale is applied to
both render and trunk collision through a portable ODEF wrapper. The larger
grouped tree meshes follow. Vegetation instancing and temporal stability must
be measured before placement density rises.

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

The first low-rise family is now asset-ready but deliberately unplaced. It
audits the 40 combined `store02`, `store03`, `store05`, `store06`, and `store08`
placements and supplies five independently authored exact-footprint variants.
The 20,296–62,416 triangle LOD0 range preserves close facade depth, while
LOD1/LOD2 fall to 660–792 and 120–276 triangles. All render and collision
objects begin at Z=0, so the legacy one-metre subgrade envelope is not carried
forward. The family has no runtime lights; only selected occupied windows use
the portable core emissive factor. See
[CityWorld Next storefront family](CITYWORLD_STOREFRONT_FAMILY.md).

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

That last criterion now has a measurement seam and an executable gate: see the
[playable frame-time budget gate](PLAYABLE_PERFORMANCE_GATE.md). It records a
bounded frame-interval distribution from the render loop's own delta time,
verifies from the runtime's own statement that the requested graphics preset
actually took effect, and fails closed on a limiter, a rejected interval, a
mid-run scene change, or a renderer fault. The first recorded macOS arm64
results are in
`evidence/PLAYABLE_PERFORMANCE_M5_2026-08-17.json`.

Those results are a measurement of the OGRE 14 compatibility executable
(`RoR-Ogre14`) against the unmodified CityWorld archive without the local
overlay. They are not an Ogre-Next result, not a full-map traversal, and not
yet a stable acceptance threshold: repeated identical runs spread widely enough
that the gate should block only over a repeated set whose worst run passes. The
run-to-run spread, native Windows and Linux repetitions, and the perceptual and
temporal camera gates above all remain open.

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
