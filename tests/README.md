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
semantics, Boolean three-argument `case`, string concatenation/byte length,
typed variables, and flattened scalar `$components` paths. Missing variables
evaluate to `nil`, duplicate environment assignments use last-write semantics,
and canonical results normalize the sign of zero and length-prefix strings.

The clean-room suite covers precedence, variables, eager `case` versus
short-circuit logic, malformed and unsupported syntax, hostile binary input,
every quota, overflow and non-finite values, UTF-8, deterministic repeats, and
canonical collisions. It runs in strict, fast-math, and sanitizer builds.
Expression source size, token count, recursion depth, deterministic work,
literal/output strings, variable count/name size, and environment strings are
all bounded.

This evaluator does not execute Lua and has no host, file, network, clock, or
random access. It rejects numeric-selector `case`, all other built-in functions,
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

Fresh actors currently derive their seed from the fixed default world seed and
runtime actor ID, so independent fresh runs must preserve the same actor-ID
assignment. Savegame restoration does not have that limitation because it
restores the resolved seed.

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
contains one contiguous physics-step number, actor/contact counts, and its
32-byte digest. A mandatory aggregate trailer distinguishes a deliberately
short run from truncation.

The reader rejects malformed schemas, nonzero reserved fields, discontinuous
steps, count/size overflow, local or aggregate checksum failures, inconsistent
summaries, and trailing bytes. Exhaustive fixtures truncate the stream at every
byte and flip every individual bit. The comparator validates both complete
inputs, reports the first divergence, and only permits worker-count metadata to
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

Automated runtime lifecycle coverage, input recording, pause/load
continuation, and runtime TSan coverage remain separate runtime gates.

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
