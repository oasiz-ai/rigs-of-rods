# rorsmith

A first-party MCP server exposing this project's material and texture
authoring capability as callable tools. Stdio transport, official MCP Python
SDK, launched with `uv`.

## Why this exists rather than a generic tool

A generic material tool does not know the fail-closed admission rules, the
reviewed repair-plan sanitizer, the byte-exact archive procedures, or the live
census. rorsmith reads all four out of this repository at call time and refuses
rather than approximating when it cannot author something exactly.

## Registration

`.mcp.json` at the repository root:

```json
{
  "mcpServers": {
    "rorsmith": {
      "type": "stdio",
      "command": "uv",
      "args": ["run", "--quiet", "--directory",
               "${CLAUDE_PROJECT_DIR:-.}/tools/mcp/rorsmith", "rorsmith"],
      "env": { "RORSMITH_REPO_ROOT": "${CLAUDE_PROJECT_DIR:-.}" }
    }
  }
}
```

Standalone checks:

```sh
uv run --directory tools/mcp/rorsmith rorsmith --selftest
uv run --directory tools/mcp/rorsmith rorsmith --call renderer_policy '{}'
```

## Tools

| Tool | Purpose |
|---|---|
| `list_materials(archive, filter?, band?, limit?)` | Inventory with textures, F3 band, pass/unit counts, static admission prediction |
| `inspect_material(archive, name)` | Full detail plus what the sanitizer injects and why |
| `renderer_policy()` | Refusal tokens, band table, structural caps, archive pins — read live from the engine sources |
| `derive_normal_map(texture, out_dir, strength?, family?)` | Albedo-proxy tangent-space normal, method stated |
| `derive_roughness(family?/material?/texture?, map_method?)` | Reviewed F3 scalar; per-texel map is opt-in and labelled |
| `list_generators()` | Procedural generators, parameters, provenance |
| `generate_material(generator, out_dir, params?, resolution?, outputs?)` | Headless PBR map set at any power-of-two size |
| `fit_generator(texture, generator?)` | Fit generator parameters to an existing texture; refuses on weak periodicity |
| `author_layers(material, out_dir, preset?/layers?, dry_run=true)` | Base + up to 4 HlmsPbs detail layers, height-blended |
| `apply_to_archive(archive, changes, dry_run=true)` | Byte-exact member rewrite, dated backup, both mods dirs, digest repin |
| `verify_live(map?, truck?, ...)` | Isolated-home session, census round-trip — the trust anchor |

## Invariants

* Every mutating tool defaults to `dry_run` and returns a per-member diff with
  sha256 before/after.
* Untouched archive members keep their original local record verbatim, so they
  are byte-identical, not merely equivalent.
* Backups are dated and are never overwritten.
* `verify_live` binds `ROR_D0_SCENE_HOME` (with the `ROR_D0_EXACT_WINDOW_EXTENT`
  the runtime demands alongside it) to a private tree and verifies the log path
  is inside it. The user's own `~/RigsOfRods/logs/RoR.log` is never read or
  written.
* `AlexisSaber.zip` is read-only here while the Alexis authoring agents own it.

## What it wraps

* `tools/apply_alexis_saber_paint.py` — the byte-exact ZIP central-directory
  rewrite (`_read_entries`, `_member_payload`, `_authored_entry`, `_rebuild`).
* `tools/classify_cityworld_material_families.py` — the fail-closed Ogre
  `.material` parser and the family/eligibility classification.
* `tools/generate_cityworld_roughness_repair_edits.py` — the F3 band table,
  `classify_band()`, and the reviewed-edit reader over the sanitizer.
* `source/main/gfx/ogre14/detail/OgreNextDemoPrivatePolicy.{h,cpp}` — the
  refusal-token vocabulary and the structural caps, parsed at call time.
* `source/main/resources/LegacyMaterialCompatibilityPlan.h` — the archive
  digest pins, read and repinned.

See `THIRD_PARTY_NOTICES.md` for the Material Maker (MIT) attribution.
