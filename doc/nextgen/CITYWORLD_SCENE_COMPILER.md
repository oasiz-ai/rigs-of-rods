# CityWorld Next Offline Scene Compiler

CityWorld Next content is authored in Blender, exported through a bounded glTF
2.0 interchange profile, and compiled offline. The game does not parse Blender
files or arbitrary glTF at runtime.

The first production fixtures are the tangent
`rorng_city_bridge_span_20m`, the 15-degree left curve
`rorng_city_bridge_curve_left_15deg_20m`, the 12 m transition, and the 40 m
gateway block. Their checked packages contain:

- three render meshes with authored LOD0, LOD1, and LOD2 geometry;
- one road and two barrier collision meshes, separate from render geometry;
- a RoR ODEF that binds the visible LOD0 mesh to all collision meshes;
- a deterministic, RTShader-compatible material fallback generated from the
  declared linear base colour, metallic, roughness, and optional core-glTF
  emissive factors; and
- optional bounded local point lights lowered from explicit, versioned
  authoring-space declarations into the terrain-object definition; and
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

The curved fixture uses the same commands with:
`resources/nextgen/cityworld/bridge/curve_left_15deg/rorng_city_bridge_curve_left_15deg_20m.asset.json`.
Its 20 m centreline is a 15-degree arc with a 76.394372684 m radius and a
19.942933147 m connector chord. The runtime connector report preserves both
end positions exactly, while the asset contract retains centreline length,
radius, angle, road width, and tangent directions for placement.

The production command requires an explicit converter; it never searches
`PATH` or silently chooses a host tool. Output is written through a
repository-local staging directory, unexpected files fail the compile, and a
failed converter does not produce an accepted report. After every output and
the canonical report are in place, the compiler atomically refreshes the
manifest's compiled-output hashes and report identity.

## Runtime contract and physical scene gate

The generated ODEF loads
`rorng_city_bridge_span_20m_lod0.mesh`. Its manual mesh LOD table switches to
LOD1 at 80 metres and LOD2 at 180 metres. Collision uses the three generated
meshes with asphalt road friction and concrete barrier friction. The ODEF
declares `standard` so RoR's legacy terrain-object loader cancels its historical
minus-90-degree import pitch; the already Y-up render and collision meshes
therefore enter the scene without a second rotation.

`tools/run_cityworld_bridge_scene.py` builds a deterministic, flat runtime pack
without downloading or modifying CityWorld. It combines only the checked
compiled outputs, the pinned `content` submodule's simple2 terrain inputs, and
the project-owned runtime fixture. It verifies that the packaged DAF rig is
byte-identical to the pinned source, launches RoR under an isolated user
profile, and requires:

- connector positions, lane centres, width, and surface height are continuous;
- collision seams neither snag a tyre nor permit tunnelling;
- the render and all three collision meshes load;
- every bridge material receives an RTShaderSystem vertex/fragment program;
- no bridge-specific missing material, ODEF, GL, or renderer diagnostic occurs;
- exactly one UI-free 1280x720 true-colour screenshot has valid PNG structure,
  chunk CRCs, bounded decompression, valid filtering, and non-degenerate decoded
  pixels; and
- the scripted vehicle reaches the exit inside bounded vertical, lateral,
  velocity, distance, and physics-step envelopes.

Run it against a packaged build:

```sh
python3 tools/run_cityworld_bridge_scene.py \
  --executable /absolute/path/to/RoR \
  --artifact-dir /absolute/path/to/fresh-artifacts
```

The runner uses native macOS, Linux, and Windows profile layouts. The
`ROR_D0_SCENE_HOME` diagnostic override is absolute-path-only on all three
platforms, so tests never write into a developer's normal profile. The
deterministic archive, executable, vehicle archive, compile report, repository
and content commits, logs, traversal metrics, and decoded RGB properties are
recorded in `ror-cityworld-bridge-runtime-report-v1`.

The first macOS arm64 run on 2026-07-28 passed three spans and both exact
connector seams over 90.1281 metres. Maximum lateral drift was 0.640167 metres;
the vehicle's average-node height remained between 0.69451 and 1.50233 metres;
exit speed was 16.9444 m/s after 20,260 deterministic physics steps. The
1,280x720 UI-free RGB frame fully decoded, contained all bridge geometry and
the vehicle, and all six bridge materials had generated GL3Plus RTSS programs.
Native Windows and Linux executions remain required before the gate is promoted
from macOS-first proof to three-platform release coverage.

`tools/solve_cityworld_bridge_corridor.py` now consumes the runtime connector
positions and directions rather than relying on hand-authored offsets. For the
checked curved fixture it places three 15-degree modules at 7.5, 22.5, and
37.5-degree yaw, with exactly zero position and tangent error at both seams.
The resulting path starts at heading 0 degrees and exits at 45 degrees.

`tools/run_cityworld_curved_bridge_scene.py` verifies the checked `.tobj` is
byte-identical to that solve, then runs the same isolated native profile and
RGB decoder. The first signed macOS arm64 run on 2026-07-28 drove the DAF
87.572 m through all three modules in 21,860 deterministic physics steps. It
crossed both seams, held maximum radial path error to 1.41593 m and actor height
to 0.805754–0.866494 m, and reached the near-exit gate at 14.5303 m/s. All
seven visible materials, including `rorng_city_lamp_emissive`, received GL3Plus
RTSS programs, and the 1280x720 UI-free frame passed full PNG/pixel decoding.
The report pins the solver, straight runner shared core, curve runner, compiled
asset, content commit, runtime pack, executable, vehicle archive, placement
report, logs, metrics, and RGB evidence.

Run the curved gate against a packaged build:

```sh
python3 tools/run_cityworld_curved_bridge_scene.py \
  --executable /absolute/path/to/RoR \
  --artifact-dir /absolute/path/to/fresh-artifacts
```

Texture ingestion/transcoding, material textures, instancing, nested applied
scene graphs, terrain tiles, mixed-module overlay placement, and the full PBR
runtime material path remain later compiler-profile revisions. They must be
added one bounded feature at a time with hostile-input and byte-determinism
tests.
