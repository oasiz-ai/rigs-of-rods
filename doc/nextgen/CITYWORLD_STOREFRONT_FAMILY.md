# CityWorld Next Storefront Family

## Scope

This milestone provides a project-owned modular replacement family for the five
highest-reuse legacy CityWorld store objects. It deliberately stops before map
placement so integration can be evaluated against fixed Penguinville cameras,
road setbacks, collision, and frame-time budgets in a separate change.

The source archive was audited read-only. No legacy geometry, collision mesh,
material, texture, signage, trademark, or shader was copied into the authored
assets.

## Compatibility audit

The pinned `CityWorld.zip` SHA-256 is
`ebeac2f0204f25ca1955f29ca1583b2afa4517a3a848feb1db203814acac2ef3`.
`audit_cityworld_visuals.py` supplied placement counts; OgreXMLConverter 14.5.2
was used only to inspect source vertex bounds. The replacement family preserves
the audited footprint and source transform policy but intentionally removes the
legacy one-metre subgrade envelope.

| Legacy object | Placements | Audited X by Y | Audited maximum Z | Replacement |
| --- | ---: | ---: | ---: | --- |
| `store02` | 6 | 20 by 20 m | 12.7238 m | contemporary corner |
| `store03` | 9 | 20 by 30 m | 16.5552 m | heritage mixed-use |
| `store05` | 9 | 20 by 50 m | 20.5984 m | market hall |
| `store06` | 9 | 30 by 10 m | 12.3121 m | industrial arcade |
| `store08` | 7 | 20 by 10.055 m | 16.9956 m | gabled townhouse |

All new render LODs and collision proxies begin at Blender Z=0. The family
contract records a zero vertical placement offset, zero below-ground
foundation, unit scale, and the normalized source yaw. It contains no map
positions and emits no TOBJ placement.

## Authored detail

The five variants share a coherent real-scale module language while retaining
different silhouettes and street identities:

- the contemporary corner uses recessed glazing and vertical facade fins;
- the heritage block adds masonry quoins, string courses, deep window frames,
  cornice, and pilasters;
- the market hall has a long clerestory monitor and warm occupied glazing;
- the industrial arcade uses repeated steel posts and a continuous header;
- the townhouse carries a pitched roof, deep eaves, and dormer volumes.

Every LOD0 includes framed and mullioned glazing, a dedicated entry, canopy,
project-owned abstract signage, facade bands, corner detail, a rear service
door, drainage, parapets or pitched roof, and bounded rooftop equipment. The
simple collision object remains separate, watertight, outward wound, and
continuous.

The assets use ten texture-free factor materials. Exactly one warm interior
glass material carries a core glTF emissive factor, and it is limited to
selected occupied windows. There are no runtime point lights and no local
shadow casters. The offline compiler emits explicit RTShader-compatible
ambient, diffuse, specular, and emissive fallback passes for OGRE GL3Plus,
Direct3D 11, and the existing macOS renderer path.

Blender still emits the required `TEXCOORD_0` vertex attribute even when no
texture exists. The family generator canonicalizes those semantically unused
UVs to zero after geometry canonicalization, preventing Blender component-join
ordering from changing otherwise identical GLB and OGRE bytes.

## LOD and runtime package

| Variant | LOD0 | LOD1 | LOD2 |
| --- | ---: | ---: | ---: |
| contemporary corner | 20,296 | 660 | 120 |
| heritage mixed-use | 35,280 | 680 | 120 |
| market hall | 62,416 | 748 | 120 |
| industrial arcade | 22,828 | 772 | 120 |
| gabled townhouse | 21,908 | 792 | 276 |

Each asset compiles into:

- one OGRE 14.5.2 LOD0 mesh with manual 80 m and 180 m LOD references;
- separate LOD1 and LOD2 meshes;
- one conservative collision mesh;
- one ODEF;
- one portable material fallback;
- one canonical compile report.

The checked family therefore contains 30 compiled runtime outputs. Shared mesh
resources keep repeated placements from duplicating geometry buffers.

## Reproduction

Generate with Blender 5.2 LTS:

```sh
/Applications/Blender.app/Contents/MacOS/Blender \
  --background --factory-startup \
  --python tools/blender/cityworld_next/generate_cityworld_storefront_family.py \
  -- --output-root "$PWD"
```

Compile each manifest with the pinned OgreXMLConverter 14.5.2:

```sh
for manifest in \
  resources/nextgen/cityworld/buildings/storefront_family/*/*.asset.json
do
  python3 tools/compile_cityworld_asset.py "$manifest" \
    --repo-root . \
    --converter /absolute/path/to/OgreXMLConverter
done
```

Validate the family and checked runtime packages:

```sh
python3 tools/validate_cityworld_storefront_family.py \
  content-source/cityworld_next/buildings/storefront_family/rorng_city_storefront_family.v1.json \
  --repo-root .

for manifest in \
  resources/nextgen/cityworld/buildings/storefront_family/*/*.asset.json
do
  python3 tools/validate_cityworld_asset.py "$manifest" --repo-root .
  python3 tools/compile_cityworld_asset.py "$manifest" \
    --repo-root . --validate-checked
done
```

The canonical GLB and all six compiled runtime outputs per variant are the
byte-reproducible contract. The authoritative gate starts with two empty
temporary roots, copies only the authored generator/compiler sources into each,
independently runs pinned Blender and production OGRE compilation, and then
requires exactly five variants, 35 matching outputs, and no mismatch:

```sh
python3 tools/verify_cityworld_storefront_clean_reproducibility.py \
  --repo-root . \
  --blender /absolute/path/to/blender-5.2.0 \
  --converter /absolute/path/to/OgreXMLConverter-14.5.2
```

No checked `.blend`, preview, GLB, asset manifest, compile report, material,
ODEF, collision fixture, or render mesh is copied into either root. The final
comparison covers each freshly built GLB, material fallback, ODEF, collision
fixture, and three render LOD meshes. For diagnosis, already built roots may be
compared directly with:

```sh
python3 tools/compare_cityworld_storefront_reproducibility.py \
  --left-root /absolute/path/to/build-a \
  --right-root /absolute/path/to/build-b
```

Blender `.blend` files and PNG previews are editable/evidence artifacts.
Blender project/session metadata and PNG render metadata are not claimed to be
byte-reproducible across independent authoring sessions. When the generator,
its declared dependencies, and Blender version are unchanged, the generator
retains these files only after their exact paths and SHA-256 values authenticate
against the previous manifest. Any mismatch fails before candidate output is
created; it is never silently rehashed. A generator, dependency, or Blender
version change is the explicit regeneration boundary and writes newly pinned
source/evidence hashes.

The normal and `python -O` family suites must both pass, including the real
filesystem tamper, artifact-free root-preparation, and binary-comparator
regressions. When pinned Blender 5.2.0 LTS is present, the suite also runs the
generator in a copied root and proves a modified retained `.blend` aborts
without changing the manifest or GLB.

The Linux leg of `.github/workflows/ogre14-native.yml` executes the full
artifact-free two-root gate. It verifies the checksum and version of Blender
5.2.0 LTS, reuses the locked OGRE 14.5.2 Conan graph already built for the
native application, runs under Xvfb, and independently asserts the comparator's
`variants=5`, `outputs=35`, and empty-mismatch report.

## Placement acceptance gate

Placement remains deferred. A later overlay change may replace the 40 audited
objects only after all of these checks pass:

- exact object-to-variant mapping, unit scale, preserved yaw, and zero vertical
  offset;
- fixed-camera facade, roofline, and grounding comparison at every affected
  block;
- no road, sidewalk, alley, door, or neighboring-building intrusion;
- no missing material, shader, renderer API, or resource diagnostic;
- collision driving tests around representative corners and service alleys;
- macOS arm64 GL3Plus capture first, followed by native Windows D3D11 and Linux
  GL3Plus;
- fixed-resolution UI-free RGB evidence and p95 frame-time comparison before
  and after all 40 instances are enabled.
