# Tasks

## 1. Expose the prose-only renderer

- [x] 1.1 Add `QString ShotSummarizer::buildShotAnalysisProse(const ShotSummary& summary) const` to the public API. Body: return `renderShotAnalysisProse(summary, RenderMode::Standalone)`. The private renderer is unchanged — the new method is a thin wrapper that exposes only the prose body, no JSON envelope.
- [x] 1.2 Add `QString AIManager::buildShotAnalysisProseForShot(const ShotProjection& shotData)`. Pairs with the existing `buildUserPromptObjectForShot` / `generateHistoryShotSummary`. Body: `summarizeFromHistory(shotData)` then `m_summarizer->buildShotAnalysisProse(summary)`.

## 2. Switch dialing_get_context to the prose-only path

- [x] 2.1 In `src/mcp/mcptools_dialing.cpp`, replace the `generateHistoryShotSummary(dbResult.shotData)` call with `buildShotAnalysisProseForShot(dbResult.shotData)`. The local variable stays named `analysis` and is still assigned to `result["shotAnalysis"]`.
- [x] 2.2 Verified by inspection that no other call site in `mcptools_dialing.cpp` depends on `result["shotAnalysis"]` being a JSON envelope — the outer response carries the structured fields at top level.

## 3. Lock the contract with a regression test

- [x] 3.1 `tst_AIManager::buildShotAnalysisProseForShot_returnsProseNotJson`: builds a `ShotProjection`, calls the helper, asserts the return value (a) carries `## Shot Summary` and `## Phase Data` headers, (b) does NOT contain `"currentBean"` / `"tastingFeedback"` / `"profile":` (the structured-field block names that would only appear if the JSON envelope leaked through), and (c) does NOT parse as a JSON object.
- [x] 3.2 `tst_AIManager::buildShotAnalysisProseForShot_matchesEnvelopeShotAnalysisField`: builds the same shot, captures both `buildShotAnalysisProseForShot(shot)` and `buildUserPromptObjectForShot(shot)["shotAnalysis"].toString()`, asserts they are `==`. Pins the single-source contract.

## 4. Verify the in-app advisor's user prompt is unchanged

- [x] 4.1 Whole-suite run confirms `tst_shotsummarizer::buildUserPrompt_shotAnalysisFieldPreservesProseSubstrings` and `buildUserPrompt_byteStableForSameInput` still pass. 1945/1945 pass.
- [x] 4.2 The in-app advisor's user prompt envelope is unchanged — it still calls `buildUserPromptObject(summary)` which renders prose under `shotAnalysis` via the same private renderer the new helper uses.

## 5. Validation + sign-off

- [x] 5.1 `openspec validate drop-nested-envelope-in-dialing-shot-analysis --strict --no-interactive` passes.
- [x] 5.2 Whole-project test run: 1945 passed, 0 failed, 0 skipped. Build: 0 errors, 0 warnings.
- [x] 5.3 Live-verified against shot 884 on the new build: `dialing_get_context.shotAnalysis` is plain prose starting with `## Shot Summary`, contains no `"currentBean":` / `"tastingFeedback":` / `"profile":` substrings, no nested JSON braces. ~1 KB smaller than the pre-change response.
- [x] 5.4 Live cross-check: `ai_advisor_invoke` (dryRun: true) on shot 884 returned `userPromptUsed` whose `shotAnalysis` field carries the same prose body byte-for-byte as `dialing_get_context.shotAnalysis`. Single-source contract holds end-to-end.
