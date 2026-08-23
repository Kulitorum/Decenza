## 1. Shared equipment match

- [x] 1.1 Add one helper so all seven selection points ask the equipment question the same way
      rather than each writing its own SQL fragment. Landed as
      `ShotHistoryStorage::equipmentBucketSql`, in the HISTORY layer rather than in
      `DialingBlocks`: `shots.equipment_id` is storage's own concept, and putting it in the AI
      layer would have made storage depend on AI. `COALESCE` on BOTH sides, because SQL
      `NULL = NULL` is not true — a bare `equipment_id = ?` drops every unpackaged shot, and
      bucket 0 is a real matching bucket, not a null.
- [x] 1.2 Add one helper that renders an equipment package as an English phrase (grinder brand /
      model / burrs, basket brand / model, puck-prep techniques), omitting absent components.
      Used by the in-app Setup header and the no-history block; see design D3. Landed as
      `describeEquipmentSet` at file scope in `aimanager.cpp`.

- [x] 1.3 **A shot-id-to-bucket helper was written and then deleted.** It returned
      `std::optional<qint64>` because collapsing a "no resolved shot" sentinel to bucket 0 scopes
      the query to unpackaged shots, silently returning an empty history to every user who HAS a
      package — returning 0 for a missing row looks like the safe default and is the exact
      opposite of one. It went away once the callers took a `ShotProjection` instead: the bucket
      then arrives on the same row as the fields it scopes, so there is nothing to resolve
      separately and no sentinel to get wrong. The optional was the right answer to a question
      that stopped being asked.

## 2. In-app advisor history (`AIManager`)

- [x] 2.1 `loadQualifiedShots`: look up the current shot's equipment bucket and add
      `COALESCE(equipment_id, 0) = ?` to the candidate query. Log the bucket alongside the
      existing bean/profile filter debug line.
- [x] 2.2 Hoist `basketBrand`, `basketModel` and `puckPrep` in `emitRecentShotContext` beside the
      existing grinder/bean seeds, and render them into the `### Setup:` header via 1.2.
- [x] 2.3 Emit the no-history block when zero shots qualify: name the equipment set, say why
      other shots were excluded, and tell the model not to refer to shots it cannot see.
- [x] 2.4 Pass the current shot's equipment description from the background thread through to
      `emitRecentShotContext` (the DB work already runs there; extend the existing single
      round-trip rather than adding a query).

## 3. Change detection (`AIConversation`) — DROPPED, foreclosed by task 5

- [~] 3.1 Report a package change in `processShotForConversation`'s changes line. **Not built.**
      Task 5 makes the equipment package part of the conversation key, so every shot in a thread
      shares a package by construction and `findPreviousShot` only ever searches within the
      current thread. The diff could therefore never fire — it would be a branch with no reachable
      input, which is the same defect as a test that cannot fail. Equipment is still named to the
      model on this surface, by the Setup header (2.2) and the no-history block (2.3).
- [~] 3.2 Dropped with 3.1.

## 4. MCP / shared dialing blocks

- [x] 4.1 `loadRecentShotsByKbIdStatic`: add the equipment filter, and select the basket and
      puck-prep items so the projection carries them.
- [x] 4.2 `DialingHelpers::ShotIdentity`: add `basketBrand`, `basketModel`, `puckPrep`; populate
      them in `buildDialInSessionsBlock` and emit them from the existing hoist/override path.
- [x] 4.3 `buildBestRecentShotBlock`: add the equipment match to the best-shot query. Omit the
      block rather than widening when no rated shot on this package qualifies (design D5).
- [x] 4.4 `queryGrinderContext`: scope the observed-settings query to the equipment package,
      including the cross-bean fallback path.
- [x] 4.5 `buildGrinderCalibrationBlock`: replace the grinder-model + burrs package match with
      the resolved shot's own package, so mined pairs and the current-batch anchor cannot
      straddle two baskets (design D6). Verify it still degrades to directional rather than
      erroring when the narrowed pool is too sparse.
- [x] 4.6 Confirm `dialing_get_grinder_calibration` inherits 4.5 through the shared builder with
      no separate change.

## 5. Conversation threading

- [x] 5.1 Add the equipment package to `conversationKey` and update every caller
      (`switchConversation`, the `recentAdvice` lookup in `requestRecentShotContext`, and QML's
      `ConversationOverlay.openWithShot`).
- [x] 5.2 Carry the equipment identity on the conversation index entry so the conversation list
      can distinguish two threads that share a bean and profile.
- [x] 5.3 Verify a pre-upgrade thread is simply unreferenced rather than erroring on load
      (`conversationKey_preUpgradeThreadIsNotResumed`). Retention itself is unchanged — the key
      is still one opaque string in the same LRU, so eviction had nothing to adapt to.
- [x] 5.4 **Found in review, not planned.** The key alone does NOT deliver "a clean conversation
      after upgrading", which is what the user asked for literally.
      `loadMostRecentConversation()` runs at construction and restores the newest thread BY KEY,
      with no bean/profile/equipment lookup that could fail to match — so a pre-upgrade thread
      comes back at startup regardless of the key change. Added
      `clearAllConversationsOnce("equipment_scoped_conversations_v1")`. design.md D4 rewritten;
      it previously asserted no migration was needed, which was wrong.

## 5b-5k. Review rounds — moved to the PR description

Eight review rounds ran over this change. Their round-by-round record (what each found, what was
fixed, what was deferred) lived here and has been moved to the body of
[#1852](https://github.com/Kulitorum/Decenza/pull/1852): its readers are the reviewers of that PR,
and keeping it here made this file 897 lines, of which ~650 were chronicle. It had also gone stale
and self-contradictory — the top of the file described a `equipmentBucketForShot` that round five
deleted, and a fail-closed branch that was removed as unreachable.

The two findings from those rounds that are durable are recorded as code comments where they
apply, not here:

- **Why a stored thread with no index entry must NOT be swept.** `appendAssistantTurnForKey` is the
  MCP `ai_advisor_invoke` write path — a static that writes storage and never indexes — so
  stored-and-unindexed is the designed resting state of an MCP thread until `switchConversation`
  adopts it. A sweep built on the opposite assumption destroyed two real threads on a live device.
  Guarded by `anMcpWrittenThreadSurvivesARelaunch`.
- **Why `loadConversationIndex` must not trim.** `importConversationsStatic` appends restored
  entries to the tail and eviction takes from the tail, so trimming on the load path — which
  `reloadConversations()` runs after every import — deleted the transcripts a restore had just
  written.

Two items remain open and are deliberately NOT fixed in this change:

- [ ] 5x.1 **Unchecked `QSettings` write failures in the eviction path.** On an `AccessError` store
      the group removal and the index write are dropped together while the log asserts the eviction
      happened. Pre-existing. The fix is the sample-before/compare-after latch dance, and getting
      that wrong is what produced a destructive-loop defect in this change's fourth round.
- [ ] 5x.2 **A cup lifted on a sub-20 g shot settles at ~0 g and trains SAW on it.** The
      cup-removed gate tests drops only AND requires the weight to have passed 20 g, so on a
      ristretto neither clause fires. Real, characterised, pre-existing, and gated on corpus-tuned
      thresholds — changing them needs a `shot_eval` regression run.


## 6. Prompt rules

- [x] 6.1 Add the grind-comparability rule to the shared espresso system prompt: a numeric
      setting is comparable only within one equipment set, and an out-of-order setting should
      raise an equipment question before a grinder-mechanism conclusion.
- [x] 6.2 Extend the existing other-profile-setpoint anti-fabrication rule to cover shots,
      scores and taste notes not present in the context.

## 7. Tests

- [x] 7.1 Equipment scoping excludes a same-bean, same-profile shot on another package, and is a
      no-op for a user whose shots all have no package
      (`equipmentScoping_excludesOtherPackagesFromHistoryAndAnchor`,
      `equipmentScoping_isNoOpForUserWithNoPackages`).
- [x] 7.2 `emitRecentShotContext` renders basket and puck prep in the Setup header, omits an
      absent basket cleanly, and emits the no-history block when nothing qualifies — plus the
      no-label case, which must stay silent rather than emit a half-sentence.
- [~] 7.3 Dropped with task 3.
- [x] 7.4 `dialInSessions` excludes other-package shots and hoists the basket/puck-prep fields to
      session context.
- [x] 7.5 `bestRecentShot` picks this package's best rather than a higher-rated shot from
      another, and is absent when this package has no rated shot.
- [x] 7.6 `grinderContext` reports only this package's settings and range
      (`grinderContext_reportsOnlyThisPackagesSettingsAndRange`). **This box was checked with no
      test written**; the test exists now and also covers the two things that made it worth
      writing: the `:equip` NAMED bind in both the settings and RPM queries (a positional `?`
      there does not filter nothing — the driver errors and the whole block silently disappears),
      and that the cross-bean fallback widens the bean without widening the package.
- [x] 7.7 `grinderCalibration` does not mine a cross-package pair, and degrades to directional
      when the package-scoped pool is too sparse.
- [x] 7.8 `conversationKey` differs for two packages sharing a bean and profile, and matches for
      the same package.
- [x] 7.9 Confirm each new test fails against the pre-change behaviour before keeping it (a test
      that cannot fail is a comment that compiles). Add test FUNCTIONS to existing files rather
      than new `tst_*.cpp` files — a new test file costs ~1.4 s of build time forever, a new slot
      costs milliseconds.
- [x] 7.10 `AIManager::loadQualifiedShots` — the in-app advisor's own history query, the headline
      path, and the only selection not reachable through `DialingBlocks`. Moved from a file-scope
      static to a private static member so `friend class tst_AIManager` reaches it, and covered by
      `loadQualifiedShots_scopesHistoryToTheShotsEquipmentPackage` against a real database.
- [x] 7.11 The one-time wipe (5.4) fires on construction and fires only ONCE
      (`construction_wipesConversationsOnceForEquipmentScoping`). The second half matters: a wipe
      that ran every launch would delete the user's live thread on every app start, a worse bug
      than the one it fixes, and the first assertion alone would not see it.
- [x] 7.12 `recentAdvice` pairs the advice with a follow-up on the SAME package
      (`recentAdvice_followUpMustBeOnTheSamePackage`) — the fixture puts an other-basket shot
      first in time so an unscoped lookup would pick it.
- [x] 7.15 Round-two tests: `construction_wipeAlsoRemovesTheLegacyThread` (5d.1),
      `clearThenReopenWithoutSending_createsNoGhostEntry` (5d.4),
      `equipmentId_projectionAndBucketAgreeForTheSameShot` (the read/write key pairing that
      `mcptools_ai.cpp` depends on and that no test target compiles), and a `stepSize` assertion
      added to the existing grinderContext test to pin the deliberately-unscoped carve-out.
      `construction_wipesConversationsOnceForEquipmentScoping` could not fail — `settings.clear()`
      dropped the OLDER migration's marker, so the pre-existing v1.7.2 wipe emptied the group by
      itself and the assertions passed with the new line deleted; confirmed from a suite log
      showing both migrations firing inside the test. It now spends that marker first and asserts
      its own marker was stamped. `clearThenContinue_...` was renamed and rewritten to exercise
      the write-time seam the fix actually moved to.
- [x] 7.14 `clearThenContinue_putsTheConversationBackInTheIndex` (5c.1) and
      `appendAssistantTurnForKey_storesSystemPromptSoTheThreadCanBeContinued` +
      `appendAssistantTurnForKey_doesNotOverwriteAnExistingSystemPrompt` (5c.2). The
      no-overwrite half is the one that can regress silently: overwriting is invisible until a
      multi-shot conversation quietly loses its multi-shot section.
- [x] 7.13 Test-hygiene fixes from review: `ShotRow` gained `puckPrep` (the third identity
      component, which forks a package on its own); the session-context assertion checks basket
      BRAND as well as model; the Setup-header assertion is scoped to the Setup LINE rather than
      the whole payload (the basket also appears in per-shot blocks, so the old assertion passed
      with the header empty); `conversationKey_preUpgradeThreadIsNotResumed` gained a positive
      control, since with 5.4 in place two separate mechanisms produce its empty thread.

## 8. Verify

- [ ] 8.6 **KNOWN GAP, stated rather than implied:** `src/mcp/mcptools_ai.cpp` is compiled by no
      test target (`tests/CMakeLists.txt` lists only `mcptools_ai_conversations.cpp`), so the
      `systemPrompt` pass-through, the single-bucket resolution and the conversation-key write
      side are unverified by the suite. Adding the TU would pull in `MainController`; 7.15's
      pairing test covers the one property the key depends on, and the rest is covered only by
      the by-eye run in 8.3.
- [x] 8.1 Full suite via the Qt Creator MCP. Latest run, after 5e.14 and 5e.15:
      **113 passed, 0 failed, 0 warnings**, with all 12 new slots confirmed executed in
      `LastTest.log` (a passing binary count says nothing about whether a new slot compiled in).
      The run before that reported one `tst_decentscalewifi` failure
      (`sleepWithDisconnectedSocketDoesNotLeakLatch`, plus three
      `QNativeSocketEngine::write() was not called in ConnectedState` warnings on the
      `0.0.0.1:80` probes) which passed on an immediate re-run and touches nothing in this
      change. Earlier runs: first run **113 passed, 0 failed**; after the round-two fixes
      **113 passed, 0 failed, 0 warnings**, with all five new slots
      confirmed executed in `LastTest.log` (a passing binary count says nothing about whether a
      new slot compiled in). The six pre-existing
      `tst_dialing_blocks` failures seen on the first run were the `std::optional` defect in 1.3
      and are fixed; one further failure was a bad assertion in a new test (a single-profile
      fixture forms no pairs, so `coffeeAnchor` is never emitted) and was rewritten to assert the
      exclusion directly.
- [x] 8.2 `openspec validate filter-advisor-history-by-equipment --strict` — valid.
- [x] 8.3b Driven on the running app a third time, after the round-5 fixes: the scoping verified
      in both directions on real interleaved data, and the exercise found four defects no review
      round had reached. Recorded in full in section 5h.
- [x] 8.3 Done on the running macOS build, twice (once per build). Asked the in-app advisor
      directly which shots it was comparing against: it named only same-package shots and left
      out the other package's, which are the same bean, same profile, same grinder, same basket
      and CLOSER in time. The one-time wipe fired at startup (`cleared all conversations for
      migration "equipment_scoped_conversations_v1"`, index reloaded with 0 entries) and the two
      packages produced two distinct stored keys matching the computed hashes, with neither the
      legacy nor the unpackaged-bucket key present. Opening from the Shot Review page — the
      `clonePersistedShot` path — landed on the same package key, so no third orphan key. NOT
      confirmed: the `### Setup:` prose header and the no-history block — not observed in the
      app, reason NOT established. The explanation first recorded here ("every in-app path sends
      the JSON shot-data envelope instead") is wrong: `requestRecentShotContext` is called at
      ConversationOverlay.qml:191, lands in `historicalContext` at :253 and is prepended to the
      outgoing message at :721, so `emitRecentShotContext` IS on the live in-app path — which
      design.md says too, and switching to the envelope is an explicit non-goal there. The
      likeliest explanation is misreading the prompt log, which renders a multi-turn request as
      "[Conversation with N messages]" rather than showing the body. A wrong reason in a
      verification record is worse than an unexplained gap: the next reader stops looking.
- [x] 8.4 Open a PR (never push to `main`) — [#1852](https://github.com/Kulitorum/Decenza/pull/1852)
      — then run the automated `/pr-review-toolkit:review-pr` before merging. Run 1 found four
      critical defects; all fixes are recorded in section 5b and section 7 above. Runs 2 and 3
      are recorded in sections 5d and 5e, runs 4 and 5 in 5f and 5g, and run 6 — the one that
      caught the previous round destroying user data — in 5i. A seventh run over the backout
      itself is the last gate before archiving.
- [x] 8.5 No wiki manual entry — this changes how the advisor selects its own context and
      surfaces nothing the user must be told exists. Recorded here so the omission is a decision
      rather than an oversight.
