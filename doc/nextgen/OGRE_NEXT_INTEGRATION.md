# OGRE-Next isolated integration checkpoint

Status: **opt-in dependency probe and first renderer-neutral N1 frontend; no shipping renderer switch**

This checkpoint compiles three standalone executables against an exact
OGRE-Next `v3-0` revision while leaving every default RoR and OGRE 14 build
unchanged. The capability executable proves that the reviewed platform
renderer registers with OGRE core, HLMS PBS links and selects the expected
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

The original capability and frame probes do not consume a RoR scene. The N1
executable does consume the renderer-neutral RoR scene and asset contracts,
but never links into the OGRE 1.14 executable or touches simulation/solver
state. None of the three executables evaluates or claims native ray tracing,
and none is a shipping presentation-window or visual-quality claim.

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
- RapidJSON `v1.1.0`, required by OGRE core even when optional tools and scene
  components are disabled, with its source archive's
  `MIT AND BSD-3-Clause AND JSON` expression, the active reviewed header
  subset's `MIT` expression, and the complete upstream notice hash;
- the one small reviewed upstream CMake adaptation and its SHA-256; and
- ABI-relevant choices: C++17, static linking, allocator/threading/string
  layout, precision, `IdString` width, node inheritance, and SIMD family.

The lock, license, ABI, platform, and FetchContent policy lives in the shared
standalone CMake module
[`PinnedOgreNext.cmake`](../../tools/ogre_next_probe/cmake/PinnedOgreNext.cmake).
The entry project and N1 target include that one policy rather than copying its
pin block. Both the shared module and the N1 CMake guard reject an existing
`OgreMain` target, preventing OGRE 1.14 and Ogre-Next from entering one binary.

FetchContent uses URL hashes and a build-local `_deps` population area. A local
archive can be supplied, but it is hashed before CMake sees it. Direct
`FETCHCONTENT_SOURCE_DIR_*` overrides are rejected because they bypass archive
verification. The wrapper and standalone CMake project both reject reuse of a
configured build directory, because FetchContent does not re-hash an already
extracted source tree. `--clean-build-dir` recovers only a directory carrying
the exact probe ownership sentinel. No OGRE-Next source archive is stored in
this repository.

The adaptation fixes only two non-Xcode macOS assumptions in the pinned
upstream CMake: SDK path resolution and Xcode-only framework staging tokens in
Ninja files. It is applied from a hash-locked patch before configuration.

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
surface or shipping game window.

## N1 frontend contract

N1 admits exactly one headless/offscreen color view and one synchronous frame
in flight. Its supported slice is intentionally small:

| Area | N1 behavior |
| --- | --- |
| Geometry | Immutable static triangle lists with authored positions and normals, imported as Ogre v2 mesh/VAO allocations |
| Materials | Texture-free opaque metallic-roughness; IOR fixed to 1.5; `PbsBrdf::Default` height-correlated GGX; live datablock getters verify base color, metalness, roughness, and emissive after mapping |
| Lighting | Constant ambient/environment radiance only |
| Output | One UI-free `RGBA16_FLOAT` HDR or `RGBA8_SRGB` CPU readback |
| Camera | Current rigid view and canonical portable `[0,1]` projection; N1 explicitly converts depth to Ogre `[-1,1]` before the active RenderSystem performs one API-native conversion |
| Lifecycle | Transactional catalog replacement, RAII rollback for newly allocated native assets, teardown failure propagation/fault latch, bounded 64-frame completion history, and latest-snapshot-only identity replay |

N1 fails closed for textures/samplers, richer vertex streams, deformable or
dynamic meshes, particles, every analytic light, shadows, depth/motion/object
ID attachments, exposure/jitter, multiple views, presentation, native interop,
and ray tracing. Analytic lights remain rejected because the portable units and
Ogre power/attenuation path are not yet calibrated; N1 does not substitute an
approximate local-light curve.

Before a device exists, the capability query reports a conservative 2048
texture extent without treating it as an initialization ceiling. Initialization
creates only a 64x64 hidden/null bootstrap, reads
`RenderSystemCapabilities::getMaximumResolution2D()`, validates the requested
offscreen extent against that exact limit, and then publishes the device value.
This allows real 4K/5K-capable devices without overclaiming an uninitialized
device.

The CMake routing selects Metal on macOS arm64, Direct3D 11 on Windows x64,
and Vulkan null-window on Linux x86_64. The same strict-warning N1 sources are
compiled in each matrix job, but only the macOS Metal runtime has been proven
locally at this checkpoint. This is not evidence for Linux presentation or
Windows DXR.

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
  --rapidjson-archive /path/to/rapidjson-pinned.tar.gz
```

The build directory must be fresh. Pass `--clean-build-dir` only to recover a
previous directory created by this probe; the wrapper refuses to clean a path
without its ownership sentinel or any path overlapping the source checkout.

The generated files are:

- `ogre-next-build-contract.json`: configured provenance, platform, compiler,
  ABI, and component policy; and
- `ror-ogre-next-probe-report.json`: runtime registration/linkage evidence;
- `ror-ogre-next-frame-probe-report.json`: native-window, HLMS PBS,
  Compositor2, and GPU-readback claims; and
- `ror-ogre-next-frame-probe.ppm`: the UI-free RGB8 pixels independently
  checked against the frame report;
- `ror-ogre-next-frontend-n1-report.json`: N1 asset, material, HDR/SDR,
  identity, and recovery evidence; and
- `ror-ogre-next-frontend-n1.ppm`: the exact N1 sRGB CPU readback independently
  hashed by the wrapper.

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
`55526239ca9bd6a5`. The executable then accepted replay of only the latest
immutable snapshot, rejected an older snapshot, shut down, reinitialized,
resynchronized, rendered again, and shut down cleanly. Its report also records
that live HLMS getters matched the reviewed metallic workflow and
height-correlated GGX mapping. These values are local macOS evidence, not
cross-platform golden pixels.

## Next gates

The checked-in optional CI matrix runs the exact probe on macOS arm64 Metal,
Windows x64 Direct3D 11, and Linux x86_64 software Vulkan/null-window. It keeps
the three jobs independent, reruns the native lifecycle tests into separate
artifacts, and revalidates the exact reports and PPM selected for upload. The
always-running upload step preserves whichever diagnostic artifacts exist; an
early build or frame failure can leave artifacts absent, which intentionally
fails the upload contract. Only the local macOS result is proven at this
checkpoint; the Windows and Linux jobs must still execute successfully before
their gates can close. The renderer remains non-shipping until these later
checkpoints pass:

1. build/run this exact probe on Windows x64/D3D11 and Linux x86_64/Vulkan;
2. reproduce the completed macOS native-window Compositor2 + HLMS PBS frame on
   Windows and Linux, including shutdown and fallback tests;
3. expand the renderer-neutral adapter beyond N1 with calibrated lighting,
   textures, richer geometry streams, UI ordering, and presentation;
4. add depth, motion, and stable object-ID outputs only with their own tests;
5. prove same-device native RT scene interop separately; and
6. keep OGRE 14 as the default until image, performance, content, and fallback
   acceptance gates pass.
