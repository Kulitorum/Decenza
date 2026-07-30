Tasks 1.1–4.2 are already implemented on branch `fix/equipment-enrichment-no-fork` and are checked off; everything below them is outstanding.

## 1. Enrichment exemption

- [x] 1.1 Add the `filledIn` per-component test and the `enrichment` verdict to `EquipmentStorage::supersedeOrEditStatic`, covering grinder brand/model/burrs, basket brand/model and the canonical puck-prep string
- [x] 1.2 Take the in-place branch on `enrichment || shotCount() == 0`, leaving the merge branch ahead of it untouched
- [x] 1.3 Update the `supersedeOrEditStatic` contract comment in `equipmentstorage.h` with the new tier

## 2. Package merge

- [x] 2.1 Add `EquipmentMergeResult` (moved counts + machine-readable `error`) to `equipmentstorage.h`
- [x] 2.2 Implement `mergePackagesStatic`: validate ids, repoint `shots`/`coffee_bags`/`recipes`, carry supersession pointers, revive the target and clear a pointer at the deleted row, delete the source — all inside one `DbWriteTxn`
- [x] 2.3 Skip referencing tables that do not exist on the connection rather than failing the merge
- [x] 2.4 Count `recipes` in `requestDeletePackage`'s reference pre-check (guarded on the table existing)

## 3. MCP surface

- [x] 3.1 Register `equipment_merge` (level `settings`) taking `sourcePackageId` + `targetPackageId`, returning the surviving package and the moved counts
- [x] 3.2 Re-select the target when the merged-away package was the active equipment
- [x] 3.3 Map each refusal token to a caller-actionable message
- [x] 3.4 Restate in `equipment_update`'s description when an edit forks and when it is applied in place

## 4. One-time heal (migration 35)

- [x] 4.0a Split the merge into `mergePackagesUnlockedStatic` (body) + `mergePackagesStatic` (transaction wrapper) + shared `validateMergeStatic`, so a caller that owns a transaction can reuse it
- [x] 4.0b `healEnrichmentForksStatic`: lineage-pair scan (equal grinder/basket/puckprep, burrs empty on the older side and set on the newer), bounded multi-pass for chains, per-fold `qInfo`, `remap` out-param, false on SQL failure with writes left staged
- [x] 4.0c Migration 35 in `shothistorystorage.cpp`: runs the heal inside the migration transaction, gates the version bump on it, resolves the merged-away active-equipment id
- [x] 4.0d `MainController` adopts `healedActiveEquipmentId()` through `SettingsDye::setActiveEquipmentId` before `setEquipmentStorage`

## 5. Tests

- [x] 5.1 `tst_equipment`: enrichment edits in place (id, `inInventory`, shot retained); a burr swap still forks; clearing a component forks; adding to an existing puck prep forks; whole-tuple enrichment (basket + puck prep) stays in place
- [x] 5.2 `tst_equipment::mergePackagesById`: fork then merge — shots and bag land on the survivor, survivor revived with no supersession pointer, source row gone, third-package lineage follows, each refusal is a clean no-op
- [x] 5.3 `tst_equipment::healEnrichmentForks`: the fork is reunited; a named-to-named burr swap and two lineage-less lookalikes both survive; a second run folds nothing; a chain heals only its enrichment link
- [x] 5.4 `tst_mcptools_write::equipmentMergeMovesHistoryAndDeletesSource`: response shape, on-disk verification, self-merge refused
- [x] 5.5 Bump the expected schema version 34 -> 35 in `tst_dbmigration` (18 sites) and `tst_coffeebags` (3)
- [x] 5.6 Run the full suite via `mcp__qtcreator__run_tests` (scope `all`) — 110 passed, 0 failed, no warnings

## 6. Documentation

- [x] 6.1 `docs/CLAUDE_MD/MCP_SERVER.md`: `equipment_merge` row, `equipment_update` fork/enrichment wording, `settings` access-level list
- [x] 6.2 Wiki manual: not needed — the fix removes a behaviour the manual never documented, and merge is an MCP repair tool, not a user-facing feature (maintainer call)
- [x] 6.3 Archive this change with `openspec archive fix-equipment-enrichment-fork` as the last commit on the branch

## 7. Deferred — decide before merging

- [x] 7.1 Bundled: every model-scoped history lookup (`shothistorystorage_queries.cpp` x6) and the AI grinder calibration block (`dialing_blocks.cpp`, model AND burrs) now fold case + whitespace like the identity matcher that writes the rows; pinned by `tst_dialing_blocks::calibrationBlock_grinderIdentityMatchIsCaseAndSpaceInsensitive`
- [x] 7.2 Merge stays MCP-only — no app Equipment page or ShotServer `/equipment` action
