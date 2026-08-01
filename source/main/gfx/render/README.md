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

Scene snapshot version 3 defines immutable analytic lighting in explicit
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

`GraphicsSceneSnapshotProducer` version 2 accepts those lights in arbitrary
source traversal order, canonicalizes them, rebases local-light history, and
enforces permanent identity/type tombstones. Once the entire asset, scene,
lighting, environment, and camera transaction commits, it release-publishes the
exact immutable owner returned to the caller. Concurrent consumers acquire-load
either the previous complete scene or the next complete scene; a failed
production publishes nothing. The atomic observer pointer does not replace the
ordered production result: renderer submission still carries that scene's asset
delta and camera together. Calls into a producer, including observer loads, must
quiesce before producer destruction starts; immutable snapshot owners already
acquired by readers remain valid independently.

There is no implicit lighting-schema migration. Scene snapshot versions 1 and 2
and joined-producer input version 1 are rejected with `UNSUPPORTED_VERSION`.
An adapter migrating legacy light colors must normalize its non-black
linear-sRGB color with `NormalizePhotometricColorLinear`, retain the scalar
lux/candela value separately, populate all version-3 sky/exposure fields, and
submit producer version 2. Old lighting hash values are not comparable with the
version-2 digest because the registry identity and calibrated photometry are now
part of the contract.

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

`HdrReference` provides two strict-CPU behaviors for the pinned Ogre-Next HDR
sample shared by the Metal, HLSL, and GLSL paths. Analytic v1 evaluates the
selected equations in ideal binary64. Shader v1 evaluates binary32 and models
the exact binary16 scene/luminance storage boundary, including multi-frame R16
exposure feedback. The bloom input is explicitly Ogre's historical gamma-2
encoding, not standard sRGB. Source hashes, admitted ranges, backend tolerances,
and the unimplemented output stages are defined in
[`HDR_REFERENCE.md`](../../../../doc/nextgen/HDR_REFERENCE.md).
