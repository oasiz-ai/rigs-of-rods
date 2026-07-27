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
cover the isolated law. Actor beam adaptation, assembled force/momentum checks,
save/replay integration, content calibration, and the Agora impact regression
remain separate P1 gates.

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
anti-lag uses a separate engine-update count plus turbo index.

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

Sleeping engines receive a distinct sample on each outer-frame engine update.
That removes shared-state races and repeated samples, but equal simulated time
with different render-frame grouping is not yet a deterministic replay
contract.

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
