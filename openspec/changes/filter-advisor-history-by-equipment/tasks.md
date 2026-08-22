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

## 5c. Conversation-storage defects found by driving the running app

Both pre-existing, both evaluated as part of this change because it multiplies the
number of conversation keys a user can hold (one per bean x profile x equipment
package), which scales up a per-key leak and a per-key dead end alike.

- [x] 5c.1 **An orphaned thread leaks.** `clearCurrentConversation()` removes the index entry
      but leaves the live conversation ON that key, so the next message writes a thread that no
      index entry names: absent from the conversation list and from
      `loadMostRecentConversation()`, and never evicted, because
      `evictOldestConversation()` only walks the index. `touchConversationEntry` is replaced by
      `noteConversationUse`, which creates the entry when it is missing; both call sites in
      `switchConversation` now go through it and the `exists` bookkeeping is gone. Observed
      live: after Clear, `ai/conversations/index` stayed `[]` while the thread sat on disk.
- [x] 5c.2 **An MCP-written thread cannot be continued in the app.**
      `appendAssistantTurnForKey` deliberately skipped the system prompt, on the reasoning that
      `recentAdvice` reads only `messages` — true of `recentAdvice`, and stale ever since
      `switchConversation` started loading those turns into the LIVE conversation. `followUp()`
      refuses a thread with an empty system prompt, so the user's only way forward was Clear,
      which deletes the turns MCP just wrote. The prompt the turn was actually produced under
      is now persisted, write-if-absent so an in-app thread keeps its own multi-shot prompt.

## 5d. Second review round — defects in the round-one fixes

`/pr-review-toolkit:review-pr` over `40559943..HEAD`. Round one found four criticals in the
original commit; this round found four more, all in the code written to fix round one. Recorded
in full because the pattern — a fix introducing a defect of the same shape it removes — is the
thing to watch for, not the individual bugs.

- [x] 5d.1 **The wipe was undone three lines later.** `migrateFromLegacyConversation()` runs right
      after `clearAllConversationsOnce`, and its guard ("legacy data exists AND the index is
      empty") is made TRUE by the wipe. It re-created a `_legacy` entry pointing at the pre-keyed
      global thread — the one that by construction spans every bean, profile and package the user
      ever had — which `loadMostRecentConversation()` then restored. The migration handed back the
      most cross-contaminated thread in the store, silently, logging "Migrating legacy
      conversation to keyed storage" as if it had succeeded. The wipe now removes the singular
      `ai/conversation/*` keys too, which is what makes the wipe stick at all (the marker
      suppresses the wipe on later launches, but not the migration that undoes it). Note the
      re-seed fires ONCE per wipe, not once per launch — the migration writes
      `ai/conversations/index` itself, so its own guard shuts it off afterwards. The earlier
      "recurring on every later launch" wording here was wrong; once was already enough.
- [x] 5d.2 **Fail-open and fail-closed on the same signal, two lines apart.**
      `queryGrinderContext` appended its predicate only `if (bucket.has_value())`, so an
      unresolved bucket published `settingsObserved` and "range explored" pooled across every
      package — the original confound, through the failure path, unlogged — while
      `buildGrinderCalibrationBlock` beside it failed closed on the identical value. Fixed by
      removing the unresolved case rather than handling it: the bucket now comes from the row
      each consumer already reads (`shots.equipment_id` added to the grinder-context SELECT;
      `cur.equipmentId` in the calibration block; `currentShot.equipmentId` in bestRecentShot;
      `dbResult.shotData.equipmentId` on the MCP path). Six `equipmentBucketForShot` call sites
      became two — the only two that hold just a shot id — and the scoping is now unconditional,
      which is stronger than failing closed because there is no path that skips it. The
      "Resolved ONCE" comment that claimed this and did not deliver it is gone with the mechanism.
- [x] 5d.3 **An MCP `systemPromptOverride` became the user's durable in-app prompt.** Persisting
      the prompt made an arbitrary caller-supplied argument the system prompt for every later
      in-app turn on that thread — invisible in the UI, removable only by deleting the
      conversation. And absent an override the persisted prompt was the SINGLE-shot one, so a
      thread the MCP path created never acquired `## Multi-Shot Context` for its whole life
      (`hasHistory` is true, so the overlay takes `followUp()` forever). Now persists
      `multiShotSystemPrompt` and persists nothing when an override was supplied. The
      `systemPrompt` parameter lost its default; all 13 test call sites pass one, and writing
      without one warns.
- [x] 5d.4 **`noteConversationUse` manufactured ghost entries.** Creating the index entry when a
      key was SELECTED meant Clear-then-reopen (no send) indexed a key with no stored thread:
      `ai_conversations list` reports `messageCount: 0` while `get` answers "Conversation not
      found", and the placeholder occupies one of five LRU slots, evicting a real transcript. The
      entry is now created when a thread is WRITTEN (`indexStoredConversation`, gated on the key
      having stored messages), which fixes the original Clear-then-send leak without inventing
      the ghost. It also indexes an MCP-written thread the first time the app switches to it.
      Round three found this fix only half-applied and the signal wiring wrong — see 5e.1 and
      5e.2.
- [x] 5d.5 **`ShotDetailPage.onShotBadgesUpdated` reverted.** The change swapped a synchronous
      four-field patch for a full async row re-read (debug-log blob, every curve array, profile
      JSON) to arrive at data `onShotReady` had just delivered, and re-ran `returnToBounds()`
      mid-read. It was justified by a false claim — that `Object.assign` on a Q_GADGET copies
      nothing — so it was a pure regression fixing a non-bug. Reverted to match 40559943 exactly.
- [x] 5d.6 **The `Object.assign` claim corrected at its source.**
      `QQmlValueTypeWrapperOwnPropertyKeyIterator::next` walks `mo->propertyCount()` and returns
      every Q_PROPERTY as an own enumerable key; only Q_INVOKABLE methods are skipped
      ("We don't return methods, ie. they are not visible when iterating",
      qtdeclarative/src/qml/qml/qqmlvaluetypewrapper.cpp:449). `clonePersistedShot`'s docstring
      asserted the opposite, unsourced, and is what licensed 5d.5 — so the docstring now says the
      mechanism is NOT established and that the whitelist is kept because it works. The
      round-one whitelist fix stands on its own: a whitelist genuinely drops unlisted fields,
      which is a different mechanism from the one that was written down.
- [x] 5d.7 Comment corrections: the Qt mixed-bind story (it is the shared `d->values` index
      space at `qsqlresult.cpp:145,690`, not the `binds` mode flag, which is dead code on the
      SQLite driver); `nullopt` means "could not be read" for EITHER reason and callers must not
      try to tell them apart; the enrichment rule's grinder-less carve-out and merge branch; the
      `-1` sentinel's actual hazard (the key hash, not the sparse serialisation) plus a clamp in
      `conversationKey` so it is handled once for every caller; `allNumeric` added to the
      package-scoped list; the MCP calibration `reason` string no longer asserts a cause it
      cannot know (it was true for one of its four triggers); three duplicated rationales
      collapsed to one site each.
- [x] 5d.8 design.md: seven selection points became eight (the `recentAdvice` follow-up lookup);
      "without a migration step" and "Deleting saved conversations" corrected against D4; the
      deliberate fail-closed/degrade-unscoped split recorded. tasks.md 8.3's stated reason was
      wrong and is replaced with an honest "reason not established".

## 5e. Third review round — defects in the round-two fixes

Five agents over `0924d9cd..HEAD`. Three of them independently reached the same top three, which
is why those are listed first. Round two's fixes contained new defects of round one's shapes, as
round two's did of round one's.

- [x] 5e.1 **The index entry was stamped with the PREVIOUS conversation's identity.**
      `switchConversation` set `m_live*` three lines AFTER `loadFromStorage()`, and
      `loadFromStorage()` emits `savedConversationChanged` — so `indexStoredConversation()` ran
      with the new key and the old bean/type/profile/package. `noteConversationUse` only touches
      an entry that already exists, so the correct call two lines later could not repair it: the
      wrong label was permanent. Reachable exactly where 5d.4 advertised a benefit — an
      MCP-written thread, on the first switch to it — and on the first switch of a session it
      stamped empty strings. Fixed by hoisting the four assignments above the load.
- [x] 5e.2 **Every app launch silently re-dated the newest thread.**
      `loadMostRecentConversation()` calls `loadFromStorage()`, whose emit reached
      `indexStoredConversation` → `touchConversationEntry` → `timestamp = now` + move-to-front,
      with no user action. `lastUpdated` is the only recency signal the conversation list and
      `ai_conversations list` have. Root cause is that `savedConversationChanged` is the NOTIFY
      for `hasSavedConversation` and therefore also fires on load and on clear; indexing is now
      wired to a new `AIConversation::conversationPersisted()` emitted only from
      `saveToStorage()`. `loadMostRecentConversation()` also populates `m_live*` now, so a
      Clear-then-send on a restored thread cannot re-index it blank.
- [x] 5e.3 **The ghost fix was only half applied.** `switchConversation`'s full path still called
      `noteConversationUse` unconditionally, so opening the advisor on any new (bean, profile,
      equipment) triple and sending nothing created the ghost 5d.4 says it prevents — and
      equipment scoping multiplies how many distinct triples a user has. It now calls
      `indexStoredConversation()`, which is gated on the key having a stored thread, so the
      MCP-thread pickup is kept and the ghost is not. The existing test could not see this: its
      own first `switchConversation` manufactured the ghost it was asserting against.
- [x] 5e.4 **An MCP `systemPromptOverride` produced a thread the app refuses to continue.**
      5d.3 stopped persisting the override — by persisting nothing, which left turns on disk with
      no system prompt. `followUp()` then refuses the thread ("Please start a new conversation
      first") and Clear, which deletes those turns, is the only way out. The `qWarning` added in
      the same round fired on this intended path, which is how a warning gets trained away.
      The override governs the CALL; it says nothing about what the thread should be continuable
      under, so `promptToPersist` is now always the multi-shot prompt. The warning becomes
      unreachable in production, which is what makes it worth keeping.
- [x] 5e.5 **`loadQualifiedShots` was the surviving degrade-to-unscoped site.** 5d.2 removed the
      unresolved case from four places and not from the advisor's own history query, which still
      resolved the bucket through `equipmentBucketForShot` and dropped the predicate on
      `nullopt` — leaving the history pooled across every package on the failure path, the exact
      shape 5d.2 condemned. The bucket now comes from the step-1 row read
      (`SELECT timestamp, COALESCE(equipment_id, 0)`), so the predicate is unconditional and
      there is no policy to pick.
- [x] 5e.6 **An empty equipment label meant three different things, and two of them took the
      "deliberate silence" branch.** The no-history block discriminated on the label string. An
      empty label means: the shot has no package (silence is right), OR the row query failed, OR
      the package's component rows are all gone — and in the last two the history WAS scoped, on
      `loadQualifiedShots`'s own connection, so shots were excluded and nothing said so. A model
      with no anchor invents one; that is the failure this change came from.
      `emitRecentShotContext` now takes `equipmentBucketKnown` / `equipmentBucket` and branches
      on the fact: unreadable says so, bucket > 0 states the absence (naming the set generically
      when the label is empty), bucket 0 stays silent.
- [x] 5e.7 **The clamp reached the hash but not the data.** `conversationKey` folded a negative
      `equipmentId` to 0 while `switchConversation` stored the raw value, so a `-1` produced an
      index entry claiming `-1` under a key computed on 0 — the entry/key disagreement the backup
      carry-through exists to prevent, arriving from inside, and self-healing only across a
      save/reload because `toJson` drops `<= 0`. Normalized once, at the `switchConversation`
      boundary. The `= 0` defaults on `conversationKey` and `switchConversation` are also gone:
      no caller used them, and omitting the argument silently files a packaged shot under the
      unpackaged thread.
- [x] 5e.8 **`ConversationEntry::equipmentId` had no reader.** Its declaration promised a
      conversation list that could tell two threads apart; no list read it, so a value that
      disagreed with its key was undetectable. `ai_conversations list` now reports
      `equipmentPackageId`.
- [x] 5e.9 **`clearAllConversationsOnce` stamped its marker without checking the wipe landed.**
      A whole-file `QSettings` failure self-heals, but the asymmetric case (marker persisted,
      removals not) leaves every pre-upgrade cross-equipment thread in place with the migration
      permanently suppressed. Now checks `status()` before stamping, following the four existing
      call sites in the settings classes. The completion line moved from `qDebug` to `qInfo`:
      it records an irreversible deletion of every advisor thread the user owns, and the
      connections views default to INFO.
- [x] 5e.10 **`loadRecentShotsByKbIdStatic` never projected `equipment_id`**, so every
      `ShotProjection` it built carried a false `equipmentId` of 0 — "unpackaged" — for packaged
      shots. Harmless today because nothing reads it there; a landmine for the first caller that
      does. Now projected.
- [x] 5e.11 `mcptools_dialing` used `shotData.equipmentId` with no `isValid()` check on the
      record it had just loaded. A failed load yields 0, which is a REAL bucket, so the grinder
      context would have been scoped to unpackaged shots rather than skipped — masked only by
      the empty `grinderModel` from the same failed record short-circuiting first. The whole
      block is now guarded on the load.
- [x] 5e.12 Comment corrections, all of them claims that were believed and could not be:
      the wipe's "recurs on every later launch" rationale (false — the migration writes the index
      itself); `indexStoredConversation`'s emitter list (named `resetInMemory`, which does not
      emit, and omitted `loadFromStorage`, which does and was the bug); "no second lookup to
      disagree with" in `mcptools_ai.cpp` (two of the four builders still resolve their own);
      "they cannot disagree" on the grinder-context query (true within that query, false against
      the history load on its other connection); the puck-prep enrichment note (an enrichment
      that matches an existing package merges into it, so it CAN move the package); the
      "logged as a warning either way" claim on `appendAssistantTurnForKey` (only when the key
      has no prompt of its own); a hard line-distance in `dialing_blocks.cpp`; and
      `qqmlvaluetypewrapper.cpp:448` → `:449`, verified in `~/Qt/6.11.2/Src`.
- [x] 5e.13 Test fixes: `clearThenWrite_...`'s persistence assertion was vacuous (an absent
      settings key is an empty `QByteArray`, which contains no `"[]"` either, so it passed with
      `saveConversationIndex()` deleted) — it now asserts the key is present; the ghost test
      gained the fresh-key case it could not see; `ShotRowFixtures::hasGear` now trims, matching
      the production helpers, so a whitespace-only fixture field cannot build a package shape
      production never produces. New slots: `switchToAnMcpWrittenThread_indexesItUnderItsOwnShot`,
      `launch_doesNotRedateTheRestoredThread`,
      `switchConversation_normalizesTheNoPackageSentinelInTheEntryToo`,
      `emitRecentShotContext_unreadableEquipmentSaysSoRatherThanGoingSilent`,
      `emitRecentShotContext_packagedShotWithNoLabelStillStatesTheAbsence`,
      `emitRecentShotContext_emptyHistoryOnNoPackageStaysSilent`,
      `importedConversationKeepsItsEquipmentPackage`,
      `importedPreEquipmentConversationReadsAsUnpackaged`.

### 5e follow-through — the two items first recorded as deferred

Both were then done, because both deferrals were argued from effort rather than
from risk, and one of them was hiding a regression this change introduced.

- [x] 5e.14 **The last two `std::optional<qint64> equipmentBucket` parameters are gone**, along
      with the helper behind them. `queryGrinderContext`, `buildGrinderContextBlock` and
      `buildDialInSessionsBlock` now take a required `qint64`; 0 (unpackaged) is a real, matching
      bucket, so there is no "don't scope" option and no degradation policy to pick. With that,
      `ShotHistoryStorage::equipmentBucketForShot` had no production caller and was deleted — the
      tri-state (`nullopt` conflating "no such shot" with "query failed") stops existing rather
      than being modelled better. Every caller now takes the bucket off a ShotRecord/ShotProjection
      it has already loaded. The 29 test call sites pass a real package through two new fixture
      helpers, `packageForShot` and `onlyEquipmentPackage` — the latter deliberately loud when a
      fixture holds more than one package, so a test with two cannot silently assert scoping for
      the wrong reason. `loadRecentShotsByKbIdStatic` keeps its optional, and that one is
      legitimate: two REAL callers want different things (the advisor scopes; the Q_INVOKABLE
      generic recents reader never had a same-gear contract), so absent means "this caller does
      not scope", never "the bucket could not be read".
      `equipmentId_projectionAndBucketAgreeForTheSameShot` now compares the projection against the
      raw column rather than against a second helper — which matters more, not less, since
      `ShotProjection::equipmentId` is the sole source left.
- [x] 5e.15 **A conversation restored from another device is rekeyed onto the mapped package.**
      This was a regression this change introduced and nothing else: putting the package id in the
      conversation key made every restored thread unaddressable, because the key is an SHA1 of
      bean|type|profile|equipmentId computed on the SOURCE device while the equipment import
      renumbers packages. `ImportResult` now carries `packageIdMap` alongside `shotIdMap` (it was
      a block-local), with a matching `packageMapOrNull()`, and `importConversationsStatic` remaps
      the entry's `equipmentId` AND recomputes the storage key from the mapped id before writing
      anything. Duplicate detection runs on the key the entry will be WRITTEN under, not the one
      the archive carried — otherwise a remapped conversation could land on top of a live local
      thread. An unpackaged conversation (equipmentId 0) is device-independent and is deliberately
      NOT rekeyed. A packaged conversation with no map, or whose package is absent from it, is
      still imported and readable but keeps its source key and is counted in the new
      `ImportTally::conversationsUnkeyed` — dropping it would be worse, and silently restoring it
      as resumable would be a lie.
      Four new slots, including one that proves the point end to end: after the import,
      `switchConversation` on the DESTINATION package resumes the restored thread. Verified by
      disabling the remap and watching `importedConversationIsRekeyedOntoTheMappedPackage` and
      `importedConversationDoesNotOverwriteALiveThreadOnTheMappedKey` go red.

## 5f. Fourth review round — defects in the third round's fixes

Five agents over `71e74205..HEAD`. The pattern held for a third time: most of what was found was
inside the previous round's fixes, and the worst item was a "fix" that was more destructive than
the bug it replaced.

- [x] 5f.1 **The wipe's new status gate was a destructive infinite loop.** 5e.9 added
      `if (settings.status() != QSettings::NoError) return;` before stamping the marker.
      `QSettings::status()` is LATCHED: `setStatus` assigns only when the incoming status is
      `NoError` or the stored one is (`qtbase/src/corelib/io/qsettings.cpp:312-316`) and none of
      its twelve call sites in that file passes `NoError`, so nothing ever clears it. The
      `AppSettings` constructor syncs before this function runs, so one unparseable line latches
      `FormatError` up front (`:1431`, `:1437`). On such a device the removals persist, the marker
      never does, and every advisor conversation created since the last launch is deleted again at
      every launch — while the warning says the opposite ("did not persist… it will retry").
      Now compares clean-before to dirty-after and stamps optimistically when the store was already
      dirty. `Settings::commitFlowCalMigrationFlag` is the same guard written for the same reason
      after v2/v3/v4 re-ran their resets and wiped calibration data; 5e.9 cited it as precedent
      while doing the opposite of what it does.
- [x] 5f.2 **The import's duplicate check could not see an MCP-written thread, and blind-wrote over
      it.** `existingKeys` is built only from `ai/conversations/index`, but
      `appendAssistantTurnForKey` writes the thread and never touches the index — which is why
      5e.3 had to make `switchConversation` index such threads on first open, and which was
      confirmed on the running app. 5e.15's recompute turned this from a freak collision into the
      normal case, because `storageKey` is now aimed at exactly the key the destination device
      computes for that shot. The skip now tests disk as well as the index.
- [x] 5f.3 **The rekey assumed its own precondition.** Moving a thread is only correct if the
      archived key really is the hash of the archived fields, and the exporters copy the key and
      the bean/profile fields out of the index entry independently. An entry written inconsistently
      — which the identity defects fixed in 5e.1/5e.2 produced, and which an older archive still
      carries — would be moved to a THIRD key naming nothing on either device, and counted as
      imported. Now checked; a mismatch is treated as unkeyed.
- [x] 5f.4 **A valid-but-EMPTY `messages` array imported as a ghost and defeated the anti-ghost
      gate.** The malformed guard rejected non-arrays; `[]` passed, and the two bytes it writes are
      not an empty `QByteArray`, so `indexStoredConversation`'s on-disk gate read it as a real
      thread and re-indexed it. Empty arrays are now malformed on import, and the gate parses the
      JSON instead of testing raw bytes.
- [x] 5f.5 **A failed history read was reported to the model as "you have no prior shots with this
      equipment set."** `loadQualifiedShots` returned an empty list for both "nothing qualified"
      and "the query errored", and 5e.6's branch turned the first into an explicit statement of
      absence — a fabricated fact produced by the branch whose whole purpose is to stop the model
      inventing one, and strictly worse than the silence it replaced. It now reports readability
      separately and says so.
- [x] 5f.6 **An unkeyed import wrote the SOURCE device's package id** into a field documented
      device-local and newly reported over MCP as `equipmentPackageId`, where it names whichever
      unrelated local package holds that integer. Reset to 0 instead.
      `conversationsUnkeyed` was also incremented before the duplicate skip (over-counting threads
      that were never restored) and had no reader — the same write-only shape as
      `ConversationEntry::equipmentId` two rounds earlier. Moved below the skip and reported by all
      four callers alongside the turn counts, because "12 imported" is misleading when none of the
      twelve will be found again from a shot.
- [x] 5f.7 Tests: `importedPreEquipmentConversationReadsAsUnpackaged` **could not fail** — it
      asserted `equipmentId == 0`, which is the field's default initialiser, so deleting the whole
      carry-through left it green. That is the fifth such assertion in this change. Deleted, along
      with `importedConversationKeepsItsEquipmentPackage`, which asserted strictly less than its
      sibling. Four new slots in their place, each verified red with its fix disabled:
      `importedConversationWithAKeyThatDoesNotMatchItsFieldsIsNotRekeyed`,
      `importedConversationDoesNotClobberAnUnindexedMcpThread`,
      `importedConversationWithNoTurnsIsRejectedAsMalformed`,
      `emitRecentShotContext_unreadableHistoryDoesNotClaimThereAreNoShots`.
- [x] 5f.8 Comment corrections, again all claims that were believed and were not true: the
      `ConversationEntry::equipmentId` "DEVICE-LOCAL… not remapped, nor could it be" note, written
      one commit before the remap landed; "every caller now takes the bucket off a loaded record"
      (two callers read the column inline); the `conversationPersisted` rationale's clause (b),
      which 9056f284 made unreachable; "same check the settings classes use", wrong for two of the
      four files cited and pointing at the opposite of what `settings.cpp` does; "nothing was
      excluded" for bucket 0, false for a user with both packaged and unpackaged shots; the
      `loadRecentShotsByKbIdStatic` contract still citing the deleted `equipmentBucketForShot`; a
      duplicated `@param` block; `conversationKey`'s clamp justified by a false claim that the
      function is `Q_INVOKABLE`; and `switchToAnMcpWrittenThread`'s comment describing a defect the
      test no longer pins.

### 5f deferred, with reasons

- [ ] 5f.9 **`loadShotRecordStatic` collapses "no such row" and "the SELECT failed"** into one
      branch that warns "Shot not found", so `mcptools_dialing` answers `"Shot not found: N"` for a
      transient database error — telling an MCP user a shot they can see in Shot History does not
      exist. Pre-existing and outside the advisor scoping work; the fix is to return
      `std::optional` from that loader and thread a distinct error through its callers.
- [ ] 5f.10 **Two source packages that merge into one destination collapse two conversations onto
      one key**; the second is counted as `skippedExisting` and logged "already present", which is
      not what happened. Needs a separate counter for keys claimed within the import itself.
- [ ] 5f.11 **The conversation-import log lines are all `qDebug`**, so a user asking where their
      threads went sees nothing in the connections views (which default to INFO). 5e.9 raised the
      wipe's line for exactly this reason; the import path should follow.
- [ ] 5f.12 **`switchConversation`'s early-return branch does not load or index an MCP-written
      thread.** Reopening the overlay on the SAME shot after an MCP write leaves the in-memory
      conversation empty, so QML takes the `ask()` branch and the model is sent a thread with none
      of the MCP history. Turns are not lost — `saveToStorage` reconciles — but the context is.

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
      are recorded in sections 5d and 5e.
- [x] 8.5 No wiki manual entry — this changes how the advisor selects its own context and
      surfaces nothing the user must be told exists. Recorded here so the omission is a decision
      rather than an oversight.
