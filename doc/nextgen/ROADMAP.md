# Rigs of Rods Next-Generation Roadmap

This roadmap turns the existing node/beam simulator into a measured, testable
next-generation Rigs of Rods. Success means preserving compatible content while
improving numerical safety, contact repeatability, material behaviour, rendering,
platform support, and authoring quality.

It does not claim parity or superiority over another simulator. Such a claim
would require comparable public scenarios, inputs, hardware, and measurements.

BeamNG and BeamNG.drive are used below only to identify an external mod format.
The project keeps independent branding, uses no BeamNG logo, and describes the
importer as unofficial and unendorsed. It must not use `BeamNG 2` or another
BeamNG-derived product name without written permission, in accordance with
[BeamNG's trademark guidelines][beamng-trademark].

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
- The compatibility build remains pinned to OGRE `1.11.6.1`, while the opt-in
  native macOS path pins OGRE `14.5.2`, GL3Plus, and RTShaderSystem. The OGRE 14
  dependency and application bundle have no Cg package or runtime plugin;
  Windows/Linux migration and measured renderer parity remain open.
- Three-cascade PSSM shadows, terrain normal/specular/height inputs, dynamic
  cubemaps, Caelum/SkyX, Hydrax, vegetation, particles, and reflection/refraction
  water already exist. There is no general HDR, PBR, FXAA, bloom, SSAO, or TAA
  pipeline. The `gfx_enable_rtshaders` CVar has no active integration.
- The opt-in Apple Silicon build now removes X11, `librt`, `.so`, Linux
  launcher, and `/proc/self/exe` assumptions; stages `.dylib` plugins through
  bundle-relative rpaths; and produces a signed relocatable `.app`. Native
  macOS CI, a supported release package, controller/audio coverage, the
  validation-scene soak matrix, and Windows/Linux OGRE 14 parity remain open.
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
| BeamNG vehicle import (local opt-in) | User-supplied [GD808 FormulaCOUPE v0.9.7][formulacoupe], resource `M764KYBVX` | Safe package inventory, 39 configurations, deterministic `FC-A7-01` resolution, structural/drivable/visual tier reports, and explicit advanced-feature diagnostics |

The Agora L definition has 151 authored nodes, 675 beams, and 222 cab triangles.
Its six legacy wheels and two cinecams bring the spawned actor to 297 runtime
nodes. It is useful for correctness but too small for a GPU throughput gate; the
benchmark must also instantiate repeated vehicles or a generated high-vertex
fixture.

FormulaCOUPE is a manual interoperability fixture, not project content. The
v0.9.7 metadata above was audited on 2026-07-27. The importer never downloads
it, and its archive, extracted files, conversions, screenshots, and golden
assets remain outside the repository and distributable builds. An opt-in test
records the user-supplied ZIP's SHA-256, detected version, resource ID, importer
version, and selected configuration; it reports `SKIP` when the archive is
absent and version drift when those values change. Public CI uses small,
original clean-room JBeam fixtures with explicit licenses.

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

The first P1 kernel now implements a versioned, opt-in-ready uniaxial
elastoplastic damage law in SI units. It uses a closed-form backward-Euler
return map, isotropic hardening, accumulated plastic strain, monotonic damage,
and a finite post-onset damage-driver capacity. This local capacity is not
claimed to be total dissipated fracture energy or mesh-objective `G_f`. Under
the kernel's monotonic post-onset nominal stress/strain-area convention,
`C = 2 (G_f / l_char) / (1 + H/E)`; a total-dissipation convention includes
stored energy at damage onset and therefore maps differently. The future beam
adapter must declare its convention, perform characteristic-length calibration,
control localization, and pass monotonic plus cyclic mesh-refinement energy
gates; characteristic length alone is not proof of mesh objectivity.
Dependency-free
analytical, cyclic-regression,
exact energy-balance, reversal, tangent, fracture-event, subdivision, malformed
state, and fixed-seed property tests pass under strict C++11 and fast-math
sanitizers. It is not yet wired into `Actor`: cross-sectional-area/rest-length
adaptation, assembled momentum, authored material parsing, save/replay state,
starter-content calibration, and the Agora impact regression remain open before
P1 can change any runtime default.

## D0 — Deterministic collision and replay

Make contact discovery and force application independent of task completion
order. Use stable actor/node/triangle contact keys, sorted candidate lists,
per-task force/impulse buffers, and one ordered reduction. Replace shared random
state with explicitly seeded per-actor streams and remove mutable camera state
from collision calculations.

The first D0 slice replaces the racy turbulent-drag and engine anti-lag random
states with counter-based samples. Turbulence is keyed by persisted actor seed,
fixed physics step, node index, and XYZ lane. Anti-lag has a domain-separated
engine-update counter and turbo index so successive sleeping-engine updates do
not repeat a sample. Those sleeping updates are still outer-frame scheduled, so
equal-time replay across different render-frame groupings remains open. Full
actor resets restart the counters; version-3 savegames carry optional
seed/counter fields so existing version-3 saves remain loadable and resumed
saves keep their next samples. Golden vectors and dependency-free
one/two/eight-thread kernel tests lock the sampler's pure-function contract.
ActorManager/content worker-count runs, save/load continuation tests, the
runtime TSan soak, the production broad-phase oracle, and input-replay hashing
remain open D0 work, as does a scenario-level seed/stream-ID contract
independent of runtime actor-ID assignment.

The contact-order slice now updates inter-actor detectors in stable actor-ID
order, canonicalizes collision-partner and KD-hit lists, discovers narrow-phase
contacts into per-actor task buffers, and applies forces only after one stable
`(surface actor, surface contact, hit actor, hit node)` reduction. Worker tasks no
longer concurrently mutate another actor's node forces. The parallel fast path
has a hard global buffered-contact cap of 65,536, split into deterministic
per-actor quotas. If any actor exceeds its quota, the entire partial buffer set
is discarded and all scheduled contacts are re-discovered and applied serially
in the same key order;
contacts are never truncated, and adaptive collision-rate state advances only
once. Actors with no contact surface scheduled on a step allocate no task
buffer. Bounded buffers grow lazily, retain storage across 2 kHz substeps and
scheduled-actor-count fluctuations, and convert worker-side allocation failure
into the full serial fallback. Fallbacks are counted for the session and logged
with their reason on counts 1, 2, 4, 8, and subsequent powers of two, making
pathological content observable without per-step log spam. A
dependency-free contract compares
10,000 fixed-seed shuffled AABB candidate sets with a brute-force ordered
oracle, produces bit-identical
reductions from one, two, and eight task buffers, and locks quota, overflow, and
fallback-order behavior. A runtime oracle against `PointColDetector` itself and
the multi-actor TSan soak are still required to close the gate.

The first pending runtime micro-scenario uses `simple2.terrn2` and two airborne
`b6b0UID-semi.truck` actors with fixed poses, explicit stable IDs/seeds, and
exactly 1,000 physics steps. Each spawned DAF has 176 runtime nodes, so the pair
requests 1,056 turbulence samples per step. Thirty runs must match ordered
per-actor node-state hashes with one and eight workers before this slice counts
as runtime-validated.

The version-1 D0 state-digest kernel now defines the byte-level contract for
those comparisons. It streams, without retaining records, exact binary32 actor
origins, integer actor state, full-width deterministic seeds/noise counters,
ordered node positions/velocities, ordered beam rest length and stress, the
complete binary64 P1 material history, beam flags, and ordered inter-actor
contact keys into a domain-separated SHA-256 digest. Invalid section order or
counts, noncanonical within-section keys, invalid numeric ranges, non-finite
values, and immutable actor/node/beam/contact ceiling violations fail closed.
IEEE exponent bits are inspected through the object representation, so NaN and
infinity are rejected even with the game's fast-math mode; strict, fast-math,
and address/undefined-sanitizer fixtures lock golden digests and field
sensitivity. Cross-section actor/reference validation, accepted material
schemas/flag masks, the production `Actor` snapshot adapter, per-step artifact
stream, input recording, pause/resume, and one/eight-worker scene runs are still
open; the digest kernel alone is not runtime determinism evidence.

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

The first Darwin bootstrap records macOS 11.0 as the native Apple Silicon
deployment floor, keeps X11 and `librt` off the Apple link line, selects the
upstream OIS 1.5.1 recipe on macOS, scopes CMake 4's legacy-policy allowance to
the dependency-build subprocess, and copies/installs `.dylib` dependencies.

On 2026-07-27, the separate OGRE dependency/package slice passed its native
macOS proof. The exact Conan reference is
`ogre3d/14.5.2#68db16985fa623986379d2b9422d0dce:a7b76c6f340c40b0b8883ed9b40acfff5165c675#7bca6071546b0b39732f0b83fb5eb89f`.
It was built as arm64 Release C++17 with AppleClang `21.0.0.21000101`, a macOS
11.0 minimum deployment target, Metal and GL3Plus, and no Cg Toolkit dependency
or Cg runtime plugin. Its relocated test initialized Metal with the exact eight
configured plugins. A second native verifier pass checked 20 Mach-O files, 17
package-local symlinks, ten relative pkg-config files, isolated loading of every
plugin, and a relocated installed tool. Loader/install/plugin/pkg-config
metadata contains no lexical or resolved Conan cache prefix. A separate raw
string inventory records non-runtime Objective-C/Objective-C++ and statically
linked SDL source/assertion paths in three binaries; this package is therefore
not claimed to be fully path-reproducible.

Libraries and plugins use only `@loader_path`, `@loader_path/..`, and
`@loader_path/../lib`. The three tools installed under `bin/macosx` additionally
use `@loader_path/../../lib`. The verifier resolves every `LC_RPATH` relative to
its owning binary in the relocated tree and rejects paths outside the package.
The checked-in lock, full commands, plugin list, patch hash, and package audit
are recorded in the
[OGRE recipe proof](../../cmake/conan/recipes/ogre3d/README.md).

A clean full dependency build with a space in `CONAN_HOME` remains unsupported:
the pinned upstream `libjpeg/9e` Autotools recipe stops before OGRE with
`configure: error: unsafe srcdir value`. The OGRE recipe's own escaped
prefix-map argument was compiled independently with a spaced source path, but
that does not turn the blocked full graph into a pass.

The first full application slice now opts the root graph into the pinned OGRE
14 and MyGUI recipes, builds a native arm64 GL3Plus executable, creates its SDL
window at 1280x832 logical points for a 2560x1664 Retina backing store,
initializes RTShaderSystem without Cg, and stages and ad-hoc signs a relocatable
`.app`. A clean 444-step build passed, followed by all 16 native CTest targets
and all 62 tool tests. The bundle audit verified four OGRE plugins and nine
runtime libraries, and the pristine package contains no user configuration.

A relocated, clean-cache live run discovered the user-supplied
`Audimans_Testor.terrn2` and XBGT Falcon archives, loaded the terrain through
`===== TERRAIN LOADING DONE`, and rendered its textured terrain, sky,
structures, and character. The Falcon's unsupported legacy Cg NiceMetal
programs are rejected explicitly; OGRE RTShaderSystem generated GL3Plus vertex
and fragment programs for both fallback material passes without a
`no supportable Techniques` error. macOS keyboard input now uses SDL physical
key and UTF-8 events while retaining an unbuffered OIS compatibility facade.
The SDL Cocoa responder is restored after OIS initialization and each focus
gain, and live input was confirmed on the packaged build. Scene unload and
application shutdown complete through OGRE teardown without a new macOS crash
report. The local OGRE patch also closes a shadow-projector index underflow
found by an LLDB hardware watchpoint during that scene load.

This is meaningful R0 progress, not completion. The remaining gates include:

- Eliminate every GL validation diagnostic and prove PSSM with controlled
  occluder captures; then cover dynamic cubemaps, water, sky, vegetation,
  particles, UI, mirrors, screenshots, and hot-load against recorded baselines.
- Add native controller and audio checks, expected user-directory behavior, and
  ten-minute `simple2_a`, `simple2`, and `simple2_w` resource-growth soaks.
- Decide and prove the production Metal material path. RoR media still has no
  authored MSL pipeline; the current application path intentionally uses
  GL3Plus.
- Build and test the full OGRE 14 application on Windows and Linux, add the
  three-platform native CI matrix, and measure the declared CPU/GPU frame-time
  budgets.

Merely changing version strings is still an explicitly rejected milestone.

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

## J0–J5 — User-side BeamNG vehicle-mod interoperability

This track is clean-room, format-level interoperability for vehicle packages a
user has legally obtained. It is not binary compatibility with BeamNG.drive,
does not use BeamNG executable code or stock assets, and does not promise the
same result from two different physics engines. Initial scope is `vehicles/`
content. Level, UI, gameplay, and executable-plugin compatibility require
separate milestones; Lua found in a package is inventoried but never executed.
The versioned field, unit, behavior, fidelity, and competitive-hypothesis
contract lives in
[the BeamNG.drive compatibility specification](BEAMNG_COMPATIBILITY.md).

Every import advertises the highest tier reached by that exact package,
configuration, source hash, importer schema, and option set:

- **J0 Inspectable:** safely index the package and issue a complete dependency,
  provenance, and unsupported-content report.
- **J1 Resolved:** parse the JBeam dialect and deterministically resolve one
  complete part/configuration graph into a normalized, versioned vehicle IR.
- **J2 Structural:** lower supported nodes, beams, triangles, and references to a
  finite physics skeleton that can spawn and settle headlessly.
- **J3 Drivable-simple:** provide a deliberately bounded conventional
  drivetrain, steering, brakes, suspension, and approximate pressure-wheel
  adapter.
- **J4 Visual:** resolve DAE meshes, namespaced materials and textures,
  flexbodies, props, cameras, emissive state, and damage-material mappings.
- **J5 Advanced-native:** implement special beams and collision semantics,
  pressure tyres, modular ICE/EV/CVT powertrains, thermals, active aero,
  parachutes, JATO/thrusters, towing, and controllers as tested native systems.

No package is called simply "compatible." Its report uses `native`,
`approximated`, `preserved-but-disabled`, `unsupported`, or `rejected` for
every discovered feature and resource, with the reason and source location.

### J0 — Package identity and hostile-input boundary

BeamNG's [documented packed-mod layout][beamng-mod-packing] places roots such as
`vehicles/`, `art/`, `assets/`, and `lua/` directly at the ZIP root. J0 accepts
only an explicit local ZIP or unpacked directory and creates a deterministic,
sorted manifest. It rejects absolute and parent-relative paths, path
normalization collisions, case collisions, duplicate entries, symlinks,
encrypted entries, excessive nesting, entry counts, expanded bytes, and
compression ratios. Scanning, previewing, and importing perform no network
access or code execution.

Do not add `.jbeam` as another single-file vehicle type in `CacheSystem`.
A BeamNG vehicle is a package of parts and configurations. Index one package
and expose virtual entries for each resolvable main/configuration pair. Generated
data lives in an untracked cache keyed by source SHA-256, main part,
configuration/slot tree, variables, importer schema, and import options.

### J1 — Parser, part graph, and normalized import IR

[JBeam][beamng-jbeam-syntax] is a case-sensitive JSON-derived syntax with
comments, optional commas, table headers, inherited row defaults, variables,
and expressions. A purpose-built front end must preserve unknown keys/sections
and file, section, row, line, and column origins. Its expression evaluator is
deterministic, resource-bounded, and allowlisted; it cannot invoke Lua or host
functions.

`JBeamPackageIndex -> JBeamSyntaxParser -> JBeamResolver ->
JBeamSemanticValidator` produces a canonical resolved IR. Resolution includes
`main`/`slotType`, recursive `slots` and `slots2`, `.pc` selections, inherited
variables, components, namespace/prefix rules, node transforms, duplicate and
optional references, missing external parts, and cycle detection. Enumeration
order, archive order, path case, and worker count cannot change the canonical
serialization or hash.

`JBeamToRigDef` then lowers a fresh, named-node `RigDef::Document` into the
existing validator and `ActorSpawner`. It must not add JBeam conditionals to the
spawner or repurpose `RigDef::SequentialImporter`, whose role is repairing
legacy RoR node ordering. The first subset is one resolved main part with
refNodes, nodes, normal/support beams, triangles, and basic hydros/rails.
BeamNG's `+X left, +Y backward, +Z up` basis is converted by one named,
unit-tested transform; offsets, rotations, reference directions, triangle
winding, meshes, forces, and inertia all use that same transform.

The first J1 front-end slice now provides a bounded relaxed-JBeam lexer/parser
and duplicate-preserving, source-spanned AST. It accepts documented comments,
optional/trailing commas, strict finite scalars, UTF-8/Unicode escapes, and
normalizes table headers, inherited defaults, positional cells, trailing row
dictionaries, malformed rows, and effective last-write lookup without erasing
raw input. Variables and `$=` expressions remain inert.

The bounded resolver indexes parts independently of archive enumeration order,
selects the sole `main` root or requires an explicit root when a package exposes
several vehicles, handles legacy `slots`, allow/deny `slots2`, core and optional
defaults, explicit empty selections, `.pc` part selections, and typed scalar
variable inheritance. It rejects duplicate definitions, ambiguous parts,
cycles, invalid slot tables, and resource-limit overflow with canonical source
diagnostics. Canonical index and resolved-graph identities retain the full
duplicate assignment/body history and source spans. Per-`vehicles/<id>` part
namespaces, allowlisted expression evaluation, field-specific semantic
validation beyond the structural subset, coordinate lowering into `RigDef`, and
the complete vehicle IR remain open J1/J2 work.

The first J2 kernel locks the vehicle-basis conversion as the exact,
handedness-preserving permutation
`(x_ror, y_ror, z_ror) = (y_beamng, z_beamng, x_beamng)`, with the exact
inverse. Dependency-free tests prove basis/refnode landmarks, distance, dot and
cross products, triangle winding and normals, in-place round trips, and
fail-closed non-finite handling under fast-math. The structural IR and every
runtime node, force, rotation, inertia, mesh, prop, and camera adapter must use
this single boundary; no imported actor is enabled merely because the transform
kernel exists.

The structural semantic pass now deterministically lowers the resolved part
preorder into bounded SI nodes, enabled normal beams, disabled special/optional
beams, triangles, fixed-diagonal quads, and exactly one source-traceable refnode
frame. It enforces global node identity, positive finite mass, finite or exact
`"FLT_MAX"` beam limits, complete references, nondegenerate geometry, documented
`+Y` back / `+X` left / `+Z` up alignment, and part/row/topology/diagnostic
quotas. Unknown and unsupported data remains preserved in stable diagnostics,
and canonical serialization is invariant to package enumeration order.

The fresh `RigDef::Document` adapter now preserves authored point masses,
normal-beam spring/damping/deformation/strength and precompression, stable
triangle winding, and the six-node reference frame without passing through the
legacy sequential importer. A dependency-light preflight reproduces the
binary32 arithmetic that `ActorSpawner` consumes, rejects narrowing,
normalization, topology, accumulated-mass, accumulated-length, and extent
failures, and enforces immutable record/work/diagnostic bounds even for
hand-built IR. The emitted beam defaults explicitly bypass RoR's legacy creak
floor so a valid BeamNG deformation threshold below 100 kN is not silently
raised. Runtime import remains disabled until this document is admitted through
the existing validator/spawner and passes the 120,000-step finite
settle/impact, authored-mass, and topology gates.

### J2/J3 — Physics lowering and bounded driveability

Each source concept gets an explicit exact, approximate, or unsupported mapping:

- Preserve authored point masses rather than passing them through RoR's legacy
  dry-mass redistribution or minimum-mass rules.
- Map normal beam spring/damping/deform/break values only where semantics match.
  Bounded/anisotropic/L-beams, torsion, pressure, deform groups, and triangle
  breaking need native implementations or remain disabled.
- Flatten quads deterministically, but gate per-triangle collision, aero,
  pressure, breaking, ground-model, and self-collision behavior separately.
- Test basic hydros and rails/slidenodes against their source limits rather than
  assuming the similarly named RoR systems are equivalent.
- Treat RoR generated wheels as a J3 approximation only. J5 requires dynamic
  pressure-tyre topology, radial/sidewall behavior, load/slip friction,
  thermals, damage, and removal of fixed wheel-node limits.
- The locked public documentation profile does not publish a powertrain device
  schema or defaults. J3 therefore inventories and preserves source powertrain
  data but does not call an inferred ICE/clutch/gearbox graph BeamNG-compatible.
  A separately versioned RoR-native simple-drivetrain approximation may be
  calibrated later. Multi-motor, EV/hybrid, CVT, converter, rangebox, energy,
  thermal, per-wheel brake, electrics, and controller graphs remain disabled
  until native adapters pass their own gates.

The first J3 actuator kernel implements the documented hydro factor examples,
asymmetric length limits, input center/locks/scaling, and independently bounded
contraction, extension, and auto-center rates as a versioned positive
rest-length-ratio state. Factor mode has the documented precedence over
separate travel/input scaling. Invalid configuration, input, state, timestep,
or resolved rest length fails closed under strict and fast-math builds.
Golden examples and 50,000 fixed-seed property cases cover target progress,
rate bounds, and finite positive output. Structural `hydros` table lowering,
native input wiring, save/replay state, force integration, and source-engine
calibration remain open before the adapter can advertise native behavior.

The first pressure-wheel pass now provides a bounded, deterministic inventory
of literal `pressureWheels` rows and every relevant source section. It preserves
exact documented fields, unknowns, duplicate history, source-order `scale*`
modifiers, controllers, powertrain data, and Lua without executing or lowering
them. Schema-admissible rows remain explicitly
`inventory-only-never-lower`; generated topology, pressure, friction, brakes,
thermals, ABS, and drivetrain behavior are still absent. Hard record, retained
byte, value depth/work, diagnostic, topology-reservation, and canonical-output
ceilings plus strict, fast-math, and sanitizer fixtures close this inventory
boundary without implying J3 driveability.

Imported vehicle Lua and controllers are untrusted data. They never run inside
the game, editor, converter, tests, or build. A requested behavior becomes
available only through an allowlisted native implementation with a versioned
input contract and deterministic test.

### J4/J5 — Visuals and advanced native systems

J4 resolves Collada DAE meshes and
[`*.materials.json`][beamng-materials] without sharing global resource names
between vehicles. `mapTo` bindings, texture paths, flexbody node groups, props,
cameras, glow/emissive states, and damage material changes remain traceable to
their source. Unknown material fields and array shapes are preserved in the IR.
BeamNG metal/rough PBR maps require V1; full GPU flex throughput requires G0.
Missing or unsupported resources use obvious placeholders and diagnostics,
never an apparently successful silent fallback.

J5 is feature-by-feature native work, not a catch-all compatibility switch.
FormulaCOUPE provides a useful manual ladder: start with `FC-A7-01`, then test an
active-wing configuration, a parachute/air-brake configuration, a JATO/thruster
configuration, an EV/CVT configuration, mixed-terrain tyres, towing/cargo, and
controlled impact/failure configurations. Until a rung is implemented, loading
it must fail closed or run at a lower declared tier with exact structured
diagnostics.

Gate J0–J5:

- Malicious ZIP, parser, resolver, table, and expression fuzz corpora pass under
  documented CPU, memory, depth, entry-count, and expanded-size budgets. No test
  can escape the cache, access the network, execute a script, or write an
  archive-derived file into a tracked path.
- Clean-room fixtures cover comments and optional commas, table defaults,
  malformed rows, duplicate names, optional references, slot cycles, `.pc`
  selection, variables, components, transforms, expression limits, unknown
  field preservation, and source-span diagnostics.
- The same input resolves to byte-identical canonical IR and compatibility
  reports across archive enumeration orders and one, two, and eight workers.
  Platform-specific cache artifacts may differ only where the manifest declares
  the converter and target format.
- J2 lowerings match approved node/beam/triangle counts, total mass, center of
  mass, bounds, handedness, wheelbase/track landmarks, and reference directions;
  every supported reference resolves and every unsupported field appears in the
  report. A 120,000-step settle/impact soak remains finite and bounded.
- J3 fixtures test static wheel load and radius, steering travel, suspension
  bump/droop, brake/drive direction, gear ratios, and torque at the driven
  wheels. Repeated runs and one/eight-worker runs satisfy D0's replay contract.
- J4 resolves every supported local mesh, material, and texture, produces finite
  flex vertices, and creates no cross-vehicle OGRE resource collision. Visual
  captures and converted third-party assets stay local unless redistribution
  permission is recorded.
- With an explicitly supplied v0.9.7 FormulaCOUPE ZIP, J0 finds 39
  configurations and J1 resolves `FC-A7-01` twice identically. Later tiers
  record invariant and behavioral measurements, never bitwise or visual parity
  with BeamNG.drive. Absence of the optional archive cannot fail public CI.
- Public compatibility CI and distributable demos use only original or
  author-cleared fixtures. A public compatibility preview requires at least one
  such representative vehicle to reach J4 on Windows, Linux, and macOS; a
  locally successful fixture without recorded redistribution permission is not
  a shipping right.

## A0 — Content and licensing

Treat content provenance as a build input. Every redistributable asset needs a
machine-readable record containing source URL or repository, pinned revision or
checksum, author, SPDX license identifier, modification status, and editable
source location.

Third-party import archives and their generated caches are local inputs, not
redistributable project content. A public download or zero price is not itself a
license to bundle an archive or a conversion. Record the source URL, archive
hash, detected version, author, permission/license, importer schema, and
conversion options; refuse export or packaging when asset-level rights are not
explicit. Follow BeamNG's [modding guidelines][beamng-modding-guidelines] for
authorship and permission, while keeping the implementation independent of
BeamNG software and stock assets.

The six optional 2022.12 content packs total about 3.07 GB, and their hosting
repository did not declare a license during this audit. They may be user-fetched
validation inputs, but must not be bundled, remastered, or redistributed until
each included asset has verified permission.

Gate A0:

- A content-audit CI job accounts for 100% of files in a distributable package
  and fails on missing provenance, license, or checksum.
- Derivative assets retain their editable sources and attribution; generated
  runtime textures/meshes identify the source and tool version.
- The packaging audit fails if a user-supplied import archive or derived cache
  enters a distributable or tracked path without explicit compatible rights.
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
2. Start J0/J1's scanner, parser, resolver, and clean-room fixtures without
   waiting for renderer work; keep the importer outside runtime spawn code until
   the IR and hostile-input gates pass.
3. Make D0 deterministic before parallelizing or replacing more solver work.
4. Develop P1 and R0 in parallel, but do not merge new defaults without their
   independent gates.
5. Land J2 after its mass, coordinate, topology, collision, and finite-spawn
   gates. Land J3 adapters only after their P0/P1 numerical and D0 replay tests.
6. Land V0 as the render-regression seam, then complete R0 before V1.
7. Land G0 only after the modern renderer exposes a stable cross-platform GPU
   data path.
8. Land J4's PBR path after V1 and its GPU-flex path after G0. Land J5 one native
   feature at a time; never make controller execution a dependency.
9. Ship the A0 DAF/asphalt vertical slice and pass imported-content provenance
   gates before expanding the asset library or publishing compatibility demos.

A next-generation preview is ready only when the pinned four-scene suite passes
on Windows, Linux, and macOS; one-worker and eight-worker physics hashes match;
sanitizers are clean; post-FX/PBR/GPU-flex budgets pass; legacy content still
loads through fallbacks; and every shipped asset passes the content audit.
Interoperability does not broaden that claim: public builds publish a
per-package/configuration J0–J5 capability matrix and never imply general
drop-in compatibility.

[beamng-modding-guidelines]: https://www.beamng.com/game/support/policies/modding-guidelines/
[beamng-materials]: https://documentation.beamng.com/modding/file_formats/materials/
[beamng-mod-packing]: https://documentation.beamng.com/modding/mod-support/mod_packing/
[beamng-jbeam-syntax]: https://documentation.beamng.com/modding/vehicle/intro_jbeam/jbeamsyntax/
[beamng-trademark]: https://www.beamng.com/game/support/policies/trademark-guidelines/
[formulacoupe]: https://www.beamng.com/resources/gd808-formulacoupe.38055/
