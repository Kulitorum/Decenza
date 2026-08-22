## Why

Restoring a backup renumbers every shot but leaves stored references to the old ids in place, and the import cannot tell an empty destination from a failed de-duplication query. Both were found in a live field log on 2026-08-22, from a restore of `shots_backup_20260821.zip` performed on 2026-08-21 at 14:31.

`shots.id` is `INTEGER PRIMARY KEY AUTOINCREMENT`. `importDatabaseStatic` re-inserts each source row, so every shot receives a fresh destination id. The import already remaps `equipment_id`, `bag_id` and `recipe_id` through per-type id maps for exactly this reason — but nothing consumes a shot id map, because none is produced. AI advisor conversations are imported minutes later in a different translation unit, with their per-turn `shotId` copied verbatim.

The result on the reporting device: the Aug 19 1:30 PM shot is `1052` in the database and `1109` in the conversation that discusses it. Every stored reference in all five restored conversations dangles. The user answered an advisor question with a roast date; `maybePersistBeanCorrectionFromReply` wrote it to shot `1109`, `ShotHistoryStorage` logged `No shot with id 1109 to update`, and the answer was discarded with no user-visible sign. This gets worse rather than settling: once AUTOINCREMENT climbs past the stale ids again (48 shots away on that device), the same write lands on a real but unrelated shot and silently corrupts it.

Separately, that restore logged `Found 0 existing shots` while the app had opened the same database with 1058 shots seconds earlier. The de-duplication query result is discarded — `if (uuidQuery.exec(...))` has no `else` — so a query that fails is indistinguishable from an empty table, and the consequence of getting it wrong is a whole history inserted twice. The summary line `1058 imported, 0 skipped, 0 failed` reads as a clean success in either case. Why that specific query returned nothing is not yet established; this change makes the ambiguity impossible rather than assuming a cause.

## What Changes

- `importDatabaseStatic` produces a shot id map (source id → destination id), the same shape as the existing `packageIdMap` / `bagIdMap` / `recipeIdMap`, and threads it out to callers.
- Backup restore, LAN device migration, and the ShotServer backup endpoint remap every stored shot-id reference through that map. AI advisor conversation turns (`ai/conversations/<key>/messages[].shotId`) are the known consumer; a reference whose source id is not in the map is cleared rather than left pointing at an arbitrary future shot.
- Writers that resolve a shot id from stored state treat a missing shot as a real failure. `requestUpdateShotMetadata` against a nonexistent id is surfaced to the caller instead of only warning, so a discarded user answer cannot pass silently.
- The merge de-duplication pre-read checks its query result. A failed pre-read aborts the import rather than proceeding as though the destination were empty.
- Merge mode independently counts destination rows and refuses to proceed when the pre-read reports zero existing shots but the destination is non-empty — the exact shape that would duplicate an entire history.
- The import summary distinguishes its outcomes: how many rows the destination held before, how many were inserted, skipped and failed, and whether any stored references were remapped. A restore that merges into an empty database no longer logs identically to one that merges into a populated one.
- **BREAKING** for nothing user-facing: no schema change, no stored format change. Existing dangling references in already-restored installs are repaired opportunistically by the same clearing rule when a conversation is next loaded, not by a migration that rewrites user data.

## Capabilities

### New Capabilities
- `restore-merge-integrity`: the guarantees a merge-mode database import must meet before it writes — that it knows the destination's true row count, that a failed pre-read aborts rather than degrading to "empty", that it cannot duplicate an existing history, and that its reported outcome distinguishes merging into an empty destination from merging into a populated one.

### Modified Capabilities
- `data-transfer-coverage`: extends the existing foreign-key remap guarantee (today: equipment, bags, recipes) to shot ids themselves, and to references held outside `shots.db` — settings-resident state that names a shot id must be remapped by the same import that changed those ids, or cleared.
- `advisor-conversation-history`: a per-turn `shotId` is currently specified as durable and meaningful across save/load. Adds that it must also survive an import that renumbers shots, and that a turn whose shot cannot be resolved reads as absent rather than as a stale id that a later write would act on.

## Impact

Code:
- `src/history/shothistorystorage.cpp` — `importDatabaseStatic` (shot id map, de-dup pre-read check, destination row count, summary logging)
- `src/core/databasebackupmanager.cpp` — restore path; AI conversation import currently copies `messages` verbatim
- `src/core/datamigrationclient.cpp` — LAN device-to-device migration, same import entry point
- `src/network/shotserver_backup.cpp` — backup endpoint, same import entry point
- `src/ai/aimanager.cpp` — `maybePersistBeanCorrectionFromReply` / `maybePersistRatingFromReply` outcome handling
- `src/ai/aiconversation.cpp` — turn `shotId` readers

Data: no schema change, no migration. Shot ids continue to be reassigned on import; this change makes references follow them.

Logging: `[Equipment][Import]` has a marker; the shot-import lines in `shothistorystorage.cpp` are bare `qDebug`. Confirm marker-registry coverage against `docs/CLAUDE_MD/LOGGING.md` before adding lines there.

Risk: the abort-on-ambiguity rule can refuse a restore that today "succeeds". That is the intent — the failure it replaces is a silently duplicated or silently unmerged history — but it needs a clear user-facing message, not a bare failure.

Not in scope: why the 2026-08-21 restore's pre-read returned zero against a populated database. That is recorded as an open question in `design.md`; the change removes the class of failure without depending on the answer.
