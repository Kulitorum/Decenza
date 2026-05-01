# Change: Drop the redundant nested JSON envelope from dialing_get_context.shotAnalysis

## Why

`dialing_get_context.shotAnalysis` currently ships a string carrying the **entire** user-prompt JSON envelope produced by `ShotSummarizer::buildUserPrompt(summary)`. That envelope contains `currentBean`, `profile`, `tastingFeedback`, and the prose `shotAnalysis` body — so on every call we double-ship `currentBean` / `profile` / `tastingFeedback` against their canonical top-level surfaces in the same response.

Live evidence (issue #1042) shows the inner and outer `currentBean` blocks **disagree on values** for the same shot — `doseWeightG: 18 vs 20`, `type: "TypeA" vs "Spring Tour 2026 #2"`, `roastLevel: "" vs "Dark"` — because the outer block is built from live `Settings::dye()` (DYE-fallback path) and the inner block is built from `summarizeFromHistory(shot)` (shot-derived path). An LLM reading both has to pick one. PR #1034's `currentProfile` vs `profile` divergence was retired for exactly this reason; this is the same class of bug at a different nesting level.

Three problems shipping today:

1. **Double-ship of `currentBean` / `profile` / `tastingFeedback`** with disagreeing values for the same shot.
2. **Nested JSON-in-JSON is hostile to readers** — the model has to parse the outer JSON, then parse the string inside `shotAnalysis` as JSON, just to reach the prose body.
3. **Token cost** — every call ships those three blocks twice plus the JSON braces and escaped whitespace overhead.

## What Changes

- **BREAKING**: `dialing_get_context.shotAnalysis` SHALL be a prose string starting with `## Shot Summary` (or equivalent) and containing `## Phase Data` and `## Detector Observations` sections — **not** a JSON-encoded object. The fields previously double-shipped inside the embedded envelope (`currentBean`, `profile`, `tastingFeedback`) continue to live exactly once at the top level of the response.
- New helper `ShotSummarizer::buildShotAnalysisProse(summary)` SHALL return the prose body (the `## Shot Summary` + `## Phase Data` + `## Detector Observations` markdown). This is the single source for the prose text — both `dialing_get_context.shotAnalysis` and the in-app advisor's user-prompt envelope's `shotAnalysis` field SHALL call it.
- `mcptools_dialing.cpp` SHALL replace its current `ai->generateHistoryShotSummary(shot)` call (which returns the full JSON envelope) with `ai->buildShotAnalysisProseForShot(shot)` (a thin AIManager wrapper around `summarizer->buildShotAnalysisProse(summary)`).
- The in-app advisor's user prompt remains unchanged — its `shotAnalysis` key continues to carry the same prose body via `ShotSummarizer::buildUserPromptObject(summary)`.

## Impact

- Affected specs: `dialing-context-payload` (the response contract for `dialing_get_context.shotAnalysis`).
- Affected code:
  - `src/ai/shotsummarizer.{h,cpp}` — new public method `buildShotAnalysisProse(summary)`. Its body is `renderShotAnalysisProse(summary, RenderMode::Standalone)` minus the JSON envelope wrapping. The existing `buildUserPromptObject` continues to call the same private renderer for its `shotAnalysis` field, so the prose stays in sync between the two surfaces by construction.
  - `src/ai/aimanager.{h,cpp}` — new public `buildShotAnalysisProseForShot(shot)` returning a `QString`. Pairs with the existing `buildUserPromptObjectForShot(shot)` and `generateHistoryShotSummary(shot)`.
  - `src/mcp/mcptools_dialing.cpp` — switch from `generateHistoryShotSummary(shot)` to `buildShotAnalysisProseForShot(shot)`. Single-line change.
  - `tests/tst_aimanager.cpp` — add a regression test asserting `dialing_get_context`'s shotAnalysis path returns prose, not JSON. Done via direct call to `buildShotAnalysisProseForShot` (no full DB stand-up needed).
- Backward compatibility: this is a **one-way breaking change** for the `dialing_get_context.shotAnalysis` field. The system prompt is already written to read top-level `currentBean` / `profile` / `tastingFeedback` (per PR #1034's "How to Read Structured Fields" section), so removing the redundant nested copy aligns the response with what the LLM is already taught.
- Token savings: ~1 KB per call on the example shot 884 captured in issue #1042 (the embedded envelope's `currentBean` + `profile` + `tastingFeedback` + JSON braces and escaped whitespace).
- Cache stability: unaffected. The prose body is byte-stable for identical shot input today; that property is preserved.
