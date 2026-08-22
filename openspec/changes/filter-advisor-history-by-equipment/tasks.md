## 1. Shared equipment match

- [x] 1.1 Add one helper that resolves a shot id to its equipment bucket, so all seven selection
      points ask the question the same way rather than each writing its own SQL fragment. Landed
      as `ShotHistoryStorage::equipmentBucketForShot` + `equipmentBucketSql`, in the HISTORY layer
      rather than in `DialingBlocks`: `shots.equipment_id` is storage's own concept, and putting
      it in the AI layer would have made storage depend on AI. It returns
      `std::optional<qint64>` — see the note under 1.3.
- [x] 1.2 Add one helper that renders an equipment package as an English phrase (grinder brand /
      model / burrs, basket brand / model, puck-prep techniques), omitting absent components.
      Used by the in-app Setup header and the no-history block; see design D3. Landed as
      `describeEquipmentSet` at file scope in `aimanager.cpp`.

- [x] 1.3 **Found during implementation, not planned.** `equipmentBucketForShot` must return
      `std::optional`, not a plain `qint64`. Callers pass -1 as a "no resolved shot" sentinel, and
      collapsing that to bucket 0 scopes the query to unpackaged shots — silently returning an
      empty history to every user who HAS a package. Returning 0 for a missing row looks like the
      safe default and is the exact opposite of one. Caught by six pre-existing
      `tst_dialing_blocks` failures on the first run, not by review.

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

## 5b. Review fixes (found by `/pr-review-toolkit:review-pr`, not planned)

- [x] 5b.1 `PostShotReviewPage.clonePersistedShot` is a field whitelist and `equipmentId` was not
      in it, so the advisor opened from a clone with the package missing — the whole feature was
      inert on the main path. Added `equipmentId`, `equipmentName`, `basketBrand`, `basketModel`,
      `puckPrep`. `ConversationOverlay` now collapses BOTH unset sentinels (C++ 0, the edit
      field's -1) to bucket 0; a leaked -1 would have keyed a third orphan thread.
- [x] 5b.2 `buildGrinderCalibrationBlock` now FAILS CLOSED on an unresolved bucket. It publishes
      a grind NUMBER, and running it with no equipment predicate pooled every package — the exact
      confound the change exists to remove, invisible at the point of use.
- [x] 5b.3 `buildRecentAdviceBlock`'s follow-up-shot lookup was the eighth selection point and
      was missed. Scoped to the advice shot's own package: without it, a user who took the advice
      on one basket and then pulled on the other had the OTHER basket's shot scored as their
      response, and the model was told its advice was ignored on a grind move never made.
- [x] 5b.4 `EquipmentStorage::updateGrinderIdentityStatic` does not exist. Three citations
      (`shothistorystorage.h`, `aimanager.cpp`, `design.md`) now name `supersedeOrEditStatic`, and
      "forks whenever" is qualified — an unused package, or one whose every differing component
      was empty, is edited in place.
- [x] 5b.5 The equipment bucket is now resolved ONCE per request and threaded through
      (`loadQualifiedShots` out-parameter; a single local on the MCP path) rather than re-read on
      a second connection. Two reads could disagree and nothing would say so.
- [x] 5b.6 `conversationKey(..., bucket.value_or(0))` replaced by an explicit
      `has_value()` gate on both surfaces: keying on 0 when the bucket is unknown reads the
      UNPACKAGED user's thread.
- [x] 5b.7 Comment/doc corrections: the stale "no shots on this exact grinder + burrs" message
      the MCP returns to the model; the `grinderBurrs` parameter documented as diagnostic-only;
      the Qt mixed-bind claim cited to source and corrected ("Parameter count mismatch", not
      "silently binds nothing"); the unreachable "-1 sentinel" claim; the inverted ordering
      comment in `ConversationOverlay`; an orphaned doc comment and an over-claim in
      `shothistorystorage.h`; two duplicated rationale paragraphs deleted; "the Aug 2026 report"
      replaced with a plain statement that there is no issue number.

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
- [x] 7.13 Test-hygiene fixes from review: `ShotRow` gained `puckPrep` (the third identity
      component, which forks a package on its own); the session-context assertion checks basket
      BRAND as well as model; the Setup-header assertion is scoped to the Setup LINE rather than
      the whole payload (the basket also appears in per-shot blocks, so the old assertion passed
      with the header empty); `conversationKey_preUpgradeThreadIsNotResumed` gained a positive
      control, since with 5.4 in place two separate mechanisms produce its empty thread.

## 8. Verify

- [x] 8.1 Full suite via the Qt Creator MCP. First run **113 passed, 0 failed**; re-run after the
      review fixes above — **113 passed, 0 failed, 0 warnings**, with all five new slots
      confirmed executed in `LastTest.log` (a passing binary count says nothing about whether a
      new slot compiled in). The six pre-existing
      `tst_dialing_blocks` failures seen on the first run were the `std::optional` defect in 1.3
      and are fixed; one further failure was a bad assertion in a new test (a single-profile
      fixture forms no pairs, so `coffeeAnchor` is never emitted) and was rewritten to assert the
      exclusion directly.
- [x] 8.2 `openspec validate filter-advisor-history-by-equipment --strict` — valid.
- [ ] 8.3 Open the advisor on a shot from each of two packages and confirm by eye: history is
      package-scoped, the Setup header names the right basket, and the conversation starts fresh
      rather than resuming a pre-upgrade thread.
- [x] 8.4 Open a PR (never push to `main`) — [#1852](https://github.com/Kulitorum/Decenza/pull/1852)
      — then run the automated `/pr-review-toolkit:review-pr` before merging. Run 1 found four
      critical defects; all fixes are recorded in section 5b and section 7 above.
- [x] 8.5 No wiki manual entry — this changes how the advisor selects its own context and
      surfaces nothing the user must be told exists. Recorded here so the omission is a decision
      rather than an oversight.
