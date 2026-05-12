## 1. Pre-flight grep audit

- [x] 1.1 `grep -rn` each removed symbol across the repo and build a complete call-site list to verify the deletions in §2-§7 are exhaustive: `tempUnstable`, `tempStabilityChecked`, `tempIntentionalStepping`, `tempAvgDeviationC`, `temperatureUnstable`, `temperature_unstable`, `TEMP_UNSTABLE_THRESHOLD`, `TEMP_STEPPING_RANGE`, `TEMP_WARMUP_SKIP_C`, `TEMP_MIN_EXTRACTION_SEC`, `hasIntentionalTempStepping`, `avgTempDeviation`, `reachedExtractionPhase`, `markPerPhaseTempInstability`, `temperature_drift`. Save as `/tmp/temp_unstable_audit.txt` for cross-reference during the rest of the change.
- [x] 1.2 Confirm no other detector or feature uses `reachedExtractionPhase`, `avgTempDeviation`, or `hasIntentionalTempStepping`. If any non-temp-detector caller surfaces (unexpected), raise it back to design before proceeding.

## 2. Detector removal in `src/ai/shotanalysis.{h,cpp}`

- [x] 2.1 Delete the four constants from `shotanalysis.h`: `TEMP_UNSTABLE_THRESHOLD`, `TEMP_STEPPING_RANGE`, `TEMP_WARMUP_SKIP_C`, `TEMP_MIN_EXTRACTION_SEC`. Delete the comment block above them.
- [x] 2.2 Delete the static-method declarations: `hasIntentionalTempStepping` (both overloads), `avgTempDeviation`, `reachedExtractionPhase`. Delete their accompanying docstrings.
- [x] 2.3 Delete the four fields from `DetectorResults`: `tempStabilityChecked`, `tempIntentionalStepping`, `tempAvgDeviationC`, `tempUnstable`. Delete their accompanying docstring section ("=== Temperature stability ===").
- [x] 2.4 In `shotanalysis.cpp`, delete the function bodies for `hasIntentionalTempStepping` (both overloads), `avgTempDeviation`, `reachedExtractionPhase`.
- [x] 2.5 In `shotanalysis.cpp` `analyzeShot`, delete the entire "--- Temperature stability ---" block (the `if (!pourTruncated && temperature.size() > 10 && ...)` clause). Also remove `temperature` and `temperatureGoal` from `analyzeShot`'s parameter list **only if no other detector reads them** — verify by grep before removing.
- [x] 2.6 Update `analyzeShot` and `generateSummary` declarations in the header to drop `temperature` / `temperatureGoal` parameters if §2.5 confirmed they're unused. Update all call sites accordingly (`shotsummarizer.cpp`, `shothistorystorage*.cpp`, `tools/shot_eval/main.cpp`, `tests/tst_shotanalysis.cpp`).
- [x] 2.7 Remove `verdictCategory` mentions of temperature ("temp" doesn't appear in the verdict cascade today, but verify) — ensure verdict precedence comments and code don't reference the temp branch.

## 3. Badge projection in `src/history/shotbadgeprojection.h`

- [x] 3.1 Delete the `temperatureUnstable` row from `decenza::deriveBadgesFromAnalysis`. Update the docstring projection table.
- [x] 3.2 Delete the `applyBadgesToTarget` field for `temperatureUnstable` and any companion struct field on `BadgeRow` (or whatever the shared struct is named).

## 4. Per-phase temp markers in `src/ai/shotsummarizer.{h,cpp}`

- [x] 4.1 Delete the `markPerPhaseTempInstability` helper and its declaration.
- [x] 4.2 Delete the `PhaseSummary::temperatureUnstable` field.
- [x] 4.3 Delete the call site in the `runShotAnalysisAndPopulate` helper that invokes `markPerPhaseTempInstability`. Update the helper docstring to drop the per-phase temp obligation.
- [x] 4.4 Search the AI advisor prompt builders (`buildUserPrompt`, `summarizeFromHistory`, anywhere `PhaseSummary` is rendered into prompt text) for temperature-drift mentions and delete them. Re-check the prompt template still parses cleanly with no orphan headers.

## 5. Storage layer in `src/history/shothistorystorage*.cpp`

- [x] 5.1 Drop the `temperature_unstable` column read in `loadShotRecordStatic` (the local var, the field assignment, and the lazy-persist comparison branch).
- [x] 5.2 Drop the `temperature_unstable` column write in `saveShot` and any insert SQL string.
- [x] 5.3 Update `requestReanalyzeBadges`: drop the temp arg from the worker, drop it from the comparison, drop it from the `shotBadgesUpdated` emit (5 args, not 6).
- [x] 5.4 Drop the `temperature_unstable` predicate from `buildFilterQuery`.
- [x] 5.5 Drop the field write in `convertShotRecord`'s output map AND the temperature block from the nested `detectorResults` it builds.
- [x] 5.6 Add migration 14 to drop the column. Match the existing migration style (in-band SQL, `withTempDb`, version bump, idempotent skip-if-absent guard). Statement: `ALTER TABLE shots DROP COLUMN temperature_unstable;`.
- [x] 5.7 Update `ShotHistoryStorage::shotBadgesUpdated` signal declaration in the `.h` to take 5 booleans (`channelingDetected, grindIssue, skipFirstFrame, pourTruncated` — pick the existing arg order minus `tempUnstable`).

## 6. MCP serializer in `src/mcp/mcptools_shots.cpp`

- [x] 6.1 Drop the `temperature` block from `detectorResults` JSON in `shots_get_detail` / `shots_compare`.
- [x] 6.2 Verify `shots_list` doesn't expose a `temperature_unstable` column field; if it does, drop it.

## 7. QML + UI

- [x] 7.1 `qml/components/QualityBadges.qml`: delete the temp chip (the `Repeater`/`if` clause for `temperatureUnstable`) and any chip text bindings. Re-check the row layout after one chip is removed.
- [x] 7.2 `qml/pages/ShotDetailPage.qml`: drop the `temperatureUnstable` property binding, drop the arg from the `shotBadgesUpdated` `Connections` handler, drop any `if (shotData.temperatureUnstable)` branches.
- [x] 7.3 `qml/pages/PostShotReviewPage.qml`: same treatment as §7.2.
- [x] 7.4 `qml/pages/ShotHistoryPage.qml`: delete the "Temp unstable" filter chip and its query helper. Confirm the filter row still aligns visually after the chip drop.
- [x] 7.5 Confirm `ShotAnalysisDialog.qml` needs no edits (it reads `summaryLines`).

## 8. Tests

- [x] 8.1 In `tests/tst_shotanalysis.cpp`, delete: `temperatureUnstable_*`, `intentionalTempStepping_*` (both threshold-edge variants), `avgTempDeviation_*` (warmup-skip, mid-shot-drop, no-data), `reachedExtractionPhase_*`. Update `analyzeShot_*` cascade tests to drop temp expectations and pass empty temp curves.
- [x] 8.2 Update `tst_shotanalysis::badgeProjection_*` tests: remove the `temperatureUnstable` row test (`badgeProjection_tempUnstable_setsBadge` if such exists) and any combined-cascade assertion that asserts on the temp badge.
- [x] 8.3 In `tests/data/shots/manifest.json`, drop `temperature_unstable` keys from every fixture entry. (Use `jq` or a careful `sed` — manifest entries are nested.)
- [x] 8.4 Run `shot_eval --validate tests/data/shots/manifest.json` locally to confirm the corpus still passes.
- [x] 8.5 Update `tools/shot_eval/main.cpp` if it references any removed symbol or the temp parameter on `analyzeShot`. Drop the temp-eval column from output if present (it isn't today, but double-check).
- [x] 8.6 Update `tst_shotsummarizer.cpp` aborted-during-preinfusion test: it currently asserts that `PhaseSummary::temperatureUnstable` markers are NOT set. Either delete the test (it's about a feature that no longer exists) or rewrite it to assert the puck-failure cascade only.

## 9. Documentation

- [x] 9.1 `docs/SHOT_REVIEW.md` §1 (badges table): drop the `temperatureUnstable` row. Update count in surrounding prose ("Five flags" → "Four flags").
- [x] 9.2 `docs/SHOT_REVIEW.md`: delete §2.3 ("Temperature unstable") entirely and renumber subsequent sections (2.4 → 2.3, 2.5 → 2.4).
- [x] 9.3 `docs/SHOT_REVIEW.md` §3 ("Observations emitted"): delete the "Temperature stability" item (#4) and renumber. Drop the `temperature_drift` line `kind` from the line-types table if present.
- [x] 9.4 `docs/SHOT_REVIEW.md` §3 ("Verdict precedence"): verify no temp branch was in the cascade (it shouldn't be, but confirm).
- [x] 9.5 `docs/SHOT_REVIEW.md` §4 (projection table): drop the `temperatureUnstable` row. Update prose around it ("five flag columns" → "four").
- [x] 9.6 `docs/SHOT_REVIEW.md` §5 (persistence): note the dropped column and migration.
- [x] 9.7 `docs/SHOT_REVIEW.md` §6 (regression corpus): drop temperature mentions from the manifest format paragraph.
- [x] 9.8 `docs/SHOT_REVIEW.md` §7 (references): add a note referencing this change archive once landed.
- [x] 9.9 `docs/CLAUDE_MD/MCP_SERVER.md` "Shot Detector Outputs": drop the temperature block from the documented JSON shape.
- [x] 9.10 `CLAUDE.md`: nothing to change directly (the `docs/SHOT_REVIEW.md` link is unaffected), but verify by re-reading the SHOT_REVIEW link section.

## 10. Manual & build verification

- [x] 10.1 Build via Qt Creator (debug + release) — should be clean, no warnings about unused `temperature` parameters anywhere.
- [x] 10.2 Run the full Qt Test suite via `mcp__qtcreator__run_tests` (per project policy). Zero failures, zero WARN lines.
- [ ] 10.3 Open at least three historical shots that previously had the badge — confirm the chip is gone, dialog has no "Temperature drifted" line, MCP `shots_get_detail` returns no `temperature` block.
- [ ] 10.4 Pull a fresh espresso shot live: confirm post-shot review page renders correctly (no chip, no orphan layout space).
- [ ] 10.5 Trigger the schema migration on a copy of the production DB. Verify `PRAGMA table_info(shots)` shows the column dropped and existing shot reads still work.
- [x] 10.6 Run shot_eval against `/tmp/issue1128/*.json` and `tests/data/shots/*.json` — verdicts on the corpus should be unchanged for everything except temp-related assertions.

## 11. PR & archive

- [ ] 11.1 Run `/pr-review-toolkit:review-pr` on the diff before opening the PR.
- [ ] 11.2 Open the PR with `gh pr create`. Body references issue #1128 with `Closes #1128`.
- [ ] 11.3 After merge, archive this OpenSpec change with `/openspec-archive-change remove-temperature-unstable-badge`.
