# CityWorld NeoQueretaro tree family

The first project-owned CityWorld vegetation family replaces the visual
contract for the 18 individually placed `arbol1Qr` objects in NeoQueretaro.
Placement is deliberately deferred so the map overlay can integrate all 18
instances atomically after fixed-camera and frame-time gates are ready.

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

## Current renderer limitations

The v1 scene compiler carries wind and impostor metadata but does not yet emit
an impostor atlas or a vegetation wind shader. Runtime assets therefore use
geometry foliage at all three current LODs; the explicit contract prevents
silently claiming those two enrichments are active. Texture-backed leaf cards,
seasonal instance tint, animation, and map placement remain follow-up work.
