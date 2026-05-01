# Tasks

## 1. Session-context dedup in dialInSessions

- [ ] 1.1 Add `McpDialingHelpers::hoistSessionContext` (pure, header-only) — given an ordered list of shots in a session, return `{ context: {grinderBrand, grinderModel, grinderBurrs, beanBrand, beanType}, perShotOverrides: [...] }` where `context` carries fields that match across all shots in the session and `perShotOverrides[i]` carries only the fields that differ.
- [ ] 1.2 Wire helper into `mcptools_dialing.cpp` session-build loop. `sessionObj["context"] = hoisted.context`; `shotToJson` stops emitting the hoisted fields and only emits per-shot overrides.
- [ ] 1.3 Update `dialing_get_context` tool description to document the new shape (one short sentence).
- [ ] 1.4 Add tests in `tst_mcptools_dialing_helpers.cpp`:
  - All shots share identity → `context` has all fields, `perShotOverrides` are empty.
  - One shot in a session has different `beanBrand` → `context` includes the common fields, that shot's override carries `beanBrand` only.
  - Single-shot session → `context` carries the shot's identity, `perShotOverrides[0]` is empty.
  - Empty / no-shot session → not produced (already enforced upstream).

## 2. Drop result["shot"] JSON block

- [ ] 2.1 Remove the `shotSummary` QJsonObject construction and `result["shot"] = shotSummary;` in `mcptools_dialing.cpp`.
- [ ] 2.2 Remove the redundant header block from `ShotSummarizer::buildUserPrompt` only if it duplicates fields from the JSON (the prose's "## Shot Summary" header stays — it's the canonical surface now).
- [ ] 2.3 Update tool description: drop the "recent shot summary" phrase since `shotAnalysis` already covers it.
- [ ] 2.4 No new tests; remove any tests that asserted on `result["shot"]` if present.

## 3. Move detector-observations legend to the system prompt

- [ ] 3.1 In `ShotSummarizer::buildUserPrompt`, remove the seven-line preamble that explains `[warning] / [caution] / [good] / [observation]` severity tags. Keep the `## Detector Observations` section header and the per-line tags.
- [ ] 3.2 In `ShotSummarizer::shotAnalysisSystemPrompt`, append the legend to the espresso/filter system prompts so it's taught once per conversation. Reuse the exact text removed from `buildUserPrompt` so the AI's mental model is unchanged.
- [ ] 3.3 Add tests in `tst_shotsummarizer.cpp`:
  - `buildUserPrompt` output no longer contains the legend preamble.
  - `shotAnalysisSystemPrompt("espresso", ...)` contains the legend.

## 4. Move framing strings to the system prompt

- [ ] 4.1 Stop emitting `currentBean.inferredNote` from `mcptools_dialing.cpp` (delete the assignment in `buildCurrentBean`). The structural fields `inferredFromShotId` + `inferredFields` stay.
- [ ] 4.2 Stop emitting `tastingFeedback.recommendation` when any of the `has*` booleans is false. The structural booleans stay.
- [ ] 4.3 Stop emitting `currentBean.daysSinceRoastNote` (it's superseded by the `beanFreshness.instruction` block in task 6).
- [ ] 4.4 Append a unified "How to read structured fields" section to `shotAnalysisSystemPrompt` covering: inferredFields semantics, tastingFeedback gating ("when has* are all false, ASK before suggesting changes"), and beanFreshness gating ("never quote age until freshnessKnown == true").
- [ ] 4.5 Tests: existing `buildCurrentBean_*` tests in `tst_mcptools_dialing_helpers.cpp` must be updated — `inferredNote` no longer appears; assert its absence on the inference path.

## 5. Strip roastDate from dialInSessions[].shots[]

- [ ] 5.1 In `mcptools_dialing.cpp`'s `shotToJson` lambda (the per-session shot serializer), remove any emission of `roastDate`. (Verify it's actually being emitted today; the `dialInSessions` shape may already omit it — if so, just lock the omission via test.)
- [ ] 5.2 Add a regression test in `tst_mcptools_dialing_helpers.cpp` (or a new integration-style harness) asserting that no `roastDate` field appears anywhere under `dialInSessions[].shots[]`.

## 6. Replace currentBean roast fields with beanFreshness block

- [ ] 6.1 Add `McpDialingHelpers::buildBeanFreshness(const QString& roastDate)` returning a QJsonObject `{ roastDate, freshnessKnown: false, instruction }`. The `instruction` text is fixed, imperative: `"Calendar age from roastDate is NOT freshness — many users freeze and thaw weekly. ASK the user about storage before applying any bean-aging guidance."` Returns an empty object when `roastDate` is empty.
- [ ] 6.2 In `mcptools_dialing.cpp`, replace the existing `currentBean.daysSinceRoast` + `currentBean.daysSinceRoastNote` block with `currentBean.beanFreshness = buildBeanFreshness(roastDateStr)`. The DYE roastDate parsing path is no longer needed in this file (only the raw string is forwarded).
- [ ] 6.3 In `ShotSummarizer::buildUserPrompt`, stop computing the inline "(N days since roast, not necessarily freshness — ask about storage)" parenthetical. Replace with `(roasted YYYY-MM-DD; ask user about storage before reasoning about age)` — no day count.
- [ ] 6.4 Add tests in `tst_mcptools_dialing_helpers.cpp`:
  - Empty `roastDate` → empty QJsonObject (no block surfaced).
  - Populated `roastDate` → block has `freshnessKnown: false` and the instruction.
  - The block does NOT contain a precomputed day count under any key.
- [ ] 6.5 Add a test in `tst_shotsummarizer.cpp` asserting `buildUserPrompt` output contains no "days since roast" string and no day-count number adjacent to a roast date.

## 7. Per-bean grinderContext.settingsObserved

- [ ] 7.1 Extend `ShotHistoryStorage::queryGrinderContext` to accept an optional `beanBrand` filter (default `QString()` for backwards compatibility). When provided, append `AND bean_brand = :brand` to the SQL and bind a third parameter — same conditional-append pattern already used in `loadRecentShotsByKbIdStatic` and `buildFilterQuery`. Update both the implementation in `shothistorystorage_queries.cpp:591–647` AND the static declaration at `shothistorystorage.h:89`.
- [ ] 7.2 In `mcptools_dialing.cpp`, pass `dbResult.shotData.beanBrand` to `queryGrinderContext` so the returned context is bean-scoped. Fall back to the unscoped query when `beanBrand` is empty (no bean recorded).
- [ ] 7.3 When the bean-scoped query returns fewer than 2 distinct settings, also include an `allBeansSettings` array in the response (the unscoped list) so the AI can still see the user's overall range — but tagged so the AI knows it's cross-bean. Document the field semantics in the spec.
- [ ] 7.4 Tests:
  - Add a unit test for `queryGrinderContext` with the new bean filter.
  - Add a test verifying the MCP tool emits `settingsObserved` (bean-scoped) and conditionally `allBeansSettings` (when bean-scoped is sparse).

## 8. Hoist profile metadata to result.profile (single canonical source)

- [x] 8.1 In `mcptools_dialing.cpp`, replace the `result["currentProfile"]` assignment with `result["profile"]` carrying `filename`, `title`, `intent`, `recipe`, `targetWeightG`, `targetTemperatureC`, `recommendedDoseG` (omit when missing). Source `intent` from the resolved shot's `profileJson.notes` (current `profileNotes` — pure CPU computation on the already-fetched shot record, safe to run in the main-thread delivery block); source `recipe` from `Profile::describeFramesFromJson` on the same string; source targets from the live `profileManager` (not the shot's recorded profile). The intent + recipe describe the *shot's* profile, while targets describe the *current* profile loaded on the machine — this asymmetry is intentional (the shot is what happened; the targets are what the user can act on now). Do NOT issue a second DB query to fetch shot-record-level targets; the asymmetry is the design.
- [x] 8.2 In `ShotSummarizer::buildUserPrompt`, drop the lines that emit `"- **Profile**:"`, `"- **Profile intent**:"`, and the `"## Profile Recipe"` section (with the recipe description). Update the rendered prose to stop carrying these.
- [x] 8.3 Update the system prompt in `shotAnalysisSystemPrompt` to teach: "Profile metadata (filename, title, intent, recipe, targets) lives in `result.profile`. Read it from there for any reasoning about profile intent or expected behavior. The prose body carries shot-variable data only."
- [x] 8.4 Tests: assert `result["currentProfile"]` is absent and `result["profile"]` carries the expected fields; assert `shotAnalysis` prose contains no `"Profile:"`, `"Profile intent:"`, or `"## Profile Recipe"` strings.

## 9. Strip shot-invariant identity from shotAnalysis prose

- [x] 9.1 In `ShotSummarizer::buildUserPrompt`, drop the `"- **Coffee**:"` line (bean brand/type/roast level/date string). The bean identity is read from `currentBean`.
- [x] 9.2 In `buildUserPrompt`, drop the `"- **Grinder**:"` brand+model+burrs prefix. The shot-variable `@ <grinderSetting>` SHALL still be available — emit it as part of a renamed `"- **Grind setting**:"` line that omits brand/model/burrs (those live in `currentBean.grinder*` and session context).
- [x] 9.3 Drop the inline "(N days since roast, ...)" parenthetical entirely (was tied to the now-removed Coffee line; the prose-builder no longer needs the days-since-roast computation at all).
- [x] 9.4 Update the system prompt to teach: "Grinder model+burrs and bean identity live in `currentBean` and `dialInSessions[].context`. The prose carries only shot-variable data (`grinderSetting`, dose, yield, ratio, duration, extraction, peaks, phase data, detector observations)."
- [x] 9.5 Tests: extend `tst_shotsummarizer.cpp` with a "prose-shot-summary-is-shot-variable-only" assertion: render a shot whose ShotSummary has populated bean and grinder identity, and verify the prose contains no bean brand/type, no grinder brand/model/burrs, and no roasted-date string.

## 10. Add history-block render mode

- [x] 10.1 Add `enum class ShotSummarizer::RenderMode { Standalone, HistoryBlock }` to `shotsummarizer.h`. Add an overload `QString buildUserPrompt(const ShotSummary& summary, RenderMode mode) const`. Default mode is `Standalone` (preserves single-shot rendering for `dialing_get_context.shotAnalysis` and the in-app dialog).
- [x] 10.2 In `HistoryBlock` mode, the prose body skips ONLY the `## Shot Summary` markdown header line — NOT the content under it. Retain Dose / Yield / Duration / `grinderSetting` / Extraction / Peaks (the shot-variable fields tasks 8+9 left in the section). Profile / Intent / Recipe / Coffee / brand+model+burrs Grinder are already gone from both modes via tasks 8 and 9. Also skip the `## Detector Observations` markdown header line in `HistoryBlock` (the per-line `[warning]` / `[good]` lines themselves still emit since they're shot-variable). Phase data and tasting feedback emit unchanged. The caller (`requestRecentShotContext` line 530) wraps each block in `### Shot (date)` so the omitted top-level headers would be redundant under that wrapper.
- [x] 10.3 Update `AIManager::requestRecentShotContext` (the lambda inside the `QThread::create` worker around `aimanager.cpp:520–565`, which is the only `buildUserPrompt` caller that needs `HistoryBlock`) to:
  - Emit a single `### Profile: <title>\n<intent>\n<recipe>` subsection at the top of the "## Previous Shots with This Bean & Profile" section.
  - Emit a single `### Setup: <grinderBrand> <grinderModel> with <grinderBurrs> on <beanBrand> - <beanType>` subsection when all shots share grinder + bean (AIManager's path runs against `qualifiedShots` directly, so check uniformity inline by iterating once before the per-shot loop).
  - Call `m_summarizer->buildUserPrompt(summary, ShotSummarizer::RenderMode::HistoryBlock)` for each shot inside the existing per-shot loop.
  - The four other `buildUserPrompt` callers — `analyzeShotWithMetadata` (~line 251), `generateEmailPrompt` (~line 319), `generateShotSummary` (~line 353), `generateHistoryShotSummary` (~line 359, called by `mcptools_dialing.cpp:313` to produce `result.shotAnalysis`) — all use `Standalone` (the default mode) and require no signature change. Confirm with grep that no other caller of `buildUserPrompt` exists in `src/`.
- [x] 10.4 Update `ShotSummarizer::buildHistoryContext` symmetrically: emit `Profile` and `Recipe` once at the top of the function's output rather than once per shot.
- [x] 10.5 Tests:
  - In `tst_shotsummarizer.cpp`, render the same `ShotSummary` in `Standalone` and `HistoryBlock` modes; assert `Standalone` carries the full body and `HistoryBlock` omits the documented sections (the two header lines, nothing else).
  - For the `AIManager` end-to-end check, use `QSignalSpy` against `recentShotContextReady` rather than inventing a third pattern. Synthesize a `qualifiedShots` list inline (the `friend class TstAIManager` pattern under `#ifdef DECENZA_TESTING` — see `docs/CLAUDE_MD/TESTING.md` — gives access to the private members needed). Call `requestRecentShotContext()`, wait on the spy, and assert that the captured payload contains exactly one `Profile intent:` paragraph and exactly one `### Setup:` subsection across the 4-shot synthetic history.
  - Avoid extracting a synchronous `buildRecentShotContext` helper just for the test — that's a public-API change with no callsite need.

## 11. Validate and ship

- [ ] 11.1 Run `openspec validate optimize-dialing-context-payload --strict --no-interactive`; resolve issues.
- [ ] 11.2 Run the full test suite via Qt Creator MCP.
- [ ] 11.3 Live verification: re-render `tests/data/dialing/northbound_80s_espresso_pre_change_baseline.json` (turn 2 of the captured 4-shot history) through the new payload + prose pipeline. Diff against the locked baseline; confirm the structural-check assertions documented in `tests/data/dialing/README.md` hold (≤1 `"2026-03-30"` occurrence, 0 `"days since roast"` occurrences, 1 `"Profile intent"` paragraph, 1 `"Profile Recipe"` block, 0 detector-legend preambles).
- [ ] 11.4 Measure post-change token count on iteration 2 of the Northbound conversation (~3,663 tokens pre-change per the baseline README) and confirm a ≥25% reduction in practice. Record the post-change baseline as `tests/data/dialing/northbound_80s_espresso_post_change_baseline.json` so future spec changes can diff against the new shape.
