# OGRE 14 terrain-composite transport receipt V2

`Ogre14TerrainCompositeCaptureReceipt` is an immutable, native-minted owner of
the exact OGRE 14 terrain composite texture and its complete mip chain. V2 is a
transport contract only. It proves texture bytes, identity/revision, the exact
bound texture-unit sampling facts, and direct scene fog mode. It makes no Pass,
RTShader, generated-program, lighting, shadow, blending, raster, framebuffer,
or final-pixel equivalence claim.

This narrow boundary is deliberate. The legacy shader graph cannot be
authenticated portably on every supported backend, while the composite texture
itself is the useful cutover artifact. The follow-on MaterialDescriptor V3 and
OgreNext integration own target execution semantics.

## Public API and publication

Production exposes one native entry point:

```cpp
ValidationResult Ogre14TerrainCompositeNativeAdapter::Capture(
    Ogre::TerrainGroup&, std::int32_t slot_x, std::int32_t slot_y,
    const Ogre14TerrainCompositeCaptureConfiguration&,
    Ogre14TerrainCompositeCaptureReceipt& output);

```

The production Capture exposes no callback. Fault injection exists only behind
`ROR_OGRE14_TERRAIN_COMPOSITE_CAPTURE_INTERNAL_TESTING`.

All validation, allocation, readback, hashing, and final invariant checks occur on a
candidate owner. A failed capture or lowering leaves the caller's output
unchanged. Copies share one `std::shared_ptr<const State>`; no mutable byte view
is published.

The caller serializes terrain, texture, texture-unit, sampler, scene-fog, and
resource mutation on the OGRE render/resource thread.

## Exact native identity and revision

The receipt records and reobserves around readback:

- exact TerrainGroup, slot, Terrain, packed key, signed coordinates, resource
  group, page-definition kind/source, generated save name, material name,
  alignment, size, world size, world position, loaded state, and derived-data
  update state;
- exact composite `Texture*`, resource handle/name/group, usage, loaded/manual
  state, `PF_BYTE_RGBA`, 2D extent/depth/face count, hardware-gamma flag, and
  nonzero `Texture::getStateCount()` revision;
- exact `HardwarePixelBuffer*` identity and tight layout for every mip;
- exact SceneManager, TextureUnitState, Sampler, and bound Texture pointer
  identities plus all admitted sampling facts below.

The existing pinned OGRE patch advances the composite texture state count only
after successful cached/initial upload and after successful composite RTT
completion. An unchanged revision is therefore meaningful after
`Terrain::updateCompositeMap()`; an exception cannot publish a revision for
unfinished content.

## Complete mip authority

Pinned OGRE 14.5.2 `Texture::getNumMipmaps()` means levels additional to level
zero. V2 stores both that raw count and total levels. The only admitted chain is
the exact contiguous `0..N` sequence through 1x1, with dimensions halved by
`max(1, dimension / 2)` at each level. Counts and `size_t` dimensions are
checked against configured/hard caps before reserve, allocation, or
`Texture::getBuffer()`.

Every mip is copied as tight `PF_BYTE_RGBA` in OGRE PixelBox row-zero-first
order with no vertical flip. Arbitrary alpha is preserved at every level.

Digest domains include their terminating NUL:

- `RoR/Ogre14/TerrainComposite/MipRGBA/v2\0`
- `RoR/Ogre14/TerrainComposite/FullMipChain/v2\0`

One mip digest hashes, in order: domain, little-endian mip/width/height,
little-endian byte count, then exact RGBA bytes. The chain digest hashes:
domain, little-endian level count and total byte count, then for each level its
little-endian mip/width/height/slice-byte-count and 32-byte mip digest.

The canonical 4x4/2x2/1x1 fixture freezes:

- mip zero SHA-256:
  `3aec02604f0b038623334e3dc92086cab92ed3ec65661bd2a974956ca0dd801a`
- full-chain SHA-256:
  `21374ae51920d5e01d2ab2eddbb634a250793b936b8cc5be6f1337fbd003ee64`

## Texture-unit, UV, sampler, gamma, and fog facts

Technique one/pass zero is used only as the legacy container path to acquire
texture-unit zero. No fact about that Pass's execution is retained.

The exact texture unit must be named, have exactly one current frame at index
zero, be 2D, be neither blank nor load-failing, bind the exact captured Texture
and Sampler pointers, and expose unordered-access mip level `-1`.

The only admitted coordinates are UV set zero with `TEXCALC_NONE`, an empty
texture-effect inventory, `+0` scroll/rotation, `+1` scale, and the exact
identity 4x4 transform. Float and matrix comparisons are bitwise; `-0`, NaN
payload changes, and other ambiguous representations do not compare equal.

The source sampler is exactly:

- clamp U/V/W;
- linear minification and magnification;
- point mip selection;
- anisotropy 1;
- mip bias `+0`;
- comparison disabled with pinned dormant `GREATER_EQUAL` function;
- OGRE `ColourValue::Black` border bits `{+0,+0,+0,+1}`.

The Texture and TextureUnitState hardware-gamma flags are observed separately
through public APIs and must agree. No output-target or shader-generator gamma
fact is inferred.

`SceneManager::getFogMode()` is recorded directly as `FOG_NONE`, `FOG_EXP`,
`FOG_EXP2`, or `FOG_LINEAR`. Capture transports all four. Opaque target lowering
admits only `FOG_NONE`; it does not pretend to reproduce a legacy fog response.

## Semantic classification and CPU oracle

RGB is baked diffuse output, not an albedo layer to light again. Alpha is
arbitrary `LINEAR_SPECULAR_MASK` evidence and must not be interpreted as
coverage, opacity, metallic, roughness, or another PBR channel.

Transfer is derived only from exact Texture/TUS gamma agreement:

- `DECODE_BEFORE_FILTER`: decode stored RGB before bilinear filtering; alpha
  remains linear UNORM;
- `LEGACY_UNORM_DISPLAY_DOMAIN`: bilinearly filter stored RGB bytes in their
  display-domain UNORM encoding; alpha remains linear UNORM.

Both modes are valid capture and lowering inputs. The renderer-neutral CPU
oracle freezes their different ordering on non-endpoint midtone texels; it also
proves alpha is identical between modes. V2 never relabels the legacy path as
sRGB texture storage.

## Transfer-preserving opaque lowering

`LowerOgre14TerrainCompositeOpaque()` transactionally creates an owned,
renderer-neutral intermediate for the follow-on MaterialDescriptor V3:

- every mip remains tight and contiguous;
- every RGB byte is copied exactly;
- every destination alpha byte is forced to 255;
- the exact source transfer enum is retained;
- the source receipt and original spec-mask alpha remain unchanged;
- UV0 identity is retained;
- the portable sampler is linear min/mag, nearest mip, clamp-to-edge,
  non-anisotropic, non-comparison, with maximum LOD exactly the final mip index.

The source border `{+0,+0,+0,+1}` is retained in receipt authority. Because
clamp-to-edge cannot sample a border, lowering deliberately canonicalizes the
portable descriptor's inert border to `Float4{}`. This is documented
normalization, not a claim that the source border alpha was zero.

The intermediate does not contain a current `TextureResourceDescriptor` or
`MaterialDescriptor`: neither can express
`LEGACY_UNORM_DISPLAY_DOMAIN` filter-then-decode semantics. MaterialDescriptor
V3 will map the explicit transfer to
`SRGB_DISPLAY_DOMAIN_FILTER_THEN_DECODE`; the OgreNext frontend will upload
UNORM and apply EOTF after filtering. That integration is outside this slice.

## Fresh readback is mandatory

Every capture brackets and reads every mip, then recomputes every digest before
publishing a new immutable owner. There is no prior-receipt or revision-equal
reuse overload. `Texture::getStateCount()` authenticates the known terrain
update paths, but pinned public pixel-buffer write paths do not all advance the
owning Texture revision. Identity and revision equality therefore cannot prove
that retained CPU bytes are current, even when mutation is serialized. V2 fails
closed by performing the full readback rather than assuming an exclusive writer.

## Existing pinned OGRE package evidence

This slice adds no OGRE recipe seam and changes no Conan lock or dependency
revision. It relies on the already-pinned terrain revision/full-mip/Metal
readback patch. The relocated Metal package probe proves texture upload,
automatic mip regeneration, per-mip selection, row order, arbitrary alpha,
readback rollback, and state-count advancement. It is a texture/readback probe,
not a live terrain shader or framebuffer-equivalence gate.

The transport NativeAdapter and native test remain backend-neutral OGRE code.
The native test compiles against real pinned OGRE 14.5.2 arm64 and exercises a
real CPU `HardwarePixelBuffer`, distinct all-mip identities/readback, texture
unit/sampler public APIs, and opaque lowering. It does not claim a live Metal
CityWorld capture.

## Gates and residual work

Required gates for this slice are:

- strict Release and Debug pure receipt tests;
- ASan+UBSan receipt tests;
- real pinned OGRE 14.5.2 arm64 NativeAdapter/native-test compilation and run;
- Python static contract in normal and `-O` modes;
- recipe graph/provenance checks confirming the existing patch and zero recipe
  drift;
- cross-platform source provenance through the existing Linux/macOS/Windows
  probe manifests.

The remaining cutover work is MaterialDescriptor V3 transport/wire/frontend
integration, OgreNext UNLIT/OPAQUE realization, a live CityWorld capture and
comparison, and only then any default switch. This V2 receipt intentionally
does not edit GfxScene, OgreNext frontend code, assets, worldmodel, or recorder.
