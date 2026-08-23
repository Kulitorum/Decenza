## Context

See proposal.md — Why.

`shots.equipment_id` is an FK to `equipment_packages.id`, backfilled by migration 22. A package
holds a grinder item, an optional basket and an optional puck-prep set, and forks when any of
them changes on a package that already has shots — except that an unused package is edited in
place, and filling in a previously-empty component folds rather than forks (#1713).

**Eight** independent selections feed the advisor and none consult the package:

1. `AIManager::loadQualifiedShots` — in-app history; bean + profile + 21 days.
2. `ShotHistoryStorage::loadRecentShotsByKbIdStatic` — MCP `dialInSessions`; profile KB id.
3. `DialingBlocks::buildBestRecentShotBlock` — KB id + rating + 90 days.
4. `AIManager::conversationKey` — bean + profile.
5. `ShotHistoryStorage::queryGrinderContext` — grinder model + beverage + bean brand.
6. `DialingBlocks::buildGrinderCalibrationBlock` — grinder model + burrs, explicitly spanning
   every package that shares them.
7. `dialing_get_grinder_calibration` — calls the same builder as 6.
8. the follow-up lookup in `DialingBlocks::buildRecentAdviceBlock` — KB id + timestamp, no
   equipment predicate. Without one, a user who takes the advice on one basket and pulls on the
   other has the other basket's shot scored as their response.

The pattern that hid 5, 6 and 8 — "this query is about the grinder, so the grinder is the right
key" — will look correct again to the next reader.

The in-app advisor sends prose, not the JSON envelope, so `currentBean.basket` never reaches the
model in-app; the `### Setup:` header is the only place equipment identity appears there.
`ShotProjection` already carries `basketBrand`, `basketModel`, `puckPrep` and `equipmentId`.

## Goals / Non-Goals

**Goals:** one equipment-match rule at all eight points, with one deliberate split in how they
degrade — a selection publishing a NUMBER the user is expected to dial in scopes
unconditionally, one merely reporting the user's own shots may fall back to unscoped. Equipment
visible wherever it is filtered on, so the filter is never silent. A clean conversation on first
use after upgrade (D4).

**Non-Goals:** switching the in-app advisor to the JSON envelope (worth doing, own change); any
schema change; cross-equipment *translation* of settings; retaining saved conversations across
the upgrade.

## Decisions

**D1 — Match on `equipment_id`, not on the basket alone.** A package forks on grinder identity
*and* basket *and* puck prep, so one integer compare answers all three with no join; basket-only
would pool two grinders the moment a user owns two. Matches how history-cards already group
(`shothistorystorage_queries.cpp:1486`). Cost: stricter than physical comparability — a
puck-prep edit forks and empties the history. Accepted; the enrichment rule covers the common
"I finally typed in my gear" case, and D3 makes the empty state legible.

**D2 — `COALESCE(equipment_id, 0)` on both sides, never a bare `=`.** SQL `NULL = NULL` is not
true, so a bare compare drops every unpackaged shot instead of matching it. Bucket 0 holds them
all, where they match each other.

**D3 — An empty history is stated, not omitted.** A missing block is indistinguishable from "no
history", and the reported failure shows what an unanchored model does with that: it cited a
70/100 shot that exists nowhere in its context. The block names the equipment set matched on.
Carve-out: when the set cannot be NAMED (no package at all — the majority case) the block stays
absent, because scoping was a no-op for those users and "no prior shots with this equipment set
()" would assert a filter that did not run.

**D4 — Change the conversation key, AND wipe once on upgrade.** The key is the load-bearing
half: a wipe alone is strictly weaker, since a user switching baskets within one bean+profile
rebuilds a mixed transcript immediately. But the key alone does not cover the upgrade case,
because `loadMostRecentConversation()` restores the newest thread **by key** at construction,
with no bean/profile lookup to fail — so a pre-upgrade thread is unreferenced by every lookup
path and still returns on the startup path. Hence `clearAllConversationsOnce` with marker
`equipment_scoped_conversations_v1`.

**D5 — `bestRecentShot` is filtered, not down-ranked.** The block is read as the outcome to
reproduce and its settings as a target. A target the user cannot hit at the settings it reports
is worse than no target.

**D6 — `grinderCalibration` narrows its pool rather than annotating pairs.** Same reason as D5:
its output is a number to dial in, and a partially-polluted estimate is indistinguishable from a
clean one at the point of use. Two checked facts decide the cost:

- *The pool is the whole database, recomputed per call* — no time window, no `LIMIT`, nothing
  cached. Filtering starts no relearning period; a package with history keeps all of it. On the
  reporter's machine: 1052 shots on the primary package against 7 on the secondary.
- *The pooling defect is live, not latent.* The anchor is the most recent dialed-in shot on the
  batch with no package filter, and endpoint medians are pooled by `(batch, kbId)` **before** any
  pair is formed — so the corruption is in the endpoint, not only in a straddling pair.

The device log (`pairs= 2 key= 1.41667 keyValid= false anchor= true`, against
`kCalibMinValidatedPairs = 3`) shows the binding constraint today is pair COUNT, not spread. Do
not reinstate a "narrowing pushes this to directional-only" warning without re-measuring — that
was written from plausibility and the log disagreed.

**D7 — Two prompt rules, phrased as reasoning discipline.** The grind-comparability rule says
"consider an equipment difference before concluding anything about the grinder's mechanism"
rather than prohibiting a statement, because the failure was a plausible mechanism theory
invented to explain otherwise-unexplainable data. The anti-fabrication rule extends the existing
other-profile-setpoint rule (`shotsummarizer.cpp:1262`) to shots, scores and taste notes.

## Risks / Trade-offs

- **A puck-prep edit empties the advisor's history** — intended (D1); the no-history block names
  the set so the user does not have to infer it.
- **The upgrade discards in-flight conversations** — the requested behaviour. Nothing is
  deleted; retired threads sit on disk until the LRU evicts them.
- **Eight selection points must agree** — the central risk. The more numeric the channel, the
  more damage: `grinderCalibration` emits a specific setting, so a cross-basket pair there
  produces a wrong number stated as fact. Each of the eight gets a test, and anything added
  later that selects prior shots for the advisor inherits the same obligation.

## Migration Plan

No schema migration. On first run: `conversationKey` produces different keys and
`clearAllConversationsOnce` retires the old threads; all eight selections begin filtering
immediately (migration 22 already populated `equipment_id`); `grinderCalibration` recomputes
from full history per call, so an established package keeps its calibration and a newly forked
one starts directional. Rollback is a plain revert.
