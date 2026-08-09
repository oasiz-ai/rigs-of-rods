# OGRE 14 material semantic catalog v2

The v2 compatibility catalog is the authored data boundary between an exact
OGRE 14 material observation and the renderer-neutral material semantic
registry. It does not infer intent from filenames, material names, texture-unit
position, legacy lighting, or specular values. No CityWorld declaration or
third-party asset is checked in with this infrastructure change.

## Authoring and compilation

Source catalogs conform to
`tools/schemas/ogre14-material-semantic-catalog-v2.schema.json`. Every object is
closed: a missing or unknown field fails compilation, as do duplicate JSON
members, trailing JSON, non-canonical hashes, non-finite float encodings, and
cross-field disagreement. Float values are authored as exact lowercase
IEEE-754 float32 bit patterns so JSON decimal conversion cannot vary by host.

Compile a reviewed catalog with:

```sh
python3 tools/compile_ogre14_material_semantic_catalog_v2.py \
  reviewed-catalog.json reviewed-catalog.rormat2
```

The compiler uses only the Python standard library, sorts records by the UTF-8
bytes of exact resource group and material name, rejects duplicate exact keys,
and atomically replaces the output after complete validation. The committed
fixture is synthetic project-owned test data; it is not a compatibility claim
for any external package.

## Exact record binding

Each record binds all of the evidence needed to reproduce one selection:

- package/archive SHA-256, exact resource group, and resource generation;
- exact source script member and SHA-256, effective/repaired script SHA-256,
  and repair-plan version;
- exact material name, native-structure SHA-256, selected scheme and LOD, and
  declared runtime-generation classification;
- explicit base-color lowering result, registry texture color role, lowering
  algorithm and version, and declaration revision;
- complete pass blend, write-mask, depth, bias, cull, alpha, fill, and iteration
  facts;
- every texture unit's exact ordinal/name/resource binding, semantic, color
  role, swizzle, UV set and matrix bits, sampler, and color/alpha combine facts;
- explicit environment and shadow augmentation declarations.

These bindings are compatibility evidence, not an authentication substitute
for the content loader. The production loader must independently prove the
archive, script, repair, generation, and native-structure digests before it may
select a record.

## Compiled format

`RORMAT2` is endian-independent. Every integer is unsigned little-endian and
every string is a length-prefixed canonical UTF-8 byte sequence. The fixed
64-byte header is:

| Bytes | Meaning |
|---:|---|
| 8 | `RORMAT2` plus NUL |
| 2 | format version (`2`) |
| 2 | header size (`64`) |
| 4 | flags (must be zero) |
| 4 | record count |
| 4 | payload byte count |
| 32 | SHA-256 of the exact canonical payload |
| 8 | reserved (must be zero) |

The C++ parser recomputes the payload SHA-256, enforces exact payload
consumption, canonical record and texture-unit ordering, known enum values,
finite float32 bit patterns, cross-field references, and all hard/configured
caps. Unknown header extensions and trailing payload bytes fail closed. Runtime
parsing has no JSON or RapidJSON dependency.

Hard caps are 64 MiB per compiled catalog, 65,536 records, 32 texture units per
record, and 16 MiB of aggregate decoded string bytes. Configuration can only
tighten those limits.

## Runtime API and transaction boundary

`ParseOgre14LegacyMaterialSemanticCatalogV2` builds a candidate immutable
catalog. Allocation failure, unexpected exceptions, malformed input, or a test
fault leaves the prior catalog and shared owner unchanged.

`FindExact` performs a case-sensitive lookup on exact resource group and
material name. Once the content loader has independently matched all record
bindings, `BuildOgre14LegacyMaterialSemanticRegistryFromCatalogV2` converts the
explicit lowering fields to `Ogre14LegacyMaterialSemanticDeclaration` records
with `VERSIONED_COMPATIBILITY_TABLE` provenance. Registry construction remains
transactional. No runtime property is guessed during this conversion.

Each declaration receives a fresh opaque identity during immutable registry
construction. Resolution copies that non-mintable receipt. Pointer-exact
authentication therefore distinguishes declarations and fresh builds even
when all numeric revisions and the diagnostic content fingerprint are equal.
The native-capture coordinator can retain the issued resolution, re-resolve
the exact key immediately before publication, and require
`Ogre14LegacyMaterialSemanticResolutionAuthenticates`; forged, cross-record,
stale-build, or publicly mutated resolutions fail.

The cross-platform probe builds the compiler-generated synthetic fixture and
runs the C++ parser/registry gate on macOS, Windows, and Linux. Compiler tests
also run under normal and optimized Python to detect order-dependent behavior.
