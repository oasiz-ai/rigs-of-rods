# Joined graphics scene snapshot producer

Status: the renderer-neutral version-four producer core and the joined OGRE 14
adapter for timing, origin, constant ambient, managed lights, authored static
`MeshObject` sections, the exact empty authored reflection-probe inventory,
and main camera are implemented. Actual OGRE Terrain pages, procedural roads,
paged/animated geometry, deformable actors, and portable legacy texture assets
remain explicit publication blockers; their presence never produces a false
complete snapshot.

## Boundary

`GraphicsSceneSnapshotProducer` consumes only `GraphicsSceneFrameInput`. Its
public headers contain no OGRE, Ogre-Next, Metal, Vulkan, D3D, platform-window,
physics, or recorder types. `IJoinedGraphicsSceneSource` is the one adapter
seam.

The live call order must be:

1. halt the simulation at the existing joined boundary;
2. run `GfxScene::BufferSimulationData()` so `GameContextSB`, `GfxActor`
   simbuffers, and `GfxCharacter` simbuffers own the frame state;
3. resume simulation and let `GfxScene::UpdateScene()` consume those copied
   buffers on the graphics thread;
4. call the OGRE-specific implementation of
   `IJoinedGraphicsSceneSource::CaptureJoinedGraphicsFrame()` using only those
   copied buffers and graphics-owned scene inventories; and
5. pass the resulting immutable snapshot and optional asset transaction to one
   or more renderer frontends.

The adapter must not follow actor pointers from `GameContextSB`, query the
solver, or read any mutable physics container. Renderer selection must not
change simulation scheduling or hashes.

## Version-three production slice

The first slice supports a complete authoritative inventory of:

- static mesh descriptors and stable source asset identities;
- textures, samplers, and PBR/unlit materials with explicit source dependency
  identities;
- static `MeshObject`/terrain-object instances with rigid transforms,
  visibility, and shadow/reflection flags;
- directional lights in lux plus point/spot lights in candela, finite range,
  spot half-cones, unit-luminance linear-sRGB Rec.709 D65 chromatic
  multipliers, static/dynamic shadow masks, and stable source identities;
- current and previous local-light positions/directions, including exact render
  origin rebasing and identity/type/tombstone lineage;
- absolute-world oriented reflection probes with authored content revisions,
  correction/influence volumes, static or simulation-tick periodic policy,
  canonical ordering, and permanent identity tombstones;
- constant or texture-backed environment radiance, an additive analytic
  zenith/horizon/ground/sun-disk sky tied to one directional light, and bounded
  scene-level exposure compensation; and
- one main camera with canonical current and previous view/projection state.

The producer owns 128-bit `RenderAssetId` allocation, revision propagation,
asset sequence, snapshot sequence, tombstones, previous transforms, light/probe and
camera history, and render-origin rebasing. Inputs are authoritative: omitting
a live asset, object, light, or reflection probe destroys it, and a destroyed source identity
cannot be reused during that producer lifetime.

Every call is transactional. Validation, ID/revision allocation, dependency
resolution, asset-registry application, immutable snapshot creation, exact
asset cross-validation, and camera validation all succeed before state is
committed. A rejected frame consumes no identity, revision, sequence, previous
transform, or tombstone state. First production emits a full catalog; later
catalog changes emit sorted incremental deltas; device recovery emits the full
live catalog plus all permanent tombstones.

After that transaction commits, `Produce()` release-publishes the exact
`shared_ptr<const SceneSnapshot>` returned in its result. Render and readback
threads acquire-load it with `LoadPublishedSnapshot()`; they can observe either
the complete old snapshot or the complete new snapshot, never staged mutable
state. A rejected capture, asset update, scene, light, environment, or camera
validation leaves the publication unchanged. The producer remains single-writer
and externally serialized; atomic publication does not make concurrent calls to
`Produce()` valid. This observer seam publishes only the immutable scene owner;
renderer frontends must still consume the successful production result so its
asset delta and camera remain ordered with that scene.

Every method call must quiesce before producer destruction begins. This is a
normal object-lifetime requirement, not a lock supplied by the atomic observer.
A reader that already acquired a `shared_ptr<const SceneSnapshot>` may retain
and inspect that immutable owner after the producer itself is destroyed.

The asset registry and source-asset catalog are one immutable copy-on-write
state. An unchanged authoritative catalog is shared by the next transaction;
its node-based maps and immutable payloads are not copied. Object history is a
sorted contiguous vector sized by an allocation-free prepass and rebuilt by a
merge pass; authoritative asset membership also uses a merge walk rather than
per-element tree allocations. A camera/transform-only frame therefore uses a
constant number of contiguous allocation calls independent of catalog size
while copying neither mesh vertex/index nor texture byte storage. It is not
allocation-free: staged object history and the new immutable snapshot each need
contiguous storage.

Each asset also keeps a weak identity for the graphics cache's immutable input
owner. Stable same-owner frames reuse the first successful full descriptor
validation and report exactly zero `asset_payload_full_validations`, validated
candidate bytes, deep-equivalence fallbacks, and compared candidate bytes in
result-local diagnostics. Exact mesh/material revision pairs and environment
references also cache successful registry compatibility, so transform-only
frames do not re-walk immutable mesh streams through cross-validation. A
replacement owner is fully validated and triggers a measured deep-equivalence
fallback; equivalent contents refresh the weak identity transactionally without
emitting a delta or advancing the asset sequence. Only unresolved material
descriptors are retained strongly in addition to canonical registry payloads,
so meshes and textures do not acquire a second persistent byte owner.

Canonical sorting retains each asset/object/light/probe's original
authoritative-vector index. Failures after sorting, including lifecycle, kind,
dependency, object reference, light photometry/history, and snapshot
compatibility failures, report that original index rather than a sorted rank.

Every immutable snapshot records a version-two FNV-1a-64 digest of the exact
lighting/environment payload. Its padding-free little-endian encoding includes
the asset-registry identity, absolute render origin, optional environment
references, ambient and analytic sky radiance, exposure, stable sorted lights,
shadow masks, and current and previous light transforms. It canonicalizes
signed zero and deliberately excludes frame IDs, time, asset sequence,
geometry, particles, and cameras. This is a stable change-detection/test digest,
not a collision-resistant security primitive.

Snapshot version 4 also records a separate version-one reflection-probe digest.
It includes the exact ordered absolute-world descriptors and authored update
policy, while excluding render origin, frame/time identity, and native capture
generations. A render-origin rebase therefore leaves authored probe lineage
unchanged; a geometry/policy change at the same content revision fails before
publication.

Light color uses canonical linear-sRGB Rec.709 D65 photometry. A non-black
chromatic multiplier is normalized so its weighted luminance is represented as
binary32 one; the scalar `intensity` therefore remains the authoritative lux or
candela value. Zero, nonfinite, negative, and unnormalized multipliers fail
closed. Shadow caster class is derived only from the resolved mesh resource's
`dynamic` bit. Motion, base/non-base deformation revision, and update presence
do not change the class selected by a light's static/dynamic mask. The analytic
sun helper normalizes both directions, centers the disk at the negated emitted
ray direction, and includes `dot == cos(radius)`. Per-view exposure combined
with scene EV must remain a finite positive normal binary32 value.

Scene snapshot versions 1, 2, and 3 and joined-frame producer versions 1 and 2 are not
implicitly migrated. They return `UNSUPPORTED_VERSION`. A legacy adapter must
explicitly normalize light chromaticity, preserve scalar photometry separately,
populate version-4 environment/probe state, and emit producer version 3. Lighting
hash version 1 must be discarded rather than compared with version 2.

Producer limits bound lifetime asset/object/light/probe records and authoritative
descriptor payload bytes.

## Live `GfxScene` adapter status and remaining taps

The adapter belongs in graphics code, not in the renderer contract library. It
must translate OGRE values in a `.cpp` file behind
`IJoinedGraphicsSceneSource`; no OGRE header may be included by
`source/main/gfx/render` public contracts.

Implemented source-side behavior is:

1. `TerrainObjectManager` exposes a read-only graphics-owned `MeshObject`
   inventory with monotonically allocated, never-reused numeric IDs. Removal
   erases and deletes the inventory record before destroying its OGRE entity,
   so capture cannot retain the former dangling pointer. ID exhaustion fails
   object loading rather than wrapping.
2. `GfxScene` preflights the entire visible geometry domain before publishing.
   Any OGRE Terrain page, procedural road, actor/deformable mesh, paged batch,
   or animated terrain object returns a stable `static_meshes.unsupported.*`
   diagnostic. The `ASSETS` and `STATIC_MESHES` availability bits are committed
   together only after the complete supported inventory succeeds.
3. Each managed entity is split at its authored `SubEntity` boundary. The
   adapter honors `vertexStart`/`vertexCount` and `indexStart`/`indexCount`,
   supports 16- and 32-bit indices, copies position plus optional normal,
   tangent, two UV, and normalized RGBA color streams under read-only buffer
   locks, and rejects blend streams, unsupported declarations, non-triangle
   operations, skeletons, and vertex animation. OGRE 14 and the portable
   contract share right-handed +Y-up local space and upper-left UVs; values are
   copied without basis or UV guessing. Anticlockwise-cull material sections
   reverse each triangle into the contract's canonical CCW front face. Tight
   local bounds are recomputed from the exact copied positions. Mirrored world
   transforms fail closed until their reflection can be baked into every
   position/normal/tangent stream without violating canonical winding. Capture
   uses authored LOD zero and rejects nonzero OGRE rendering-distance culling,
   so camera state cannot silently change the submitted topology or membership.
4. Mesh, material, and object-section identities are domain-separated,
   length-framed FNV-1a-64 hashes over exact case-sensitive resource groups,
   names, draw ranges, winding, manager object ID, and section index. Every
   mapping is collision-audited, omissions are permanent tombstones, and
   equivalent frames reuse immutable descriptor owners. Native mesh pointer
   and OGRE resource-state changes invalidate the CPU cache transactionally.
5. Compatibility-material fallback version 1 accepts exactly one pass with no
   texture units or vertex/fragment program and preserves that pass's
   renderer-linear diffuse/emissive factors, lighting enable, Phong-shininess
   to roughness conversion, straight alpha or alpha test, and culling. It
   records ambient and specular state as audited native input but deliberately
   creates no guessed portable contribution. Additional passes, texture units,
   shader programs, unsupported blending, alpha combinations, or native values
   fail closed instead of losing authored visuals.
6. `GfxScene` enumerates the authoritative managed `Ogre::MOT_LIGHT` registry
   at the joined boundary (not backend render queues or scene-node traversal),
   verifies each registry key equals the exact unique Light name, and hashes
   those exact bytes into collision-audited stable identities. It captures
   active and inactive directional/point/spot records transactionally, maps
   local range, full-to-half spot cones, and shadow enable exactly, and applies
   compatibility-calibration v1: legacy renderer-linear `diffuse * powerScale`
   luminance times 1024. Ogre-Next's reciprocal scale reproduces that direct
   RGB term before attenuation. This is not physical photometric calibration;
   schema v4 cannot preserve OGRE c/l/q attenuation, spot falloff exponent,
   separate specular color, or light masks, and RT4/V1 still rejects local
   lights downstream.
7. `GfxScene` captures the main graphics camera after its copied simbuffer has
   been consumed, converts OGRE matrices into the canonical right-handed,
   column-major, depth-[0,1] contract, and submits the current drawable pixel
   extent. The producer owns previous matrices.
8. OGRE 14 has no authored world reflection-probe registry, so the adapter
   publishes the exact empty inventory rather than inferring probes from the
   vehicle-local environment-map implementation.

Remaining source-side work is an exact CPU adapter for `Ogre::TerrainGroup`,
portable texture/sampler extraction for legacy material units, and explicit
adapters for procedural roads, paged vegetation, and animated/deformable
geometry. `GfxScene::ClearScene()` must also deliver the final authoritative
empty inventory before the producer is destroyed so a new terrain receives a
fresh registry lifetime. Until those domains are covered, maps containing any
of them stay fail-closed even though their immutable `MeshObject` subset is
fully convertible.

Deformable `GfxActor` meshes, particle emission, water state, volumetric weather,
and auxiliary cameras are later producer slices. Shipping Ogre-Next local-light
and native RT adapters also still needs an explicitly versioned attenuation and
sky calibration. This milestone transports and validates authoritative data; it
does not implement shadow maps, sky scattering, GI, reflections, denoising, or
ray-traced lighting by itself.
