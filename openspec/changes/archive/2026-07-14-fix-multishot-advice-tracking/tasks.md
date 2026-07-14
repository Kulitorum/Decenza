## 1. Background-thread `recentAdvice` data

- [x] 1.1 In `AIManager::requestRecentShotContext`'s existing grinder-context query (`SELECT ... FROM shots s WHERE s.id = ?`), also select `s.profile_kb_id` so it's available for the `recentAdvice` build.
- [x] 1.2 In the same background-thread block, compute `convKey = AIManager::conversationKey(beanBrand, beanType, profileName)` and call `AIConversation::loadRecentAssistantTurnsForKey(convKey, 3)`.
- [x] 1.3 When turns are non-empty, build `DialingBlocks::RecentAdviceInputs` (`turns`, `currentProfileKbId` from 1.1, `currentShotId = excludeShotId`) and call `DialingBlocks::buildRecentAdviceBlock(db, in)`.
- [x] 1.4 Thread the resulting `QJsonArray recentAdvice` through the existing `QMetaObject::invokeMethod(qApp, ...)` hop into `emitRecentShotContext` (add a parameter, matching the existing `grinderCtx`/`grinderCalibration` pattern).

## 2. Render `## Recent Advice Tracking` in `emitRecentShotContext`

- [x] 2.1 Add a markdown renderer for one `recentAdvice` entry: turns-ago label, `recommendation` sentence, the recommended `structuredNext` fields (grinderSetting/doseG/profileTitle, expected ranges), and `userResponse` (adherence, actual grinderSetting/doseG, outcomeRating0to100 if present, outcomeNotes if present, outcomeInPredictedRange flags).
- [x] 2.2 Append the rendered section (header `## Recent Advice Tracking`) to `result` in `emitRecentShotContext` when `recentAdvice` is non-empty; omit the section entirely when empty (no placeholder header).
- [x] 2.3 Verify section placement relative to `## Grinder Calibration` reads naturally (recent advice should read as "what I told you last time" — place it near the top of the historical context, before the raw shot-by-shot history, so the model sees its own prior call before re-deriving from scratch).

## 3. Wire `setShotIdForCurrentTurn` into the in-app flow

- [x] 3.1 In `qml/components/ConversationOverlay.qml`'s `conversationInput.sendFollowUp()`, call `conversation.setShotIdForCurrentTurn(overlay.shotId)` immediately before the existing `conversation.ask(...)` / `conversation.followUp(...)` calls, guarded on `overlay.shotId > 0`.
- [x] 3.2 Confirm the guard correctly skips free-form follow-ups sent after `overlay.pendingShotSummaryCleared()` has already fired for the current shot (no stale/wrong id gets stamped on an unrelated question).

## 4. Strengthen the taste-feedback-gating system prompt rule

- [x] 4.1 Locate the shared espresso system prompt's existing `tastingFeedback` guidance (single-shot "ask when all three flags are false" rule).
- [x] 4.2 Add the new paragraph (adapted from the A/B-tested Candidate B wording) requiring the model to ask for a taste score before using success/quality language when the last 2+ shots in the conversation have no feedback, framing curve-only observations as preliminary until then.
- [x] 4.3 Confirm the new paragraph doesn't duplicate or contradict the existing single-shot `tastingFeedback` rule or the "If it tasted good (score 80+), acknowledge success!" line in Response Guidelines — the new rule should read as a stricter multi-shot-specific gate, not a replacement.

## 5. Tests

- [x] 5.1 `tst_aimanager`: extend or add a test proving `setShotIdForCurrentTurn` is actually reachable from a simulated `sendFollowUp()`-equivalent call path (or, if QML isn't directly testable here, a C++-level test that stamps shotId via the same call sequence and asserts `shotIdForTurn` returns it).
- [x] 5.2 `tst_aimanager` or `tst_dialing_blocks`: test that `requestRecentShotContext`/`emitRecentShotContext` produces a `## Recent Advice Tracking` section when a qualifying prior turn + follow-up shot exist, and omits it when none qualify (mirrors the existing `buildRecentAdviceBlock` scenarios, exercised through the in-app code path).
- [x] 5.3 Add a byte-for-byte comparison test (or as close as the two different renderings allow) confirming the in-app `## Recent Advice Tracking` section and the MCP `recentAdvice` JSON array carry the same underlying `turnsAgo`/`adherence`/`outcome` data for identical inputs.
- [x] 5.4 `tst_shotsummarizer` (or wherever system-prompt content is tested): assert the strengthened taste-feedback-gating paragraph is present in `shotAnalysisSystemPrompt()`'s output.
- [x] 5.5 Real-provider check (not a Claude subagent replay): called `ai_advisor_invoke` live against GPT-5.4-mini (local Mac instance, shipped system prompt, no override needed) replaying the exact "2+ untasted shots" scenario byte-for-byte. Result: mini now explicitly asks for taste feedback before recommending any change ("taste it before changing anything else" / "send the taste notes... I'll help decide") — a real improvement over the documented baseline (never asked at all). Not a clean pass on the strictest reading: it still frames the curve data with some conclusory language ("the shot is now in the right neighborhood", "doing what this profile wants") rather than fully "preliminary pending taste feedback," and it doesn't use any of the explicitly-banned words (successful/optimal/excellent/dialed in). Net: meaningfully better, imperfect on wording nuance — logged here rather than iterating the prompt further, since design.md's non-goals rule out chasing incremental prompt wins beyond the validated A/B result.

## 6. Manual verification

- [x] 6.1 Build via Qt Creator MCP (0 errors, 0 warnings) and run the full test suite. 2811/2811 passed, 0 warnings.
- [ ] 6.2 On a real device/build, replay a multi-shot conversation where a grind recommendation is deliberately not followed; confirm the next in-app response explicitly calls out the divergence (matching the A/B-tested behavior).
- [ ] 6.3 Confirm no regression on a conversation with a genuine multi-shot trend (still correctly identified) and one with an already-provided score (not re-asked).
