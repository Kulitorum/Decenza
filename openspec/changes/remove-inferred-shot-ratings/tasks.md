## 1. Schema migration (DB rollback)

- [x] 1.1 In `src/history/shothistorystorage.cpp`, add migration 16. Steps, all inside a single `QSqlDatabase::transaction()` / `commit()` block (rollback on any step failure):
  1. Read `shot/defaultRating` from `QSettings` (fallback `75`).
  2. `SELECT id, visualizer_id FROM shots WHERE enjoyment_source = 'inferred' AND visualizer_id IS NOT NULL AND visualizer_id != ''` — capture the (shotId, visualizerId) pairs of rows that were uploaded to Visualizer; serialize as a JSON array and stash in `QSettings` under key `migration16/pendingVisualizerSync` (append, don't overwrite — preserves any partially-drained list from a prior failed sync).
  3. `UPDATE shots SET enjoyment = <default> WHERE enjoyment_source = 'inferred'`.
  4. `ALTER TABLE shots DROP COLUMN enjoyment_source`.
  5. Bump `kSchemaVersion` to 16.
- [x] 1.2 Confirm migration 14 still works on a clean DB (no code change expected; documenting that a downgrade-then-upgrade cycle re-adds the column at `'none'` and migration 16 then drops it again with no inferred rows to reset — safe noop).
- [x] 1.3 Add `tests/tst_dbmigration.cpp` test `v16_resetsInferredAndDropsColumn`: synthesize a DB at version 14 schema (column present), insert four rows — (`enjoyment=75 source='inferred' visualizer_id='V1'`), (`enjoyment=75 source='inferred' visualizer_id=NULL`), (`enjoyment=90 source='user' visualizer_id='V2'`), (`enjoyment=0 source='none'`) — set `QSettings shot/defaultRating = 50`, run `ShotHistoryStorage::initialize`. Assert: column gone, both inferred rows' enjoyment now `50`, user row untouched at `90`, none row untouched at `0`, schema_version is `16`, AND the `migration16/pendingVisualizerSync` QSettings key contains exactly one entry (the one with `visualizer_id='V1'`).
- [x] 1.4 Update or remove `tst_dbmigration.cpp::v14_idempotentReapply` — the test currently asserts the column survives idempotent re-init; after migration 16 it must NOT survive. Rename to `v16_columnIsDropped` and update the assertion.

## 2. Storage / projection cleanup

- [x] 2.1 In `src/history/shotprojection.h`, remove the `enjoymentSource` field (line ~42-44) and any serialization references.
- [x] 2.2 In `src/history/shothistory_types.h`, remove the `enjoymentSource` field from `ShotData` (~line 57) and `ShotRecord` (~line 215).
- [x] 2.3 In `src/history/shothistorystorage.cpp`:
  - Remove the inferred-good evaluator block (lines ~995-1042 — the `if (data.espressoEnjoyment <= 0)` clause and the `else if` clause that sets `enjoymentSource = "user"`).
  - Remove `enjoyment_source` from the INSERT column list (~line 1133), placeholder list (~line 1143), and `bindValue` (~line 1184).
  - Remove `enjoyment_source` from the SELECT column list in `loadShotRecordStatic` / `convertShotRecord` (~line 1502).
  - Remove the `{"enjoymentSource", "enjoyment_source"}` entry from the metadata column map (~line 1822) and the surrounding default-to-`"user"` logic (~line 1825).
  - Remove `enjoyment_source` from the data-import SELECT (~line 2315) and the value-read at line 2358; skip-with-warning if the source DB has it.
- [x] 2.4 Update `convertShotRecord` and any other `ShotRecord` ↔ `ShotProjection` converters to no longer carry the field.

## 3. AI / dialing-blocks cleanup

- [x] 3.1 In `src/ai/dialing_blocks.cpp` and `src/ai/dialing_blocks.h`:
  - Remove the `enjoymentSource: "inferred"` conditional emission in `buildCurrentBeanBlock` (~line 67 and the helper).
  - Remove the same in `buildDialInSessionsBlock` (~line 123).
  - Restore `buildBestRecentShotBlock` to pre-Layer-3 logic: highest `enjoyment0to100 > 0` in the 90-day window, no `confidence` field, no user/inferred tier preference. Remove the SQL clauses that reference `enjoyment_source` (~lines 205, 256).
- [x] 3.2 In `src/ai/shotsummarizer.h`, remove the `enjoymentSource` field on the input struct (~line 124-125) and any setters.
- [x] 3.3 In `src/ai/shotsummarizer.cpp`:
  - Remove the system-prompt paragraph teaching `currentBean.enjoymentSource: "inferred"` and the `"inferred"` value on `dialInSessions[].shots[].enjoymentSource` (~lines 1142-1149).
  - Remove the `bestRecentShot.confidence` system-prompt teaching.
  - Remove any code that copies `enjoymentSource` from `ShotProjection` into the prompt-input struct.

## 4. MCP tool cleanup

- [x] 4.1 In `src/mcp/mcptools_shots.cpp`:
  - Remove the `enjoymentSource` filter argument from `shots_list`'s JSON schema (~lines 117-125).
  - Remove the filter validation block (~line 154) and all references to `enjoymentSourceFilter` (~lines 249-311).
  - Remove the `enjoymentSource` field from the per-shot output JSON (~line 270-271).
- [x] 4.2 In `src/mcp/mcptools_write.cpp`:
  - Remove the `enjoymentSource` argument from `updateShot`'s JSON schema (~lines 53-58).
  - Remove the validation block that rejects non-`"user" | "inferred" | "none"` values (~lines 92-98).
  - Remove the metadata-map write for `enjoymentSource`.
- [x] 4.3 Update `docs/CLAUDE_MD/MCP_SERVER.md` if it documents the removed argument / filter.

## 5. Visualizer back-sync of corrected ratings

- [x] 5.0a In `src/controllers/maincontroller.cpp` / `.h`, add a private method `processPendingVisualizerRatingSync()` and a `QStringList` member to track in-flight work. Called once Visualizer credentials are confirmed available (existing signal — check `VisualizerUploader::testConnection` success or simply on first phase transition to `Idle`). The method:
  1. Reads `migration16/pendingVisualizerSync` from `QSettings` (JSON array of `{shotId, visualizerId}` pairs).
  2. If empty or Visualizer credentials are absent, returns silently.
  3. Pops one entry, loads the shot via `ShotHistoryStorage::requestShot(shotId, callback)`.
  4. In the callback, calls `m_visualizer->updateShotOnVisualizer(visualizerId, projection)` — this PATCHes the corrected `enjoyment` (and re-sends the rest of the metadata, which is correct because we already updated the local row).
  5. On `VisualizerUploader::uploadSuccess` (or equivalent), removes the entry from the QSettings list and recurses to drain the next entry. On `uploadFailed`, leaves the entry in the list (it retries on next app boot) and aborts the drain to avoid hammering the API.
- [x] 5.0b Wire `processPendingVisualizerRatingSync()` into `MainController`'s boot sequence so it runs after `VisualizerUploader` is constructed and credentials are loaded. Use existing mechanisms — do NOT add a timer guard (per CLAUDE.md "no timers as guards" rule). The natural hook is the existing `testConnection` success path or the first phase-change to `Idle`.
- [ ] 5.0c Add a test in `tests/tst_maincontroller.cpp` (or appropriate harness) — or, if MainController is hard to test in isolation, a focused test on the QSettings list draining: seed `migration16/pendingVisualizerSync` with two pairs, mock the VisualizerUploader's PATCH (intercept the request), assert two PATCH calls with the correct visualizerIds and the corrected `espresso_enjoyment` value.
- [ ] 5.0d Manual smoke: with a DB known to contain inferred-rated shots that were uploaded to Visualizer, run the new build, confirm via Visualizer.coffee web UI that the shot's rating now matches the local value (the user's `defaultShotRating`, not 75).

## 6. Tests

- [x] 6.1 Delete `tst_dialing_blocks.cpp::bestRecentShot_prefersUserOverInferredEvenWhenInferredScoresHigher` and `bestRecentShot_inferredFallbackWhenNoUserRated` (lines ~870, ~892). Delete the `insertShotWithSource` helper (~line 845); update remaining callers (e.g., `bestRecentShot_emptyWhenNoCandidates` at line 914) to use `insertShot` directly.
- [x] 6.2 Update `tst_dialing_blocks.cpp::bestRecentShot_emptyWhenNoCandidates` to assert `confidence` is absent in any bestRecentShot block returned by post-change code (if there's a positive test, add one for "user-only" highest-rated). Cross-check that no remaining test asserts presence of `confidence`.
- [x] 6.3 Search for stragglers: `grep -rn "enjoymentSource\\|enjoyment_source\\|inferred-good\\|Layer 3\\|kInferredScore\\|confidence.*inferred\\|inferredGood" src/ tests/ docs/` should return empty except for archived OpenSpec changes.
- [x] 6.4 Run the `shotanalysis_tests`, `dialing_blocks_tests`, `mcp_tools_tests`, `shotsummarizer_tests`, `dbmigration_tests`, `shothistorystorage_tests` Qt Creator targets via `mcp__qtcreator__run_tests`; resolve any failures introduced by the removal.
- [ ] 6.5 Manual smoke: build the app, pull an espresso shot through to save, confirm the shot row in `shots` has `enjoyment` equal to the Visualizer "Default Shot Rating" setting (not 75 unless that's the user's default). Repeat with `defaultShotRating = 0` to confirm the bug from issue #1150 is fixed.

## 7. Docs and proposal hygiene

- [x] 7.1 In `docs/SHOT_REVIEW.md`, remove any reference to the inferred-good evaluator / `enjoymentSource = "inferred"` if present. The recompute-on-load contract no longer touches enjoyment at all.
- [x] 7.2 No CLAUDE.md updates required (CLAUDE.md doesn't currently document the inferred mechanism).

## 8. Release / wrap-up

- [ ] 8.1 Commit the change with conventional-commit message `fix: remove inferred-good shot auto-rating (#1150)` and open a PR.
- [ ] 8.2 Reference issue #1150 in the PR body so it auto-closes on merge.
- [ ] 8.3 Update the release notes (when next version cuts) with a user-visible line: "Fixed: shots saved with the user's configured Default Shot Rating again, no longer overwritten by the auto-rating heuristic (issue #1150). Existing affected shots are reset on first launch and re-synced to Visualizer."
- [ ] 8.4 After merge, archive this OpenSpec change via the `openspec-archive-change` flow.
