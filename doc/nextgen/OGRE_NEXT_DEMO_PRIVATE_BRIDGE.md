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
  every capture. The adapter reobserves exact terrain, texture, TUS0, sampler,
  UV, gamma, fog, and native mip identities after readback before publishing.
- The native RGBA8 base level keeps byte-identical RGB and receives opaque
  alpha. Every tail mip is generated through 1x1 with the deterministic integer
  2x2 display-domain box rule. This unconditional cross-platform rule avoids a
  pinned OGRE 14 Metal bug that aliases nonzero `blitToMemory` requests to mip
  zero. The portable sampler is clamp, linear min/mag, nearest mip,
  non-comparison, and anisotropy one.
- OGRE derived terrain work is joined at the capture boundary because the
  product bridge skips the legacy render traversal that normally pumps its
  main-thread WorkQueue responses.
- Textured or programmed non-terrain materials that the factor-only fallback
  cannot represent become one neutral opaque matte while keeping real mesh
  geometry, culling, visibility, transforms, and FlexBody deformation. Alexis
  texture-blend colors are dropped. Its authored zero-alpha, no-depth-write
  `invisible` cab section is omitted instead of being made visible.
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

Terrain assets use private, domain-separated source IDs with a bidirectional
collision check. Terrain, static, and dynamic candidates commit or discard as
one joined capture; no payload is trusted solely from `Texture::stateCount`.
No receipt, digest, or audit type is added to a public header.

Fresh base-level GPU readback and synchronous derived-work joining are
correctness-first and may stall. A later demo optimization may cache only at a
private map-generation boundary after proving equivalent native identity; it
must not turn this disposable adapter into a public receipt API.

The first demo intentionally does not add Alexis's twelve suspension props.
CityWorld's two unresolved `wrecker.truck` references remain omitted content,
not a reason to mutate user configuration.
