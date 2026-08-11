# Disposable OgreNext game demo bridge

This path exists only to get the first honest CityWorld/Alexis game frame into
the product OgreNext renderer. It is named `OgreNextDemo` in `GfxScene` and is
not a public terrain API, a semantic-admission branch, or a compatibility
promise. Delete or replace it as native OgreNext terrain and full material
translation arrive.

The intended demo launch is:

```text
-checkcache -map CityWorld.terrn2 -truck AlexisSaber.truck -enter
```

On macOS, a package configured with `ROR_OGRE_NEXT_DEMO_ADMISSION=ON`
applies that exact launch only when Finder supplies no arguments. It also
requires OgreNext with PSSM, so the demo cannot silently reopen the legacy
renderer. Any explicit command-line argument preserves the ordinary launcher
contract unchanged. The staged app is visibly named `Rigs of Rods OgreNext
Demo` and uses a distinct bundle identifier so it cannot be confused with an
installed legacy `RoR.app` by LaunchServices.

## Deliberate lowering

- Each loaded terrain page keeps the existing CPU geometry extraction. Its
  active OGRE 14 composite is synchronously updated and freshly read back on
  the first accepted capture of each map generation. The adapter reobserves
  exact terrain, texture, TUS0, sampler, UV, gamma, fog, and native mip
  identities after readback before publishing.
- The native RGBA8 base level keeps byte-identical RGB and receives opaque
  alpha. Every tail mip is generated through 1x1 with the deterministic integer
  2x2 display-domain box rule. This unconditional cross-platform rule avoids a
  pinned OGRE 14 Metal bug that aliases nonzero `blitToMemory` requests to mip
  zero. The portable sampler is clamp, linear min/mag, nearest mip,
  non-comparison, and anisotropy one.
- OGRE derived terrain work is joined at the capture boundary because the
  product bridge skips the legacy render traversal that normally pumps its
  main-thread WorkQueue responses.
- An internal map-scoped material source projects the first authored
  technique's ordinary TUS0 of a narrowly eligible opaque non-terrain pass into
  conventional sRGB rough-dielectric PBR. For an authenticated package it
  resolves the exact retained source receipt and decodes bounded DDS, PNG, or
  JPEG bytes directly; source selection, decode, and authority failure reject
  the joined capture and never fall back to GPU readback. Ordinary packages
  retain an explicitly counted mip-zero native-readback path. Both routes
  preserve RGB, force opaque alpha, and generate the complete tail in linear
  light before sRGB re-encoding. Exact texture, sampler, and material owners
  are cached for the map generation; no nonzero native mip is read through the
  pinned Metal path. General content requires one pass, one texture unit, and
  no authored GPU program. It does not consult OGRE's mutable active-scheme
  `getBestTechnique()` selection. An exact Alexis Saber opaque-material
  allowlist may deliberately use only pass zero's diffuse TUS0 while
  discarding the known additive specular pass; transparent lens and window
  materials are not on that allowlist.
- Textured or programmed materials outside that opaque TUS0 subset remain one
  neutral matte while keeping real mesh geometry, culling, visibility,
  transforms, and FlexBody deformation. This includes transparent glass,
  alpha-tested foliage, animated/projective units, nonidentity UV transforms,
  and unavailable texture data. Alexis texture-blend colors are dropped. Its
  authored zero-alpha, no-depth-write `invisible` cab section is omitted instead
  of being made visible.
- Every private non-terrain mesh is normalized to RT4's position, normal,
  tangent, UV0 layout. Missing UV0 becomes zero; finite nonzero normals are
  normalized while absent, zero, or non-finite normals become deterministic
  +Y; tangents are rebuilt from that sanitized direction (including every
  dynamic update). Color, UV1, and velocity streams with no demo consumer are
  dropped. Terrain uses the same private basis sanitation on a copied payload
  while retaining its authored positions and complete UV0.
- CityWorld's six `topeQr.mesh` instances with exact derived scale
  `(1, 0.5, 0.5)` are explicitly omitted because the current RT4/PSSM tier
  rejects non-uniform instance transforms. This omission is bounded and logged
  once; it does not generalize transform admission.
- The captured frame is normalized, without mutating OGRE or user settings, to
  the active child surface, a 0.5/350 clip range, and the exact visible
  cast-shadow directional `Terrain::getMainLight()` required by PSSM.

## Transaction and performance boundary

Terrain and projected material assets use private, domain-separated source IDs
with bidirectional collision checks. Terrain, static, dynamic, and projected
material candidates commit or discard as one joined capture; source dependency
ID collisions reject instead of aliasing unrelated payloads. No payload is
trusted solely from `Texture::stateCount`, and no receipt, digest, or audit type
is added to a public header.

The first joined material observation fixes whether each exact static or
dynamic section remains on the generic factor path or enters the private
matte/projection path, together with material name, UV layout, and cull.
Generic factor values remain owned by the existing inventory. A later mode,
material-name, UV-layout, cull, or projected native-authority transition
rejects the joined capture; the bridge retains the last accepted frame rather
than toggling a live object between identities.
The projected source revalidates authored technique 0/pass 0/TUS0 pointers,
shape, sampling, texture storage, and material factors directly. It deliberately
does not treat the enclosing `Material::getStateCount()` as source authority:
pinned RTSS appends and loads a derived destination technique after the first
capture, which reloads the whole Material resource without changing the
authored pass being projected.
Projected material, texture, and sampler owners remain published through actor
destruction until the ordered map reset, so a later respawn never resurrects a
retired source ID.
Only current frame-reachable authenticated projections must fresh-resolve their
receipt and pass one common final authority snapshot before publication.
Unreachable cached owners are immutable anti-tombstone catalog entries; a
same-map bundle reload cannot make one reachable again without fresh exact
authority. Missing, revoked, or changed authority rejects the whole candidate
transaction with zero authenticated GPU fallback.

Static CityWorld admission uses the smallest camera-centered sphere enclosing
the current child drawable aspect and normalized 350 m frustum. The product
session publishes that exact resize-revision extent only for its synchronous
capture. Native world AABBs enter the resulting sphere
transactionally and the admitted stable-object set grows monotonically for the
map generation. An admitted object is never removed, so driving cannot
resurrect a renderer tombstone; far content is added only when the camera first
approaches it. This private spatial bootstrap bounds first-frame validation and
native Item creation without changing the renderer-neutral lifecycle contract.
Directional-shadow casters wholly outside this receiver envelope are likewise
deferred until approached; that is an explicit disposable-demo tradeoff, not a
general renderer culling contract.

The hidden OGRE 14 source scene does not generate automatic render-only mesh
LODs after demo capture is enabled. Pinned OGRE 14's LOD baker is unsafe for
some CityWorld v1.40 meshes, and those LODs have no consumer because OgreNext
owns the visible frame and its spatial policy. Authored base geometry is still
loaded and captured; direct legacy launches retain their configured LOD path.

After the first accepted frame, terrain geometry, composite bytes, sampler,
material, and instance owners are frozen until the ordered map-generation
reset. Later captures still join OGRE derived work and require the exact native
Terrain pointer, but deliberately do not rebuild the million-vertex portable
mesh or read the composite again. SkyX lighting changes therefore do not alter
the already-published terrain texture. This is an explicit performance-first
demo tradeoff, not a generalized texture revision or receipt contract.

The first demo intentionally does not add Alexis's twelve suspension props.
CityWorld's two unresolved `wrecker.truck` references remain omitted content,
not a reason to mutate user configuration.
