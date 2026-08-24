## Why

The AI Advisor groups a shot's history by bean + profile and nothing else, so shots pulled on
different equipment land in one dial-in story. A user with two equipment packages on the same
grinder — a straight-wall Decent 18g basket dialled at 9.75, a stepped Graph Coffee 58→46mm
basket dialled at 17 — gets those two dials compared as if they were one. In the reported case
(Aug 2026, Sweet Bloom Hometown Blend on D-Flow / Q) the advisor saw a single dial jump from
9.75 to 17 with pressure rising rather than falling, had no way to learn that the basket had
changed, and concluded the burrs "may have crossed a zero-point threshold". It then recommended
10.5 — a setting for the other basket entirely.

Two failures compound here. The history is filtered on the wrong key, and the equipment is
absent from the payload, so the advisor cannot name what it is comparing or notice that it is
comparing across a basket change. The same reply also cited a "70/100 shot" that appears nowhere
in its context and reasoned from it: with no anchor available, the model invented one.

## What Changes

- The in-app advisor's recent-shot history SHALL match on the shot's equipment package
  (`equipment_id`) in addition to bean and profile. A package forks whenever the grinder
  brand/model/burrs, the basket, or the puck prep changes, so one integer answers "is this the
  same gear?" for all three.
- The MCP advisor's `dialInSessions` history SHALL be filtered the same way, so the two surfaces
  agree.
- `bestRecentShot` — the highest-rated recent shot, handed to the model as the target to aim at
  — SHALL be matched on equipment too. An anchor from the wrong basket is worse than a history
  entry from it: the model treats an anchor as the goal, not as one data point among several.
- `grinderContext` — the observed-settings list and explored range for the user's grinder —
  SHALL be scoped to the equipment package. Today it pools every setting used on that grinder
  model, so a stepped-basket dial appears in the same list as a straight-wall one and the model
  reads them as one continuous range.
- `grinderCalibration` (and the `dialing_get_grinder_calibration` tool that shares its builder)
  SHALL mine its within-batch pairs and its current-batch anchor from the current equipment
  package only. It currently matches every package sharing the grinder model and burrs, so a
  pair can straddle two baskets — and this block emits a NUMBER presented as a recommended
  setting, which makes it the costliest place to pool. The anchor the number is built on is
  package-blind today, and endpoint medians pool both baskets before any pair is formed.
- Matching SHALL use `COALESCE(equipment_id, 0)` rather than a bare `=`. Everything unpackaged
  falls into bucket 0 and matches every other bucket-0 shot, so a user who has never created a
  package sees no change — SQL `NULL = NULL` is not true, and a bare compare would silently drop
  every unpackaged shot.
- The equipment set SHALL be named in the payload, once, in one format. `ShotIdentity::fields()`
  already puts `basketBrand` / `basketModel` / `puckPrep` into the MCP session `context` from a
  single table row. The in-app surface does not read that table: it hand-renders a prose
  `### Setup:` header from its own list of setup fields, which is why the same three components
  do not reach it. **The fix is to delete the second renderer, not to add a ninth copy of the
  field list to it** — see "One payload, one format" below.
- When no prior shots match, the in-app advisor SHALL emit an explicit "no prior shots with this
  equipment set" block naming the set, instead of omitting the history section. An absent block
  is indistinguishable from "this user has no history", and an unanchored model invents an
  anchor.
- The system prompt SHALL state that grind settings are comparable only within one equipment
  set, and SHALL forbid citing a shot, score, or taste note that is not present in the context.
- The advisor's conversation thread SHALL be keyed on the equipment package in addition to bean
  and profile. A saved conversation replays its stored turns to the model on every request, and
  those turns contain the old cross-equipment context verbatim — filtering future context does
  nothing about contamination already in the transcript. Changing the key retires every existing
  thread, so the first advisor use after upgrade starts clean with no migration step, and keeps
  a later basket switch from re-accumulating a mixed thread.
- **BREAKING** for advisor context only: after an equipment change (including a puck-prep edit,
  which forks the package), the advisor's history starts empty until shots accumulate on the new
  set. This is intended — those shots make different coffee at the same dial — and the explicit
  no-history block is what keeps the state legible rather than silent.

## One payload, one format

The advisor has two surfaces and they send the model two different things. `ai_advisor_invoke`
(MCP) sends a JSON object — `dialInSessions`, `bestRecentShot`, `currentBean`, `grinderContext`,
`grinderCalibration`, `recentAdvice`, `sawPrediction`. The in-app conversation overlay sends
prose: a hand-rendered `### Setup:` header plus per-shot markdown, composed in QML and passed
to `AIConversation::ask()` verbatim.

**Both surfaces build their system prompt from the same function.**
`AIConversation::multiShotSystemPrompt` calls `ShotSummarizer::shotAnalysisSystemPrompt` and
appends a Multi-Shot Context paragraph; `ai_advisor_invoke` calls the same builder. That shared
prompt is ~45,000 characters and it names the structured blocks 35 times — `currentBean` 16,
`dialInSessions` 7, `bestRecentShot` 5, `tastingFeedback` 4, `recentAdvice` 2, `grinderContext`
1 — instructing the model to read them by field path.

The in-app surface delivers none of those paths. The model is told to read
`dialInSessions[].context` and receives markdown. So one prompt is being served by two payload
formats, only one of which it describes.

The second renderer is not free:

- **Every field is written twice.** `ShotIdentity::fields()` is the one table for shot identity,
  and the prose header does not use it — it declares, seeds, compares and renders seven setup
  fields by hand. That is exactly why this change's own task list contains "the `### Setup:`
  header gains basket and puck prep": the JSON side got them from one row, the prose side needs
  four more edits. The next component will need them again.
- **The composed prose is parsed back out with string heuristics.**
  `AIConversation::getConversationText` recovers the user's question from the sent message by
  searching for `"Here's my latest shot:"`, taking the last `\n\n`, and guessing (its own
  comment says "Simple heuristic") whether what follows "looks like a question" by testing for
  `": "` and a length under 500. That parser exists only because the question is glued to a
  prose payload; it is ~70 lines and it has no correct implementation.
- **Blocks are already half-shared.** `emitRecentShotContext`'s own thread opens `withTempDb`,
  builds an `AdviceScope`, and calls `DialingBlocks::buildGrinderCalibrationBlock` and
  `buildRecentAdviceBlock` — the same builders MCP uses — then hand-renders the remaining two
  blocks as prose from the same `qualifiedShots` and the same DB handle. The split is not along
  any line in the data.

So: **one format, and it is the JSON one**, because that is what the shared system prompt
describes and what the MCP surface already proves out. The in-app path routes through
`DialingBlocks::buildDialInSessionsBlock` / `buildBestRecentShotBlock` and
`AIManager::enrichUserPromptObject` like the MCP path does, the prose header and its field list
are deleted, and the user's question travels as its own field instead of being recovered from
the text by heuristic.

This is in scope here rather than deferred because this change is what surfaced it: the
equipment-scope work has to touch every place the payload names the gear, and doing that twice
is the cost the change exists to remove.

## Capabilities

### New Capabilities
<!-- None. Both surfaces already have specs covering the payload they build. -->

### Modified Capabilities
- `advisor-user-prompt`: the in-app advisor's history enrichment gains an equipment-package match
  and a named equipment set in the hoisted Setup header; a new no-history block; two new system
  prompt rules (grind comparability within an equipment set, no citing absent shots/scores).
- `dialing-context-payload`: `dialInSessions` history, `bestRecentShot` selection,
  `grinderContext` observed settings and the `grinderCalibration` data pool are all scoped to
  the resolved shot's equipment package, and the session-level identity hoist gains
  `basketBrand`, `basketModel` and `puckPrep`.
- `advisor-conversation-history`: the advisor's conversation thread is identified by equipment
  package as well as bean and profile.

## Impact

Implementation is `AdviceScope` (`src/history/shotscope.h`): the equipment package as a value
carrying its own SQL predicate, taken as argument 3 by every advice-scoped read. The predicate
and its bucket travel together, so a call site cannot forget to filter, misname the table alias,
mismatch the surrounding statement's placeholder style, or bind a null. Those were four separate
things to remember at each selection point, and remembering is what failed repeatedly.

Delivered:

- `src/history/shotscope.h` — new, the scope value.
- `src/history/shothistorystorage_queries.cpp` / `.h` — `loadRecentShotsByKbIdStatic` and
  `queryGrinderContext`'s observed settings and RPM axes take a scope. The grinder-model subquery
  in `queryGrinderContext` becomes redundant (a package names one grinder) and goes, taking its
  `:model` bind with it.
- `src/ai/dialing_blocks.cpp` / `.h` — `buildDialInSessionsBlock` (via the loader),
  `buildBestRecentShotBlock`, `buildGrinderContextBlock`, `buildGrinderCalibrationBlock`.
  Calibration's inline grinder-identity subquery is replaced by the scope, which also removes
  `grinderBurrs` from the signature and from four call sites.
- `src/ai/aimanager.cpp`, `src/mcp/mcptools_dialing.cpp`, `src/mcp/mcptools_ai.cpp` — resolve the
  scope from the shot under review rather than from live machine state.
- Deleted: `requestRecentShotsByKbId` and `recentShotsByKbIdReady`, which had no caller in any
  surface, so no dead path needed a scope invented for it.

Not scoped, deliberately: `stepSize` / `rpmStepSize` stay grinder-wide, because the advisor's
step must equal the Grind quick-select widget's `grindStepForGrinder()` on the same screen.
`grinderModelMatchSql`'s remaining callers are that derivation and a picker read.

Not yet delivered — see `tasks.md`:

- The conversation key, which is the requirement that actually repairs the reported case. A saved
  thread replays its stored turns on every request, so scoping future context leaves the
  contaminated transcript intact until it ages out of the LRU.
- The named equipment set in the payload, the explicit no-history block, the two system prompt
  rules, the MCP identity hoist fields, and the import/backup package remap.

- Saved advisor conversations from before the key change become unreferenced and age out of the
  five-thread LRU. No data is deleted eagerly.
- No schema migration. `shots.equipment_id` already exists and migration 22 backfilled it.
- Wiki manual: no entry. This changes how the advisor selects its own context; nothing new is
  discoverable or actionable by the user.
