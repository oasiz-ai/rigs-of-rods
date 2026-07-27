# Content provenance audit contract

The A0 audit treats attribution and redistribution rights as build inputs. It
does not infer permission from a public download, a zero price, or a working
import. The authoritative implementation is
`tools/content_provenance_audit.py`; it has no third-party Python dependency and
does not use the network.

## Inputs

The provenance manifest follows
`tools/content_provenance_manifest.schema.json` and identifies itself as
`ror-content-provenance-v1`. Every shipped file has exactly one asset record
with:

- a normalized package-relative path and SHA-256;
- an author and an expression from the auditor's frozen, non-deprecated SPDX
  [3.28 identifier list][spdx-3.28] subset;
- an explicit modification boolean and classification;
- a canonical ASCII HTTPS source URL with a syntactically valid DNS/IP
  authority,
  plus a lowercase 40/64-character content-addressed revision or source
  SHA-256;
- the repository-relative editable-source path and SHA-256; and
- an affirmative redistribution decision with HTTPS evidence.

An `import-archive` or `derived-cache` additionally records the source archive
SHA-256, detected version, pinned importer schema, and canonical conversion
options. Archive-looking paths must use `import-archive`, and cache-looking
paths must use `derived-cache`; changing only the classification cannot bypass
this metadata. This metadata is evidence, not permission; the same asset still
needs an allowed redistribution decision and license evidence.

A `generated` asset must be marked modified and use the `generator` source
kind. Its source URI and revision/checksum pin the producing tool while
`editable_source` pins the preferred source asset.

The supplied inventory identifies itself as
`ror-distributable-inventory-v1`:

```json
{
  "files": [
    {
      "path": "content/example/asset.mesh",
      "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
      "size": 1234,
      "type": "file"
    }
  ],
  "format": "ror-distributable-inventory-v1"
}
```

The manifest is kept outside the audited package. This avoids a self-referential
manifest checksum; packaging may ship a separately generated audit report.

## Gate invocation

Metadata-only preflight proves that every supplied inventory path has one
matching, non-stale provenance record:

```sh
python3 tools/content_provenance_audit.py \
  --manifest build/content-provenance.json \
  --inventory build/distributable-inventory.json
```

Preflight does **not** prove that the supplied inventory accounts for a real
package or that editable sources exist. It is useful for authoring feedback but
does not satisfy Gate A0.

Release packaging must select `--release-gate` and supply both roots. The
package root is scanned so an omitted file cannot evade the inventory, and every
package and editable source checksum is recomputed:

```sh
python3 tools/content_provenance_audit.py \
  --manifest build/content-provenance.json \
  --inventory build/distributable-inventory.json \
  --release-gate \
  --package-root build/package \
  --editable-root .
```

`--release-gate` fails when either root is absent. The repository's
`Content provenance preflight` workflow runs the dependency-free auditor tests
on supported hosts and Python versions; it deliberately does not claim Gate A0
or audit a distributable until the build supplies a real package, inventory,
manifest, and editable-source root to the command above.

Exit status is `0` for a clean audit, `1` for policy/schema findings, and `2`
when an input cannot be read or parsed. Standard output is one canonical JSON
object with format `ror-content-audit-result-v1`. It contains no timestamps or
host paths; retained diagnostics are sorted by code, path, pointer, and message.
Standard error is unused after argument parsing. The summary `errors` count is
the total number of findings even when diagnostic retention is capped. The
summary distinguishes
`path_matched_files` from `checksum_matched_files`; the latter counts only
manifest/inventory pairs with two valid, equal SHA-256 values.

## Fail-closed boundaries

Paths must be relative NFC POSIX paths without traversal, backslashes, drive
prefixes, control characters, Windows-forbidden characters, reserved device
names, components ending in a dot/space, overlong components, ambiguous
components, or case-fold collisions. The JSON schema expresses portable
structural constraints; the dependency-free runtime auditor is authoritative
for UTF-8 byte lengths, NFC, URI authority syntax, SPDX, and cross-record
collisions.

Only regular files are distributable. Root and nested symlinks plus Windows
filesystem reparse points are rejected. On platforms with `dir_fd` and
`O_NOFOLLOW`, hashing opens every component relative to an already-open
directory descriptor. The Windows fallback rejects reparse components and
requires the before/open/after file identities and metadata to remain stable.
The descriptor being checked is the descriptor being hashed.

Duplicate JSON keys, unknown fields, stale provenance, checksum mismatches,
untracked files, and untracked archive/cache-looking paths all fail. Input JSON
is read from one descriptor in bounded chunks and must remain identity/metadata
stable. Input bytes, entry counts, filesystem files, path lengths, conversion
metadata, retained diagnostic count, diagnostic fields, and aggregate
diagnostic detail are bounded by immutable ceilings. At most 4,096 diagnostics
and 4 MiB of aggregate diagnostic detail are retained; a single deterministic
`DIAGNOSTIC_LIMIT_EXCEEDED` record reports omitted findings. Root verification
also has a 16 GiB per-file and 64 GiB total hashing ceiling. Every `--max-*`
option may tighten its corresponding ceiling; a larger value is clamped and
cannot loosen it.

The SPDX list check is deliberately conservative. A valid identifier outside
the frozen audited subset fails until the tool and its tests are reviewed and
extended. This is preferable to silently accepting a typo or deprecated
identifier at release time.

## Tests

The standard-library suite uses only clean-room synthetic metadata and
temporary files. Python 3.11 or newer is required:

```sh
python3 -m unittest -v tests/tools/test_content_provenance_audit.py
```

[spdx-3.28]: https://github.com/spdx/license-list-data/releases/tag/v3.28.0
