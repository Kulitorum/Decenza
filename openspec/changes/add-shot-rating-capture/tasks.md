# Tasks: Capture shot ratings — three layers

## Layer 1 — Conversational rating capture

- [x] 1. Add `parseUserRatingReply(const QString& reply) -> std::optional<UserRatingReply>` in `src/ai/aimanager.{h,cpp}` (or a `src/ai/ratingparser.{h,cpp}` if scope warrants). `UserRatingReply { int score; QString notes; }`. Pure function. Permissive numeric token detection per spec rules; trim notes; reject out-of-range; reject non-numeric.
- [x] 2. Wire into `AIManager`'s user-message handler (where the conversation overlay's user reply lands and is dispatched to `m_conversation->followUp(...)`). Before dispatching:
  - call `parseUserRatingReply(reply)` once,
  - retrieve `m_conversation->shotIdForTurn(latestUserTurnIndex)` (added by #1053),
  - if both are present (score parsed, shotId non-zero), AND the prior assistant turn's prose contains a recognizable taste-question marker (look for `"hasEnjoymentScore"`-driven phrasings; simplest test: previous assistant message contains `"taste"` AND `"score"` OR `"how did"`),
  - dispatch a background-thread `ShotHistoryStorage::updateShotMetadata` write for `enjoyment` + `espressoNotes` (notes only when non-empty),
  - log warning on DB failure; do NOT block the conversation flow.
- [x] 3. Tests in `tests/aimanager_tests/tst_aimanager.cpp`:
  - `parseUserRatingReply_extractsBareNumber` — `"82"` → 82, empty notes.
  - `parseUserRatingReply_extractsNumberWithNotes` — `"82, balanced and sweet"` → 82, `"balanced and sweet"`.
  - `parseUserRatingReply_acceptsOutOf100` — `"75 out of 100"` → 75.
  - `parseUserRatingReply_acceptsPercent` — `"75%"` → 75.
  - `parseUserRatingReply_rejectsNonNumeric` — `"really good!"` → nullopt.
  - `parseUserRatingReply_rejectsOutOfRange` — `"150"` → nullopt; `"0"` → nullopt.
  - `parseUserRatingReply_takesFirstInRangeNumber` — `"around 80, maybe 85 next time"` → 80.
  - `conversationalRatingPersists` — synthesize an `AIConversation` with prior assistant taste-question, `setShotIdForCurrentTurn(8473)`, dispatch user reply `"82, balanced"`, assert DB receives the write to shot 8473.
  - `conversationalRatingNoOpWhenShotIdAbsent` — same but shotId=0; assert no DB write.
  - `conversationalRatingNoOpWhenPriorAssistantDidntAsk` — score-bearing reply but prior assistant message was not asking for taste; assert no DB write.

## Layer 2 — QuickRatingRow component

- [x] 4. Create `qml/components/QuickRatingRow.qml`:
  - signals `rateClicked(int score)` and `dismissed()`,
  - properties `currentScore` (0 = unrated), `dismissed` (bool — drives collapsed state),
  - three icon `AccessibleButton` instances mapped to high/med/low (80/60/40),
  - one `AccessibleButton` for dismiss with `Accessible.name: TranslationManager.translate("rating.dismiss", "Dismiss rating prompt")`,
  - styled via `Theme.*`, all text via `TranslationManager.translate(...)` / `Tr`,
  - state shows three icons when `currentScore == 0 && !dismissed`, "Rated N — tap to revise" pill when `currentScore > 0`, hidden entirely when `dismissed`.
- [x] 5. Add `qml/components/QuickRatingRow.qml` to `CMakeLists.txt`'s `qt_add_qml_module` file list.
- [x] 6. Wire into `qml/pages/PostShotReviewPage.qml`:
  - instantiate `QuickRatingRow` near the top of the editing column (above metadata),
  - bind `currentScore: editEnjoyment`,
  - bind `dismissed: Settings.value("shotRatingDismissed/" + editShotId, false) === true`,
  - on `rateClicked(score)`: `editEnjoyment = score; saveEditedShot()`,
  - on `dismissed`: `Settings.setValue("shotRatingDismissed/" + editShotId, true)` then re-evaluate the binding.
- [x] 7. QML smoke (manual — call out in the PR description that the project has no QML test harness): build, open `PostShotReviewPage` for an unrated shot, tap each face, confirm persistence; tap dismiss, confirm hide; reopen, confirm stays hidden.

## Layer 3 — Inferred-good auto-rating

- [x] 8. Schema migration in `src/history/shothistorystorage.cpp` (or wherever schema migrations live):
  - guard with column-existence check (`PRAGMA table_info(shot_history)`),
  - `ALTER TABLE shot_history ADD COLUMN enjoyment_source TEXT NOT NULL DEFAULT 'none'`,
  - `UPDATE shot_history SET enjoyment_source = 'user' WHERE enjoyment > 0`,
  - idempotent (re-runnable; no-op when column exists).
- [x] 9. Add `enjoymentSource` to `src/projections/shotprojection.{h,cpp}` (or wherever `ShotProjection` lives) — `Q_PROPERTY(QString enjoymentSource MEMBER enjoymentSource)`. Default `"none"`.
- [x] 10. Update `ShotHistoryStorage::loadShotRecordStatic` (or equivalent) to read the new column.
- [x] 11. Update `ShotHistoryStorage::updateShotMetadataStatic` to:
  - accept `enjoymentSource` in the metadata map,
  - when `enjoyment` is in the map but `enjoymentSource` is not, default `enjoymentSource` to `"user"` (any explicit user-driven write is a user rating),
  - when `enjoymentSource` is in the map, persist verbatim.
- [x] 12. Update post-shot analysis pipeline (`src/ai/shotanalysis.cpp` or the per-shot save handler that already runs detectors and persists drift):
  - after detector results compute and the shot's `enjoyment0to100 == 0`,
  - if all inferred-good criteria pass (verdictCategory clean, channelingSeverity none, grindDirection onTarget, ratio within 0.1 of target ratio OR yield within 0.5g of target weight, duration within ±15% of profile median of last 5 user-rated shots — fallback to `targetDurationSec` when no user-rated history),
  - dispatch `updateShotMetadataStatic({enjoyment: 75, enjoymentSource: "inferred"})` for the shot id.
  - the evaluation runs ONCE per saved-and-unrated shot; do not re-evaluate after the user manually rates (the user write switches `enjoymentSource` to `"user"`).
- [x] 13. Update `buildBestRecentShotBlock` in `src/mcp/mcptools_dialing_blocks.cpp`:
  - SELECT prefers `enjoyment_source = 'user'` rows over `'inferred'` rows; tie-break by enjoyment desc, then timestamp desc,
  - emit `confidence: "user_rated"` or `"inferred"` on the resolved candidate.
- [x] 14. Update `buildCurrentBeanBlock` (in `src/mcp/mcptools_dialing_blocks.h` since it's inline) to emit `enjoymentSource` only when `"inferred"`.
- [x] 15. Update `buildDialInSessionsBlock` to emit per-shot `enjoymentSource: "inferred"` only.
- [x] 16. Extend the espresso `shotAnalysisSystemPrompt` builder in `src/ai/shotsummarizer.cpp` to teach `bestRecentShot.confidence` semantics (treat `"inferred"` as a hint requiring user confirmation).

## Tests for Layer 3

- [x] 17. `tests/dialing_blocks_tests/tst_mcptools_dialing.cpp` (or wherever `buildBestRecentShotBlock` is tested):
  - `bestRecentShotPrefersUserOverInferred` — DB with one user-rated 70 and one inferred 80 in the window; assert resolved is the user-rated 70 with `confidence == "user_rated"`.
  - `bestRecentShotInferredFallback` — only inferred candidates; assert highest inferred wins with `confidence == "inferred"`.
  - `bestRecentShotEmptyWhenNoCandidates` — no rated, no inferred; assert block omitted (existing contract preserved).
  - `currentBeanOmitsEnjoymentSourceForUserRated` — resolved shot user-rated; assert `currentBean` has no `enjoymentSource` key.
  - `currentBeanCarriesEnjoymentSourceForInferred` — resolved shot inferred-rated; assert `currentBean.enjoymentSource == "inferred"`.
  - `dialInSessionsSparseEnjoymentSource` — 3-shot session with mixed sources; assert only the inferred shot carries the field.
- [x] 18. `tests/shotanalysis_tests/tst_shotanalysis.cpp` (or the closest existing harness for the post-shot pipeline):
  - `inferredGoodTriggers` — synthesize all-passing detector results + on-target ratio/duration on a saved unrated shot; assert auto-rate writes 75 + `inferred`.
  - `inferredGoodSkippedOnAnyFailure` — flip each criterion individually; assert no write occurs.
  - `inferredGoodNoOverwriteOfUserRating` — pre-rate the shot as user/90; run the evaluator; assert rating stays 90/user.
  - `userRewriteOfInferredFlipsSourceToUser` — pre-rate as inferred/75; user write of 60; assert persisted as 60/user.
  - `schemaMigrationBackfill` — set up a pre-migration test DB, run migration, assert column added with `'user'` for `enjoyment > 0` rows and `'none'` for the rest.
  - `schemaMigrationIdempotent` — run migration twice; assert second run no-ops cleanly.

## Validation + build

- [x] 19. Run `openspec validate add-shot-rating-capture --strict --no-interactive`; resolve any issues.
- [x] 20. Build via Qt Creator (Decenza-Desktop), run `tst_aimanager` and `tst_mcptools_dialing` (and `tst_shotanalysis` if present), confirm green.
