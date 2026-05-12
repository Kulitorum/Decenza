## Why

Issue #1128 surfaced a recurring false-positive "Temp unstable" badge on Extractamundo Dos! and similar profiles. Audit of the 60+ built-in profiles found at least 12 whose actual-vs-goal temperature gap is **by design** — D-Flow family (`espresso_temperature_steps_enabled = 0`, KB explicitly says "DO NOT flag the large gap…it is by design"), Extractamundo Dos! / TurboBloom (16°C bloom drops the head physically can't cool to), 80's Espresso / TurboTurbo / Classic Italian / Blooming Espresso / A-Flow dark / Innovative long preinfusion / Flow Profile straight & milky (6–10°C designed swings).

The detector measures **average deviation from goal** but is labeled "Temp unstable" — the math doesn't match the label. The `hasIntentionalTempStepping` curve-based suppression is fragile: it relies on the captured `temperatureGoal` series spanning the full goal range, which collapses to a near-constant on some firmware/BLE paths (one user-shared shot showed goal frozen at 72.5°C despite a profile spanning 67.5–83.5°C). The remaining diagnostic value (cold portafilter crash, heater failure, boiler exhaustion) is rare, already visible in the post-shot temperature chart, and crowded out by the false-positive noise. Tagging profiles individually with a new `temp_drift_expected` flag was the alternative considered — rejected because every new "intentional drop" profile would need the flag, the false positive remains permanent until tagged, and the underlying detector still measures the wrong thing.

## What Changes

- **BREAKING:** Remove the `temperatureUnstable` quality badge end-to-end: detector logic, badge column, dialog observation line, AI advisor prompt input, MCP `detectorResults.temperature` field, history-list filter chip, and DB read paths.
- Drop these symbols from `src/ai/shotanalysis.{h,cpp}`: `tempUnstable`, `tempStabilityChecked`, `tempIntentionalStepping`, `tempAvgDeviationC` fields on `DetectorResults`; `TEMP_UNSTABLE_THRESHOLD`, `TEMP_STEPPING_RANGE`, `TEMP_WARMUP_SKIP_C`, `TEMP_MIN_EXTRACTION_SEC` constants; `hasIntentionalTempStepping` (both overloads), `avgTempDeviation`, `reachedExtractionPhase` static methods (the last is only used by the temp detector); the temperature-stability block in `analyzeShot`.
- Drop the projection row from `src/history/shotbadgeprojection.h`.
- Drop the chip from `qml/components/QualityBadges.qml`; remove `temperatureUnstable` property bindings and the badge-update signal arity from `qml/pages/ShotDetailPage.qml`, `qml/pages/PostShotReviewPage.qml`, and the `shotBadgesUpdated` signal in `ShotHistoryStorage` (currently 6 args → 5).
- `qml/components/ShotAnalysisDialog.qml` updates automatically — it renders `shotData.summaryLines` and we stop emitting the `temperature_drift` line. The `caution` line type stays for other uses.
- `src/history/shothistorystorage.cpp`: drop column read/write in `loadShotRecordStatic`, `saveShot`, `requestReanalyzeBadges`, `convertShotRecord`, `buildFilterQuery`, and the lazy-persist path. Add a migration to drop the `temperature_unstable` column, matching the existing migration style (column adds happen in migrations 10–13; this is the first removal so we'll match nearby pragma usage).
- Drop the temperature block from `src/mcp/mcptools_shots.cpp` `detectorResults` JSON; update `docs/CLAUDE_MD/MCP_SERVER.md` "Shot Detector Outputs" section.
- Drop temperature drift from per-phase `PhaseSummary::temperatureUnstable` markers in `src/ai/shotsummarizer.{h,cpp}`; remove the `markPerPhaseTempInstability` helper and any AI prompt fragments that mention temperature drift.
- Remove the `temperatureUnstable` filter chip from `qml/pages/ShotHistoryPage.qml` and corresponding query helpers.
- `docs/SHOT_REVIEW.md`: update §1 (badges table down to four flags), delete §2.3 entirely, update §3 (drop "Temperature stability" observation #4 from the dialog list, drop the `temperature_drift` line kind), §4 (drop the projection-mapping row), §5 (note the dropped column), §6 (drop temp manifest assertions), §7 (note the removal in references).
- `tests/data/shots/manifest.json`: drop `temperature_unstable` assertions across all fixtures.
- `tests/tst_shotanalysis.cpp`: delete `temperatureUnstable_*`, `intentionalTempStepping_*`, `avgTempDeviation_*`, `reachedExtractionPhase_*` test cases. Update any combined-cascade tests that assert on temp output to drop the temp expectation.

## Capabilities

### New Capabilities
None.

### Modified Capabilities
- `shot-analysis-pipeline`: removes the temperature-unstable detector branch from `analyzeShot`, drops `tempUnstable` / `tempStabilityChecked` / `tempIntentionalStepping` / `tempAvgDeviationC` from `DetectorResults`, drops the badge-projection row, drops the per-phase `PhaseSummary::temperatureUnstable` marker, and removes `reachedExtractionPhase` from the helper surface. Existing requirements that mention `tempUnstable`, `temperatureUnstable`, `TEMP_STEPPING_RANGE`, or per-phase temp markers must be deleted or rewritten to drop those clauses.

## Impact

- **Code**: ~15-20 files. Detector logic itself is ~30 lines; the rest is fanout (DB column, projection helper, badge UI, MCP serializer, AI prompt, filter query, manifest, tests, docs).
- **Database**: one schema migration adding a column-drop step. SQLite `ALTER TABLE … DROP COLUMN` is supported on the version we ship; matches existing migration pattern.
- **APIs (BREAKING)**: external MCP consumers reading `detectorResults.temperature` lose the field. The five legacy badge booleans on `convertShotRecord` shrink to four (`temperatureUnstable` removed). No deprecation period — this is a defect-removal change, not a feature deprecation.
- **History compatibility**: existing shots that have stored `temperature_unstable = 1` are unaffected at the user-facing layer (badge no longer renders) and the column read is dropped. Lazy-persist on view becomes a four-badge update.
- **CI**: `shot_corpus_regression` ctest target's manifest must be updated to drop temp assertions or it will fail.
- **Closes**: GitHub issue #1128.
- **Out of scope for this change**:
  - Replacing the detector with an "actual instability" measure (variance / mid-shot crash). Worth considering separately; weak enough value that we may never add it.
  - Tagging profiles with a `temp_drift_expected` flag (the rejected alternative).
  - The grind-detector false positives also surfaced by #1128 — separate change.
