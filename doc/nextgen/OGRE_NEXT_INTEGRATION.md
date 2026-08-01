# OGRE-Next isolated integration checkpoint

Status: **opt-in N1 raster frontend plus an Apple Metal N2 geometry/RT capability probe; no shipping renderer switch**

This checkpoint compiles four standalone executables against an exact
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

The fourth executable is an explicit macOS-only N2 acceptance slice. It asks
that same frontend to raster a full deformed `SceneSnapshot` revision, borrows
the exact live Ogre Metal device and command queue, exports the actual pooled
v2 `MTLBuffer` position and index slices selected by the raster `Item`, and
builds BLAS/TLAS directly from those slices. One Metal ray query must hit the
exported triangle at one metre and survive an independently validated eight-byte
GPU probe readback. It creates no second device or queue and produces no
ray-traced image.

The original capability and frame probes do not consume a RoR scene. The N1
and N2 executables consume the renderer-neutral RoR scene and asset contracts,
but never link into the OGRE 1.14 executable or touch simulation/solver state.
Only the N2 executable evaluates native ray tracing. Its result is deliberately
limited to API, hardware, one-ray dispatch/readback, exact geometry interop,
and lifecycle acceptance; none of the four executables is a shipping
presentation-window or visual-quality claim.

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

The generated build contract and N1 runtime report also identify the canonical
RoR repository, exact Git commit/ref, and a sorted path/size/SHA-256 manifest of
the renderer sources and probe implementation used by the build. The wrapper
independently regenerates that manifest from the checkout, so locally modified
relevant source cannot masquerade as the named commit.

FetchContent uses URL hashes and a build-local `_deps` population area. A local
archive can be supplied, but it is hashed before CMake sees it. Direct
`FETCHCONTENT_SOURCE_DIR_*` overrides are rejected because they bypass archive
verification. A normal invocation requires a fresh build directory. The sole
reuse path is `--reuse-build-dir` with an explicitly selected `n1` or `legacy`
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
  licenses/LicenseRef-Heitz-LTC-Paper-Notice.txt
  # Linux additionally carries:
  licenses/Apache-2.0.txt
  licenses/glslang-LICENSE.txt
  licenses/SPIRV-Tools-LICENSE.txt
  licenses/SPIRV-Headers-LICENSE.txt
  provenance/ogre-next-linux-shader-toolchain.lock.json
  provenance/ogre-next-linux-static-closure.json
```

Every staged notice is copied from its hash-validated source and byte-compared
before the package stamp is written. The Linux source lock and built-archive
manifest are likewise byte-compared into `provenance/`; incomplete or changed
licensing/provenance data prevents package completion.

The staged executable is run with the resolved absolute form of
`share/rigsofrods/ogre-next/Samples/Media` from outside its `bin` directory.
Application packaging must resolve that same relative resource path using the
platform bundle/install locator and pass it through `OgreNextN1Configuration`;
relative, missing, or incomplete roots fail before native initialization.
The build wrapper also hashes all four staged license files against the
checked-in RoR license and the locked Ogre-Next, RapidJSON, and shader-notice
provenance, then independently compares the staged HLMS manifest with the
pinned source tree before accepting the package. A native negative test
corrupts one staged shader and requires an integrity-specific initialization
failure.

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
  by the wrapper and retained with the report.

The baseline GitHub `macos-15` runner currently identifies an M1/family-7 GPU,
so it compiles all N2 code and records an explicit capability skip (CTest exit
77) instead of claiming a family-9 runtime pass. A genuine M3-or-newer runner
must produce the probe artifact and pass the full validator.

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

## Next gates

The checked-in optional CI matrix runs the exact probe on macOS arm64 Metal,
Windows x64 Direct3D 11, and Linux x86_64 software Vulkan/null-window. It keeps
the three jobs independent, runs N1 plus its lifecycle/media-tamper tests before
the legacy probes, reruns the complete native test set, and revalidates the
exact reports and PPM selected for upload. An explicit always-running artifact
gate requires all six regular, non-empty artifacts before upload, so a partial
result cannot be published as a complete checkpoint. Only the local macOS
result is proven at this checkpoint; the Windows and Linux jobs must still
execute successfully before their gates can close. The renderer remains
non-shipping until these later checkpoints pass:

1. build/run this exact probe on Windows x64/D3D11 and Linux x86_64/Vulkan;
2. reproduce the completed macOS native-window Compositor2 + HLMS PBS frame on
   Windows and Linux, including shutdown and fallback tests;
3. expand the renderer-neutral adapter beyond N1 with calibrated lighting,
   textures, richer geometry streams, UI ordering, and presentation;
4. add depth, motion, and stable object-ID outputs only with their own tests;
5. extend the proven M5 same-device geometry path to RT materials, lighting,
   frontend texture import/compositing, image and performance gates, then
   reproduce equivalent explicit interop on Windows/DXR and Linux/Vulkan KHR;
   and
6. keep OGRE 14 as the default until image, performance, content, and fallback
   acceptance gates pass.
