## Context

See `proposal.md` — Why. Design-relevant current state:

`ShotHistoryStorage::importDatabaseStatic(destDbPath, srcFilePath, merge)` returns a bare `bool`. Inside its transaction it already builds three id maps — `packageIdMap`, `bagIdMap`, `recipeIdMap` — each produced by a `import<Type>Static(srcDb, destDb, merge, outMap, ...)` helper and consumed by the next importer down the chain so foreign keys land on destination ids. Shots are inserted last and get new ids the same way, but no map is built for them and nothing downstream could use one: the map's natural consumers live outside `shots.db` entirely.

Three call sites enter that function: `databasebackupmanager.cpp:872` (backup restore), `datamigrationclient.cpp:1091` (LAN device migration), `shotserver_backup.cpp:1179` (backup endpoint).

The AI conversation import is **hand-rolled three times** — `databasebackupmanager.cpp:1116-1143`, `datamigrationclient.cpp:618-660`, and `shotserver_backup.cpp:1118-1163` — each writing `ai/conversations/<key>/messages` straight from the source JSON. The copies are already free to drift; putting the remap into some of them and not the rest is the default failure mode here, not a hypothetical. (This section said "twice" while planning. The third copy surfaced when the backup endpoint's call site was reached — which is the argument for the shared function stated more sharply than the plan managed to state it.)

The three surfaces sequence differently, and one of them inverts the order:

- **Backup restore** runs shots and conversations inside one background-thread lambda, so a value can be held in a local.
- **LAN migration** is an async queue (`startNextImport`) where each type is a separate network round trip; `importAll` enqueues `shots` before `ai_conversations`, but the user can select types, and `importOnlyAIConversations()` is reachable with no shot import at all.
- **The backup endpoint** imports conversations *before* shots — the conversation block sits in the zip-entry loop at `:1118`, the shot import at `:1179` — so the id map does not exist yet at the point the conversations are written. Its shot import also runs on a background thread with a main-thread continuation, and `reloadConversations()` must stay on the main thread.

`AIConversation` holds no database handle. The write path that consumes a stale id (`AIManager::maybePersistBeanCorrectionFromReply` → `requestUpdateShotMetadata`) does have one via `m_shotHistory`.

## Goals / Non-Goals

**Goals:**

- One definition of "remap a stored shot reference", used by every import surface.
- Make the id map a value the import produces, so a caller cannot forget it exists — an import that renumbers and a caller that ignores the renumbering should not both compile cleanly.
- Fail an ambiguous merge before it writes, in the transaction that would do the writing.
- Repair already-broken installs at read time without rewriting stored user data.

**Non-Goals:**

- Preserving shot ids across an import. Stable ids would need a UUID-keyed foreign key everywhere, or `INSERT` with explicit ids and a collision strategy; that is a schema change touching every table that references a shot, to fix a problem that a map solves. Rejected on scope, not on merit — see Decisions.
- Changing when or how shot ids are assigned in normal operation.
- Auditing every settings key for shot-id-shaped values. AI conversation turns are the known consumer; the design makes adding a second consumer cheap, and the task list includes one sweep to confirm there is not already one.

## Decisions

### `importDatabaseStatic` returns a result, not a bool

Add an out-parameter struct (`ImportResult`) carrying the shot id map plus the counts the `restore-merge-integrity` spec requires: destination rows before, inserted, skipped, failed, references remapped, references cleared. Keep the `bool` return for the success/failure branch every call site already has.

*Alternative — a separate `lastImportShotIdMap()` getter.* Rejected: the map is meaningful only for the transaction that produced it, and a getter invites reading it after a later import has overwritten it. An out-param scopes it to the call.

*Alternative — apply the remap inside `importDatabaseStatic`.* Rejected: it would put `QSettings` writes inside a `shots.db` transaction, and the function is deliberately database-only. It also cannot work for LAN migration, where the conversations arrive in a later network round trip.

### One remap helper, and delete the other copies of the conversation import while we are here

A single function takes the incoming conversation JSON and the shot id map, rewrites each turn's `shotId`, drops the key when the source id is not in the map, and reports how many it did of each. All three existing hand-rolled importers call it.

The three copies of that importer are near-identical and already a drift hazard (`CLAUDE.md`, "Centralize anything produced at more than one site"). Collapsing them to one is in scope for this change rather than left for later: the alternative is writing the remap three times, which is the exact shape the rule exists to prevent. This is the cheapest it will ever be to remove.

*Alternative — remap only in the backup-restore copy.* Rejected: LAN migration and the backup endpoint renumber shots identically and would keep the bug.

### The backup endpoint defers its conversation import into the shots continuation

Its conversation block runs before the shot import, so there is no map to apply. The conversations array is stashed during the zip-entry loop and imported in the existing main-thread continuation, once `success` and the id map are known — the same shape backup restore already has, and the continuation is already where the `aiConversationsImported` response field is assembled. No new thread and no second pass over the archive.

*Alternative — read the archive twice, taking conversations in a pass after the shots import.* Rejected: it adds a pass and splits the conversation handling away from where the archive's other entries are read, for no gain over stashing one array.

*Alternative — leave this surface unremapped and fix it separately.* Rejected: `data-transfer-coverage` requires the remap on every import surface, and this one is a restore path — the same defect on a third route.

### Conversations imported without an accompanying shot import have their ids cleared

When `importOnlyAIConversations()` runs, or a user deselects shots, there is no map. The ids in the incoming conversations name rows in a database this device does not have. Clearing them is the correct reading of `data-transfer-coverage`'s "a reference whose source shot id is absent from the mapping SHALL be cleared" — an absent map is the degenerate case of an absent entry, not an exemption from it.

The cost is losing shot linkage on conversations imported alone. That linkage was never valid in that scenario; today it is silently wrong instead of absent.

### The merge-integrity check runs inside the transaction, before the first insert

Two additions where the de-duplication pre-read happens:

1. Check `uuidQuery.exec()`. On failure, roll back and return a failure naming the pre-read — do not proceed with an empty set.
2. Independently `SELECT COUNT(*) FROM shots` on the destination and require it to EQUAL the number of uuids read back; roll back and report both numbers otherwise.

   **Revised during review.** As first written this compared "pre-read returned zero" against "count is non-zero", which `shots.uuid TEXT UNIQUE NOT NULL` makes reachable only by a read truncated at row 0. The equality covers that as a subset and also catches a read truncated anywhere later — `next()` returning false is how end-of-rows and a mid-scan error look the same, and a mid-scan `SQLITE_BUSY` during a restore is a mechanism that can actually occur.

Both are cheap (one indexed count) and sit next to the read they guard, so the guard cannot be separated from what it protects. Merge mode only — replace mode clears the destination first and an empty pre-read is expected there.

*Alternative — compare counts after the import and roll back on doubling.* Rejected: it detects the same fault later, after the expensive work, and a rollback of a large import is precisely the thing a pre-check makes unnecessary.

### Stale ids are resolved when a conversation loads, not by rewriting stored data

`AIConversation::repairStaleTurnShotIds` resolves the distinct shot ids in the loaded turns against the database once and treats the unresolvable ones as absent.

**Two corrections from review, both material.** (1) "Nothing is written back" was wrong: `saveToStorage` persists `m_messages`, so the drop becomes permanent at the next save — acceptable only because the id is known not to resolve, which is why an unanswerable lookup now leaves the data alone instead of clearing it. It also had to survive `saveToStorage`'s reconcile branch, which adopts another writer's on-disk copy and was silently restoring the stale ids. (2) The repair was a private step inside `loadFromStorage`, which `AIManager` calls from its own constructor — before any storage is wired — so the most recently used conversation loaded unrepaired on every launch. It is now a named method called again from `setShotHistoryStorage`.

This is a bounded read on a discrete user action: at most ~16 turns per conversation, distinct ids, a single `SELECT id FROM shots WHERE id IN (...)`. Inline is appropriate under `CLAUDE.md`'s threading rule; the task list requires the measurement be recorded at the call site rather than asserted here.

*Alternative — a migration that rewrites stored conversations.* Rejected: it cannot know the right destination id for an install whose broken import already happened, so it could only clear — and clearing by rewrite is destructive where clearing by read is not.

*Alternative — validate only at the write site.* This is where the actual harm lands, and it is a strictly smaller change. Rejected as the sole fix because `recentAssistantTurns` also feeds stale ids into the advisor's `recentAdvice` context, so a reader-side fix is needed regardless. The write-site guard is kept as well — see below.

### The write site reports failure instead of warning

`requestUpdateShotMetadata` against a missing id currently emits `No shot with id N to update` and an async `success: false` that nothing reads. The success flag is surfaced to `AIManager`, which is what turns a discarded user answer into something the app can act on. What the app should *do* about it — retry, re-ask, tell the user — is deliberately left to a follow-up; this change stops it vanishing.

## Risks / Trade-offs

- **A stricter merge refuses a restore that today reports success** → The refusal fires only on the contradictory shape (non-empty destination, zero-length pre-read), which has no legitimate meaning in merge mode. The user-facing message must name the inconsistency and state that nothing was changed, otherwise a hard failure is a worse experience than the silent one it replaces.
- **Collapsing the three conversation importers touches restore, LAN migration and the backup endpoint in one change** → All three are exercised by the same helper afterwards, so a defect shows up in all of them rather than in one. Test the shared helper directly. The collapse and the remap land in one commit: splitting them was considered and dropped, since the extraction has no independent value and the endpoint's reordering has to move with it anyway.
- **Clearing ids on a conversations-only import loses linkage users might expect** → It was never correct linkage. Report the count of cleared references so the outcome is visible rather than inferred.
- **The read-time resolution adds a database read to conversation load** → Bounded and on a discrete action, but it is on the advisor's open path. Measure on a realistic database and record median and worst case at the call site; if it does not hold up, hoist it to the point where `m_shotHistory` is already being read.
- **`shots.id` remains unstable across imports** → This change makes references follow the renumbering rather than removing the renumbering. Any future consumer that stores a shot id outside `shots.db` inherits the obligation to be remapped. The single helper is the place that obligation is discharged, which is why it is one function and not a per-site fix.

## Migration Plan

No schema change and no data migration. Deployment is the code change alone.

Installs already carrying dangling references are handled by the read-time resolution, which takes effect on the next conversation load. Nothing needs to run once at upgrade.

Rollback is a straight revert. A database imported by the new code is indistinguishable from one imported by the old code — the difference is in what the settings-resident references point at, and a reverted build reads those the same way it always did.

## Open Questions

- Why the 2026-08-21 14:31 restore's de-duplication pre-read returned zero against a destination the app had opened with 1058 shots seconds earlier.

  **Partly answered during implementation, from the tablet's real database** (pulled over `/api/backup/shots`; 1061 shots, 18.1 MB, `PRAGMA integrity_check` ok, 1061 distinct non-null uuids — so nothing about the data would defeat the pre-read).

  The decisive reading is `sqlite_sequence`: `shots` is at **1061**, exactly the current max id. `DELETE FROM shots` does NOT reset that counter — verified directly rather than assumed: after three inserts and a `DELETE FROM`, seq stays 3 and the next insert is id 4; only `DROP` + recreate returns the next id to 1. So the destination's `shots` table was **dropped and recreated**, not emptied. That is consistent with the pre-read matching nothing, with the 1058 rows being re-inserted as ids 1..1058, and with today's three shots landing on 1059-1061.

  So the question is no longer "how did a populated table read as empty" — it did not; the table was fresh. It is now: **what recreated it.** Nothing in `restoreBackup` drops tables; `createTables()` does, on a database whose tables are absent. A replaced or newly-created `shots.db` file before the merge is the remaining candidate, and that is not established. Still deferrable, and still worth chasing separately — but the shape of the answer has changed, so do not re-open this looking for a failing `SELECT`.
- Whether any settings key besides AI conversation turns stores a shot id. One sweep is in the task list; if the answer is yes, that consumer calls the same helper and neither the specs nor this approach changes. The count of hand-rolled conversation importers was wrong in this document until implementation corrected it, so treat "three" as measured rather than the sweep being finished.
