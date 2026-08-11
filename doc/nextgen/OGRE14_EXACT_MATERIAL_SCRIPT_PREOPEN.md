# OGRE 14 exact material-script pre-open authority

Status: pinned prerequisite implemented and natively probed on macOS arm64.

This is the renderer-neutral authority seam required before RoR can mint an
authenticated material-script provenance receipt. It does not activate a
semantic catalog, alter `ContentManager`, wire `GfxScene`, infer declarations,
or distribute CityWorld data.

## Pinned source and patch

- Upstream: OGRECave OGRE `v14.5.2`
- Source archive SHA-256:
  `1949fe62f3e4b8043e82e4dc94f9b0ab412a5bffc9e10d3b1dddc80fe54fe1e3`
- RoR base commit:
  `ebad7e37c1f3b700ffd7d2f6ac7a63f797f9548d`
- Patch:
  `cmake/conan/recipes/ogre3d/patches/14.5.2/exact-material-script-preopen.patch`
- Patch SHA-256:
  `3344cd639959553bda2ec978ad66e4b42df00e2f56f75d39a2d780ce4aa38478`
- Conan recipe revision:
  `ogre3d/14.5.2#2e5eda6c54bfb7f9ae19831a65d52f74`
- Derived MyGUI recipe revision:
  `mygui/3.4.0#ca50701442923d90c0a15cf40a4644fa`
- Native macOS arm64 Release package:
  `ogre3d/14.5.2#2e5eda6c54bfb7f9ae19831a65d52f74:5c43930ec5f93ceae6d2e5fcd4957341351cdf91#08dc6a38d8e464cd089ff1eb444ea77a`

The patch is applied after the nine earlier pinned patches and before the
additive shadow-material declaration-name getter patch. The checked-in Linux,
macOS, and Windows OGRE-only locks and the corresponding three RoR locks all
name the same combined recipe revision. The root RoR and MyGUI recipes name it
directly as well.

## API contract

`Ogre::ResourceLoadingListener` gains an explicit opt-in and one optional
virtual callback:

```cpp
virtual bool resourceStreamOpeningEnabled() const;

virtual Ogre::DataStreamPtr resourceStreamOpening(
    const Ogre::String& requestedName,
    const Ogre::String& group,
    Ogre::Resource* resource,
    const Ogre::Archive* selectedArchive,
    const Ogre::FileInfo* exactFileInfo,
    bool& handled);
```

Both methods are appended after the existing listener methods and are
non-pure. The opt-in returns false by default; the callback sets
`handled = false` and returns null by default. Existing source-derived
listeners therefore retain normal archive-open behavior without metadata
lookup or callback delivery. Because the listener vtable grows, consumers must
be rebuilt against this package revision; this is source/default compatible,
not binary-compatible with listener objects built against pre-seam OGRE.

The callback runs after OGRE selects the archive but before `Archive::open` in
both relevant paths:

1. `parseResourceGroupScripts`, for every enumerated script; and
2. `openResourceImpl`, including `ScriptCompiler` imports.

`selectedArchive` is the actual selected archive object. When `exactFileInfo`
is non-null, its `archive` must be that same object. Both pointers are borrowed
and valid only for the callback. A listener must not retain them or
reentrantly unload/remove the selected resource location during the callback.
The authoritative member is `path + basename`; `filename` is explicitly not
authoritative because OGRE's non-strict ZIP mode may collapse it to a basename.

For `openResourceImpl`, OGRE asks only the already-selected archive for file
metadata. Exactly one record whose archive pointer and `path + basename` both
match is exposed. Zero, multiple, foreign, wrong-member, or throwing results
are recovered only from a unique exact entry in that selected archive's full
listing; otherwise they become a null `FileInfo`. Legacy fallback remains the
requested name when exact metadata is unavailable. Only metadata-resolution
exceptions are caught; exceptions from the listener continue to propagate
before any fallback open.

The result state is explicit:

- `handled == false`: the callback result is ignored and OGRE opens the exact
  selected member normally;
- `handled == true` with a stream: only that returned stream is consumed; and
- `handled == true` with a null stream: the request is rejected and OGRE never
  falls back to `Archive::open`.

Exceptions from the pre-open callback propagate before any fallback open.
Successful streams still pass through the existing post-open listener.

## Hostile runtime proof

The Conan `test_package` builds a standalone C++17 probe from synthetic,
in-memory archives. It proves:

- an opted-out, default-derived listener causes no additional ordinary-open
  metadata I/O, performs normal archive opens, and receives original bytes;
- an opted-in listener receives a null `FileInfo` when a synthetic archive
  throws a non-OGRE metadata exception, then preserves unhandled fallback;
- two different archive objects containing identical bytes remain two owners;
- duplicate basenames in distinct nested members remain distinct through
  `FileInfo::path + FileInfo::basename` even when `filename` is basename-only;
- `FileInfo::archive` equals the exact selected archive on every admitted
  script;
- a handled replacement causes zero fallback archive opens;
- handled null rejection causes zero fallback archive opens in both script
  enumeration and ordinary `openResourceImpl`; and
- an imported script with two archives exposing the same exact qualified
  member selects the first indexed archive, rejects a deliberately wrong
  singleton `FileInfo`, recovers that archive's unique exact listing entry,
  accepts synthetic replacement bytes, and performs no fallback or metadata
  I/O against the unselected shadow archive.

The fixture has no production map, package, or script bytes. Equal payloads
are deliberate so digest equality cannot stand in for source identity.

## Acceptance gates

The static contract is run with ordinary Python and `python -O`; it verifies
the exact touched upstream files, callback signature and ordering, no-fallback
branches, exact-member construction, patch/source digests, probe markers, and
all six locks. The Conan graph assertion treats the pre-open seam and the
following shadow-name getter as part of the exact eleven-patch set.

The native acceptance command is the pinned macOS arm64 Release `conan create`
from the recipe README. Its relocated `test_package` must compile both probes,
run the hostile material-script probe without loader environment overrides,
then run the existing Metal/package probe. Package reference and final command
results are recorded in the recipe README after every revision change.

The 2026-08-09 native run used AppleClang `21.0.0.21000101`, emitted
`authenticated-material-script-preopen=ok`, selected Metal in the existing
relocated probe, and passed the package verifier as arm64 with a macOS 11.0
minimum deployment target, 20 Mach-O files, 17 package-local symlinks, ten
relative pkg-config files, and isolated plugin loading.

This seam supplies source-selection evidence only. A future, separate
`ContentManager` change must own bytes and archive-generation identity,
authenticate a canonical reviewed repair plan, and mint immutable receipts.
It must fail closed when this callback supplies a null or mismatched exact
record; this patch does not manufacture that missing authority.
