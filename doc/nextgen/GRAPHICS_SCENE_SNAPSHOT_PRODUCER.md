# Joined graphics scene snapshot producer

Status: the renderer-neutral producer core is implemented; the OGRE 1.x
`GfxScene` source adapter is the next integration change.

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

## Version-one production slice

The first slice supports a complete authoritative inventory of:

- static mesh descriptors and stable source asset identities;
- textures, samplers, and PBR/unlit materials with explicit source dependency
  identities;
- static `MeshObject`/terrain-object instances with rigid transforms,
  visibility, and shadow/reflection flags;
- constant or texture-backed scene environment state; and
- one main camera with canonical current and previous view/projection state.

The producer owns 128-bit `RenderAssetId` allocation, revision propagation,
asset sequence, snapshot sequence, tombstones, previous transforms, camera
history, and render-origin rebasing. Inputs are authoritative: omitting a live
asset or object destroys it, and a destroyed source identity cannot be reused
during that producer lifetime.

Every call is transactional. Validation, ID/revision allocation, dependency
resolution, asset-registry application, immutable snapshot creation, exact
asset cross-validation, and camera validation all succeed before state is
committed. A rejected frame consumes no identity, revision, sequence, previous
transform, or tombstone state. First production emits a full catalog; later
catalog changes emit sorted incremental deltas; device recovery emits the full
live catalog plus all permanent tombstones.

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

Canonical sorting retains each asset/object's original authoritative-vector
index. Failures after sorting, including lifecycle, kind, dependency, object
reference, and snapshot compatibility failures, report that original index
rather than a sorted rank.

Producer limits bound lifetime asset/object records and authoritative
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
4. `GfxScene` captures the main graphics camera after its copied simbuffer has
   been consumed, converts OGRE matrices into the canonical right-handed,
   column-major, depth-[0,1] contract, and submits the current drawable pixel
   extent. The producer owns previous matrices.
5. `GfxScene::ClearScene()` destroys the producer after its final authoritative
   empty inventory has been delivered, so a new terrain starts a fresh registry
   identity instead of resurrecting tombstones.

The current `TerrainObjectManager` and `MeshObject` APIs expose OGRE objects and
do not yet provide stable source IDs or cached portable resource descriptors.
That coupling is why the first commit lands the production core and exact seam
rather than a fake renderer demo or a producer that scans the OGRE scene graph.

Deformable `GfxActor` meshes, analytic lights, particles, water/sky, and
auxiliary cameras are later producer slices. They extend the same joined source
and immutable contract; they do not bypass it.
