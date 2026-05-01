# Design: Migrate AI advisor user prompt from prose to JSON

## Context

The system prompt for shot analysis (`ShotSummarizer::shotAnalysisSystemPrompt`) instructs the LLM to read structured fields by name: `currentBean.inferredFields`, `currentBean.beanFreshness.freshnessKnown`, `tastingFeedback.hasEnjoymentScore`, `dialInSessions[].context`, etc. These instructions were added during the openspec `optimize-dialing-context-payload` work (PRs #1028, #1030) and assume the LLM receives a JSON payload structured like `dialing_get_context`'s response.

The actual user prompt the in-app advisor ships is built by `ShotSummarizer::buildUserPrompt(summary)`, which currently emits prose markdown:

```
## Shot Summary
- **Dose**: 20.0g → **Yield**: 35.9g ratio 1:1.8
- **Duration**: 26s
- **Grind setting**: 4.5
…

## Phase Data
…

## Tasting Feedback
- No tasting feedback provided

## Detector Observations
- [good] Grind tracked goal during pour
```

None of the system-prompt-referenced fields appear. The LLM is asked to follow rules that key off `currentBean.inferredFields` but never receives `currentBean`.

## Goals

1. The user prompt SHALL deliver every structured field the system prompt references.
2. Single source of truth per field — no duplication between prose and JSON.
3. The migration SHALL keep `ShotSummarizer::buildUserPrompt(summary)` as the entry point. Callers do not change.
4. Existing prose content (Shot Summary, Phase Data, Detector Observations) becomes a `shotAnalysis` string field within the JSON. The phase-by-phase rendering still appears under that field as a single readable block — re-flowing it into structured arrays is out of scope.
5. Tests that asserted on prose substrings migrate cleanly: parse the JSON, assert on field presence/values.

## Non-Goals

- **Routing the in-app advisor through `dialing_get_context`'s full payload.** That would require giving the in-app caller DB scope and access to `MainController` / `Settings::calibration()`. Out of scope for this change. Fields requiring DB/MainController scope (`dialInSessions`, `bestRecentShot`, `sawPrediction`, `grinderContext`) stay un-shipped to the in-app advisor for now, as a known limitation.
- **Restructuring `Phase Data` from a single prose block into per-phase JSON objects.** The current prose is dense but readable; pulling it apart into structured arrays is a separate change.
- **Backwards compatibility with the prose shape.** The system prompt and user prompt update in lock-step; there is no "transition period" where both shapes are accepted.

## Decisions

### Decision: Output is a serialized JSON string, not a `QJsonObject`

`ShotSummarizer::buildUserPrompt(summary)` SHALL continue to return `QString`. The new return is `QJsonDocument(payload).toJson(QJsonDocument::Indented)`. Reasons:

- Keeps the function signature stable. Callers (`AIManager::sendShotAnalysisRequest`, MCP `ai_advisor_invoke`) are already wired to consume a string.
- LLM input is a string. Whatever upstream serialization happens, the bytes shipped to the provider are JSON-as-text.
- Indented JSON is more legible in the `userPromptUsed` echo from `ai_advisor_invoke` (the dryRun A/B testing surface) and helps prompt caching by being deterministic across calls for the same shot.

Rejected: returning `QJsonObject` and serializing at the call site. Forces every caller to learn the new return type and serializes inconsistently across them.

### Decision: Prose content lives in a single `shotAnalysis` JSON field

The existing `## Shot Summary` / `## Phase Data` / `## Detector Observations` prose is well-shaped enough that the LLM parses it correctly. Decomposing those sections into structured arrays (per-phase objects, per-detector objects) is meaningful work that risks regression in the deterministic detector pipeline.

Therefore the migration keeps the prose intact as a string under the `shotAnalysis` key:

```json
{
  "currentBean": { … },
  "currentProfile": { … },
  "tastingFeedback": { … },
  "shotAnalysis": "## Shot Summary\n\n- **Dose**: 20.0g …"
}
```

This is the same prose content `dialing_get_context` already returns under its `shotAnalysis` field — by design, the advisor user prompt and the dialing context become structurally identical for the fields they share.

Rejected: a flat structure where each phase becomes a JSON object. Possible future work, but not load-bearing for this change.

### Decision: Out-of-scope fields that need DB scope are simply absent

The in-app advisor's call site in `AIManager` builds a `ShotSummary` from the loaded shot record without DB access. Fields like `dialInSessions`, `bestRecentShot`, `sawPrediction`, `grinderContext` need either DB queries or `Settings::calibration()` / `ProfileManager` lookups that aren't available in the build path.

Rather than synthesize empty placeholders, this change simply omits those fields from the in-app advisor's user prompt. The system prompt's references to them remain accurate for the `dialing_get_context` consumer (an external AI client like Claude Code) and the in-app advisor sees a strict subset.

A follow-up change could route the in-app advisor through the same `withTempDb` + helper composition that `dialing_get_context` uses, getting the full payload at both surfaces. Tracked separately so this change stays small.

Rejected: shipping empty `dialInSessions: []` / `bestRecentShot: null` keys to "match the schema." That would mislead the LLM ("there are no rated shots" vs. "we didn't compute this"). Omission is honest.

### Decision: Migration is a hard cut-over, no feature flag

The user prompt and system prompt are tightly coupled. A flag-gated rollout would require maintaining two prose shapes simultaneously and the coupled system prompt would have to gate its instructions on the flag too — not feasible.

The mitigation is the system prompt is already written for the new shape (the references to `currentBean.inferredFields` etc. are already there). Cut-over flips the user prompt to match what the system prompt was already promising.

### Decision: Migration MUST not regress prompt caching, and SHOULD improve it

`AnthropicProvider::buildCachedSystemPrompt` (`src/ai/aiprovider.cpp:329-345`) wraps the system prompt with `cache_control: {"type": "ephemeral"}` so the long shotAnalysis system prompt (system rules + reference tables + profile catalog + profile knowledge — easily 10K+ tokens) caches across repeated calls within the 5-minute TTL.

Two properties this change must hold:

1. **Byte-stability for the same input.** Cache hits depend on byte-for-byte identity of the cached prefix. The migration MUST produce the same bytes every call for the same `ShotSummary` input. Concretely:
   - JSON serialization SHALL use `QJsonDocument::Indented` with deterministic field ordering (`QJsonObject` is sorted by key in Qt 6, satisfying this).
   - The user prompt SHALL NOT carry any wall-clock timestamp, request id, or other per-call value that would bust caching. (`dialing_get_context` includes `currentDateTime` at the top level; the in-app advisor's user prompt SHALL omit it — the LLM has access to clock context separately if it ever needs it.)
   - String fields with embedded floats SHALL use a fixed precision (already done in the prose path: `'f', 1` for grams, `'f', 2` for ratios).

2. **Improvement: cache the per-shot user-prompt context too.** Today only the system prompt has `cache_control`. The user prompt is sent uncached on every call. For multi-turn conversations on the same shot (in-app conversation overlay's follow-up flow), the per-shot context is also stable — only the latest user question varies.

   The JSON migration SHALL be paired with a follow-up cache breakpoint: when the AnthropicProvider sends a multi-message conversation, the FIRST user message (carrying the JSON shot payload) SHALL have `cache_control: {"type": "ephemeral"}` applied so subsequent turns reuse the cached prefix. The variable portion (the user's follow-up question) appears in subsequent `messages[]` entries, uncached.

   This mirrors the system-prompt pattern. Net effect: a 4-turn dial-in conversation on a single shot caches both the system prompt AND the per-shot context, paying full input cost only for the new follow-up text on each turn (typically <100 tokens).

   Single-shot calls (the `ai_advisor_invoke` MCP path with no follow-up) see no caching benefit on the user prompt — the cache write happens but no read follows. Net cost: a one-time cache-write surcharge (~25% of input cost), amortized across multi-turn or skipped if the call is single-shot. To avoid the unconditional surcharge, the cache_control on the user message SHALL be applied only when the message belongs to a conversation with at least one prior turn, OR when the caller signals "expect follow-ups."

   Implementation: simplest path is to always set cache_control on user message with ephemeral TTL — the cache-write cost is small and the multi-turn benefit dominates. A more conservative path adds a flag to `AIProvider::sendAnalysisRequest` that the in-app conversation overlay sets (multi-turn) but the MCP `ai_advisor_invoke` clears (single-shot).

Verification: a `tst_shotsummarizer` test SHALL assert `buildUserPrompt(summary) == buildUserPrompt(summary)` for identical inputs (byte-stable). A separate test SHALL assert that a sample of 10 well-formed `ShotSummary` instances produce strictly deterministic output across two builds.

Rejected: putting cache_control on every user message unconditionally without measurement. The 25% cache-write surcharge can outweigh the benefit if the 5-minute TTL expires before the next read. Decision tracked in tasks for measurement before defaulting on.

## Open Questions

- Should the `shotAnalysis` field strip the leading `## Shot Summary` / `## Phase Data` / `## Detector Observations` headers, since those headers are now redundant once the field is keyed `shotAnalysis`? Leaning toward keeping them for skim-readability when the LLM logs the prompt back. Cost: ~30 redundant tokens per call.

- The system prompt references fields the advisor user prompt won't ship (`dialInSessions`, `sawPrediction`, etc., per Decision 3). Should the system prompt fork into a "dialing-context flavor" and an "in-app advisor flavor" that drops references to absent fields? Probably yes, but tracked as follow-up — for this change the system prompt stays as-is and the LLM tolerates the gap.
