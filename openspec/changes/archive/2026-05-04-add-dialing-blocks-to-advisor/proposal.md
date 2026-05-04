# Change: Route in-app advisor user prompt through dialing-context block builders

## Why

PR #1034 (`migrate-advisor-user-prompt-to-json`) put the in-app advisor's user prompt into a JSON envelope so the shotAnalysis system prompt's structured-field references would land on real fields. It intentionally scoped out four blocks that need DB / `MainController` / `Settings::calibration()` scope: `dialInSessions`, `bestRecentShot`, `sawPrediction`, `grinderContext`.

Today the system prompt teaches the LLM to read `dialInSessions[].context`, `bestRecentShot.changeFromBest`, and `sawPrediction.predictedDripG` — but on the in-app advisor path those fields never arrive. The MCP `dialing_get_context` tool already produces them. The in-app advisor and `ai_advisor_invoke` ship a JSON envelope that stops at `currentBean` / `profile` / `tastingFeedback` / `shotAnalysis`.

User-facing impact (from issue #1036):

- `bestRecentShot` gives the AI an anchor for "what does success look like on this profile?" — turns reactive advice into aspirational ("walk back toward the parameters of your best shot 14 days ago").
- `sawPrediction.predictedDripG` lets the AI explain a 0.5g undershoot as post-cut drip in 30 seconds, instead of starting a 5-shot grind chase.
- `dialInSessions` with hoisted `context` lets the AI see "what moved between adjacent shots" without re-deriving from prose.
- `grinderContext.smallestStep` lets the AI calibrate grind change advice ("try 0.5 finer" vs. "grind finer") to the user's actual grinder resolution.

## What Changes

- Extract the four DB / settings backed block builders out of `mcptools_dialing.cpp` into a new shared module so both `dialing_get_context` and the in-app advisor / `ai_advisor_invoke` call the same code (option 2 from issue #1036). The new module sits next to `mcptools_dialing_helpers.h` and exposes:
  - `buildDialInSessionsBlock(db, profileKbId, resolvedShotId, historyLimit) -> QJsonArray`
  - `buildBestRecentShotBlock(db, profileKbId, resolvedShotId, currentShot) -> QJsonObject`
  - `buildGrinderContextBlock(db, grinderModel, beverageType, beanBrand) -> QJsonObject`
  - `buildSawPredictionBlock(settings, profileManager, currentShot) -> QJsonObject` (main-thread only — needs `Settings::calibration()` and `ProfileManager`)
- `mcptools_dialing.cpp` SHALL be refactored to call the new helpers. Its response shape SHALL be byte-equivalent to today (parity baseline).
- `AIManager::analyzeShotWithMetadata` (in-app advisor, conversation overlay) SHALL extend its existing background-thread DB work to also call the three DB-backed builders. The main-thread continuation SHALL call `buildSawPredictionBlock` (needs `Settings` + `ProfileManager`) and merge all four blocks into the user prompt JSON envelope before sending to the provider.
- `ai_advisor_invoke` (MCP) SHALL do the same enrichment in its existing background-thread path, so the `userPromptUsed` echo is identical to what the in-app advisor sends.
- `ShotSummarizer::buildUserPrompt` SHALL gain a sibling that returns a `QJsonObject` (not a serialized `QString`), so callers with DB scope can add the four blocks before serialization without parse → modify → re-serialize. The existing `QString`-returning `buildUserPrompt` SHALL keep working for synchronous callers (email export, history block) by wrapping the new helper.
- The four added blocks SHALL be omitted (no key, no `null` placeholder) when their preconditions don't hold — same gating `dialing_get_context` uses today (no rated shot in the 90-day window → no `bestRecentShot`; no scale + profile + flow data → no `sawPrediction`; etc.).
- Cache stability: the enriched user prompt SHALL NOT introduce per-call wall-clock or per-request unique values. `daysSinceShot` (the only wall-clock-derived field, already shipped by `dialing_get_context`) is acceptable — it changes on day boundaries, not per call. `currentDateTime` SHALL NOT appear in the user prompt.

## Impact

- Affected specs:
  - `advisor-user-prompt` — gains the four blocks as documented contract; out-of-scope clause from PR #1034 lifts.
  - `dialing-context-payload` — refactor-only, with a parity requirement noting that `dialing_get_context`'s blocks are now produced by shared helpers (no shape change).
- Affected code:
  - New: `src/mcp/mcptools_dialing_blocks.h` (header-only or paired `.cpp`) — the four block builders.
  - `src/mcp/mcptools_dialing.cpp` — replace inline block construction with calls to the shared helpers.
  - `src/ai/aimanager.cpp` — extend `analyzeShotWithMetadata`'s background-thread closure to also produce the four blocks; main-thread continuation merges them into the user prompt JSON envelope.
  - `src/mcp/mcptools_ai.cpp` (`ai_advisor_invoke`) — same enrichment in its background-thread path so MCP echo and in-app match byte-for-byte.
  - `src/ai/shotsummarizer.{h,cpp}` — add `buildUserPromptObject(summary, mode) -> QJsonObject`; existing `buildUserPrompt` becomes a thin serializer wrapper.
  - `tests/tst_mcptools_dialing.cpp` — gains shared-helper unit tests; existing dialing_get_context tests retarget to assert the helper-produced shape.
  - `tests/tst_aimanager.cpp` — new parity test: build a fake DB + ShotProjection, call the in-app advisor's enrichment path, assert each of the four blocks matches `dialing_get_context`'s shape exactly for the same input.
- Affected callers (no signature changes; behavior change only):
  - In-app conversation overlay (`AIManager::sendShotAnalysisRequest` flow).
  - `ai_advisor_invoke` MCP tool.
- NOT in scope:
  - Removing the redundant nested `shotAnalysis` JSON envelope inside `dialing_get_context`'s response (the embedded user prompt currently double-ships `currentBean` / `profile` / `tastingFeedback`). Tracked as a follow-up — orthogonal to this change's scope.
  - Reshaping any of the four blocks. They're shipped as-is; only their delivery surface expands.
