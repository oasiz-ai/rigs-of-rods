# OGRE 14.5.2 Conan recipe — R0 package foundation

This directory contains the pinned, natively verified OGRE package recipe used
by the repository's opt-in `ROR_OGRE14` macOS build. The proof below covers the
dependency/package slice; application scene and bundle validation is tracked
separately.

## Provenance

- Base recipe: `ogre3d/14.5.2@anotherfoxguy/stable`
- Nexus remote:
  `https://nexus.anotherfoxguy.com/repository/rigs-of-rods/`
- Base recipe revision: `09d5bedad24a0560adc53606214f2cbf`
- Base revision timestamp: `2026-07-23T09:33:28Z`
- OGRE archive:
  `https://github.com/OGRECave/ogre/archive/refs/tags/v14.5.2.tar.gz`
- OGRE archive SHA-256:
  `1949fe62f3e4b8043e82e4dc94f9b0ab412a5bffc9e10d3b1dddc80fe54fe1e3`
- Local relocation patch SHA-256:
  `6292eb8bf8a9b373f68e7ff180b5750051eff2e21d39cacc84ae020410d06dc2`
- Local bounds-safe shadow-projector patch SHA-256:
  `13bbbd974dfe0106dc51a8846caff800394b371a57fc98bcdd0dbcf783823d51`
- Local deferred GLSL validation patch SHA-256:
  `d60d2684b6fd29ba1d3bdc4aaa34bb21463488ab16af03592e3b19594f249e72`
- Local always-on ZIP archive mutex patch SHA-256:
  `7674db9811bdf80abb0248b39504f259b85ecd9331f5bb1ca19c9b5d7a9db1b4`
- Local ArchiveManager unpublished-instance rollback patch SHA-256:
  `cf7aaac084432441167a384245b65400c07f23ea80e2af386cebd41832cc967a`
- Local terrain composite revision and Metal transfer patch SHA-256:
  `cb8bf0aa200793a02574725396c407e7e58eded5b242c2e7dd617c745a6dbaf5`
- macOS arm64 Release lock:
  `cmake/conan/locks/ogre3d-14.5.2-macos-arm64-release.lock`

The recipe retains the base revision's `pugixml-fix`,
`FindPkgMacros.cmake`, and optional Remotery download patches byte-for-byte. It
intentionally omits `use-external-imgui.patch`.

The local shadow-projector patch fixes an OGRE 14.5.2 bounds bug in
`resolveShadowTexture()`. A fallback shadow index could exceed both projector
vectors, underflow `mShadowTextureCameras.size() - shadowIndex`, and then bind an
out-of-bounds camera pointer. The valid path now requires both a texture and a
camera, and only that path computes layered-camera bounds. The fallback path
binds the no-shadow texture and explicitly clears its projector slot.
`destroyShadowTextures()` additionally clears every
`OGRE_MAX_SIMULTANEOUS_LIGHTS` slot as lifecycle defense for PSSM and layered
shadows.

The local GLSL patch keeps `glLinkProgram()`, `GL_LINK_STATUS`, and the link
info-log diagnostic in `GLSLMonolithicProgram::compileAndLink()`, but removes
its immediate `glValidateProgram()` and validation-log query. OpenGL program
validation depends on complete live draw state, including sampler bindings and
a compatible VAO; RoR has not established that state while OGRE is linking the
program. Omitting that state-dependent check here avoids reporting valid linked
programs as failed because of transient setup state.

The local ZIP archive patch protects the shared `zip_t` handle and cached file
list with a class-local `std::recursive_mutex`. It is deliberately independent
of `OGRE_CONFIG_THREADS`, whose no-thread definitions otherwise reduce OGRE's
archive mutex macros to no-ops. Recursive locking is required because sloppy
resource lookup makes `ZipArchive::open()` call the separately locked
`findFileInfo()` helper. Load, unload, open, list, find, and existence
operations now serialize on macOS, Linux, and Windows.

The local ArchiveManager rollback patch retains the exact factory which
created an archive, rejects null factory results and duplicate map insertion,
and returns every unpublished instance to that factory if `load()` or map
publication throws. This closes the ownership edge required by authenticated
`EmbeddedZip` rollback without changing successful archive lookup.

The local terrain-composite patch makes `Texture::getStateCount()` an exact,
monotonic content revision for terrain composite maps: successful cached/black
initialization and every successful composite RTT update call `_dirtyState()`.
The dirty call follows the upload/RTT call, so exceptions do not publish a
revision for content that was not completed. The backend-neutral Terrain
change is shared by Metal, D3D11, and GL3Plus. Targeted Metal corrections make
every level-zero `TU_AUTOMIPMAP` upload regenerate its mip chain, synchronize
shared-storage readback, preserve the per-mip dimensions already supplied by
`Texture::createSurfaceList()`, and select the pixel buffer's actual mip level
and face for both private and shared textures.

On 2026-07-28, the native arm64 application exercised the rendering patches
with PSSM plus mixed cube, 2D, and shadow samplers. The two-truck scene
completed 1,000 physics steps, wrote and fully decoded a 2560x1440 Retina PNG,
and logged neither a sampler-validation failure nor `GL_INVALID_*`. On
2026-07-29, the package was rebuilt with the ZIP mutex patch and passed the
relocated package probe described below. Application rebundling against this
new package revision is tracked separately from the package proof.

## Intentional policy differences

- There is no Cg Toolkit package requirement and the OGRE Cg plugin is disabled.
  OGRE's RTShaderSystem still compiles source-level program-writer support for
  several shader languages; this policy is about the package dependency and
  runtime plugin graph, not removal of every Cg-named source file.
- The ImGui overlay is disabled, with no ImGui package requirement or external
  overlay patch.
- Apt packages are requested only for Linux.
- macOS frameworks and Direct3D 9 are always disabled.
- Direct3D 11 is enabled only on Windows.
- GL3Plus is enabled on Linux and macOS; Metal is additionally enabled on
  macOS.
- Collected library metadata is sorted before publication.
- Installed metadata and binaries use package-relative paths. The common
  macOS RPATH set is `@loader_path`, `@loader_path/..`, and
  `@loader_path/../lib`. Only tools installed one level deeper under
  `bin/macosx` receive `@loader_path/../../lib`.

## Verified native package

On 2026-08-09, AppleClang executable `21.0.0.21000101` built the locked Release
package as native arm64 C++17 with `os.version=11.0`. The reproducibility
profile intentionally retains its existing Conan compatibility setting
`compiler.version=15`:

```text
ogre3d/14.5.2#a6522b345c9ed182253e570281c54a28:
  5c43930ec5f93ceae6d2e5fcd4957341351cdf91#
  4988ee3efa3ef825134195858fb368a9
```

The line breaks above are for readability. The exact Conan reference is:

```text
ogre3d/14.5.2#a6522b345c9ed182253e570281c54a28:5c43930ec5f93ceae6d2e5fcd4957341351cdf91#4988ee3efa3ef825134195858fb368a9
```

The proof established all of the following:

- The Conan `test_package` linked `Main`, `Bites`, `MeshLodGenerator`,
  `Overlay`, `Paging`, `Property`, `RTShaderSystem`, `Terrain`, and `Volume`.
  It copied the package to a random temporary prefix, cleared
  `DYLD_LIBRARY_PATH` and `DYLD_FALLBACK_LIBRARY_PATH`, loaded the package's
  `plugins.cfg`, selected Metal, and completed OGRE initialization.
- The same relocated probe opened a generated ZIP through one shared
  `ZipArchive` from eight threads. Each thread completed 500 iterations of
  `open`, `exists`, `list`, `listFileInfo`, `find`, and `findFileInfo`. Basename
  opens deliberately entered the recursive `open()` to `findFileInfo()` path.
- A deliberately failing archive factory threw from `Archive::load()` twice.
  Both unpublished instances returned to the exact factory, and neither name
  appeared in `ArchiveManager` after the failure.
- An asymmetric 4x4 RGBA texture round-tripped level zero byte-for-byte,
  including row order and alpha. Its four constant 2x2 quadrants reappeared in
  the correct positions at mip level one, independently proving automatic mip
  regeneration and mip-level selection. An out-of-bounds read left its
  sentinel destination untouched, and a subsequent valid read still matched.
  Two `_dirtyState()` calls advanced the native resource state count by
  exactly one each; the pinned Terrain source probe separately verifies those
  calls occur only after successful initialization/upload and RTT completion.
- The package contains 20 arm64 Mach-O files, all with a macOS 11.0 minimum
  deployment target, relocatable install names, 17 package-local symlinks, ten
  relative pkg-config files, and no absolute Conan prefix in loader, install,
  plugin, or pkg-config metadata. Both lexical paths and resolved macOS path
  aliases are checked.
- The exact active plugin set is `Codec_FreeImage`,
  `Plugin_BSPSceneManager`, `Plugin_OctreeSceneManager`,
  `Plugin_OctreeZone`, `Plugin_PCZSceneManager`, `Plugin_ParticleFX`,
  `RenderSystem_GL3Plus`, and `RenderSystem_Metal`.
- The native verifier copied the package to a second relocated prefix, repeated
  every Mach-O metadata check there, loaded each plugin in an isolated process,
  and ran the relocated `OgreMeshUpgrader` far enough to reach its argument
  parser.
- For every Mach-O, the verifier resolves each `LC_RPATH` relative to that
  binary and rejects a result outside the relocated package. Libraries and
  plugins contain only the three common paths. The three tools under
  `bin/macosx` also contain their required `@loader_path/../../lib`, which
  resolves to this package's `lib` directory from that specific location.
- A raw string inventory separately reports non-runtime build-source/assertion
  paths in exactly three files:
  `lib/libOgreBites.14.5.dylib`,
  `lib/OGRE/RenderSystem_GL3Plus.14.5.dylib`, and
  `lib/OGRE/RenderSystem_Metal.14.5.dylib`. The OGRE paths come from Objective-C
  or Objective-C++ `__FILE__` sites; OgreBites also contains source paths from
  the statically linked SDL package. These strings do not participate in
  loading, but they mean this artifact is not claimed to be fully
  path-reproducible.

## Reproduce the proof

Run the graph checks from the repository root:

```sh
PYTHONDONTWRITEBYTECODE=1 \
  python3 -m unittest discover \
  -s tests/tools -p 'test_assert_ogre_*.py' -v

PYTHONDONTWRITEBYTECODE=1 \
  python3 tests/tools/assert_ogre_recipe_graph.py
```

The graph assertion creates a temporary `CONAN_HOME`, adds the pinned remotes,
exports the local recipe, checks the source and patch hashes, resolves the
checked-in lock, and rejects dependency revisions, target settings, or options
that drift.

The following reproduces the dated native build on an Apple Silicon host.
`CMAKE_POLICY_VERSION_MINIMUM=3.5` applies only to the Conan dependency-build
subprocess; do not apply it to the top-level RoR configure.

```sh
OGRE_CONAN_HOME="$(mktemp -d /tmp/ror-ogre-native-proof.XXXXXX)"

CONAN_HOME="$OGRE_CONAN_HOME" conan profile detect --force
CONAN_HOME="$OGRE_CONAN_HOME" conan remote add \
  conancenter https://center2.conan.io --force
CONAN_HOME="$OGRE_CONAN_HOME" conan remote add \
  nexus https://nexus.anotherfoxguy.com/repository/rigs-of-rods/ --force

PYTHONDONTWRITEBYTECODE=1 \
CMAKE_POLICY_VERSION_MINIMUM=3.5 \
CONAN_HOME="$OGRE_CONAN_HOME" \
  conan create cmake/conan/recipes/ogre3d \
  --version=14.5.2 \
  --lockfile=cmake/conan/locks/ogre3d-14.5.2-macos-arm64-release.lock \
  -pr:h=cmake/conan/profiles/macos-arm64-release \
  -pr:b=cmake/conan/profiles/macos-arm64-release \
  --build=missing

OGRE_PACKAGE_REF='ogre3d/14.5.2#a6522b345c9ed182253e570281c54a28:5c43930ec5f93ceae6d2e5fcd4957341351cdf91#4988ee3efa3ef825134195858fb368a9'
OGRE_PACKAGE_PATH="$(
  CONAN_HOME="$OGRE_CONAN_HOME" conan cache path "$OGRE_PACKAGE_REF"
)"
PYTHONDONTWRITEBYTECODE=1 \
  python3 tests/tools/assert_ogre_native_package.py "$OGRE_PACKAGE_PATH"
```

Changing compiler settings can legitimately change the package ID or package
revision; update this evidence only after rerunning the complete native proof.

## Whitespace-path evidence and limitation

The recipe rejects unsafe source-path characters and escapes spaces in its C
and C++ compiler prefix maps. This independent CMake proof compiled
successfully and preserved the escaped space in the final compiler command:

```sh
cmake -S tests -B '/tmp/ror CMake flag audit' \
  -DCMAKE_BUILD_TYPE=Release \
  '-DCMAKE_CXX_FLAGS=-ffile-prefix-map=/tmp/Conan\ Home/source=ogre3d-14.5.2' \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build '/tmp/ror CMake flag audit' \
  --target ror_beam_axial_response_tests --verbose -j2
```

A separate clean full-graph build with a space in `CONAN_HOME` progressed
through several dependencies but stopped before OGRE in the pinned upstream
`libjpeg/9e` Autotools recipe:

```text
configure: error: unsafe srcdir value: '.../RoR OGRE native proof.../libj.../b/src'
```

Therefore the OGRE recipe's CMake whitespace escaping is proven, but a clean
full dependency build in a spaced Conan cache is not supported yet. This is an
upstream dependency-recipe limitation and must not be reported as a passing
package configuration. Objective-C/Objective-C++ prefix maps and equivalent
source-path remapping in the pinned SDL dependency also remain open if
byte-for-byte path-reproducible packages become a release requirement.

## Remaining R0 blockers

- Metal remains experimental and RoR's game media does not yet provide an
  authored MSL material pipeline; the current native application path uses
  GL3Plus and RTShaderSystem.
- Complete validation-scene render parity, performance measurements, and
  Windows/Linux OGRE 14 application migration remain open R0 gates.
