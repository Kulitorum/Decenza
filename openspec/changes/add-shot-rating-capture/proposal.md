# Change: Capture shot ratings the user actually gives — three escalating layers

## Why

`dialing_get_context.bestRecentShot` is built to anchor advice on the user's highest-rated past shot on the same profile within 90 days. The intent — "anchor advice on what success looked like, not just what changed last shot" — is exactly right.

The problem: in the May-2026 advisor testing run on a real user's database (199 shots, weeks of dial-in iteration), `bestRecentShot` was omitted from the envelope on every call because **none of the 199 shots had `enjoyment0to100` set**. The 90-day window had zero rated shots in it. The block is permanently dark for this user, and likely for most users — rating a shot is a deliberate action that's easy to skip.

Compounding the problem: the advisor *does* ask "how did it taste?" when `tastingFeedback.hasEnjoymentScore` is false, but when the user replies with a numeric score, the conversational answer doesn't propagate to `ShotProjection.enjoyment0to100`. The data plumbing for ratings exists (`ShotHistoryStorage::updateShotMetadataStatic`) but only the manual editor on `PostShotReviewPage` writes through it.

The advisor's most valuable mode is "walk back toward your best shot." Without ratings, that mode never engages.

See issue #1055.

## What Changes

This change ships three escalating layers. All three address the same starvation problem from different angles.

### Layer 1 — Conversational rating capture

When the advisor's assistant message asks for taste feedback (gated by `tastingFeedback.hasEnjoymentScore == false` on the prior turn) AND the user's *next message in the same conversation* contains a parseable numeric score, the score SHALL be persisted back to `ShotProjection.enjoyment0to100` for the shot the conversation is anchored to.

- The shot id used for write-back is the `shotId` recorded on the conversation turn pair (introduced by #1053's per-turn linkage). When `shotId == 0` (legacy or no-shot conversation), the reply is NOT persisted — the linkage is the load-bearing precondition.
- A "parseable score" is a number 1-100 appearing as a standalone token in the user's reply, optionally followed by `/100`, `out of 100`, or `%`. The parser is permissive (extracts the score from a sentence like "82, balanced and sweet") but does not invent scores from non-numeric language ("really good" does NOT become 80; the LLM teaches the user to give a number, but the parser doesn't infer).
- The remaining text — minus the parsed score — SHALL be persisted to `espressoNotes`. When the score is the entire message, `espressoNotes` is left unchanged.
- Existing rating UI on `PostShotReviewPage` continues to work unchanged. When the user has already rated the shot via the editor, conversational rating SHALL still overwrite (the user is being explicitly asked again — last-write-wins by design).
- The write happens through `ShotHistoryStorage::updateShotMetadataStatic` on the existing background-thread path. Failures log a warning; the conversation continues.

### Layer 2 — Lightweight post-shot rating prompt

Today the user-visible rating UI on `PostShotReviewPage` is a numeric slider buried in the metadata editor. This layer adds a *prominent, low-friction* rating row at the top of the review page (above the metadata fold) that:

- displays three icon buttons: 😊 / 😐 / 😞 corresponding to `enjoyment0to100` defaults of 80 / 60 / 40,
- taps persist immediately via the existing `saveEditedShot()` path — no "save" button required,
- displays the current `editEnjoyment` value when it's non-zero,
- collapses to a compact "Rated 80" pill once the user has tapped, with a tap-to-revise affordance,
- the row is fully dismissable for the current shot (tap a small × → row hides, the shot remains unrated, and a per-shot "dismissed" flag prevents the row from re-appearing on subsequent visits to the same shot),
- the precision slider in the metadata editor stays for users who want a number other than 40/60/80,
- this row uses existing components (`AccessibleButton`, `Theme.*`, `TranslationManager`) and follows accessibility rules from `docs/CLAUDE_MD/ACCESSIBILITY.md`.

The "dismissed" flag SHALL be a per-shot QSettings key (`shotRatingDismissed/<shotId>`) — local-only, not synced to history DB. It exists only to keep the prompt from feeling nagging on shots the user has already chosen not to rate.

### Layer 3 — Inferred-good auto-rating with `confidence` flag

When a shot is saved unrated (`enjoyment0to100 == 0`) AND the deterministic detectors fire all of:

- `verdictCategory == "clean"` (no truncation, no skip-first-frame),
- `channelingSeverity == "none"`,
- `grindDirection == "onTarget"`,
- ratio within 0.1 of `targetRatio` (or yield within 0.5g of target weight when no ratio target),
- duration within ±15% of the profile's median duration over the most recent 5 rated shots on the same profile (fall back to ±15% of the profile's `targetDurationSec` when no rated history exists),

THEN the app SHALL provisionally set the shot's `enjoyment0to100` to `75` and stamp it with a new `enjoymentSource` field set to `"inferred"`. User-rated shots use `enjoymentSource = "user"` (the default). The default for legacy unrated shots is `"none"` (no value).

The `bestRecentShot` block SHALL gain a `confidence` field:

- `"user_rated"` when the resolved best shot has `enjoymentSource == "user"`,
- `"inferred"` when `enjoymentSource == "inferred"`.

When both rated and inferred candidates exist in the 90-day window, **the user-rated candidate SHALL win**, even when the inferred candidate has a higher score. Inferred ratings are tiebreakers and back-fill, never primary.

The system prompt SHALL teach the LLM to read `confidence`: "Treat `inferred` as a hint, not ground truth — confirm with the user before strongly anchoring on it."

`enjoymentSource` SHALL also surface in:

- `currentBean` block — when the resolved shot itself was inferred-rated (so the LLM does not anchor on it as a "user-validated" success).
- `dialInSessions[].shots[]` — when individual shots in the session were inferred-rated.

The auto-rating SHALL be re-evaluated when the user manually edits enjoyment via the editor or Layer 2 prompt: the user write switches the field's `enjoymentSource` to `"user"` and the inferred value is replaced.

## Impact

- **Affected specs:**
  - New spec `shot-rating-capture` — pins all three layers' contracts.
  - `dialing-context-payload` (MODIFIED Requirements) — adds `confidence` to `bestRecentShot`, `enjoymentSource` to `currentBean` and `dialInSessions[].shots[]` where relevant.
- **Affected code:**
  - **Layer 1:**
    - `src/ai/aimanager.cpp` — when a user message lands in the conversation overlay, run `parseUserRatingReply(content)` (new helper). When it returns a score AND `m_conversation->shotIdForTurn(latest user turn)` is non-zero AND the prior assistant turn asked about taste, dispatch `ShotHistoryStorage::updateShotMetadataStatic` to persist the score and remaining-text notes.
    - `parseUserRatingReply(const QString&) -> std::optional<UserRatingReply>` where `UserRatingReply { int score; QString notes; }`. Pure function; tested in isolation.
  - **Layer 2:**
    - `qml/components/QuickRatingRow.qml` (new) — three-icon rating row, dismissable, persists via callback.
    - Add to `CMakeLists.txt` qml file list per project conventions.
    - `qml/pages/PostShotReviewPage.qml` — instantiate `QuickRatingRow` near the top, wire to `editEnjoyment`/`saveEditedShot`. Read/write `shotRatingDismissed/<shotId>` via Settings.
  - **Layer 3:**
    - `src/ai/shotanalysis.cpp` (or the post-shot analysis pipeline) — after detector results are computed for a saved-and-unrated shot, evaluate the inferred-good criteria and, when satisfied, write `enjoyment0to100 = 75` + `enjoymentSource = "inferred"` via the same metadata path.
    - `src/projections/shotprojection.{h,cpp}` (or wherever `ShotProjection` is defined) — add `enjoymentSource` (`"none" | "user" | "inferred"`).
    - `src/history/shothistorystorage.{h,cpp}` — schema migration: add `enjoyment_source TEXT NOT NULL DEFAULT 'none'` column with a one-time migration that:
      - back-fills `'user'` for rows where `enjoyment > 0` (existing rated shots are user-rated by definition),
      - leaves `'none'` for rows where `enjoyment <= 0`.
    - `src/mcp/mcptools_dialing_blocks.cpp` — `buildBestRecentShotBlock` reads and emits `confidence` based on `enjoyment_source`. Tie-break logic: prefer `user` over `inferred` regardless of score; within each tier, prefer higher score.
    - `src/mcp/mcptools_dialing_blocks.cpp` — `buildCurrentBeanBlock` and `buildDialInSessionsBlock` emit `enjoymentSource` on shot-level entries when it is `"inferred"`. Omit the field when `"user"` or `"none"` — only the inferred case is informative for the LLM.
    - `src/ai/shotsummarizer.cpp` — system prompt teaches `confidence` semantics on `bestRecentShot`.
- **Tests:**
  - `tests/aimanager_tests/tst_aimanager.cpp`:
    - `parseUserRatingReply_extractsBareNumber` — `"82"` → score 82, notes empty.
    - `parseUserRatingReply_extractsNumberWithNotes` — `"82, balanced and sweet"` → score 82, notes `"balanced and sweet"`.
    - `parseUserRatingReply_acceptsOutOf100` — `"75 out of 100"` → score 75.
    - `parseUserRatingReply_rejectsNonNumeric` — `"really good!"` → `nullopt`.
    - `parseUserRatingReply_rejectsOutOfRange` — `"150"` → `nullopt`.
    - `conversationalRatingPersistsToShot` — synthesize a conversation with an assistant taste-question and a user numeric reply, dispatch through the AIManager handler, assert the shot's `enjoyment0to100` and `espressoNotes` updated.
    - `conversationalRatingNoOpWhenShotIdAbsent` — same flow with `shotId = 0` on the turn pair, assert no DB write occurs.
  - `tests/dialing_blocks_tests/tst_mcptools_dialing.cpp`:
    - `bestRecentShotPrefersUserOverInferred` — DB with one user-rated 70 and one inferred 80, assert the block resolves to the user-rated 70 with `confidence == "user_rated"`.
    - `bestRecentShotInferredFallback` — only inferred candidates, assert the block emits `confidence == "inferred"` and the highest inferred score wins.
    - `bestRecentShotEmptyWhenNoCandidates` — no rated, no inferred, assert block omitted (existing contract preserved).
  - `tests/shotanalysis_tests/tst_shotanalysis.cpp`:
    - `inferredGoodTriggers` — synthesize detector results matching all the criteria, assert auto-rate writes `75` + `enjoymentSource == "inferred"`.
    - `inferredGoodSkippedOnAnyFailure` — flip each criterion individually, assert no auto-rate occurs.
    - `inferredGoodNoOverwriteOfUserRating` — pre-rate the shot with `enjoyment = 90`, run the inferred-good evaluator, assert the rating stays at 90 with `enjoymentSource == "user"`.
  - QML smoke (manual; the project does not have a QML test harness): build, open `PostShotReviewPage` for a shot, tap each face, confirm persistence; tap dismiss, confirm the row hides; reopen the page, confirm the row stays hidden.
- **Affected callers (no signature change unless noted):**
  - The advisor envelope's `tastingFeedback.hasEnjoymentScore` rendering downstream of `currentBean.enjoymentSource == "inferred"`: when the resolved shot is inferred-rated, `hasEnjoymentScore` SHALL still be `true` (a value exists), but the new `enjoymentSource` field tells the LLM not to weight it as ground truth.
- **Migration:**
  - Schema: one-time `ALTER TABLE shot_history ADD COLUMN enjoyment_source TEXT NOT NULL DEFAULT 'none'` + UPDATE for rows with `enjoyment > 0`. Re-runnable (idempotent — guarded by column existence check).
  - Behavior: existing user-rated shots keep their ratings under `enjoymentSource = 'user'`. Existing unrated shots are eligible for Layer 3 inferred-good scanning (gated; see below).
  - Backfill of inferred ratings on the user's history is **OPT-IN** (a one-time prompt on first launch after the migration; defaulting to off). The auto-rating writes are tagged `enjoymentSource == "inferred"`, so the user can revoke the policy and clear them with a single SQL `UPDATE` if needed. The default-off posture matches the project memory note "prefer fewer settings" — this is a one-time migration prompt, not a permanent setting.
  - Going forward: every newly-saved-and-unrated shot is evaluated by the inferred-good criteria as part of the post-shot analysis pipeline. There is no setting to disable this — it is part of the shot-analysis contract, the same way detector observations are.
- **NOT in scope:**
  - Voice-to-text rating capture (Layer 2 mentions short-text; voice is a future enhancement).
  - Adjusting the inferred-good criteria thresholds via UI. They are constants in the analysis pipeline. Future tuning can ship as a follow-up change.
  - Backfilling inferred ratings on shots older than 1 year. The optional one-time backfill is window-limited to the last 365 days to avoid sweeping up legacy data with stale detector logic.
- **Cache stability:** all three layers preserve byte-stability of the user-prompt envelope. `enjoymentSource` is a structural string ("user"/"inferred"/"none") that does not change per call for the same shot. `confidence` on `bestRecentShot` is similarly stable.
