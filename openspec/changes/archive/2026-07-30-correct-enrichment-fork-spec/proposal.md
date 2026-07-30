## Why

The review of [#1720](https://github.com/Kulitorum/Decenza/pull/1720) changed two spec-level behaviours after `fix-equipment-enrichment-fork` had already been archived, so the main spec now describes rules the code no longer follows. A spec that is wrong in a knowable way is worse than one that is silent — the next reader trusts it.

## What Changes

- The enrichment rule gains its one exception: a package with **no grinder item at all** is deliberately grinder-less (a basket-only tea setup), so giving it a grinder is a real identity change and forks. Absence of a *basket* or *puck prep* is not treated the same way — there is no espresso pulled without a basket, so an unrecorded basket is unrecorded data, while tea genuinely has no grinder.
- Merge gains its revival limit: the surviving package returns to inventory **only when nothing else supersedes it**. Reviving a package that a third one replaced put a stale duplicate back in the inventory, carrying its own successor's derived name.
- Records the `[Equipment]` log subsystem as a requirement, since what it must answer (which branch an identity edit took) is behaviour, not implementation detail.

No code changes — the code already behaves this way as of `848e2df7`. This change exists to make the spec match it.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `equipment-package-model`: corrects the enrichment requirement and the merge requirement, and adds a requirement for the equipment log subsystem.

## Impact

- Specs only: `openspec/specs/equipment-package-model/spec.md` via this change's delta.
- Code already in place: `EquipmentStorage::supersedeOrEditStatic` (the `gainingAGrinder` guard), `mergePackagesUnlockedStatic` (the conditional revive), `src/core/logtags.h` + `src/history/equipmentlogging.h`.
- Tests already in place: `tst_equipment::copyOnWriteAndMerge` (grinder-less fork) and `::healEnrichmentForks` (the chain's middle package stays retired).
