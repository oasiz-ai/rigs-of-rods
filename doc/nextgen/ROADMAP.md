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
  graph pins OGRE `14.5.2` and the current MyGUI recipe for exact native macOS
  arm64, Linux x86_64, and Windows x86_64 targets. The macOS application uses
  GL3Plus and RTShaderSystem, and the OGRE 14 graph has no Cg package or runtime
  plugin. Linux/Windows runtime packaging, native application execution, and
  measured renderer parity remain open.
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
- Native ray tracing is now a renderer-selection priority. The opt-in Apple M5
  N2 checkpoint proves same-device Metal geometry export, BLAS/TLAS build,
  ray query/eight-byte probe readback, and guarded lifecycle. N3 additionally
  exports the exact UI-free Ogre HDR target, derives rays from the submitted
  camera, writes a hit-only Metal contribution, GPU-composites it into that
  target, and independently reads back raster, contribution, and hybrid images.
  This is still not a shipping RT renderer, reflection/GI implementation,
  performance result, or visual-fidelity claim. Ogre-Next's audited tree provides a materially
  stronger PBS/HDR raster foundation and Metal/Vulkan integration seams, not a
  complete cross-platform native RT implementation or a D3D12 renderer. The
  [Ogre-Next/native RT decision RFC](NATIVE_RAY_TRACING_BACKEND.md) makes a real
  Metal RT scene pass and DXR/Ogre-Next interop hard continuation gates while
  keeping OGRE14 default and fail-closed.
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
| AirSim visual floor | Pinned AirSim v1.8.1 plus one shared rights-cleared glTF scene | Same-scene PBR/HDR/shadow/AA/atmosphere/temporal comparison against offline path-traced truth |
| Legacy OGRE content smoke (local opt-in) | User-supplied `CityWorld.zip` + `CityWorld.terrn2` | Large v1.40 mesh loading, generated LOD safety, missing-material diagnostics, and clean terrain unload |

The Agora L definition has 151 authored nodes, 675 beams, and 222 cab triangles.
Its six legacy wheels and two cinecams bring the spawned actor to 297 runtime
nodes. It is useful for correctness but too small for a GPU throughput gate; the
benchmark must also instantiate repeated vehicles or a generated high-vertex
fixture.

FormulaCOUPE is a manual interoperability fixture, not project content. The
v0.9.7 metadata above was audited on 2026-07-27. The importer never downloads
it, and its archive, extracted files, conversions, screenshots, and golden
assets remain outside the repository and distributable builds. An opt-in test
checks the user-supplied ZIP's pinned SHA-256, J0 format profiles, archive and
expanded byte counts, entry count, and 39 `.pc` files; it reports `SKIP` when
the archive is absent and fails explicitly on version drift. J1 must extend
that record with importer version and selected configuration. Public CI uses
small, original clean-room JBeam fixtures with explicit licenses.

The current v0.9.7 archive was also inspected locally through the J0
metadata-only boundary: SHA-256
`f0ecff776eeb8962ed039ca02695713972f1839d754edd3385d47bb597a2cbcd`,
223,853,684 archive bytes, 460 entries, 642,023,303 declared expanded bytes,
and 39 `.pc` configurations. Its most compressible declared entry is a uniform
DDS resource at about 838.735:1, so the default ratio ceiling is 1,024:1 while
the independent 1 GiB per-entry and 4 GiB total-expanded ceilings remain
mandatory. The archive is a local validation input only and is not tracked or
redistributed.

CityWorld is likewise a local compatibility fixture rather than project
content. The archive tested on 2026-07-28 is 158,845,395 bytes with SHA-256
`ebeac2f0204f25ca1955f29ca1583b2afa4517a3a848feb1db203814acac2ef3`.
Do not track or redistribute it. Its reviewed legacy material defects are
handled by a hash-pinned, fail-closed compatibility path rather than by
modifying the archive or globally suppressing diagnostics. Unknown archive or
script revisions retain their original diagnostics. The measured fixtures,
bridge corridors, Blender/glTF authoring contract, LOD/collision requirements,
and visual acceptance gates are tracked in the
[CityWorld visual-upgrade specification](CITYWORLD_VISUAL_UPGRADE.md).

The first rights-cleared CityWorld Next content family is now present without
modifying that archive: Blender-generated 20 m tangent and 15-degree curved
bridge spans with three render LODs, separate validated collision, lane
connectors, standard glTF interchange artifacts, and a full A0 release-gate
provenance inventory. The curve adds an integrated pier/bearings, expansion
joints, four LED fixtures, and an emissive material lowered into OGRE.
Dependency-free validation runs on macOS, Linux, and Windows CI. The offline
glTF-to-runtime compiler has landed. The local-only v4 overlay retains the v3
corridor runtime and now
starts inside Penguinville's authenticated east carriageway, clears its curb
with a 14.8491 m collision-authoritative overlap, and joins NeoQueretaro's west
road with a continuous 1,075.448 m collision surface, destination seam closure,
160 m eased ramps, an 8 m raised deck, and 47 native terrain-reaching support
stations. It also derives a disabled, non-shipping manifest for the 67
authenticated luminaria poles within 400 m of NeoQueretaro Spawn, capped at
one representative 24 m light per existing pole with no duplicate geometry.
The same overlay now packages the first completed CW3 placement slice: exactly
18 authenticated `arbol1Qr` records are replaced in place by the project-owned
round, columnar, and windswept family. A shared native/package plan preserves
every source position, applies deterministic yaw and uniform render/collision
scale through 18 unique ODEF wrappers, emits no duplicate TOBJ placements, and
fails closed on archive, TOBJ, selector, source-record, or resource drift.
The building-overlapping prototype gateway is no longer placed.

Both modules now compile through the first production
[`ror_scene_compiler` boundary](CITYWORLD_SCENE_COMPILER.md). Each package owns
three manual render LOD meshes, separate road/barrier collision meshes, an
ODEF, a generated material fallback, and a canonical report. OGRE 14.5.2,
`MeshSerializer_v1.100`, little-endian output, the Blender/glTF/OGRE coordinate
basis, source/intermediate/output hashes, bounded counts, and stable
object/material traversal are enforced. The same standard-library validation
runs under normal and optimized Python on Windows, Linux, and macOS. The
macOS arm64 rolling app has cleanly loaded and visually checked the v3
Penguinville curb-free overlap, destination seam, raised deck collision and
visible pillars. Replacing the generic
construction alignment with the checked Blender ramp/deck/pier kit, then
running a deterministic full-corridor vehicle traversal on macOS, Windows and
Linux, is the next CW2 gate.

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

The first P1 kernel implements a versioned uniaxial
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
state, and fixed-seed property tests pass under strict C++11, fast-math, and
sanitizer builds.

The first production adapter is now wired into both local and inter-actor
`Actor` axial-beam paths as an explicit per-beam opt-in. It maps SI stress to
force using configured cross-sectional area and the beam's reference length, so
its elastic force/displacement slope is `E A / L`. Plastic strain, accumulated
plastic strain, damage, damage-driver density, last total strain, and fracture
state live on each beam. Actor reset clears that history while preserving a
validated opt-in configuration. Successful fracture disconnects both material
and damping forces and enters the existing beam-break path. Every assembled
force pair is generated equal-and-opposite, and invalid geometry,
configuration, history, material response, or float-runtime force range latches
the calibrated beam fault closed instead of silently reverting to the legacy
law. A faulted production beam is disabled; deterministic snapshots therefore
record its disabled flag and unchanged material history when that history is
finite. Corrupted non-finite history makes the canonical snapshot fail, rather
than admitting a NaN payload into a digest. The digest schema does not yet
distinguish a runtime fault from another disabled beam.

The native truck format now exposes the same opt-in through the versioned
`set_calibrated_beam_material 1, on, ...` directive and an explicit `1, off`
transition. Parsing is strict, finite, locale-independent, copy-on-write, and
atomic on failure; serialization preserves binary64 values at round-trip
precision. Version 1 is deliberately limited to normal `NOSHOCK` entries in
the `beams` section. Rope, support, shock, hydro, command, wheel, and other
specialized roles fail closed instead of inheriting a material law whose
semantics have not been calibrated. Omitting the directive leaves the exact
legacy path active.

Version-3 savegames now carry an optional actor-local schema-1 payload for
every authored calibrated beam. It records stable beam identity and role,
bit-exact authored configuration, all material history, fracture state,
fault/error state, and the legacy broken/disabled flags. The complete payload
is decoded and validated into temporary storage before assignment; unknown,
duplicate, non-finite, structurally mismatched, or configuration-mismatched
state rejects the restore and fault-latches authored calibrated beams instead
of silently reverting them to the legacy law. Actors without calibrated beams
retain the legacy v3 JSON shape, and old v3 saves without the optional member
retain their existing pristine-history behavior. Dependency-free and strict
RapidJSON fixtures lock atomic failure, hostile inputs, bit-exact text
round-trip, resumed state, and next-step force under strict, fast-math, and
sanitizer builds.

The prescribed-history calibration gate now exercises a non-grid-aligned
monotonic ramp, warped cyclic reversals, damage, and unloading at exact
`0.25`, `0.5`, and `1.0 ms` steps. Across those three rates, peak-force spread
is `0.0000213%`, permanent-plastic-strain spread is `0.0144%`, and irreversible
dissipation spread is `0.0274%`; the maximum independent work-quadrature energy
shortfall is `0.0500%`, and every equal-and-opposite endpoint force has exact
zero normalized momentum residual. Twenty-four additional fixed-seed profiles,
non-finite input, and fault-latch fixtures pass under strict, fast-math, and
sanitizer builds. This closes the local constitutive step-sensitivity gate; it
does not substitute for vehicle mass integration, contact, mesh-localization,
or real coupon calibration.

The version-1 monotonic crack-band convention is now executable rather than a
comment-only formula. A validated helper maps `G_f`, characteristic length,
elastic modulus, and hardening modulus to the local damage-driver capacity,
with transactional failure and finite checks that remain effective under
fast-math. A genuine series coupon—common force, summed element elongation,
one deterministic 0.5%-weaker notch, and unloading intact elements—passes at
1, 2, 4, 8, and 16 elements. Post-onset crack work, peak force, and permanent
set have zero measured spread; total work and irreversible-dissipation spread
is `0.371259375%`, the maximum equilibrium residual is `1.192e-16`, and exactly
one element fractures at every refinement. This closes only the monotonic,
single-localization kernel gate under the declared nominal-work convention.
Cyclic mesh objectivity, production-network localization control, selection of
a physical adapter characteristic length, and calibrated coupon data remain
open.

No BeamNG lowering rule, UI, or shipped vehicle enables the new model yet.
Replay injection/ownership, calibrated material datasets, mesh-refinement and
localization validation beyond the monotonic kernel, starter-content tuning,
and the versioned Agora impact regression remain open. P1 is not complete and
cannot become a runtime default until those gates pass.

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
not repeat a sample. Sleeping engines now advance that counter once per fixed
physics step and integrate on exact 32-step boundaries (62.5 Hz), independent
of render-frame grouping. The deterministic path is enabled by the
`sim_deterministic_sleeping_engine` CVar, with the old outer-frame scheduler
retained as a fallback. Full actor resets restart the counters; version-3
savegames carry optional seed/counter fields so existing version-3 saves remain
loadable and resumed saves retain the next cadence boundary. Golden vectors,
dependency-free one/two/eight-thread noise tests, and 50,000 fixed-seed cadence
fixtures lock the pure-function and frame-regrouping contracts.
Save/load continuation tests, the runtime TSan soak, the production broad-phase
oracle, and input-replay hashing remain open D0 work, as does a general
scenario-level seed/stream-ID contract independent of runtime actor-ID
assignment.

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

The first runtime micro-scenario now uses `simple2.terrn2` and two airborne
`b6b0UID-semi.truck` actors with fixed poses, explicit stable IDs/seeds, and
exactly 1,000 physics steps. Each spawned DAF has 176 runtime nodes, so the pair
requests 1,056 turbulence samples per step. The startup script synchronously
pauses at the zero-step boundary, spawns both actors while paused, enables the
trace, and advances exactly ten 2 kHz steps per rendered frame until the
immutable 1,000-step limit finalizes the artifact. The production runner
verifies the pinned content revision and every fixture byte, isolates the user
home, disables online/audio and nonessential visual systems, fixes macOS
rendering at 1280x720 with scale factor 1, and fails closed on missing markers,
extra traces, invalid metadata, timeout, crash, or state divergence. On native
Apple Silicon, thirty fresh-process runs with one worker and thirty with eight
workers matched every canonical state record. This closes the pinned
two-vehicle worker-count runtime check, but not the remaining D0 gates.

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
sensitivity. The production `Actor` adapter now canonicalizes live actors and
contacts after worker synchronization and validates cross-section references,
accepted material schemas, and flag masks.

The version-1 state-trace container adds a checked run header, contiguous
64-byte per-step digest records, and a mandatory aggregate trailer. Its bounded
streaming reader rejects every tested truncation or single-bit corruption, and
the comparator reports the first divergent step while allowing only an
explicit worker-count exception for D0's one-versus-eight-worker check. The
`ror_state_trace` CLI emits canonical JSON with distinct match, divergence, and
invalid-input exit codes.

The opt-in production caller now records a digest after every completed 2 kHz
fixed step, after physics workers, canonical contact reduction, and free-force
application have finished. `sim_deterministic_state_trace` controls capture and
`sim_deterministic_state_trace_scenario_id` supplies an unsigned decimal
scenario identity. Each uniquely reserved `.rortrace` artifact is written
under the configured log directory without overwriting an existing file. Its
metadata records the actual worker-pool size, exact `1/2000 s` cadence, and
fast-math mode. The parallel path captures contact keys from the exact ordered
buffers it applies; the serial fallback captures the same resolved stream
without truncating collision response. Disable, scene cleanup, destruction,
immutable step limits, and capture errors finalize the artifact. Diagnostic
allocation, quota, digest, or I/O failures stop only tracing and never skip a
physics force.

The version-1 D0 input-trace kernel now defines a bounded canonical recording
contract independently of devices and worker scheduling. It samples stable
logical target/control IDs at the start of each contiguous fixed step, records
only persistent-state deltas plus explicit one-step impulses, and emits no
wall-clock or paused-frame records. Exact binary64 values, reduced cadence,
canonical UTF-8 identities, scenario/source identity, immutable ceilings, a
per-frame SHA-256 chain, and an authenticated mandatory trailer make malformed,
truncated, reordered, redundant, non-finite, or trailing data fail closed. The
streaming reader reconstructs a copyable persistent state for save/load
continuation, while the comparator drains both streams before reporting the
first semantic divergence so later corruption cannot be hidden by an earlier
difference. Golden bytes and an independently checked digest, every truncation,
every single-bit mutation, strict/fast-math/sanitizer builds, segmented writes,
worker-bucket ordering, and a 10,000-step fixed-seed continuation fixture lock
the dependency-free kernel.

The version-1 deterministic-input runtime now supplies the bounded lifecycle
around that byte contract. Recording and replay have explicit
idle/running/paused/finished/faulted states, accept only contiguous fixed-step
keys, and call their source or sink exactly once for an accepted step. Device
aggregation remains outside the dependency-free kernel: a collector admits
only strictly ordered, unique `(target, control)` keys and distinguishes
persistent deltas from one-step impulses. Replay authenticates the complete
trace before it can inject any batch, reconstructs the sorted persistent
control state at every step, and stops permanently on a rejected or throwing
sink. Version-1 continuations retain the immutable identity, tightened quotas,
complete authenticated trace, processed-step count, digest, and next step.
Their canonical integrity envelope also binds mode, lifecycle, quotas, cursor,
and trace identity, so internally consistent cursor or policy edits fail
closed; hostile savegames still require authentication by their owning
container because this digest is not a keyed MAC.
Export/import deliberately costs `O(trace)` so a savegame cannot resume from an
unauthenticated internal hash or stream cursor. Strict, fast-math, sanitizer,
hostile-I/O, hostile-source/sink, quota, corruption, frame-regrouping,
continuation, and 10,000-step fixtures lock the contract.

The first restricted applied-control-state bridge now gives that runtime a
versioned, dependency-free vehicle boundary. Schema 1 assigns stable nonzero
control IDs to steering command, service brake, throttle, clutch, parking
brake, engine contact/starter, gear/range, steering-speed coupling, trailer
parking brake, and all 84 native command keys. One stream owns one stable
target. Every value must be exactly representable as the binary32 value used by
`Actor`/`Engine`; negative zero is rejected and positive zero is the only
release value. Its canonical registry manifest is bound into D0 metadata as
`ror-restricted-applied-control-state-v1` plus SHA-256
`5368675b48c68ee2804455ed0577bc5069aab2fa50a210c3dbe9d28785057f95`;
the stable target ID is also the authenticated D0 stream ID. A
schema/control-table or target change therefore cannot silently reuse an old
trace identity, including for an all-zero control stream.
Recording captures one complete fixed-step-start snapshot and emits only
bitwise persistent changes. Replay reconstructs the complete authenticated
state, proves that nonredundant step deltas produce that state, rejects active
zeros, unknown targets/controls, impulses, noncanonical ordering, non-finite or
out-of-domain values, and invokes its consumer once only after full validation.
Adapter construction is bound to an initialized runtime of the correct mode;
the runtime rejects cross-runtime source/sink use before sampling or consuming
a frame. The adapter automatically derives the mandatory fresh or continuation
baseline from its authenticated persistent state. Strict, fast-math, sanitizer,
hostile-batch, quota, transactional-failure, continuation, registry-mismatch,
and 4,096-step fixed-seed round-trip fixtures lock the adapter without
importing device, actor, OGRE, or scheduler dependencies.

This is not yet authorization to replay every truck. A live consumer must also
validate gear counts and either encode or reject unsupported automatic-shift
intent/timers, gearbox selector/mode changes, differential and transfer-case
modes, ABS/traction-control, cruise, speed limiting, and other controller state
before it mutates an `Actor`. Multi-actor atomic input uses a future composite
schema rather than silently sharing this single-target stream.

Production input-map capture and replay injection, lifecycle/error CVars,
general scenario-assigned IDs independent of runtime actor indexes, savegame
ownership of the input runtime continuation, and the TSan soak are still open.
The completed two-truck scene validates collision/state determinism without
claiming that its neutral controls close the input-replay gate.

For a local kernel stress pass, run
`ROR_PHYSICS_TEST_REPEAT=30 tools/run-physics-tests.sh`, then repeat with
both `ROR_PHYSICS_TEST_REPEAT=30` and
`ROR_PHYSICS_TEST_FAST_MATH=1`. This covers the 120,000-step axial soak,
fixed-seed contact oracle, one/two/eight-buffer reductions, counter-noise
threading, state-digest golden vectors, and exhaustive trace integrity checks.
Run `tools/run_deterministic_scene.py` against a full application build and its
`ror_state_trace` tool for the pinned `simple2` production runtime gate. The
runner defaults to thirty runs each at one and eight workers and compares every
artifact with the first accepted trace.

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
`no supportable Techniques` error. OGRE 14-safe placeholder texture units
preserve every managed diffuse/specular lookup name; the packaged Falcon run
loaded and assigned all body, interior, engine, glass, wheel, tyre, and needle
maps and rendered the textured vehicle. macOS keyboard input now uses SDL
physical key and UTF-8 events while retaining an unbuffered OIS compatibility facade.
The SDL Cocoa responder is restored after OIS initialization and each focus
gain, and live input was confirmed on the packaged build. Scene unload and
application shutdown complete through OGRE teardown without a new macOS crash
report. The local OGRE patch also closes a shadow-projector index underflow
found by an LLDB hardware watchpoint during that scene load.

OpenAL teardown now retains the device and context it owns, deletes only
successfully generated sources and buffers while that context is current, and
is safe when device creation, context creation, or context activation fails.
An executable smoke test opens the pinned OpenAL Soft null backend and exercises
device, context, source, buffer, playback, and teardown calls without requiring
audio hardware. This verifies the native library lifecycle, not audible output,
output-device switching, or long-running scene audio.

A dependency-free controller state contract and executable SDL virtual-joystick
test now cover deterministic slot assignment and reuse, bounded axis/button/hat
state, transitions versus repeated values, focus reset, disconnect, remapping,
and invalid input. The production macOS runtime now enumerates, hot-plugs, and
samples gamepads, wheels, flight sticks, and throttles through that SDL
backend, while the existing OIS-shaped state facade keeps input-map evaluation
portable. Standard gamepads use SDL's position-based
`SDL_GameController_v1` profile; specialized and unmapped devices retain a
bounded raw profile. Focus loss clears state and focus gain performs a fresh
poll so stale controls cannot remain engaged. Virtual devices prove the
integration and mapping contract, not representative physical HID behavior or
force feedback; the SDL macOS path still exposes no force-feedback device.

The exact-commit coworker preview
`macos-arm64-preview-2026-07-27-9270490` independently passed a post-download
audit: all 14 Mach-O executables/libraries are thin arm64 with macOS 11.0
minimum deployment metadata, bundle-relative rpaths, deep ad-hoc signatures,
four renderer plugins, nine frameworks, byte-identical staged OGRE shader
trees, and 29 valid resource archives. The downloaded artifact then completed
an Apple M5 GL3Plus/OpenAL/AngelScript runtime smoke. This establishes a
reproducible Apple Silicon handoff, but the artifact is not notarized and does
not replace the longer scene/resource or physical-device gates.

On 2026-07-28, the signed arm64 bundle completed the 1,000-step deterministic
two-truck scene with PSSM enabled on each of `simple2_a`, `simple2`, and
`simple2_w`, including terrain unload, the following main-menu render, and
process shutdown. The fix activates PSSM only after the terrain camera and main
light exist, and disables its shadow texture cameras plus RTSS template before
terrain scene objects are destroyed. This closes the reproduced stale-projector
crashes during terrain loading and unloading. All 39 configured CTests passed
after the change, with the user-supplied FormulaCOUPE test skipped as designed.
A 2560x1440 Retina capture proves that terrain, sky, character, generated RTSS
material programs, and the complete bottom edge render into the backing store.
The first automated PNG appeared to contain a 240-pixel black band because the
detached encoder was terminated during rapid process shutdown; strict decoding
instead identified a truncated file. Screenshot encoding is now a bounded,
owned asynchronous operation: a second capture or renderer teardown joins the
previous writer and reports codec failures on the main thread. The 1,000-step
rapid-shutdown capture now exits cleanly, fully decodes, and has non-black
pixels across all 128 sampled bottom-edge positions. Sampler-validation
diagnostics were then traced to OGRE's premature link-time
`glValidateProgram()`: mixed 2D, cube, and PSSM shadow samplers still had their
default unit values because draw-time state had not been assigned. The pinned
recipe already carried the correct deferred-validation patch, but the
application build referenced an older detached Conan package directory. After
regenerating the locked dependency graph and relinking, the same 1,000-step
PSSM capture logs no sampler-validation failure or `GL_INVALID_*`, exits
cleanly, and fully decodes. The deterministic scene runner now rejects either
diagnostic. These findings do not close the broader R0 visual-parity gate.

The same bundle reproduced a CityWorld load crash in OGRE 14's
`EdgeListBuilder::buildTrianglesEdges()` while
`MeshLodGenerator::_configureMeshLodUsage()` rebuilt edge data for the
203,593-vertex `NeoQ2-0main-city-section-B.mesh`. RoR exposes texture shadows
or no shadows, so `MeshObject` now discards imported stencil-shadow edge lists
and disables automatic edge-list rebuilding on OGRE 14 before preserving or
generating mesh LODs. The pinned local archive then loaded through
`===== TERRAIN LOADING DONE CityWorld.terrn2`, rendered ten frames, emitted
`[RoR|CI|BundleSmoke] PASS frames=10`, unloaded, and exited with status zero
without a new crash report. The guard is renderer-version-specific rather than
OS-specific; native Linux and Windows execution remains part of the
three-platform R0 gate.

The macOS host now treats logical window points and renderer backing pixels as
one explicit platform-neutral display contract. At 1x, the isolated
two-truck scene reported 1280x720 logical and backing extents and completed all
1,000 deterministic steps. At 2x, the signed bundle reported a 1280x832
logical host, 2560x1664 backing viewport, and 2.0 framebuffer scale, then
loaded and rendered the ten-frame bundle smoke without a renderer diagnostic.
SDL high-pixel-density mode is requested only for a configured scale above 1x;
mouse bounds, MyGUI view size, Dear ImGui projection/scissors, and its
high-resolution font atlas share the resolved metrics. The dependency-free
contract test covers 1x, 2x, fractional/non-uniform, zero, hostile, NaN, and
infinite inputs under the project's Release fast-math flags. All 40 configured
native CTests pass, with the user-supplied FormulaCOUPE fixture skipped as
designed. The window was visually inspected at 2x to confirm UI retained its
logical size instead of doubling with the backing viewport.

The sky selection now has one compile-time availability contract shared by
config parsing, settings UI, resource loading, terrain setup, and lighting.
This closes a CityWorld darkness failure on builds compiled without Caelum:
the saved Caelum choice previously bypassed both Caelum's light and the basic
directional `MainLight`. Such builds now persist and use the dependency-free
Sandstorm fallback, including its directional sun, ambient lighting, fog, and
skybox; SkyX and compiled Caelum choices remain unchanged. The dependency-free
resolver covers available, missing-Caelum, missing-SkyX, and basic-only builds.
On 2026-07-28, all 41 configured native CTests passed, with the opt-in
FormulaCOUPE fixture skipped as designed. The signed arm64 candidate then
loaded the pinned local CityWorld fixture, captured a fully decoded 2560x1664
frame after a 120-frame warmup, exited at frame 180 with the declared pass
marker, and persisted `Sky effects=Sandstorm (fastest)`. The capture confirms
direct sun and ambient response; it also keeps CityWorld's missing-material
messages visible as the separate content-authoring failures they are.

The authenticated CityWorld material-compatibility gate now resolves those
reviewed content failures without altering or redistributing the original ZIP.
The OGRE 14 resource listener matches the opened script bytes to an exact
member of the SHA-256-authenticated archive, then applies seven exact
archive-and-script-hash plans with 1, 1, 2, 4, 2, 47, and 5 line edits, for 62
edits total. The `NeoQueretaro.material` plan clears the authenticated second
copy of the `concretorojo` block at original lines 1772-1784 and preserves its
first definition at lines 1698-1710. Its four additional exact edits convert
the legacy environment-map pairs to OGRE 14 syntax, removing the blank
environment layers from `parabus` and `semaforogris3` and eliminating both
associated deprecation warnings. The replacement declarations explicitly
match RoR's manually created cube, zero-mipmap, `PF_R8G8B8`
`EnvironmentTexture`, preventing OGRE 14 from treating the shared render
target as a conflicting redeclaration. Mesh requests follow OGRE's exact-case
archive precedence and are opened from the selected authenticated ZIP before
the listener maps 23 exact
legacy names to target materials parsed from that same archive SHA or creates
11 explicitly reviewed lit fallbacks. Seven exact missing `.dds` directives
may use collision-resistant deterministic 4x4 DDS resources. Once authorized,
those names are always served from the in-memory payload and never delegated
to later resource locations; stale authorization aborts the load. The missing
JPEG reference is converted to a texture-free lit pass instead of receiving
bytes from the wrong codec. A macOS arm64 load of the local overlay reached
`TERRAIN LOADING DONE`, used GL3Plus RTSS programs, emitted no CityWorld script
or missing-material diagnostic, made no JPEG request, and shut OGRE down
cleanly. The only two missing-material warnings were pre-existing `MeshesRG`
resources outside the authenticated CityWorld group. All 47 configured native
CTests passed, with FormulaCOUPE still skipped by default. Linux and Windows
share the OGRE 14 code path, but their native runtime gate remains open.

The first original Blender-authored CityWorld Next bridge-to-city family now
crosses the offline/runtime boundary. A fail-closed glTF compiler lowers its
applied, bounded static scenes into deterministic OGRE 1.100 little-endian
meshes, manual 80/180 metre render LODs, separate collision meshes,
RTShaderSystem material fallbacks, and ODEFs that preserve the Y-up basis. The
checked package and provenance cover every output byte. The family now includes
the tangent and 15-degree curved spans, a 12 m abutment/transition, and a 40 m
gateway block with four mid-rise façades, individual windows, sidewalks, eight
trees, and eight streetlights.

The gateway lights are the first bounded dynamic-light compiler slice. Eight
versioned warm point lights are validated for count, identifiers, type, finite
position/colour, colour range, and physical range before deterministic
Blender-to-OGRE coordinate conversion and ODEF lowering. Runtime creation is
logged, emissive lenses remain active, and the local lights do not alter the
global PSSM configuration.

The corrected full CityWorld link now has its first topology-safe Blender
visual layer. Sixteen collisionless, parapet-mounted LED fixtures alternate at
40 m stations along the flat raised deck, point inward, and each instantiate a
validated 24 m warm point light. Overlay v3 extends the procedural road to
1,075.448 m with a 14.8491 m source overlap: it begins on Penguinville's
authenticated carriageway surface, rises 11.2 cm over 10 m, and clears the
decoded 0.30 m curb by 1 cm across the full 8.9 m bridge mouth. This
collision-authoritative apron functionally removes the curb from the driven
connection while leaving the private original city archive unchanged; a later
rights-cleared direct-city pass may bake the cut into an editable mesh. The
7.5 percent grade ceiling, continuous collision surface, and 47
terrain-reaching pillar requests remain authoritative. The cross-platform
provenance matrix now executes the overlay builder under normal and optimized
Python on macOS, Linux, and Windows; native full-map Windows/Linux driving is
still an open runtime gate.

Overlay v4 establishes the first deterministic existing-city relighting gate.
The v2 archive audit classifies luminaria as fixtures and proves that the
pinned map contains 528 `luminariaLQr`, 239 `luminariaQr`, and 12
`luminariaYQr` placements. Within 400 m of the authenticated NeoQueretaro spawn
there are exactly 42 single-arm and 25 dual-arm poles. Their three source ODEFs
are collision-bearing and contain neither LOD nor local-light directives, so
the generator reuses the existing pole geometry and emits only a local,
nonredistributable candidate manifest. Each of its 67 records specifies one
representative warm point light, a hard 24 m range ceiling, no requested
shadow casting, and a future light-only `none`-mesh adapter in the legacy
Z-up ODEF basis.

This slice is intentionally runtime-disabled and emits zero NeoQueretaro
lights. The terrain-object path now disables shadow casting for all local
point and spot lights and reports `local_shadow_casters=0`, so the manifest
records the zero-local-shadow contract as satisfied. Activation remains
fail-closed until the renderer provides the
`ror-cityworld-local-light-budget-v1` policy, followed by UI-free fixed-camera
visual comparison and frame-time gates on macOS, Windows, and Linux. The build
rejects family-count, 400 m scope, or exact source-ODEF drift, and its canonical
report exposes the candidate payload hash, derived-record count, zero runtime
emission, and non-shipping rights state. It is activation-ready content
provenance, not a claim that whole-map relighting or ray tracing is complete.
The corridor runner now consumes report v5, authenticates every candidate and
source-pole definition, rejects any emitted core adapter/placement, validates
both intercity procedural-road contracts and all 48 project bridge lights, and
requires a byte-identical 50-entry rebuild before it runs the unchanged v3
Penguinville road diagnostic.

Overlay v5 adds the direct road-to-road NeoQueretaro-to-NeoQ2.0 link. It
authenticates the exact east and west distributor placements, their six
render/collision/ODEF resources, and line-378 `autopistaQr` with its exact
mesh/ODEF pair, then joins the decoded distributor seams with a
3,076.132 m native procedural surface. Both city endpoints have zero generated
overlap, leaving one authoritative collision surface at each flush seam and
preserving NeoQ2.0's median and both live carriageways. The 24 m deck tapers to
the decoded 15.1 m destination span, ends level at 0.2 m, and reports zero
step, grade, yaw, and width-edge discontinuity. Per-road
`collision_endcaps_enabled false` removes all six transverse cap collision
faces without changing legacy roads.

The pinned `autopistaQr` decode identifies 9,599 upward-facing live-road
triangles in `calleunsolosentido` and `pavimento`. Expanding prospective
columns by the 2.5 m heavy-truck clearance intersects those polygons at the 18
stations from 80 through 760 m, so those complete pairs are authored
`bridge_no_pillars`. The remaining raised deck requests 56 paired outboard
side-pier stations and 33 alternating inward bridge lights. Hammerheads remain
at least 5 cm below the road slab, and all 168 support collision AABBs clear
the heavy-truck prism. Native accounting must report exactly 56 requested and
built pairs with zero skips.

The earlier clean macOS arm64 five-camera run covered a superseded
center-pillar/destination-overlap prototype and is retained only as historical
diagnostic evidence. The v4 replacement gate now passes natively on the Apple
M5 arm64 executable
`2113bed84fb40313b707206c3e48188866d4fa9e907e9f958b55590eb394609c`.
It produced six byte-distinct UI-free 1280x720 RGBs, including an unobstructed
`autopistaQr` underside and wheel-height destination seam, and reported
the exact combined SidePiers multiset `46/46/0` and `56/56/0`. The
collision-enabled eastbound plus westbound drive
covered 3,161.36 m in 424,240 physics steps; the reverse actor crossed 60.1921 m
from the live NeoQ2.0 lane onto the deck with 0.0822754 m maximum path error,
0.808374 m vertical error, and 0.00537109 m regression. Normal and optimized
Python tests lock the portable content contract; native Windows and Linux
replacement gates remain open.

The full-map visual acceptance gate remains open, but the combined two-corridor
macOS arm64 runtime gate now passes on that same executable. It rebuilds the
50-entry overlay byte for byte, validates all 48 lights and the exact
`46/46/0` plus `56/56/0` support summaries, and produces four byte-distinct
UI-free 1280x720 seam views. Two collision-enabled DAF traversals cover
2,146.23 m in 377,000 fixed physics steps with 1.1988 m maximum path error,
0.808091 m vertical error, and 0.00591469 m regression. This directly proves
the flush Penguinville road seam, curb-free travel width, both bridge
directions, and complete native paired-pier construction.

Overlay v7 adds the first authenticated regional-infill slice without changing
or redistributing the private CityWorld archive. Its version-2 plan places 46
project-owned instances across eight bounded sites: 13 farmstead instances, 17
suburb blocks, two service stations, and 14 natural-landmark instances. Seven
curb-free native procedural routes originate at exact decoded legacy-road
anchors. Five route-to-asset contracts close the two station forecourts, two
suburb streets, and one farm lane with zero measured seam gap. Plan validation
rejects non-designated route/asset intersections, road/collision overlap,
building intrusion, component drift, and a generated-road clearance below
5.4 m.

The five glTF families now carry versioned, connected collision-component
manifests. The service station has 17 component boxes, the suburb block has
eight, and each remaining family has one, for 28 components total. Validation
derives real render and collision bounds from the GLBs, rejects node transforms,
inverted winding, overlapping boxes, non-cuboid deformation, stale Blender or
compiler identity, and packaged component drift. The v7 ZIP has 76 unique
members and rebuilds byte for byte at SHA-256
`c81206e5f7805b4bf289458ebdd5960993f79a82ec18114b50799001995d359f`.

At content commit `16373204b9e8bbf4c7b6949a6e7a7a37f09f7d2a`, the installed
Apple Silicon app passed the regional-infill native gate on the Apple M5
GL3Plus renderer. It produced 13 distinct UI-free 1280x720 captures, including
one close view for each active seam, over 545 ready render frames and exactly
2,176 completed deterministic physics steps. Both station instances created
all 12 bounded canopy lights, both bridge support summaries remained
`46/46/0` and `56/56/0`, every reviewed material resolved, and the process
shut down cleanly. The first ready render frame arms the four-step batch, so
the exact completed-step invariant is `(545 - 1) * 4`, not `545 * 4`.

This is a verified content-density and connectivity checkpoint, not completion
of the full-map or AirSim-referenced visual gate. The native captures remain
stylized and sparse: regional ground materials, terrain blending, decals,
street furniture, higher-detail vegetation, building variation, traffic, and
PBR/HDR are still open. Fixed cameras do not prove vehicle traversal across all
seven new routes. Native Windows and Linux repetitions, route-driving gates,
frame-time budgets, and the shared-scene V2 comparison remain required.

The optional transition, curve, span, and gateway visual families remain
unplaced; the accepted routes use native procedural surfaces plus the
project-owned direct road seam. Promotion still requires a complete
rights-cleared visual pass, whole-route building-clearance and performance
review, and native Windows and Linux repetitions.

A portable runtime gate assembles an isolated native fixture from the pinned
content submodule. On macOS arm64, the DAF drove 137.569 metres through three
curves, the transition, and the gateway in 30,580 deterministic physics steps.
All four mathematical seams are exact, maximum path error was 1.43999 m, actor
height stayed at 0.807714–0.867233 m, all eight point lights and every
render/collision mesh loaded, all visible materials received GL3Plus RTSS
programs, and the UI-free 1280x720 RGB proof fully decoded. The installed
rolling app reproduced the pass. Native Windows/Linux physical execution and
insertion into a rights-cleared full CityWorld overlay remain open.

The controlled CityWorld PSSM gate is now closed on the macOS arm64 reference
host. At commit `7f7f131fed2a8bfaa77f0bb1bfed919112140b7f`, fresh isolated
no-shadow and quality-2 PSSM runs used the same signed executable, Apple M5
OpenGL 4.1 device, 1280x720 camera, runtime pack, renderer configuration, and
30,580-step traversal. Requested and effective RoR configurations normalized
to identical hashes after removing only `gfx_shadow_type`. The PSSM run
proved the RTSS receiver, three `PF_DEPTH16` cascades at
3072/2048/2048, lambda 0.97, and 0.5/7.816331/45.241116/350 m splits.
All gated physics outputs were exactly equal. The cast-shadow comparison
darkened 2.0818142% of the frame by at least four luminance levels and
1.3471137% by at least twelve; the fixed occluder region measured
20.2045455% and 17.1233766%, respectively, with no qualifying lightened
pixels. Across 1,410 post-warmup samples, PSSM measured 1.99006 ms mean and
3.44846 ms p95, adding 0.72936 ms mean and 1.24438 ms p95 over the control.
The parent report hashes every child report, RGB proof, and stdout artifact.

The first three-platform OGRE 14 dependency foundation is now checked in
separately from runtime support. Exact release profiles and application locks
cover macOS arm64, Linux x86_64, and Windows x86_64; unsupported operating
system and architecture tuples fail closed before Conan runs. The graph selects
GL3Plus on macOS/Linux and D3D11 on Windows, uses OIS 1.5.1 on all three OGRE 14
targets, preserves the upstream Windows MyGUI configuration-library layout, and
keeps the legacy graph unchanged. A lock auditor exports the current local
recipes and verifies the exact OGRE revision
`68db16985fa623986379d2b9422d0dce`, MyGUI revision
`a8b971a8ab16d2deb80ee5ea91cf023b`, target identity, options, and
platform-specific dependency closure. The audited application graphs contain
62 macOS, 199 Linux, and 47 Windows references. A fresh locked macOS arm64
provider configure built all 352 targets, the MyGUI consumer probe linked and
ran, and all 41 configured CTests passed. This proves the cross-platform
dependency contracts and current macOS realization; platform-aware plugin
staging, relocatable Linux/Windows packages, native application builds, and
runtime smoke tests remain required.

The following runtime-configuration slice centralizes the renderer, package
plugin directory, installed plugin folder, and debug-suffix policy for the same
three exact target tuples. Generated release and debug configurations activate
only FreeImage, the platform renderer, ParticleFX, and OctreeSceneManager:
GL3Plus on macOS/Linux and D3D11 on Windows. All configuration tokens remain
unsuffixed because OGRE 14's loader applies `_d` when resolving physical
Windows Debug DLLs; this avoids the legacy template's invalid `_d_d.dll`
lookup. D3D9, legacy GL, Metal, Cg, and every non-platform renderer remain
inactive. Release and Debug each resolve their exact immutable Conan package
root, preventing multi-config builds from drifting to whichever dependency
graph happened to configure last. The R0 multi-config contract exposes exactly
`Debug;Release`, matching the two graphs installed by the provider; unsupported
configurations fail closed instead of linking a partial graph. Build-tree
configurations may name those package roots, but installed configurations are
generated separately with only relative plugin folders. The macOS stager owns
both `plugins.cfg` and `plugins_d.cfg`, and writes identical unsuffixed runtime
tokens for both names. Synthetic CMake tests prove all six platform/build-type
configurations, package-root selection, and unsupported targets fail closed.
The full macOS arm64 application rebuilt, all 41 configured CTests passed, and
the stager produced a deep-valid signed bundle containing the expected four
plugins under the contract-owned `../PlugIns` folder. Linux shared-object
staging now has an implementation contract but still requires native proof;
Windows DLL closure, native Debug and relocated smoke tests, and native CI
remain open.

The Linux x86_64 OGRE 14 install contract places its four config-selected
plugins under `lib/OGRE`, gives the executable only package-relative
`$ORIGIN/lib` and `$ORIGIN/lib/OGRE` search paths, and generates identical
unsuffixed Release and Debug plugin tokens. Its install-time stager resolves
the ELF closure from the build executable and exact active plugin modules,
accepts non-system libraries only from Conan's declared runtime roots, follows
complete SONAME symlink chains, and explicitly excludes the distribution-owned
`/lib`, `/lib64`, `/usr/lib`, and `/usr/lib64` roots. Unresolved dependencies,
basename conflicts, unexpected plugins, absolute or escaping symlinks,
non-x86_64 ELF inputs, absolute loader metadata, and build/cache paths all fail
closed. A separate launcher resolves the package from its own location instead
of the caller's working directory. Dependency-free CMake tests cover the
parser, package boundary, loader metadata, symlink closure, hostile paths, and
legacy install separation on non-Linux hosts. A native Linux build followed by
a relocated clean-environment launch is still required before this contract is
called proven.

An isolated native Release workflow now supplies that execution boundary for
Linux x86_64/GCC 11 and Windows x64/MSVC 19.44. Each job verifies its compiler,
resolves the exact checked-in Conan 2.31.1 profile and lock, builds and runs
CTest, installs into a fresh prefix, relocates the result, and audits the
package from outside its build tree. The Linux lane validates the complete ELF
closure and loads the exact GL3Plus plugin set under Xvfb with llvmpipe; the
Windows lane validates an AMD64 PE/DLL closure and loads the exact D3D11 plugin
set from the flat application directory. Both lanes run working-directory-
independent help and version smokes. They now also drive the deterministic
CityWorld bridge scene at 1280x720 using a fail-closed GL3Plus configuration on
Linux and D3D11 configuration on Windows, preserving the runtime, decoded RGB,
requested/effective configurations, reports, and diagnostics as CI artifacts.
The workflow and its hostile-input contract tests are checked in, but native
parity is not called proven until both hosted jobs pass on the committed
revision. Hosted llvmpipe/D3D11 execution proves native API behavior, not a
physical GPU, vendor-hardware performance, physical input, or Debug package;
those remain separate acceptance gates.

The legacy OGRE 1.11 Windows build also keeps RoR in C++17 while consuming
Caelum/PagedGeometry headers that still expose `std::auto_ptr`. MSVC now enables
its documented `_HAS_AUTO_PTR_ETC=1` transition surface only on the RoR target
and only when either affected addon is active. It is not a global compiler flag,
does not affect the OGRE 14 path, and can be removed when those legacy
dependencies are modernized or retired. Dependency-free contract tests prove
the guard and target-private scope; the native Windows build remains the final
acceptance gate.

This is meaningful R0 progress, not completion. The remaining gates include:

- Extend the display-metrics proof to native Windows/Linux, window resize and
  cross-monitor density transitions. Extend the zero-GL-diagnostic proof
  across every validation scene, and execute the paired PSSM gate on native
  Windows/D3D11 and Linux/GL3Plus hardware with declared backend-specific
  timing profiles; then cover dynamic cubemaps, water, sky, vegetation,
  particles, UI, mirrors, screenshots, and hot-load against recorded
  baselines.
- Verify real controller enumeration, hot-plugging, representative vendor
  mappings, and force feedback on physical hardware. Add a native
  force-feedback path rather than silently presenting the OIS device API as
  available. Verify audible scene audio and
  output-device changes, expected user-directory behavior, and ten-minute
  `simple2_a`, `simple2`, and `simple2_w` resource-growth soaks.
- Decide and prove the production Metal material path. RoR media still has no
  authored MSL pipeline; the current application path intentionally uses
  GL3Plus.
- Complete platform-aware OGRE plugin-binary and runtime-library staging, build
  and test the full OGRE 14 application on Windows and Linux, add the
  three-platform native CI matrix, and measure the declared CPU/GPU frame-time
  budgets.

Merely changing version strings is still an explicitly rejected milestone.

## R1 — Ogre-Next/native ray tracing decision gate (priority)

Do not treat an Ogre-Next library upgrade as proof of native ray tracing.
Ogre-Next is the candidate high-quality raster/PBR frontend; RoR must supply and
prove native Metal RT, DXR, and Vulkan KHR backends through explicit same-device
resource interop. OGRE14 remains the default until the full migration gate
passes.

Gate R1:

- A dependency-free selector defaults to OGRE14/RT-disabled and refuses to
  report RT without a compiled backend, accepted hardware capability, real
  BLAS/TLAS dispatch/readback probe, and scene interop.
- The standalone Metal admission probe and the Ogre-Next N2/N3 interop probes
  have passed on the recorded Apple M5. N2 rastered a renderer-neutral deformed RoR
  scene, exported the exact pooled Ogre v2 position/index slices from that
  raster `Item`, built BLAS/TLAS from those buffers on Ogre's own device and
  queue, dispatched one ray, and validated its eight-byte GPU probe. This closes
  API/hardware/dispatch and geometry-interoperability subgates. N3 retained and
  exported the exact Ogre `RGBA16_FLOAT` target through a renderer-neutral
  image lease, traced view-dependent primary rays, wrote a separate hit-only
  contribution, and GPU-composited that contribution into the target on the
  same device and queue. The independent readbacks prove 80 affected pixels,
  bit-identical misses, camera-dependent output, resize after release, and
  bounded device-loss/timeout cleanup. It still does not enable a shipping
  `native_rt=metal` path or claim reflection, shadow, GI, denoising, material,
  image-quality, or performance parity.
- Ogre-Next `v3.0.0` is evaluated as an exact pin on macOS arm64, Windows
  x86_64, and Linux x86_64; development `master` is not a shipping dependency.
- The first isolated dependency checkpoint now pins `v3-0` commit
  `37149a802de747f6806996fa3067b0748ecc1084`, verifies archive/license/patch
  hashes, records the loaded HLMS shader tree as
  `MIT AND LicenseRef-Heitz-LTC-Paper-Notice` with exact LTC source and
  redistribution-notice hashes, and leaves the RoR/OGRE 14 graph untouched.
  Its native macOS arm64
  executable registered Metal, linked HLMS PBS with the Metal shader family,
  and compiled Compositor2 while explicitly reporting native RT as not
  evaluated. The next isolated macOS checkpoint creates a hidden native Metal
  window, renders a manual PBR triangle through HLMS PBS and Compositor2,
  performs UI-free GPU readback, independently validates the raster pixels,
  and records clean renderer shutdown. The following Apple N2 checkpoint adds
  a shared-event ownership boundary, immutable deformation revisions, exact
  pooled-buffer bounds/generations, same-device BLAS/TLAS/query evidence, and
  explicit shutdown plus both frontend/backend destructor orders. The N3
  checkpoint then adds the versioned color-image handoff, exact retained Ogre
  HDR target, GPU-only contribution/composite path, three independently hashed
  image artifacts, view change, resize, and post-submission fault seams. The N4
  checkpoint adds a distinct receiver and occluder, two native BLAS, a
  two-instance TLAS, full-view primary/secondary ray lineage, canonical R16
  hard-shadow visibility, and an exact RGBA16 GPU composite on Ogre's Metal
  device and queue. Operational
  Metal faults revoke leases and backend registration while latching the
  frontend unusable, so device loss cannot permanently block cleanup. Windows
  D3D11 and Linux Vulkan
  reproduction remain open; Linux is deliberately an offscreen null-window
  raster gate rather than a presentation-window claim. Soft/area-light
  shadows, reflection semantics, material attributes,
  presentation, image quality, performance, DXR, and Vulkan KHR interop remain
  open; see the
  [isolated integration checkpoint](OGRE_NEXT_INTEGRATION.md).
- The RT4/V1 raster frontend now also has an explicit, default-off directional
  `PSSM_3_CASCADE_V1` checkpoint. It programmatically creates one fixed
  three-cascade `D32_FLOAT` Compositor2 atlas, honors the renderer-neutral
  static/dynamic light masks and per-mesh cast/receive flags, reads back its
  native topology/splits/filter/runtime bias/lifecycle, preserves perspective
  tangents while Ogre mutates the camera near/far planes, and proves isolated
  receiver shadows in all three cascades (HDR and SDR for the near cascade).
  Frame-local datablock and generated-workspace-node fault seams prove clean
  same-frame retry. A separate off-center-lens and zero-thickness receiver-AABB
  fixture binds the native tangent and caster-bound path to UI-free pixels. A
  transactional D32 allocation/residency/readback/cleanup probe replaces
  generic format support as the backend capability gate. The 86 upstream
  Ogre/HLMS source files used by the feature
  have their own exact
  source lock and build-time verifier, including Mesh/Item/SceneManager/AABB and
  the Metal, Vulkan, and D3D11 allocation/readback owners. A fresh child-process
  challenge is atomically bound through an execution receipt, exact attestation,
  artifact manifest, and (on trusted CI runs) GitHub OIDC/DSSE provenance. The
  smoke passed locally on Apple Metal;
  Linux Vulkan and Windows D3D11 run the same fail-closed test in CI and may
  report only a real pass or explicit unsupported capability evidence. This
  does not close local-light shadows, CityWorld quality, ray-traced shadows,
  presentation, or cross-platform native runtime parity.
- The next native-shadow increment now has one platform-neutral
  `NATIVE_DIRECTIONAL_HARD_SHADOW_V1` admission and sample oracle shared by
  Metal, Vulkan KHR, and DXR. It requires RT4, two distinct receiver/occluder
  BLAS, a two-instance TLAS, exact primary/secondary ray lineage, UI-free input,
  exact R16 visibility, and byte-exact RGBA16 composition. N4 and PSSM are
  mutually exclusive within one initialized frontend; incomplete native
  capability selects the already validated PSSM fallback before initialization,
  never midway through a frame. The Metal N4 implementation now passes on a
  physical Apple M5 with 5,712 visible receiver pixels, 432 pixels blocked by
  the distinct occluder, zero primary misses, exact R16 visibility, exact R32
  lineage, and byte-validated RGBA16 composition. CI compiles and executes the
  target on macOS, accepts only an explicit capability skip on unsupported
  hosted hardware, independently revalidates every retained byte, and runs the
  portable source contract on Linux and Windows. Vulkan/DXR implementations,
  soft/area-light shadows, resize/fault soak, temporal stability, and
  image/performance gates remain open.
- The renderer-neutral scene boundary now has the prerequisite lighting slice:
  snapshot version 4 retains the sorted stable directional/point/spot identities
  introduced by version 3 and adds an ordered absolute-world reflection-probe set,
  current/previous transforms, lux/candela photometry, exact local attenuation
  and cones, static/dynamic shadow masks, ambient/texture/analytic-sky radiance,
  sun linkage, bounded EV compensation, and a canonical portable digest. The
  version-three joined-scene producer owns light/probe lineage, render-origin rebasing,
  permanent type/tombstone lineage, and release/acquire atomic publication.
  Dependency-free strict C++ tests include concurrent readers, and the existing
  test graph compiles them with GCC/Clang/MSVC on Linux, macOS, and Windows.
  This closes the data/transaction milestone only: broader Ogre-Next PBS
  calibration, local-light and content-scale shadow rendering, atmospheric
  scattering, RT light/material export, GI, denoising, shipping source
  adapters, image quality, and performance remain open.
- macOS first renders a measured RT contribution in a real UI-free RoR frame
  on Apple family 9 or newer. M1/M2 and unsupported OS versions retain the
  complete Ogre-Next raster fallback.
- Linux proves a shared application-owned Vulkan device with the required KHR
  extension/feature chain.
- Windows proves D3D12/D3D11On12/Ogre-Next lifecycle and DXR tier 1.1. Failure
  is an architecture no-go that requires a D3D12 renderer or different renderer
  choice; silently substituting Vulkan RT does not satisfy the DXR endpoint.
- Renderer choice cannot change fixed-input deterministic physics traces, and
  the migration never reads or mutates live solver state from an RT backend.
- No “native RT” or “UE5-quality” claim is published before the platform
  runtime, image, performance, fallback, and lifecycle gates in the
  [decision RFC](NATIVE_RAY_TRACING_BACKEND.md) pass.

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

The first runtime slice is V0A only. Its archived CVar defaults off; mode 1
requests the locked LDR color curve plus five-tap FXAA on GL3Plus/GLSL 330 or
D3D11/Shader Model 4. The fail-closed lifecycle owns only the main viewport,
attaches after Terrain water/weather setup, stays last in the scene compositor
chain, leaves native-resolution overlays outside the pass, recreates on real
backing-extent changes, and detaches before scene/render teardown. This is a
testable seam, not the V0 milestone: native three-platform image/performance
gates, default-off pixel identity, bloom, and the other V0 acceptance items
above remain open.

V1 follows R0 and introduces a linear HDR pipeline and a documented PBR material
schema: albedo, normal, packed occlusion/roughness/metalness, emissive, alpha
mode, and legacy fallback. Generate or import tangents rather than pretending
legacy cab meshes already contain them. Convert one DAF material and one terrain
layer before bulk conversion.

The opt-in Ogre-Next `MODERN_PBR_RT4_V1` checkpoint now implements a measured
subset of that path: authored tangent/UV0 geometry, sRGB base-color/emissive
uploads, packed linear roughness/metallic extraction, padded multi-mip rows,
portable samplers, canonical positive-Z tangent-space normal maps at exactly
unit scale, fail-closed non-uniform object scale, one calibrated directional light, transactional replacement and
exact native texture retirement, default-off three-cascade directional PSSM,
HDR/SDR evidence, simultaneous Metal N3 interop, and the Metal N4 native
directional hard-shadow slice. The normal contract
validates every linear RGBA8 texel/mip against the
pinned positive-Z reconstruction within exactly `1/255`, requires alpha 255,
derives `RG8_UNORM`, and binds `PBSM_NORMAL` with UV0 and the authored sampler.
The checkpoint also verifies every derived RG byte in Ogre's row-pitched
`Image2` staging memory and proves authored tangent `w` handedness with
controlled native HDR/SDR captures.
It has passed locally on the recorded Apple M5. This is progress toward V1, not
completion: ambient occlusion, the full lighting inventory, local-light and
content-scale shadow gates,
exposure/tone mapping, reflections/GI, native Windows/Linux runtime evidence,
content conversion, and image/performance gates remain open. The occlusion gap
is explicit: the pinned PBS interface has no ambient-only occlusion slot, and
detail-weight or direct-light multiplication are not accepted substitutes.

The renderer-neutral V1 numerical oracle evaluates the selected analytic
equations from the pinned Ogre-Next `PbsBrdf::Default` full32 source profile in
strict binary64 arithmetic. A feature-specific manifest binds its exact shader
and datablock source hashes to the canonical Ogre-Next dependency-lock commit.
The oracle locks the metallic workflow, squared perceptual roughness and alpha
floor, GGX distribution, height-correlated Smith visibility, Schlick Fresnel,
and normalized Disney diffuse. Golden samples, an independent source-equation
evaluator at normal/grazing/tangent/back-hemisphere boundaries, comprehensive
malformed inputs, and 20,000 fixed-seed reciprocity, nonnegativity, and rotation
fixtures run under strict AppleClang, GCC 14, Release, and ASan/UBSan builds.

This idealized CPU equation reference is not bit-exact GPU shader parity. The
pinned float path uses `0.318309886f` for datablock diffuse upload and
`3.14159f` for metallic color reconstruction, while the oracle retains the
intended equations in binary64. Each Metal, Vulkan, and D3D capture must still
demonstrate the declared V1 1% relative gate, plus fixture-specific absolute
tolerances near zero. Because this Default profile omits diffuse Fresnel,
"normalized Disney diffuse" does not claim that the combined BRDF always
integrates to at most one.

The portable HDR numerical reference now has separate ideal-binary64 and
shader-binary32 behaviors. The shader behavior deterministically models the
RGBA16/R16 storage boundary and multi-frame exposure feedback; it names Ogre's
historical bloom transfer gamma-2 rather than sRGB. An HDR-specific source lock
is bound to the canonical Ogre-Next lock and hashes the complete selected
utility/material/shader/compositor closure. Backend captures use the documented
storage-normalized tone-map comparison, conditioning-aware exposure bound, and
exact R16 policy in [the HDR reference contract](HDR_REFERENCE.md).
Display transfer/gamut policy and image/performance acceptance remain open.
The deterministic temporal handoff now derives Ogre exposure and adaptation
delta from the immutable view/scene exposure and simulation timestamp, and
advances its persistent R16 history only after an exact native-oracle readback.
Native compositor media, spatial bloom, output transfer, and backend images are
still required before this is a shipping HDR path.

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

The first reflection-fallback reference now pins Ogre-Next's box-projected
cubemap shader and probe-buffer sources. Its portable oracle fixes strict box
membership, manual and automatic probe weights, ray-box correction, and the
left-handed sampling-vector convention before Metal/HLSL/GLSL backend captures
are compared. The first renderer-neutral runtime layer now validates rigid
oriented correction/influence volumes and full-shape capture planes, schedules
static invalidations or periodic updates by simulation tick under a stable
priority budget, and publishes only atomic six-face/all-filtered-mip generations with
revision lineage, retry/abort semantics, deterministic seeds, and permanent ID
tombstones. Binary64 world positions are converted transactionally against each
frame's render origin, so large-world rebasing is distinct from authored probe
changes. Joined-scene producer wiring, the portable scheduler, canonical
readback measurement, opaque plan/request receipts, and the native Ogre-Next
PCC adapter are implemented. The adapter renders isolated `RGBA16_FLOAT`
cubemap faces, filters every reviewed mip, issues the concrete receipt, and
publishes the native generation only at the final fallible frame boundary.
Aborting the first generation now proves balanced PCC create/destroy ownership,
an unbound PBS pointer, and unchanged public lineage. The source-locked contract
also pins the cleanup order and Ogre's public zero reset used to recompute the
automatic-IBL mip state; the hidden mip value is not claimed as runtime
readback. Three-backend image-quality acceptance, authoring, performance, and
production scene coverage remain open.
The portable contract mirrors Ogre-Next PCC's owned filtered
IBL output (`max(full_chain_mips, 5) - 4`) at reviewed 32..2048 resolutions,
including 32 => 2 mips and 256 => 5; it does not claim the source cubemap's
full raw chain. Portable measurements bind the complete schedule request,
exact per-mip dimensions, and active RGBA16F bytes for every face/mip while rejecting padding
dependence and partial data. Commit rejects stale lineage,
cross-plan replay, and reset-era transaction ABA without treating a caller's
integer or digest as evidence of backend execution.

## V2 — AirSim-referenced visual fidelity and scene import

AirSim is a simulator plugin whose visual quality depends on its Unreal/Unity
host environment, not one fixed map or renderer preset. Pin AirSim v1.8.1 and
compare both engines on one rights-cleared shared source scene against the same
offline path-traced reference. Official AirSim environments are
capture-only references: the v1.8.1 release notes state that the high-detail
downloads use proprietary assets whose source projects cannot be distributed.
Unreal `.uasset`, `.umap`, cooked packages, Marketplace content, and engine
template assets are therefore not RoR import inputs without separate verified
rights.

The RoR path is an offline, fail-closed glTF 2.0 scene compiler with bounded
parsing, deterministic conversion, PBR materials, tangents, LODs, instancing,
collision proxies, terrain tiles, texture transcodes, provenance, and canonical
output hashes. The runtime consumes only compiled packages and never imported
scripts or shaders.

Gate V2:

- On the shared camera/weather/material suite, no diagnostic stratum is more
  than 2% worse than the pinned AirSim result relative to offline truth.
- Aggregate perceptual error beats AirSim by at least 5% with a bootstrap 95%
  confidence interval, while HDR, edge, shadow, reflection, transparency,
  vegetation, LOD, and temporal-stability absolute gates all pass.
- The macOS arm64 high preset sustains 60 FPS at 1920x1080 on the declared
  reference Apple Silicon machine, with p95 frame time at or below 18.3 ms and
  bounded resources through a ten-minute camera loop.
- Missing geometry/materials, blank/stale/corrupt/non-finite frames, UI
  contamination, invalid rights, unsupported glTF features, or conversion
  nondeterminism fail closed.

The complete fixture, capture, metric, provenance, and platform contract is in
[the AirSim visual-fidelity specification](AIRSIM_VISUAL_FIDELITY.md).

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
raw input. Variables and `$=` expressions remain inert in this syntax AST and
in the part resolver.

The independent J1 expression core now evaluates a strict documented scalar
subset with typed variables and flattened scalar `$components` paths. It
implements finite decimal arithmetic, comparisons, Lua-style
`and`/`or`/`not` and their short-circuit ternary idiom, Boolean
three-argument `case`, string concatenation/length, numeric `abs`, `square`,
`clamp`, and bounded variadic `min`/`max`, explicit precedence, and
source-independent canonical scalar results. Function arguments are eager and
numeric-only; `min`/`max` reduce without argument-proportional storage and have
a hard 64-argument ceiling. Input, token, recursion, function-argument, work,
string, output, and environment quotas are deterministic; non-finite values
fail closed under strict and fast-math builds. It does not execute Lua or expose
host functions. Numeric-selector `case`, every function outside Boolean `case`
and the five-name numeric allowlist, table-valued components, and
numeric-to-string concatenation remain unsupported. This is an independent
implementation of public format behavior and does not reuse BeamNG code or
assets.

`ParseJBeam` and `JBeamPartResolver` deliberately retain expressions as inert,
duplicate-preserving source data. The J2 structural semantic pass now invokes
the core only at its explicit scalar-field readers. It builds each part's
environment from effective configuration/slot variables plus deterministically
merged scalar component leaves, resolves standalone variables, `$=`
expressions, and `$.name` namespace strings, then enforces the destination
field's number/Boolean/string policy. Missing variables remain `nil` for
documented existence checks; a final `nil` in a typed structural field fails
closed. Expression diagnostics retain the field source span and decoded byte
offset.

Table-valued and expression-valued components remain preserved-but-disabled,
and no unknown field or section is evaluated. Per-expression quotas are backed
by aggregate evaluation/work, component node/depth, environment, and retained
memory gates. Clean-room end-to-end tests cover configuration and slot
variables, namespace expansion, scalar components, missing-variable
short-circuiting, forbidden host calls, quotas, and representative
FormulaCOUPE v0.9.7 arithmetic shapes such as node mass scaling and
beam-precompression tuning. A local scan found no scalar built-in calls in the
fixture's 84 `.jbeam` files, so `FC-A7-01` is not blocked by the remaining
function set. Full table components, authored tuning-variable default tables,
the remaining documented math functions, and semantic evaluation for
non-structural sections remain open J1/J2 work.

The bounded resolver indexes parts independently of archive enumeration order,
selects the sole `main` root or requires an explicit root when a package exposes
several vehicles, handles legacy `slots`, allow/deny `slots2`, core and optional
defaults, explicit empty selections, `.pc` part selections, and typed scalar
variable inheritance. It rejects duplicate definitions, ambiguous parts,
cycles, invalid slot tables, and resource-limit overflow with canonical source
diagnostics. Canonical index and resolved-graph identities retain the full
duplicate assignment/body history and source spans. Per-`vehicles/<id>` part
namespaces beyond the structural field gate, field-specific semantic validation
beyond the structural subset, coordinate lowering into `RigDef`, and the
complete vehicle IR remain open J1/J2 work.

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

The advanced-structure inventory now covers the documented `hydros`, legacy
`rails`, `rails2`, `slidenodes`, `thrusters`, and `torsionbars` sections under
the locked `beamng-docs-0.38.5.0-2026-07-27` profile. It retains exact source
ASTs, inherited and row-local field origins, unknown fields, source spans, and
official defaults; validates node/rail references and nondegenerate static
geometry; and gives every accepted row an explicit static-ready,
inventory-only, or inert-expression classification. Duplicate sections and
rail names, malformed tables, non-finite values, unresolved references, cyclic
resolved graphs, and quota overflow fail closed. Canonical identity is stable
across package enumeration order and container capacity, with strict,
fast-math, and sanitizer fixtures. No actuator, rail constraint, thrust, or
torsion force is enabled by this inventory pass; runtime lowering and
behavioral conformance remain open.

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

The first J4 material-inventory gate is implemented. It transactionally
enumerates every manifest `*.materials.json` source in canonical path order,
retains duplicate object fields, numeric spelling, unknown fields, arrays, and
source spans, and gives every package material a collision-free scoped identity.
It recognizes PBR inputs at both the root and within legacy `Stages`, validates
all texture references against the bounded package manifest, and distinguishes
exact local files, documented package-local cooked DDS substitutes, dynamic
textures, missing files, case mismatches, and invalid paths. It performs no
filesystem access, archive extraction, script execution, shader creation, or
runtime material activation. Strict, fast-math, and sanitizer fixtures cover
truncation, embedded NULs, traversal, collisions, quotas, and canonical identity.
The opt-in FormulaCOUPE v0.9.7 fixture records 70 materials, 280 stages, and 335
texture references: 281 cooked DDS, 7 dynamic, 19 missing/external, and 28
case-mismatched. Those last 47 remain visible placeholders until an explicit
policy or corrected local asset resolves them; the inventory does not guess.

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
10. The project-owned CityWorld tangent and 15-degree curved spans are compiled
    through the fail-closed glTF boundary; tangent in-game lane/collision
    continuity and a connector-solved three-curve traversal are proven on
    macOS arm64. The first intercity construction alignment now starts inside
    Penguinville's authenticated carriageway, clears its curb with a continuous
    collision apron, and closes the NeoQueretaro seam with ramps, collision and
    terrain-reaching supports. Next replace its generic visuals with the Blender
    abutment/transition/deck/pier kit and pass the full-corridor three-platform
    drive gate before starting the longer corridor.

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
