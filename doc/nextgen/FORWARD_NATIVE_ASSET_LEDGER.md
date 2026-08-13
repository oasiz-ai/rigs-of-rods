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
