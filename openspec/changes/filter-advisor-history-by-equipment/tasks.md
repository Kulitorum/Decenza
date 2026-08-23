# Tasks

Requirements are in `specs/`. This file tracks delivery against them.

PR #1857 delivers the selection scoping. The conversation key and the payload work are **not**
delivered and are the larger half of the user-visible behaviour.

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

### Known gaps in section 1

- [ ] 1.11 No test exercises the three production sites that BUILD the scope from a live shot
      (`aimanager.cpp` `requestRecentShotContext`, `mcptools_ai.cpp`, `mcptools_dialing.cpp`).
      Every current test calls the block builders with a hand-made `AdviceScope`, so a call site
      passing the wrong shot's `equipmentId` — or falling back to bucket 0 — would compile clean
      and pass the whole suite.
- [ ] 1.12 `docs/CLAUDE_MD/MCP_SERVER.md:1044-1048` cites `getRecentShotsByKbId()`, a symbol that
      does not exist. Pre-existing, adjacent to this work.

## 2. Conversation threads keyed by equipment (NOT delivered)

This is the requirement that repairs the reported case. A saved thread replays its stored turns
on every request, so scoping future context leaves the contaminated transcript in place until it
ages out of the five-slot LRU. For a user with an existing thread, section 1 alone changes
nothing.

- [ ] 2.1 `AIManager::conversationKey` takes the equipment package alongside bean and profile.
- [ ] 2.2 Every caller passes it: `switchConversation`, the `recentAdvice` follow-up lookup, and
      QML's `openWithShot`.
- [ ] 2.3 Throw away pre-upgrade conversations for a user with equipment. They are not
      continued; no notice, no migration, no recovery path.
- [ ] 2.4 `src/ai/aiconversation.cpp` change detection reports an equipment-package swap between
      consecutive shots.
- [ ] 2.5 Tests for the three scenarios in `specs/advisor-conversation-history/spec.md`: fresh
      thread after upgrade, separate thread per basket, resuming the earlier package's thread.

## 3. Name the equipment in the payload (NOT delivered)

Section 1 scopes the data correctly and never tells the model what changed, so numbers move
between conversations with no stated cause.

- [ ] 3.1 `describeEquipmentSet` helper; the in-app hoisted `### Setup:` header gains basket and
      puck prep.
- [ ] 3.2 `DialingHelpers::ShotIdentity` gains basket + puck-prep fields, picked up by the
      existing `hoistSessionContext`.
- [ ] 3.3 MCP session `context` gains `basketBrand` / `basketModel` / `puckPrep` under the
      existing hoist discipline.
- [ ] 3.4 Parity test that the two surfaces emit the same identity fields.

## 4. Explicit no-history block (NOT delivered)

- [ ] 4.1 When no prior shots match, emit a block naming the equipment set instead of omitting
      the history section — an absent block is indistinguishable from "no history at all", and
      an unanchored model invents an anchor.
- [ ] 4.2 Distinguish "the query failed" from "the query ran and matched nothing"; do not report
      one as the other.
- [ ] 4.3 Test that a package with no history produces the block rather than silence.

## 5. System prompt rules (NOT delivered)

- [ ] 5.1 Grind settings are comparable only within one equipment set.
- [ ] 5.2 Do not cite a shot, score, or taste note absent from the context. (The reported reply
      cited a "70/100 shot" that appeared nowhere in its context.)

## 6. Import / backup (NOT delivered)

- [ ] 6.1 Package-id remap through `shotserver_backup.cpp`, `datamigrationclient.cpp` and
      `databasebackupmanager.cpp`, so a restored conversation keys to the destination package.

## 7. Documentation

- [ ] 7.1 `docs/CLAUDE_MD/AI_ADVISOR.md` still describes `queryGrinderContext` as
      "Grinder-model-wide" and `loadRecentShotsByKbIdStatic` as "Last N shots with same profile
      family". Both are now false. The word "equipment" does not appear in the file.
- [ ] 7.2 `docs/CLAUDE_MD/MCP_SERVER.md` — payload fields, once section 3 lands.
- [ ] 7.3 Wiki manual: no entry. This changes how the advisor selects its own context; nothing
      new is discoverable or actionable by the user.
