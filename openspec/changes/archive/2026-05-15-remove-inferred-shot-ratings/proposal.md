## Why

Layer 3 of the May-4 `add-shot-rating-capture` change (issue #1055) auto-stamps clean+onTarget+yield-OK+duration-OK unrated shots with `enjoyment=75, enjoymentSource="inferred"` so the advisor's `bestRecentShot` block stays warm even when the user never rates anything. In practice it has produced two failures:

1. **User-visible breakage** — issue [#1150](https://github.com/Kulitorum/Decenza/issues/1150): the Visualizer "Default Shot Rating" setting is silently overwritten. A user who set it to 0% still gets shots saved at 75%.
2. **No measurable advisor benefit** — the inferred 75 carries `confidence: "inferred"` which the system prompt teaches the LLM to ignore as a hint. The LLM already receives the underlying signals (`verdictCategory: "clean"`, `channelingSeverity: "none"`, `grindDirection: "onTarget"`) directly. Synthesizing a score the LLM is told to disregard adds plumbing without changing decisions.

Treat Layer 3 as a failed experiment. Going forward, a shot's rating is whatever the user provided (manually, via QuickRatingRow, or via conversational reply) and nothing else.

## What Changes

- **BREAKING**: post-shot save no longer auto-rates clean shots. Shots saved without a user rating remain at whatever default the user configured (`SettingsVisualizer::defaultShotRating`, same as pre-Layer-3 behavior).
- **BREAKING**: `bestRecentShot.confidence` and `currentBean.enjoymentSource` / `dialInSessions[].shots[].enjoymentSource` removed from the dialing-context envelope. The `tastingFeedback.hasEnjoymentScore` boolean is again the sole signal for "user has / has not rated this shot."
- **BREAKING**: `ShotProjection::enjoymentSource` field removed. MCP `enjoymentSource` filter on `shots_list` and `enjoymentSource` validation on `updateShot` removed.
- **BREAKING**: `shots.enjoyment_source` SQLite column dropped (migration 16). Before the drop, rows with `enjoyment_source = 'inferred'` have their `enjoyment` reset to the user's configured default rating (QSettings key `shot/defaultRating`, fallback `75`), so the auto-stamped 75s from the last ~11 days stop polluting `bestRecentShot` and local Visualizer uploads.
- **Visualizer back-sync**: any row reset by migration 16 that has a non-empty `visualizer_id` SHALL be re-PATCHed to visualizer.coffee with the corrected `espresso_enjoyment` value, so the cloud copy of the shot matches the corrected local record. The sync is deferred to first app boot after the migration (network/credentials may not be ready at migration time), persisted in a QSettings pending list that survives crashes / no-network startups, and drains lazily once Visualizer credentials are available.
- Layer 1 (conversational rating capture) and Layer 2 (QuickRatingRow) stay exactly as they are.
- System prompt teaching about `inferred` ratings, `confidence`, and the "treat inferred as a hint" framing all removed.

## Capabilities

### New Capabilities

_None._

### Modified Capabilities

- `shot-rating-capture`: remove the Layer 3 requirements — the inferred-good evaluator that auto-rates 75, the `ShotProjection.enjoymentSource` field with values `"none" | "user" | "inferred"`, the `bestRecentShot.confidence` field, the `currentBean` / `dialInSessions[].shots[]` `enjoymentSource` emissions, and the schema migration that added the `enjoyment_source` column. Layer 1 (conversational rating capture) and Layer 2 (QuickRatingRow) requirements remain unchanged. (The `bestRecentShot.confidence` and per-shot `enjoymentSource` requirements live in this spec, not in `dialing-context-payload`, so this is the only spec needing modification.)

## Impact

- **Code:**
  - `src/history/shothistorystorage.cpp` — drop the auto-rating block (~lines 1014-1042) and the `enjoyment_source` column reads/writes throughout (insert binding ~1184, select projection ~1502, migration 14 lines 742-758, data-import path ~2315-2358, metadata column map ~1822). Add migration 16: `UPDATE shots SET enjoyment = <user default> WHERE enjoyment_source = 'inferred'; ALTER TABLE shots DROP COLUMN enjoyment_source`.
  - `src/history/shotprojection.h` — remove `enjoymentSource` field and any related serialization.
  - `src/ai/dialing_blocks.h` — remove `enjoymentSource: "inferred"` emission on `currentBean` and `dialInSessions[].shots[]`; remove `confidence` on `bestRecentShot`; restore user-rating-only selection logic (revert preference tiering).
  - `src/ai/shotsummarizer.cpp` / `.h` — remove `enjoymentSource` field on the input struct, the system-prompt paragraphs teaching `inferred` / `confidence`, and any tie-break code.
  - `src/mcp/mcptools_shots.cpp` — drop the `enjoymentSource` filter argument and the field in the output payload.
  - `src/mcp/mcptools_write.cpp` — drop the `enjoymentSource` field in the `updateShot` schema and validator.
- **Docs:**
  - `docs/SHOT_REVIEW.md` — remove any mention of the inferred mechanism; the recompute-on-load contract no longer touches rating.
- **Tests:**
  - Delete tests added by `add-shot-rating-capture`'s Layer 3: `inferredGoodTriggers`, `inferredGoodSkippedOnAnyFailure`, `inferredGoodNoOverwriteOfUserRating`, `bestRecentShotPrefersUserOverInferred`, `bestRecentShotInferredFallback`.
  - Add migration-16 test: synthesize a DB with three rows (`enjoyment=75, source='inferred'`, `enjoyment=90, source='user'`, `enjoyment=0, source='none'`), run migration with `shot/defaultRating=50`, assert the inferred row's enjoyment becomes `50`, the column is gone, the other two rows untouched.
  - Update bestRecentShot tests to expect no `confidence` field and user-rating-only candidate gate.
- **Migration is one-way.** A user who downgrades to a Layer-3 build after this ships will find the column missing and migration 14 will re-add it (default `'none'`) — the reset enjoyment values stay reset. Acceptable: downgrade isn't a supported flow.
- **MainController:** new `processPendingVisualizerRatingSync()` method, hooked into the existing post-boot Visualizer-ready signal path (no new timers per the CLAUDE.md "no timers as guards" rule). Drains the pending list one entry at a time using the existing `VisualizerUploader::updateShotOnVisualizer` API.
- **No setting / UI surface changes.** The Visualizer "Default Shot Rating" already exists and is the canonical default. After this change it once again becomes the actual default for unrated shots.
