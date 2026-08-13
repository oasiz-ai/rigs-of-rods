# Native render asset package v1

`.rornative` is the forward-native, renderer-neutral package boundary for V2
visual content. It is an offline product of reviewed glTF and explicit
texture/material declarations. It is not an OGRE mesh, material script, ODEF,
terrain format, or serialized `RenderAssetDelta`.

## Scope

Version 1 carries immutable static triangle meshes, uncompressed textures,
explicit samplers, `MaterialDescriptor` v4-equivalent declarations, and finite
invertible-affine static instances. Its canonical runtime result is a sorted set of
`GraphicsSceneAssetInput` and `GraphicsSceneStaticMeshInput` values. Material
asset references remain canonically absent in the descriptor; the package's
source texture/sampler identities populate `material_bindings`, so the joined
scene producer remains the sole owner of runtime `RenderAssetId` and revision
assignment.

Version 1 deliberately does not represent collision, terrain ownership, mesh
LODs, ambient-occlusion evidence, skeletal/deformable geometry, particles, or
streaming updates. Those features require later versioned records and their own
acceptance gates. A visual-only package must not be treated as a physics or
terrain resource.

## Authoring contract

`tools/validate_native_render_asset.py` validates a
`ror-native-render-source-v1` declaration and its hash-pinned inputs. The source
GLB is glTF 2.0 with one embedded buffer, one default scene, applied transforms,
and one indexed TRIANGLES primitive per mesh. Every vertex supplies finite
float32 `POSITION`, `NORMAL`, `TANGENT`, and `TEXCOORD_0` streams. Normals and
tangents are unit length and orthogonal, tangent `w` is exactly `-1` or `1`, and
each tangent is derived from the triangle UV derivatives toward increasing U.
For every triangle the validator independently reconstructs increasing-V as
`B = w * cross(N, T)`; an opposite-U tangent or flipped handedness fails closed.
The normalized geometric face normal from counter-clockwise index winding must
point into the positive hemisphere of every authored vertex normal; reversing
only the indices therefore fails closed even when the UV-derived tangent basis
would remain unchanged. Front faces are counter-clockwise in the renderer
contract's right-handed, Y-up, metre basis. JSON and buffer layout are canonical
and unsupported glTF extensions, sparse accessors, morphs, skins, animations,
cameras, lights, and implicit transforms fail closed.

Textures are hash-pinned, top-left-origin, uncompressed 32-bit TGA mip levels.
Every mip is authored explicitly through the complete chain. Base-color and
emissive roles are sRGB; normal, metallic/roughness, occlusion, and specular
roles are linear. A material names every factor, alpha/depth state, transform,
texture role, and sampler. No gamma, alpha, mip, filter, wrap, comparison, or
anisotropy state comes from a renderer default.

Every source number that reaches a package float must survive a finite
IEEE-754 binary32 round trip before semantic range checks. Negative zero,
overflow, underflow-to-zero, and attacker-sized integers are rejected with
stable diagnostics. Material workflow-unused factors are canonical, and a
texture scale component must remain nonzero after the binary32 round trip.
Static-instance affine and invertibility checks use the same ordered binary32
3x3 determinant expression as `RenderMath`; an absolute determinant equal to
`1e-8F` is rejected, not rounded or re-evaluated in binary64.
The validator caps aggregate assets at 4096 and preflights the complete
declared TGA-source plus decoded-RGBA working set before opening or decoding the
first texture mip.

`tools/compile_native_render_asset.py` validates first and lowers those inputs
directly into the package. Stable 64-bit source identities are the first eight
bytes of SHA-256 over the versioned package/kind/logical-name domain. A zero or
collision is rejected. Compilation does not invoke OGRE or write `.mesh`,
`.material`, or `.odef` files.

## Binary container

All integers and IEEE-754 binary32 values are little-endian. The fixed 80-byte
header is:

| Offset | Type | Meaning |
| ---: | --- | --- |
| 0 | `u8[8]` | `RORNAT1\0` magic |
| 8 | `u32` | container version, exactly `1` |
| 12 | `u32` | header bytes, exactly `80` |
| 16 | `u32` | flags, exactly zero |
| 20 | `u32` | total record count |
| 24 | `u32` | asset-record count |
| 28 | `u32` | static-instance count |
| 32 | `u64` | exact package byte count |
| 40 | `u8[32]` | SHA-256 of bytes `[80, package_size)` |
| 72 | `u8[8]` | reserved, exactly zero |

Each record begins with `u32 type`, zero `u32 flags`, `u64 source_identity`,
and `u64 payload_bytes`. The first record is the canonical ASCII provenance
manifest (type 1, identity zero). Asset records follow in strictly increasing
source-identity order and use types 2 mesh, 3 texture, 4 material, and 5
sampler. Static instances (type 6) follow in strictly increasing object-source
identity order. Record payloads have no alignment padding and the final record
ends exactly at the declared package size.

Descriptor payloads encode every version, enum, state value, count, and byte
array needed to construct the renderer-neutral descriptors. Strings are a
`u32` byte count followed by canonical printable ASCII. Mesh arrays are tightly
packed binary32 and `u32` indices. Texture mip records contain exact dimensions,
row/layer pitches, and bytes. Material records end with six ordered
`(texture_source_id, sampler_source_id)` pairs in `MaterialTextureSlot` order.
An absent pair is `(0, 0)`; half-absent pairs are malformed.

### Record payload layouts

Asset payloads 2 through 5 start with their descriptor version and a logical
name encoded as `u32 name_bytes` plus those printable-ASCII bytes. Type 6 starts
with its version but carries identity only in the record header. The remaining
fields are:

| Type | Version | Ordered fields after the asset name, or after the type-6 version |
| ---: | ---: | --- |
| 2 mesh | 1 | `u8 topology`, `u8 index_format`, `u8 dynamic`, zero `u8`; `u64 topology_revision`; `float3 bounds_min`, `float3 bounds_max`; eight `u32` counts for position, normal, tangent, velocity, UV0, UV1, color, index; then tightly packed streams in that order |
| 3 texture | 1 | `u8 type`, `u8 format`, `u8 color_space`, zero `u8`; `u32 width`, `height`, `array_layers`, `mip_count`; for each mip: `u32 width`, `height`, `u64 row_pitch`, `layer_pitch`, `byte_count`, then exact RGBA bytes |
| 4 material | 4 | eight state bytes; 17 binary32 factors; six 44-byte bindings in base-color, metallic/roughness, normal, occlusion, emissive, specular order |
| 5 sampler | 1 | three filter bytes, three address bytes, zero `u16`; three LOD floats; `u8 anisotropy_enabled`, three zero bytes, anisotropy float; `u8 compare_enabled`, compare byte, zero `u16`; `float4 border_color` |
| 6 static instance | 1 | `u64 mesh_source_id`, `u64 material_source_id`; column-major `float4x4 render_from_object`; `u32 visibility_mask`; `u32 instance_flags` |

Mesh v1 fixes topology to triangle list `0`, `dynamic=0`, and topology revision
`1`. Index format is the smallest valid descriptor token (`uint16=0`,
`uint32=1`), while the portable package stream itself stores every index as
`u32`. Texture v1 fixes type to 2D `0`, format to RGBA8 UNORM `2`, and array
layers to `1`; color space is linear `0` or sRGB `1`.

Material state tokens are model PBR `0`/unlit `1`; workflow
metallic/roughness `0`/specular `1`; blend replace `0`, straight-source-over
`1`, or legacy-straight-alpha `2`; alpha test disabled `0`, greater `1`, or
greater-equal `2`; and base-color transfer decode-before-filter `0` or
display-domain-filter-then-decode `1`. The remaining state bytes are
double-sided, depth-write, and zero reserve. The factors are base-color RGBA,
metallic, roughness, specular RGB, normal scale, occlusion strength, emissive
RGB, emissive strength, alpha cutoff, and index of refraction.

Each material binding is `u64 texture_source_id`, `u64 sampler_source_id`,
`u8 uv_set`, seven zero bytes, then binary32 scale XY, offset XY, and rotation.
Sampler filters are nearest `0`/linear `1`; address modes are repeat `0`,
mirrored-repeat `1`, clamp-to-edge `2`, clamp-to-border `3`; compare operations
are the ordered tokens never through always, values `0..7`. Boolean bytes are
exactly `0` or `1`. All reserve bytes are zero, and every float is finite with
negative zero rejected.

Each source mesh declares a sorted, duplicate-free `instance_flags` array. The
only v1 tokens are `casts_shadow` (bit 0), `receives_shadow` (bit 1), and
`visible_in_reflections` (bit 2). The compiler writes that exact bit set into
the type-6 instance payload; an empty array is a deliberate shadow/reflection
negative control, while any unknown bit fails closed in the decoder.

The embedded provenance manifest binds the source declaration, composition
descriptor, GLB, authoring generator, compiler and both validator dependencies,
origin class, license, explicit capability non-claims, logical names, derived
source identities, instance flags, and all ten exact decoded counts. Its JSON
is ASCII, key-sorted, whitespace-free, and byte-bounded. The C++ decoder parses
the schema rather than only its JSON shape, recomputes every source identity,
and compares each manifest entry to the decoded record type, payload name, and
instance policy. Version 1's manifest numeric fields are non-negative integer
counts; alternate or overflowing number spellings are not accepted.
Manifest paths are relative portable POSIX paths. Empty, `.`, and `..` segments,
leading `/`, and backslashes fail closed in both the source validator and C++
decoder; neither layer normalizes a non-canonical path before accepting it.

## Runtime validation and ownership

`NativeRenderAssetPackage` is OGRE-free. Decoding first verifies a mandatory
caller-supplied SHA-256 of the complete package, then verifies the header and
body digest before interpreting records; checks every length and count before
allocation; rejects unknown flags, enums, versions, non-finite values,
non-canonical order, duplicate identities, dangling bindings, and incompatible
mesh/material or texture/material pairs; and publishes only a
`shared_ptr<const NativeRenderAssetPackage>` after the complete candidate is
valid. Failure returns no partial package.

The decoder copies package bytes into immutable descriptor owners. It performs
no GPU upload, texture readback, resource fallback, legacy inference, or
renderer-specific conversion. The scene producer and asset registry retain
their existing transaction, revision, retirement, and replay authority.

The body SHA-256 is an integrity checksum, not a signature or origin authority.
The decoder has no unpinned overload: a caller supplies the checked full-package
SHA from the workflow/ledger gate. Only after that authentication succeeds does
the decoder expose the schema-validated origin plus compiler, generator, GLB,
composition, and source-manifest hashes. A caller must not derive that trusted
digest from the same untrusted bytes it is attempting to authenticate.

## Determinism and gates

- Identical validated sources and tool revisions produce byte-identical
  packages and canonical reports under normal and optimized Python.
- The checked A0 road-tile fixture is regenerated and compared byte-for-byte on
  macOS, Linux, and Windows CI.
- Hostile Python and C++ tests mutate lengths, hashes, order, identities,
  descriptors, references, texture roles, glTF structure, and source paths.
- Provenance validation binds every source and output hash and records the
  fixture as `project_original` without adding a legacy-conversion ledger row.

The checked `NATIVE-A0-001` fixture is a 6 m by 12 m visual-only
lighting-response coupon. It contains high-roughness non-metal asphalt with
authored base-color, normal, and metallic/roughness mip chains; a distinct
low-roughness wet strip with base-color, normal, and linear specular maps; an
alpha-tested lane; specular/emissive road reflectors; and a project-original
1.45 m two-post/crossbar shadow gate. The rough and wet surfaces are
receiver-only and reflection-visible; the gate is caster/receiver and
reflection-visible; and the thin alpha lane and reflector quads are deliberate
RT-inert negative controls. They remain raster-visible through the all-bits
visibility mask, but do not cast, receive, or enter reflection acceleration
structures in v1. The fixture therefore gates the complete instance-flag value
set to exactly `{0, 6, 7}`: inert lane/reflector `0`, dry/wet receivers `6`, and
the raised gate caster/receiver `7`.

The checked machine-readable composition is
`content-source/native_render/a0_road_tile_12m/rorng_a0_road_tile_12m.composition.json`.
It pins world AABB `[-3, 0, -6]` to `[3, 1.45, 6]`, camera position
`(8, 7, 10)`, target `(0, 0, -0.2)`, vertical FOV `50 degrees`, near/far
`0.1/50 m`, and a D65 directional key pointing
`(0.60, -0.64, 0.48)` toward the scene at `110000 lux`. Use neutral white
balance at `6500 K` and exposure `EV100 14`; set the checked analytic clear-sky
environment/background luminance to `2000 cd/m^2`. This oblique framing is
chosen to place a dry/wet highlight
boundary in view while the
gate's crossbar throws an oblique shadow across both receiver materials, the
lane alpha edge remains legible, and the emissive reflectors retain a hot core.
For the exact directional projection of the complete gate AABB onto `y=0`, the
checked ROI is `x=[-1.8, 3.159375]`, `z=[-1.65, -0.2625]`; that interval
straddles the wet-strip edge at `x=0.35` and is the deterministic RT Off/On
region of interest. The descriptor also hashes a deterministic 640x360 PPM
layout preview containing dry road, wet highlight guide, lane alpha pattern,
reflectors, gate, and projected shadow. That preview is explicitly labeled
`authoring-layout-preview-not-renderer-evidence`; it removes framing guesswork
but is not a renderer-quality claim.

These are exact PBR scene inputs ready for a renderer lighting pass, not
evidence that a renderer capture, ray-traced reflection, AO, LOD, collision,
or native terrain has passed.
