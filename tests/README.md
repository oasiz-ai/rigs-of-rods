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

## Beam axial response invariant

The axial damping kernel limits the effective damping coefficient to:

```text
d_effective <= 1 / (effective_inverse_mass * physics_timestep)
```

This means damping alone can reduce relative axial velocity to zero in one
fixed step, but cannot reverse it and inject kinetic energy. The deterministic
property test checks that invariant across 20,000 fixed-seed combinations of
mass, velocity, damping, mobility, and timestep.
