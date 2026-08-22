# Embedded OgreNext provider

This directory is the root-build provider for the reviewed Release combined
runtime slices: macOS arm64 Metal, Windows x64 D3D11, and Linux x86_64 Vulkan,
all under the private `RoROgreNext` namespace. It deliberately reuses the
exact `SDL2::SDL2` imported by RoR's hidden OGRE14 content-host dependency
graph; no second SDL source or binary is admitted.

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

`ror_ogre_next_combined_resources` creates the authenticated shader and
presentation roots directly in `bin/resources/ogrenext`. `RoR-Combined`
depends explicitly on that stage and resolves only
`resources/ogrenext/{ShaderMedia,Presentation}` beneath its ordinary
executable-derived resources root. Absolute build roots are retained only in
configure-time provenance; they are never compiled into runtime media lookup.
The same tree is covered by the normal `Base_Game` install rule, so raw and
installed executables use one fail-closed layout. App-bundle qualification is
still independent of this install-tree contract.
