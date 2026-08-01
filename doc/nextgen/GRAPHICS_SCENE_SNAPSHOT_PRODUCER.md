# Joined graphics scene snapshot producer

Status: the renderer-neutral version-two producer core, including immutable
analytic lighting/environment publication, is implemented; the OGRE 1.x
`GfxScene` source adapter remains the next integration change.

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

## Version-two production slice

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
- constant or texture-backed environment radiance, an additive analytic
  zenith/horizon/ground/sun-disk sky tied to one directional light, and bounded
  scene-level exposure compensation; and
- one main camera with canonical current and previous view/projection state.

The producer owns 128-bit `RenderAssetId` allocation, revision propagation,
asset sequence, snapshot sequence, tombstones, previous transforms, light and
camera history, and render-origin rebasing. Inputs are authoritative: omitting
a live asset, object, or light destroys it, and a destroyed source identity
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

Canonical sorting retains each asset/object/light's original
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

Scene snapshot versions 1 and 2 and joined-frame producer version 1 are not
implicitly migrated. They return `UNSUPPORTED_VERSION`. A legacy adapter must
explicitly normalize light chromaticity, preserve scalar photometry separately,
populate version-3 environment state, and emit producer version 2. Lighting
hash version 1 must be discarded rather than compared with version 2.

Producer limits bound lifetime asset/object/light records and authoritative
descriptor payload bytes.

## Exact remaining `GfxScene` adapter tap

The adapter belongs in graphics code, not in the renderer contract library. It
must translate OGRE values in a `.cpp` file behind
`IJoinedGraphicsSceneSource`; no OGRE header may be included by
`source/main/gfx/render` public contracts.

Required source-side changes are:

1. `TerrainObjectManager` exposes a read-only graphics-owned static-object
   inventory with a monotonically allocated, never-reused numeric object ID.
   Its current private `m_mesh_objects` vector and pointer position are not a
   stable identity contract.
2. `MeshObject`/`ContentManager` cache portable mesh, texture, sampler, and
   material descriptors when authored resources are loaded. The adapter may
   use OGRE to extract them once in its implementation file, but it must not
   make a frontend reverse-engineer an `Ogre::Entity` each frame.
3. The inventory assigns stable source asset IDs to authored resource
   lifetimes. Material texture/sampler dependencies are submitted as source
   IDs; the adapter never fabricates a `RenderAssetReference`.
4. `GfxScene` captures graphics-owned sun, terrain/object lights, and sky state
   into stable, never-reused light identities without asking a backend to scan
   an OGRE scene graph. Authored legacy attenuation must be converted into the
   documented candela/range curve by a calibrated adapter, not copied as
   renderer-specific coefficients.
5. `GfxScene` captures the main graphics camera after its copied simbuffer has
   been consumed, converts OGRE matrices into the canonical right-handed,
   column-major, depth-[0,1] contract, and submits the current drawable pixel
   extent. The producer owns previous matrices.
6. `GfxScene::ClearScene()` destroys the producer after its final authoritative
   empty inventory has been delivered, so a new terrain starts a fresh registry
   identity instead of resurrecting tombstones.

The current `TerrainObjectManager` and `MeshObject` APIs expose OGRE objects and
do not yet provide stable source IDs or cached portable resource descriptors.
That coupling is why the first commit lands the production core and exact seam
rather than a fake renderer demo or a producer that scans the OGRE scene graph.

Deformable `GfxActor` meshes, particle emission, water state, volumetric weather,
and auxiliary cameras are later producer slices. Shipping Ogre-Next raster and
native RT adapters also still need measured mappings for the version-two light
and sky contract. This milestone transports and validates authoritative data; it
does not implement shadow maps, sky scattering, GI, reflections, denoising, or
ray-traced lighting by itself.
