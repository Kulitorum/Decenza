# Change: Migrate AI advisor user prompt from prose to JSON

## Why

The shotAnalysis system prompt explicitly references structured fields the AI is supposed to read — `currentBean.inferredFields`, `dialInSessions[].context`, `tastingFeedback.hasEnjoymentScore`, `currentBean.beanFreshness.freshnessKnown`, `bestRecentShot`, and `sawPrediction`. The `dialing_get_context` MCP tool already produces these fields in JSON. But the in-app advisor (and `ai_advisor_invoke`) ships a prose user prompt produced by `ShotSummarizer::buildUserPrompt` that contains only Shot Summary, Phase Data, Tasting Feedback, and Detector Observations — none of the structured fields the system prompt promises.

The result: the system prompt is writing checks the user prompt can't cash. The LLM is told "read `currentBean.inferredFields`" but the field never arrives. Three openspec changes (`optimize-dialing-context-payload`, `currentbean-grinder-fallback` — #1024, `best-recent-shot` — #1025, `saw-prediction` — #1026) added structured signals to `dialing_get_context`. None of them reach the in-app advisor today.

## What Changes

- **BREAKING**: `ShotSummarizer::buildUserPrompt(summary)` SHALL return a JSON-shaped payload instead of prose markdown. The shape mirrors `dialing_get_context`'s response (minus the dialing-only wrapper fields like `currentDateTime` and `shotId`, which the in-app advisor already has via Settings/MainController).
- The new payload SHALL carry: `currentBean` (with `inferredFields` / `inferredFromShotId` / `beanFreshness` when populated), `currentProfile` (with `intent` and `recipe`), `tastingFeedback` (with `hasEnjoymentScore` / `hasNotes` / `hasRefractometer`), and the existing prose `shotAnalysis` text as a `shotAnalysis` field.
- The advisor `ai_advisor_invoke` and the in-app conversation overlay SHALL switch to the JSON shape transparently (same callers, same `buildUserPrompt(summary)` signature, only the returned content changes).
- The system prompt's "How to Read Structured Fields" section becomes load-bearing instead of aspirational — every field it references SHALL appear in the user prompt.
- **NOT in scope:** `dialInSessions`, `bestRecentShot`, `sawPrediction`, and `grinderContext`. Those require DB / MainController scope (`withTempDb`, `Settings::calibration()`, `ShotHistoryStorage::queryGrinderContext`) and the in-app advisor's call site does not have that context. Adding them is a separate change (route the in-app advisor through `dialing_get_context`'s code path entirely, or extend `ShotSummary` to carry these fields). This proposal restricts itself to fields the existing `ShotSummary` already has.

## Impact

- Affected specs: new spec `advisor-user-prompt` (the contract for `ShotSummarizer::buildUserPrompt`'s output).
- Affected code:
  - `src/ai/shotsummarizer.cpp` — replace `buildUserPrompt`'s `QTextStream`-driven prose construction with `QJsonObject` construction returning a serialized JSON string.
  - `src/ai/shotsummarizer.h` — no signature change; same `QString` return.
  - `src/mcp/mcptools_ai.cpp` — the `ai_advisor_invoke` echo of `userPromptUsed` will reflect the new shape automatically; verify the MCP response contract still parses.
  - `tests/tst_shotsummarizer.cpp` — every test that asserts on prose substrings (e.g. `"## Shot Summary"`, `"- **Dose**: …"`, `"Temperature instability"`) needs to re-target the JSON shape (`payload.value("shotAnalysis").toString().contains(...)` or `payload.value("currentBean").toObject()["brand"].toString()`).
  - `tests/tst_aimanager.cpp` — same migration.
- Affected callers (no signature change, behavior change only):
  - `AIManager::sendShotAnalysisRequest` (in-app conversation overlay)
  - `AIManager::ai_advisor_invoke` MCP tool
- Backward compatibility: this is a one-way migration. The system prompt is updated in lock-step so the LLM expectations and prompt payload stay consistent.
