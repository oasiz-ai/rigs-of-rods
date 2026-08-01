# OGRE-Next isolated integration checkpoint

Status: **opt-in dependency and capability probe; no shipping renderer switch**

This checkpoint compiles a standalone executable against an exact OGRE-Next
`v3-0` revision. It leaves every default RoR and OGRE 14 build unchanged. The
probe proves that the reviewed platform renderer can register with OGRE core,
that HLMS PBS links and selects the expected shader family, and that
Compositor2 is present in core with initialization correctly deferred until a
real render window exists.

It does not create a RoR scene or render window, does not share RoR resources,
and does not evaluate or claim native ray tracing.

## Reproducible dependency contract

[`ogre-next.lock.json`](../../tools/ogre_next_probe/ogre-next.lock.json) pins:

- official `OGRECave/ogre-next` branch `v3-0` commit
  `37149a802de747f6806996fa3067b0748ecc1084` and its archive SHA-256;
- the upstream MIT `COPYING` file and its SHA-256;
- RapidJSON `v1.1.0`, required by OGRE core even when optional tools and scene
  components are disabled, with its source archive's
  `MIT AND BSD-3-Clause AND JSON` expression, the active reviewed header
  subset's `MIT` expression, and the complete upstream notice hash;
- the one small reviewed upstream CMake adaptation and its SHA-256; and
- ABI-relevant choices: C++17, static linking, allocator/threading/string
  layout, precision, `IdString` width, node inheritance, and SIMD family.

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
renderer. The Linux policy compiles the null-window backend; upstream renderer
registration initializes a Vulkan instance and enumerates physical devices,
but this probe does not create a presentation surface, logical rendering
device, compositor workspace, or frame. A later window/presentation checkpoint
must prove the shipping surface.

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
- `ror-ogre-next-probe-report.json`: runtime registration/linkage evidence.

`--validate-contract-only` checks pins, patch hashes, and the current platform
policy without accessing the network or compiling.

## Completed local checkpoint

On 2026-07-31 the Release probe configured and built natively with AppleClang
21 for macOS arm64 and the macOS 11.0 deployment floor. The executable found
the Apple M5 through OGRE-Next Metal, registered exactly one reviewed renderer,
reported three Metal configuration options, linked HLMS PBS with
`Hlms/Pbs/Metal`, and observed Compositor2's pre-window deferred state. The
report retained `native_ray_tracing: not_evaluated`. The checked-in
[machine-readable evidence record](evidence/OGRE_NEXT_METAL_PROBE_M5_2026-07-31.json)
captures the source, dependency, build-contract, executable, and runtime-report
hashes plus the exact host and toolchain. The build contract and runtime report
are retained beside that record. The locally built executable is not committed;
its basename, hash, and explicit non-retention state are recorded without an
ephemeral absolute path.

## Next gates

This checkpoint is ready to become an optional CI matrix. The renderer remains
non-shipping until these later checkpoints pass:

1. build/run this exact probe on Windows x64/D3D11 and Linux x86_64/Vulkan;
2. create a native window and execute a minimal Compositor2 + HLMS PBS frame on
   all three platforms, including shutdown and fallback tests;
3. consume renderer-neutral RoR scene snapshots without touching solver state;
4. prove material/mesh conversion, HDR output, UI ordering, and lifecycle;
5. prove same-device native RT scene interop separately; and
6. keep OGRE 14 as the default until image, performance, content, and fallback
   acceptance gates pass.
