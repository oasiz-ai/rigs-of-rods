# CityWorld Next Offline Scene Compiler

CityWorld Next content is authored in Blender, exported through a bounded glTF
2.0 interchange profile, and compiled offline. The game does not parse Blender
files or arbitrary glTF at runtime.

The first production fixture is
`rorng_city_bridge_span_20m`. Its checked package contains:

- three render meshes with authored LOD0, LOD1, and LOD2 geometry;
- one road and two barrier collision meshes, separate from render geometry;
- a RoR ODEF that binds the visible LOD0 mesh to all collision meshes;
- a deterministic, RTShader-compatible material fallback generated from the
  declared linear base colour, metallic, and roughness factors; and
- a canonical conversion report with source, compiler, converter, options,
  intermediate, output, and connector hashes.

## Trust boundary

`tools/compile_cityworld_asset.py` first runs the complete CityWorld asset
validator, then applies an additional compiler profile. Version 1 accepts only:

- one GLB 2.0 scene and one embedded buffer;
- root-level, uniquely named static mesh nodes in deterministic scene order;
- applied object transforms, metre units, and the declared Blender Z-up to glTF
  Y-up export;
- indexed triangle lists with float position/normal attributes;
- float tangent/UV attributes on render geometry;
- allowlisted project material identifiers and texture-free
  metallic/roughness factors; and
- the three authored render LODs and declared collision objects.

The compiler rejects extensions, animation, skins, morph targets, cameras,
lights, child graphs, unapplied transforms, reused or unreferenced meshes,
unknown vertex attributes, normalized or malformed accessors, invalid buffer
bounds, non-finite values, non-unit normals/tangents, bad tangent parity,
out-of-range or degenerate indices, missing materials, and aggregate counts
above the pinned limits.

The Blender exporter has already rotated authoring coordinates with
`(x, y, z) -> (x, z, -y)`. Both glTF interchange and OGRE runtime coordinates
are right-handed and Y-up, so the compiler's geometry transform is deliberately
the identity. The report retains the complete authoring-to-runtime basis and
transformed connectors. This prevents an accidental second axis swap.

## Deterministic outputs

OGRE XML is generated in stable node, primitive, vertex, and triangle order with
canonical float formatting. Production compilation invokes exactly
`OgreXMLConverter Tsathoggua (14.5.2)` with GL colour packing and explicit
little-endian output. The converter executable identity is recorded, and every
binary must use `MeshSerializer_v1.100`.

The XML intermediates are not packaged, but their hashes are. This lets
Windows, Linux, and macOS CI reproduce and compare the platform-neutral
lowering without requiring an OGRE tool installation. Checked runtime outputs
are validated byte-for-byte, including their mesh format header, report,
manifest ownership, and provenance.

Production compile:

```sh
python3 tools/compile_cityworld_asset.py \
  resources/nextgen/cityworld/bridge/rorng_city_bridge_span_20m.asset.json \
  --repo-root . \
  --converter /absolute/path/to/OgreXMLConverter
```

Portable checked-output gate:

```sh
python3 tools/compile_cityworld_asset.py \
  resources/nextgen/cityworld/bridge/rorng_city_bridge_span_20m.asset.json \
  --repo-root . \
  --validate-checked
```

The production command requires an explicit converter; it never searches
`PATH` or silently chooses a host tool. Output is written through a
repository-local staging directory, unexpected files fail the compile, and a
failed converter does not produce an accepted report.

## Runtime contract and next gate

The generated ODEF loads
`rorng_city_bridge_span_20m_lod0.mesh`. Its manual mesh LOD table switches to
LOD1 at 80 metres and LOD2 at 180 metres. Collision uses the three generated
meshes with asphalt road friction and concrete barrier friction. The ODEF
declares `standard` so RoR's legacy terrain-object loader cancels its historical
minus-90-degree import pitch; the already Y-up render and collision meshes
therefore enter the scene without a second rotation.

The compiled resource is now package-ready, but the CW2 integration gate still
requires a rights-cleared terrain overlay that places repeated spans, fixed
approach/deck cameras, and automated vehicle traversals. That test must prove:

- connector positions, lane centres, width, and surface height are continuous;
- collision seams neither snag a tyre nor permit tunnelling;
- visual and collision LODs do not disappear or change alignment;
- no missing material or shader diagnostic is emitted; and
- macOS arm64 passes first, followed by native Windows and Linux validation.

Texture ingestion/transcoding, material textures, instancing, nested applied
scene graphs, terrain tiles, curved-span connector solving, and the full PBR
runtime material path remain later compiler-profile revisions. They must be
added one bounded feature at a time with hostile-input and byte-determinism
tests.
