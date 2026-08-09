# CityWorld material modernization classifier

CityWorld is user-supplied compatibility content, not a Rigs of Rods
redistributable. `tools/classify_cityworld_material_families.py` reads bounded
OGRE `.material` members in place and emits metadata-only review evidence. It
does not extract assets, rewrite the archive, copy source scripts into the
repository, infer PBR maps from filenames, or admit a material to the live
renderer.

The versioned output contract is
`ror.cityworld.material-modernization-report.v1`, documented by
`tools/schemas/cityworld-material-modernization-report-v1.schema.json`. The
checked test fixture is project-owned synthetic text. No CityWorld texture,
model, script, report, screenshot, or other package byte is tracked by this
feature.

## Invocation

Run the report against a local, user-supplied archive and keep the output
outside the repository:

```sh
python3 tools/classify_cityworld_material_families.py \
  /path/to/CityWorld.zip \
  --expect-sha256 <64-lowercase-hex-archive-sha256> \
  --pretty \
  --output /tmp/cityworld-material-modernization-v1.json
```

Exit status `0` means the report was produced, not that every material may be
modernized. Add `--require-no-review-blockers` for an automation gate; it
writes the complete report first and returns `2` if review-blocked or
unsupported records remain. Unsafe archives, digest mismatch, malformed
unrecoverable syntax, fixed-cap violations, and I/O failures return `1` and do
not replace an existing output. File output uses a temporary sibling, flushes
it, and atomically replaces the requested report.

Material text is capped at 32 MiB per member and 256 MiB in aggregate. Token
counts are capped at one million per script and four million in aggregate;
scope depth, statements, archive entries, material definitions, expanded
bytes, compression ratio, and retained report arrays have independent fixed
ceilings. These ceilings cannot be raised by CLI input.

## Authored-structure families

Classification is based on parsed technique, pass, texture-unit, directive,
and layer-equation structure. Expected family counts are never encoded in the
tool.

- `SPHERICAL_BASE_SPEC_CURRENT_ALPHA_ENVIRONMENT` requires one opaque default
  pass with three ordered 2D units: canonical base modulation, specular
  `BLEND_TEXTURE_ALPHA(texture,current)`, then spherical
  `BLEND_CURRENT_ALPHA(texture,current)`. UV, sampler, alpha-combine, cull,
  fill, iteration, and transform state must remain at the supported defaults.
- `CLEAN_TWO_PASS_ALPHA_REJECTED_EMISSIVE` requires an opaque base pass and a
  source-over second pass with `GREATER 128` or `GREATER 192` alpha rejection,
  RGB emissive exactly `0.3`, one named texture per pass, and the exact authored
  second-pass depth-write choice. Environment mapping, programs, transforms,
  and effects exclude the record.
- Transparent spherical bus-stop, Cielo planar-window, ordinary planar,
  combined planar/emissive, combined environment/emissive, additive-specular
  furniture, suspicious cube-plus-planar, simple single-pass, suspicious, and
  unsupported structures remain separate families. The report also publishes
  non-exclusive structural rollups such as all planar environment records and
  all planar `superficie-metalica.jpg` records.

Two B-like records in the audited compatibility package contain a bare
pass-level `texture_unit` token and an extra brace. A previous line-oriented
heuristic counted them among 45 clean candidates. Exact scope parsing leaves
43 strict B records and classifies those two independently as
`SUSPICIOUS_B_LIKE_ORPHAN_TEXTURE_UNIT`; neither receives an automatic repair
plan.

## Fidelity labels

`LEGACY_SEMANTIC_EQUIVALENT` means only that every reported layer equation is
structurally representable without changing the authored composition. It is
not a claim that the live renderer has authenticated or implemented that
closure.

The two strict modernization families receive the target label
`DECLARED_PBR_MODERNIZATION`. This is intentionally not legacy equivalence:
the derived PBR material changes the rendering model and therefore requires a
separate explicit reviewed declaration. The report exposes both structural
legacy-equivalence eligibility and declared-PBR eligibility. Native runtime
texture authentication, resource generation, selected scheme/LOD, and exact
pass state remain mandatory before a renderer may consume either result.

## Source evidence and repair boundary

The archive record binds its byte size, expanded-size declaration, entry
count, and SHA-256. Every script binds its exact archive member, byte size,
encoding, SHA-256, and parser anomalies. Every material binds the script hash,
an exact raw-byte source span, one-based line/column range, span SHA-256, and a
stable material ID. Paths from the host filesystem and timestamps are omitted.

The independently verified malformed spherical stack receives one repair-plan
record for its orphan `texture_unit` directive. The plan is bound to all of:

- archive SHA-256;
- exact script member and script SHA-256;
- exact material-span SHA-256; and
- exact token byte/line/column span and SHA-256.

Its state is `PENDING_HUMAN_REVIEW`, `apply_automatically` is always `false`,
and its material stays `REVIEW_BLOCKED`. The classifier never deletes the
token or emits a repaired script. Other malformed B-like or anonymous/brace
structures remain separately blocked without inheriting that reviewed repair
intent.

## Cross-platform gate

The implementation uses only the Python standard library. Synthetic normal
and optimized (`python -O`) tests run in the existing content-provenance matrix
on macOS, Windows, and Linux. Tests cover deterministic output, raw-byte spans
for UTF-8 BOM and Windows-1252 scripts, archive and script hash gates,
case-colliding/unsafe ZIP members, fixed caps, recoverable syntax evidence,
unrecoverable syntax rejection, fidelity labels, and atomic output behavior.
