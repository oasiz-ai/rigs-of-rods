# OGRE 14.5.2 Conan recipe prototype

This directory is an intentionally unactivated prototype. The repository's root
`conanfile.py` does not reference it.

## Provenance

- Base recipe: `ogre3d/14.5.2@anotherfoxguy/stable`
- Nexus remote: `https://nexus.anotherfoxguy.com/repository/rigs-of-rods/`
- Base recipe revision: `09d5bedad24a0560adc53606214f2cbf`
- Base revision timestamp: `2026-07-23T09:33:28Z`
- OGRE archive: `https://github.com/OGRECave/ogre/archive/refs/tags/v14.5.2.tar.gz`
- OGRE archive SHA-256:
  `1949fe62f3e4b8043e82e4dc94f9b0ab412a5bffc9e10d3b1dddc80fe54fe1e3`

The prototype retains the base revision's `pugixml-fix`,
`FindPkgMacros.cmake`, and optional Remotery download patches byte-for-byte.
It intentionally omits `use-external-imgui.patch`.

## Intentional policy differences

- Cg is neither required nor built.
- The ImGui overlay is disabled, with no ImGui package requirement or external
  overlay patch.
- Apt packages are requested only for Linux.
- macOS frameworks and Direct3D 9 are always disabled.
- Direct3D 11 is enabled only on Windows.
- GL3Plus is enabled on every non-Windows platform.
- Collected library metadata is sorted before publication.

## Verification

Run the isolated graph assertion from the repository root:

```sh
python3 tests/tools/assert_ogre_recipe_graph.py
```

The tool creates a temporary `CONAN_HOME`, exports this recipe locally, resolves
a fresh macOS arm64 graph, and verifies that the graph contains neither
`cg-toolkit` nor `imgui`. It also verifies the source SHA in both the source tree
and Conan's exported recipe, plus the exact retained Nexus patch set.
