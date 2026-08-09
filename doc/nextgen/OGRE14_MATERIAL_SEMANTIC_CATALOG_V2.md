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

- package/archive SHA-256, exact resource group, and stable reviewed
  package/catalog revision in domain `REVIEWED_PACKAGE_REVISION_V1`;
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

For format version 2, `resource_generation` is the stable reviewed revision
above. It is never ContentManager's mount-order-dependent runtime resource-group
generation nonce, and the two values must not be compared numerically. There
was no production v2 activation before this definition; live runtime admission
retains both values as distinct domains. See
`OGRE14_MATERIAL_SEMANTIC_RUNTIME_ADMISSION.md` for that authority boundary.

## Canonical native structure digest v1

`CaptureOgre14LegacyNativeMaterial` now mints the record's native side of that
last comparison directly from the pinned OGRE 14.5.2 object graph. The
canonical byte stream starts with the eight bytes `RORNMD1\0`, followed by a
little-endian `uint32` serialization version (`1`). Strings are exact
little-endian-`uint32`-length-prefixed bytes; booleans are `uint8`; other
integers use their named unsigned width; and finite IEEE-754 float32 values are
written as their exact little-endian bit patterns. The complete stream is
bounded to 64 KiB before SHA-256 is computed.
Both shipping native-authentication translation units override the game's
Release fast-math defaults with strict floating-point compilation on MSVC,
Clang, and GCC, so non-finite rejection and exact bit encoding remain active
in production builds.

Fields occur in native order and cannot be sorted or deduplicated:

- exact material resource group/name, receive-shadow and
  transparency-casts-shadow policy, and technique count;
- every admitted technique ordinal, exact scheme and LOD, support result,
  hardware-rule counts, exact custom caster/receiver declaration names and
  resolved-material presence, and pass count;
- every admitted pass ordinal, program-stage presence, lighting and fixed
  function colors, the complete catalog-v2 blend/write/depth/bias/cull/alpha/
  fill/iteration facts, and every additional v1 default-state gate for vertex
  color, shading, lights, fog, polygon override, clipping, sorting, line, and
  point rasterization;
- every admitted texture-unit ordinal/name and exact texture group/name,
  content/type/UV/frame/projective/effect/environment/UAV/gamma state, exact
  4-by-4 texture matrix, color and alpha combine type/operation/sources,
  complete sampler facts, and render-target policy.

The exact v1 sequence is below. `str` means the length-prefixed byte string
defined above, `bool8` is exactly `0` or `1`, `enum8` is the pinned OGRE or
portable enum's nonnegative numeric value, and `f32` is the finite exact-bit
encoding defined above. Counts precede their records; v1 admits exactly one
technique, one pass, and zero or one texture unit, but still encodes every
count and ordinal so a prefix cannot stand for a larger graph. Pass-pipeline
and sampler `enum8` fields use their portable `Ogre14Legacy*` ordinals;
pass-default, texture-unit, and combine `enum8` fields use the pinned OGRE
14.5.2 ordinals.

| Section | Exact field sequence |
|---|---|
| Prefix | magic `[8]`, serialization version `u32` |
| Material | group `str`, name `str`, receive shadows `bool8`, transparency casts shadows `bool8`, technique count `u32` |
| Technique | ordinal `u32`, scheme `str`, LOD `u16`, supported `bool8`, GPU-vendor rule count `u32`, GPU-device rule count `u32`, exact custom caster name `str`, exact custom receiver name `str`, resolved caster present `bool8`, resolved receiver present `bool8`, pass count `u32` |
| Pass programs and lobes | ordinal `u32`; vertex, fragment, geometry, hull, domain, and compute program presence `bool8[6]`; lighting enabled `bool8`; diffuse `f32[4]`; ambient, specular, and emissive `f32[3]` each; shininess `f32` |
| Pass pipeline | source/destination color and alpha factors `enum8[4]`, color/alpha operations `enum8[2]`, write mask `u8`, depth check/write `bool8[2]`, depth compare `enum8`, constant/slope/iteration depth bias `f32[3]`, cull/manual cull/alpha reject `enum8[3]`, alpha reject value `u8`, alpha-to-coverage and solid-fill `bool8[2]`, pass iteration count `u32` |
| Pass default gates | vertex-color tracking and shading `enum8[2]`; maximum/start light `u16[2]`; light mask `u32`; iterate-per-light and one-light-type-only `bool8[2]`; only-light type `enum8`; lights per iteration `u16`; fog override, polygon overrideable, light scissor, and light clip planes `bool8[4]`; illumination stage `enum8`; transparent sorting enabled/forced `bool8[2]`; line width and point size `f32[2]`; point sprites and point attenuation `bool8[2]`; point attenuation constant/linear/quadratic and point minimum/maximum size `f32[5]`; texture-unit count `u32` |
| Texture unit | ordinal `u32`, unit name `str`, resolved texture group/name `str[2]`, content/type `enum8[2]`, UV set `u8`, frame count/current frame `u32[2]`, projective `bool8`, effect count `u32`, environment-map present `bool8`, UAV mip (`u32`, canonical `0xffffffff` for native `-1`), gamma `f32`, row-major 4-by-4 texture transform `f32[16]` |
| Each color/alpha combine | blend type, operation, source one, and source two `enum8[4]`; source-one-manual, source-two-manual, and manual-operation-active `bool8[3]` |
| Sampler and target | min/mag/mip filters `enum8[3]`, U/V/W addressing `enum8[3]`, mip bias/minimum LOD/maximum LOD `f32[3]`, anisotropy `u32`, comparison enabled `bool8`, comparison operation `enum8`, border color `f32[4]`, render target `bool8` |

The sampler minimum LOD is the extractor's canonical zero. Maximum LOD is
zero when mip filtering is disabled and otherwise the last captured mip
ordinal. Those are deterministic normalized capture facts derived from the
exact native texture mip inventory; OGRE 14 has no independent sampler
min/max-LOD fields to read or guess.

The pinned OGRE recipe exposes the exact retained caster/receiver declaration
names because the stock pointer getters cannot distinguish absent state from an
unresolved authored name. V1 requires both names empty and both pointers null.
It also requires inactive `only-light-type` state to retain OGRE's canonical
`LT_POINT`; stale or unknown inactive enum values fail closed.

The v1 extractor rejects extra techniques, passes, and units, non-canonical
combine/environment/shadow state, null sampler owners, unknown enums,
non-finite values, or any
structure that cannot fit this format; it never hashes a supported prefix.
OGRE leaves inactive manual combine union members indeterminate, so the
serializer never reads or invents them. V1 admits only
`texture * current` color and alpha combines and encodes the operation/source
facts plus explicit absence of active manual operands. A future format must
version and encode active manual operands before accepting them.

Catalog texture semantic, color-role, swizzle, environment augmentation, and
scene shadow-technique declarations remain authored evidence, not native
`Material` facts. In particular, PSSM versus stencil is scene-level state and
cannot be inferred from `Ogre::Material`; runtime admission must authenticate
those catalog/scene inputs separately. `RORNMD1` excludes decoded
texture pixels and archive/source authority; the enclosing capture receipt separately
binds the decoded pixels and exact loaded-resource authority as described
below.

Capture runs on the serialized OGRE resource/render owner thread with material,
technique, pass, texture-unit, sampler, and texture mutation excluded for the
whole call. It serializes one direct native observation before readback, then
two direct native observations after readback. The last observation omits the
synthetic-only declaration-digest callbacks, which are compiled exclusively
under `ROR_OGRE14_NATIVE_MATERIAL_DECLARATION_DIGEST_TESTING`; production
capture exposes no declaration-digest callback surface. All three versions and
SHA-256 values must agree before
publication. This catches setters which do not dirty `Material::mStateCount`
and prevents a stale or hybrid declaration assembled from earlier portable
capture fields.

The opaque receipt also authenticates a renderer-neutral `RORNCP2` version-2
projection of every mutable public capture field: material identity/revision,
program and lobe state, full pipeline, every exact texture-unit name and
sampler field,
texture metadata and exact mip layout, a SHA-256 child digest of every mip byte
vector, the native declaration version/digest, and authenticated-resolution
count. The receipt privately retains the exact loaded-resource authority:
registry and source-receipt control blocks, resolver identity, and loaded
revision. Copying a receipt onto a capture with altered diffuse, sampler, mip,
resolution, order, or identity state therefore fails before runtime admission.

Successful capture publishes serialization version 1 and the 32-byte digest,
then mints a version-2 opaque
`Ogre14LegacyNativeMaterialAuditReceipt`. That receipt binds the digest and
serialization version to the exact immutable native-audit pointer and shared
ownership control block. Copying preserves authority; altering the digest,
reboxing an equal audit, substituting a translated owner, or mixing two
fresh native captures fails authentication. Allocation, cap, OGRE, and
unexpected failures leave the caller's complete prior capture and owners
unchanged. The 32-byte digest itself is a value: copying identical bytes is
equivalent, while changing any byte fails.

Runtime admission must keep the authorities in this order: capture through
`CaptureOgre14LegacyNativeMaterial`; require
`native_material_audit_receipt.Authenticates(native_capture)`; require the
selected catalog record's
`native_structure_sha256` to equal that authenticated digest; then
independently authenticate archive/script/repair provenance, authored
semantic/environment/shadow declarations, texture resolution, and exact
source-byte authority before publishing a prepared material. Decoded pixels
are already covered by the receipt's authenticated `RORNCP2` projection. The
digest/receipt gate is a prerequisite for that sequence, not permission to
skip any later authority and not live `GfxScene` wiring.

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
