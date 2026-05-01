# Tasks

## 1. Extract dialing block builders into a shared module

- [x] 1.1 Create `src/mcp/mcptools_dialing_blocks.h` (paired `.cpp` if needed for non-trivial bodies). Sits alongside `mcptools_dialing_helpers.h`.
- [x] 1.2 Define `QJsonArray buildDialInSessionsBlock(QSqlDatabase& db, const QString& profileKbId, qint64 resolvedShotId, int historyLimit)`. Body lifted from the `dialing_get_context` registration's session-grouping loop. Same `kDialInSessionGapSec` threshold, same `groupSessions` + `hoistSessionContext` calls, same per-shot serializer (`shotToJson`), same `changeFromPrev` chaining.
- [x] 1.3 Define `QJsonObject buildBestRecentShotBlock(QSqlDatabase& db, const QString& profileKbId, qint64 resolvedShotId, const ShotProjection& currentShot)`. Body lifted verbatim — same 90-day window constant `kBestRecentShotWindowDays`, same `enjoyment > 0` filter, same `changeFromPrev(best, current)` diff under `changeFromBest`.
- [x] 1.4 Define `QJsonObject buildGrinderContextBlock(QSqlDatabase& db, const QString& grinderModel, const QString& beverageType, const QString& beanBrand)`. Body lifted verbatim — same bean-scoped → cross-bean fallback, same `allBeansSettings` tagging.
- [x] 1.5 Define `QJsonObject buildSawPredictionBlock(Settings* settings, ProfileManager* profileManager, const ShotProjection& currentShot)`. Main-thread only (touches `settings->calibration()` and `profileManager->baseProfileName()`). Body lifted verbatim — same espresso-only gate, same `flowAtCutoff > 0` gate, same `predictedDripG >= 0.2` recommendation gate.
- [x] 1.6 Refactor `mcptools_dialing.cpp` to call the new helpers. Inline construction code is removed; behavior stays byte-equivalent.

## 2. Lock parity between MCP and in-app surfaces with golden tests

- [ ] 2.1 (Deferred) Golden DB-backed test for `buildDialInSessionsBlock`. Requires standing up a real schema; gating-only coverage shipped in `tst_aimanager` for now (see 2.4 / 6.4). Tracked as a follow-up.
- [ ] 2.2 (Deferred) Same — `buildBestRecentShotBlock` over-90-days golden test. Gating coverage shipped (`bestRecentShotBlock_returnsEmpty_whenProfileKbIdEmpty`).
- [ ] 2.3 (Deferred) Same — `buildGrinderContextBlock` cross-bean fallback golden test. Gating coverage shipped (`grinderContextBlock_returnsEmpty_whenGrinderModelEmpty`).
- [x] 2.4 `tst_AIManager::sawPredictionBlock_omittedWhenFlowAtCutoffIsZero` — empty-flow ShotProjection → empty `QJsonObject`. Covers the most failure-prone gate (the SAW estimator's window) without needing DB infrastructure.

## 3. Expose `buildUserPromptObject` so DB-scoped callers can append blocks

- [x] 3.1 Add `QJsonObject ShotSummarizer::buildUserPromptObject(const ShotSummary& summary, RenderMode mode = RenderMode::Standalone)` returning the unwrapped envelope. Body lifted from current `buildUserPrompt` minus the final `QJsonDocument(...).toJson(...)` call.
- [x] 3.2 Refactor `buildUserPrompt(summary, mode)` to call `buildUserPromptObject(summary, mode)` and serialize. Behavior unchanged for existing synchronous callers (`generateEmailPrompt`, `generateShotSummary`, `generateHistoryShotSummary`, history-block path).
- [x] 3.3 Confirmed byte-stability: `tst_shotsummarizer::buildUserPrompt_byteStableForSameInput` and the rest of the suite (1940/1940) pass after the refactor.

## 4. Enrich the in-app advisor user prompt

- [x] 4.1 In `AIManager::analyzeShotWithMetadata`, replace the synchronous `m_summarizer->buildUserPrompt(summary)` with `m_summarizer->buildUserPromptObject(summary)`.
- [x] 4.2 Extend the existing background-thread `withTempDb` closure to also call `buildDialInSessionsBlock`, `buildBestRecentShotBlock`, and `buildGrinderContextBlock`.
- [x] 4.3 In the `QMetaObject::invokeMethod(qApp, ...)` main-thread continuation, call `buildSawPredictionBlock(m_settings, m_profileManager, resolvedShot)`.
- [x] 4.4 Merge the four blocks into the unwrapped `QJsonObject`, suppressing keys whose builder returned an empty value. Then serialize and append the existing `historyContext` text block.
- [x] 4.5 Wire `ProfileManager*` into `AIManager` (Settings already wired). `MainController::setAiManager` calls `setProfileManager(m_profileManager)`.
- [x] 4.6 Plumb the resolved `ShotProjection` from the bg-thread DB call up to the main-thread continuation (load via `loadShotRecordStatic(db, lastSavedShotId)`).

## 5. Apply the same enrichment to `ai_advisor_invoke` (MCP)

- [x] 5.1 In `mcptools_ai.cpp`'s background-thread closure, after `loadShotRecordStatic` + `convertShotRecord`, also call the three DB-backed block builders against the same `db` connection.
- [x] 5.2 In the main-thread continuation, build the user prompt via `ai->buildUserPromptObjectForShot(shot)` (new primitive — see 5.3), merge the four blocks (calling `buildSawPredictionBlock` here for the SAW one), and serialize.
- [x] 5.3 Added `AIManager::buildUserPromptObjectForShot(shot)` returning `QJsonObject` so the MCP path can avoid round-tripping through string serialization.

## 6. Lock byte-equivalence between in-app and MCP user prompts

- [ ] 6.1 (Deferred) `tst_aimanager::userPromptParity_inAppAdvisorVsMcpAdvisorInvoke` — drive both surfaces from the same input and assert `==`. Requires real DB stand-up; deferred. Byte-equivalence is by construction (both call the same `McpDialingBlocks` helpers + same `buildUserPromptObject`).
- [ ] 6.2 (Deferred) `tst_aimanager::userPromptCarriesDialingBlocks_whenDbScopeAvailable` — needs DB.
- [ ] 6.3 (Deferred) `tst_aimanager::userPromptOmitsDialingBlocks_whenPreconditionsFail` — needs DB. Gating-level omission coverage shipped in section 2.
- [x] 6.4 `tst_aimanager::buildUserPromptObjectForShot_omitsCurrentDateTime` — assert no `currentDateTime` key in the un-enriched envelope (the four enrichment keys are layered on by callers and never carry a `currentDateTime` themselves either).

## 7. Verify cache stability across multi-turn

- [ ] 7.1 (Pending live verification) With the in-app conversation overlay, send a shot analysis on a shot that triggers all four blocks (rated history within 90 days; scale configured; flow data present), then send a follow-up. Capture turn 2's `cache_read_input_tokens`. Assert it is non-zero AND ≥ the size of the cached system + first-user blocks.
- [ ] 7.2 (Pending live verification) Confirm the system-prompt cache hit (already working today) is not regressed.
- [x] 7.3 Update `docs/CLAUDE_MD/AI_ADVISOR.md` to note the four blocks now ship in the in-app user prompt and how the bg-thread enrichment is wired.

## 8. Validation + sign-off

- [x] 8.1 `openspec validate add-dialing-blocks-to-advisor --strict --no-interactive` passes.
- [x] 8.2 Full `tst_mcptools_dialing_helpers`, `tst_shotsummarizer`, and `tst_aimanager` suites pass via Qt Creator. Whole-project test run: 1940 passed, 0 failed, 0 skipped. Build: 0 errors, 0 warnings.
- [ ] 8.3 Live verification: send a shot analysis through the in-app advisor; capture `userPromptUsed` echo via `ai_advisor_invoke` for the same shot; confirm the JSON shapes match.
- [x] 8.4 Confirmed `buildUserPrompt`'s return is only ever shipped to the LLM (no production callsite treats it as prose markdown for human display). Regex consumers in `AIConversation::processShotForConversation` read `shotAnalysis` field substrings, which are unchanged.
