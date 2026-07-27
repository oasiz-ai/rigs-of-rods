# Physics core tests

These tests exercise dependency-free simulation kernels without requiring OGRE,
MyGUI, a game window, or the content pack.

Run the fast local path with any C++11 compiler:

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
