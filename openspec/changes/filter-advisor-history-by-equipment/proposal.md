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
- The equipment set SHALL be named in the payload on both surfaces: the in-app history's hoisted
  `### Setup:` header gains basket and puck prep, and the MCP session `context` gains
  `basketBrand` / `basketModel` / `puckPrep` under the existing hoist discipline.
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

- `src/ai/aimanager.cpp` — `loadQualifiedShots` (equipment match), `requestRecentShotContext`
  (basket + puck-prep lookup for the current shot), `emitRecentShotContext` (Setup header,
  no-history block), new shared `describeEquipmentSet` helper.
- `src/ai/aimanager.h` — `emitRecentShotContext` gains a trailing `equipmentLabel` parameter.
- `src/ai/dialing_helpers.h` — `ShotIdentity` gains basket + puck-prep fields, picked up by the
  existing `hoistSessionContext`.
- `src/ai/dialing_blocks.cpp` — `buildDialInSessionsBlock` populates and emits the new identity
  fields; `buildBestRecentShotBlock` gains the equipment match.
- `src/ai/aimanager.cpp` / `.h` — `conversationKey` takes the equipment package; every caller
  (`switchConversation`, the `recentAdvice` lookup, QML's `openWithShot`) passes it.
- `src/history/shothistorystorage*` — the `dialInSessions` history loader and
  `queryGrinderContext` gain an equipment-package filter.
- `src/ai/dialing_blocks.cpp` — `buildGrinderCalibrationBlock`'s history query and its anchor
  selection are scoped to the package rather than to grinder model + burrs. The block recomputes
  from the whole database on every call (no window, no limit), so an established package keeps
  its calibration intact; only a newly forked package starts thin and fills as it is used.
- `src/ai/aiconversation.cpp` — change detection reports an equipment-package swap between
  consecutive shots.
- `src/ai/shotsummarizer.cpp` — system prompt rules.
- Tests: `tests/tst_aimanager.cpp`, `tests/tst_dialingblocks.cpp` (or the existing dialing-block
  test target).
- Saved advisor conversations from before the upgrade become unreferenced and age out of the
  five-thread LRU. No data is deleted eagerly; the user simply starts a fresh thread.
- No schema migration. `shots.equipment_id` already exists and migration 22 backfilled it.
- Wiki manual: no entry. This changes how the advisor selects its own context; nothing new is
  discoverable or actionable by the user.
