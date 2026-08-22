## Context

See proposal.md — Why. The relevant current state:

- `shots.equipment_id` is an FK to `equipment_packages.id`, added and backfilled by migration 22.
  A package holds a grinder item, an optional basket item and an optional puckprep item.
  `EquipmentStorage::updateGrinderIdentityStatic` forks a new package whenever the grinder
  brand/model/burrs, the basket, or the puck-prep set changes — with one deliberate asymmetry:
  filling in a component that was previously empty is *enrichment* and folds rather than forking
  (#1713).
- **Seven** independent selections feed the advisor and none of them consult the equipment
  package:
  1. `AIManager::loadQualifiedShots` — in-app history; bean + profile + 21-day window.
  2. `ShotHistoryStorage::loadRecentShotsByKbIdStatic` — MCP `dialInSessions`; profile KB id.
  3. the `bestRecentShot` query in `DialingBlocks::buildBestRecentShotBlock` — profile KB id +
     rating + 90-day window.
  4. `AIManager::conversationKey` — bean + profile.
  5. `ShotHistoryStorage::queryGrinderContext` — observed settings / explored range / typical
     step; grinder model + beverage + bean brand.
  6. the history query in `DialingBlocks::buildGrinderCalibrationBlock` — grinder model + burrs,
     explicitly spanning every package that shares them.
  7. `dialing_get_grinder_calibration` — the standalone MCP tool, which calls the same builder
     as 6 and so is fixed by the same change.

  5 and 6 were found only on a second pass, after 1–4 were already scoped. They are listed
  explicitly because the pattern — "this query is about the grinder, so the grinder is the right
  key" — is exactly the reasoning that produced the defect, and it will look correct again to
  the next reader.
- The in-app advisor sends **prose**, not the JSON envelope: `ConversationOverlay.openWithShot`
  calls `buildShotAnalysisProseForShot`, so `currentBean.basket` — which the MCP surface does
  carry — never reaches the model in-app. The hoisted `### Setup:` header on the history block
  is the only place equipment identity appears there.
- `ShotProjection` already carries `basketBrand`, `basketModel`, `puckPrep` and `equipmentId`,
  so no new plumbing is needed to get equipment into either renderer.

## Goals / Non-Goals

**Goals:**

- One equipment-match rule, applied identically at all seven selection points.
- Equipment visible in the payload wherever it is filtered on, so the model can name it and the
  filter is never silent.
- A clean advisor conversation on first use after upgrade, without a migration step.

**Non-Goals:**

- Switching the in-app advisor from prose to the JSON envelope. That would carry basket, puck
  prep and bean freshness to the in-app surface for free and is worth doing, but it changes the
  payload shape for every in-app request and belongs in its own change.
- Any schema change. `equipment_id` exists and is populated.
- Cross-equipment *translation* — telling the user what setting 9.75 on one basket corresponds
  to on another. The UGS grind-calibration machinery is the place for that question if it is
  ever asked; this change only stops the surfaces from conflating them.
- Deleting saved conversations. Retiring them by key is enough.

## Decisions

### D1: Match on `equipment_id`, not on the basket alone

The basket is what makes the two dials incomparable in the reported case, so basket-only
matching was considered. Rejected: a package forks on grinder identity *and* basket *and* puck
prep, so `equipment_id` answers all three questions with one integer compare and no join, and a
basket-only key would pool two grinders the moment a user owns two. It also matches how the rest
of the app already keys dial history (the history-cards grouping at
`shothistorystorage_queries.cpp:1486` groups on `equipment_id`).

The cost is that `equipment_id` is stricter than physical comparability — a puck-prep edit forks
the package and empties the history. Accepted deliberately: the puck prep of the earlier shots
really was different, the enrichment rule keeps the common "I finally typed in my gear" case
from forking, and D3 makes the empty state legible rather than silent.

### D2: `COALESCE(equipment_id, 0)` on both sides, never a bare `=`

SQL `NULL = NULL` is not true, so a bare compare would drop every unpackaged shot rather than
matching it. Coalescing puts all unpackaged shots in bucket 0, where they match each other —
which is the single-equipment-set user, for whom the filter is then a no-op. Same idiom the
history-cards grouping already uses.

### D3: An empty history is stated, not omitted

A missing history block cannot be distinguished from "this user has no history", and the
reported failure shows what an unanchored model does with that: it cited a 70/100 shot that
exists nowhere in its context and reasoned from it. The no-history block names the equipment set
that was matched on and says why other shots were excluded, giving the model a fact to use in
place of an invented one. This is why the equipment description is worth sharing between the
Setup header and this block rather than writing twice — two copies of one phrase drift, and a
reader comparing them cannot tell which is stale.

### D4: Change the conversation key rather than migrating stored conversations

A one-time wipe stamped with a version marker was the alternative. Rejected: it is a strictly
weaker version of the same outcome. A saved thread replays its stored turns on every request, so
a user who switches baskets back and forth within one bean+profile rebuilds a mixed transcript
immediately after the wipe. Keying the thread on the equipment package fixes the upgrade case
*and* the recurring case, and needs no migration code at all — every pre-upgrade key simply
stops matching, so the first use after upgrade starts clean and the retired threads age out
under the existing five-thread LRU.

### D5: `bestRecentShot` is filtered, not down-ranked

The anchor could have been kept with a "different equipment" annotation. Rejected: the block is
presented to the model as the outcome to reproduce, and its dose/yield/duration/grind are read
as a target. A target the user cannot hit at the settings it reports is worse than no target, so
the block is omitted when no rated shot exists on the matching equipment.

### D6: `grinderCalibration` narrows its pool rather than annotating pairs

Two alternatives were considered for the calibration block: tag each mined pair with its package
and let the estimator weight cross-package pairs down, or require both members of a pair to
share the current package. The second is chosen for the same reason as D5 — the block's output
is a number the user is expected to dial in, and a partially-polluted estimate is
indistinguishable from a clean one at the point of use.

Two facts about this block, both checked rather than assumed, decide how much the narrowing
costs:

**The pool is the whole database, recomputed per call.** The history query carries no time
window and no `LIMIT` (`dialing_blocks.cpp`, the `WHERE equipment_id IN (...)` query), and the
block is rebuilt on every advisor request. Nothing is cached or learned, so filtering does not
start a relearning period — it only changes which existing rows qualify. A package that already
has history keeps all of it. On the reporter's machine that is 1052 shots on the primary package
against 7 on the secondary, so the primary loses nothing and the secondary fills as it is used.
A single-package user sees no change at all.

**The pooling defect is live, not merely latent.** Two mechanisms, and the second is the one
that is easy to miss:

- The anchor — the intercept every emitted number is built on — is the most recent dialed-in
  shot on the current batch on a UGS-placed profile, with no package filter. It can already be a
  shot from the other basket.
- Endpoint medians are pooled by `(batch, kbId)` BEFORE any pair is formed, so a profile's
  endpoint for a given coffee mixes settings from both baskets into one median. The corruption
  is in the endpoint, not only in a pair that happens to straddle.

An earlier draft of this section warned that narrowing would push the block to directional-only
more often, and guessed that cross-basket outliers were currently blowing the dimensionless
spread gate. The device log refutes the guess: `pairs= 2 key= 1.41667 keyValid= false
anchor= true`. With `kCalibMinValidatedPairs = 3`, the binding constraint today is pair COUNT,
not spread — the block is directional for a plainer reason than pollution, and is one qualifying
pair away from publishing a number built on a mixed-basket median. Filtering is therefore a fix
for a live defect that costs an established package nothing, rather than a trade of availability
for correctness. Do not reinstate the warning without re-measuring; it was written from
plausibility and the log disagreed.

### D7: Two prompt rules, phrased as reasoning discipline

The grind-comparability rule is stated as "consider an equipment difference before concluding
anything about the grinder's mechanism" rather than as a prohibition, because the failure it
targets was not a forbidden statement — it was a plausible mechanism theory invented to explain
data the model had no other way to explain. The anti-fabrication rule extends the existing
other-profile-setpoint rule (`shotsummarizer.cpp:1262`) to shots, scores and taste notes, in the
same terms, because that rule's phrasing has held up.

## Risks / Trade-offs

- **A puck-prep edit empties the advisor's history** → Intended (D1), but the user should not
  have to infer it. The no-history block names the set, and the enrichment-vs-fork asymmetry
  keeps the common recording-my-gear case from triggering it.
- **The upgrade discards in-flight advisor conversations** → Accepted; it is the requested
  behaviour. Nothing is deleted, so a retired thread is still on disk until the LRU evicts it.
- **Seven selection points must agree** → This is the central risk. Leaving one unfiltered
  leaves the advisor reasoning across equipment through that one channel, and the more numeric
  the channel, the more damage: `grinderCalibration` emits a specific recommended setting, so a
  cross-basket pair there produces a wrong number stated as fact rather than a vague comparison.
  All seven are in scope and each gets a test. Anything added later that selects prior shots for
  the advisor inherits the same obligation.
- **The in-app surface still drops `currentBean.basket`** → Out of scope (Non-Goals). The Setup
  header and the no-history block cover the equipment-naming requirement on that surface; the
  envelope migration would make them redundant, not wrong.

## Migration Plan

No schema migration. On first run of the new build:

1. `conversationKey` produces different keys, so every saved thread is unreferenced. The user's
   next advisor use starts a fresh conversation. Old threads stay on disk and age out.
2. All seven selections begin filtering on `equipment_id` immediately; no backfill is needed
   because migration 22 populated it.
3. `grinderCalibration` recomputes from the full history on every call, so a package that
   already has shots keeps its calibration immediately. A newly forked package starts
   directional and gains numbers as it accumulates qualifying pairs (D6).

Rollback is a plain revert: the old keys are still on disk and match again, and the queries lose
the filter.
