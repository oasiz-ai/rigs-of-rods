# Building Rigs of Rods

The supported product build is OgreNext-first on macOS arm64, Linux x86_64,
and Windows x64. A fresh CMake/Conan configuration builds a three-executable
renderer suite:

- `RoR`: the public launcher, which requests OgreNext by default;
- `RoR-OgreNext`: the isolated presentation renderer;
- `RoR-Ogre14`: the simulation/game host and bounded compatibility fallback.

The packaged OgreNext child is deliberately not the same binary as the
standalone renderer probes. Until the production content, image, performance,
and cross-platform admission gates pass, immutable package facts keep the
normal launch fail-closed on `RoR-Ogre14`. This is a temporary runtime
admission result, not a different build default. An explicit
`--renderer-frontend=ogre-next-require` request never falls back; use
`--renderer-frontend=legacy-only` only when diagnosing compatibility.

For a bounded renderer demo, configure a fresh build with
`-DROR_OGRE_NEXT_DEMO_ADMISSION=ON`. This build-only switch admits the matched,
verified `RoR-OgreNext` child and its PSSM path in the generated launcher facts.
It requires `ROR_OGRE_NEXT_PRODUCTION_PACKAGE=ON`, does not admit native ray
tracing, and does not change ordinary product-build defaults.

See [the OgreNext integration checkpoint](doc/nextgen/OGRE_NEXT_INTEGRATION.md)
for the process boundary and current admission status. The upstream wiki still
contains useful platform setup background, but its historical dependency graph
does not describe this renderer suite.

## Supported toolchains

The locked release graphs and CI cover:

| Host | Architecture | Compiler profile | Renderer API |
| --- | --- | --- | --- |
| macOS 11 or newer | arm64 | AppleClang 15, C++17 | Metal/GL3Plus |
| Linux | x86_64 | GCC 11, C++17 | Vulkan/GL3Plus |
| Windows | x64 | MSVC 19.44, C++17 | Direct3D 11 |

CMake 3.16 or newer is required. Reproducible builds use Conan 2.31.1 and
Ninja 1.13.0. The repository pins platform-specific Conan lockfiles under
`cmake/conan/locks`; do not substitute an unlocked graph for release or
renderer-admission evidence.

The primary graph currently pins OGRE 14.5.2, SDL 2.32.10, MyGUI 3.4.0,
AngelScript 2.38.0, OpenAL Soft 1.24.3, fmt 12.1.0, RapidJSON
`cci.20211112`, curl 8.2.1, OpenSSL 3.6.3, and SocketW 3.11.0. The isolated
OgreNext child owns its separately pinned source and closed shader/media
manifest; it is not linked into the OGRE 14 process.

## Configure and build

Initialize the repository and install the pinned build clients:

```sh
git submodule update --init --recursive
python3 -m pip install conan==2.31.1 ninja==1.13.0
conan profile detect --force
conan remote add conancenter https://center2.conan.io --force
conan remote add rigs-of-rods-deps \
  https://nexus.anotherfoxguy.com/repository/rigs-of-rods/ --force
```

On Apple Silicon, configure and build the complete default suite with:

```sh
cmake -S . -B build-macos -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=cmake/conan_provider.cmake \
  -DROR_BUILD_TESTS=ON \
  -DROR_CREATE_CONTENT_FOLDER=ON
cmake --build build-macos --target all --config Release
cmake --build build-macos --target ror_macos_bundle --config Release
ctest --test-dir build-macos --build-config Release --output-on-failure
```

The testable application is `build-macos/bin/RoR.app`. The bundle target owns
Mach-O dependency rewriting and nested signing, so flat CMake install and CPack
are intentionally disabled for this topology on macOS.

On Linux or Windows, use the matching locked profile:

```sh
cmake -S . -B build-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=cmake/conan_provider.cmake \
  -DCONAN_HOST_PROFILE="$PWD/cmake/conan/profiles/<platform>-release" \
  -DCONAN_BUILD_PROFILE="$PWD/cmake/conan/profiles/<platform>-release" \
  -DROR_BUILD_TESTS=ON \
  -DROR_CREATE_CONTENT_FOLDER=ON
cmake --build build-release --target all --config Release
ctest --test-dir build-release --build-config Release --output-on-failure
cmake --install build-release --config Release --prefix "$PWD/stage-release"
```

Replace `<platform>` with `linux-x86_64` or `windows-x86_64`. These platforms
retain normal install and CPack rules, and the staged runtime must keep all
three sibling executables together. The platform workflows under
`.github/workflows` are the authoritative release commands and runtime-audit
gates.

## Renderer build options

The supported defaults are:

| CMake option | Default | Purpose |
| --- | --- | --- |
| `ROR_OGRE14` | `ON` | Build the simulation host and compatibility renderer. |
| `ROR_RENDERER_PUBLIC_LAUNCHER` | `ON` | Build the OgreNext-first public chooser. |
| `ROR_OGRE_NEXT_PRODUCTION_PACKAGE` | `ON` | Build and verify the real isolated OgreNext child. |
| `ROR_OGRE_NEXT_DEMO_ADMISSION` | `OFF` | Admit the matched OgreNext/PSSM package for an explicit demo build. |

The three renderer-suite topology defaults are independent, explicit `ON`
cache initializers. The separate demo admission is explicitly `OFF`. A fresh
no-flag CMake configuration therefore cannot silently inherit an OGRE14-only
product topology or an admitted demo from option evaluation order. Existing
build directories retain their previously cached values; use a fresh build
directory when validating either configuration.

`ROR_RENDERER_PUBLIC_LAUNCHER=ON` requires `ROR_OGRE14=ON`, because the current
two-process OgreNext product uses the OGRE 14 executable as its simulation host
as well as its bounded pre-readiness fallback. Disabling either dependent
option is an explicit developer configuration and is not a supported shipping
package.

`ROR_OGRE_NEXT_DEMO_ADMISSION=ON` additionally requires
`ROR_OGRE_NEXT_PRODUCTION_PACKAGE=ON`. The public launcher depends on the
verified product-stage target, so a demo launcher is not emitted ahead of its
matched `RoR-OgreNext` executable and media closure.

The packaged user entrypoint is always the public chooser: `RoR.app` with
`CFBundleExecutable=RoR` on macOS, `RunRoR` (which executes sibling `RoR`) on
Linux, and `RoR.exe` on Windows. Storefront action metadata names those same
entrypoints and never launches either renderer child directly.

`ROR_DEPENDENCY_DIR` remains available for the historical dependency-folder
workflow. The locked Conan provider is the reproducible path used by current
native CI and renderer admission.
