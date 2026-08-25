# Forward-Native Asset Policy Ledger

This ledger makes the V2 compatibility budget auditable. The policy baseline is
repository commit `1cc1d99518a0ec5576b062c6efca7ad6670fcdbd`; the adoption
commit is the first commit containing this ledger and the V2 forward-native
policy in `ROADMAP.md`. Any entry added after that adoption commit consumes the
post-baseline budget unless a later roadmap revision explicitly replaces the
budget.

## Grandfathered compatibility evidence

The following reviewed work predates policy adoption and is retained without
consuming the new 20% compatibility budget. Its presence here is not a claim
that every commit is merged to `master` or redistributable.

| Evidence | Scope at adoption | Status |
| --- | --- | --- |
| Baseline commit `1cc1d99518a0ec5576b062c6efca7ad6670fcdbd` | Exact legacy compatibility inventories and exceptions already recorded by `CITYWORLD_MATERIAL_MODERNIZATION.md`, `CITYWORLD_VISUAL_UPGRADE.md`, and `ROADMAP.md` at that commit | Grandfathered repository baseline |
| `68212622fc3198a47e35ac14ddb8e9aab9146b2d` | Independent blend/test/depth, gamma, alpha, specular, and sampler renderer contract | Reviewed feature evidence; merge status must be checked separately |
| `295cf84ca306f6a49c921673ba266d151fb38f71` | Source-backed legacy Dust particle/alpha compatibility proof | Reviewed feature evidence; merge status must be checked separately |
| `9544d29d359c71e261e348e48769311ef0a8fe6e` | Managed Alexis material-authority refresh and bounded `4/7` specular scope | Reviewed feature evidence; merge status must be checked separately |
| `28a3aaa4388fffb3b731b17f82a0a42526bacf19` | Three hash-pinned CityWorld Asia material declarations used as a compatibility vertical-slice proof | Reviewed feature evidence; merge status must be checked separately |

Grandfathering freezes scope. A new material declaration, asset family, alias,
fallback, inferred semantic, or compatibility-only renderer branch is a new
entry even when it extends one of the rows above.

## Forward-native A0 origin records

These records document new-source provenance and do not consume the legacy
compatibility budget. An origin record is evidence of declared authorship,
license, and byte lineage; it is not by itself the V2 visual-quality,
performance, redistribution, or release approval.

| ID | Origin class | Family | Author and license | Source evidence | Checked native product | Bounded capability evidence | Review status |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `NATIVE-A0-001` | `project_original` | `rorng_a0_road_tile_12m` | Rigs of Rods contributors; `GPL-3.0-or-later` | Canonical source declaration `content-source/native_render/a0_road_tile_12m/rorng_a0_road_tile_12m.native.json` (`sha256:473aa35ab82f24dee8e54845a17f82a14a958fe30694927539bada46d3a2fc68`) attests independently authored geometry, textures, and material declarations; canonical GLB `sha256:e67238c03ad09a46b7683232456b8942377c33fbf05a4cfe439ea6dee02d6f4f`; checked composition `sha256:169422fb94a71ababbd0220768cdb33f5e3d43957275c9782506ebe9e1a9bf42` and non-evidence preview `sha256:7cd7d43d3d81de9e95470030f3a8bf246eb7603b3eadeedbf969e0a4ecee86c0` | `resources/nextgen/native/a0_road_tile_12m/rorng_a0_road_tile_12m.rornative` (`sha256:5f91c134231d5b86cd0c291d30018aa2f8aa4958c8e9267ec1c9068a0ea9bc05`) | Visual-only 6 m by 12 m lighting-response tile: deterministic 512 px multi-scale rough asphalt with base-color sRGB plus linear normal and metallic/roughness maps; a deterministic 512 px low-roughness wet/specular strip with shared film/flow structure across base, normal, and specular maps; full 10-level color mip chains and vector-filtered normal mip chains; alpha-tested lane markings; specular/emissive reflectors; a 1.45 m two-post/crossbar shadow gate; explicit flag values `{0,6,7}` for inert/receiver/gate caster-receiver roles; and explicit 4x anisotropic mipped samplers with zero LOD bias. No AO, LOD, collision, or native-terrain evidence. | Origin and hashes are mechanically gated; approval commit pending independent review |
| `NATIVE-A1-001` | `project_original` | `rorng_a1_native_course_60m` | Rigs of Rods contributors; `GPL-3.0-or-later` | Canonical source declaration `content-source/native_render/a1_native_course_60m/rorng_a1_native_course_60m.native.json` (`sha256:f13af91e56670bec17aa286d3b57e1a52d343f1bc307a90dafb9142f21430556`) attests independently authored A1 geometry, textures, materials, alignment, and the versioned thin-slab transmission profile; canonical GLB `sha256:7b0648cde63053385d9a7ec66f56da470cfe8bf3465ef5d6c52cc0c9702b7801`; checked composition `sha256:db7cbacdf1228d9e9836b32afc1c7d587151d61b4661715bd4132373f3403980`; renderer-neutral alignment `sha256:ef6764702e6c70375b4bd8e897e83e3191bd3a52778323538f87c8a4f81a1078` | `resources/nextgen/native/a1_native_course_60m/rorng_a1_native_course_60m.rornative` (`sha256:fe37f2bb05f15bc4954c07ff83a71c2dea24b51af473056f8257a47b4cc8cc7e`) | Visual-only 60 m by 8 m two-lane course with high-quality 1024 px dry PBR and wet/specular asphalt sets, 512 px shoulders, markings, curbs, joined barriers/posts, calibration markers and gate, explicit samplers, vector-filtered normal mips, and a 50-placement alignment manifest whose six surfaces and four curb boundaries are reconciled against decoded GLB components. One project-original 4 m by 3 m, 0.08 m thick glass slab authors IOR 1.52, Beer-Lambert attenuation, and thin parallel-slab transmission. The historical M5 gate for predecessor package `sha256:6399101c63ca8d5eff25ab499db215c45d89a4ce91cba08145692d025401505d` remains in `evidence/PLAYABLE_VISUAL_CHECKPOINT_M5_2026-08-14.json`; it is not evidence for the current restage. No collision, RoR physics binding, native terrain, AO, LOD, path tracing, GI, soft-shadow, ray-traced-reflection, or performance evidence. | Origin and hashes are mechanically gated; current restaged package awaits fresh live renderer evidence |

## Post-baseline ledger

This table starts empty. Each accepted entry records both the planned estimate
and actual engineer-hours so the V2 80/20 limit can be evaluated under both
measures. The cumulative allowance through the first V2 preview is one legacy
asset family and three legacy material declarations.

| ID | Origin class | Family | New legacy declarations | Planned hours | Actual hours | Native-contract justification | Owner | Expiry | Approval commit |
| --- | --- | --- | ---: | ---: | ---: | --- | --- | --- | --- |

The owner of the V2 release gate reviews this ledger with every milestone. An
exception is valid only when the approval commit changes `ROADMAP.md`, names the
owner and expiry, supplies measured native-contract evidence, and preserves the
80/20 limit by removing an equal or larger amount of planned compatibility
work. An implementation-only change cannot rewrite the baseline or approve its
own exception.
