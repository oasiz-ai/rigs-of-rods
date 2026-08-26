# Block-Compressed Textures

## The problem

The renderer had no block-compressed storage format. `TextureResourceFormat`
enumerated eight uncompressed formats; the DDS decoder understood BC1 through
BC5 but only in order to expand them to RGBA8; and the presenter uploaded only
RGBA8, RG8, and R8.

The measured consequence: 629 shipped texture files, about 89.3 MiB on disk,
inflating to roughly 713 MiB resident once decoded to RGBA8 and given mip
chains. An 8x blow-up, on a renderer that is GPU-bound.

That inflation, not the source art, was the binding constraint. Raising texture
resolution multiplies an already 8x-inflated footprint, so compression had to
land first.

## The format family

| Format | Block | Bytes/block | Bytes/texel | Role |
| --- | --- | --- | --- | --- |
| `BC1_UNORM` | 4x4 | 8 | 0.5 | Opaque colour |
| `BC3_UNORM` | 4x4 | 16 | 1.0 | Colour with interpolated alpha |
| `BC4_UNORM` | 4x4 | 8 | 0.5 | One channel (roughness, occlusion) |
| `BC5_UNORM` | 4x4 | 16 | 1.0 | Two channels (tangent-space normal XY) |
| `BC7_UNORM` | 4x4 | 16 | 1.0 | Colour, highest fidelity per byte |

`BC1`, `BC3`, and `BC7` may carry the sRGB transfer, because BC decode happens
in fixed-function hardware before filtering.

### Why BC3 and not BC7 for shipped terrain art

BC7 is the better format and it is fully supported on Metal, D3D12, and Vulkan.
It is nonetheless **not** usable for a texture that ships inside a terrain
archive on macOS, for a reason that is structural rather than incidental.

The combined runtime is two renderers. A hidden OGRE14 producer builds the
legacy scene; an Ogre-Next presenter draws it. Archive texture bytes flow
through **both**: `ContentManager` intercepts the stream open and hands the same
bytes to Ogre's own codec (producing the `Ogre::Texture` the producer inspects)
and to this project's decoder (producing what the presenter shows). The producer
hard-requires a successfully loaded texture — `PreflightTextureIdentity` checks
`isLoaded()` — and a failed load yields a blank layer and a fail-closed capture,
with no fallback.

On macOS the producer runs on GL3Plus, and macOS core profile caps at OpenGL
4.1. BC7 requires GL 4.2 or `ARB_texture_compression_bptc`. So any texture that
must survive both renderers is limited to the S3TC/RGTC set.

This costs less than it first appears: **BC3 is the same 1 byte per texel as
BC7**. The memory result is identical; only fidelity per byte differs. BC7
remains admitted end-to-end for content that reaches the presenter directly and
never passes through the producer.

## Pitch semantics

`TextureMipLevelDescriptor::row_pitch_bytes` is now the stride of one **block**
row, and `layer_pitch_bytes` must cover `ceil(height / block_height)` such rows.
For every uncompressed format the block is 1x1, so a block row is a texel row
and both fields keep their original meaning exactly. Partial edge blocks round
**up**: a 6x6 mip stores 2x2 whole blocks.

Because that reinterpretation is not detectable from the wire bytes,
`kTextureResourceDescriptorVersion` and
`kRenderAssetDeltaTransportTextureVersion` both moved 1 -> 2, in the lockstep the
existing `static_assert` requires. There is no partial-compatibility path: a
version-1 reader refuses a version-2 descriptor rather than misreading it.

## Why the CPU channel split had to stay for metallic-roughness

One RGBA8 `METALLIC_ROUGHNESS` texture is uploaded as two separate
single-channel GPU textures, by extracting green (roughness) and blue
(metallic). A channel cannot be read out of a BC block without decoding it, so
that slot **refuses every block format by name**:

> metallic-roughness cannot be block-compressed: the slot is channel-split into
> separate roughness and metallic textures, which requires addressable texels

Uploading the packed block to both roles would silently make metallic equal
roughness. Refusing is the honest outcome. Supporting per-texel roughness under
compression needs separate single-channel BC4 assets and separate slots to bind
them to; the storage format for that already exists here.

For every other role the split simply disappears — a BC5 normal map *is* two
channels, so there is nothing to extract.

## Per-slot admission

| Slot | Accepts |
| --- | --- |
| `BASE_COLOR`, `EMISSIVE` | RGBA8 / BC1 / BC3 / BC7, sRGB |
| `METALLIC_ROUGHNESS` | linear RGBA only — block formats refused by name |
| `NORMAL` | linear RGBA or BC5 |
| `SPECULAR` | linear RGBA / BC1 / BC3 / BC7 |
| `OCCLUSION` | linear; block-compressed must be BC4 |
| `DETAIL_WEIGHT` | linear RGBA only — block formats refused |
| `DETAIL0..3` | RGBA8 / BC1 / BC3 / BC7, sRGB |
| `DETAIL0_NM..3_NM` | linear RGBA or BC5 |

`DETAIL_WEIGHT` is closed to compression deliberately. BC fits two endpoints per
4x4 block, so a compressed weight mask bleeds layer selection across block
boundaries at precisely the layer edges the mask exists to define.

## Measuring it

`OgreNextN1TextureAllocationAudit` (version 3) gained
`resident_texture_bytes` and `block_compressed_allocations`.
`resident_texture_bytes` is summed from Ogre's own block-aware size arithmetic
over every live allocation in its actual uploaded format — measured, not
estimated. Before this field existed, none of the previous counters would have
moved when a texture changed format, so the whole change was unmeasurable.

## The content pipeline

Two steps, in order:

1. `tools/build_cityworld_local_overlay.py` derives overlay content and writes
   uncompressed PNG textures. Unchanged.
2. `tools/compile_cityworld_textures.py` compresses the colour textures to BC3
   DDS with complete authored mip chains, rewrites in-archive material
   references, and reports the digest to re-pin.

Keeping them separate keeps the authoring step lossless: the PNG stage remains
the master, and compression is a reproducible transform of it.

`tools/cityworld_block_compression.py` holds the encoders. It is standard
library only and every routine is a pure function of its input bytes, so
recompiling always produces identical output. It carries reference decoders and
a round-trip self-test:

```bash
python3 tools/cityworld_block_compression.py --self-test
```

### Encoder notes

Endpoints come from the block's **principal axis**, found by power iteration
seeded from the covariance row of largest variance. The cheaper bounding-box
diagonal is wrong in a way that matters: it can be exactly orthogonal to the
true axis whenever two channels vary in opposite directions, so the iteration
returns zero and keeps the bad seed. A block half `(200,30,30)` and half
`(30,30,200)` then collapses to a single average colour — and that pattern is
not exotic. Red mortar against grey brick, white lane markings on dark asphalt,
and coloured signage all produce it. Fixing the seed moved that case from
12.6 dB to 54.2 dB.

Mip chains are authored offline, in full to 1x1, because a compressed mip cannot
be derived from a compressed mip without decode/re-encode, which would compound
quantisation error at every level. sRGB textures are filtered in **linear
light**, matching the runtime's own sRGB mip rule, so an offline chain and a
runtime chain agree.

Measured round-trip quality on realistic content: BC1 34–42 dB, BC7 40–57 dB.
A 210-level ramp inside a single 4x4 block is measured separately at 22.8 dB and
barred there deliberately — BC1 has four palette entries, so that figure is
where the *format* sits, not where the encoder sits.

## Packaging

Any overlay content change must, in one commit, re-pin
`kCityWorldNextLocalOverlayArchiveSha256` and
`kCityWorldNextLocalOverlayArchiveBytes` in
`source/main/resources/LegacyMaterialCompatibilityPlan.h`. Without the re-pin the
runtime falls back to the ordinary unauthenticated mount and road captures fail
closed.
