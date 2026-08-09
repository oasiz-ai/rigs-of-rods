# OGRE 14 material semantic runtime admission

This slice is the fail-closed boundary between reviewed `RORMAT2` bytes and a
prepared renderer-neutral legacy material. It authenticates an exact catalog,
an exact immutable registry publication, the current material-script closure,
and extractor-owned OGRE 14 state before it mints an opaque per-material
admission. It does not activate a catalog in the game.

## Approval authority

`Ogre14LegacyMaterialSemanticApprovedManifest` is an opaque immutable owner.
Its public default constructor creates no authority, copying preserves only an
existing owner, and production exposes no loader, constructor, expected-hash
parameter, or friend seam that can mint one. A caller-supplied whole-file SHA
next to caller-supplied bytes is not approval. The only current mint is guarded
by `ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING` and exists solely for the
synthetic fixture. A future production loader must be implemented as a complete
library-owned type which authenticates compiled or signed configuration before
it receives any private mint access.

The approved owner pins:

- the exact full-file SHA-256 of the canonical `RORMAT2` bytes;
- package archive SHA-256, exact resource group, and stable reviewed revision;
- every material key and its complete ordered script closure, including role,
  member, original/effective SHA-256, repair state/version, and repair-plan
  SHA-256;
- every catalog texture ordinal, key, archive-member source kind, and exact
  member name.

The whole-file digest is checked before the parser runs. Authentication then
parses the bytes, verifies the trusted package/group/revision and complete
inventory, rejects unsupported surfaces, constructs the exact registry from
that parsed catalog, and retains the approved manifest, catalog, and registry
owners together. Failure or an injected exception leaves the prior authority
unchanged.

Fault stages, callback types, and callback-bearing overload parameters exist
only in the synthetic build guarded by
`ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING`. Production authentication
has no callback between hashing and parsing caller-owned bytes, and production
live admission/preparation has no callback which can mutate an OGRE pass after
native validation. The authenticated coordinator factory likewise exposes no
production translator fault injector; its synthetic parameter is test-gated,
while the unrelated generic coordinator factories retain their test seams.
The extractor's declaration-digest stage type, setter, thread-local storage,
and callback calls likewise exist only in the dedicated synthetic build guarded
by `ROR_OGRE14_NATIVE_MATERIAL_DECLARATION_DIGEST_TESTING`; production native
capture has no inherited declaration-digest callback which could mutate a
previously admitted live material.

Production live admission is sealed at the concrete `RoR::ContentManager`
edge. `ContentManager` is final and all six script/texture resolver/provider
overrides are final; the production capture function, authenticated factory,
and coordinator storage accept that concrete authority only. The abstract
combined authority and its factory overload exist solely under
`ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING` for hostile synthetic fixtures,
so an independent forwarding implementation cannot interpose production
resolver or snapshot calls.

## Two generation domains

For format version 2, each record's `resource_generation` is now defined as a
stable reviewed package/catalog revision in the explicit domain
`REVIEWED_PACKAGE_REVISION_V1`. It is not, and must never be compared with,
ContentManager's process-local resource-group generation.

The runtime group generation is a mount/lifecycle nonce. It is taken only from
the current authenticated script and texture receipts and is stored separately
as `runtime_group_generation`. Tests intentionally use reviewed revision 17
and runtime generation 101 to prevent accidental numeric coupling. This closes
the previously undefined v2 field semantics; there was no production catalog
activation whose live compatibility could be preserved.

## Exact admission order

Catalog activation is ordered as follows:

1. externally approved full-file SHA-256;
2. bounded `RORMAT2` parser;
3. trusted package/group/reviewed-revision and complete-closure scope;
4. exact immutable semantic registry construction;
5. transactional runtime-authority publication.

`CaptureAndAdmitOgre14LegacyMaterialSemanticRuntime` performs the live path in
one serialized authority call:

1. resolve the current native `Ogre::Material` to its authenticated script
   receipt;
2. compare every ordered root/import source and repair-plan field with the
   approved closure;
3. resolve and authenticate the exact semantic declaration identity;
4. call `CaptureOgre14LegacyNativeMaterial` with the same combined
   ContentManager script/texture authority;
5. authenticate the opaque native receipt, reviewed declaration digest, and
   bit-exact pass, sampler, texture, archive/member/source-kind projection;
6. capture current texture authority, then perform the resolver-owned no-throw
   material/script revalidation and capture the current script authority;
7. re-resolve final semantics and transactionally publish the opaque admission.

A stale caller-supplied capture is never a production input. The admission
retains the exact runtime authority, script resolution, semantic identity,
native receipt/digest, authenticated texture resolutions, and both distinct
generation values.

## Initial accepted surface

Version 1 admits authored or repaired-script records only, using the exact
`ror.ogre14.explicit-fixed-function` lowering algorithm at version 2. It
requires Default scheme, LOD 0, no environment or shadow augmentation, and at
most one base-color texture unit with identity swizzle, UV set/transform, and
canonical color/alpha modulation. Texture sources must be authenticated
archive members. Activation projects the complete catalog pass and sampler
facts into the translator's exact v1 validators, so unsupported blend, depth,
color-write, filter, address, comparison, anisotropy, or LOD state never mints
runtime authority even when a native capture would match it bit-for-bit.

Generated/listener materials, generated texture fallback, RTSS-like program
surfaces, non-default scheme/LOD, multiple texture units, nonidentity
swizzle/UV/combine, augmentation, and unknown lowering algorithms or versions
fail closed. Supporting any of these requires an explicit reviewed format and
manifest extension; audit or classifier output is never approval authority.

## Prepared-frame capability

`CreateOgre14LegacyAuthenticatedMaterialCoordinator` creates its translator
internally from the registry owned by the authenticated runtime authority. A
fresh equal-value registry is a different authority and cannot be supplied.
The authenticated coordinator rejects its raw observation API.

Production `PrepareAdmittedFrame` accepts exact live `Ogre::Material` pointers,
not previously minted admissions. Before any readback it performs the shared
transaction/state/count/sequence gate, rejects null, duplicate-pointer, and
duplicate-key inputs with a bounded sort, and verifies the coordinator's exact
registry and final concrete ContentManager authority. It then performs a fresh
`CaptureAndAdmitOgre14LegacyMaterialSemanticRuntime` for each material and
charges decoded mip bytes immediately before capturing the next material. It
also incrementally charges material-owned samplers and unique stable texture
keys, authenticates repeated keys against the same live texture authority, and
rejects texture/live-asset caps before another readback. The inner preparation
retains the same authoritative checks.
The old admission-based entry point exists only in the synthetic test build.

The coordinator allocates the private outer capability and retains the fresh
admission wrappers before beginning the inner translator lease. It prepares
the inner frame, captures and checks fresh final script and texture snapshots,
and then publishes an opaque capability retaining the manifest/catalog/
registry, admissions, script/native/texture owners, and inner frame. Any later
synthetic fault discards the inner pending transaction. Commit requires the
exact outer identity; a mismatched capability preserves the pending lease,
while an inner commit invariant failure consumes it and permanently fail-stops
the coordinator. There is no fallible work after accepted exposure.

## Current activation blocker

The project intentionally contains no production approved-manifest loader, no
reviewed package catalog, no CityWorld asset, no guessed SHA, and no game
activation call. The synthetic fixture's exact compiled full-file SHA-256 is
`e41391acb8f5e13232f2515a5d6cac6b9e8c486c3d54825d0dfe18894384d2ff`.
Production activation remains blocked until an independently reviewed package
and canonical complete-closure manifest are supplied through a real trusted
compiled or signed configuration loader.
