## Why

Equipment identity is copy-on-write: editing any identity component of a package that has shots forks a new package and retires the old one. That is right for a *swap* and wrong for *recording what was always there* — [#1713](https://github.com/Kulitorum/Decenza/issues/1713) reported typing the burr make/model into a long-used Eureka and having the app treat it as a brand-new grinder.

The history really is gone, not just relabelled: grinder calibration matches shots on model **and burrs**, and dial-in grouping keys on `equipment_id`, so both stop seeing anything pulled before the edit. The reporter's log shows the calibration down to a single pair, and the grind wheel lost the 0.1 resolution it had learned from his own dial-ins.

Nothing could undo it either. Merge only fires on an exact identity match between two in-inventory packages, it leaves the source's shots behind, and it will not touch a retired package — so once a fork happened for the wrong reason, no surface in the app or over MCP could put the grinder back together.

## What Changes

- Identity edits gain an **enrichment** tier between "unchanged" and "fork": when every component that differs was EMPTY and now has a value, the edit is applied in place — same package id, still in inventory, every shot still pointing at it — no matter how many shots the package has.
- Real identity changes are unaffected: a value replaced by a different value, or cleared, still forks a used package, so earlier shots keep the gear they were actually pulled on.
- New `EquipmentStorage::mergePackagesStatic(db, sourceId, targetId)`: folds one package into another by explicit id, repointing shots, bags **and recipes**, carrying supersession pointers over, returning the target to inventory, and deleting the source. Accepts a retired source or target, which is what makes undoing a fork possible.
- New `equipment_merge` MCP tool (`settings` level) exposing that repair, re-selecting the target when the merged-away package was the active one. MCP-only by decision — no app or web UI.
- **Migration 35**, a one-time heal for users this already happened to: where a package is superseded by one differing ONLY by having burrs where it has none, the older is folded into the newer. Narrow by design — a burr swap between two named sets, a cleared field, or two lookalike packages with no lineage between them are all left alone.
- Every model-scoped history lookup now folds case and whitespace on the grinder model (and, in the AI calibration block, the burrs) the way `findPackageByGrinderIdentityStatic` — which *writes* those rows — always has. An exact compare meant a model string differing only in case or padding read back as a grinder with no history, which is silent: an empty result looks exactly like a new grinder.
- Fixes a pre-existing gap found in the same file: `requestDeletePackage`'s reference pre-check never counted `recipes.equipment_id`, so a hard delete could strand a recipe on a package id that no longer existed.

## Capabilities

### New Capabilities

None — this extends the existing equipment model rather than introducing a new capability.

### Modified Capabilities

- `equipment-package-model`: adds the enrichment exemption to copy-on-write forking, and adds an explicit two-id merge with its reference-repointing and refusal rules.

## Impact

- `src/history/equipmentstorage.{h,cpp}` — `supersedeOrEditStatic` enrichment tier, `mergePackagesStatic`, `EquipmentMergeResult`, delete pre-check.
- `src/mcp/mcptools_write.cpp` — `equipment_merge` tool; `equipment_update`'s description now states when an edit forks and when it does not.
- Data: merge is destructive and not undoable — the source row and its items are deleted and its shots then report the target's gear. It is exposed only where the user names both packages.
- Migration 35 rewrites `equipment_id` pointers and deletes the merged-away package rows for matching pairs. No user-entered value is changed — burrs, names and dial settings are untouched; only the app-minted package split is undone. Anything the heal does not match stays exactly as it is and is repairable with `equipment_merge`.
- The active-equipment selection lives in QSettings, so a merged-away id is remapped at migration time and adopted through `SettingsDye::setActiveEquipmentId`.
- Docs: `docs/CLAUDE_MD/MCP_SERVER.md` tool table + access-level list. No wiki manual change — this removes a behaviour the manual never described, and merge is an MCP repair tool rather than a user-facing feature.
- Not covered here: the app's Equipment page and the ShotServer `/equipment` page have no merge action yet, so the repair is MCP-only for now.
