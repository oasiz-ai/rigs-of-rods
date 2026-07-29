# CityWorld NeoQueretaro tree family

The first project-owned CityWorld vegetation family replaces the visual
contract for the 18 individually placed `arbol1Qr` objects in NeoQueretaro.
Overlay v4 integrates all 18 atomically through an exact in-place compatibility
plan: no second TOBJ placement is emitted, so legacy and replacement trees
cannot coexist.

The family contains round, columnar, and windswept urban broadleaf silhouettes.
The stable SHA-256 selector assigns every legacy placement ordinal to one of
those assets, plus a bounded yaw and 0.94–1.06 scale variation. All geometry,
materials, editable Blender files, and previews are generated locally by
`tools/blender/cityworld_next/generate_neoq_tree_family.py`; no external model,
texture, or executable shader is imported.

## Runtime assets

Each variant ships as a separate compiled terrain object so all three can be
loaded together without shared material-name collisions:

- `rorng_city_neoq_tree_round`
- `rorng_city_neoq_tree_columnar`
- `rorng_city_neoq_tree_windswept`

Every asset has authored normals, tangents, UVs, and applied transforms. Opaque
geometry foliage avoids alpha sorting, halo, and missing-texture failure modes
in the current bounded compiler. LOD0, LOD1, and LOD2 contain 21,136, 1,988,
and 268 triangles respectively. The reductions are 9.41% and 1.27% of LOD0,
below the hero-tree limits of 40% and 12%. A separate 44-triangle watertight
trunk proxy handles vehicle contact without colliding against the canopy.

`CityWorldNeoQTreePlan.inc` is the shared native/package placement authority.
The overlay builder parses it strictly and proves that source lines 9–26,
positions, and original rotations match the exact pinned `CityWorld.tobj`, and
that variant, yaw, and scale agree with the family selector. It emits 18 unique
portable ODEF wrappers. Each wrapper changes only the ODEF's uniform scale, so
the render node and watertight trunk collision proxy receive the same
per-instance transform on macOS, Linux, and Windows.

At runtime the native compatibility path requires both the authenticated
`CityWorld.zip` dependency and exact `CityWorld.tobj` SHA-256. It preflights all
18 wrapper ODEFs before committing any change, then replaces the original ODEF
and Y rotation in place while preserving exact X/Y/Z, X/Z rotation, type,
instance identity, and rendering distance. Any archive, TOBJ, placement,
selector, name, or resource drift leaves all legacy trees untouched.

The family and every render LOD carry:

- a stable silhouette identity and family identity;
- a ground anchor, canopy bend start, maximum tip displacement, and
  instance-hash wind phase contract;
- an eight-azimuth by three-elevation, 4,096-square impostor bake contract;
- alpha-mask edge dilation and coverage-preserving mip requirements for the
  future atlas.

## Reproduction

Generate all editable sources, GLBs, manifests, and 1280x720 previews with
Blender 5.2:

```sh
/Applications/Blender.app/Contents/MacOS/Blender \
  --background --factory-startup \
  --python tools/blender/cityworld_next/generate_neoq_tree_family.py -- \
  --output-root /absolute/path/to/rigs-of-rods
```

Compile each generated asset with the pinned OgreXMLConverter 14.5.2, then
rebuild provenance:

```sh
python3 tools/compile_cityworld_asset.py \
  resources/nextgen/cityworld/vegetation/rorng_city_neoq_tree_round/rorng_city_neoq_tree_round.asset.json \
  --repo-root . --converter /absolute/path/to/OgreXMLConverter
python3 tools/compile_cityworld_asset.py \
  resources/nextgen/cityworld/vegetation/rorng_city_neoq_tree_columnar/rorng_city_neoq_tree_columnar.asset.json \
  --repo-root . --converter /absolute/path/to/OgreXMLConverter
python3 tools/compile_cityworld_asset.py \
  resources/nextgen/cityworld/vegetation/rorng_city_neoq_tree_windswept/rorng_city_neoq_tree_windswept.asset.json \
  --repo-root . --converter /absolute/path/to/OgreXMLConverter
python3 tools/build_cityworld_next_provenance.py --repo-root .
```

`tools/validate_cityworld_tree_family.py` recomputes all 18 selection records,
checks the rights and scale envelope, invokes the complete glTF asset validator,
requires exact wind/impostor node metadata, verifies trunk-only collision, and
hashes every compiled runtime output. The existing compiler's
`--validate-checked` mode independently verifies the deterministic OGRE package.
`tools/build_cityworld_local_overlay.py` additionally emits
`cityworld_next_neoq_tree_replacements.v1.json`, hashes every wrapper, records
zero duplicate placements, and produces byte-identical archives under normal
and optimized Python.

`tests/fixtures/cityworld_neoq_tree_runtime/cityworld_neoq_tree_runtime.as`
provides the native visual gate. It hides UI, advances an exact fixed-step
schedule, and captures both rows plus oblique and overhead context before
quitting with a machine-readable PASS record. Acceptance requires the
transaction marker with `tree_replacements=18`, all three render/collision
families and nine tree materials to load, four 1280x720 captures, and no
tree-specific missing-material or renderer-fatal record.

## Current renderer limitations

The v1 scene compiler carries wind and impostor metadata but does not yet emit
an impostor atlas or a vegetation wind shader. Runtime assets therefore use
geometry foliage at all three current LODs; the explicit contract prevents
silently claiming those two enrichments are active. Texture-backed leaf cards,
seasonal instance tint, animation, and replacement of the larger grouped tree
meshes remain follow-up work.
