## Context

See proposal.md — Why for the measured motivation. Design-relevant current state:

- The SAW key is built at one choke point, `SettingsCalibration::sawPairKey(profileFilename,
  scaleType)` → `"<profile>::<normalizedScaleTypeId>"`, used by `perProfileSawHistory`,
  `sawPendingBatch`, `addSawPerPairEntry` and `resetSawLearningForProfile`.
- SAW data lives in **QSettings** (`saw/perProfileHistory`, `saw/perProfileBatch`,
  `saw/globalBootstrapLag/<scale>`, legacy `saw/learningHistory`), not in SQLite. The
  `ShotHistoryStorage` migration chain and its rules do not apply here.
- `sawModelSource()` compares the scale key **raw** against each global-pool entry's stored `scale`
  field; `resolveScaleKey()` exists specifically because of that comparing consumer.
- The active equipment package's basket brand/model is already resident in `SettingsDye`
  (`m_dyeBasketBrand`, `m_dyeBasketModel`, refreshed when the active package changes) — no query
  needed to read it.
- The model snapshot for a shot is taken at `espressoCycleStarted` in `main.cpp`, beside
  `ProfileManager::latchForShot()`; the learning point is written much later, at
  `sawLearningComplete`.

## Goals / Non-Goals

**Goals:**

- One choke point still produces every SAW key, now three-dimensional.
- Existing users lose no history and do not fall back to a cross-profile prior on upgrade.
- No permanent transition machinery: the upgrade path is a one-time re-key, not a fallback
  tier the code carries forever.
- The basket a shot learned under is recoverable from that shot's saved debug log alone.

**Non-Goals:**

- No portafilter/spout dimension, and no reserved field for one (see proposal.md — Assumptions).
- No change to the prediction *math* — σ, recency weighting, batch size N=3, dispersion gate and
  auto-reset rules are untouched.
- **No change to the scale dimension of the key.** The transport-collapse idea was measured and
  refuted (see proposal.md — Rejected); `scale/type`, `knownScales`, `sensorLag()` and the SAW scale
  segment all keep the per-transport canonical type-id.
- No new MCP tool (`mcp-tool-surface-budget`), no new test file (`test-suite-cost`).

## Decisions

### Key shape: a third `::` segment, and segment count distinguishes the tier

`sawPairKey(profile, scaleType, basketKey)` →
`"<profile>::<normalizedScaleTypeId>::<basketKey>"`. The scale segment is unchanged from today —
per-transport, normalized exactly as it already is.

`basketKey` is `brand + " " + model` lowercased with every run of non-alphanumerics collapsed to `-`
(e.g. `Graph Coffee` + `Stepped 58→46mm` → `graph-coffee-stepped-58-46mm`). No basket → the literal
`(none)`, which real values cannot produce because `(` and `)` are not alphanumeric.

A two-segment key is therefore, by construction, a pre-basket key. That is what the one-time
migration below keys off, and it is why detecting un-migrated data needs no schema marker.

Alternative considered: nested JSON (`{profile: {scale: {basket: [...]}}}`). Rejected — it rewrites
every read path and every test for a structure whose only advantage is prettier printing.

### Basket is latched for the shot, like the target

The basket is captured into the SAW snapshot at `espressoCycleStarted` and the *same* captured value
is used when the learning point is written at `sawLearningComplete`. Reading `SettingsDye` again at
write time would let a package switch between the stop and the settling completion file the entry
under a basket that did not pull the shot — the same failure the yield-anchor change had to fix for
the target (see `SAW_LEARNING.md`, "Why the yield-ratio anchor needs NO changes here").

### Read-path tier order

1. per `(profile, scale, basket)` — ≥ `kSawMinMediansForGraduation` medians
2. `globalSawBootstrapLag(scale)`
3. global pool
4. `sensorLag(scale) + 0.1`

Unchanged from today apart from the key. **An earlier draft added a basket-blind tier between
1 and 2**, reading the pre-basket two-segment bucket when the active basket had not graduated,
plus a "warmup" state that blended the two while the basket filled its read window. It was
built, then removed: nothing ever writes those buckets, so they never trim and never age out —
"it ages out" was simply false — and keeping them readable meant a second reader, a blended
tier, two extra `sawModelSource` values and their UI and advisor branches living forever to
smooth about six shots per user, once. The migration below does the same job and then stops
existing.

### Bootstrap pools across baskets, per scale

`recomputeGlobalSawBootstrap(scale)` considers every per-basket bucket on that scale, contributing
each one's most recent committed median lag, IQR-fenced as today. (Review narrowed this: copied
medians and the frozen pre-basket bucket are both excluded — see "The bootstrap contributor set
excludes the source bucket too" below, which supersedes this paragraph.) A brand-new basket
therefore cold-starts from this device's own experience on that scale rather than from 0.38 s. This is
the piece that pays for the extra bucket dimension, and it is the only behavioral change to the
bootstrap: its key is unchanged.

### One-time seed into the combinations actually pulled

`seedSawBucketsFromPreBasketKeys(basketsByProfile, historyComplete)` copies each two-segment bucket
into `"<profile>::<scale>::<basket>"` for every basket that profile was pulled with, guarded by
`saw/basketKeyMigrated`. Scoped per profile, not the (profile × basket) product: a basket used
with one profile says nothing about another, and a profile with no shots in the window is left
alone entirely. Copy rather than move: the two-segment keys stay in place, inert but
readable by an older build on rollback, which also makes the seed repeatable. Copies are tagged
`inherited`, and `recomputeGlobalSawBootstrap()` skips buckets whose newest median carries the
tag — otherwise one batch of shots copied into N baskets votes N times in the cross-basket
median. A basket that already has data is never overwritten, and the flag is set only on a run
marked complete, so an early partial run cannot foreclose the rest. Pending batches are copied
but never committed; the commit path owns the dispersion gate and the auto-reset check.

**The combinations come from the shot history**, via a new bounded
`ShotHistoryStorage::requestRecentProfileBasketPairs(500)` joining `shots.equipment_id` to
`equipment_items` where `kind='basket'` and returning distinct (profile title, basket) pairs,
wired in `MainController` where the storages already are. Not the equipment inventory: a user can
own 25 baskets and pull shots with three, and seeding from the inventory would fabricate 22
buckets of borrowed data.

Windowed rather than a `DISTINCT` over the whole table because `shots` carries the `debug_log`
and `profile_json` blobs, so a full scan drags those pages along. The window is the one real
limitation: a profile untouched for 500 shots is treated as untried and cold-starts from the
bootstrap when it comes back. And `shots.profile_name` is the profile TITLE while SAW keys use
the FILENAME, so `MainController` maps each through `ProfileManager::titleToFilename()`; an
title whose slug matches no stored key drops out — the renamed-profile case, since the
transform consults no catalog and a DELETED profile still yields a filename. `Settings` has no handle on the shot database, which is why the
wiring lives in `MainController` rather than with the other migrations.

### Closing the copy is gated on a demonstrated success, not on absence of failure

Found in review, by three reviewers independently, and worth stating as a decision because the
first implementation looked correct and was not. Once `saw/basketKeyMigrated` is set, nothing
reads two-segment keys — so any path that closes the flag having copied nothing orphans the user's
entire learning history, unrecoverably. Two such paths existed:

- `withTempDb` reports whether the CONNECTION opened and the body ran, **not** whether the body
  succeeded (`dbutils.h:334-336`). Gating the emit on that let a failed query deliver an empty
  list. There is now a separate `queryOk`, set only after the read completes.
- The `!m_ready` early return emitted an empty list. That shape came from
  `requestMostRecentShotId`, whose `-1` a consumer can tell apart from a real answer; an empty
  LIST cannot be told apart from "nothing pulled recently". It now emits nothing.

The seed also refuses an empty basket set when pre-basket buckets still exist — that combination
can only be a failed read, since those buckets exist because shots were pulled. Three independent
guards for one outcome, deliberately, because the outcome is unrecoverable and the flag is
one-way.

Two consequences elsewhere: `sawLearningImport()` clears the flag (a pre-basket export restores
two-segment buckets, and every device has already closed the seed by then), and the seed logs at
INFO on **every** outcome including "created nothing" — the destructive case was the one case the
first version did not log.

### The bootstrap contributor set excludes the source bucket too

`recomputeGlobalSawBootstrap()` skips buckets whose newest median is `inherited`, so one copied
batch cannot vote once per basket. It must also skip the two-segment bucket the copies came from:
it is left in place for rollback and nothing writes it again, so it would contribute a frozen
snapshot of its own past forever. With a single profile and basket its mere presence lifts the
contributor count to 2 and conjures a bootstrap out of the live bucket averaged against its own
history.

### Two rejected designs, and why each looked safer

**A permanent basket-blind fallback tier** — read the two-segment bucket when the active basket
had not graduated, with a "warmup" state blending the two. Built, then removed: nothing ever
writes those buckets, so they never trim and never age out (the claim that they would was
false), and keeping them alive meant a second reader, a blended tier and two extra
`sawModelSource` values with their UI and advisor branches, forever, to smooth about six shots
per user once.

**Re-keying onto the single active basket** — one line simpler than seeding a set, and wrong in
the case that matters: on the maintainer's own device the active package was the 4-shot Graph
basket while 1038 shots of history came from the Decent basket (both counts are the packages'
own `shotCount` aggregates, read from the equipment inventory), so it would have labelled all
of that history Graph. A most-used-basket rule would fix that case but still has to judge which
basket owns a blend. Copying into every basket in use avoids the judgement entirely.

## Risks / Trade-offs

- **More buckets means slower graduation for a new basket** → the migration covers existing
  pairs, and the per-scale bootstrap covers genuinely new baskets. Graduation is 1 committed median (3
  shots), so a new basket is on its own model within three shots either way. This is the one cost the
  change carries, and it is bounded at three shots.
- **Free-text basket brand/model can typo into a sibling bucket** → accepted. Package identity already
  forks on any brand/model change (`equipment-package-model`), so the app's own model has the same
  property; normalization absorbs case and punctuation differences.
- **Users who never set up equipment all land in `(none)`** → identical to today's behavior for them
  (one bucket per profile+scale), so no regression; they simply do not get the benefit.
- **A user who switches basket often now splits their shots across buckets** and each bucket ages more
  slowly, so the trim-10 window covers a longer real time span per basket → accepted; that is the
  point, and staleness within one basket is a smaller error than borrowing another basket's model.
- **Pre-existing drift found while reading the spec**: `stop-at-weight-learning` documents
  `kSawMinMediansForGraduation = 2`; the code is `1` (`settings_calibration.cpp:29`). Reconciled in
  this change (task list) rather than left to be re-read as fact — it is the kind of un-sourced claim
  CLAUDE.md warns licenses wrong code later.

## Migration Plan

1. Ship the three-argument `sawPairKey()` (the basket segment arrives already normalized by
   `sawBasketKey()`) with all callers updated.
2. On first launch after upgrading, `MainController` queries the last 500 shots for the baskets in
   use and `seedSawBucketsFromPreBasketKeys()` copies every two-segment bucket into each of them.
3. Rollback: the seed copies rather than moves, so the two-segment keys an older build reads are
   still there, untouched. Rolling back is lossless.
