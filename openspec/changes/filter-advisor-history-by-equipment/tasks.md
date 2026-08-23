# Tasks

Requirements are in `specs/`. This file tracks delivery against them.

PR #1857 delivers the selection scoping and the conversation key. The payload work (sections 3-6)
is not delivered.

## 1. Scope the advice reads (PR #1857 — done)

- [x] 1.1 `AdviceScope` value type (`src/history/shotscope.h`), predicate and bucket together.
- [x] 1.2 `loadRecentShotsByKbIdStatic` takes a scope; `buildDialInSessionsBlock` inherits it.
- [x] 1.3 `buildBestRecentShotBlock`'s own query scoped.
- [x] 1.4 `queryGrinderContext` observed settings and RPM axes scoped; redundant grinder-model
      subquery and its `:model` bind removed.
- [x] 1.5 `buildGrinderCalibrationBlock` scoped to the package; inline grinder-identity subquery
      removed, `grinderBurrs` dropped from the signature and four call sites.
- [x] 1.7 Call sites resolve the scope from the shot under review, not live machine state.
- [x] 1.8 `stepSize` / `rpmStepSize` left grinder-wide, pinned by a test that fails if narrowed.
- [x] 1.9 Invariant test over all five selections; calibration scoping test; both verified by
      breaking the code and watching them go red.
- [x] 1.10 Dead `requestRecentShotsByKbId` + `recentShotsByKbIdReady` deleted (no caller in any
      surface, described as live in three docs).

### Section 1 follow-ups

- [x] 1.11 The three sites are now ONE. Sections 9.1/9.2 already routed the in-app advisor and
      `ai_advisor_invoke` through `buildAdvisorContextBlocks`, which derives the scope itself;
      `dialing_get_context` was still hand-writing its own three builder calls for a single
      difference (no grinder calibration, #1164), which is why it held a second scope construction
      and was the only surface with no `noDialInHistory` block. That difference is now a
      `GrinderCalibration::Omit` argument, so one line reads `equipmentId` off a live shot.
      `adviceScope_theAssemblerDerivesItFromTheShotUnderReview` covers it, asserting both halves —
      no foreign package's settings, and at least one of the shot's own, since a scope of 0 matches
      nothing and would otherwise pass an emptiness check. Verified by setting that scope to 0:
      the new test goes red and NO other test notices, which is the gap this item named.
- [x] 1.12 `getRecentShotsByKbId()` does not exist — it was removed as callerless, and five doc
      citations plus one source comment outlived it. The design-time "ShotHistoryStorage
      prerequisite" section in `MCP_SERVER.md` is deleted (the tool it gated has shipped for
      months); the rest now name `loadRecentShotsByKbIdStatic()`.

## 2. Conversation threads keyed by equipment (done)

This is the requirement that repairs the reported case. A saved thread replays its stored turns
on every request, so scoping future context alone leaves the contaminated transcript in place.

- [x] 2.1 `conversationKey` takes the SHOT and is the only place a key is derived. The MCP tools
      and the in-app overlay share one conversation, so the previous shape — each surface
      unpacking a shot into four arguments — was a way for them to drift into separate threads.
      There is nothing left to keep in agreement.
- [x] 2.2 `switchConversation` takes the shot too (QVariant, same reason as `isMistakeShot`), so
      QML hands over `shotData` rather than fields it could pick wrongly.
- [x] 2.3 Pre-upgrade conversations thrown away via the existing `clearAllConversationsOnce`.
      Changing the key already orphans them; this stops dead threads holding index slots.
- [x] 2.6 The index names the package. Keying on equipment means one bean and profile can
      hold several threads; `ConversationEntry` snapshots the package label and id so the app,
      the ShotServer page and `ai_conversations` can tell them apart. `ConversationEntry::label()`
      is the one producer for all four surfaces, replacing four copies with two separators.
- [x] 2.4 `aiconversation.cpp` change detection reporting an equipment-package swap between
      consecutive shots. **Dropped, and it could not be built:** the diff compares a shot with the
      previous shot IN THE SAME CONVERSATION, and the key holds the package id, so both shots
      share it by construction. The arm would compare a value with itself and never emit. Written
      when the key was bean+profile only, where a swap genuinely could land two packages in one
      thread; 2.1 removed the condition rather than narrating it. Recorded at the call site so the
      next reader does not re-add it — the existing `grinder` arm is not that value, it carries
      the grind SETTING, which does move within a thread.
- [x] 2.5 `conversationKey_separatesEquipmentPackages` covers the three spec scenarios: separate
      thread per basket, resuming a package's own thread, and a packaged shot never landing on a
      pre-change (unpackaged) key.

## 3. Name the equipment in the payload (mostly delivered)

Section 1 scopes the data correctly and never tells the model what changed, so numbers move
between conversations with no stated cause.

- [x] 3.1 `ShotProjection::equipmentLabel()` (grinder / basket) for display, used by the
      conversation index and the auto-favourite cards.
- [x] 3.2 `DialingHelpers::ShotIdentity` carries the whole package. Its fields are now one table
      (`ShotIdentity::fields()`) naming both ends of each mapping, walked by the hoist, the
      session-context emit, the per-shot override emit and the fill from a shot — so tracking a
      new component is one row, not the same name written in four places. Verified live: every
      `dialInSessions[].context` now carries basketBrand / basketModel / puckPrep beside the
      grinder.
- [x] 3.3 The history read joins the whole package. `EquipmentJoin` (`src/history/equipmentjoin.h`)
      holds the four LEFT JOINs, the column list and the reader in one place; the advisor's read
      joined the grinder alone, which is why a basket switch reached the model as one setup.
- [x] 3.4 Parity test that the two surfaces emit the same identity fields —
      `tst_dialing_blocks.cpp:898`, with `:1022` asserting it must fail if either surface drifts.

**Superseded.** The remaining unchecked entries here read "the in-app `### Setup:` header gains
basket and puck prep", "`ShotIdentity` gains basket + puck-prep fields", "MCP session `context`
gains them" — the last two landed as 3.2/3.3 above and the first is the wrong fix. The header
is a second renderer of data `ShotIdentity::fields()` already owns; adding two names to its
hand-written list is the ninth copy, and the next component needs a tenth. Section 9 removes the
renderer instead.

## 4. Explicit no-history block

- [x] 4.1 When no prior shots match, emit a block naming the equipment set instead of omitting
      the history section — an absent block is indistinguishable from "no history at all", and
      an unanchored model invents an anchor.
- [x] 4.2 Distinguish "the query failed" from "the query ran and matched nothing"; do not report
      one as the other.
- [x] 4.3 Test that a package with no history produces the block rather than silence.

## 5. System prompt rules

- [x] 5.1 Grind settings are comparable only within one equipment set.
- [x] 5.2 Do not cite a shot, score, or taste note absent from the context. (The reported reply
      cited a "70/100 shot" that appeared nowhere in its context.)

## 6. Import / backup

- [x] 6.1 Package-id remap through `shotserver_backup.cpp`, `datamigrationclient.cpp` and
      `databasebackupmanager.cpp`, so a restored conversation keys to the destination package.
      `ImportResult` now publishes `equipmentIdMap` beside `shotIdMap` — the map already existed
      inside `importDatabaseStatic`, it was simply never handed out — and
      `importConversationsStatic` RE-KEYS each conversation through it rather than writing the
      archive's key through verbatim. Two shapes are refused instead of imported wrong: a key the
      conversation's own fields do not derive (an archive written before the package joined the
      key — nothing on this device would ever open it), and a package the map does not contain
      (bucket 0 would file a thread about one basket under "no basket", which is the mixing the
      key exists to stop). Both are counted and named in the importer's log line.
- [x] 6.2 The key derivation moved to `ConversationKey::derive` (`src/ai/conversationkey.h`), since
      the import path has to re-derive it and a second copy of the hash would orphan every
      restored thread silently — the index entry present, the shot never reaching it.
- [x] 6.3 The conversation record had TWO hand-written serializers (`databasebackupmanager.cpp`
      and `shotserver_backup.cpp`) against one deserializer, and neither carried the equipment
      fields section 2.6 added — so a restored conversation lost its package label and id. One
      producer now: `AIConversation::exportConversationsStatic`.
- [x] 6.4 Tests: re-key onto the destination package, refusal of an unresolvable package, refusal
      of a pre-equipment archive entry, an unpackaged thread keeping its key, and an
      export/import round trip. All five verified red by breaking the re-key, the refusal and the
      legacy check together.

## 7. Documentation

- [x] 7.1 `docs/CLAUDE_MD/AI_ADVISOR.md` — a "Scoped to the Equipment Package" section carrying
      the three things a reader needs before touching this area: the conversation key holds the
      package, no match emits `noDialInHistory`, and an import re-keys through the package map.
      Two stale claims fixed while there: the deleted prose readers described as still serving old
      conversations, and `src/mcp/mcptools_dialing_blocks.h`, a path that does not exist.
- [x] 7.2 `docs/CLAUDE_MD/MCP_SERVER.md` — `dialing_get_context` history scoped to the package and
      the `noDialInHistory` block; `ai_conversations` list/get entries carry `equipment`.
- [x] 7.3 Wiki manual: no entry, decided 2026-08-23. This changes how the advisor selects its own
      context, which nothing asks the user to do differently. The one user-visible consequence is
      that the conversation list can hold several threads for one bean and profile, told apart by
      the package in their labels — a label that explains itself, which is not what the manual is
      for.

## 8. Auto-favourites (folded in — same PR)

The grouping already keyed on `equipment_id` in the "+ Grinder" modes and not in the default one,
while the stats query mirrored that split. A card counted one package and averaged another.

- [x] 8.1 `bean_profile` (the default) keys on the package. Two baskets on one bean and profile are
      two cards; verified live, a 41-shot card became 34 + 7.
- [x] 8.2 The details query scopes to the package for every mode that keys on it, with the
      condition hoisted out of the per-mode branches so a new mode cannot skip it. Verified
      against the database: the 7-shot card's averages are exactly those 7.
- [x] 8.3 Cards follow the Shot History row: identity line (recipe, else profile), secondary line
      for what it did not carry, then the metrics. The package appears only when there is no
      recipe name — a recipe already names its equipment.
- [x] 8.4 Info page names the package rather than composing the grinder, and shows "Last Grind"
      (the group's latest, which is what Load applies) instead of presenting one setting as the
      group's. The "Grind Setting:" row now appears only in the modes that key on one.
- [ ] 8.5 No test covers any of this — `tests/` has nothing for auto-favourites. The recurring
      defect shape is "a query that keys on the package does not scope to it", which is worth one
      test asserting the card count and the details count agree for a two-package fixture.

## 9. One payload, one format

Both surfaces build their system prompt from `ShotSummarizer::shotAnalysisSystemPrompt` — ~45,000
characters naming the structured blocks 35 times and instructing the model to read them by field
path. `ai_advisor_invoke` sends that payload. The in-app overlay sends prose. One prompt, two
formats, and the prose one delivers none of the paths the prompt describes.

- [x] 9.1 Route `AIManager::emitRecentShotContext` through `DialingBlocks::buildDialInSessionsBlock`
      and `buildBestRecentShotBlock`. Its thread already opens `withTempDb`, builds an
      `AdviceScope`, and calls `buildGrinderCalibrationBlock` / `buildRecentAdviceBlock` — the same
      builders MCP uses — so two of the four blocks are already shared and the remaining two are
      hand-rendered from the same `qualifiedShots` and the same DB handle. Verify: no block is
      built twice, in two places, from one dataset.
- [x] 9.2 Send the structured payload from the in-app path via
      `AIManager::enrichUserPromptObject`, which today has exactly one caller and it is
      `mcptools_ai.cpp`. Verify: `ai_advisor_invoke --dryRun` and the in-app send produce the same
      top-level keys for the same shot.
- [x] 9.3 Delete the prose `### Setup:` header and the seven hand-declared `setup*` fields with
      their `seedOrCompare` calls (`aimanager.cpp`). The shared-setup detection it performs is what
      `hoistSessionContext` already does over `ShotIdentity::fields()`. Verify: adding a row to
      that table reaches both surfaces with no further edit — the check scenario in the spec.
- [x] 9.4 Carry the user's question and the shot label as their own fields instead of
      concatenating them around the payload, and delete every reader that existed only to undo
      that concatenation. `getConversationText` recovered the question by searching for
      `"Here's my latest shot:"`, taking the last `\n\n`, and guessing whether the trailing text
      "looks like a question" by testing for `": "` and a length under 500; `extractShotFields`
      carried a hand-written brace-matching JSON scanner (depth counter, string-literal and escape
      handling) plus seven regexes, because "Qt's JSON parser rejects trailing prose".
      **All of it is gone**, because the change already wipes stored conversations once — they are
      keyed on the equipment package now and the old keys cannot match. The wipe marker is
      `equipment_scoped_conversations_v2`, not `v1`: `v1` already fired on any device that ran an
      interim build of this branch, and those devices would otherwise keep prose turns no reader
      can render. Verify: content parses with a plain `QJsonDocument::fromJson`; no regex, brace
      scanner, prose extractor or question heuristic remains; the wipe is observed in the log.
- [x] 9.5 Keep the displayed conversation unchanged for the user. `getConversationText` feeds four
      QML call sites (`ConversationOverlay.qml` ×2, `SettingsAITab.qml` ×2); the `**[Shot <date>]**`
      / `**You:** …` rendering is what a user sees and SHALL survive the format change. Verified on
      screen 2026-08-23 against a two-turn conversation on the Aug 22, 9:37 AM shot: the second
      turn renders `**[Shot Aug 22, 9:37 AM] You:** How much finer, in Niche Zero numbers?` with no
      JSON reaching the display, and the overlay header carries the equipment package
      (`… / D-Flow / Q / Niche Zero / Decent 18g Ridged`).
- [x] 9.6 Live A/B before merge. This changes what the in-app advisor receives, which a green suite
      cannot judge. Run the same shot through both formats and read the replies. Verified
      2026-08-23 on the Aug 22, 9:37 AM shot, Gemini: the in-app reply cites the tasting feedback
      (75/100, balanced, medium), the profile's own 84 °C / 6 bar intent, the measured 8.2 bar peak,
      and the Aug 21, 11:33 AM shot at the same 9.5 setting — every one of those a field path the
      shared system prompt names and the prose payload never delivered. Both surfaces' stored
      payloads carry the same ten top-level keys for the same shot, plus `question` and `shotLabel`
      on the conversation side; the two system prompts differ only by the intended
      `## Multi-Shot Context` appendix.
- [x] 9.7 `docs/CLAUDE_MD/AI_ADVISOR.md` — one payload, one assembler, and the field paths the
      system prompt names. Verify: no section still describes an in-app prose payload.
- [x] 9.8 One identity definition, sliced per increment. `ShotIdentity`'s 12 `QString` fields are
      a **strict subset** of `CurrentBeanBlockInputs`'s 16 — every one of `basketBrand`,
      `basketModel`, `beanBrand`, `beanType`, `defrostDate`, `frozenDate`, `grinderBrand`,
      `grinderBurrs`, `grinderModel`, `openedDate`, `puckPrep`, `storageHint` is declared in both,
      and the four extras (`beanBaseJson`, `grinderSetting`, `roastDate`, `roastLevel`) are what
      makes `currentBean` a different increment, not a different dataset. The advisor slices the
      same identity three ways — `currentBean` (the setup now), `dialInSessions[].context` (the
      setup a past session shared), per-shot overrides (what one shot differed on) — and only the
      last two read `ShotIdentity::fields()`. Make the increment a selection over one definition:
      `CurrentBeanBlockInputs` composes the identity rather than redeclaring it. Verify: adding a
      component is one row, and no struct lists an identity field a second time.
- [x] 9.9 Sweep the other increments for the same shape before closing this out —
      `bestRecentShot` (grinderModel + grinderSetting), `grinderCalibration` (grinderModel),
      `grinderContext`. Each is a narrower slice of the same identity. Verify: each names the
      fields it needs by selecting from the table, or the task records why a slice genuinely
      cannot.

### Landed under section 9

Net −814 lines before the docs, all from code that existed only to render or
un-render a second format:

- `AIManager::emitRecentShotContext` — the 264-line prose renderer, and
  `renderRecentAdviceEntry` (57) and `loadQualifiedShots` (112) with it, the last
  being the bean+profile+window history query `buildDialInSessionsBlock` already
  does.
- `tests/tst_aimanager.cpp` — 14 test slots (596 lines) pinning `### Setup:` header
  text. The invariant they asserted (identity shared across a session is hoisted
  once) is asserted by `tst_dialing_helpers.cpp` against `hoistSessionContext`;
  rewriting them would have been the same invariant in two places.
- `AIConversation::processShotForConversation` — replaced by
  `changesFromPreviousShot`, which returns the diff as an object instead of
  prepending a `**Changes from Shot (X)**` banner to the payload.
- Two comments in `mcptools_ai.cpp` asserting the surfaces were "produced by the
  same shared helpers so the userPromptUsed echo is byte-equivalent" — false in
  both directions until this change made it true.
- Every reader of the prose wrapper, once the one-time conversation wipe made
  legacy content unreachable: the brace-matching JSON scanner, seven regexes,
  `extractShotProse`, the `fromStructuredEnvelope` flag, the ~70-line
  question-recovery heuristic in `getConversationText`, and `addShotContext` (31
  lines) — the last being the only remaining producer of the wrapper, and it had
  no callers.
- `CurrentBeanBlockInputs` now composes `DialingHelpers::ShotIdentity` instead of
  redeclaring its twelve fields; `beanInputsFromProjection` drops fourteen
  hand-written assignments for one `identityFromShot` call. The compiler found all
  18 call sites.
