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

### Known gaps in section 1

- [ ] 1.11 No test exercises the three production sites that BUILD the scope from a live shot
      (`aimanager.cpp` `requestRecentShotContext`, `mcptools_ai.cpp`, `mcptools_dialing.cpp`).
      Every current test calls the block builders with a hand-made `AdviceScope`, so a call site
      passing the wrong shot's `equipmentId` — or falling back to bucket 0 — would compile clean
      and pass the whole suite.
- [ ] 1.12 `docs/CLAUDE_MD/MCP_SERVER.md:1044-1048` cites `getRecentShotsByKbId()`, a symbol that
      does not exist. Pre-existing, adjacent to this work.

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
- [ ] 2.4 `aiconversation.cpp` change detection reporting an equipment-package swap between
      consecutive shots. Not done — the key prevents the mixing; this would only narrate it.
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
- [ ] 3.1b The in-app hoisted `### Setup:` header gains basket and puck prep.
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
