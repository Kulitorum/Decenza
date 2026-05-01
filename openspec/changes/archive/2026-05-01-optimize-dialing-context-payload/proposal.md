# Change: Optimize dialing_get_context payload (cost + data quality)

## Why

After PRs #1023–#1026, `dialing_get_context` carries every signal a competent dial-in advisor needs. A simulated audit and a real Northbound Spring Tour 2026 #2 dial-in conversation (4-shot iteration on 80's Espresso, ~30k char system prompt + 4 turns of context) together identify two systemic problems:

1. **~600–900 tokens/call of cross-shot redundancy.** `dialInSessions[].shots[]` repeats `grinderBrand` / `grinderModel` / `grinderBurrs` / `beanBrand` / `beanType` on every shot in an iteration session, even though those values change at most once per session. Static framing strings (`inferredNote`, `daysSinceRoastNote`, `tastingFeedback.recommendation`, the `## Detector Observations` legend) ship every turn instead of being taught once in the system prompt. The `result["shot"]` block duplicates fields the `shotAnalysis` prose already renders.

2. **Roast-date signal leaks past its caveat.** `dialInSessions[].shots[].roastDate` lets the AI silently subtract from each shot's timestamp to derive aging trends across an iteration window — exactly the inference issue #1022 closed, just implicit instead of explicit. `currentBean.daysSinceRoast` ships a precise day-count alongside an advisory note that's easy to skim past. Many users freeze beans; calendar age without storage context is misleading data dressed as precise data.

Empirical validation from the Northbound 80's Espresso conversation (4-shot iteration captured 2026-04-30, turn-2 user message = 14,652 chars / ~3,663 tokens of context):

- **What the AI uses, every reply.** Across all four assistant replies in the conversation, the AI consistently leans on: profile-intent prose quoted directly into advice ("the profile's author specifically says 'the darker the roast, the slower the recommended flow rate'", "0.5–1.2 ml/s target", "35–45s is the profile's ideal"), per-shot comparison tables with shot-variable columns (`Date | Grind | Time | End flow | Preinfusion exit`), end-of-pour flow values from phase data, per-line detector tags ("Puck stable, no channeling"), and extraction measurements (the AI flagged a `TDS 0.30%` reading as suspicious — exactly the kind of judgment call extraction data enables).
- **What the AI never references in replies.** Across all four replies: zero quotes of `roastDate` or any "days since roast" phrase, zero quotes of `Niche Zero with 63mm Mazzer Kony conical` (the grinder model+burrs string), zero references to frame-level recipe details (the AI reasons from intent, not from the frame list), zero use of the detector-legend explanation prose. The data was shipped 3–4 times per turn and consumed 0 times — this is the redundancy the change eliminates.
- **The static framing weight is bigger than initially estimated.** Counted in turn 2 of the conversation:
  - "Profile intent" paragraph (~1,000 chars) repeated 4× = **~4,000 chars** (~28% of the message)
  - "Profile Recipe" header + 4 frames (~210 chars) repeated 4× = **~840 chars**
  - "Detector Observations" legend preamble (~430 chars) repeated 2× = **~860 chars**
  - "31 days since roast, not necessarily freshness — ask about storage" (~75 chars) repeated 4× = **~300 chars**
  - "Temperature instability" line firing 12× across phases on an intentional-stepping profile (~1,320 chars total) — already addressed by the merged #1018 fix (this conversation predates it)
- **Conservative dedup target on this turn alone: ~5,000 chars (~1,250 tokens), or 34% reduction**, before counting any wins from the JSON-side changes.
- **The AI naturally compresses iteration turns.** In turn 3 of the conversation the user shipped 4 KB of context instead of 15 KB, manually constructing a "Changes from Shot (Apr 27)" diff because they (correctly) judged that re-shipping the full session history was redundant. The session-context hoist serves the same goal structurally: identical fields surface once, deltas surface per shot.

Across a 3–5 shot dial-in iteration, the AI doesn't need any of this. Cutting it makes each turn cheaper *and* removes anchors the AI shouldn't be pulling on.

**Operating principle: single canonical source per fact.** Every fact in the response (profile metadata, grinder identity, bean identity, roast date, shot-variable data) SHALL appear in exactly one place. Two surfaces means two opportunities for divergence — between snapshot data persisted with the shot and live data on the user's machine, between JSON precision and prose rounding, between the inline computation and the structural field. Where a fact appears in both prose and JSON today, this change picks the JSON as canonical and strips the prose. The prose-side `shotAnalysis` becomes shot-variable data only (phase physics, detector observations, tasting feedback, dose/yield/duration/grind-setting/extraction). Profile metadata, grinder model+burrs, bean brand+type, and roast date all live in structured JSON blocks at the top of the response, and the system prompt teaches the AI to read them from there.

## What Changes

Cost — same information, fewer tokens:

- **BREAKING (data shape)** Hoist shot identity fields to a session-level `context`. `dialInSessions[].context` carries `grinderBrand`, `grinderModel`, `grinderBurrs`, `beanBrand`, `beanType`. `dialInSessions[].shots[].*` carries only fields that differ from the session context. The first shot of each session always has full identity in `context`; subsequent shots inherit unless overridden.
- **BREAKING (data shape)** Drop the `result["shot"]` JSON block. Its fields are all already rendered in `shotAnalysis` prose; shipping both forced the AI to pick a canonical version.
- Move the `## Detector Observations` legend (the seven `[warning]/[caution]/[good]` explanation lines) out of `shotAnalysis` prose and into the system prompt where it lives once per conversation.
- Move the static `inferredNote`, `daysSinceRoastNote`, and `tastingFeedback.recommendation` framing strings out of the JSON payload and into the system prompt. The fields they qualify (`inferredFields`, the `beanFreshness` block below, `tastingFeedback.has*` booleans) stay in the JSON.
- **BREAKING (prose shape)** When the in-app AI advisor renders multiple historical shots in a single context block (`AIManager::buildRecentShotContext` calling `ShotSummarizer::buildUserPrompt` per shot, and `ShotSummarizer::buildHistoryContext`), profile-level constants (`Profile`, `Profile intent`, `Profile Recipe`) SHALL appear once at the top of the history section, not on every shot. Shot-identity constants (`Grinder`, `Beans`) SHALL appear once at the top when shared across all shots in the section, with per-shot overrides only when a shot differs. Per-shot blocks retain shot-variable fields (dose, yield, duration, grinder *setting*, extraction, score, notes, phase data, detector observations) and `changeFromPrev`-style deltas.

Canonical-source separation — strip invariant data from prose so the JSON is the single authority:

- **BREAKING (prose shape)** Add a top-level `result.profile` JSON block carrying `filename`, `title`, `intent`, `recipe`, `targetWeightG`, `targetTemperatureC`, `recommendedDoseG`. The `shotAnalysis` prose SHALL NOT emit the `Profile:`, `Profile intent:`, or `## Profile Recipe` sections — those move to `result.profile`. Today `result.currentProfile` ships a subset of these fields; the new `result.profile` supersedes it (drop `currentProfile`). One source of profile truth.
- **BREAKING (prose shape)** Strip shot-invariant identity from `shotAnalysis` prose. The prose `## Shot Summary` block keeps ONLY shot-variable fields (`dose`, `yield`, `ratio`, `duration`, `grinderSetting`, `extraction TDS/EY`, `overall peaks`). It drops the `Coffee:` line (bean identity → `currentBean`) and the `Grinder:` model/burrs string (grinder model+burrs → session context or `currentBean`; only the per-shot `grinderSetting` survives in prose). Single canonical source for grinder model+burrs and bean identity.
- **BREAKING (prose shape)** Strip the roast-date reference from `shotAnalysis` prose entirely. Today the prose emits "(roasted YYYY-MM-DD; ask user about storage…)" alongside the bean line. After this change, the bean line is gone from prose and the date+caveat live exclusively in `currentBean.beanFreshness`. The system prompt teaches the AI to read `currentBean.beanFreshness` for any bean-age reasoning. Single canonical source for roast date.

Data quality — apply the "precise-looking but poor-quality data is worse than no data" principle to roast age and grinder context:

- **BREAKING (data shape)** Strip `roastDate` from `dialInSessions[].shots[]`. Iteration reasoning is about within-session deltas (already encoded in `changeFromPrev`); silent aging derivation is a misuse vector with no defensible use case.
- **BREAKING (data shape)** Replace `currentBean.daysSinceRoast` + `daysSinceRoastNote` with a structured `currentBean.beanFreshness` block: `{ roastDate, freshnessKnown: false, instruction }`. The precomputed day-count is gone; the AI must flip `freshnessKnown` (by asking the user about storage) before quoting age. The `instruction` field reads imperative, not advisory.
- **BREAKING (data shape)** Split `grinderContext.settingsObserved` per `(profile, bean)` instead of per `(profile, grinder)`. Today the list mixes settings used on different beans — inviting the AI to suggest "try grind 9, you've used it before" when 9 was on a different bean entirely.

## Impact

- Affected specs: `dialing-context-payload` (NEW capability)
- Affected code:
  - `src/mcp/mcptools_dialing.cpp` — payload assembly: session-context hoisting, `roastDate` removal from session shots, `beanFreshness` block, per-bean grinder context query, drop the duplicated `shot` block.
  - `src/mcp/mcptools_dialing_helpers.h` — pure helpers for session-context dedup and `beanFreshness` construction.
  - `src/ai/shotsummarizer.cpp` — `buildUserPrompt` drops the detector-legend block and the duplicated shot-summary section, and gains a "history-block mode" parameter (or sibling function) that omits profile-level constants when the caller is rendering a per-historical-shot block under a parent history section. `buildHistoryContext` deduplicates `Profile` + `Recipe` to a single top-of-section header. `shotAnalysisSystemPrompt` gains the detector legend + the three framing strings hoisted out of the JSON.
  - `src/ai/aimanager.cpp` — `buildRecentShotContext` (the in-app advisor path that powers conversations like the Northbound Spring Tour 2026 #2 dial-in) emits a single profile-level header at the top of the "## Previous Shots with This Bean & Profile" section and calls `buildUserPrompt` in history-block mode for each shot.
  - `src/history/shothistorystorage.cpp` / `shothistorystorage_queries.cpp` — `queryGrinderContext` gains a `beanBrand` filter (new parameter, not a new function).
- Backwards compatibility: `dialing_get_context` is a read-only tool with no in-tree consumers storing its output. The shape changes are observable only on the next AI turn after upgrade.
- Test impact: extend `tests/tst_mcptools_dialing_helpers.cpp` for session-context dedup + `beanFreshness`, extend `tests/tst_shotsummarizer.cpp` for the prose-body changes (legend removed, shot-summary section removed). No regressions expected on existing scenarios.
- Estimated payload reduction: ~3,950 → ~2,800 tokens/call (-29%) on a typical 4-session iteration history.
