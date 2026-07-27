# Rigs of Rods Next-Generation Roadmap

This roadmap turns the existing node/beam simulator into a measured, testable
next-generation Rigs of Rods. Success means preserving compatible content while
improving numerical safety, contact repeatability, material behaviour, rendering,
platform support, and authoring quality.

It does not claim parity or superiority over another simulator. Such a claim
would require comparable public scenarios, inputs, hardware, and measurements.

## Audited baseline

- Physics uses a fixed `0.0005 s` (2 kHz) Euler-style step. Existing beam,
  plasticity, contact, tyre, and drivetrain behaviour must be recorded before it
  is changed.
- Inter-actor beam and contact tasks can concurrently mutate shared simulation
  state. Contact traversal includes unordered data, turbulence uses shared
  pseudo-random state, and collision code contains shared camera state. A fixed
  time step alone therefore does not make a run deterministic.
- Plastic deformation currently changes beam rest length using heuristic
  spring-plus-damping stress. It does not model accumulated plastic strain,
  hardening, fracture energy, or a calibrated damage law.
- Visual flex deformation and normal generation run on the CPU, then stream
  complete dynamic vertex buffers to the GPU. Simulation nodes remain the
  authoritative geometry.
- The renderer is pinned to OGRE `1.11.6.1`. The generated configuration enables
  legacy OpenGL on non-Windows systems and D3D9 on Windows; GL3Plus and D3D11
  are disabled. Several managed materials depend on Cg-era shader profiles.
- Three-cascade PSSM shadows, terrain normal/specular/height inputs, dynamic
  cubemaps, Caelum/SkyX, Hydrax, vegetation, particles, and reflection/refraction
  water already exist. There is no general HDR, PBR, FXAA, bloom, SSAO, or TAA
  pipeline. The `gfx_enable_rtshaders` CVar has no active integration.
- Apple-specific source branches and Conan platform detection exist, but there
  is no macOS CI or supported package. Non-Windows CMake and runtime handling
  still assume X11, `librt`, `.so` plugins, a Linux launcher, and
  `/proc/self/exe`; there is no complete `.dylib`, rpath, or `.app` bundle path.
  The present tree is therefore not a verified macOS build.
- A historical OGRE 14 migration branch exists, but it is roughly 740 commits
  behind the audited `master`, and its Conan/platform assumptions remain biased
  toward Linux and Windows. Treat it as research, not as a merge base.
- CI and publishing currently target the `master` branch. Renaming it to `main`
  is a separate repository operation, not an engine change.

## Validation content

The initialized `content` submodule is pinned to
`34fefdd126784bf87b068fc283f812525d159dd7`. Its repository carries GPLv3 at
the repository level. Keep this exact revision in automated comparisons.

| Scene | Assets | Required coverage |
| --- | --- | --- |
| Asphalt baseline | `simple2_a.terrn2` + `b6b0UID-semi.truck` | Terrain normal/specular response, tyre contact, cab flex, emissive lights, FXAA/PBR screenshots |
| Deformation baseline | `simple2.terrn2` + `95bbUID-agoral.truck` | Dense node/beam deformation, self-contact, CPU/GPU flex comparison, crash replay |
| Water baseline | `simple2_w.terrn2` + DAF semi | Reflection/refraction and Hydrax/compositor ordering |
| Articulation baseline | DAF semi + `b6b0UID-semi.trailer` and `b6b0UID-semiflat.trailer` | Inter-actor beams, contact ordering, hooks, and deterministic multi-body replay |

The Agora L fixture has 151 nodes, 675 beams, and 222 cab triangles. It is useful
for correctness but too small for a GPU throughput gate; the benchmark must also
instantiate repeated vehicles or a generated high-vertex fixture.

## Measurement contract

No milestone below is complete until its test is automated.

1. Record the executable revision, content revision, compiler, build flags,
   renderer, worker count, resolution, and reference hardware with each result.
2. Drive scenarios from timestamped input recordings. Never use wall-clock input
   or an unseeded random source in a deterministic test.
3. At each physics step, hash ordered node position/velocity, beam rest
   length/stress/state, actor state, and the ordered contact set. Bitwise hashes
   are required across repeated runs of the same binary and CPU architecture.
   Cross-compiler and cross-platform comparisons use separately declared numeric
   tolerances and invariant checks.
4. Store baseline frame captures, frame-time distributions, physics-step time,
   contact counts, energy, momentum, and state hashes as CI artifacts.
5. Keep every new solver and renderer path behind a CVar until its fallback and
   compatibility gates pass.

The config-spawn path now honors both `cli_preset_veh_enter` and
`diag_preset_veh_enter`, so the documented `-enter` command can be used by the
deterministic scene-capture harness.

## P0 — Numerical safety and a physics test seam

Extract small dependency-free kernels before changing the full actor solver.
The first kernel is axial beam damping shared by intra-actor beams, inter-actor
beams, and paired scripted half-beams. Degenerate beam lengths must not reach
inverse-square-root normalization. Damping must be capped by

`d_effective <= 1 / ((inverse_mass_1 + inverse_mass_2) * dt)`

so one step can stop relative axial motion but cannot reverse it and inject
kinetic energy. A representative pair of 50 kg nodes with `d = 12000 Ns/m` is
below its `50000 Ns/m` cap and must remain unchanged; lighter custom nodes may
be limited by design.

Gate P0:

- Exact legacy force below the cap.
- Zero, tiny, NaN, and infinite lengths or inputs produce finite, bounded output.
- Unequal-mass, one-fixed-end, and both-fixed-end cases are covered.
- In 20,000 fixed-seed randomized cases, damping opposes relative velocity,
  never reverses it in one step, and axial kinetic energy after the step is no
  greater than before within `1e-5 * (1 + energy_before)`.
- The dependency-free C++11 test passes in Debug, Release, and Release with the
  game's fast-math flags under GCC, MSVC, and AppleClang. The standalone
  three-platform workflow explicitly enables that release floating-point mode.
- A 120,000-step (60 simulated seconds) starter-content soak reports no NaN,
  infinity, or invalid beam length.

## P1 — Calibrated soft-body materials

Add a versioned, opt-in material model rather than retuning global constants.
Use an elastic predictor and return-mapped plastic correction with explicit
yield stress, hardening, accumulated plastic strain, damage, and fracture
energy. Preserve the legacy model for old content until conversion is explicit.

Gate P1:

- Analytical single-beam tension/compression fixtures match elastic slope within
  1% and the configured yield point within 2%.
- A fixed cyclic load matches the approved hysteresis energy and residual strain
  reference within 5%; damage is monotonic and fracture dissipates, rather than
  creates, energy.
- Results at `0.25`, `0.5`, and `1.0 ms` steps differ by no more than 2% in
  peak force and permanent strain for the calibration fixtures.
- Internal beam forces conserve linear momentum with normalized residual
  `|sum(force)| / max(sum(|force_i|), epsilon) <= 1e-6`.
- A fixed-speed Agora impact has repeatable peak deceleration, absorbed energy,
  permanent deformation, and broken-beam count. The approved numbers become
  versioned regression data, not subjective tuning targets.

Tyre, suspension, and drivetrain refinements follow the same pattern: isolate a
model, cite its calibration data, version it, and pass force-slip, energy, and
step-sensitivity fixtures before changing defaults.

## D0 — Deterministic collision and replay

Make contact discovery and force application independent of task completion
order. Use stable actor/node/triangle contact keys, sorted candidate lists,
per-task force/impulse buffers, and one ordered reduction. Replace shared random
state with explicitly seeded per-actor streams and remove mutable camera state
from collision calculations.

Gate D0:

- The broad phase returns the same ordered contact keys as a brute-force oracle
  for at least 10,000 fixed-seed randomized fixtures.
- Thirty runs of each validation scene produce identical per-step hashes with
  one worker, and the same hashes with eight workers, for the same binary and
  architecture.
- ThreadSanitizer reports no race in a 10-minute multi-actor collision soak.
- Every internal contact pair has normalized linear impulse residual at or below
  `1e-6`; angular-momentum and energy deltas are recorded with scenario-specific
  limits.
- Loading, replaying, pausing, and resuming an input recording does not change
  its final state hash.

Cross-platform bitwise floating-point identity is not required initially.
Cross-platform replay must nevertheless remain within declared position,
velocity, energy, and momentum tolerances and report the first divergent step.

## R0 — OGRE 14 and native macOS foundation

First pin an exact OGRE 14 patch release in a migration branch and record an API
compatibility inventory. Do not combine the dependency jump with new lighting.
Bring up a modern renderer, remove Cg requirements, and preserve a temporary
legacy fallback until scene parity is measured.

Choose the macOS renderer through a capability spike; do not assume a production
Metal path without proving the selected OGRE 14 configuration. GL3Plus may be a
temporary bring-up path but is not a long-term macOS renderer commitment.

Gate R0:

- Clean, pinned dependency builds pass on Windows x64, Linux x64, and native
  macOS arm64 CI. The supported macOS deployment target is recorded in CMake.
- The full game configures and compiles on Linux and Windows before migration
  parity is assessed; the macOS arm64 build has no X11, `librt`,
  `/proc/self/exe`, Linux-launcher, or `.so` dependency.
- A signed macOS `.app` launches outside the build tree, finds all `.dylib`
  plugins and resources through bundle-relative paths, accepts
  keyboard/controller input, produces audio, writes config to the expected user
  directory, and contains no build-machine paths in `otool -L` output.
- `simple2_a`, `simple2`, and `simple2_w` load for ten minutes without renderer
  exceptions, validation-layer errors, or unbounded resource growth.
- PSSM, terrain layers, dynamic cubemaps, water, sky, vegetation, particles,
  ImGui/MyGUI, mirrors, screenshots, and resource hot-load are either equivalent
  to the OGRE 1.11 baseline or have an explicit tracked replacement.
- On the declared Windows and Linux reference machines, CPU and GPU frame-time
  medians regress by no more than 10% before any new visual feature is enabled.

## V0/V1 — Post-processing and PBR

V0 establishes a deterministic LDR post-processing seam: scene capture,
half-resolution bloom, a restrained color/exposure curve, and FXAA. It must be
the last scene compositor while leaving UI at native resolution. Hydrax
underwater and Caelum precipitation passes must remain correctly ordered.

Gate V0:

- Disabling post-processing is pixel-identical to the pre-feature baseline.
- Shader/compositor initialization is clean on every supported renderer.
- FXAA reduces approved high-contrast edge error without blurring UI text.
- Bloom does not clip emissive detail or introduce halos in the DAF scene.
- High quality adds at most 10% GPU frame time at 1920×1080 on the declared
  reference GPU; physics-step time is unchanged.

V1 follows R0 and introduces a linear HDR pipeline and a documented PBR material
schema: albedo, normal, packed occlusion/roughness/metalness, emissive, alpha
mode, and legacy fallback. Generate or import tangents rather than pretending
legacy cab meshes already contain them. Convert one DAF material and one terrain
layer before bulk conversion.

Gate V1:

- Automated BRDF fixtures match the selected reference implementation within 1%
  for approved dielectric, metal, roughness, and grazing-angle samples.
- Texture color-space, exposure, tone mapping, and reflection-probe rules are
  explicit and tested; no asset relies on renderer-default gamma behaviour.
- Approved screenshots for all validation scenes pass perceptual regression
  thresholds, while numeric HDR buffers remain finite.
- Transparent cab materials, emissive lights, shadows, water, weather, mirrors,
  and UI have dedicated regression captures.
- The PBR DAF and terrain slice meets a recorded render budget on each reference
  platform before additional assets are converted.

## G0 — GPU visual flex deformation

Physics remains CPU-authoritative. Upload the interpolated node snapshot and
immutable vertex-to-node mapping, then calculate render positions, normals, and
eventually tangents on the GPU. Retain the CPU path as a correctness oracle and
fallback. This work improves presentation throughput; it does not make the
soft-body solver itself more physical.

Gate G0:

- Across 1,000 captured frames, GPU output differs from the CPU oracle by no
  more than `0.5 mm` in vertex position and `1 degree` in normal direction.
- The render frame performs no GPU readback and no full deformed-vertex upload;
  only node snapshots and changed metadata cross the bus.
- A generated 250,000-visible-vertex fixture costs at most `1.5 ms` of GPU time
  and `0.5 ms` of render-thread CPU time on the declared reference machine, and
  reduces CPU deformation time by at least 2× versus the existing path.
- Agora crash silhouettes and material seams show no one-frame lag, cracks, or
  tangent discontinuities beyond approved image thresholds.
- The CPU fallback passes the same scene and image tests on unsupported
  hardware.

## A0 — Content and licensing

Treat content provenance as a build input. Every redistributable asset needs a
machine-readable record containing source URL or repository, pinned revision or
checksum, author, SPDX license identifier, modification status, and editable
source location.

The six optional 2022.12 content packs total about 3.07 GB, and their hosting
repository did not declare a license during this audit. They may be user-fetched
validation inputs, but must not be bundled, remastered, or redistributed until
each included asset has verified permission.

Gate A0:

- A content-audit CI job accounts for 100% of files in a distributable package
  and fails on missing provenance, license, or checksum.
- Derivative assets retain their editable sources and attribution; generated
  runtime textures/meshes identify the source and tool version.
- Community packs remain separate downloads unless they independently pass the
  same audit.
- The first vertical slice remasters only the pinned DAF semi and
  `simple2_a`: PBR source textures, authored LODs, collision proxy, thumbnail,
  metadata, and performance captures are required.
- The remastered slice loads with zero missing-resource or material errors and
  stays inside its recorded texture-memory, triangle, draw-call, and frame-time
  budgets.

## Integration order and release gate

1. Land the measurement contract and P0 numerical tests.
2. Make D0 deterministic before parallelizing or replacing more solver work.
3. Develop P1 and R0 in parallel, but do not merge new defaults without their
   independent gates.
4. Land V0 as the render-regression seam, then complete R0 before V1.
5. Land G0 only after the modern renderer exposes a stable cross-platform GPU
   data path.
6. Ship the A0 DAF/asphalt vertical slice before expanding the asset library.

A next-generation preview is ready only when the pinned four-scene suite passes
on Windows, Linux, and macOS; one-worker and eight-worker physics hashes match;
sanitizers are clean; post-FX/PBR/GPU-flex budgets pass; legacy content still
loads through fallbacks; and every shipped asset passes the content audit.
