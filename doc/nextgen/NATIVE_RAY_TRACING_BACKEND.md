# Ogre-Next and Native Ray-Tracing Decision RFC

Status: **priority architecture gate; Apple M5 N3 same-device
view-dependent hybrid-HDR slice passed, production RT not yet implemented**

Audit date: **2026-08-01**

RoR baseline: `6e7c81d3cf5cac8501af1a2be8158a5d7f14202d`

## Decision

Proceed with a bounded Ogre-Next integration spike, but do not switch the
shipping renderer or claim native ray tracing yet.

Ogre-Next is a strong candidate for RoR's high-quality raster foundation. Its
[official feature set][ogrenext-readme] includes PBS/HDR, forward-clustered
lighting, area lights, parallax-corrected cubemaps, voxel cone tracing, and
[irradiance methods][ogrenext-gi]. It does
**not** currently provide the required native RT endpoint by itself:

- its official renderer list is Direct3D 11, OpenGL 3.3+, Metal, and Vulkan;
- there is no Direct3D 12 renderer in the audited tree;
- the audited Metal and Vulkan renderers do not implement Metal acceleration
  structures or the Vulkan KHR ray-tracing pipeline; and
- the audited Direct3D renderer is D3D11, while DXR is a D3D12 API.

The viable candidate is therefore:

1. Ogre-Next as the scene, material, HDR, and high-quality raster frontend;
2. a small RoR-owned native RT layer using Metal on macOS, DXR on Windows, and
   Vulkan KHR ray tracing on Linux;
3. explicit same-device resource interop with Ogre-Next; and
4. an Ogre-Next raster fallback that remains visually complete when RT is
   unavailable.

The ownership boundary is explicit across platforms. Ogre-Next owns the
Metal, Direct3D 11, and Vulkan raster/PBS/HDR paths. RoR owns acceleration
structures, ray dispatch, and raster interop in its Metal RT, D3D12/DXR, and
Vulkan KHR RT backends; Ogre-Next native ray tracing is not assumed or claimed.

This is a **conditional go**, not a dependency-selection conclusion. The
candidate becomes a full go only after a real Metal RT scene pass on supported
Apple Silicon and a real DXR/Ogre-Next interop pass on Windows. If either hard
gate fails, RoR must reconsider the renderer choice rather than dilute the
native-RT requirement.

## What “native ray tracing” means here

Native RT means all of the following are true in the running RoR process:

- the platform-native API implementation is compiled and selected;
- the selected physical adapter reports the required API capabilities;
- the adapter meets the accepted hardware-acceleration floor;
- RoR builds bottom- and top-level acceleration structures;
- RoR dispatches rays and validates an exact GPU readback;
- RoR shares real scene geometry, materials, frame resources, and
  synchronization with the raster renderer; and
- at least one UI-free RoR scene frame contains a measured RT contribution.

An API header, extension string, shader compiler, renderer name, or successful
device creation is not native-RT completion.

The initial product target is hybrid rendering: rasterized primary visibility
plus RT reflections, contact/area shadows, and selected indirect-lighting
queries. Full real-time path tracing is not an initial requirement and is not
implied by this RFC.

## Upstream capability audit

### Exact Ogre-Next evidence

The newest stable upstream release at audit time is
[`v3.0.0`][ogrenext-v300], published 2024-10-15 at commit
`75643c3997f5b6d2aa1d7bd8400b9be6736d9908`. Upstream `master` was also audited
at commit
[`75f66d9dd48630693afad011f6223a2864b2455d`][ogrenext-audit-commit],
dated 2026-07-26. The documentation generated from current development sources
identifies that line as 4.0.0 unstable, so it is research input rather than a
shipping pin.

The exact upstream tree has these renderer directories:

| Renderer directory | Native RT consequence |
| --- | --- |
| `Direct3D11` | Not DXR; Windows needs a D3D12/DXR interop or renderer path. |
| `GL3Plus` | High-quality fallback only; no required native RT API. |
| `Metal` | Suitable host API, but Ogre-Next does not supply an RT pass. |
| `Vulkan` | Suitable host API, but Ogre-Next does not supply KHR RT setup or dispatch. |
| `GLES2`, `NULL` | Outside the desktop RT endpoint. |

There is no `Direct3D12` directory in the
[pinned renderer tree][ogrenext-render-systems]. Exact GitHub source searches
at the audited commit returned zero implementation matches for:

- `VK_KHR_ray_tracing`
- `VkAccelerationStructureKHR`
- `MTLAccelerationStructure`
- `ID3D12GraphicsCommandList4`
- `D3D12_RAYTRACING`

The generic SPIR-V headers contain RT enumerants and the Vulkan synchronization
code knows an acceleration-structure access bit. Those are not an RT backend.

Ogre-Next does expose three useful integration seams:

- [`MetalDevice`][ogrenext-metal-device] exposes the active `MTLDevice`,
  command queue, and current command buffer;
- [`VulkanExternalDevice`][ogrenext-vulkan-device] and the
  [external-device tutorial][ogrenext-vulkan-external] allow the application to
  create the Vulkan device and declare its extensions before Ogre-Next uses it;
  and
- [`D3D11Device::TransferOwnership`][ogrenext-d3d11-device] can accept an
  application-provided D3D11 device. Whether a D3D11On12 device satisfies the
  full Ogre-Next window/resource lifecycle is unproven and is a hard Windows
  spike.

### Platform API floor

| Platform | Required native API proof | Accepted hardware floor for RT preset | High-quality fallback |
| --- | --- | --- | --- |
| macOS | `MTLDevice.supportsRaytracing`, acceleration-structure build, Metal ray dispatch/readback, shared Ogre-Next `MTLDevice` lifecycle | Apple GPU family 9 or newer for hardware-accelerated RT; currently M3-class and newer. M1/M2 retain raster fallback. | Ogre-Next Metal PBS/HDR and non-RT reflection/GI methods |
| Windows | D3D12 device, `D3D12_FEATURE_D3D12_OPTIONS5`, DXR tier 1.1 or newer, BLAS/TLAS, `DispatchRays`, readback, Ogre-Next frame interop | Adapter reporting DXR tier 1.1 or newer | Ogre-Next D3D11 PBS/HDR, initially through the same D3D12 command queue if D3D11On12 proves viable |
| Linux | Required Vulkan KHR extensions and feature structs, BLAS/TLAS, ray-tracing pipeline dispatch/readback, shared Ogre-Next `VkDevice` lifecycle | Adapter and driver exposing the complete accepted KHR feature set | Ogre-Next Vulkan PBS/HDR and non-RT reflection/GI methods |

Apple documents
[`supportsRaytracing`][apple-metal-capability] as the runtime capability query. The
[May 2026 Metal feature table][apple-metal-table] places ray tracing in compute
pipelines at Apple family 6 and later, while Apple documents
[hardware-accelerated ray tracing][apple-hardware-rt] at Apple family 9 and
later. RoR uses the
hardware floor for the RT preset because the goal is real-time UE5-class hybrid
quality, not merely API availability. `supportsRaytracing` remains a mandatory
query even on accepted families.

Microsoft documents [DXR through D3D12][dxr-reference] interfaces including
`ID3D12Device5` and `ID3D12GraphicsCommandList4`. RoR must query
[`D3D12_FEATURE_DATA_D3D12_OPTIONS5::RaytracingTier`][dxr-capability] with
`CheckFeatureSupport`; adapter naming or feature level alone is insufficient.

Khronos specifies that
[`VK_KHR_ray_tracing_pipeline`][vulkan-rt-pipeline] depends on
`VK_KHR_acceleration_structure` and Vulkan 1.2 or `VK_KHR_spirv_1_4`. RoR's
initial Linux contract additionally requires the extension/feature set needed
to build device acceleration structures and dispatch a ray-tracing pipeline.
The actual device feature chain, not extension enumeration alone, is
authoritative.

## Apple M5 executable proof

The standalone `ror_metal_rt_probe` passed on the local Apple M5 at this
revision. This was an actual GPU execution test, not capability detection:

- `supportsRaytracing` returned true;
- Apple GPU family 9 support returned true;
- Metal built one triangle BLAS and a one-instance TLAS;
- a Metal compute kernel issued one ray through that TLAS; and
- CPU readback validated the expected triangle hit distance of exactly `1`.

The probe is compiled with the shipping macOS 11 deployment floor, guards the
newer Apple-family query, returns CTest skip code 77 on unsupported devices,
and fails on any build, dispatch, or readback error. The checked-in
[machine-readable evidence](evidence/METAL_RT_PROBE_M5_2026-07-30.json)
records the OS, SDK, compiler, device, source and artifact hashes, allocation
sizes, and exact result.

This closes only the standalone Metal API/hardware subgate. It does **not**
close Ogre-Next same-device resource sharing, RoR scene geometry, HDR
compositing, lifecycle validation, performance, or fallback gates. The
application therefore must not report `native_rt=metal` from this result.

## RoR render-boundary audit

This is a migration, not a renderer-library drop-in. At the audited RoR
revision, 322 of 557 C++/Objective-C++ source files contain `Ogre::` references,
with 8,276 scoped uses. The heaviest groups are:

| Area | Files containing `Ogre::` | Migration implication |
| --- | ---: | --- |
| `source/main/gfx` | 118 | Scene objects, cameras, materials, particles, water, sky, flex visuals |
| `source/main/physics` | 46 | Mostly visual/flex types colocated with physics; must not make the solver depend on a new renderer |
| `source/main/gui` | 42 | MyGUI/ImGui composition and input/window lifecycle |
| `source/main/resources` | 25 | OGRE mesh/material/resource loading |
| `source/main/terrain` | 15 | Terrain, roads, vegetation, PSSM material generation |
| `source/main/scripting` | 16 | Public script bindings expose OGRE objects and require a compatibility plan |

The existing code nevertheless has a valuable isolation seam:
`GfxScene::BufferSimulationData()` stops at a copied simulation buffer, and
`GfxScene::UpdateScene()` consumes it on the rendering side.
`SimBuffers.h` explicitly describes this simulation/snapshot/graphics split.
That seam should become renderer-neutral rather than creating any RT access to
live solver objects.

The first exact taps are:

- `AppContext::SetUpRendering()` — renderer/device/window bootstrap;
- `GfxScene::{Init,BufferSimulationData,UpdateScene,ClearScene}` — scene
  lifecycle and the authoritative simulation-to-render boundary;
- `GfxActor::{UpdateSimDataBuffer,UpdateFlexbodies,FinishFlexbodyTasks}` —
  vehicle instance, deformable geometry, lights, and effects;
- `FlexBody::{computeFlexbody,updateFlexbodyVertexBuffers}` and
  `FlexMesh::{flexitCompute,flexitFinal}` — current CPU deformation and GPU
  vertex upload;
- `Terrain`, `TerrainGeometryManager`, `TerrainObjectManager`, and
  `ProceduralRoad` — static world geometry and instanced vegetation;
- `MeshObject` and `ContentManager` — mesh/material identity and lifetime; and
- `GUIManager`, `CameraManager`, `EnvironmentMap`, `GfxWater`, Hydrax, and SkyX
  — compositor ordering, auxiliary cameras, and UI-free output.

No code under `source/main/worldmodel` is part of this renderer migration.

## Target architecture

```text
physics/gameplay thread
        |
        | immutable RenderSceneSnapshot at existing joined boundary
        v
renderer-neutral scene registry
   | stable IDs, meshes, materials, transforms, deformable streams, lights
   |
   +---------------- Ogre14 frontend (shipping compatibility) ----------------+
   |
   +---------------- Ogre-Next frontend --------------------------------------+
                         |                       |
                         | raster HDR/PBS        | native resource handles
                         v                       v
                 high-quality fallback    INativeRayTracingBackend
                                          /          |          \
                                      Metal RT      DXR      Vulkan KHR RT
                                          \          |          /
                                           hybrid HDR compositor
                                                    |
                                           scene RGB, then UI
```

### Renderer-neutral snapshot

Add a versioned `RenderSceneSnapshot` owned by graphics code. It contains no
Ogre, Metal, D3D, Vulkan, physics, or recorder objects:

- stable entity, mesh, primitive, material, light, and camera IDs;
- immutable mesh topology and per-primitive material assignment;
- current rigid transforms and visibility masks;
- deformable vertex/normal streams with monotonically increasing revisions;
- PBR inputs with explicit color space, alpha mode, and two-sided state;
- analytic lights, emissive primitives, sky/environment state, and exposure;
- current and previous camera/object transforms for temporal reprojection; and
- ordered create/update/destroy deltas with a complete-snapshot recovery mode.

`GfxScene::BufferSimulationData()` remains the joined producer boundary. A new
adapter converts existing simbuffers and authored scene objects into the
snapshot after physics without changing solver scheduling or state. Renderer
selection must not change physics hashes.

### Frontends

Introduce a narrow `IRendererFrontend` with:

- device/window creation and destruction;
- scene registry create/update/destroy;
- camera and frame constants;
- HDR scene color, depth, normal, motion, material-ID, and object-ID outputs;
- UI-free frame completion;
- resize/device-loss handling; and
- an optional native-resource bridge for the matching RT backend.

OGRE14 remains the default frontend. Ogre-Next is opt-in until it passes content,
script, UI, and visual gates. Legacy AngelScript OGRE bindings remain available
only with the compatibility frontend during migration; new renderer-neutral
bindings are introduced before removing them.

### Native RT interface

`INativeRayTracingBackend` accepts only renderer-neutral descriptors plus an
explicit platform resource bridge. It owns:

- capability probing and stable diagnostics;
- BLAS/TLAS allocation, build, refit, compaction, and destruction;
- shader library/pipeline and binding-table equivalents;
- per-frame descriptor/resource binding;
- RT output images and denoiser history;
- queue/command-buffer synchronization with the raster frontend; and
- exact readiness/provenance reporting.

The native backend never reads or mutates simulation state.

## Platform implementation candidates

### macOS first: Ogre-Next Metal plus Metal RT

This is the priority implementation:

1. Build a tiny Ogre-Next `v3.0.0` Metal application on arm64.
2. Obtain the exact `MTLDevice` and command queue used by Ogre-Next.
3. Query `supportsRaytracing` and the Apple GPU family. The hardware RT preset
   requires Apple family 9 or newer.
4. Build one triangle acceleration structure, dispatch one deterministic ray,
   and validate a small exact readback.
5. Import one Ogre-Next mesh and share its vertex/index resources without a
   CPU copy after upload.
6. Add an RT reflection result to the Ogre-Next HDR compositor.
7. Run create/load/resize/unload/device-teardown loops with Metal validation.
8. Repeat in a real RoR starter scene, then with one deforming vehicle.

The RT deployment floor may be higher than RoR's macOS 11 raster floor. The
bundle must retain the raster path on older OS/hardware and must not reference
unavailable API entry points before guarded capability checks.

### Linux: application-owned Vulkan device

Create the Vulkan instance/device in RoR, enable only an allowlisted feature
chain, and pass it to Ogre-Next through `VulkanExternalDevice`. Required
admission includes:

- `VK_KHR_acceleration_structure`;
- `VK_KHR_ray_tracing_pipeline`;
- their specified dependencies, including buffer device address and deferred
  host operations where required by the selected Vulkan version;
- `VkPhysicalDeviceAccelerationStructureFeaturesKHR::accelerationStructure`;
- `VkPhysicalDeviceRayTracingPipelineFeaturesKHR::rayTracingPipeline`; and
- limits sufficient for the bounded validation scene.

The first probe builds one BLAS/TLAS, dispatches a fixed ray, validates
readback, and exits cleanly under validation layers. The scene slice then uses
the same device and explicit queue ownership/barriers as Ogre-Next.

### Windows: DXR interop is the hardest gate

Ogre-Next has no D3D12 renderer. The first candidate is:

1. RoR creates a D3D12 device and command queue.
2. RoR creates a D3D11On12 device on that queue.
3. Ogre-Next receives the D3D11 device through its ownership seam.
4. The raster and DXR paths share explicitly wrapped resources and fences.
5. DXR tier 1.1, BLAS/TLAS, dispatch/readback, swapchain resize, and teardown
   pass in one process.

Microsoft documents
[D3D11On12 wrapped-resource acquire/release semantics][d3d11on12], but
that does not prove Ogre-Next compatibility. If the spike cannot make the
device, swapchain, resource states, and lifecycle reliable, the choices are:

- implement and maintain an Ogre-Next D3D12 renderer;
- select another renderer architecture that supports D3D12, Metal, and Vulkan
  integration while retaining RoR's cross-platform requirement; or
- explicitly abandon DXR, which would fail this RFC's endpoint.

Silently using Vulkan RT on Windows is not an acceptable substitute while DXR
is a stated requirement.

## Dynamic soft-body acceleration structures

The RT scene cannot treat RoR vehicles as rigid meshes:

- static terrain/buildings: build once, compact, rebuild only on scene change;
- rigid props and nondeforming vehicle parts: static BLAS plus TLAS transform
  updates;
- flexbodies and flex wheels: persistent dynamic vertex buffers and
  update-capable BLAS, refit when topology is unchanged;
- broken or topology-changing geometry: bounded rebuild queued by stable
  revision, never an unsafe in-place refit;
- near-camera/high-contribution deformables: eligible for RT;
- distant or over-budget deformables: raster fallback/proxy geometry; and
- alpha-masked vegetation: initially excluded from RT or represented by a
  bounded opaque proxy until a measured any-hit path exists.

Acceleration-structure work is scheduled from the copied render snapshot. It
must not block or reorder the fixed physics scheduler. Per-frame build/refit
budgets, dropped/deferred counts, GPU time, scratch memory, and residency are
captured in acceptance artifacts.

## High-quality non-RT fallback

The fallback is a product path, not a debug mode. It must retain:

- linear HDR and the same exposure/tone-mapping output contract;
- Ogre-Next HLMS PBS materials with explicit metalness, roughness, normal,
  emissive, transmission/alpha, and color-space metadata;
- forward-clustered direct lights and measured shadow quality;
- area-light approximations where supported;
- image-based lighting and parallax-corrected reflection probes;
- a measured non-RT diffuse-GI choice such as VCT/irradiance methods where its
  platform cost and scene constraints pass; and
- the same temporal AA/upscaling, particles, atmosphere, water, and UI
  composition contract as the RT preset.

RT-disabled captures must remain complete; missing reflection, GI, material, or
lighting data cannot be hidden by the word “fallback.”

## Selection contract landed by this RFC

`RendererBackendPolicy` is a dependency-free C++11 decision seam. It:

- defaults the requested frontend to OGRE14 and RT to disabled;
- maps macOS to Metal, Windows to DXR, and Linux to Vulkan KHR;
- rejects unknown enum values and uncompiled frontends;
- refuses to select RT unless the native backend is compiled, the API and
  hardware floor pass, a real dispatch/readback probe passes, and scene interop
  is ready;
- permits `PREFER_HARDWARE` to fall back only to an explicitly ready raster
  frontend; and
- makes `REQUIRE_HARDWARE` reject rather than silently degrade.

The Apple-only test executable proves its isolated Metal dispatch, but no game
runtime populates a successful RT capability record. This change therefore
does not claim or enable native RT in RoR.

## Milestones and commit/PR sequence

### RT0 — Contract and decision evidence (this change)

- Land this RFC and the pure selector.
- Run strict and game-fast-math tests on AppleClang, GCC, and MSVC.
- Keep OGRE14 and RT-disabled defaults unchanged.

Exit: policy truth table is green on all three platforms.

### RT1 — Ogre-Next dependency and compatibility probe

- Pin Ogre-Next `v3.0.0` plus exact dependency revisions in a separate build
  option; do not follow `master`.
- Build minimal Metal, Vulkan, and D3D11 applications in CI.
- Record licenses, patches, compiler/SDK versions, plugin/resource inventory,
  ABI, and artifact hashes.
- Load one project-owned static glTF-derived scene with HLMS PBS/HDR.

Exit: dependency builds and a UI-free raster frame pass on macOS arm64,
Linux x86_64, and Windows x86_64. Shipping RoR remains OGRE14.

### RT2 — Renderer-neutral scene snapshot

- Add the snapshot/registry and OGRE14 adapter.
- Prove default-off OGRE14 frames and physics traces are unchanged.
- Add Ogre-Next adapters for camera, static mesh, rigid actor, material, and
  light data.

Exit: the same immutable snapshot renders in both frontends; no solver or
world-model dependency exists.

### RT3 — Metal hardware RT proof (first real backend)

- The standalone Apple capability, BLAS/TLAS, dispatch, and readback subgate is
  complete on the recorded Apple M5; retain it as a CTest admission probe.
- Sharing the exact Ogre-Next Metal mesh and retained `RGBA16_FLOAT` target is
  complete in N3 through versioned renderer-neutral geometry/image leases.
- A view-dependent primary-ray hit contribution is now GPU-composited into the
  exact UI-free Ogre target and independently read back as raster-only,
  contribution-only, and hybrid artifacts. A calibrated reflection or shadow
  contribution remains open.
- Validate M3-class or newer hardware; validate raster fallback on M1/M2.

Exit: exact probe, scene image, GPU capture, frame timings, lifecycle soak, and
provenance artifacts pass. Only then may the macOS build report
`native_rt=metal`.

### RT4 — Production raster fallback

- The first opt-in RT4/V1 slice now proves authored tangents/UV0, sRGB base
  color and emissive, split roughness/metallic channels from a packed linear
  texture, canonical positive-Z unit-scale normal maps derived from validated
  linear RGBA8 into `RG8_UNORM`, padded multi-mip uploads, sampler mapping,
  byte-exact native `Image2` RG staging, FLOAT4 tangent-handedness readback,
  fail-closed non-uniform scale, deterministic texture retirement, one calibrated directional light,
  HDR/SDR readback, and simultaneous Metal N3 geometry/image interop on the
  recorded Apple M5. A strict feature lock binds the exact pinned Ogre shader,
  datablock, pixel-format, and Metal/D3D11/Vulkan mapping owners used by that
  normal-map contract, including vertex TBN, UV, sampling-precision, vertex
  declaration, and native image row-layout owners.
- Complete the remaining Ogre-Next PBR/HDR material and lighting floor,
  including a semantically correct ambient-occlusion path, shadows, exposure,
  and presentation, and reproduce the implemented slice on Windows and Linux.
  The pinned PBS surface has no ambient-only occlusion slot; detail-map weight
  and direct-light multiplication remain explicitly rejected substitutes.
- Keep Metal, HLSL, and GLSL exposure/tone-map results bound to the portable
  pinned-source analytic/shader HDR references and their storage-aware,
  conditioning-aware comparison contract;
  treat output transfer, gamut mapping, dithering, and framebuffer clamping as
  separately versioned presentation stages.
- Feed persistent exposure history from the deterministic simulation-time
  temporal contract. Keep accepted finite-positive native R16 bits authoritative
  only after comparison with the pinned shader oracle using its conditioning
  and binary32 rounding bound plus one storage ULP; also reject stale lineage,
  unchanged history, or a non-exact current-to-old compositor copy.
- Publish HDR history/audit, reflection state, native interop, submission
  completion, and output only after every participant has prepared and can
  commit. Abort all staged public state on a later failure; because the GPU
  exposure-history copy cannot be rolled back after rendering, fault-latch the
  frontend rather than reuse that advanced history.
- Keep the HDR workspace RoR-owned, programmatic, source-manifested, and free of
  `HdrRenderUi`; require a negative-control workspace wired to the actual
  `HdrRenderUi` output and a real `Ogre::v1::Overlay`, independently recomputed
  raw compositor attachments, canonical exact-byte repeat artifacts, and all
  staged same-object reinitialization checks in native evidence.
- Add probes/SSR or accepted non-RT reflection path and a measured diffuse-GI
  path.
- Bind the probe fallback to the pinned portable box-projection/influence
  oracle before accepting backend images; separately gate capture scheduling,
  IBL filtering, and probe authoring.
- Pass CityWorld/starter-content missing-material, luminance, reflection,
  temporal, and performance gates with RT disabled.

Exit: `PREFER_HARDWARE` degradation is visually complete on every supported
machine.

### RT5 — Vulkan external-device foundation

- Create and prove the application-owned Vulkan instance, device, graphics
  queue, timeline, exact Ogre-Next adoption, and teardown order.
- Make no native-ray-tracing or ray-image claim at this checkpoint.

Exit: hardware ownership passes or software/incomplete devices return explicit
unsupported evidence.

### RT6 — Vulkan KHR primary-hit proof

- Require the exact KHR extension and feature chain on Linux hardware.
- Build fixed mirror geometry, BLAS/TLAS, descriptors, ray pipeline, and SBT on
  the shared graphics-and-compute queue.
- Dispatch one primary ray and require the deterministic nonzero host readback.

Exit: only an actual hardware dispatch may report the scoped RT6
`hardware_dispatch_pass`; production `native_rt=vulkan-khr` remains gated on
shared-scene integration, validation layers, resize, soak, and compositing.

### RT7 — DXR interop decision

- Complete the D3D12/D3D11On12/Ogre-Next proof.
- Bind every pass/unsupported result to the exact executable, DXIL, Windows SDK
  compiler closure, clean source manifest, build contract, observed process
  exit, and CI rerun receipt.
- Pass DXR tier query, triangle probe, shared-scene pass, resource-state
  validation, a native UI-free Ogre frame readback, resize, device removal, and
  soak. Initialise-only or fabricated offline artifacts never satisfy RT7.

The RT7 probe therefore rejects arbitrary `MZ`/`DXBC` byte blobs: its
independent verifier parses the x64 PE32+ section table and the DXBC/DXIL
container, LLVM bitcode header, and locked shader exports. The compiler is
resolved only from the canonical Program Files (x86)
`Windows Kits/10/bin/<version>/x64` directory;
`dxc.exe`, `dxcompiler.dll`, and `dxil.dll` are all hashed and recorded with the
SDK/version/path identity. Both pass and unsupported reports carry a fresh
runner nonce and the actual child-process exit code. Offline verification is
deliberately scoped to integrity and semantics: the workflow cryptographically
attests the resulting execution receipt with GitHub's OIDC-backed artifact
attestation, and `gh attestation verify` is the execution-provenance gate.

The D3D11On12 smoke is a real renderer exercise, not an initialise-only check:
it creates a hidden native window, registers pinned HLMS PBS media, renders a
lit material through Compositor2 into a UI-free texture, reads it back, rejects
blank output, destroys the frame resources, shuts Ogre down, and only then
releases the application-owned D3D11On12/D3D12 chain. CTest runs the same
report/image destination twice to prove Windows atomic replacement semantics.

Exit: either `native_rt=dxr` is proven or the project records a renderer
architecture no-go and chooses the D3D12-renderer/alternative-engine path.

### RT8 — Dynamic vehicles and hybrid quality

- Add rigid and deformable BLAS policies, motion history, denoising, reflection,
  shadow, and selected GI passes.
- Exercise vehicle deformation, broken beams, multiple vehicles, particles,
  water, vegetation, and day/night/weather profiles.

Exit: bounded AS time/memory, no stale geometry, no physics-hash changes, and
the immutable visual benchmark passes.

### RT9 — Default decision

- Run the shared AirSim/UE5-reference suite and platform performance budgets.
- Complete legacy content/script/UI compatibility and crash soaks.
- Compare the candidate against OGRE14 with all new features disabled.

Exit: only then consider making Ogre-Next the default. Native RT remains a
quality preset with a complete raster fallback.

## Acceptance gates

Every native backend must pass:

1. exact compile and dependency provenance;
2. runtime API/hardware capability query;
3. deterministic triangle BLAS/TLAS dispatch/readback;
4. static RoR scene geometry and material interop;
5. deformable vehicle update/rebuild correctness;
6. UI-free HDR frame evidence with the RT contribution separately capturable;
7. resize, minimize/restore, scene reload, device-loss, and teardown;
8. 10-minute fixed-camera and driving soaks with bounded memory;
9. no validation-layer/API-debug errors;
10. RT-required rejection and RT-preferred raster fallback on unsupported
    hardware;
11. pixel-identical approved baseline when the new frontend is not selected;
12. identical deterministic physics trace for renderer selections fed the same
    inputs; and
13. macOS, Windows, and Linux frame-time and image-quality reports on declared
    hardware.

“UE5-quality,” “ray traced,” or “better than” claims require the separate
shared-scene metric suite. Backend completion alone does not establish visual
parity.

## Immediate go/no-go

**Passed locally:** the RT0 contract tests, standalone Apple M5 Metal
BLAS/TLAS/dispatch/readback subgate, and the N3 view-dependent same-device
hybrid-HDR contribution slice including camera change, resize, and bounded
post-submission fault cleanup.

**Go:** RT1, RT2, and the remaining Ogre-Next/RoR-scene work in the
macOS-first RT3 spike.

**Do not go yet:** changing the default renderer, deleting OGRE14, importing
Ogre-Next development `master`, or claiming native RT.

**Hard continuation gates:**

- Metal RT must advance the proven family-9 scene composite to calibrated
  reflection/shadow semantics, GPU capture/timing, and soak evidence while
  M1/M2 fall back cleanly.
- The D3D11On12/DXR spike must prove Ogre-Next and DXR can share one correct
  Windows frame lifecycle. If it cannot, the renderer architecture must be
  revisited before a large content migration.

This preserves the user's deciding requirement: Ogre-Next is adopted only if
it can participate in a real, native, cross-platform RT architecture rather
than merely improving raster visuals.

[apple-metal-table]: https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf
[apple-metal-capability]: https://developer.apple.com/documentation/metal/mtldevice/supportsraytracing
[apple-hardware-rt]: https://developer.apple.com/documentation/metal/improving-your-games-graphics-performance-and-settings
[dxr-reference]: https://learn.microsoft.com/en-us/windows/win32/direct3d12/direct3d-12-raytracing
[dxr-capability]: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_feature_data_d3d12_options5
[d3d11on12]: https://learn.microsoft.com/en-us/windows/win32/api/d3d11on12/nf-d3d11on12-id3d11on12device-acquirewrappedresources
[vulkan-rt]: https://docs.vulkan.org/guide/latest/extensions/ray_tracing.html
[vulkan-rt-pipeline]: https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_ray_tracing_pipeline.html
[ogrenext-v300]: https://github.com/OGRECave/ogre-next/releases/tag/v3.0.0
[ogrenext-audit-commit]: https://github.com/OGRECave/ogre-next/commit/75f66d9dd48630693afad011f6223a2864b2455d
[ogrenext-render-systems]: https://github.com/OGRECave/ogre-next/tree/75f66d9dd48630693afad011f6223a2864b2455d/RenderSystems
[ogrenext-readme]: https://github.com/OGRECave/ogre-next/blob/75f66d9dd48630693afad011f6223a2864b2455d/README.md
[ogrenext-gi]: https://github.com/OGRECave/ogre-next/blob/75f66d9dd48630693afad011f6223a2864b2455d/Docs/src/manual/Rendering/GiMethods.md
[ogrenext-metal-device]: https://github.com/OGRECave/ogre-next/blob/75f66d9dd48630693afad011f6223a2864b2455d/RenderSystems/Metal/include/OgreMetalDevice.h
[ogrenext-vulkan-device]: https://github.com/OGRECave/ogre-next/blob/75f66d9dd48630693afad011f6223a2864b2455d/RenderSystems/Vulkan/include/OgreVulkanDevice.h
[ogrenext-vulkan-external]: https://github.com/OGRECave/ogre-next/blob/75f66d9dd48630693afad011f6223a2864b2455d/Samples/2.0/Tutorials/Tutorial_VulkanExternal/Tutorial_VulkanExternal.cpp
[ogrenext-d3d11-device]: https://github.com/OGRECave/ogre-next/blob/75f66d9dd48630693afad011f6223a2864b2455d/RenderSystems/Direct3D11/include/OgreD3D11Device.h
