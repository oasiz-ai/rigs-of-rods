# Dependency-free core tests

These tests exercise dependency-free simulation and content-validation kernels
without requiring OGRE, MyGUI, a game window, or the content pack.

Run the fast local physics subset with any C++11 compiler:

```sh
tools/run-physics-tests.sh
```

Or use the standalone CTest project:

```sh
cmake -S tests -B build-physics-tests \
    -DCMAKE_BUILD_TYPE=Release \
    -DROR_TEST_GAME_FAST_MATH=ON
cmake --build build-physics-tests --config Release
cmake -E chdir build-physics-tests ctest -C Release --output-on-failure
```

The full game build can include the same tests with `-DROR_BUILD_TESTS=ON`.
`ROR_TEST_GAME_FAST_MATH` mirrors `/fp:fast` on MSVC and `-ffast-math` on
GCC/Clang; omit it for a precise floating-point comparison run.

## BeamNG package manifest boundary

The package-manifest test uses only synthetic metadata. It verifies deterministic
archive ordering and canonical reports plus fail-closed rejection of path
traversal, ambiguous portable filenames, duplicates, normalization/case
collisions, symlinks, encryption, and size/compression bombs. It also verifies
that the data-only compatibility schema records a declared format profile,
native/approximated/disabled/unsupported/rejected feature states and source
diagnostics. The primitive neither extracts files nor executes imported Lua.

## JBeam syntax boundary

The relaxed-JBeam front end accepts comments, optional and trailing commas,
strict finite JSON scalars, and UTF-8 strings within explicit source, token,
node, nesting, string, and diagnostic budgets. The duplicate-preserving AST
retains exact source spans and source-order object fields so later resolution
can apply documented last-assignment behavior without erasing provenance.

The table normalizer preserves raw rows, inherited default history, duplicate
headers, positional cells, trailing row dictionaries, and stable
duplicate-aware paths. Variable references and `$=` expressions remain inert
data.

The resolver deterministically indexes duplicate-preserving part bodies,
selects one `main` root or requires an explicit root for a multi-vehicle
package, accepts legacy `slots` and allow/deny `slots2` tables, reads `.pc` part
selections and scalar variables, and resolves a bounded recursive part graph.
Missing required and optional parts, duplicate definitions, disallowed slot
types, cycles, unused selections, and every configured resource limit receive
stable diagnostics. Configuration and slot variables remain inert typed
assignments; no `$=` expression or packaged Lua is executed. The canonical
index and graph identities retain source spans and duplicate assignment history
so different source inputs cannot silently collapse to one cache key. Semantic
physics lowering remains a separate J2 gate.

## JBeam expression boundary

`JBeamExpressionEvaluator` is an independent C++11 scalar-expression core. It
requires the documented `$=` prefix and supports finite decimal arithmetic,
comparisons, Lua-style `and`/`or`/`not` with short-circuit operand-returning
semantics, Boolean and bounded positive-integer-selector `case`, string
concatenation/byte length,
numeric `abs`, `square`, `round`, `floor`, `ceil`, clamped
`smoothstep`/`smootherstep`/`smootheststep`, `clamp`, and bounded variadic
`min`/`max`, typed variables, and flattened scalar `$components` paths. Missing
variables evaluate to `nil`, duplicate environment assignments use last-write
semantics, and
canonical results normalize the sign of zero and length-prefix strings.

The clean-room suite covers precedence, variables, eager `case` versus
short-circuit logic, eager numeric function arguments, nesting, arity and type
failures, invalid clamp bounds, square overflow, malformed and unsupported
syntax, hostile binary input, every quota, UTF-8, deterministic repeats, and
canonical collisions. It runs in strict, fast-math, and sanitizer builds.
Expression source size, token count, recursion depth, function arguments,
deterministic work, literal/output strings, variable count/name size, and
environment strings are all bounded. Function calls never allocate
argument-proportional storage: `min` and `max` reduce at parse time and accept
one through 64 numeric arguments.

This evaluator does not execute Lua and has no host, file, network, clock, or
random access. It rejects every function outside the bounded Boolean/integer
`case` signatures and eleven-name deterministic numeric allowlist,
numeric-to-string concatenation, table values, indexing, and method calls.
`ParseJBeam` and `JBeamPartResolver` still preserve expression strings as inert
data. The structural semantic pass now constructs the scalar environment from
resolved configuration/slot variables and merged scalar component leaves, then
applies standalone variables, `$=` expressions, and `$.name` namespace
expansion only at explicitly supported typed field readers. Unsupported fields,
sections, component tables, and expression-valued components remain inert and
preserved; there is no blanket AST evaluation.

## JBeam coordinate boundary

The J2 coordinate kernel locks the documented BeamNG vehicle frame
`+X left, +Y backward, +Z up` to RoR's vehicle frame
`+X backward, +Y up, +Z left` with the exact cyclic permutation
`(x_ror, y_ror, z_ror) = (y_beamng, z_beamng, x_beamng)`. Its inverse is
explicit, and point and vector APIs are distinct so later part translations
cannot leak into force or direction conversion.

The transform has determinant `+1`, so it preserves handedness, distances, dot
and cross products, and triangle winding. Basis, refnode landmarks, triangle
normals, in-place round trips, null outputs, and NaN/infinity rejection are
tested under strict C++11, the game's fast-math mode, and sanitizers. Runtime
lowering still must route nodes, forces, rotations, inertia, meshes, and cameras
through this one boundary before imported actors may spawn.

## JBeam structural IR

The dependency-free J2 semantic pass consumes a valid resolved part graph and
emits a bounded structural IR in authored BeamNG SI coordinates. It resolves
positive node masses, normal-beam properties, deterministic quad triangles, and
one refnode frame while preserving unsupported fields and special beams with
source-spanned diagnostics. Required references, global node IDs, table
ambiguity, non-finite numbers, degenerate geometry, resource quotas, and the
documented `+Y` back / `+X` left / `+Z` up refnode alignment fail closed.

Pipeline tests pass source through `ParseJBeam`, `.pc` parsing, package indexing,
part/slot resolution, and structural lowering. They cover standalone and
computed variables, missing-variable `nil` branches, scalar component paths,
Boolean results, BeamNG 0.38 prefix/suffix namespace strings, forbidden host
calls, aggregate quotas, and FormulaCOUPE-shaped node-mass and
beam-precompression expressions. Component arrays are retained with warnings,
and evaluator errors remain attached to their structural field and source.

The exact `"FLT_MAX"` sentinel is represented as an explicit unbounded
deformation/strength flag; arbitrary strings remain invalid. Canonical output is
tested against package enumeration changes. Strict, fast-math, and sanitizer
tests run before the future `JBeamToRigDef` adapter may enable spawning.

## Calibrated beam material

The version-1 dependency-free material kernel uses SI stress/strain units and a
closed-form one-dimensional return map with isotropic hardening, accumulated
plastic strain, monotonic ductile damage, and a finite post-onset damage-driver
capacity. The capacity is not presented as total dissipated fracture energy.
For the monotonic post-onset nominal stress/strain-area convention implemented
by this law, `C = 2 (G_f / l_char) / (1 + H/E)`; total-dissipation and cyclic
calibrations differ. Mapping physical `G_f` through a characteristic length
and controlling localization belong to the future beam adapter and
mesh-refinement gates. Invalid parameters, state, strain, or numeric overflow
fail closed with zero force response and the previous history state intact.

Analytical elastic/yield fixtures, a versioned cyclic-load regression, exact
energy balance, reversal, finite-difference tangent, fracture monotonicity,
one-through-1,000-step subdivision checks, and fixed-seed property histories
cover the isolated law. The separate SI beam-adapter fixture covers explicit
programmatic opt-in, `E A / L` stiffness, tension/compression force signs,
viscous-force composition, plastic residual strain, reset semantics, fracture
disconnect, atomic configuration, latched malformed-state failures, runtime
float-range rejection, and 50,000 fixed-seed equal-and-opposite force pairs
under strict and fast-math builds.

The adapter is wired into `Actor` and the native truck parser through
`set_calibrated_beam_material 1, on, ...` with an explicit `1, off`
transition. The dependency-light directive tests cover strict finite
locale-independent parsing, invalid-input atomicity, exact serialization
round trips, the parser's production copy-on-write transition, serializer
normal/specialized/normal role transitions, field-specific diagnostics, and the
inert legacy default in Release, fast-math, and sanitizer builds. It also
exercises the exact atomic, role-aware spawn preparation called by
`ActorSpawner`.

When the full application is built with `ROR_BUILD_TESTS=ON`, the
`rigdef_calibrated_beam_material_roundtrip_integration` CTest invokes a hidden
pre-renderer test hook in the real `RoR` executable. It loads the authored
`calibrated_beam_material_roundtrip.truck` fixture through
`RigDef::Parser::ProcessRawLine()` and `SequentialImporter`, proves that an
enabled rope is rejected while an explicit-off rope remains legacy, prepares
the supported beams through the production spawn seam, serializes the complete
document with `RigDef::Serializer`, checks the fail-closed on/off/on/off
transitions, reparses the output, and requires bit-exact binary64 material
values. The hook is excluded from normal builds and returns before renderer,
audio, input, or GUI startup.

Version 1 is limited to normal `NOSHOCK` entries in `beams`; specialized beam
roles remain disabled. There is intentionally no JBeam mapping or shipped
material configuration yet. Savegame/replay restoration, authored calibration
data, mesh-refinement/localization gates, step-size comparison, starter-content
calibration, and the Agora impact regression remain separate P1 gates.

## BeamNG ZIP package index

`BeamNGZipArchiveIndex` implements the metadata-only J0 container boundary for
classic single-disk ZIPs. It validates central/local headers, descriptors,
record coverage, filesystem types, and the existing canonical package manifest
without extraction or decompression. Dedicated errors distinguish unsupported
ZIP64, spanning, encryption, alternate names/streams, SFX prefixes, symlinks,
special files, overlaps, and hidden data from malformed records.

The dependency-free suite checks every prefix truncation and 5,000 fixed-seed
mutations under strict, fast-math, and address/undefined-sanitizer builds. With
`ROR_BUILD_TESTS=ON`, inspect an explicitly supplied local package using:

```sh
ror_beamng_zip_index PACKAGE.zip
```

The command emits a compact JSON result and exits `0` for a valid bounded
manifest, `1` for a rejected archive, and `2` for usage, I/O, or resource
failure. It records compression methods but does not claim decoder support.
The `beamng_formulacoupe_opt_in` CTest reads only
`ROR_BEAMNG_FORMULACOUPE_ZIP`; it skips when unset and otherwise requires the
pinned v0.9.7 SHA-256 plus the audited J0 byte, entry, expanded-size, and
39-configuration counts.

## Bounded hydro actuator response

The dependency-free hydro kernel implements the documented length-factor
examples and a versioned interpretation of input center, input locks, in/out
length limits, input scaling, and contraction/extension/auto-center rates.
Targets and state are positive length ratios relative to the initial beam
length. Factor mode takes precedence over separate travel limits and input
scaling; malformed or non-finite parameters, input, state, timestep, or rest
length fail closed before reaching the beam solver.

Golden examples cover doubling, halving, reversed factors, asymmetric travel,
clamping, centering, and rate-limited motion. A 50,000-case fixed-seed property
test proves finite positive output, monotonic progress toward the target, and
the per-step rate bound. Structural JBeam parsing, native input wiring, beam
force integration, replay state, and source-engine behavioral calibration
remain separate J2/J3 gates.

## Deterministic counter noise

Physics noise is a pure function of a persisted actor seed, an effect-specific
integer step, a domain salt, a stable element index, and a component lane. It
does not contain a shared or advancing random-number state. Turbulent drag uses
the actor's completed fixed-physics-step count plus node and XYZ lane; engine
anti-lag uses a separate engine fixed-step count plus turbo index.

Full actor resets restore both counters to zero while preserving the actor seed.
Savegames persist the resolved seed and both next counters. Golden integer and
exact float-bit vectors define the sampling ABI. The threaded regression
generates a dependency-free canonical sample array with one, two, and eight
threads, reversed traversal, and omitted actors. It does not replace the pending
pinned-content ActorManager worker-count run, save/load continuation test, or
full-game ThreadSanitizer soak.

Legacy actor spawns continue to derive their seed from the fixed default world
seed and runtime actor ID. Deterministic scenario producers can instead call
`spawnTruckDeterministic()` with a nonzero scenario seed and nonzero stable
actor stream ID. That pair derives the same actor seed regardless of runtime
actor ID or spawn order. A partial pair and a duplicate live pair fail before
actor allocation. Version-3 savegames retain the pair in an optional schema-1
object and require its stored resolved seed to rederive exactly; old version-3
saves remain compatible.

With `sim_deterministic_sleeping_engine` enabled (the default), sleeping engines
advance the same counter once per 2 kHz physics step and integrate on exact
32-step boundaries, or 62.5 Hz. A dependency-free cadence contract proves that
the tick sequence survives arbitrary render-frame grouping, pauses, and
save/restore continuation; malformed serialized phases fail closed. The legacy
once-per-render-frame path remains available by disabling the CVar. Full
engine-state replay and pinned-content worker-count runs remain separate gates.

## Deterministic contact ordering

Inter-actor point-versus-triangle contacts are discovered into one task-local
buffer per surface actor. The buffers are merged by a stable
surface-actor/contact/hit-actor/node key before collision forces are applied on
the simulation thread. KD-tree point hits are likewise canonicalized by
actor/node key, so tree partitioning does not leak into the solver order.

The parallel path has a hard global buffered-contact cap of 65,536, split across
deterministic per-actor quotas. If any quota overflows—or bounded task-buffer
allocation fails—the partial buffers are not consumed. Scheduled surface
contacts are instead re-discovered and applied serially in the same key order
with constant contact-storage use. No contact is silently dropped, and
collision-rate state is not advanced twice during the replayed discovery.
Buffers grow lazily, retain storage across physics substeps and active-actor
count changes, and catch worker-side allocation failure for the same serial
fallback. Actors whose rate schedule has no active contact surface do not
receive an active quota. The session fallback counter logs its reason at powers
of two so sustained pathological contact sets are visible without writing a
message every physics step.

The dependency-free regression compares 10,000 fixed-seed randomized AABB
queries against a brute-force ordered oracle and verifies bit-identical force
accumulation when the same contacts are partitioned into one, two, and eight
task buffers. It also locks quota remainder allocation, hard overflow, partial
buffer rejection, and canonical fallback order.

## Deterministic state traces

`DeterministicStateTrace` wraps each fixed-step state digest in a bounded,
versioned binary stream. Its checked header records scenario identity, worker
count, the exact rational physics step, and floating-point mode. Every record
contains one contiguous physics-step number, actor/contact counts, its 32-byte
state digest, and either the exact authenticated deterministic-input prefix
digest accepted for that step or an explicit no-input marker. Schema 2 keeps
the input and resulting-state evidence in the same checksummed transaction. A
mandatory aggregate trailer distinguishes a deliberately short run from
truncation.

The reader rejects malformed schemas, nonzero reserved fields, discontinuous
steps, count/size overflow, local or aggregate checksum failures, inconsistent
summaries, and trailing bytes. Exhaustive fixtures truncate the stream at every
byte and flip every individual bit. The comparator validates both complete
inputs, distinguishes input-prefix divergence from resulting-state divergence,
reports the first divergent step, and only permits worker-count metadata to
differ when explicitly requested for the D0 one-versus-eight-worker gate.

With `ROR_BUILD_TESTS=ON`, compare completed artifacts using:

```sh
ror_state_trace [--allow-worker-count-difference] LEFT.trace RIGHT.trace
```

The command emits canonical JSON and exits `0` for a match, `1` for a valid
divergence, and `2` for invalid input.

Live recording is opt-in through `sim_deterministic_state_trace`; set the
unsigned-decimal `sim_deterministic_state_trace_scenario_id` before enabling
it. The actor manager writes a uniquely named `.rortrace` under
`sys_logs_dir`, after each completed 2 kHz step and after ordered contact
resolution and free-force application. Turning the CVar off or unloading the
scene finalizes the aggregate trailer. Capture failure is logged and latched
off until the CVar is disabled, while physics continues normally.

The bounded production input record/replay path is configured only after the
single local player truck exists. Set synchronous physics and the immutable run
identity first:

```text
app_async_physics=false
sim_deterministic_input_scenario_id=<nonzero unsigned decimal>
sim_deterministic_input_target_id=<stable nonzero unsigned decimal>
sim_deterministic_input_step_limit=<positive bounded step count>
```

Set `sim_deterministic_input_mode=record` with an empty
`sim_deterministic_input_path` to reserve a new authenticated `.rorinput` under
`sys_logs_dir`. For a fresh-process replay, use the same terrain, vehicle,
manual-sequential gearbox configuration, fixed gear/range, hydro configuration,
scenario, target, and limit; set `sim_deterministic_input_path` to the completed
recording and then set `sim_deterministic_input_mode=replay`. Replay validates
the complete stream and source digest before applying controls, owns the normal
player-input path while active, and pauses physics after clean exhaustion or a
fault. Pair each run with the state-trace CVars above and compare the two
`.rortrace` files using `ror_state_trace` for an end-to-end state-equivalence
gate.

Schema 1 intentionally admits only one local simulated manual-sequential truck
with fixed gear/range, no transfer case, no actor links or AI, and disabled ABS,
traction control, cruise control, and speed limiter. It does not claim automatic
gearbox/controller intent, multi-actor atomic input, or savegame continuation.
Runtime TSan coverage and those broader policies remain separate gates.

## Terrain resource bundle dependencies

`ror_terrain_bundle_dependency_tests` locks the dependency validator's exact
ZIP/member/SHA-256 syntax, portable root-name policy, rejection of unhashed
dependencies, duplicate handling, and bounded count and byte quotas without
requiring OGRE. `ror_terrain_bundle_archive_verifier_tests` exercises streaming
OpenSSL SHA-256 verification for matching, mismatched, malformed, and missing
archives. In a full RoR build, `ror_terrain_bundle_config_syntax_tests` loads a
real in-memory descriptor through `Ogre::ConfigFile` with the production
separators and proves that the fully authenticated colon-qualified values
survive parsing intact.

## CityWorld intercity overlay gates

`tests/tools/test_audit_ogre14_material_scripts.py` exercises the bounded,
read-only legacy material inventory under normal and optimized Python. It locks
archive path safety, hash admission, definition/directive caps, duplicate-name
reporting, exact authored texture aliases, deterministic output, and the rule
that the audit never invents texture roles. The private archive is optional;
synthetic scripts cover the portable gate.

`tests/tools/test_cityworld_neoq_intercity_bridge.py` locks the exact
NeoQueretaro and NeoQ2.0 distributor placements, all six endpoint resources,
and the line-378 `autopistaQr` mesh/ODEF pair. It locks zero overlap at both
city seams, one authoritative collision surface per seam, the decoded 15.1 m
flush merge, and open collision endcaps. The pinned ground-road decode covers
9,599 `calleunsolosentido`/`pavimento` triangles: stations 80 through 760 are
therefore authored as 18 no-pillar pairs, while 56 feasible outboard pairs and
all 168 support AABBs retain the 2.5 m lateral/5 cm vertical truck clearances.
The 33-fixture alternating light schedule is unchanged. Synthetic archives
make hostile and deterministic cases portable; the exact private archive is
additionally authenticated when installed locally.

`tests/tools/test_build_cityworld_local_overlay.py` then proves that overlay v7
packages both intercity routes into one deterministic, local-only ZIP without
copying the private source archive. `tests/tools/test_run_cityworld_corridor_scene.py`
independently validates the complete report, 48 runtime bridge lights, and
byte-identical rebuild contract. These standard-library tests run normally and
under `python -O` on macOS, Linux, and Windows.

The project-owned
`tests/fixtures/cityworld_neoq_bridge_runtime/cityworld_neoq_bridge_runtime.as`
diagnostic supplies six UI-free fixed cameras for native review of both road
joins, the driver-height deck, ramp and mid-span undersides, paired side piers,
and the complete raised alignment. The
companion `cityworld_neoq_bridge_drive_runtime` fixture drives the packaged DAF
over the complete link and ten metres beyond the zero-overlap seam, then spawns
a westbound DAF on the preserved NeoQ2.0 carriageway and drives it back across
the destination seam onto the generated deck. Both directions must keep the
heavy-truck footprint inside the live lane.
`tools/run_cityworld_neoq_bridge_scene.py` turns those fixtures into one
fail-closed native acceptance: it authenticates and independently rebuilds the
private overlay, requires six ordered UI-free 1280x720 RGB captures, drives
through both seams with collisions and self-collisions enabled, checks the
destination live-lane footprint in both directions, and requires the exact
complete SidePiers summary multiset with no skipped supports. Static and drive
runs use separate ephemeral RoR homes, and their logs, images, hashes, and
report are published atomically only after both pass. The private
`CityWorld.zip` and generated local overlay remain external to the fixtures and
retained artifacts.

## Deterministic two-truck runtime scene

`tools/run_deterministic_scene.py` drives the pinned D0 production scene
through a full RoR build. It verifies content commit
`34fefdd126784bf87b068fc283f812525d159dd7` and the byte-exact DAF/simple2
fixture inventory before launching anything. The scene spawns two fixed-ID
DAF trucks while paused at step zero, then advances exactly 1,000 physics steps
in ten-step render batches and writes one bounded state trace.

The runner defaults to 30 fresh-process runs with one worker and 30 with eight
workers. It validates the script/engine completion markers and trace metadata,
self-checks every artifact, and compares all canonical step records with the
first trace while allowing only the declared worker-count metadata difference.
Use a fresh artifact directory:

```sh
python3 tools/run_deterministic_scene.py \
    --executable BUILD/bin/RoR.app/Contents/MacOS/RoR \
    --trace-tool BUILD/bin/ror_state_trace \
    --runtime-content BUILD/bin/RoR.app/Contents/MacOS/content \
    --artifact-dir /tmp/ror-d0-scene-artifacts
```

Each launch receives an isolated RoR home, null audio, disabled online access,
and a fixed 1280x720 renderer configuration with content scale factor 1.
Shadows, sky, and water are disabled because this is a physics gate rather
than a renderer comparison. On macOS the command also disables AppKit state
restoration so a previous crash-recovery dialog cannot block automation.

## Beam axial response invariant

The axial damping kernel limits the effective damping coefficient to:

```text
d_effective <= 1 / (effective_inverse_mass * physics_timestep)
```

This means damping alone can reduce relative axial velocity to zero in one
fixed step, but cannot reverse it and inject kinetic energy. The deterministic
property test checks that invariant across 20,000 fixed-seed combinations of
mass, velocity, damping, mobility, and timestep. A separate 120,000-step kernel
soak repeatedly excites an unequal-mass pair and checks finite state, energy
dissipation, and momentum conservation. It complements, but does not replace,
the pending starter-content full-solver soak.

## Calibrated beam 3-D production kinematics

The calibrated production path accepts raw endpoint positions and velocities
at an out-of-line strict-FP boundary. A scaled binary64 normalization supplies
the material length, axial relative velocity, damping projection, and final
equal-and-opposite force axis. No caller-side subtraction or legacy
`fast_invSqrt` approximation touches an opted-in calibrated beam.

`BeamAxialKinematicsTests` covers exact and hostile geometry, 20,000 fixed-seed
3-D damping properties, 120,000 actual position/velocity integration steps,
exact material handoff and transactional fault latching, and a stateful
675-beam digest that must match across one, two, and eight deterministic
partitions visited in different global orders. Run the full dependency-free
physics suite in both supported
floating-point modes:

```sh
tools/run-physics-tests.sh
ROR_PHYSICS_TEST_FAST_MATH=1 tools/run-physics-tests.sh
```

The kinematics and calibrated production-step translation units always finish
their compile command with `-fno-fast-math -ffp-contract=off -fno-lto` (or
`/fp:strict /GL-` on MSVC), independently of the surrounding legacy Actor
flags. The legacy branch preserves the same arithmetic operations and order
and continues to use the game's optimized floating-point mode; exact
scene-trace comparison remains a pending integration gate.

The accompanying allocation-checking benchmark compiles the same strict
kernel and reports p50/p95/p99 nanoseconds per beam, output digests, compiler
identity and flags, source-manifest SHA-256, failures, and hot-loop allocation
counts for 675- and 10,800-beam fixtures:

```sh
tools/run-physics-benchmarks.sh
```

Publish benchmark evidence only from a clean exact commit. The microbenchmark
does not replace a full Actor/vehicle benchmark, starter-content soak, or the
pending Agora impact and mesh-localization gates.

## Calibrated beam starter-content runtime soak

`tools/run_calibrated_beam_soak.py` exercises the material adapter through the
real `RoR-Combined` Actor solver. It authenticates the pinned content commit and
DAF semi source, creates a deterministic local-only ZIP containing a derived
truck with exactly 15 opted-in ordinary beams, preloads that actor before the
first scene snapshot, and runs 120,000 fixed `0.5 ms` physics steps. The script
audits every rendered frame and fails on non-finite or unreachable material
history, faults, fractures, disabled calibrated beams, step overshoot, missing
execution history, renderer failure, or malformed trace metadata.

Run the one-versus-eight-worker gate against a clean combined build:

```sh
python3 tools/run_calibrated_beam_soak.py \
  --executable BUILD/bin/RoR-Combined \
  --trace-tool BUILD/bin/ror_state_trace \
  --runtime-content BUILD/bin/content \
  --artifact-dir ARTIFACTS \
  --runs 1 --workers 1 8
```

The runner self-compares every trace and then compares each trace to the first
with `--allow-worker-count-difference`; all 120,000 ordered state records must
match. The 2026-08-14 Apple M5 receipt is checked in as
`doc/nextgen/evidence/CALIBRATED_BEAM_RUNTIME_SOAK_M5_2026-08-14.json`. Its
worker-1 and worker-8 traces have SHA-256 values `d0117eaa890718c601835f658369151cafe22a973f2fd396bad1932315d714b7`
and `7e20fee2e956939229140337eba98ebe5cef99d0b174165ee7459e52583ec51e`.
Both report 15 active histories, maximum absolute strain `0.00106908`, and zero
plastic strain or damage. The derived material values are deliberately a
numerical integration fixture, not physically calibrated DAF data or a new
legacy-content default.

## Calibrated Agora full-vehicle impact regression

`tools/run_agora_impact_regression.py` authenticates the pinned Agora L source
and derives one local-only `P1CalibratedAgoraImpact.truck`. Exactly the 675
ordinary authored beams are opted into a numerical calibrated-material
fixture; hydro, shock, wheel, and generated beams retain their existing paths.
While the simulation is paused, the runner applies one transactional
`(0, -12, 0) m/s` velocity to every movable node, then records every exact
`0.5 ms` step for three simulated seconds.

The AngelScript scenario measures mass-weighted center-of-mass acceleration,
mechanical energy including gravity, all 11,325 pair distances between the 151
authored nodes, calibrated fractures, disabled beams, and total broken beams.
One-worker and eight-worker state traces must match for all 6,000 records and
their complete telemetry receipts must be identical:

```sh
python3 tools/run_agora_impact_regression.py \
  --executable BUILD/bin/RoR-Combined \
  --trace-tool BUILD/bin/ror_state_trace \
  --runtime-content BUILD/bin/content \
  --artifact-dir ARTIFACTS \
  --runs 1 --workers 1 8
```

This is a versioned deterministic impact fixture. It does not claim that its
material parameters are measured Agora construction data; physical calibration
and approval of the emitted regression numbers remain separate gates.

## Playable frame-time budget

The playable frame-time budget is the measurement seam behind the CityWorld
visual milestone's and the Ogre-Next combined runtime's declared frame-rate
budgets. `frame_time_budget` compiles the exact production kernel
(`source/main/system/FrameTimeBudget.cpp`) into a standalone strict binary with
`-Wall -Wextra -Werror -pedantic -fno-fast-math -ffp-contract=off`. It proves
the nearest-rank percentile is a conservative bin upper edge, that warm-up
frames are excluded, that a non-finite, non-positive, or beyond-ten-second
interval is rejected rather than clamped, that an active limiter or a mid-run
scene change fails closed, that the requested-frame ceiling arms exactly once,
that `Finalize` is pure, and that a receipt is created exclusively and never
overwritten.

The kernel classifies intervals by IEEE-754 bit pattern rather than by calling
`std::isfinite`, because optimized game builds enable `-ffast-math` globally and
may fold that predicate to a constant true. The game compiles the same
translation unit strictly for the same reason.

`frame_time_budget_runtime_contract` locks the seam the C++ tests cannot see:
the nine non-archived `gfx_frame_budget_*` CVars and their declared 60 Hz
defaults, the single render-loop sample point on the loop's own committed delta
time, the fail-closed startup, finalization before renderer teardown, exit code
75 not colliding with the renderer child's 73/74, and the build graph compiling
the production kernel into both the game and the test binary.

`playable_performance_scene_tool` covers the driver's decision logic against
synthetic receipts: request validation, exact receipt-to-request identity,
refusal of a limiter/VSync/fullscreen run, refusal of incoherent statistics,
the macOS packaged-versus-development directory split, explicit graphics
presets, command-line arming, and the display-pacing detector.

Run the gate against a built executable:

```sh
python3 tools/run_playable_performance_scene.py \
  --executable BUILD/bin/RoR-Ogre14 \
  --artifact-dir ARTIFACTS \
  --scenario-id playable-cityworld-alexis-1080p-high \
  --terrain CityWorld.terrn2 --actor AlexisSaber.truck \
  --graphics-preset high --width 1920 --height 1080 \
  --warmup-frames 120 --minimum-frames 600 --frames 1800
```

The 2026-08-17 Apple M5 result set is checked in as
`doc/nextgen/evidence/PLAYABLE_PERFORMANCE_M5_2026-08-17.json`. It measures the
OGRE 14 compatibility executable, not Ogre-Next, with PSSM shadows verified
active from the runtime's own effective-settings statement. The sustained 60
FPS budget is met on every recorded run (80-86 FPS mean), but the 18.3 ms p95
ceiling is marginal: one of four recorded runs exceeded it. A fifth attempt
crashed during CityWorld terrain load with `SIGBUS` inside OGRE's
`MeshLodGenerator`. See `doc/nextgen/PLAYABLE_PERFORMANCE_GATE.md`.
