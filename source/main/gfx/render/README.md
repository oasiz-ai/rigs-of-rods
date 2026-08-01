# Renderer-neutral boundary

This directory is the versioned, portable C++17 data and interface boundary
between simulation/gameplay producers and renderer frontends. Public contracts
use only standard C++ and the types defined here. Backend adapters may include
OGRE, Ogre-Next, Metal, D3D12, or Vulkan headers in their own implementation
directories, but those types must never appear in this boundary.

`SceneSnapshot` is an immutable, validated deep copy shared as
`std::shared_ptr<const SceneSnapshot>`. Resource identities are generational:
each registry has a process-unique domain, releasing a slot makes every earlier
handle for that slot stale, and a slot is retired instead of wrapping its
generation. Collections with stable IDs are strictly ordered for reproducible
backend consumption. Descriptor validation is structural; each frontend must
additionally reject handles that are not live in its own resource registry.

Successful frame submission leases every referenced resource through frame
completion. Releasing a resource prevents future submissions immediately while
physical destruction waits for material dependencies, in-flight frames, and
native exports. Materials own strong internal texture/sampler leases; updates
swap dependency graphs transactionally, so releasing a public texture handle
cannot invalidate an already-created material. Portable mesh, texture, sampler,
and material creation APIs are the only resource path required by scene
producers; native objects are optional, borrowed interop.

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
that current origin. Backends own API-specific projection and framebuffer remapping. Rendered
outputs are validated against the exact request, including frame/snapshot IDs,
view/output pairs, extents, presentation state, and attachment completeness.
Texture rows/UVs, cube faces, material channel packing, triangle winding, depth,
motion, object-ID, and normal attachments also have one canonical interpretation
so Metal, D3D12, Vulkan, OGRE, and Ogre-Next adapters cannot silently diverge.

Snapshots are self-contained for deformation: every non-base revision carries
one full vertex-stream state and exact bounds, so out-of-order or repeated
rendering never depends on cached partial uploads. Newly created snapshot IDs
and particle-event IDs increase globally. A frontend emits each event only on
the first successful submission of its snapshot; repeats and multi-view renders
do not duplicate smoke, dust, sparks, or other effects.

The native interop and native ray-tracing interfaces are contracts, not an
implementation or readiness claim. All related capabilities fail closed by
default. A frontend may report geometry interop ready only after deformable
position/index export, native device/queue context, frame synchronization,
native dispatch/readback, and lifecycle acceptance have passed on the real
backend. The combined proof validator rejects mismatched APIs, contradictory
reports, missing live interop, or partial evidence. Version 1 explicitly proves
geometry interop only: it does not claim normals, tangents, UVs, materials,
textures, samplers, lights, particles, or shading parity in a native RT path.
Those attribute and shading exports are a subsequent adapter-contract milestone.
Native RT version 1 is correspondingly limited to offscreen CPU readbacks; it
does not claim frontend texture import, compositing, or window presentation.

Surface state is explicit and pixel-based. The host submits monotonically
increasing resize/DPI/minimize revisions after platform configure handling;
portable resources survive surface recreation. A presented frame selects one
view and the current surface revision, and that view must exactly match the
active surface pixel extent. Frontends may not implicitly stretch a differently
sized view. Suspended 0x0 surfaces skip presentation until reactivated.

This foundation does not yet adapt OGRE 1.14 or Ogre-Next and does not change
the current game's rendering. Those adapters are separate implementation
milestones behind this contract.
