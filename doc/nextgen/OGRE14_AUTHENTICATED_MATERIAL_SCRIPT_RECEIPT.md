# OGRE 14 authenticated material-script provenance receipt

Status: implementation prerequisite. This contract authenticates the script
sources from which OGRE 14 created a live material. It does not activate the
semantic catalog, translate the material for OgreNext, wire `GfxScene`, or
claim that source text alone describes the final native declaration.

## Authority boundary

`ContentManager` opts into the pinned exact material-script pre-open seam. For
an authenticated package it requires the exact selected `Ogre::Archive*`, the
exact `Ogre::FileInfo*`, and `FileInfo::path + FileInfo::basename`. It resolves
that archive pointer against the current immutable mounted-archive snapshot,
requires its ArchiveManager entry to have exactly one process-wide resource
location, and compares the selected member against a bounded immutable
name/size/SHA-256 manifest captured from that snapshot before mount
publication. Before EmbeddedZip registration, a renderer-neutral parser reads
the immutable classic/ZIP64 end records plus every central/local member record
with checked arithmetic. It rejects more than 65,536 entries, more than 16 MiB
of member name/extra/comment metadata, per-member or aggregate decoded-size
overflow, unsupported compression/encryption flags, malformed or overlapping
local data/descriptor spans, directory-attribute disagreement, malformed UTF-8
or path identities, and ASCII-case/slash lookup aliases. The mounted OGRE file
and directory indexes must then match the complete preflighted member set;
every file must also retain its exact compressed/uncompressed sizes.
It then opens that one member once, enforces a 16 MiB source
limit, and returns OGRE a replacement `MemoryDataStream` over the exact
effective bytes. The post-open
callback requires the same stream pointer, compiler-file name, size, cursor,
and bytes before the source becomes eligible for publication.

Equal bytes, a basename match, archive iteration order, a caller-provided
digest, or a separately mounted archive with the same digest cannot confer
authority. Reusing a freed `Archive*` address also cannot confer authority:
manager/location identity and the snapshot-derived member digest are both
revalidated immediately around the read.

The receipt retains:

- the immutable archive snapshot owner and its source identity, SHA-256,
  selected archive name/type/pointer, and resource-group generation;
- exact `FileInfo` filename/path/basename and the derived member name;
- original and effective bytes, byte counts, and SHA-256 values;
- the versioned, domain-separated canonical repair-plan digest, including an
  explicit `NONE` or `APPLIED` state; and
- the exact ContentManager-created Material pointer, handle, name, group,
  origin, parse token, source-open ordinal, and material-event ordinal.

Nonempty receipts and resolutions have private constructors. Only
`ContentManager` can commit a whole group or mint a current resolution.

## Conservative import closure

OGRE folds imported scripts into the root compiler AST but exposes no exact
per-material dependency graph. A material receipt therefore retains the whole
authenticated source closure observed for that root parse: the root plus every
exact imported compiler dependency, ordered by a contiguous source-open
ordinal. `primary_source_index` identifies the unique source matching the
material event's compiler-file identity. The closure is deliberately
conservative; it says these sources participated in the parse, not that every
source modified every material.

Any unowned, ambiguous, conflicting, undelivered, or reboxed dependency in an
otherwise authenticated parse poisons that group candidate. An untrusted root
remains on the legacy path and does not accidentally acquire authority merely
because it later opens an authenticated dependency.

## Publication and lifecycle

For an authenticated first definition, `ContentManager` calls
`MaterialManager::create` itself, retains the returned `MaterialPtr`, writes
the exact `Material**` event result, and waits until OGRE has populated the
material and assigned its origin. Script completion validates pointer, handle,
name, group, manager indices, and origin. Nothing is published per script.

Resource-group completion first revalidates every retained material and
atomically publishes the material registry, texture registry, compatibility
indexes, and exact committed generation. Shader compatibility mutation runs
only after that authority publication and is best-effort; it cannot mint or
alter source provenance. A parser exception triggers an explicit candidate
abort before native resource-group teardown, so a partial parse cannot
publish. Group generation changes remove the prior group's records and every
compatibility index before new parsing begins; old external receipts keep
their immutable bytes but can no longer resolve as current.

Normal material removal publishes a copy-on-write registry update. If that
update fails after OGRE has already erased its native indices, ContentManager
performs an allocation-free `noexcept` poison before any fallible log work.
Every resolution and revalidation also queries the live MaterialManager by
pointer, handle, name, group, and origin because OGRE's bulk removal path does
not issue a per-resource callback.

## Repair-plan digest

Repair-plan version 1 is a canonical little-endian, length-prefixed SHA-256
record. `APPLIED` hashes the domain tag, version, exact archive SHA, exact
member, original script SHA, and every edit's kind, line, expected token, and
replacement token. `NONE` uses a different domain tag and zero edits. A
reviewed plan is accepted only for its exact archive/member/script digest and
only after its full transactional application succeeds.

## Limits and portability

The live loader permits 128 total pre-open attempts, and therefore at most 128
reachable captured sources, per parse. The immutable registry retains a
defensive 4,096-source validation ceiling for independently supplied input.
The other hard limits are 16 MiB per original or effective script, 65,536 live
sources and receipts, 65,536 group records, 16 MiB of
identity strings, and 1 GiB of retained script bytes. Parse and generation
tokens never wrap or reuse. Cap and validation failures preserve the prior
immutable registry snapshot. The lower live attempt ceiling is the
cross-platform compiler-stack safety boundary. Archive central-directory
admission happens before any EmbeddedZip factory, ArchiveManager, or resource
location mutation, and its exact entry/identity totals are charged against the
process-wide archive-manifest budget before that external mount.

The RoR host intentionally uses synchronous OGRE resource-group
initialization. A successful authenticated mount binds package lifecycle,
script callbacks, mesh deserialization, resolver calls, and removal callbacks
to that one process-lifetime resource/render thread. Pure invalid mount input
does not bind the gate. Application source is contract-checked not to enable
`ResourceBackgroundQueue` or background-loaded resources while this authority
is present; a foreign-thread removal is fail-stop because OGRE has already
erased its native manager indices before invoking the listener.

The receipt, canonical serializer, and registry are renderer-neutral C++17.
The native taps use only pinned OGRE 14 public APIs and are shared by Metal,
Direct3D, OpenGL, and Vulkan builds. macOS arm64 is the first native gate;
Windows and Linux compile/runtime gates remain mandatory before the later live
OgreNext default switch.

## Deliberate non-claims

This receipt does not authenticate decoded texture pixels, classify semantic
material roles, or hash the final native Material/Technique/Pass/texture-unit
declaration. Those are separate authenticated inputs: the source-texture
resolution, the reviewed semantic-catalog admission, and the native material
declaration digest respectively. Live OgreNext admission must require all of
them rather than treating this source receipt as a substitute.
