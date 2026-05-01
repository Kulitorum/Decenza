# Tasks

## 1. Audit current prose path

- [ ] 1.1 Map every caller of `ShotSummarizer::buildUserPrompt` (in-app `AIManager::sendShotAnalysisRequest`, MCP `ai_advisor_invoke`, tests).
- [ ] 1.2 Capture the prose output for ~5 representative `ShotSummary` instances (single-phase shot, multi-phase shot, shot with detector warnings, shot with full DYE, shot with blank DYE — falling back) so the migration can assert behavioral parity on the `shotAnalysis` field.
- [ ] 1.3 Verify the prose path is currently NOT byte-stable across calls (or confirm it is) — establishes the baseline for the byte-stability requirement.

## 2. Extend ShotSummary with the structured fields the new payload requires

- [ ] 2.1 Confirm `ShotSummary` already carries: bean brand/type/roastLevel/roastDate (from DYE), grinder brand/model/burrs/setting (from DYE OR resolved-shot fallback), profile title/intent/recipe, target weight/temperature, tasting flags. If any are missing, add them.
- [ ] 2.2 Add an `inferredFields` (`QStringList`) and `inferredFromShotId` (`qint64`) on `ShotSummary` if not already present, populated by the same logic `dialing_get_context`'s `buildCurrentBean` helper uses (DYE wins, fallback to resolved shot for grinder/dose).
- [ ] 2.3 Confirm `ShotSummary` does NOT carry any wall-clock or per-call value that would bust caching. If it does (e.g. `currentDateTime`), exclude it from the rendered payload.

## 3. Implement JSON `buildUserPrompt`

- [ ] 3.1 Add a private helper `ShotSummarizer::buildUserPromptJson(const ShotSummary&)` that returns a `QJsonObject` carrying the new shape (see spec).
- [ ] 3.2 Compose the payload from existing `ShotSummary` fields. Reuse the existing prose-rendering logic for `shotAnalysis`: extract everything from `## Shot Summary` through the end of `## Detector Observations` into a string field.
- [ ] 3.3 Add a private helper `buildBeanFreshness(roastDate)` (or reuse the one from `mcptools_dialing_helpers.h` if extraction is reasonable) so the bean-freshness block matches the dialing-context shape exactly.
- [ ] 3.4 Replace `buildUserPrompt`'s body with `QJsonDocument(buildUserPromptJson(summary)).toJson(QJsonDocument::Indented)`.
- [ ] 3.5 Verify output is alphabetically ordered (Qt 6 default) — no manual sorting needed.

## 4. Lock byte-stability with a regression test

- [ ] 4.1 Add `tst_shotsummarizer::buildUserPrompt_byteStableForSameInput` — call `buildUserPrompt(summary)` twice on the same `ShotSummary`; assert `==`.
- [ ] 4.2 Add `tst_shotsummarizer::buildUserPrompt_omitsWallClockFields` — assert the output JSON does NOT contain keys named `currentDateTime`, `requestId`, `nowMs`, or any other timestamp.
- [ ] 4.3 Add `tst_shotsummarizer::buildUserPrompt_omitsOutOfScopeKeys` — assert the output JSON does NOT contain keys `dialInSessions`, `bestRecentShot`, `sawPrediction`, `grinderContext`.

## 5. Apply user-message cache_control for multi-turn

- [ ] 5.1 Inspect `AIManager::sendShotAnalysisRequest` and the conversation-overlay path to see how `messages[]` is assembled across turns. Determine whether the FIRST user message (per-shot context) is preserved across turns or rebuilt each call.
- [ ] 5.2 If preserved across turns: add `cache_control: {"type": "ephemeral"}` to the first user message in `AnthropicProvider::sendAnalysisRequest` when `messages.size() > 1`. If rebuilt each call: pause and reconsider — caching only works on stable byte-equivalent prefixes.
- [ ] 5.3 Add a `bool expectFollowUps` parameter to `AIProvider::sendAnalysisRequest` (default `true` from in-app, `false` from MCP `ai_advisor_invoke`). Set `cache_control` on the first user message only when the flag is true OR when `messages.size() > 1`.
- [ ] 5.4 Update `mcptools_ai.cpp::ai_advisor_invoke` to pass `expectFollowUps=false`.

## 6. Verify cache caching is exercised end-to-end

- [ ] 6.1 With the in-app conversation overlay, send a shot analysis, then send a follow-up. Capture the Anthropic response's `usage.cache_read_input_tokens` for turn 2. Assert it is non-zero AND ≥ the size of the cached system + first-user blocks.
- [ ] 6.2 Confirm the system-prompt cache hit (already working today) is not regressed: `cache_read_input_tokens` on turn 2 SHALL be at least as large as before this change.
- [ ] 6.3 Document the multi-turn + cache verification in `docs/CLAUDE_MD/AI_ADVISOR.md` so future changes know to preserve the byte-stability + breakpoint placement.

## 7. Migrate existing tests from prose-substring to JSON-field assertions

- [ ] 7.1 In `tests/tst_shotsummarizer.cpp`, find every `QVERIFY*` that calls `prompt.contains(QStringLiteral("..."))` against a buildUserPrompt result. Replace with `QJsonDocument::fromJson(prompt.toUtf8()).object().value(...).toString().contains(...)` where the substring is a prose section, OR with direct field assertions where the substring tests for a structured value.
- [ ] 7.2 In `tests/tst_aimanager.cpp`, same migration.
- [ ] 7.3 Re-verify the existing `intentionalCrossPhaseSteppingSuppressesPerPhaseTempProse` test (and friends) still pass — they assert on `"Temperature instability"` substring; this string still appears in `shotAnalysis` field, just not at the top level of the payload.

## 8. Update the system prompt's "How to Read Structured Fields" section

- [ ] 8.1 The system prompt currently references `currentBean.inferredFields`, `currentBean.beanFreshness`, `tastingFeedback.hasEnjoymentScore`, etc. Verify the new payload delivers each — they're now load-bearing.
- [ ] 8.2 The system prompt also references `dialInSessions[].context`, `dialInSessions[].shots[].changeFromPrev`, and (post #1025/#1026) `bestRecentShot` and `sawPrediction`. These are NOT in the in-app advisor's user prompt. Either:
  - (a) Fork the system prompt into "dialing-context flavor" (full payload references) and "in-app advisor flavor" (subset references), OR
  - (b) Leave the system prompt as-is and accept the LLM tolerates references to absent fields gracefully.
  Decide and document. Lean toward (b) for this change; (a) tracked as follow-up.

## 9. Validation + sign-off

- [ ] 9.1 `openspec validate migrate-advisor-user-prompt-to-json --strict --no-interactive` passes.
- [ ] 9.2 Full `tst_shotsummarizer` and `tst_aimanager` suites pass via Qt Creator.
- [ ] 9.3 Live verification: send a shot analysis through the in-app advisor; capture `userPromptUsed` echo; confirm the JSON shape matches the spec; confirm a follow-up turn shows `cache_read_input_tokens > 0` covering both system + first user blocks.
- [ ] 9.4 No production callsite invokes `buildUserPrompt` and treats the return value as prose markdown for human display (it's only ever shipped to the LLM). Confirm this.
