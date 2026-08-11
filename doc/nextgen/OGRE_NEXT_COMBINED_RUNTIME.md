# OgreNext combined runtime

Status: **implemented as an opt-in macOS arm64 combined product; the exact
one-process N1/Metal link and live CityWorld authenticated source-texture path
pass, while visible image/resize acceptance, performance, native mesh import,
cross-platform parity, and default-product cutover remain gated**

## Product objective

The shipping renderer is one RoR process with one visible OgreNext window.
Gameplay, simulation, audio, content loading, scene production, input, and
rendering share one lifecycle. The process bridge is compatibility scaffolding,
not the target architecture.

The first combined slice may retain Ogre 1.14 as a hidden, in-process content
and scene producer. It must not present frames. That transitional dependency is
removed incrementally after the combined executable works; it is not made into
a permanent public renderer API.

## Runtime boundary

`RendererFrontendDirectDispatcher` owns the renderer-neutral typed consumer
transaction:

1. apply and validate an exact asset delta in a private logical registry;
2. synchronize that delta with the initialized frontend;
3. validate an immutable scene and camera against that registry;
4. retire a stale-size scene without consuming a frontend frame identity;
5. submit rendered scenes with contiguous process-lifetime frame identities;
6. wait for completion and release every unique transferred resource once;
7. reset a map generation only after an authoritative empty scene and empty
   registry;
8. permanently fail closed after any validation, frontend, wait, output,
   release, allocation, or reset error.

This path accepts typed C++ objects. It contains no transport envelope, byte
codec, channel, acknowledgement, pipe, process, or child-runtime dependency.
The compatibility `RendererFrontendTransportDispatcher` decodes the existing
wire protocol and delegates the typed operation to this same consumer, so the
two-process regression path cannot acquire different render lifecycle rules.

`RendererInProcessSession` now owns that dispatcher for one freshly initialized
frontend lifetime. The game must obtain a one-shot pre-simulation event grant
before advancing physics, then publish the joined graphics capture. A second
event poll immediately before presentation catches a resize that arrived during
capture. Surface-drain timeout retains the exact immutable production; the next
pre-simulation pump retries it without recapturing or advancing simulation. The
session then synchronizes an optional asset delta before its scene, preserves
snapshot and frontend-frame IDs across map reset, and releases the frontend's
native-window borrow before shutting down the event owner.

Camera conventions and supported-light validation are injected through
`IRendererInProcessFramePolicy`, not embedded in the reusable lifecycle. The
first demo adapter reuses the existing drawable-pixel camera normalization and
one-shadow-sun admission. A standalone strict-warning test links the complete
session from an explicit source closure that rejects every file named
`*Transport*` or `*Bridge*`. `RoR-Combined` now wires that same session to the
production game loop and the namespaced N1 frontend; the standalone test
remains the smaller lifecycle and link-closure regression.

## Why the OgreNext fork is required

Stock Ogre 1.14 and stock OgreNext both export incompatible C++ entities in
`namespace Ogre`, along with process-global singletons, RTTI, plugin entry
points, and platform classes. `OGRE_USE_NEW_PROJECT_NAME` changes library names
only. Linking both stock implementations into one executable is unsupported.

The RoR OgreNext fork therefore needs an embedded namespace mode:

- every OgreNext C++ and Objective-C++ translation unit is force-included with
  a fork-owned exact token remap from `Ogre` to `RoROgreNext`;
- the remap never reaches a RoR/Ogre14 translation unit;
- plugin exports, Objective-C runtime class names, CMake targets, and remaining
  non-C++ globals receive explicit RoR prefixes;
- adapter public headers use Pimpl and renderer-neutral RoR types, so no source
  file includes both OGRE generations;
- a compile-database audit proves every fork object received the namespace
  header, and a symbol audit rejects any unprefixed OgreNext `Ogre::*`, plugin,
  or Objective-C symbol.

Link-order tricks, duplicate-symbol allowances, visibility alone, or a global
game-target preprocessor define are not acceptable substitutes.

### Downstream compile contract

The fork remap is intentionally a private compile option, not an interface
property. Every downstream target with a C++ or Objective-C++ translation unit
that includes an OgreNext header must explicitly call:

```cmake
ror_ogre_next_enable_embedded_namespace(the_target)
```

Mixed-language targets may narrow the helper to the exact language that owns
the OgreNext header boundary, for example `LANGUAGES OBJCXX` for the Cocoa
Metal-view adapter. The compile-database audit must then prove both halves: all
OgreNext consumers receive the forced include, while renderer-neutral and
Ogre14 translation units do not.

That call belongs next to the target's OgreNext include and link declarations.
Targets which compile Ogre14 headers must never call it. A combined adapter
must keep the two header families in separate translation units and cross the
boundary through renderer-neutral RoR types or a narrow C/Pimpl seam. Merely
linking a namespaced static archive does not remap a consumer translation unit;
omitting this call is a build-contract error even when a particular source file
happens not to name `Ogre` directly.

The canonical lock records patch 0006 and the remap header as conditional fork
inputs. Build-contract schema 7 records whether the mode was enabled and
whether those inputs were applied. The standalone probe deliberately reports
`full_n1_link_evidence: not_evaluated`; its dual-Root smoke remains namespace
evidence. The root combined provider adds a stricter audit whose evidence scope
requires the dual Root link, renderer-neutral session, concrete presenter, and
full N1 runtime link. The final binary verifier consumes that report, its
compile database, the exact clean RoR commit, all contributing static archives,
and the complete OGRE 14 dylib closure.

## First product target

The opt-in target is `RoR-Combined` until native parity gates pass. Its intended
link and ownership graph is:

```text
RoR-Combined
  RoR game, simulation, content, audio, and scripts
  temporary hidden Ogre 1.14 producer (no frame presentation)
  renderer-neutral scene producer and direct dispatcher
  in-process OgreNext runtime adapter (Pimpl boundary)
  namespaced static RoR OgreNext fork
  one application-owned SDL runtime and one visible window
```

The executable must not link the launcher, supervisor, child main, bridge
channel, transport stream, or renderer transport codecs. The legacy
two-process package remains a separate regression build while the combined
target is opt-in; it is not an automatic fallback from a failed combined run.

## Current macOS arm64 evidence

On 2026-08-11, a fresh clean build from exact source commit
`cd693c2f404d29c087e8788030e3645ecfc4debf` produced the arm64
`RoR-Combined` binary with SHA-256
`fca0609a59a73c5b01faf7e0596bd88c85c49299c10479c319cba3976b8aac8f`.
Its final closure receipt,
`source/main/RoR-Combined.binary-closure.json`, has SHA-256
`30eafbd656f874b78afaf239424378295a885400dead501e88f2d60da1e892a7`
and proves all 376 fetched OgreNext compile entries used the strict
floating-point suffix, the complete namespace and selected-source audits
passed, all required OgreNext and OGRE 14 contributors were present, and no
bridge/transport object, second SDL implementation, root static libpng/libjpeg
member, external PNG/JPEG/stb symbol, or extra codec dylib outside the
authenticated OGRE 14 `Codec_FreeImage` closure entered the process.
It also binds the single private `stb_image` implementation and the exact
upstream header, lock, license, compile options, object, and live symbol set.
Separately, the staged-resource completion receipt,
`ror-ogre-next-combined-resources/.complete.json`, has SHA-256
`94f706dab4b4a3a389b48cf4c93654456f0804f27665067df9c9a1734b9006ef`
and proves all 270 renderer-resource files matched manifest SHA-256
`6be661fc989dc6f4d5c4c5b37a9127a7d8582d5ad372eebf7b487aa13ee8a535`.
The resource receipt is distinct from the final binary-closure receipt.

An isolated CityWorld/Alexis run then stayed active for more than 2 minutes 21
seconds as one process with zero child processes and zero pipe or FIFO
descriptors. It loaded the Metal/AGX runtime, and CoreGraphics reported one
onscreen window owned by that exact PID. The exact 158,845,395-byte CityWorld
archive was mounted as the sole authenticated `EmbeddedZip` location under
SHA-256
`ebeac2f0204f25ca1955f29ca1583b2afa4517a3a848feb1db203814acac2ef3`;
Alexis remained an ordinary ZIP and therefore retained the explicitly labelled
unauthenticated fallback.

The first joined material commit kept 32 opaque TUS0 projections and reported
29 authenticated source-byte decodes, zero authenticated GPU readbacks, and
three explicitly labelled unauthenticated-path GPU readbacks. Later lazy
observations raised the active projection count to 45 and the authenticated
source-decode total to 42 without changing the zero authenticated-readback
count. All 20 CityWorld material scripts completed without compiler errors; 23
reviewed aliases and 11 reviewed lit fallbacks resolved the remaining legacy
names, and CityWorld missing-material diagnostics remained at zero. The
supervised `SIGTERM` was
handled as an SDL quit, exited with status zero, reached
`[RoR|RendererCombined|Shutdown] status='closed', pending=0`, and produced no
crash report.

The logged lifetime `projections` counter is the cumulative sum of per-capture
reachable projections, not another active-catalog count; only the explicit
32-to-45 active count and 29-to-42 source-decode count are used above.

The raw build-tree window could not be addressed or captured by the available
desktop automation. The onscreen-window and Metal runtime observations are
therefore not a visual pixel-quality, steering/camera-input, or live-resize
result. No FPS was recorded. Those visible-image and performance gates remain
open, along with same-process map-reset/unmount acceptance.

## Acceptance gates

The first macOS gate requires all of the following:

- one PID and no renderer child process or renderer pipes;
- one visible SDL/Metal window and one SDL implementation in the link closure;
- both `Ogre::Root` and `RoROgreNext::Root` link without ambiguous symbols;
- zero unprefixed OgreNext `Ogre::*`, plugin, or Objective-C runtime exports;
- direct typed asset-before-scene ordering, stale-resize retirement, contiguous
  frame IDs, exact resource release, generation reset, and fail-stop teardown;
- positive OgreNext presented-frame evidence and zero Ogre14 presentations;
- CityWorld terrain, projected opaque materials, and Alexis deformation render
  through the same direct session;
- every frame-reachable authenticated projected texture is decoded from its
  retained source receipt, with zero authenticated GPU readback or fallback;
- resize, map reset, shutdown, and a sustained live run do not crash or leak.

Linux/Vulkan and Windows/D3D11 then require the same symbol, lifecycle, scene,
and image gates before `RoR-Combined` can replace the default executable.

## Removal sequence

After the combined runtime is proven, work proceeds in this order:

1. RoR-owned archive/VFS and material-script services;
2. direct OgreNext mesh, texture, and semantic material import with an offline
   v1-to-v2 cache;
3. direct static, dynamic, procedural, particle, and vehicle scene producers;
4. native RoR terrain state and rendering, eliminating composite readback;
5. OgreNext-native ImGui/HUD/menu/dashboard presentation;
6. one SDL input/controller/haptics owner and force-feedback path;
7. remove Ogre types from audio/script/resource helpers;
8. delete the Ogre14 renderer, extraction bridge, RTSS, legacy window, and
   bridge package, then rename the verified combined target to `RoR`.

Each removal is an internal product migration. Generalizing or publishing the
interfaces is lower priority than renderer correctness, image quality, and
performance.
