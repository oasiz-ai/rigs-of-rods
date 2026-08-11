# Embedded OgreNext provider

This directory is the root-build provider for the first reviewed combined
runtime slice: Release, macOS, arm64, Metal, and the private `RoROgreNext`
namespace. It deliberately reuses the exact `SDL2::SDL2` imported by RoR's
OGRE14 dependency graph; no second SDL source or binary is admitted.

The root `RoR-Combined` executable links `RoR::OgreNextEmbeddedRuntime`.
The provider owns the renderer-neutral direct contract source list exposed as
`ROR_OGRE_NEXT_COMBINED_OWNED_SOURCES`; `source/main/CMakeLists.txt` requires
and removes every implementation exactly once before creating the executable.
It also excludes the renderer bridge, transport, child, launcher, live, and
product-session closure. A post-link `nm` plus linker-map gate fails the build
if any retired symbol or object returns. The official build target is
`ror_ogre_next_combined_verified`; it is always run, invalidates any old
receipt, and regenerates a byte-bound binary proof even when the executable's
link step is already up to date. Building the raw `RoR-Combined` target alone
never leaves a stale success receipt.

`ror_ogre_next_combined_resources` creates a build-tree-only authenticated
shader and presentation root. The provider exports their immutable absolute
paths as CMake variables and interface compile definitions. `RoR-Combined`
depends explicitly on that stage. App-bundle staging is intentionally disabled
for this slice; the raw build-tree binary is the current demo artifact.
