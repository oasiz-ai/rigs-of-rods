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

`PbrReference` is the strict-CPU numerical oracle for the portable direct-light
material slice. Version 1 records the exact pinned Ogre-Next commit and mirrors
its full-precision `PbsBrdf::Default` metallic workflow: squared perceptual
roughness, the 0.001 alpha floor, GGX distribution, height-correlated Smith
visibility, Schlick Fresnel, and normalized Disney diffuse. It deliberately
does not turn backend success into a fidelity claim; Metal, Vulkan, and D3D
captures must compare their resolved shader samples to this same oracle.
