## 1. Remove the sticky enjoyment field

- [x] 1.1 Remove `dyeEspressoEnjoyment` `Q_PROPERTY`, getter/setter declarations, and change signal from `src/core/settings_dye.h`; rewrite the class comment to state that no rating field lives here and why
- [x] 1.2 Remove the getter/setter bodies from `src/core/settings_dye.cpp`
- [x] 1.3 Remove the three `metadata.espressoEnjoyment = …` reads in `src/controllers/maincontroller.cpp` (espresso save, pending upload, simulated shot) and the two now-empty post-save resets; update the `[metadata] Reset …` log string
- [x] 1.4 Remove the backup serialize/restore entry in `src/core/settingsserializer.cpp`, leaving a comment that an `espressoEnjoyment` key in an older backup is ignored
- [x] 1.5 Remove the `settings_get` entry (`src/mcp/mcptools_settings.cpp`) and the `settings_set` schema entry + handler (`src/mcp/mcptools_write.cpp`), pointing callers at `shots_update enjoyment0to100`
- [x] 1.6 Remove `dyeEspressoEnjoyment` from the dye key list in `docs/MCP_TEST_PLAN.md`

## 2. Stop migration 16 reading the legacy key

- [x] 2.1 Change the inferred-row reset in `src/history/shothistorystorage.cpp` to `UPDATE shots SET enjoyment = 0`, dropping the `shot/defaultRating` read
- [x] 2.2 Rewrite the migration's header comment to record why 0 replaced the configured default (feature gone; PATCH sends `null` for 0 so Visualizer shows Unrated)
- [x] 2.3 Reframe the QSettings-scope comment so it justifies the scope for the pending back-sync list (which `MainController` reads back) rather than for the removed rating read

## 3. Evict the dead keys

- [x] 3.1 Remove `shot/defaultRating` and `dye/espressoEnjoyment` in the `Settings` constructor after `sync()`, with a comment on why unread is not the same as gone

## 4. Tests

- [x] 4.1 Replace the property test in `tests/tst_settings.cpp` with `deadShotRatingKeysAreEvicted`: seed both keys, construct `Settings`, assert both are absent afterwards, assert a backup round-trip does not carry `espressoEnjoyment`, assert a fresh `ShotMetadata` is unrated, and assert a second construction is a no-op
- [x] 4.2 Update `v16_resetsInferredAndDropsColumn` in `tests/tst_dbmigration.cpp` to seed a stale `shot/defaultRating = 50` and assert inferred rows land on `0` — pinning that the stale value has no effect — with user-rated and already-unrated rows untouched
- [x] 4.3 Full suite green locally (85/85), clean build with zero warnings

## 5. Verification

- [x] 5.1 Pull a simulator shot and confirm it saves unrated and the taste intake appears (shot 1116, `enjoyment0to100: null`)
- [x] 5.2 Relaunch the app and re-read the live settings store to confirm `shot.defaultRating` and `dye.espressoEnjoyment` are actually gone — the suite proves eviction against the test store only. Confirmed 2026-07-20: both keys present before the relaunch (`shot.defaultRating = 50`), both absent after, with bean/profile/favorites settings intact — a surgical eviction, not a reset

## 6. Specs

- [x] 6.1 Add the save-time invariant and migration-16 reset requirements to the `shot-rating-capture` delta
- [x] 6.2 Correct the stale `shot-rating-capture` Layer 2 requirement: it documents a three-icon `QuickRatingRow` with a per-shot dismiss flag, but that component was deleted in #1245 and replaced by the inline `RatingInput` in #1243, and `shotRatingDismissed/<shotId>` exists nowhere in the codebase
- [x] 6.3 Drop the removed `setDefaultShotRating` → `setDyeEspressoEnjoyment` example from `settings-architecture` and add the rule that removing a setting removes its stored key
- [ ] 6.4 Archive this change so the deltas promote into `openspec/specs/`, as the last commit on the branch before merge
