# OGRE-Next isolated integration checkpoint

Status: **opt-in N1/RT4 raster frontend with bounded directional PSSM, Apple
Metal N2 geometry, N3 view-dependent hybrid HDR, soaked N4 native directional
hard shadows, bounded Vulkan/DXR semantic probes, and a non-admitted native
SDL window-host probe; no shipping renderer switch**

This checkpoint compiles a core standalone probe family plus platform-specific
Metal, Vulkan, and DXR executables against an exact OGRE-Next `v3-0` revision
while leaving legacy RoR builds unchanged. OGRE 14 builds now default to the
renderer-suite process topology described below, but still select the admitted
OGRE 14 compatibility child at runtime. The capability
executable proves that the reviewed platform renderer registers with OGRE core,
HLMS PBS links and selects the expected
shader family, and Compositor2 remains deferred before a window exists. It
does not create a render window. The frame executable then creates the
platform's reviewed hidden or null-window surface, initializes the logical
device and Compositor2 workspace, renders one PBR triangle, and performs an
independently validated UI-free GPU readback.

The third executable exercises the first renderer-neutral frontend slice. It
imports a real `RenderAssetRegistry` catalog into immutable Ogre v2 vertex and
index buffers plus a `VertexArrayObject`, maps a texture-free material into an
HLMS PBS datablock, renders a `SceneSnapshot`, and returns both
`RGBA16_FLOAT` and `RGBA8_SRGB` CPU attachments through `IRendererFrontend`.
It is deliberately an N1 admission slice, not a complete game renderer.

The fourth executable is an explicit macOS-only N2 acceptance slice. It asks
that same frontend to raster a full deformed `SceneSnapshot` revision, borrows
the exact live Ogre Metal device and command queue, exports the actual pooled
v2 `MTLBuffer` position and index slices selected by the raster `Item`, and
builds BLAS/TLAS directly from those slices. One Metal ray query must hit the
exported triangle at one metre and survive an independently validated eight-byte
GPU probe readback. It creates no second device or queue and produces no
ray-traced image.

The fifth executable is the explicit macOS-only N3 image slice. The frontend
retains the exact UI-free Ogre `RGBA16_FLOAT` render target, exports it through
the version-2 renderer-neutral image lease, bound to the exact immutable scene
owner and complete raster camera, and hands it to the native backend
on Ogre's own Metal device and queue. The backend derives camera rays from the
submitted view, traces the exact N2 geometry, writes a separate hit-only
contribution texture, and GPU-composites that contribution into the exported
HDR target. Independent raster-only, contribution-only, and hybrid GPU
readbacks prove both affected and untouched pixels. The slice supports one
view, one instance, one sample, and one primary-ray hit contribution; it does
not implement reflections, shadows, GI, denoising, multi-bounce transport, or
production material parity.

The sixth executable is the portable RT4/V1 directional-shadow slice. It
constructs a three-cascade PSSM shadow node and workspace entirely through
Compositor2 APIs, renders both UI-free HDR and SDR pairs, and toggles only one
occluder instance's cast flag. The proof accepts a pass only when changed
pixels stay on the receiver and darken in both formats; an unsupported host
writes an explicit report and exits with CTest skip code 77. It is not a
local-light, ray-traced-shadow, CityWorld, or shipping-quality claim.

The seventh executable is the explicit macOS-only N4 directional-shadow slice.
`NATIVE_DIRECTIONAL_HARD_SHADOW_V1` remains one renderer-neutral contract
shared by Metal, Vulkan KHR, and DXR adapters. The Metal implementation exports
the exact RT4 receiver, distinct occluder, and UI-free `RGBA16_FLOAT` target;
builds two BLAS and a two-instance TLAS; traces one primary camera ray followed
by one ray toward the directional light; writes R16 visibility and R32 lineage;
and GPU-composites the result on Ogre's device and queue. Its oracle accepts
only canonical R16 visibility (`0x3c00` visible, `0x0000` occluded): visible
pixels preserve every RGBA16 bit, while occluded pixels zero RGB and preserve
alpha exactly. Capability admission is fail-closed: the standalone N4 smoke
initializes its frontend, then records an explicit unsupported result if the
Metal backend rejects the device. The separately validated PSSM path exists,
and the pure `RendererStartupPlan` now resolves native N4, Ogre-Next PSSM,
OGRE14 PSSM, or rejection before either ABI is loaded. The planned production
launcher now has a separate, versioned `RendererStartupHandoff` contract which
selects only the packaged `RoR-Ogre14` or `RoR-OgreNext` child. It uses package
facts only, prefers Ogre-Next by default with an explicit legacy fallback,
requires a trusted package-platform match and a distinct production-readiness
admission fact, never accepts cross-platform backend identity, and never
crosses `OGRE_NEXT_REQUIRE` or `REQUIRE_NATIVE`. The readiness fact is supplied
by the caller until package/signing code derives it; N1 and other probes cannot
set it. Native preflight remains mandatory inside the selected Ogre-Next child
because device identity is process-local. The dependency-free
`RendererChildLauncher` core now enforces the next process boundary without
exposing a path override: it derives the running launcher's canonical directory
and launches only the exact selected sibling. It rejects a trusted package
platform which differs from the compile-time host before deriving the child
basename, preventing `.exe`/extensionless cross-host execution. POSIX uses
`execv`; Windows uses Unicode `CreateProcessW`, creates the child suspended,
resolves the executable handle to a final normalized DOS path, preserves its
validated extended-length `\\?\` prefix for exact sibling and long-path
semantics, assigns the child to a kill-on-close Job Object, and propagates the
full child exit code.
Arguments after `argv[0]`, the current working directory, environment,
and standard streams are inherited. A test-only fake child proves those
properties and proves that `PATH`, cwd, and a renderer-path environment decoy
cannot redirect selection. The contract and launcher core tests are wired into
all three platform-policy probe builds. OGRE 14 builds now package the public
launcher as `RoR`, with the real OGRE 14 game emitted as its exact
`RoR-Ogre14` sibling. A no-flag launch carries Ogre-Next-preferred/PSSM intent,
but immutable generated facts admit only the OGRE 14 child in this phase. The
launcher strips only exact options from the initial owned prefix, retains the
normalized intent independently, and now serializes an admitted future
Ogre-Next selection as an exact version/frontend/shadow/native-backend argv
prefix. The child decoder strips only that ordered prefix, owns the preserved
game suffix, binds the request and declared backend to the compiled host, and
rejects unknown, malformed, or duplicate reserved state before renderer
initialization. That codec now lives in the dependency-free
`RendererChildIntent` module rather than the OS process launcher. A dedicated
target includes and links only the intent side of this boundary, so the future
native child does not inherit `execv`, `CreateProcessW`, or compatibility-child
process ownership. The argv contract conveys intent rather than authenticating
a local caller. Linux/Windows install and CPack staging plus the signed macOS
application bundle now retain both exact executable roles. Flat macOS install
and CPack rules remain disabled so they cannot bypass Mach-O dependency
rewriting and nested-code signing; entering this topology also removes only
stale generated install/CPack control files from a reused macOS build tree.
The isolated probe now also builds a probe-only `RoR-OgreNext` executable with
the real POSIX `main` or Windows Unicode `wWinMain` entrypoint. It decodes a
synthetic versioned child intent, resolves the child-owned startup plan, and
constructs a seam-free `OgreNextN1Frontend` against the absolute reviewed
media root. The bounded callback selects `MODERN_PBR_RT4_V1`, the validated
three-cascade PSSM path, HDR off, a headless 64x64 extent, one frame in flight,
and vsync off, then performs a clean shutdown. Exit 77 is reserved solely for
the exact reviewed PSSM capability-unsupported result; every other rejection,
initialization failure, shutdown failure, or internal failure has a stable
nonzero diagnostic and fails CTest. This executable is not installed, staged,
bundled, or production-admitted, and it has no presentation, game bridge, UI,
input, or scene loop. Its real output name is evidence that the ABI/process
boundary can bootstrap, not an immutable package-readiness fact.
Every probe CTest launch now runs through a fail-closed wrapper which writes
`ror.ogre_next_child_runtime_execution_receipt.v1` even when the child returns
a nonzero result. The receipt binds the exact RoR and OGRE-Next commits, build
contract, platform renderer backend, four-record intent contract, child binary
SHA-256 and byte size before and after execution, and captured stdout/stderr
logs. Exit 77 is a skip only with the exact reviewed terminal marker (CRLF on
Windows, LF on macOS/Linux); every other nonzero or marker mismatch is a
failure. An OS-CSPRNG `execution_nonce` provides per-run uniqueness without
claiming challenge-response, while wall-clock timestamps are deliberately
omitted. CI independently validates the receipt, uses the pinned
`actions/attest` action to GitHub-attest both the receipt and exact
platform-selected child executable, verifies that DSSE bundle, and uploads the
receipt, binary, logs, and bundle. This evidence retention does not install,
stage, bundle, default-select, or production-admit the child.
On POSIX, timeout cleanup starts the child in a new session, kills its process
group, and reaps the direct child. Windows kills and reaps the direct child. A
separate build gate lexically scans the reviewed, pinned RoR/OGRE source closure
and rejects `fork`, spawn, exec, shell, and process-creation calls; the target
also excludes `RendererChildLauncher`. This is a reviewed-source-closure
constraint, not a proof over arbitrary injected linked code or a general
descendant-process sandbox. If any process-spawn call enters the reviewed child
closure, the build fails until cross-platform process-tree containment is
designed and admitted.

Separately, the probe builds a renderer-neutral presentation-window ownership
contract and a real pinned SDL 2.32.10 adapter. The adapter creates only a
hidden native window. On macOS it requires the Cocoa main thread, owns an
autoresizing `OgreMetalView` child of SDL's `NSWindow`, and serializes that
exact view as `externalWindowHandle`. On Windows it serializes SDL's exact
`HWND` as `externalWindowHandle`, never `parentWindowHandle`. On Linux it
forces SDL's X11 driver and uses the audited process-local `SDL2x11`
`{Display*, Window}` bridge with Ogre's Vulkan `Interface=xcb`; Wayland is an
explicit unsupported result. The Linux Ogre build retains both the hidden
`windowType=null` bootstrap and XCB presentation sources.

Window lifecycle is fail-closed. Creation starts hidden, Cocoa view/window/SDL
video ownership unwinds in reverse order, and show/hide complete only after a
bounded native SDL event acknowledgement. The first runtime validation records
one SDL/native-window owner thread on every platform; Cocoa additionally
requires and revalidates the AppKit main thread. Resume, suspend, resize,
metrics refresh, and shutdown reject a foreign thread before any native
callback or lifecycle mutation. Explicit successful owner-thread shutdown is
required before host/runtime destruction; destructors never attempt native UI
cleanup after owner validation fails.

Teardown is dependency-ordered and retryable. A destroy callback returning
false or throwing means that exact retained view, window, or SDL-video owner
remains live. The host enters `FAILED`, invalidates its renderer binding, stops
before destroying a dependency, and permits owner-thread `Shutdown` retry.
Only confirmed destruction clears a handle or ownership flag, and only complete
view -> window -> video teardown publishes `SHUTDOWN`. Resume then re-queries
and validates the same native window before publishing its post-show drawable
extent; it never commits the potentially stale hidden-window backing scale.
Logical resize does not publish a surface revision until a matching
Cocoa/WM_SIZE/X11 configure event and the acknowledged drawable-pixel extent
arrive. A separate metrics refresh observes
same-logical-size HiDPI/display-scale changes, so Retina migration can advance
the monotonic surface generation without pretending a resize occurred. SDL
event watches observe these acknowledgements without consuming close, focus,
minimize, input, or other-window events.

This remains a source/ABI/lifecycle checkpoint, not presentation admission.
The live smoke may report CTest skip 77 when a hosted runner has no native
window server. It does not create an Ogre presentation `RenderWindow`, attach
a Compositor2 workspace, swap or read back a presented frame, bridge game/UI/
input state, package SDL with `RoR-OgreNext`, or flip immutable readiness
facts. The pinned Vulkan/XCB implementation's device-wide resize wait is also
not accepted as a finite GPU-drain contract; real resize after Ogre surface
creation remains blocked on a reviewed drain strategy or upstream patch.

The compatibility child's runtime closure
and crash symbols remain the OGRE 14 closure; the dependency-free public
launcher is audited separately. The fake child is confined to the test output
directory and is never installed or staged. A complete production
`RoR-OgreNext` game child remains open, so current no-flag packages still run
OGRE 14 after the explicit policy fallback. This bootstrap is a real
cross-platform frontend initialization boundary and N4 is a real Metal
hard-shadow pass, not a soft-shadow, local-light, GI,
denoising, Vulkan KHR, DXR, production-material, image-quality, or performance
claim.

The production migration uses a supervisor-owned two-process topology rather
than loading both renderer ABIs into one address space. Its first
process-independent contract is `RendererBridgeEndpoint` version 1. The
`RendererBridgeProcessSupervisor` version-1 core accepts a caller-owned nonzero
128-bit session identifier, creates two unidirectional inherited byte streams,
and launches the OGRE 14 simulation/game host and Ogre-Next presentation
frontend with mirrored roles and stream directions.
Each child receives an exact six-record native argv prefix containing the
contract version, role, compile-time platform, lowercase session identifier,
and fixed-width native read/write handle tokens. Foreign platforms, unknown
roles, malformed or noncanonical tokens, all-zero sessions, reserved/equal
handles, reordered records, and duplicate reserved suffixes fail before a
child adopts any OS resource. The prefix composes after the existing renderer
intent: the public launcher first wraps game arguments with the bridge endpoint
and then wraps that result with Ogre-Next selection intent.

Before touching an OS resource, the supervisor executes and self-validates the
complete launch plan with reserved preview handles. It then resolves only the
two canonical executable siblings; cwd, `PATH`, environment, and caller path
overrides cannot select a child. POSIX creates close-on-exec bridge pipes plus
private close-on-exec startup controls, places both forked children in one
separate process group behind an atomic startup gate, closes every unrelated
non-standard descriptor in each child, and clears close-on-exec only on that
child's exact inbound/outbound endpoints. Per-child exec-error pipes distinguish
a successful `execv` from a partial startup. Windows creates both children
suspended with exact two-handle `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` allow-lists,
assigns both to one mandatory `KILL_ON_JOB_CLOSE` Job Object, and resumes them
only after both assignments succeed. Either implementation terminates and
reaps the peer when one direct child exits, never leaves a direct zombie, and
retains the game's exact POSIX wait status (exit or signal) or full Windows
`DWORD` exit code for a later public-launcher propagation call.

The native fake-child gate runs from a decoy cwd and `PATH`, validates its exact
role/session and inherited endpoints, sends one valid asset envelope followed
by one valid scene envelope on the shared game-to-presentation stream, and
returns an acknowledgement on the reverse stream. It also covers a natural
game exit, a POSIX terminating signal and exact propagation, presentation-first
peer teardown, and a missing presentation executable during partial startup.
These children and the supervisor test harness are test-only and never enter
install or package rules.

The session identifier in argv binds both endpoints to one launch transaction;
it remains a local consistency value rather than an authentication boundary.
`RendererPublicLauncher` now calls the supervisor only after the immutable
handoff selects a production-admitted Ogre-Next child. It supplies a nonzero
per-transaction session, preserves the original game argv behind both bridge
contracts, and propagates the game host's exact exit code or POSIX terminating
signal. A presentation-first or partial-startup failure is terminal and never
causes an unreviewed runtime fallback to the legacy child. The existing policy
still selects the exact single-process `RoR-Ogre14` sibling when
`OGRE_NEXT_PREFER` is allowed to fall back, while `OGRE_NEXT_REQUIRE` remains a
hard gate.

A test-only admitted-facts fixture runs the real public entrypoint against the
two exact fake siblings on all three platform policies. It covers no-flag
Ogre-Next preference, explicit Ogre-Next requirement, Unicode/space-containing
game argv, exact game exit propagation, and presentation-first teardown. The
production package generator remains unchanged and still records Ogre-Next as
absent/unadmitted, so current packages continue to choose OGRE 14. This wiring
does not itself make either child consume production scene/input traffic or
change generated presence/readiness/admission facts. Production stream
back-pressure, endpoint handshake/EOF policy, game/frontend consumers, and
package admission remain separate gates.

Configure and build the renderer suite explicitly (the launcher is already the
default when `ROR_OGRE14=ON`):

```sh
cmake -S . -B build-renderer-launcher \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=cmake/conan_provider.cmake \
  -DROR_CREATE_CONTENT_FOLDER=OFF \
  -DROR_OGRE14=ON \
  -DROR_RENDERER_PUBLIC_LAUNCHER=ON
cmake --build build-renderer-launcher \
  --target ror_renderer_launcher --config Release
```

The active runtime output directory is `build-renderer-launcher/bin` for this
tree. It contains exact siblings `RoR` and `RoR-Ogre14` on Linux/macOS, or
`RoR.exe` and `RoR-Ogre14.exe` on Windows. The same roles are retained by
Linux/Windows install/package targets and by `RoR.app/Contents/MacOS` on
macOS. Initialize the starter-content submodule and omit
`-DROR_CREATE_CONTENT_FOLDER=OFF` when that content is required in the build
tree.

Linux RT6 and Windows RT7 now mirror the renderer-neutral N4 sample semantics
inside their native API probes: two distinct BLAS, a two-instance TLAS, one
receiver ray and one +light visibility ray per sample, canonical R16 visibility,
R32 instance lineage, and RGBA16 hybrid output. DXR uses typed UAVs; Vulkan
packs the same binary16 and R32 encodings into a std430 storage buffer. Both
reports are deliberately marked `semantic_probe_only`; neither claims that its raster sample came from
the exact Ogre target or that its hybrid result was composited back into an
Ogre image. Their portable validators are green on macOS, while real Vulkan RT
and DXR execution remains a Linux/Windows hardware gate.

The original capability and frame probes do not consume a RoR scene. The N1
through N4 and N4A executables consume the renderer-neutral RoR scene and asset
contracts,
but never link into the OGRE 1.14 executable or touch simulation/solver state.
N2 evaluates capability and exact geometry interop without producing an image;
N3 adds the first measured view-dependent hybrid scene output, and N4 adds
directional visibility over distinct receiver and occluder geometry. None of the
executables is a shipping presentation-window, performance, or visual-quality
claim.

## Reproducible dependency contract

[`ogre-next.lock.json`](../../tools/ogre_next_probe/ogre-next.lock.json) pins:

- official `OGRECave/ogre-next` branch `v3-0` commit
  `37149a802de747f6806996fa3067b0748ecc1084` and its archive SHA-256;
- the upstream core MIT `COPYING` file and its SHA-256;
- the loaded `Samples/Media/Hlms` shader tree's combined
  `MIT AND LicenseRef-Heitz-LTC-Paper-Notice` expression, including the exact
  `AreaLights_LTC_piece_ps.any` source hash and a checked-in notice preserving
  its Eric Heitz, Jonathan Dupuy, Stephen Hill, and David Neubelt attribution,
  paper-reference condition, and source/binary redistribution terms;
- FreeType `2.14.3` from its official Savannah release archive with the
  official SourceForge mirror as an ordered availability fallback; both
  transports must resolve to the same pinned archive SHA-256. The lock also
  records its `FTL OR GPL-2.0-or-later` license expression, the
  selected `GPL-2.0-or-later` license and overview hashes, static linkage, and
  the disabled BZip2, Brotli, HarfBuzz, PNG, and ZLIB optional dependencies;
- RapidJSON `v1.1.0`, required by OGRE core even when optional tools and scene
  components are disabled, with its source archive's
  `MIT AND BSD-3-Clause AND JSON` expression, the active reviewed header
  subset's `MIT` expression, and the complete upstream notice hash;
- ABI-relevant choices: C++17, static linking, allocator/threading/string
  layout, precision, `IdString` width, node inheritance, and SIMD family.

The RT4 normal slice adds a feature-specific
[`ogre-next-normal-map-source.lock.json`](../../tools/ogre_next_probe/ogre-next-normal-map-source.lock.json).
Its whole-file digest and 23 ordered owner hashes lock the exact PBS pixel
decode and vertex TBN transform, UV and full32 sampling macros, Metal/GLSL/HLSL
FLOAT4 tangent declarations, slot/datablock behavior, `Image2`/`TextureBox`
row layout, pixel-format metadata, and D3D11, Metal, and Vulkan `RG8_UNORM`
mappings. CMake verifies those files in the extracted pinned source, and both
the runtime report and artifact attestation carry the feature-lock digest.

The lock, license, ABI, platform, and FetchContent policy lives in the shared
standalone CMake module
[`PinnedOgreNext.cmake`](../../tools/ogre_next_probe/cmake/PinnedOgreNext.cmake).
The entry project and N1 target include that one policy rather than copying its
pin block. Both the shared module and the N1 CMake guard reject an existing
`OgreMain` target, preventing OGRE 1.14 and Ogre-Next from entering one binary.

The generated schema-5 build contract records the exact FreeType version,
archive, source/package license paths and hashes, derived `STATIC_LIBRARY`
target type, Overlay target linkage, and disabled optional dependency set
alongside the Ogre-Next and RapidJSON pins. Schema 4 remains the historical HDR
contract and is read only for older evidence. The build contract and N1
runtime report also identify the canonical RoR repository, exact Git
commit/ref, and a sorted path/size/SHA-256 manifest of the renderer sources and
probe implementation used by the build. The wrapper independently regenerates
that manifest from the checkout, so locally modified relevant source cannot
masquerade as the named commit.

FetchContent uses URL hashes and a build-local `_deps` population area. Local
Ogre-Next, RapidJSON, and FreeType archives can be supplied, but each is hashed
before CMake sees it. FreeType is built first as the reviewed static target and
is explicitly wired into Ogre's Overlay component instead of accepting a host
Homebrew, distro, or Windows package-cache library. Direct
`FETCHCONTENT_SOURCE_DIR_*` overrides are rejected because they bypass archive
verification. A normal invocation requires a fresh build directory. The sole
reuse path is `--reuse-build-dir` with an explicitly selected `n1`, `n2`,
`n3`, or `legacy`
checkpoint; it requires the exact ownership sentinel and source directory,
revalidates the build contract, and rejects archive or generator changes.
`--clean-build-dir` recovers only a directory carrying the exact probe
ownership sentinel. No OGRE-Next source archive is stored in this repository.

The adaptation fixes only two non-Xcode macOS assumptions in the pinned
upstream CMake: SDK path resolution and Xcode-only framework staging tokens in
Ninja files. It is applied from a hash-locked patch before configuration. On
Linux, a second hash-locked patch removes OGRE's short local redeclaration of
`glslang::SpvOptions` and compiles against the exact pinned glslang header, so
the C++ ABI is never guessed. A third hash-locked patch keeps glslang's install
export disabled alongside shaderc's reviewed static skip-install build; this
avoids an invalid partial export while leaving the compiled targets intact.

Linux shader compilation has its own canonical
[`linux-shader-toolchain.lock.json`](../../tools/ogre_next_probe/linux-shader-toolchain.lock.json).
It builds [shaderc `v2025.3`](https://github.com/google/shaderc/tree/v2025.3)
from commit `8c2e602ce440b7739c95ff3d69cecb1adf6becda` and the exact compatible
family selected by that release's `DEPS`: glslang commit
`efd24d75bcbc55620e759f6bf42c45a32abac5f8`, SPIRV-Tools commit
`33e02568181e3312f49a3cf33df470bf96ef293a`, and SPIRV-Headers commit
`2a611a970fdbc41ac2e3e328802aed9985352dca`. Every source archive, upstream
license, dependency manifest, compatibility patch, and OGRE-embedded
SPIRV-Reflect source/header is SHA-256 locked. Distro shaderc, glslang, and
SPIR-V C++ archives are deliberately not accepted: their independently moving
versions and transitive target layouts cannot provide the same cross-distro
C++ ABI contract.

The Linux link consumes the source-built `shaderc_combined` target. A generated
`ogre-next-linux-static-closure.json` records the actual compiler, source
commits, and SHA-256 of all seven build outputs (`shaderc_combined`, `shaderc`,
`shaderc_util`, `glslang`, `SPIRV`, `SPIRV-Tools-opt`, and
`SPIRV-Tools-static`). A separate verification target rehashes the files after
linking and fails if the manifest no longer matches. The Vulkan loader remains
the explicit host-provided dynamic system boundary; CI rejects a dynamic
shaderc, glslang, SPIRV-Tools, or split glslang/SPIR-V component dependency in
both the frame probe and staged N1 executable.

The N1 frontend never compiles the FetchContent `_deps` path into its library.
Its constructor requires a caller-owned absolute shader-media root,
canonicalizes it, and verifies the exact regular-file set, byte sizes, and
SHA-256 digests of all pinned HLMS media before claiming the process-global
Ogre Root or creating a device. Symlinks, missing files, extra files, and
changed bytes fail closed. The standalone build stages a relocatable proof
package with this layout:

```text
ror-ogre-next-n1-package/
  bin/ror_ogre_next_frontend_n1_smoke[.exe]
  share/rigsofrods/ogre-next/Samples/Media/Hlms/
  licenses/Rigs-of-Rods-GPL-3.0.txt
  licenses/Ogre-Next-MIT.txt
  licenses/RapidJSON-license.txt
  licenses/FreeType-GPLv2.txt
  licenses/FreeType-LICENSE.txt
  licenses/LicenseRef-Heitz-LTC-Paper-Notice.txt
  licenses/IBLBaker.txt
  # Linux additionally carries:
  licenses/Apache-2.0.txt
  licenses/glslang-LICENSE.txt
  licenses/SPIRV-Tools-LICENSE.txt
  licenses/SPIRV-Headers-LICENSE.txt
  provenance/ogre-next-linux-shader-toolchain.lock.json
  provenance/ogre-next-linux-static-closure.json
```

All seven common staged license/notice files are copied from hash-validated
sources and byte-compared before the package stamp is written. Linux adds four
shader-toolchain notices, for eleven total. The Linux source lock and
built-archive manifest are likewise byte-compared into `provenance/`;
incomplete or changed licensing/provenance data prevents package completion.

The staged executable is run with the resolved absolute form of
`share/rigsofrods/ogre-next/Samples/Media` from outside its `bin` directory.
Application packaging must resolve that same relative resource path using the
platform bundle/install locator and pass it through `OgreNextN1Configuration`;
relative, missing, or incomplete roots fail before native initialization.
The build wrapper also hashes all seven common staged license/notice files
against the checked-in RoR license and the locked Ogre-Next, RapidJSON,
FreeType, HLMS shader, and IBLBaker provenance. On Linux it verifies the four
additional shader-toolchain notices as well. It then independently compares
the staged HLMS manifest with the pinned source tree before accepting the
package. A native negative test corrupts one staged shader and requires an
integrity-specific initialization failure.

## Platform policy

| Host | Required architecture | Probe renderer | HLMS shader family |
| --- | --- | --- | --- |
| macOS | arm64 | Metal | Metal |
| Windows | x86_64 | Direct3D 11 | HLSL |
| Linux | x86_64 | Vulkan, null-window backend compiled | GLSL |

Any other host/architecture fails configuration. A missing renderer target,
SDK, or Vulkan/D3D dependency also fails rather than substituting another
renderer. The Linux policy compiles the null-window backend. The capability
executable initializes a Vulkan instance and enumerates physical devices
without creating a logical rendering device. The frame executable then
creates the Vulkan logical device, null-window offscreen target, Compositor2
workspace, and RGB8 frame. It still does not prove a Linux presentation
surface or shipping game window. Linux needs only the Vulkan loader and driver
from the host distribution; shader compiler sources and their static closure
come exclusively from the reviewed lock, yielding the same source family on
Ubuntu, Fedora, Arch, and other x86_64 distributions rather than selecting
whatever package versions happen to be installed.

## N1 frontend contract

N1 admits exactly one headless/offscreen color view and one synchronous frame
in flight. Its supported slice is intentionally small:

| Area | N1 behavior |
| --- | --- |
| Geometry | Immutable static triangle lists with authored positions and normals, imported as Ogre v2 mesh/VAO allocations after derived native Aabb/sphere values are proven finite |
| Materials | Texture-free opaque metallic-roughness; IOR fixed to 1.5; `PbsBrdf::Default` height-correlated GGX; glTF-style double-sided lighting; live datablock getters verify base color, metalness, roughness, emissive, and sidedness after mapping |
| Lighting | Constant ambient/environment radiance only |
| Output | One UI-free `RGBA16_FLOAT` HDR or `RGBA8_SRGB` CPU readback |
| Camera | Current rigid view and canonical portable `[0,1]` projection; N1 explicitly converts depth to Ogre `[-1,1]` before the active RenderSystem performs one API-native conversion |
| Lifecycle | Transactional catalog replacement, RAII rollback for newly allocated native assets, teardown failure propagation/fault latch, process-global Ogre Root exclusion, contiguous frame IDs represented by a completion high-water mark, and weak snapshot-owner identities pruned after caller release |

The default N1 tier fails closed for textures/samplers, richer vertex streams,
deformable or dynamic meshes, particles, every analytic light, shadows,
depth/motion/object ID attachments, exposure/jitter, multiple views,
presentation, native interop, and ray tracing. Analytic lights remain rejected
because the portable units and Ogre power/attenuation path are not yet
calibrated; N1 does not substitute an approximate local-light curve. Mirrored
TRS is also rejected because Ogre's signed parent scale can produce a negative
world-bound radius.

## RT4/V1 opt-in raster tier

`MODERN_PBR_RT4_V1` is the first portable raster extension of N1. It keeps the
same immutable scene and lifecycle contract while adding authored tangent/UV0
vertices, sRGB base-color and emissive uploads, packed linear
metallic-roughness channel extraction, the first normal-map slice, sampler
translation, and one calibrated directional light. The normal slice admits
only canonical positive-Z tangent-space normals authored as linear RGBA8 with
alpha 255 in every texel and mip. Every authored B value must agree with
`sqrt(max(0, 1 - x*x - y*y))` within exactly `1/255`; negative-Z encodings and
normal scales other than exactly 1 fail closed. This preserves the renderer-
neutral glTF normal contract instead of misusing Ogre's normal-map weight,
which lerps the reconstructed normal rather than scaling tangent-space X/Y.
The frontend derives a linear `RG8_UNORM` allocation, binds it to
`PBSM_NORMAL` with UV0 and the authored sampler, and leaves Ogre's weight at
exactly 1. The exact input rows and every mip are copied into Ogre images;
padded source rows are accepted without uploading their padding. Textures that
are not referenced by a live material are not allocated.

RT4/V1 additionally rejects genuine non-uniform object scale. The pinned PBS
vertex template only selects inverse-transpose normal handling behind an
unset property and always transforms the authored tangent with the world
matrix; admitting non-uniform scale would therefore manufacture a non-
orthogonal, renderer-specific TBN. Uniform scale and float-level rotation
composition noise remain admitted. The native smoke proves this gate fails
before submission and does not consume the frame identity.

The tier also proves live texture replacement rather than only successful
creation. Its 2x2/one-mip -> 4x2/two-mip -> 2x2/one-mip sequence records native
create/destroy/live counts and requires every retired Ogre texture name to be
absent before reuse. The rollback and replacement sequence uses the derived
normal `RG8_UNORM` allocation directly, including padded rows and multiple
mips. The smoke reads Ogre's own row-pitched `Image2` RG bytes back exactly
before residency upload, and the artifact validator independently checks the
resulting counters. A separate pair of controlled native HDR/SDR captures
changes only authored FLOAT4 tangent `w` from +1 to -1 and proves the pinned
handedness path affects pixels. Seven otherwise-identical HDR/SDR captures show that base
color, roughness, metallic, emissive, the normal map, and sampler/UV changes
each affect rendered pixels. The report, PPM, packed isolation captures,
executable, source manifest, build contract, normal-map source-owner lock, and
Ogre/media provenance are atomically hash-bound and semantically revalidated
before publication.

RT4/V1 now also owns a native Ogre-Next parallax-corrected cubemap runtime. It
captures at most one scheduler-selected probe per frame into isolated
`RGBA16_FLOAT` faces, runs the pinned IBL filter chain, measures every active
face/mip byte, and issues the concrete receipt required by the renderer-neutral
scheduler. A candidate generation remains unbound from PBS sampling and absent
from the public audit until every other fallible frame stage has prepared
successfully. The first-frame abort proof checks both the unchanged audit and
the native ownership ledger: the PCC create/destroy counts balance, no PCC
remains live, and PBS is unbound.

Creating Ogre's PCC raises HlmsPbs' automatic specular-IBL mip high-water mark.
N1 therefore admits the adapter only with its freshly created, exclusively
owned HlmsPbs in automatic mode; N1 exposes no reflection/environment texture
slot and never selects manual IBL mip policy. After an uncommitted or shutdown
PCC is fully unbound and destroyed, RoR calls Ogre's public
`resetIblSpecMipmap(0)` to recompute that state from the remaining live
datablocks. Any failed ownership cleanup fault-latches the frontend and requires
complete Root/Hlms teardown.

On Metal, RT4/V1 may run simultaneously with N3. That path uses an explicitly
reviewed 48-byte position/normal/tangent/UV0 layout for both Ogre rasterization
and exact native position-slice export; the original N2 proof remains frozen to
its reviewed 24-byte position/normal layout. The combined Apple M5 checkpoint
rendered the textured, directionally lit geometry into the exact retained HDR
target and composited the native primary-hit contribution on the same device
and queue.

This is not the complete RT4 or V1 gate. Occlusion textures remain fail-closed:
the pinned HLMS PBS surface has no ambient-occlusion-only texture slot, and the
frontend does not repurpose detail-map weight or multiply direct lighting.
Only canonical positive-Z unit-scale normal maps and one directional-light
calibration are admitted. There are no local-light shadows, SSR, diffuse-GI,
presentation, CityWorld, or performance claims yet. The opt-in HDR compositor
proves deterministic native auto-exposure, bloom, tone mapping, and R16 temporal
history for the isolated fixture; production display transfer/gamut and image
acceptance remain open. N3 proves geometric hit contribution only; it does not
claim ray-material parity.

## RT4/V1 directional PSSM checkpoint

`PSSM_3_CASCADE_V1` is separately opt-in and valid only with
`MODERN_PBR_RT4_V1`; the default configuration remains `DISABLED` and retains
the previous zero-shadow admission and pixel path. Enabled admission requires
exactly one directional light with a nonzero static/dynamic shadow mask. Point
and spot lights, multiple lights, other raster tiers, and views whose near/far
planes differ from exactly 0.5 m and 350 m fail closed. Mesh cast admission is
the intersection of the light mask, instance cast flag, and mesh static or
dynamic class. Receive admission remains the independent instance receive
flag; non-receivers use frame-local PBS datablock clones with native getter
verification and exact create/destroy auditing.

The reviewed policy is three cascades, lambda 0.97, blend 0.125, one metre
split padding, 0.313 terminal fade, 1.5 XY padding, one stable cascade, and
PCF 4x4. A single sampled `D32_FLOAT` 2048x3072 atlas has an explicit
2048x2048 first region and two ordered 1024x1024 lower regions. RoR writes and
reads back every atlas, split, blend, fade, bias, filter, light-count, caster,
receiver, and lifecycle property. It never substitutes a format, resolution,
filter, backend, or unpinned compositor script. Capability admission itself is
an exact transactional backend allocation: RoR creates the reviewed D32 atlas,
makes it resident, performs an `Image2` readback through the backend's async
texture ticket, verifies its dimensions and format, destroys it, and proves
the reserved name no longer exists. A generic format-support answer cannot
admit the feature.

The portable projection is admitted only when it is a canonical finite
perspective matrix for the fixed near/far contract. RoR converts its side
planes to `FET_TAN_HALF_ANGLES`, then verifies Ogre's generated native
projection so cascades two and three retain the reviewed field of view as the
PSSM setup changes clip distances. The native visibility mask is explicitly
limited to Ogre's lower 30 portable bits. Frame-local receiver datablocks and
autogenerated workspace-node definitions are removed transactionally, with
injected post-create failures proving that the identical frame can retry.
The native fixture also renders a nonzero horizontal/vertical lens offset with
an exact zero-thickness receiver AABB, then binds its reviewed tangents and
caster bounds to a separate pair of UI-free GPU readbacks.

[`ogre-next-pssm-shadow-v1.lock.json`](../../tools/ogre_next_probe/ogre-next-pssm-shadow-v1.lock.json)
pins the 86 upstream source and HLMS shader files on which this behavior
depends. The standalone build runs
`verify_pssm_shadow_source_closure.py` against the extracted canonical Ogre
commit before compiling the frontend; missing files, path indirection, hash
drift, reordered roles, duplicate JSON keys, or lost behavioral seams stop the
build. The closure includes the Mesh-to-Item-to-SceneManager caster/AABB path,
portable scalar/NEON/SSE AABB implementations, base allocation/readback
ownership, and Metal, Vulkan, and D3D11 D32 allocation and async-ticket owners.
Apple Metal,
Linux Vulkan, and Windows D3D11 all compile and run the same adapter and smoke.
Capability gaps such as a missing 2048x3072 `D32_FLOAT` render target or
texture-gather PCF support are recorded as unsupported, never as a fallback.

Selecting `METAL_RAY_TRACING_N2` is explicit and Apple-only. It admits full
position/normal deformation snapshots solely to create immutable per-frame
Ogre v2 buffers used by both raster and native export. Windows/D3D11 and
Linux/Vulkan keep native interop and native RT false/null, and the default N1
configuration preserves the original fail-closed policy on every platform.

The Metal bridge publishes byte-exact pooled slices using
`_getFinalBufferStart() * getBytesPerElement()` plus the position element
offset. Offset, span, stride, count, index format, frame generation, device,
and `MTLBuffer.length` are validated before export. A single `MTLSharedEvent`
orders Ogre release, external BLAS/TLAS/query work, and Ogre reacquisition on
the exact Ogre queue. Every encoder ends before its signal; the only CPU wait
is bounded and happens after submission. Stale generations, replacement while
leased, timeout, and frontend-before-backend shutdown all fail closed.

N3 extends that neutral contract with a versioned color-image request/export,
explicit usage and state handoff, generation validation, and a separately
released image lease. The Metal implementation exports the exact live
`MTLTexture`; no Metal type enters the public renderer contract. A completed
frame releases both geometry and image leases before resize or replacement.
Submitted timeout/device-loss paths keep ownership conservative until bounded
backend shutdown abandons the native work, after which frontend shutdown can
complete. Vulkan KHR and D3D12/DXR backends must mirror the same neutral
contract rather than adding Metal assumptions to shared code.

Before a device exists, the capability query reports a conservative 2048
texture extent without treating it as an initialization ceiling. Initialization
creates only a 64x64 hidden/null bootstrap, reads
`RenderSystemCapabilities::getMaximumResolution2D()`, validates the requested
offscreen extent against that exact limit, and then publishes the device value.
This allows real 4K/5K-capable devices without overclaiming an uninitialized
device.

The CMake routing selects Metal on macOS arm64, Direct3D 11 on Windows x64,
and Vulkan null-window on Linux x86_64. The same strict-warning N1 sources are
compiled twice in each matrix job: the established seamful smoke library keeps
all fault-injection evidence and package contents unchanged, while the new
runtime library exposes no `ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM` or
`ROR_OGRE_NEXT_N2_TEST_SEAM` definition to the child. The actual child CTest
runs on all three hosts. Only the macOS Metal runtime has been proven locally
at this checkpoint. This is not evidence for presentation on any platform or
for Windows DXR.

## Run

The wrapper downloads only the pinned archives when local copies are not
provided, configures the standalone project, builds the capability target,
runs it, and validates the machine-readable report:

```bash
python3 tools/run_ogre_next_probe.py \
  --build-dir /tmp/ror-ogre-next-probe
```

For an offline/cached run:

```bash
python3 tools/run_ogre_next_probe.py \
  --build-dir /tmp/ror-ogre-next-probe \
  --clean-build-dir \
  --ogre-archive /path/to/ogre-next-pinned.tar.gz \
  --rapidjson-archive /path/to/rapidjson-pinned.tar.gz \
  --freetype-archive /path/to/freetype-2.14.3.tar.xz
```

On macOS, the full local N4 evidence gate is an explicit follow-up to the
complete probe so unsupported hosts can still retain a capability report:

```bash
cmake --build /tmp/ror-ogre-next-probe \
  --target ror_ogre_next_metal_n4_directional_shadow_report \
  --config Release --parallel 2
python3 tools/verify_ogre_next_artifact_set.py \
  --build-dir /tmp/ror-ogre-next-probe \
  --verify-metal-n4-evidence
```

The build directory must be fresh. Pass `--clean-build-dir` only to recover a
previous directory created by this probe; the wrapper refuses to clean a path
without its ownership sentinel or any path overlapping the source checkout.

The generated files are:

- `ogre-next-build-contract.json`: configured dependency and RoR source
  provenance, platform, compiler, ABI, and component policy;
- `ror-ogre-next-probe-report.json`: runtime registration/linkage evidence;
- `ror-ogre-next-frame-probe-report.json`: native-window, HLMS PBS,
  Compositor2, and GPU-readback claims; and
- `ror-ogre-next-frame-probe.ppm`: the UI-free RGB8 pixels independently
  checked against the frame report;
- `ror-ogre-next-frontend-n1-report.json`: N1 asset, material, HDR/SDR,
  identity, and recovery evidence; and
- `ror-ogre-next-frontend-n1.ppm`: the exact N1 sRGB CPU readback independently
  hashed by the wrapper;
- `ror-ogre-next-pssm-shadow-report.json`: exact topology, split, capability,
  isolation, lifecycle, and source/build provenance, or explicit unsupported
  evidence;
- `ror-ogre-next-pssm-shadow-isolation.bin`: tightly packed HDR/SDR
  cast-disabled and cast-enabled readbacks, present only for a native pass;
- `ror-ogre-next-pssm-shadow-execution-receipt.json`: atomically written child
  process receipt binding its fresh execution challenge, exit status, exact
  executable, report, evidence, build contract, source, and workflow identity;
- `ror-ogre-next-pssm-shadow-attestation.json` and
  `ror-ogre-next-pssm-shadow-artifact-manifest.json`: persisted exact-subject
  SHA-256 bindings for the receipt and complete PSSM artifact set;
- `ror-ogre-next-pssm-shadow-execution-receipt.sigstore.jsonl`: on trusted
  non-PR CI runs, the GitHub OIDC/DSSE provenance bundle for the execution
  receipt, independently verified against the repository, workflow, source
  ref, and source digest before upload;
- `bin/ror_ogre_next_pssm_shadow_smoke`: the strict-warning cross-platform
  executable retained beside the report;
- `ogre-next-linux-static-closure.json`: on Linux, exact source and
  built-library provenance with a per-archive SHA-256, also retained inside
  the N1 package;
- `ror-ogre-next-metal-n2-report.json`: versioned same-device provenance,
  geometry-slice, timeline, BLAS/TLAS, ray-hit, and lifecycle evidence on
  Apple family 9 or newer; and
- `ror-ogre-next-metal-n2-probe.bin`: the exact eight-byte GPU-written ray-hit
  evidence, present only when the hardware gate passes;
- `ror-ogre-next-metal-n2-attestation.json`: checked-out RoR commit/ref plus
  SHA-256 identities for the report, executable, and optional probe; and
- `bin/ror_ogre_next_metal_n2_smoke`: the exact executable independently hashed
  by the wrapper and retained with the report; and
- `ror-ogre-next-metal-n3-report.json`: versioned device, image-contract,
  view-dependence, resize, fault-path, and image-metric evidence;
- `ror-ogre-next-metal-n3-{raster,contribution,hybrid}.bin`: exact tightly
  packed 96x64 `RGBA16_FLOAT` GPU readbacks, present only when N3 passes;
- `ror-ogre-next-metal-n3-attestation.json`: source and SHA-256 identities for
  the N3 report, executable, and optional image artifacts; and
- `bin/ror_ogre_next_metal_n3_smoke`: the exact N3 executable retained with
  those artifacts;
- `ror-ogre-next-metal-n4-directional-shadow-report.json`: versioned device,
  dual-geometry, ray-lineage, visibility, coverage, sample, and executable
  provenance for N4;
- `ror-ogre-next-metal-n4-raster.bin`,
  `ror-ogre-next-metal-n4-visibility-r16.bin`,
  `ror-ogre-next-metal-n4-ray-lineage-r32.bin`, and
  `ror-ogre-next-metal-n4-hybrid.bin`: exact 96x64 GPU readbacks, present only
  when N4 passes; and
- `bin/ror_ogre_next_metal_n4_directional_shadow_smoke`: the exact executable
  whose size and SHA-256 are bound into the N4 report. The independent artifact
  verifier recomputes all four payload hashes and validates every texel rather
  than trusting the report.

The baseline GitHub `macos-15` arm64 runner currently identifies an
`Apple Paravirtual device` exposing only `OSX_GPUFamily1_v1`, so it compiles all
N2/N3/N4 code and records explicit capability skips (CTest exit 77) instead of
claiming a family-9 runtime pass. A genuine M3-or-newer runner must produce the
N2 probe, all three N3 images, and all four N4 readbacks and pass their
independent validators.
The paravirtual device also produces about one third of the physical M5
luminance for these emissive N1 and texture-backed RT4 fixtures. Both fixtures
therefore use scene-linear energy with enough headroom to prove an unclamped
`RGBA16_FLOAT` attachment on either device. That is only an HDR-storage gate:
the V1 backend-oracle comparison, not this smoke, owns cross-device photometric
parity.

`--validate-contract-only` checks pins, patch hashes, and the current platform
policy without accessing the network or compiling.

## Completed local checkpoint

On 2026-07-31 the Release probe configured and built natively with AppleClang
21 for macOS arm64 and the macOS 11.0 deployment floor. The executable found
the Apple M5 through OGRE-Next Metal, registered exactly one reviewed renderer,
reported three Metal configuration options, linked HLMS PBS with
`Hlms/Pbs/Metal`, and observed Compositor2's pre-window deferred state. A
second executable then created a hidden native Metal window, rendered a manual
PBR triangle through HLMS PBS and Compositor2 into a 192x128 UI-free RGB8
target, and read the pixels back from the GPU after four frames. An independent
validator confirmed 203 distinct RGB8 values, 5,514 non-background pixels, a
0.011497 to 0.922464 luminance range, and the FNV-1a-64 pixel hash
`47f35fe4bdec9207`. The version-2 frame report is joined to the capability
report by the exact OGRE-Next commit/archive, ABI cookie, platform policy, and
renderer; it also records the initialized Apple M5, surface mode, and clean
renderer shutdown. Validation recomputes the luminance extrema and requires a
dominant four-corner background plus bounded foreground geometry through the
frame center, so plausible metadata over blank or noisy pixels fails closed.
Both reports retained
`native_ray_tracing: not_evaluated`. The checked-in
[machine-readable evidence record](evidence/OGRE_NEXT_METAL_PROBE_M5_2026-07-31.json)
captures the source, dependency, build-contract, executable, and runtime-report
hashes plus the exact host and toolchain. The build contract, both runtime
reports, and the exact P6 PPM encoded as deterministic base64-gzip evidence are
retained beside that record. The locally built executables are not committed;
their basenames, hashes, and explicit non-retention state are recorded without
ephemeral absolute paths.

The N1 Metal smoke additionally synchronized a renderer-neutral static
triangle/PBR catalog, rejected an unsupported depth request without consuming
frame identity, and rendered both readback formats. `RGBA16_FLOAT` produced
2 distinct quantized RGB values, 2,104 non-background pixels, luminance
0 to 1.52817905, and FNV-1a-64 hash `ae63b06b829ba785` while preserving HDR
energy above 1.05. `RGBA8_SRGB` produced 2 distinct colors, the same 2,104
foreground pixels, luminance 0 to 0.869247854, and hash
`55526239ca9bd6a5`. The executable then accepted replay of both the latest and
an older immutable snapshot only when the exact caller-owned snapshot identity
matched, rejected identity aliases, proved simultaneous process-global Root
ownership is denied, shut down, transferred Root ownership to a second
frontend, reinitialized, resynchronized, rendered again, and shut down cleanly.
Its report also records that live HLMS getters matched the reviewed metallic
workflow and height-correlated GGX mapping. These values are local macOS
evidence, not cross-platform golden pixels.

The opt-in N2 smoke then passed on the same Apple M5. Ogre rastered deformation
revision 2 and exported its live pooled 24-byte-stride vertex allocation and
16-bit index allocation with 60-byte and 6-byte exact slices, respectively.
The native backend used those exported buffers directly for a 512-byte BLAS
and 512-byte TLAS, ordered the two queue stages with shared-event values 1 and
2, and read back the expected `0x52545254` hit at distance 1.0 in the exact
eight-byte GPU result. The earlier synthesized 96x64 RGBA payload was not a
rendered RT frame and is no longer emitted. The smoke also rejected
a stale buffer generation, blocked revision N+1 and frontend shutdown while N
remained leased, exercised explicit backend-first shutdown plus both destructor
orders, rendered revision N+1 after backend destruction, and then shut down the
frontend. These measurements prove this M5 geometry path;
they do not prove ray-traced materials, lighting, denoising, Ogre texture
import, compositing, presentation, image quality, or performance parity.

The opt-in N3 smoke then passed on the same Apple M5. Its exact 96x64 GPU
readbacks contained 80 affected pixels and 6,064 bit-identical miss pixels.
Raster-only, hit-contribution, and hybrid SHA-256 values were respectively
`b00d575ae105ff80bc02fc5b238cd109f1956afd9aa9ceda01614b9307eab444`,
`1780912f67a6ac39e029e6de62b7f5c2cfb72848f5de8a8a557335147b0f79fb`,
and `5df5301227b4f5316a2567e84396ec627f969a257dd6bf468427941bb7f87d81`.
Moving the camera changed the contribution hash and affected-pixel count, an
80x48 follow-up frame proved lease release and target replacement, and real
post-submission device-loss/timeout seams proved conservative cleanup. These
measurements close the first view-dependent same-device HDR-composite slice;
they do not close reflection/shadow quality, material, timing, GPU-capture,
fallback-soak, presentation, Vulkan KHR, or DXR gates.

The directional PSSM development smoke also passed natively on Apple Metal.
Toggling only the contained occluder's cast flag changed and darkened 244
receiver pixels in the HDR pair and the same 244 receiver pixels in the SDR
pair, with zero changes outside the reviewed receiver mask or inside the
visible occluder interior. Four shadow frames created and destroyed four
programmatic shadow nodes and four non-receiver datablock clones; default and
explicitly disabled SDR bytes were identical. This is local development
evidence, not checked-in golden pixels or proof of Linux/Windows runtime parity.

The opt-in N4 smoke then passed on the physical Apple M5. The exact 96x64
receiver produced 5,712 visible pixels, 432 pixels blocked by the distinct
occluder, and zero primary misses. Every visibility texel was canonical R16;
every visible pixel preserved the raster RGBA16 bytes; every blocked pixel
zeroed RGB while retaining alpha; and every pixel carried the expected R32
primary/secondary lineage. The backend used two exact Ogre geometry leases,
two BLAS, two TLAS instances with disjoint receiver/occluder masks, the exact
Ogre color-image lease, and one command buffer on Ogre's Metal queue. Allocation
failures are checked before any acceleration-structure object enters a Metal
descriptor collection, so memory pressure follows the pre-submission cleanup
path instead of raising an Objective-C exception. A consecutive identical
scene produced byte-identical raster, visibility, lineage, and hybrid outputs;
moving only the occluder preserved the raster while changing all shadow-derived
outputs. Releasing the frame permitted an 80x48 target replacement, and fresh
post-submission device-loss and timeout cases both revoked leases, tore down the
backend, and latched the frontend fail-closed. These measurements close the
first native full-view hard-directional-shadow slice on Metal only. They do not
close soft shadows, local lights, materials beyond the retained raster color,
long-running soak, CityWorld content quality, performance, full Vulkan/Ogre
interop, or full DXR/Ogre interop.

## Next gates

The checked-in optional CI matrix runs the exact probe on macOS arm64 Metal,
Windows x64 Direct3D 11, and Linux x86_64 software Vulkan/null-window. It keeps
the three jobs independent, runs N1 plus its directional PSSM proof, N2, N3,
and N4 before the legacy probes,
reruns the complete native test set, and revalidates the exact reports and
images selected for upload. Explicit always-running artifact gates require the
baseline set plus internally consistent N2/N3 attestations and independently
verified N4 pass-or-skip evidence, so a
partial result cannot be published as a complete checkpoint. Only the local macOS
result is proven at this checkpoint; the Windows and Linux jobs must still
execute successfully before their gates can close. The renderer remains
non-shipping until these later checkpoints pass:

1. build/run this exact probe on Windows x64/D3D11 and Linux x86_64/Vulkan;
2. reproduce the completed macOS native-window Compositor2 + HLMS PBS frame on
   Windows and Linux, including shutdown and fallback tests;
3. reproduce RT4/V1 texture, normal-map, retirement, directional-light,
   directional-PSSM, HDR/SDR, and artifact gates on native Windows and Linux,
   then add a
   semantically correct occlusion path, the full renderer-neutral light
   inventory, UI ordering, and presentation;
4. add depth, motion, and stable object-ID outputs only with their own tests;
5. extend the proven M5 hard-shadow path to soft/area-light shadows,
   reflections, materials, calibrated lighting, temporal stability, GPU
   capture, image and performance gates, then reproduce equivalent explicit
   interop on Windows/DXR and Linux/Vulkan KHR;
   and
6. keep Ogre-Next as the preferred public-launcher default while retaining the
   fail-closed OGRE 14 fallback until image, performance, content, and fallback
   acceptance gates pass.
