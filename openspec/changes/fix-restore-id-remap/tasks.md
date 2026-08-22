## 1. Import result and shot id map

- [x] 1.1 Add an `ImportResult` struct to `shothistorystorage.h` carrying `shotIdMap` (source id → dest id), `destShotsBefore`, `imported`, `skipped`, `failed`, `referencesRemapped`, `referencesCleared`.
- [x] 1.2 Add an `ImportResult*` out-parameter to `ShotHistoryStorage::importDatabaseStatic`, defaulting to `nullptr`; keep the `bool` return.
- [x] 1.3 Populate `shotIdMap` as each source shot is inserted, alongside the existing `packageIdMap` / `bagIdMap` / `recipeIdMap` handling. A skipped duplicate maps the source id to the id of the destination row it matched; a failed row is absent from the map.
- [x] 1.4 Populate the counts and replace the `imported/skipped/failed` summary `qDebug` with one that also states `destShotsBefore`, so merging into an empty destination no longer logs identically to merging into a populated one.
- [x] 1.5 Check `shothistorystorage.cpp` against `COVERED_GLOBS` / `MARKER_ONLY_GLOBS` in `scripts/check_log_markers.py` before touching log lines there, and follow `docs/CLAUDE_MD/LOGGING.md` for tier and marker. Audience picks the tier: a refused restore is user-facing.
- [x] 1.6 Update the three call sites to pass an `ImportResult`: `databasebackupmanager.cpp:872`, `datamigrationclient.cpp:1091`, `shotserver_backup.cpp:1179`.

## 2. Merge-integrity guards

- [x] 2.1 Check the result of the merge-mode de-duplication pre-read (`SELECT uuid FROM shots`, `shothistorystorage.cpp` ~4276). On failure, roll back and return a failure naming the pre-read; do not proceed with an empty set.
- [x] 2.2 Add an independent `SELECT COUNT(*) FROM shots` on the destination in merge mode and record it as `destShotsBefore`.
- [x] 2.3 Abort before the first insert when the pre-read returned zero and the count is non-zero; report both numbers. Merge mode only — replace mode clears the destination first and an empty pre-read is expected there.
- [x] 2.4 Confirm both guards run inside the existing transaction and leave the destination unchanged on abort.

## 3. One conversation importer, one remap

- [x] 3.1 Extract the hand-rolled AI conversation import — duplicated at `databasebackupmanager.cpp:1116-1143`, `datamigrationclient.cpp:618-660` and `shotserver_backup.cpp:1118-1163` (THREE copies, not the two the plan assumed) — into a single shared function. Landing it in one commit with the remap: splitting was dropped, the extraction has no independent value and the endpoint's reordering moves with it.
- [x] 3.2 Delete all three original copies and route each caller through the shared function. Verify they were behaviourally identical first; if they differ, record which behaviour was kept and why.
- [x] 3.2a Backup endpoint only: its conversation block runs BEFORE the shot import, so there is no id map at that point. Stash the conversations array during the zip-entry loop and import it in the existing main-thread continuation, once `success` and the map are known. Keep `reloadConversations()` on the main thread and the `aiConversationsImported` response field correct on both the async and the no-shots-db paths.
- [x] 3.3 Add the shot id map as a parameter of the shared function: rewrite each turn's `shotId` to the destination id, drop the key when the source id is absent from the map, and return the remapped and cleared counts.
- [x] 3.4 Clear every turn's `shotId` when no map is supplied — the conversations-only import path (`importOnlyAIConversations()`, or a user deselecting shots). The ids name a database this device does not have.
- [x] 3.5 Feed the returned counts into `ImportResult.referencesRemapped` / `referencesCleared` so the import summary reports them.
- [x] 3.6 Grep the settings surface for any other stored shot-id reference (`src/core/settings*`, `settingsserializer.cpp`, SAW learning, shot badges). Route any found through the same helper; record in the PR that the sweep ran and what it found, including "nothing else". **Result:** one other key stores shot ids — `migration16/pendingVisualizerSync` (`maincontroller.cpp:709`, `:4896`), an array of `{shotId, visualizerId}`. It does NOT cross an import: `SettingsSerializer::exportToJson` is an explicit per-property allowlist, not an `allKeys()` sweep, so the key is never exported or restored and stays device-local. SAW learning (`saw/*`) stores per-profile/basket keys and lag values, no shot ids. No shot-badge persistence outside `shots.db`. AI conversation turns are the only stored shot-id reference that crosses an import.

## 4. Stale ids stop being acted on

- [x] 4.1 In `AIConversation::loadFromStorage`, resolve the distinct turn `shotId`s against the database once and treat unresolvable ones as absent in memory. Do not write back.
- [x] 4.2 Make `shotIdForTurn` return `0` and `recentAssistantTurns` skip a turn whose id did not resolve, matching the existing treatment of a turn that never carried an id.
- [x] 4.3 Measure the added read on a realistic database and on a multiple of one, and record median and worst case in a comment at the call site — evidence, not the conclusion. **Done against the real tablet database** (1061 shots / 18.1 MB, pulled over `/api/backup/shots`) and a synthetic 4x copy (4244 shots / 45.1 MB), 200 runs per point. Median 0.018-0.045 ms, worst 1.02 ms on a cold cache; flat against 4x the rows. Query plan is `SEARCH shots USING INTEGER PRIMARY KEY (rowid=?)`. Numbers and the caveat (measured on the Mac against the tablet's file, not on the tablet's own hardware) are in the comment at the call site. No hoist needed.
- [x] 4.4 Surface the `requestUpdateShotMetadata` success flag to `AIManager` so a write to a missing shot reports failure to its caller instead of only warning. What the app does about it (retry, re-ask, tell the user) is out of scope — this stops it vanishing.

## 5. User-facing failure

- [x] 5.1 Add a restore failure message naming the inconsistency and stating that nothing was changed. Follow the existing `(translation key, English fallback)` pair convention in `databasebackupmanager.cpp` — worker thread accumulates, `joinErrors` translates on the main thread.
- [x] 5.2 Make the same refusal reach the LAN migration and backup-endpoint surfaces; verify none of the three reports a refused import as a completed restore.
- [x] 5.3 Add the translation keys with English fallbacks.

## 6. Tests

- [~] 6.1 Add slots to `tests/tst_shotimportdedupe.cpp` (do not create a new test file). Written: failed pre-read aborts without writing (real malformed destination — a shots table with no uuid column); agreeing counts proceed and skip duplicates; replace mode is not subject to the check; genuinely empty destination imports normally. **NOT written: "non-empty destination with an empty pre-read is refused".** The count and the pre-read read the same table in the same transaction, so they cannot be made to disagree without stubbing one of them out. CLAUDE.md treats needing fault injection to reach a branch as a stop sign, so no faked test was added; the guard stays (the field evidence for that state is real) and is documented as untested. See the open question in design.md.
- [x] 6.2 Add slots covering the id map: an imported shot's map entry is its destination id; a skipped duplicate maps to the matched destination row; a failed row is absent from the map.
- [x] 6.3 Add slots to `tests/tst_aimanager.cpp`: turns are rewritten to destination ids; a turn whose shot did not come across loses its `shotId` and is skipped by `recentAssistantTurns`; a conversations-only import clears every id; a metadata write to a missing shot reports failure.
- [x] 6.4 Add a regression test for the reported defect: import a source whose shot ids exceed anything in the destination, then assert no stored reference names a source id — the shape that would later resolve to a real but unrelated shot.
- [x] 6.5 Before keeping each test, break the code it covers and watch it go red. Drop any that cannot fail. **Three mutations, all caught with the right message.** (1) Dropping the `existingByUuid.isEmpty()` merge guard: `failed_pre_read_aborts_without_writing` went red. (2) Making `remapTurnShotIds` keep an unmapped source id instead of dropping it: the regression slot went red on a turn still carrying source id **1096** — the reported defect reproduced exactly. (3) Making `loadFromStorage` skip the resolve pass: the `recentAssistantTurns` slot went red. All reverted; `grep -c MUTANT` is 0 in both files.

## 7. Verification and close-out

- [x] 7.1 Build via `mcp__qtcreator__build` and run the full suite via `mcp__qtcreator__run_tests` (scope `all`). Confirm Qt Creator's active project is this clone first — two clones exist and its active project has drifted mid-session before. **Result: 113 passed, 0 failed, 0 skipped in 41670 ms.** One pre-existing flake was fixed on the way: `tst_beanbaseclient::inFlightSupersedeEmitsSingleTerminalSignal` used `QTest::qWait(600)` as a gate for two events (the request reaching the wire, the terminal signal arriving) — a timer standing in for an event, which CLAUDE.md bans — and lost the race under parallel load. Now `QTRY_COMPARE_WITH_TIMEOUT` / `QTRY_VERIFY_WITH_TIMEOUT`, with a short explicit settle window kept only for the negative assertion (proving a second signal never arrives inherently needs one).
- [ ] 7.2 Restore a real backup on a populated database and confirm: shots merge without duplication, conversation turns resolve to existing shots, and the summary states the pre-import count.
- [ ] 7.3 Run a LAN device migration selecting only AI conversations and confirm every imported turn's id is cleared rather than carried.
- [x] 7.4 Update the wiki manual (`Kulitorum/Decenza.wiki.git`) — a restore can now fail where it previously reported success, which is user-visible. Three to five sentences: what the message means and that nothing was changed. Cut it by half before committing. **Done** — two sentences added under **Backup and Restore**, wiki commit `f38d97d`. Drafted at three sentences, cut to two.
- [ ] 7.5 Run `openspec archive fix-restore-id-remap` as the last commit on the feature branch, before merge.
