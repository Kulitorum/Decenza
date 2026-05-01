# Design

## Why a design doc

This change touches three subsystems (MCP tool payload, prose builder, system prompt) and makes several non-obvious shape decisions. Pinning them here so reviewers don't have to reverse-engineer intent from the spec.

## Decision: session-context structure

Two reasonable shapes for dedup:

A. **Implicit inheritance** — `shots[i].grinderModel` is omitted when it equals `shots[i-1].grinderModel`. AI infers absence-means-same.
B. **Explicit session.context** — `sessionObj.context` carries common identity, `shots[]` carry only differences.

Going with (B). Implicit inheritance saves slightly more tokens but lets the AI silently misread an absence as "data missing" rather than "same as prior." Explicit context is structurally clear: every shot has identity, it just may live one level up.

Per-shot override semantics: when a shot in the middle of a session has a different `beanBrand` (rare — user rotated bean mid-session), that shot carries `beanBrand` directly under `shots[i]`. The AI reads "if a field appears under the shot, use it; otherwise inherit from `session.context`." Documented in the spec scenarios.

When all shots in a session share identity entirely, `perShotOverrides[i]` is `{}` and the JSON serializer can omit it. Single-shot sessions: `context` carries the shot's identity; the lone shot in `shots[]` has no overrides.

## Decision: beanFreshness contract — no day-count, ever

The closed issue #1022 and the user's repeated note ("I freeze my beans") establish that calendar age without storage context is misleading data. The temptation to "just include the number with a strong warning" loses to the observation that precise-looking numbers anchor the AI regardless of adjacent text.

The block emits `roastDate` (the one atomic fact the user actually entered) and `freshnessKnown: false` plus a fixed `instruction` string. There is **no** computed day-count anywhere in the block, the prose, or the parent `currentBean` object. If the AI wants the calendar age, it must do the subtraction itself in front of the user — which makes the assumption visible.

Future scope (separate change): when the user has confirmed storage mode (a future `dyeBeanStorageMode` setting), `freshnessKnown` flips to `true` and a `effectiveAgeDays` field is added with the storage-adjusted estimate. Today there's no storage-tracking field, so `freshnessKnown` is always `false`.

The `instruction` text is fixed and ships verbatim:

> "Calendar age from roastDate is NOT freshness — many users freeze and thaw weekly. ASK the user about storage before applying any bean-aging guidance."

Reads as imperative because it is one. The previous `daysSinceRoastNote` ("Many users freeze beans and thaw weekly — ask about storage before assuming degradation") read as advisory and was demonstrably skimmed past.

## Decision: roastDate stays on currentBean, leaves dialInSessions

`currentBean.beanFreshness.roastDate` is the *one* surface where the user-entered roast date lives in the payload. The AI can quote it ("you entered April 15 as the roast date") without computing age. This is fine — the user's own data, surfaced as itself.

`dialInSessions[].shots[].roastDate` is removed. Iteration reasoning ("what changed since last shot?") doesn't need it; it's `changeFromPrev` territory and bean rotation within a session is captured by `changeFromPrev.beanBrand`. The implicit-aging-derivation path (subtract from each shot's `timestamp`) is the failure mode this removal closes.

## Decision: per-bean grinderContext, with cross-bean fallback

`grinderContext.settingsObserved` mixes settings used on different beans today. The fix is per-`(profile, bean)` filtering, but a hard cutover would break the case where the user just switched beans and has only one shot on the new bean — the bean-scoped list would be `[currentSetting]`, useless for "what's your range?" reasoning.

Two-tier emission:

- `settingsObserved` is always bean-scoped (filtered by `dbResult.shotData.beanBrand`).
- When bean-scoped returns fewer than 2 distinct settings, also include `allBeansSettings` (cross-bean) so the AI sees the user's overall range — explicitly tagged as cross-bean so it doesn't get misread as "this bean's range."

Spec language: `allBeansSettings` is documented as "user's grinder range across all beans on this profile; do NOT recommend specific values from this list — they were observed on different beans."

## Decision: history-block mode for buildUserPrompt + AIManager assembly

The empirical Northbound conversation shows `AIManager::buildRecentShotContext` calling `buildUserPrompt` for every historical shot and concatenating the results under a "## Previous Shots with This Bean & Profile" header. Each `buildUserPrompt` block carries the full prose body — Profile, Profile intent, Profile Recipe, Phase Data, Detector Observations legend, Tasting Feedback. On a 4-shot history that's 4× repetition of constants that don't vary across shots in the same session.

`buildHistoryContext` (a separate, terser function used by some MCP/tool paths) has a smaller version of the same problem: per-shot `Profile`, `Recipe`, full `Grinder` string repetition.

Two implementation shapes:

A. **Add a `RenderMode` parameter to `buildUserPrompt`** with values `Standalone` (default, full body for single-shot rendering — what `dialing_get_context.shotAnalysis` and the in-app Shot Summary dialog use today) and `HistoryBlock` (omits profile-level constants because the caller emits them once at the top of the history section).
B. **Refactor `buildUserPrompt` into composable section emitters** (`emitShotHeader`, `emitProfileRecipe`, `emitPhaseData`, `emitTastingFeedback`, `emitDetectorObservations`) and have `AIManager` and `buildHistoryContext` call them selectively.

Going with (A) for this change. (B) is the long-term right answer but expands the diff considerably and risks breaking the standalone path. (A) is one parameter, one if-statement around each section, and a tested standalone-vs-history equivalence check.

The `RenderMode::HistoryBlock` mode SHALL omit:
- The "## Profile Recipe" section.
- The "Profile intent" line in the Shot Summary block.
- The "## Detector Observations" legend preamble (which is already moved to the system prompt under task 3 — but the per-shot block doesn't even need the section header when running in history-block mode, since the legend explanation isn't repeated).
- The redundant "## Shot Summary" header line itself (the date heading from `AIManager` already serves this role).

`RenderMode::HistoryBlock` mode SHALL retain:
- Dose / Yield / Ratio / Duration / Grinder setting / Bean (only when different from session top) / Extraction / Score / Notes.
- Phase Data — this is shot-variable and the AI's primary diagnostic input.
- Per-line detector observations (the actual `[warning]` / `[caution]` / `[good]` lines), since the legend in the system prompt teaches how to read them.

`AIManager::buildRecentShotContext` SHALL:
1. Emit the "## Previous Shots with This Bean & Profile" header.
2. Emit a single profile-level subsection (`### Profile: <title>` + intent paragraph + recipe). When all shots in the section share the same profile, this is unconditional. When shots span multiple profiles (rare), fall back to per-shot intent/recipe — but the historical_limit query already filters by profile_kb_id, so cross-profile sections shouldn't occur in practice.
3. Emit a single shot-identity subsection (`### Setup: <grinder> on <bean>`) when all shots share grinder + bean. Per-shot overrides go inline on the differing shot.
4. Call `buildUserPrompt(summary, RenderMode::HistoryBlock)` for each shot.

`buildHistoryContext` follows the same pattern: emit `Profile` + `Recipe` once at the top, emit per-shot blocks without those fields.

## Decision: legend + framing strings move to system prompt, not tool description

Tool descriptions are loaded by Claude when the tool is registered (essentially once per conversation); system prompts are also loaded once. Either would be cheaper than per-call. System prompt wins because:

- It already exists (`shotAnalysisSystemPrompt`) and is explicitly profile-aware.
- It's the natural home for "how to interpret detector observations" guidance.
- Tool descriptions are user-visible in some MCP clients; static framing belongs out of sight.

The three migrating strings:
1. The seven-line `[warning] / [caution] / [good]` legend.
2. `inferredNote` semantics ("inferred fields = ask before recommending").
3. `tastingFeedback.recommendation` semantics ("when has* are all false, ask first").
4. (replacing) `daysSinceRoastNote` semantics → integrated into the new `beanFreshness.instruction` field, *not* migrated to the system prompt. The freshness instruction is bean-state-specific (only emitted when `roastDate` is present) and benefits from sitting next to the `roastDate` field.

## Non-goals

Things explicitly out of scope to keep the change reviewable and the iteration loop coherent:

- Conditional per-phase Start/End sample emission. Per-phase data is the AI's primary diagnostic input; thresholding what counts as "deviating" introduces a tunable that would need its own dial-in.
- Aggregate token-budget cap on `dialInSessions`. The session-context dedup is the bulk of the win; an additional cap is over-engineering.
- `bestRecentShot` grinder identity diff. Most users don't change grinders; the rare case can be handled by the AI noticing the `grinderSetting` numbers don't match the user's range.
- `changeFromBest` vs `changeFromPrev` naming differentiation. `bestRecentShot.daysSinceShot` already gives the AI temporal disambiguation.
- `sawPrediction` confidence banding or recommendation reformulation. Useful refinements but not iteration-loop-critical.
- Surfacing `bestRecentShot.notes` with explicit time framing. `daysSinceShot: 32` already in the parent block is sufficient.
- A storage-mode setting (`dyeBeanStorageMode`). Required for `freshnessKnown: true` but outside this change; today the field is always `false`.

## Empirical anchors

The Northbound Spring Tour 2026 #2 dial-in conversation (4 turns on 80's Espresso, captured 2026-04-30) is the test bed the spec scenarios should hold up against. Measured redundancy in turn 2 (14,652 chars / ~3,663 tokens of context):

| Pattern | Repetitions | Chars per | Total | Disposition |
|---|---|---|---|---|
| `Profile intent` paragraph | 4 (1 per shot) | ~1,000 | ~4,000 | Hoist to `result.profile.intent` + single `### Profile:` history header → ~3,000 chars saved |
| `Profile Recipe` header + 4 frames | 4 | ~210 | ~840 | Hoist to `result.profile.recipe` + single history header → ~630 saved |
| `## Detector Observations` legend preamble | 2 (only on shots with detector lines) | ~430 | ~860 | Move to system prompt → ~860 saved |
| `roasted YYYY-MM-DD (N days since roast, not necessarily freshness — ask about storage)` | 4 | ~75 | ~300 | Strip from prose entirely; canonical source is `currentBean.beanFreshness.roastDate` → ~300 saved |
| `Temperature instability` per-phase line on a stepping profile | 12 | ~110 | ~1,320 | Already fixed by merged #1018 (conversation predates it) |
| `Coffee:` line (bean brand+type+roast level) | 4 | ~95 | ~380 | Strip from prose; canonical source is `currentBean` → ~380 saved |
| `Grinder:` line (brand+model+burrs+setting prefix) | 4 | ~80 | ~320 | Strip brand/model/burrs from prose; keep `grinderSetting` only → ~240 saved |

Total dedup target on turn 2 alone: **~5,400 chars (~1,350 tokens, ~37% reduction)** before counting any session-context dedup of `dialInSessions[].shots[]` fields (a JSON-side change that only applies to the MCP tool path, not the in-app prose path).

What the AI uses across all four replies in the conversation (informs what the spec MUST preserve):

- **Profile-intent prose**, quoted directly: "0.5–1.2 ml/s target", "35–45s is the profile's ideal", "the darker the roast, the slower the recommended flow rate". → `result.profile.intent` is canonical.
- **Per-shot variable fields** assembled into comparison tables: `grinderSetting`, `durationSec`, end-of-pour `flow` (from phase data), preinfusion exit reason (computed from phase duration vs frame duration). → Phase data per shot stays in prose.
- **Per-line detector tags**: "Puck stable" was quoted; the legend explanation was not. → Per-line tags stay; legend moves to system prompt.
- **Extraction measurements**: `TDS 0.30%` — the AI flagged it as suspicious (right call: typical is 7–12%). → TDS/EY stay in prose.
- **Tasting feedback**: AI asked for taste in 3 of 4 replies. → `tastingFeedback.has*` booleans stay; the framing string moves to system prompt.

What the AI never references across the full conversation (informs what the spec safely strips):

- `roastDate`, `daysSinceRoast`, "31 days" — zero references in any reply.
- `Niche Zero with 63mm Mazzer Kony conical` (grinder model+burrs string) — zero references; the AI says "your grinder" or just "grind 4.0".
- Frame-level recipe details ("preinfusion start (2s) FLOW 7.5ml/s 82°C") — zero references; the AI reasons from profile intent, not frame definitions.
- Detector legend explanation prose — zero references; only the per-line tags are used.

Concrete structural checks the implementation MUST pass against this conversation re-rendered through the new payload + prose pipeline:

- The `dialing_get_context` response contains zero occurrences of the substring "days since roast" anywhere — including inside `shotAnalysis` prose, `currentBean.beanFreshness`, and `dialInSessions[*].shots[*]`.
- `currentBean.beanFreshness.roastDate` is the only key path in the response whose key name contains the substring "roast" (case-insensitive).
- The `2026-03-30` literal date appears exactly once in the response (inside `currentBean.beanFreshness.roastDate`).
- The full `Profile intent` paragraph appears at most once across the entire response (in `result.profile.intent` and once in the in-app advisor's history-header rendering — never per-shot).
- Across a 4-shot session on the same grinder + bean, `dialInSessions[0].context` carries the grinder + bean fields; none of `shots[0..3]` carry them.

## Implementation phasing

The change is structured to ship as two PRs against `main`:

- **PR 1 — pure cost wins (tasks 1–7).** Session-context hoist, drop `result["shot"]`, move legend + framing strings to the system prompt, strip `roastDate` from `dialInSessions[].shots[]`, replace `daysSinceRoast` with `beanFreshness`, per-bean `grinderContext.settingsObserved`. None of these depend on the `RenderMode` enum or the `buildUserPrompt` signature change. The AI sees the same facts in fewer tokens; the canonical-source story is unchanged.
- **PR 2 — canonical-source separation (tasks 8–10).** `result.profile` (replacing `currentProfile`), strip Profile/Coffee/Grinder lines from `shotAnalysis` prose, add `RenderMode::HistoryBlock` and rewire `AIManager::requestRecentShotContext` + `buildHistoryContext` to hoist profile/intent/recipe to a single header. Tasks 8 and 9 are purely subtractive on `buildUserPrompt` (strip lines), so they don't require `RenderMode` to exist. Task 10 adds `HistoryBlock` on top.

The split is independently shippable — task 11 (validate + measure) runs at the end of PR 2 against the post-change baseline. A regression in PR 2 can be reverted without touching PR 1's wins.

## Risks

- **AI misreads session.context inheritance.** If the AI treats absence of `grinderModel` on `shots[i]` as "missing data" rather than "same as session," it may ask redundant questions ("what grinder are you using on shot 2?"). Mitigation: spec scenarios pin the inheritance contract, and the system-prompt addendum (task 4.4) explains the structure.
- **Removing `result["shot"]` breaks an external consumer.** No in-tree consumer exists, but external MCP clients may have parsed it. Mitigation: tool description documents the change; `shotAnalysis` prose still carries every field that was in `shot`.
- **`beanFreshness.instruction` text drift.** If the wording changes between releases the AI's behavior may shift. Mitigation: fix the text in a constant in `mcptools_dialing_helpers.h` and pin it in tests.
