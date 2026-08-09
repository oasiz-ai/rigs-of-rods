# Renderer-neutral boundary

This directory is the versioned, portable C++17 data and interface boundary
between simulation/gameplay producers and renderer frontends. Public contracts
use only standard C++ and the types defined here. Backend adapters may include
OGRE, Ogre-Next, Metal, D3D12, or Vulkan headers in their own implementation
directories, but those types must never appear in this boundary.

`SceneSnapshot` is an immutable, validated deep copy shared as
`std::shared_ptr<const SceneSnapshot>`. Snapshot-facing assets use stable
128-bit `RenderAssetId` values plus exact nonzero revisions. They are portable
across processes, recordings, and frontend instances; frontend-local
`ResourceHandle` values never appear in scenes or materials. This means the
same snapshot can be submitted independently to OGRE 1.14 and Ogre-Next.

`RenderAssetRegistry` applies strictly sequenced transactions atomically.
Incremental deltas advance the catalog by exactly one; full snapshots contain
every live record and permanent tombstone so a fresh or device-recovered
frontend can rebuild without replaying history. Asset IDs never change kind or
return after tombstoning. Same-sequence full replay is accepted only when every
descriptor is identical. Incremental transactions share immutable payloads for
unchanged assets, so updating one material does not duplicate the entire mesh
and texture catalog. Materials resolve exact texture/sampler revisions, and
dependency validation prevents a transaction from retiring assets that a live
material still needs. Optional asset references use one exact all-zero absent
value; partially populated invalid references fail closed. Each frontend maps
this logical catalog to its own generation-checked handles and retains old
physical revisions while submitted frames lease them.

Successful frame submission leases every referenced asset revision through
frame completion. Catalog updates and tombstones prevent future scenes from
using older revisions, while physical destruction waits for material
dependencies, in-flight frames, and native exports. The only public resource
handles transferred to a caller are optional frontend-owned output attachments;
native objects remain optional, borrowed interop.

Teardown order is contractual: stop submissions, shut down every native RT
backend (which ends external frames and releases geometry), drain frontend
frames, then shut down the frontend. A frontend with outstanding external
leases returns `OUTSTANDING_LEASES`; a timeout leaves the device initialized so
the caller can release/drain and retry. Native context tokens remain valid until
shutdown succeeds and are revoked together on success.

Frontend execution has one explicit owner/render thread established by
`Initialize()`. Every frontend, native-interop, and native-RT interface call is
serialized there through shutdown. Platform window callbacks are converted to
immutable surface updates and queued to that thread; borrowed handles stay live
until the transactional update releases them. A host that requires Cocoa work
on the application main thread selects that thread as the owner.

The boundary uses right-handed meter units, +Y up, camera-forward -Z,
column-major matrices applied to column vectors, and non-reversed [0, 1] clip
depth. Each snapshot carries a double-precision absolute origin; all float
transforms, cameras, lights, particles, and prior-frame values are rebased to
that current origin. Camera requests keep rigid right-handed view matrices and
canonical unjittered perspective/orthographic projections separate for correct
metric PBR, depth, and motion without unsafe backend decomposition. Temporal
jitter is a separate half-pixel-bounded image displacement. Backends own
API-specific projection and framebuffer remapping. Rendered outputs are
validated against the exact request, including explicit SDR or linear HDR color
format, frame/snapshot IDs, view/output pairs, extents, presentation state, and
attachment completeness. Texture rows/UVs, cube faces, material channel
packing, triangle winding, depth, motion, object-ID, stable 128-bit material-ID,
and normal attachments also have one canonical interpretation so Metal, D3D12,
Vulkan, OGRE, and Ogre-Next adapters cannot silently diverge. Requests are
validated against the selected frontend's output mask, limits, HDR, deformable,
particle, and asynchronous-compute capabilities before backend submission.

Snapshots are self-contained for deformation: every non-base revision carries
one full vertex-stream state and exact bounds, so out-of-order or repeated
rendering never depends on cached partial uploads. Newly created snapshot IDs
and particle-event IDs increase globally. A frontend emits each event only on
the first successful submission of its snapshot; repeats and multi-view renders
do not duplicate smoke, dust, sparks, or other effects.

Scene snapshot version 4 retains the version-3 immutable analytic lighting in explicit
photometric units. `color_linear` is a non-black, unit-photopic-luminance
linear-sRGB Rec.709 D65 chromatic multiplier; directional `intensity` is lux
and point/spot `intensity` is candela. This prevents color magnitude from
silently rescaling the scalar photometry. Local attenuation and spot half-cones
are exact. Static/dynamic shadow participation is a bit mask, and geometry
class comes only from the referenced `MeshResourceDescriptor::dynamic` bit;
instance motion, deformation revision, and update presence never reclassify it.
Stable sorted identities carry current and previous position/direction; point
lights have a canonical orientation and directional lights have canonical zero
local-only fields. Environment state adds ambient radiance, optional linear HDR
texture radiance, a fully specified additive gradient sky/sun disk tied to a
directional-light identity, and bounded EV compensation. Sun-disk membership
normalizes the view and emitted-ray directions, uses `-direction` as the disk
center, and includes the exact angular boundary. Effective view exposure must
remain a positive normal binary32 value, avoiding backend-dependent subnormal
handling. A padding-free little-endian version-two FNV-1a digest covers that
exact state plus the asset-registry identity and folds signed zero for portable
change detection. It is not a security hash.

Version 4 additionally carries a strictly ordered set of absolute-world
reflection-probe descriptors. Its separate version-one FNV-1a digest covers
authored probe identities, revisions, orientation, correction/influence boxes,
capture policy, and binary64 position while deliberately excluding snapshot
identity, simulation time, capture generations, and the float render origin.
That separation lets origin rebasing preserve static probe lineage while the
native plan still binds the exact derived render-relative transform.

`GraphicsSceneSnapshotProducer` version 4 accepts those lights and absolute-world
reflection probes in arbitrary
source traversal order, canonicalizes them, rebases local-light history, and
enforces permanent identity/type/revision tombstones. It also accepts complete
dynamic section states under independent limits of 65,536 objects, 16 Mi
vertices, and 512 MiB of copied position/normal/tangent/velocity storage per
candidate. These aggregate limits are checked without integer wrap before any
producer state commits. Once the entire asset, scene, lighting, environment,
camera, and dynamic-inventory transaction commits, it release-publishes the exact
immutable owner returned to the caller. Concurrent consumers acquire-load either
the previous complete scene or the next complete scene; a failed production
publishes nothing. The atomic observer pointer does not replace the ordered
production result: renderer submission still carries that scene's asset delta and
camera together. Calls into a producer, including observer loads, must quiesce
before producer destruction starts; immutable snapshot owners already acquired by
readers remain valid independently.

There is no implicit lighting/schema migration. Scene snapshot versions 1, 2, and 3
and joined-producer input versions 1 and 2 are rejected with `UNSUPPORTED_VERSION`.
An adapter migrating legacy light colors must normalize its non-black
linear-sRGB color with `NormalizePhotometricColorLinear`, retain the scalar
lux/candela value separately, populate all version-4 sky/exposure fields and an
explicit (possibly empty) reflection-probe set, and submit producer version 3.
Old lighting hash values are not comparable with the current version-2 digest
because the scene schema, registry identity, and calibrated photometry are part
of the contract.

## Cross-process scene, asset, and input transport

`RenderTransportEnvelope` version 1 is the deterministic, fail-closed wire
edge shared by isolated render processes. Message kind `1` carries one complete
`SceneSnapshot` version 4 plus one `CameraViewRequest` under render-frame
contract version 2. Message kind `2` carries one complete `RenderAssetDelta`
version 1. Reverse-direction message kind `3` carries one input-event batch
version 1 from the renderer/window host to the game process. Reverse kind `4`
is a cumulative presentation acknowledgement and reverse kind `5` is a
lifecycle control command. Typed decoders publish immutable owners only after
the entire candidate passes framing, digest, allocation, semantic, registry,
and exact-consumption validation.

The fixed 64-byte header is independent of host structure packing:

| Offset | Bytes | Encoding | Meaning |
| ---: | ---: | --- | --- |
| 0 | 8 | bytes | ASCII `RORSCN01` magic |
| 8 | 2 | little-endian `u16` | transport version (`1`) |
| 10 | 2 | little-endian `u16` | header size (`64`) |
| 12 | 2 | little-endian `u16` | message kind (`1` scene, `2` assets, `3` input, `4` ACK, `5` control) |
| 14 | 2 | little-endian `u16` | reserved flags (`0`) |
| 16 | 8 | little-endian `u64` | strictly ordered sequence |
| 24 | 8 | little-endian `u64` | exact payload byte count |
| 32 | 32 | bytes | SHA-256 of the exact payload |

`RenderTransportStreamDecoder` version 1 reconstructs these atomic envelopes
from pipes or other arbitrary byte streams without assuming read boundaries.
It validates the complete fixed header before reserving the declared payload,
enforces the lower of an immutable caller cap and the typed 128-byte control,
4 MiB input, 64 MiB scene, or 640 MiB asset cap, consumes no bytes from a
coalesced following frame, and exposes at most one validated frame at a time.
Callers must take that frame before continuing. Invalid framing, corruption,
allocation failure, or EOF inside a frame permanently poisons the decoder;
there is deliberately no magic-byte resynchronization that could conceal lost
state or break the shared scene/asset sequence lineage.

The scene payload carries every current scene field: identities and simulation
time, absolute origin, environment and analytic sky, mesh instances, current
and previous lights, reflection probes, full dynamic-mesh updates, particle
events, and camera history/jitter/exposure. Scene collections retain their
validated strictly increasing identity order, so no map or backend traversal
can reorder them.

The live OGRE 14 adapter samples only after
`GfxScene::BufferSimulationData()` has copied simulation state and
`GfxScene::UpdateScene()` has consumed those copies and joined flex/wheel work.
Source version 2 requires a nonzero post-update epoch exactly equal to the joined
buffer epoch; a capture attempted between those boundaries fails with a sequence
mismatch before identity, lifecycle, or cache state can change. `FlexBody`,
`FlexObj`, `FlexMesh`, and `FlexMeshWheel` expose copies of their private,
fully joined CPU graphics staging only. Capture never reads `Actor`, `NodeSB`,
solver, or hardware-buffer vertex state. Its constant ambient conversion is
an explicit numeric compatibility calibration: one renderer-linear OGRE 14
ambient unit equals one scene-radiance unit, with identity intensity and
exposure and no invented compatible linear-float equirectangular environment
texture. Cubemap/procedural sky presentation remains part of the pending
static asset/instance inventory; this ambient field does not claim to replace
it. OGRE 14 has no authored
reflection-probe registry, so its complete authored probe set is exactly empty;
the vehicle-local dynamic `GfxEnvmap` is not promoted to a world-space probe.
The complete managed `Ogre::MOT_LIGHT` registry is captured at that same joined
boundary, including authored-invisible lights. Exact case-sensitive OGRE names
are domain-separated FNV-1a-64 identities; empty names, duplicate names, hash
collisions, type changes, malformed native values, and unsupported rectangle
lights fail the whole light transaction. Authored visibility maps to zero
intensity and zero shadow classes without removing the record, so enable/disable
changes do not churn identity. Directional position/range/cones are canonical
zero, point direction
is canonical, local positions and range are retained, OGRE full spot cones are
halved into portable half-angles, and the OGRE shadow enable maps to both static
and dynamic shadow classes.

Legacy OGRE lighting is not measured photometry. Calibration version 1 maps one
renderer-linear Rec.709 luminance unit of OGRE `diffuse * powerScale` to exactly
1024 canonical lux for directional lights or candela for local lights. RT4/V1's
documented `1/1024` native scale therefore reconstructs the legacy diffuse RGB
term numerically before attenuation. This compatibility claim intentionally
does not cover OGRE's constant/linear/quadratic attenuation coefficients,
spotlight falloff exponent, separate specular color, visibility/light masks, or
material response because scene schema v4 cannot represent those values.
RT4/V1 also still admits only one directional light and rejects point/spot
lights; full native local-light rendering remains a downstream milestone.

Every supported actor cab, flexbody, flex-mesh wheel, and mesh-wheel tire is
split by effective OGRE `SubEntity`, preserving its exact topology draw range,
material binding, affine transform, visibility mask, reflection visibility, and
shadow participation. Stable domain-separated identities use actor creation ID,
component kind, component creation ID, and section ordinal; they never use vector
position, display name, or a native address. The immutable base asset owns UV0,
indices, material, and topology, while each scene owns a full post-physics
position/normal update and exact tight bounds. Semantic deformation revisions
advance only when copied contents change, unchanged frames reuse the prior
immutable owner, removed object identities become permanent tombstones, and base
assets remain retained so actor removal cannot invalidate in-flight snapshots.
The actor topology cache retains only copied numeric OGRE resource handles, never
native pointers, and is cleared when the graphics scene is destroyed.

The actor-deformation adapter deliberately fails closed when a vertex declaration
requires an update stream it cannot reproduce exactly. Current exclusions include
frame-varying FlexBody blend colors, dynamic tangents, skinning, extra texture
coordinate sets, and any other unsupported dynamic declaration. As with static
objects, the compatibility material fallback rejects texture units, shader
programs, or multipass state. The renderer-neutral dynamic input can instead own
the same immutable exact translated material closure used by static sections. In
that mode it rederives the exact group/name key and translator ID, validates the
closure version and source/catalog lineage, preserves dependency order and shared
payload owners, compares complete producer-owned bindings, validates authored UV
availability, and derives required topology winding from the translated audit.
The factor-only path is not consulted. Full-asset canonicalization and collision
rules are intentionally shared with the static path, including payload plus all
material bindings. Mirrored dynamic transforms and identity resurrection also
fail closed.

Before a joined caller merges independently built static and dynamic asset
vectors, it must call
`ValidateOgre14GraphicsSceneResolvedMaterialFrameLineage()` over both candidate
input sets. That transaction revalidates every detached closure and proves a
single source/catalog epoch across both domains; equivalent payloads from
different epochs are not interchangeable. The live `GfxScene` tap does not yet
populate `resolved_material` or its `mesh_reverse_winding` proof, so textured
vehicles remain fail-closed end to end until that native wiring lands.

The adapter now publishes an authored `MeshObject` static subset only when its
whole geometry domain is representable. `TerrainObjectManager` supplies
monotonic never-reused object IDs; every `SubEntity` becomes one exact
mesh/material section; CPU extraction honors OGRE vertex/index draw ranges,
16/32-bit indices, authored basis/UV/color streams, material culling, and tight
bounds. Domain-separated exact-resource IDs are collision-audited, omitted
identities are permanent tombstones, and an immutable payload cache makes
stable frames reuse the same owners. Entity and section visibility plus
shadow/reflection flags are preserved.

The same authoritative transaction now includes every defined OGRE 14
`TerrainGroup` slot. Every page must be loaded, retain complete CPU LOD0 height
data (`highest_lod_prepared == 0`), have no pending group preparation or
derived-data work, and agree with its packed signed slot, group layout, and
translation-only render node. Capture copies all row-major heights and a
one-cell `getPointFromSelfOrNeighbour` halo. It emits one camera-independent
full-LOD0 mesh per page, converts OGRE's alternating strip to canonical
triangles, retains all four page-perimeter skirts, and reproduces alignment,
eight-face normals, tangent handedness, upper-left UVs, winding, and tight
bounds. Because the canonical page is one draw topology, there are no internal
quadtree LOD boundaries needing camera-selected skirts or morph deltas.
`highest_lod_loaded` and target LOD are range-audited native GPU/draw metadata
but do not enter the exact geometry key or affect the payload. Adjacent loaded
pages must agree along shared world edges.

Terrain page identities include the exact resource group, filename convention,
resolved slot filename, and signed coordinates. Their derived IDs are
collision-audited and omissions are permanent tombstones. An exact byte cache
key contains every topology scalar, height, halo point, and winding byte;
unchanged pages reuse immutable owners while changed pages advance their
revision. The candidate terrain cache and combined terrain/`MeshObject`
inventory commit only after all pages and sections succeed. Capture never
mutates LOD, normal, delta, or derived-data state.

Procedural-road capture version 1 now starts at the graphics owner rather than
recovering data from collision triangles or GPU buffers. `ProceduralRoad`
retains an owning post-`createMesh()` copy of the exact uploaded positions,
normalized render normals, UV0 values, and safely promoted uint16 indices; its
post-`finish()` snapshot adds exact native mesh/entity/material identity,
derived node transform, visibility, and shadow state. `ProceduralManager`
allocates monotonic nonzero identities independent of vector position and road
name. Duplicate registration, identity exhaustion, and re-adding a removed
object fail closed. Rebuilds preserve identity and advance topology revision
exactly once only when the finalized geometry byte key changes; the previous
native road remains live until its replacement validates.

`Ogre14ProceduralRoadSource` is the renderer-neutral transaction for that
snapshot. It audits native bounds, finite unit normals, triangle winding,
uint16 promotion, exact `road2` identity, transforms, collision-separated
identity derivation, live/lifetime/payload limits, permanent removal
tombstones, stable ordering, and revision lineage. Byte-identical roads reuse
the same immutable mesh owner. The source currently produces static-section
candidates which can be combined with terrain/native static sections before
the generic collision-audited static-inventory transaction. The focused
combined-static contract proves collision rejection, stable ordering, and
immutable owner reuse without mutating durable road state before commit.

### Continuous OGRE 14 particle capture v1

`SceneSnapshot::ParticleEvent` remains the compact contract for discrete burst
emissions. It cannot represent a continuously retained particle, its exact
material, direction, rotation, age, or explicit system stop/destruction. It is
therefore incorrect to flatten the native smoke, exhaust, dust, fire, or water
systems into a fresh burst on every frame. `Ogre14ParticleCaptureSource` adds a
version-one, wire-adjacent continuous-state delta without changing the
established scene-snapshot-v4 transport layout. Transport and frontend
consumption are still a downstream milestone, so this source is not yet wired
into `GfxScene` or advertised as shipping particle support.

The future native tap supplies a complete value-only inventory after the
copied simulation buffer and graphics update epochs have joined. It must issue
monotonic never-reused system, per-system particle, and transition-event IDs;
copy realized render-space position, unit direction, velocity, linear color,
width/height, rotation, age, and lifetime; and preserve the frame's absolute
world origin. Input traversal order is irrelevant. The source sorts systems,
particles, and events by stable identity, derives exactly one `CREATE`,
`UPDATE`, `STOP`, or `DESTROY` transition for every semantic change, and
rejects missing, extra, or mismatched producer events. Unchanged frames produce
an empty delta, exact same-sequence replay reproduces the preceding result, and
omitted systems or particles leave permanent tombstones that cannot return.
Effective visibility is the logical AND of system and parent visibility.

There is no guessed smoke shader. Every system carries a versioned material
closure receipt naming the exact catalog registry, catalog sequence, material
revision, and successful translator source sequence. Capture also receives a
borrowed const `RenderAssetRegistry` view for that exact joined boundary and
resolves the receipt to a live `MaterialDescriptor` before lifecycle state can
move. The caller must serialize `Apply()` and keep the view quiescent for the
call; the registry type remains mutable and this is not a thread-safe immutable
snapshot. A forged, stale, missing, wrong-kind, or cross-catalog receipt rejects
the whole frame. Version one accepts only already-realized, world-space,
camera-facing-point billboards. It fails closed if a frontend would need to
evaluate native emitter or affector definitions, sort particles, animate a
texture, apply a local-space transform, or reinterpret another billboard mode.

All configured per-system, per-frame, lifetime-identity, event, and logical
payload-byte limits are nonzero. The logical byte sum is derived from named
fixed-width terms (including the full closure receipt and vector counts) and
uses checked arithmetic before candidate allocation. Registry and output
publication use a candidate copy and non-throwing final moves: malformed
values, identity collisions, sequence gaps/regressions, cap exhaustion,
allocation exceptions, and injected pre-commit faults leave both the durable
registry and caller output unchanged. The remaining native work is to assign
stable IDs at the owners in `DustPool`, actor exhaust/custom-particle creation,
terrain particle objects, turboprop/turbojet smoke, and extinguishable fire;
copy the post-update realized particle arrays without hardware-buffer reads;
and submit the resulting adjunct transaction alongside the same joined scene
and catalog snapshot. Signed-zero floating values are folded to canonical
positive zero before replay comparison and publication.

A system first observed with emission disabled is still created explicitly with
`CREATE` and a stopped complete state. A previously stopped but not destroyed
system may resume emission under the same identity; that transition is an
`UPDATE`, not a second `CREATE`. While a system remains stopped, retained
particles may update or age out, but the complete snapshot may not introduce a
new particle identity until emission resumes. `DESTROY` remains the permanent
identity boundary.

Compatibility-material fallback version 1 is intentionally factor-only. It
preserves first-pass diffuse/emissive factors, lighting, shininess-derived
roughness, supported culling, straight alpha, and alpha rejection while
requiring exactly one pass, zero texture units, and no vertex/fragment program.
Additional passes or authored texture/shader content fail closed rather than
being silently dropped. Terrain layer/sampler names and world scales, blend
textures, global-colour maps, lightmaps, composite maps, and generated material
state are audited before meshing; any authored texture state remains an exact
publication blocker until portable texture transport is implemented. The
original overload still keeps textured legacy `road2` blocked and never
coerces it through this fallback. The separately activation-gated overload now
accepts an exact road2 material closure only when it is resolved from one
authoritative translated full snapshot and the road capture supplies a
bit-exact native pipeline audit. That closure carries rederivable exact keys,
immutable texture/sampler/material owners, producer-owned bindings, and the
common source/catalog epoch; no factor-only road material or PBR semantic is
guessed. OGRE 14
Terrain has no hole API, and the renderer-neutral builder rejects a claimed
hole rather than silently filling it. Procedural roads, characters or other
unimplemented deformable geometry, paged vegetation, animated terrain objects,
unsupported vertex declarations, and unsupported material states likewise
return exact diagnostics. Mirrored instance transforms also fail closed until
reflection can be baked into the canonical mesh basis and winding. Consequently
`ASSETS`, `STATIC_MESHES`, and `DYNAMIC_MESHES` are advertised together
only after a complete supported inventory; terrain textures and joined
procedural-road collection remain required before ordinary maps can publish
end to end.

### Exact OGRE 14 legacy asset translator v1

`Ogre14LegacyAssetTranslator` is the replacement path for textured legacy
assets. It is deliberately a pure-data catalog and is not wired into
`GfxScene` yet. `gfx/ogre14/Ogre14LegacyNativeAssetExtractor` is the native
integration adapter and deliberately lives outside this renderer-neutral
boundary; the native application and its focused compile test pin that edge to
OGRE 14.5.2. The eventual static/terrain adapter
must submit a complete post-buffer inventory to the translator, then map each
dependency-ordered `source_asset_id` and immutable payload owner into
`GraphicsSceneAssetInput`. For a material, it must use the two IDs in
`Ogre14LegacyMaterialPipelineAudit` as the base-color binding and reverse mesh
winding when `requires_reverse_winding` is true. It may publish nothing unless
the companion audit is present and version 1.

The v1 acceptance set is exact and intentionally narrow:

- one loaded material technique containing one pass, with no authored or
  RTSS-generated GPU program, custom shadow material, hardware vendor/device
  rule, nondefault shadow policy, or nondefault technique scheme/LOD;
- zero or one ordinary named, loaded, non-manual 2D base-color texture, one
  frame, identity UV transform, and OGRE's texture-times-current color and
  alpha combine;
- explicit `BASE_COLOR_SRGB` or `LINEAR_DATA` intent supplied by versioned
  content metadata, with hardware-gamma state required to agree. The decoder
  never applies a transfer curve: native `PF_BYTE_RGBA` or the pure contract's
  byte-order-specific formats are channel-normalized to RGBA8 and the sRGB bit
  is attached exactly once;
- a contiguous base-to-smallest-provided mip prefix with exact halved
  dimensions, byte pitches, padding, and slice sizes. Output rows and slices
  are canonical tightly packed RGBA8;
- exact wrap/mirror/clamp/border, min/mag/mip filtering, anisotropy, LOD bias,
  effective LOD range, comparison, and border-color state;
- replace or true straight-alpha source-over blending, full color writes,
  canonical depth checking/writing, default manual culling, solid fill, one
  pass iteration, no bias or alpha-to-coverage, and always-pass or `>=` alpha
  rejection. Clockwise, anticlockwise, and disabled hardware culling remain in
  the immutable audit; anticlockwise culling requires mesh winding reversal;
- canonical Gouraud shading and scene-controlled fog, conditional transparent
  sorting, default line/point rasterization, the default all-light mask/range,
  no per-light iteration, vertex-colour tracking, light scissoring/clipping,
  manual illumination staging, or unordered-access texture mip;
- an explicit unlit or rough-dielectric PBR base-color declaration. The latter
  fixes metallic to zero and roughness to one by contract, not by inspecting
  a filename or shininess. Ambient, specular, emissive, and shininess lobes
  reject because v1 has no exact role for them.

Multipass or multi-technique materials; compressed or unsupported formats;
cubemap, array, 3D, multisample, external, compositor, render-target, manual,
generated, animated, procedural, projective, or environment texture content;
nonidentity gamma/UV/color transforms; comparison base-color sampling; and
every other blend/depth/raster state fail closed. A later version must add an
explicit portable semantic before accepting any of those cases.

Each accepted source frame starts at sequence one and advances exactly once.
Exact length-prefixed resource-group/name keys produce domain-separated stable
64-bit IDs. Semantic byte changes require a higher native source revision and
advance one translated revision; owner replacement and source-only revision
changes reuse the immutable payload and do not advance it. Transactions emit
texture/sampler/material UPSERTs in dependency order and material/sampler/
texture DESTROYs in reverse order. Removed keys become permanent tombstones;
full snapshots include them. Validation, allocation, native readback, injected
fault, collision, or lineage failure leaves the catalog, sequence, output
frame, and previously shared owners untouched.

Versioned translator configuration bounds texture inputs, material inputs,
derived live assets (including material-owned samplers), permanent lifetime
records (including tombstones), canonical decoded bytes per texture, and
aggregate canonical decoded bytes per source frame. Every limit is nonzero.
The default 65,536-record and 512 MiB payload ceilings match the joined
`GraphicsSceneSnapshotProducer` defaults; a native capture declaration must use
the same configuration as its consuming translator. Counts and decoded-byte
sums use checked arithmetic, and a cap failure is transactional: no source or
catalog sequence, output frame, lifetime identity, or immutable payload owner
is changed. Native readback applies the per-texture cap before allocating mip
storage, while the pure translator rechecks both the per-texture and aggregate
frame budgets before decode.

Whole-scene adapters may stage this catalog with `CloneForTransaction`. A fork
deep-copies the mutable sequence, revision, registry, identity, and tombstone
maps but retains the exact shared owners of immutable descriptors and material
audits. A private immutable lineage identity and the source transaction epoch
bind each candidate to one exact committed translator; candidates cannot fork,
ordinary translator move assignment is disabled, and self, forged, foreign,
consumed, or stale candidates cannot publish. Noexcept move construction
preserves the exact source/candidate role and lineage rather than laundering a
candidate into a source. Clone metadata has its own
versioned logical-byte budget and checked addition, so copying a hostile
catalog cannot silently amplify its mutable keys beyond the configured cap.
Those clone and epoch limits live in a separate versioned transaction
configuration, leaving the legacy translator configuration v1 unchanged. The
transaction epoch counts committed publications: a direct source `Translate`
or successful `CommitTransaction` consumes exactly one, candidate translation
consumes none, and even a no-op candidate commit consumes one so sibling forks
become stale. Tests use a small nonzero ceiling to exercise exact commit
exhaustion while production defaults to the full unsigned 64-bit range.

`CommitTransaction` is an allocation-free `noexcept` publication step. It
rechecks role, lineage, base epoch, and epoch exhaustion before swapping the
candidate state into the source and invalidating the candidate. A caller may
instead destroy a candidate to discard it. Allocation failure or an arbitrary
exception before candidate publication leaves the source and an already
populated clone output byte- and owner-equivalent; candidate translation,
discard/retry, and rejected publication likewise leave the committed catalog
and all previously shared owners unchanged.
The optional fault injector is borrowed by the source and its candidates; it
must outlive them, and publication never swaps or detaches the source pointer.
The private lineage makes candidate configuration externally immutable;
publication still compares every legacy and transaction configuration field
and the borrowed injector pointer before the state swap, failing closed on any
internal incompatibility.

`Ogre14LegacyMaterialClosure` closes the last pure-data seam between that
catalog and `GraphicsSceneSnapshotProducer`. Given an exact material key, it
accepts only a complete full snapshot with nonzero source/catalog lineage,
strict texture/sampler/material live order, strict tombstone/UPSERT mutation
order, and an exact UPSERT for every live immutable owner. It independently
parses each length-delimited stable key, recomputes its domain-separated ID,
validates every descriptor and material audit, and applies the translator's
65,536-record and 512 MiB hard ceilings before resolving anything.

The resolver revalidates every material in the snapshot, not only the selected
one. A textured material must name an sRGB RGBA8 2D texture and its unique
material-derived sampler together; orphan samplers, kind mismatches, invalid
sampler state, linear base color, semantic/model drift, pre-resolved portable
references, guessed metallic/roughness state, and winding/cull disagreement
all fail closed. Success preserves the immutable texture and sampler owners,
then the immutable material owner, and writes the two audited source IDs only
to the producer-owned base-color `material_bindings` slot. The descriptor's
portable references remain canonical absent. Any failure or exception leaves
the caller's closure untouched, so no partially allocated dependency list can
enter a joined graphics transaction. A borrowed test-only fault seam exercises
both pre-index allocation failure and an unexpected exception after partial
local dependency assembly; production callers leave it null.

Each detached closure also retains one exact key per dependency and the exact
immutable translated pipeline audit. Static-section admission rederives every
texture, sampler, and material ID from those keys, rechecks canonical debug
identity and sampler derivation, and requires every resolved section in one
inventory to carry the same source/catalog epoch. The merged asset collision
audit covers mesh, texture, sampler, and material IDs and compares all material
bindings as well as payloads. Dependency conflicts, forged keys, mixed epochs,
missing authored mesh streams required by producer-owned bindings,
mesh/material winding disagreement, and allocation or arbitrary exceptions
after partial candidate assembly leave lifecycle state and outputs untouched.

The asset payload pins the registry, mesh, texture, material, and sampler
descriptor versions. It carries registry/base/target sequence lineage, the
full-snapshot marker, sorted UPSERT/DESTROY mutations, every descriptor field,
mesh stream and index bytes, and every texture-mip byte. Each UPSERT has an
explicit resource kind and exact `u64` byte length; its nested reader must
consume that resource subframe exactly. Material texture and sampler
dependencies remain stable asset references. No separate bulk-data carrier is
needed for the current descriptor contract because mesh and texture storage is
already embedded. Any future resource larger than this atomic message's caps
requires an explicitly versioned chunking contract rather than an implicit
reference or partial payload.

The input payload has an explicit payload version, SDL2 physical-scancode
table version, SDL2 standardized-gamepad table version, host clock domain, and
nonzero clock-origin identity. Every event has a strictly increasing nonzero
`u64` event ID, a nondecreasing `u64` host-monotonic nanosecond timestamp, and
one exact variant. Version 1 carries physical keyboard key/repeat events,
pixel mouse position/delta, explicitly numbered mouse buttons, binary32 wheel
motion/direction, standardized gamepad connect/disconnect/buttons/axes, strict
UTF-8 text, focus gained/lost, window close, and raw controller
connect/disconnect/buttons/axes/hats/sliders. Text is layout-resolved input;
physical scancodes remain layout-independent.

Keyboard, mouse-button, hat, standardized-gamepad button, and standardized-
gamepad axis numbers are pinned to SDL 2.32.10's
[`SDL_Scancode`](https://github.com/libsdl-org/SDL/blob/release-2.32.10/include/SDL_scancode.h)
and
[`SDL_GameController`](https://github.com/libsdl-org/SDL/blob/release-2.32.10/include/SDL_gamecontroller.h)
semantics without including an SDL header. Standardized stick samples preserve
SDL's exact signed-int16
`[-32768, 32767]` values and triggers preserve `[0, 32767]`; there is no
backend-dependent float normalization. Raw joysticks, steering wheels, flight
sticks, and throttles remain distinct from standardized gamepads. Each raw
connection carries a stable nonzero device ID, increasing nonzero connection
generation, 16-byte GUID, vendor/product/version, SHA-256 name digest, device
class, and bounded ordered descriptors for signed axes, deadzones, buttons,
hats, and two-component sliders. Relative axes are centered at zero; sliders
are absolute. A device ID cannot change between standardized and raw families,
and a descriptor cannot change inside one raw connection generation.

Every batch ends with a complete authoritative level-state reconciliation
through an event-ID/timestamp watermark: focus, latched close request, pressed
physical keys and mouse buttons, every connected standardized gamepad and its
exact controls, and every connected raw device with its descriptor and exact
controls. Focus loss requires all level controls to be neutral, preventing
stuck keys, buttons, axes, hats, or sliders even if the operating system does
not deliver releases. Contiguous event IDs must transform the previous
reconciliation into the new one exactly. A forward gap is permitted only
because that complete snapshot heals coalesced/dropped events; replay, IDs at
or below the prior watermark, timestamp regression, clock-origin changes,
stale connection generations, silent state changes on complete lineage, and a
cleared close latch fail transactionally.

All payloads use explicit little-endian integers and IEC 559 binary32/binary64
encoding; none serializes C++ object storage. Scene values canonicalize
signed zero to positive zero, and the scene decoder rejects negative zero.
Asset values preserve both signed-zero encodings because asset revision
identity is bit-exact. Every transported floating-point field rejects NaN and
infinity. Trailing bytes and unknown envelope, payload, or descriptor versions
are forbidden.

Before any decoded vector reserves memory, its count is checked against the
protocol cap, bytes still present, and a cumulative allocation budget. Scene
payloads are capped at 64 MiB and decoded allocations at 128 MiB. Asset
payloads are capped at 640 MiB and decoded allocations at 768 MiB, with at most
65,536 mutations, 512 MiB per resource, 512 MiB per texture blob and across all
texture blobs, and 16 mip levels per texture. Mesh stream and index counts have
additional fixed caps. These limits are checked before reserve or blob copy.
Input payloads are capped at 4 MiB and decoded allocations at 8 MiB, with at
most 8,192 events, 1 MiB of UTF-8 text, 10 connected devices, 64 device
generations per batch, and 256 stable device identities retained by a decoder.
Per-device raw limits match the current RoR controller ABI: 32 axes, 128
buttons, four hats, and four sliders. Every count and text length is bounded
before reserve or copy.

An acknowledgement names the endpoint-derived registry and a strictly
increasing `through_forward_sequence`; it cumulatively retires every forward
envelope through that value. Its optional scene pair must name one exact scene
envelope and immutable snapshot ID at or below the cumulative watermark. A
scene may first become presented after an earlier ACK has already consumed it;
the game host therefore retains bounded identity lineage for acknowledged but
not-yet-presented scenes until a newer presentation makes them unreachable. A
control command names the same registry, has an exact command-ID lineage
starting at one, and is one of `PEER_READY`, `REQUEST_GRACEFUL_SHUTDOWN`, or
`HEARTBEAT`; control payload version 2 also admits `SURFACE_CHANGED`. The first
control command is `PEER_READY`, and it cannot be repeated. Ready and changed
surface controls carry the committed surface revision plus logical and
drawable integer extents. Their exact X/Y content scales are the corresponding
drawable/logical ratios, so no redundant floating value can disagree. An
active surface has four nonzero bounded extents. A suspended changed surface
preserves nonzero logical size, explicitly marks suspension, and carries a
0x0 drawable; a ready surface cannot be suspended. Every changed revision is
strictly newer. Heartbeat and shutdown carry zero surface fields. ACK payload
version 1 remains independent. Both payload kinds retain reserved-zero layouts
and a 128-byte cap.

Standalone typed decoders own a private envelope sequence. A live bridge gives
the scene and asset decoders one `RenderTransportSequenceState`, producing one
strictly ordered game-to-renderer stream. The input decoder can likewise join
the acknowledgement/control decoder on one reverse sequence; the two
directions still own distinct states, may both validly begin at sequence `1`,
and cannot advance one another.
The receiver applies an asset transaction to its private `RenderAssetRegistry`
before committing the forward envelope sequence; the next scene can then
validate against that exact asset sequence. Replay, gaps, wrong-kind routing,
corruption, truncation, semantic failure, dependency failure, tombstone
resurrection, or input-lineage failure leave the applicable expected sequence,
registry/state, and previously published immutable owner unchanged. One caller
serializes operations on each decoder.

`RendererFrontendTransportDispatcher` is the renderer-neutral consumer for one
complete, already-envelope-validated game-to-presentation frame at a time. It
derives a nonzero, non-maximum asset-registry identity from the 128-bit bridge
session with the pinned SHA-256 domain
`ror.render.asset-registry-id/renderer-bridge-session/v1`; the two processes can
therefore agree on the registry without a new handshake. It shares one sequence
state across both typed decoders, synchronizes every accepted asset delta before
dependent scene submission, validates scene references against that exact
catalog, and uses the scene envelope sequence as the strictly increasing
frontend frame ID.

Presentation is an explicit caller policy. A presented scene selects the sole
transported camera, exact active surface revision, and current drawable extent.
After decoding, the dispatcher retires a camera whose captured extent differs,
including when an idle poll announced the resize before the scene arrived. An
offscreen scene names no surface. The dispatcher adds no UI and never copies CPU attachment bytes to
a window. It invokes the frontend's native presentation path, waits infinitely
for every successful submission before attempting retirement, and calls
`ReleaseResource` exactly once for each unique transferred GPU attachment on
success and failure paths. A failed wait still performs that required frontend-
owned retirement, which may defer native destruction behind queue work. Any
decode, reverse-direction input, lineage, capability, synchronization, render,
wait, correlated-output, or release error permanently poisons the dispatcher.
This establishes transport consumption and ownership, not visual readiness or
a shipping Ogre-Next default.

`RendererOgre14GameHostSession` owns the active game-side stream around an
already initialized `RendererOgre14GameBridge`. `Submit` applies and serializes
ordered asset deltas; `PostPhysics` accepts only complete scenes against that
exact catalog and monotonically increasing snapshot/tick lineage. Both calls
perform no pipe I/O and enqueue only within configured message, byte, and
unacknowledged-lineage limits. One worker owns both channel halves, drains
whole forward envelopes in order, incrementally reconstructs reverse frames,
and exposes input, cumulative ACK, and control messages through zero-wait poll
methods. Queue saturation returns deterministic `BACKPRESSURE` without
advancing any sequence. The worker drains immediately available reverse bytes
before every bounded zero-wait forward write, and it pauses forward progress
when the delivered reverse queue is full; neither direction can pin the other
behind a blocking native-pipe write. `Close` therefore remains bounded even if
an open peer stops reading. Scene publication remains blocked until an active
`PEER_READY`, while suspended, or when camera pixels differ from the latest
drawable extent; there is no guessed 1280x720 path. A consumed newer active
`SURFACE_CHANGED` applies before the next accepted scene.

Surface events are pumped by the presentation process even while no forward
frame is available. A resize can still race bytes already assigned an exact
forward sequence; those bytes cannot be removed without creating a sequence
gap. The presentation side therefore consumes a stale-size or suspended scene
without rendering or presenting it, sends `SURFACE_CHANGED` first, and then
sends a cumulative retirement ACK whose presented-scene pair remains at the
last scene actually presented. Input may interleave between those two reverse
messages because their shared envelope sequence preserves causality. Once the
host consumes the surface change it rejects newly produced stale scenes.
`FinishForward` drains queued envelopes, closes only the outbound half, and
continues reading final reverse acknowledgements until peer EOF; corruption,
wrong registry/sequence, stale surface revision, impossible ACK lineage, or
unexpected peer teardown is terminal and preserves its first cause.

The codecs contain no socket, process-spawn, OS packing, SDL, OIS, OGRE header,
or third-party serializer dependency, so a byte-stream adapter can choose
platform IPC without changing scene, asset, or input semantics. Input version
1 is transport only: it neither polls SDL nor injects `InputEngine` actions.
Force feedback is deliberately absent and requires a separate versioned
game-to-host command message rather than overloading input state.

Process isolation is required for the live migration bridge. OGRE 1.14 and
Ogre-Next expose overlapping global `Ogre::*` C++ symbols and runtime-global
state; loading both into one executable would make ABI resolution and teardown
unsafe even when the public renderer boundary itself is neutral. Keeping the
legacy simulation/game process and modern render process in separate address
spaces lets each link exactly one OGRE generation. Scene/camera, asset, input,
ACK, control, bounded host back-pressure, and half-close transactions now have
versioned contracts. The endpoint-adopted `RoR-Ogre14` role also has a real
product owner which drains reverse traffic before gameplay, binds reconciled
input to a transport-only `InputEngine`, retains one post-`UpdateScene`
production across backpressure, and shuts down in dependency order. Its
validated ownership plan keeps the legacy resource host hidden and disables
legacy physical-device ownership and per-loop presentation. This does not admit or package the production
Ogre-Next child, change standalone OGRE 14 behavior, add UI to the UI-free
scene stream, or provide force feedback across the bridge.

The native interop and native ray-tracing interfaces are contracts, not an
implementation or readiness claim. All related capabilities fail closed by
default. Raster API reporting is independent of native interop: a Windows
D3D11 frontend reports D3D11 raster while leaving native API and DXR readiness
at NONE/false. Same-device native claims require exact Metal-to-Metal,
D3D12-to-D3D12, or Vulkan-to-Vulkan pairing; OpenGL, D3D11, and cross-API pairs
fail closed until a separately versioned bridge contract exists. A frontend may
report geometry interop ready only after deformable position/index export,
native device/queue context, frame synchronization, native dispatch/readback,
and lifecycle acceptance have passed on the real backend. The combined proof
validator rejects mismatched APIs, contradictory reports, missing live interop,
or partial evidence. Version 1 explicitly proves geometry interop only: it does
not claim normals, tangents, UVs, materials, textures, samplers, lights,
particles, or shading parity in a native RT path. Those attribute and shading
exports are a subsequent adapter-contract milestone. Native RT version 1 is
correspondingly limited to offscreen CPU readbacks; it does not claim frontend
texture import, compositing, or window presentation.

Surface state is explicit and pixel-based. The host submits monotonically
increasing resize/DPI/minimize revisions after platform configure handling;
portable resources survive surface recreation. A presented frame selects one
view and the current surface revision, and that view must exactly match the
active surface pixel extent. Frontends may not implicitly stretch a differently
sized view. Suspended 0x0 surfaces skip presentation until reactivated.

This boundary still does not change the shipping game's OGRE 1.14 renderer.
An isolated opt-in Ogre-Next N1 adapter exercises static PBR/HDR raster, and an
Apple-only N2 acceptance backend proves exact same-device Metal geometry
export plus one-ray BLAS/TLAS dispatch and an exact eight-byte probe readback.
It intentionally returns `UNSUPPORTED` from the image-rendering interface until
it can produce a view-dependent attachment. These remain standalone gates: N2
does not yet import a result into an Ogre texture or implement RT
materials, lighting, denoising, compositing, or presentation, while Windows and
Linux continue to report native RT false until their explicit backends exist.
The richer lighting/environment snapshot is likewise transport and validation,
not evidence that N1/N3 or a shipping frontend already maps its photometry,
shadows, sky, exposure, reflections, or GI.

`PbrReference` is the strict-CPU analytic oracle for the portable direct-light
material slice. Version 1 binds the canonical Ogre-Next dependency lock to a
feature-specific source-hash manifest and evaluates the selected full32
`PbsBrdf::Default` equations in binary64: squared perceptual roughness, the
0.001 alpha floor, GGX distribution, height-correlated Smith visibility,
Schlick Fresnel, and normalized Disney diffuse. Saturated `NdotV` keeps tangent
and back-facing view directions in the supported equation domain; saturated
`NdotL` multiplies the final response exactly as in the source. An undefined
view/light half vector is rejected transactionally.

This is an idealized equation reference, not bit-exact backend arithmetic. The
pinned upload path uses the binary32 literal `0.318309886f`, and the metallic
shader path reconstructs color using `3.14159f`; the CPU oracle instead carries
the intended equations in binary64. Metal, Vulkan, and D3D captures therefore
have a declared 1% relative comparison gate, with fixture-specific absolute
tolerances near zero. `PbsBrdf::Default` also omits diffuse Fresnel, so the
normalized Disney diffuse label is not a claim that the combined BRDF always
integrates to at most one. Backend success alone remains no fidelity claim.

`ParallaxProbeReference` is the strict-CPU numerical oracle for Ogre-Next's
box-projected cubemap sampling and automatic-probe influence math. It pins the
exact common-equation, manual-weight, automatic-weight, and C++ buffer/probe
sources; reproduces strict box membership, manual edge fade, fourth-power
automatic NDF weighting, ray-box intersection, and Ogre's left-handed cubemap
sampling vector. The API consumes
already transformed probe-local values so Metal, HLSL, GLSL, and CPU adapters
can compare one result without leaking backend matrix types into the boundary.
The path-bound source closure lives in
`tools/ogre_next_probe/ogre-next-parallax-probe-reference.lock.json`; its test
can hash an extracted pinned Ogre-Next tree directly when the source-root
environment gate is supplied.

`ReflectionProbeRuntime` is the renderer-neutral admission and scheduling
contract that feeds that oracle and the future native Ogre-Next capture path.
Version 1 owns rigid oriented probe coordinates, correction/influence box
containment, capture planes, resolution/PCC-filtered-mip shape, static or simulation-tick
periodic updates, stable priority budgeting, revision lineage, and permanent
identity tombstones. Probe position remains binary64 absolute-world state;
each plan derives and bounds its exact float render-relative transform from the
frame origin, so large-world rebasing neither changes authored lineage nor
forces a static recapture. `BeginFrame` creates a candidate transaction without
changing committed lineage; `Commit` accepts a generation only through an
opaque receipt bound to the exact plan, request slot, and complete cubemap
measurement; `Abort` and failed captures leave the prior complete cubemap
authoritative. Selection and
capture seeds depend only on validated scene identities and simulation ticks,
never wall time or backend iteration order. This is the portable control plane,
not evidence that a Metal, Vulkan, or D3D11 cubemap has rendered yet.
Revision enforcement compares the complete canonical descriptor field by field
(with signed zero folded); the 64-bit fingerprint is only a digest/cache key,
so a hash collision cannot authorize changed contents at an old revision.

`ReflectionProbeCaptureReceipt` keeps the portable-to-native completion edge
fail-closed. The public measurement function validates the complete immutable
scheduler request field by field and hashes exactly one RGBA16F readback for
every required face and filtered IBL mip, ordered mip-major then face-major.
The output layout mirrors Ogre-Next
`ParallaxCorrectedCubemapBase::getIblNumMipmaps` exactly:
`max(full_chain_mips, 5) - 4`, with reviewed resolutions 32..2048 (32 => 2
mips; 256 => 5). Receipt metadata records every output mip's exact width and
height, so this contract cannot be mistaken for the source cubemap's full raw
mip chain. The measurement is explicitly
non-authoritative and cannot be passed to `Commit`. Successful receipts are
opaque, bind the exact plan and request rather than trusting a digest as
identity, and may be issued only by a reviewed concrete adapter after native
execution. No shipping adapter exists in this milestone; only the contract-test
adapter can issue successful receipts, so this remains portable control-plane
work rather than evidence that native IBL ran. Missing, duplicate, reordered,
under-pitched, stale, partial, transformed, or replayed data fails without
publication. Scheduler reset clears scene lineage but never reuses a transaction
ID during that object lifetime, closing late-completion ABA across terrain
changes.

The standalone modern frontend also stages and authenticates the exact pinned
Ogre-Next PCC depth-compressor, local-cubemap blend/copy, and compute IBL
resource closure. Metal, Vulkan, and D3D11 consume the same 22-file manifest;
missing, additional, indirect, or byte-modified files are rejected before
`Ogre::Root` or a GPU device is created. This media gate is necessary plumbing
for native captures, not by itself evidence that a cubemap was executed.

`HdrReference` provides two strict-CPU behaviors for the pinned Ogre-Next HDR
sample shared by the Metal, HLSL, and GLSL paths. Analytic v1 evaluates the
selected equations in ideal binary64. Shader v1 evaluates binary32 and models
the exact binary16 scene/luminance storage boundary, including multi-frame R16
exposure feedback. The bloom input is explicitly Ogre's historical gamma-2
encoding, not standard sRGB. Explicit comparison APIs equalize texture storage
before tone-map comparison and use a conditioning-aware bound for exposure
adaptation near zero frame time. Source hashes, admitted ranges, backend
tolerances, and the unimplemented output stages are defined in
[`HDR_REFERENCE.md`](../../../../doc/nextgen/HDR_REFERENCE.md).

`OgreNextHdrTemporalState` turns that numerical behavior into a fail-closed
per-frontend history contract without importing Ogre headers. It maps portable
view/scene exposure to the pinned shader parameter, derives binary32 deltas
only from immutable simulation time, and accepts only the pinned upstream
`0.01` initial history. Version 2 compares finite-positive native R16 feedback
against the shader oracle's conditioning and binary32 rounding bound plus one
binary16 storage ULP, records the full comparison, and commits the accepted
native bits as authoritative history. The native frontend separately proves an
exact current-to-old copy, creates the RoR-owned UI-free workspace in code, and
uses a visible-overlay negative control plus staged same-object reinitialization
to keep the compositor and lifecycle claims fail-closed.
