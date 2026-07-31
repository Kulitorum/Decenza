## Context

A grinder's **step** is the smallest dial increment the user makes repeatedly — `0.25` on a Niche
Zero with 28 observed settings. It decides how far each row of the grind picker moves and, via
`_stepDecimals`, how many decimals a written-back value keeps. It is derived by `deriveGrindStep()`
from the distinct numeric `grinder_setting` values recorded against that grinder model.

One estimator, two delivery paths:

```
                       deriveGrindStep()  (shared, correct)
                        /                        \
   grinderWideStep()                          grindStepForGrinder()
   direct SQL, synchronous                    reads m_distinctCache["grinder_setting:<model>"]
   runs on queryGrinderContext's              async: cache miss -> return 0, kick a fetch,
   background thread                          QML re-asks on distinctCacheReady
        |                                          |
   AI dialing payload                          GrindRowSource  ->  5 QML hosts
   (works)                                     ShotServer grind candidates  (broken)
```

The cached path has two holes. `requestDistinctCache()` clears the entire cache but refills only
its six bare columns, so `grinder_setting:<model>` is dropped by every shot save, shot edit and
equipment change. And a single-key fetch whose key was cleared mid-flight is discarded by
`if (!m_pendingDistinctKeys.remove(cacheKey)) return;` — no re-request, no `distinctCacheReady` —
so nothing schedules another attempt and the caller's `1.0` fallback becomes permanent. The device
log for #1724-era builds ends in exactly that state.

Two constraints bound the fix:

- **No DB I/O on the main thread** (CLAUDE.md). Not a latency preference: a read contending with an
  in-flight shot-save transaction waits on `busy_timeout`, unbounded, regardless of query cost.
- **The query is a full table scan.** No index on `shots(equipment_id)`, and the scan reads whole
  pages including the `debug_log` / `profile_json` / `steam_json` blobs it never looks at, so cost
  is linear in table bytes rather than in rows that matter.

Measured, warm page cache, one machine, real 1,123-shot DB and a 20× inflation of it (21,776 shots,
190 MB):

| Query | 1,123 shots | 21,776 shots | `(equipment_id)` | covering `(equipment_id, grinder_setting)` |
|---|---|---|---|---|
| One grinder | 1.7 ms | 33.9 ms (max 702) | 16.5 ms | **1.18 ms** |
| All grinders | 2.0 ms | 40.1 ms | 21.9 ms | **5.12 ms** |

## Goals / Non-Goals

**Goals:**

- The step a reader sees is the step the history supports, at the instant of the read, on every
  surface — with no async round-trip to wait out and no fallback standing in for a cold cache.
- Derivation cost that does not grow with shot-blob volume.
- One derivation, three readers. The widget, the web endpoint and the AI payload are the same
  number by construction.
- The `1.0` / `50` fallbacks mean what the spec says they mean: no derivable history.

**Non-Goals:**

- Redesigning the distinct-value cache. Its two defects are repaired in place (D6); its structure,
  its async shape and its `distinctCacheReady` contract are left alone.
- Changing `deriveGrindStep()`'s semantics: smallest-repeated-gap, 0.05 floor,
  grinder-model-wide scope, all unchanged.
- Changing what `GrindRowSource`, the picker, or `GrindCandidates` do with the step.

## Decisions

### D1: Derive every grinder at once, hold the result in memory

The store computes a `model -> {grindStep, rpmStep}` map in one background query and serves reads
from it.

*Why:* the measurement makes the choice. Deriving **all** grinders costs the same as deriving one
(2.0 ms vs 1.7 ms at current size; 5.1 ms vs 1.2 ms indexed at 20×) because both are dominated by
the same scan or the same index walk. Once that is true, there is no reason to derive lazily per
model, and lazily-per-model is precisely what forced the async round-trip whose failure is the bug.
A resident map makes the read synchronous, which is the only way a QML binding and a one-shot HTTP
handler can both be correct at the moment they ask.

*Alternatives considered:*

- **Fix the cache lifecycle** (re-request composite keys on refresh; make the discard path emit
  `distinctCacheReady`). Correct, and repairs the other composite keys too — but it keeps the
  async round-trip, so a reader that cannot wait (ShotServer) still gets a fallback on a cold
  cache, with no recovery. Fixes the reported symptom without fixing the shape.
- **Memoize the last good step per model** (the previous draft of this proposal). Simpler, and it
  makes reads synchronous — but it keeps a "never derived yet" hole on first use, and keeps two
  derivation paths alive. Eager derivation is the same code with the hole closed.
- **Derive on picker open.** Reaches only the five QML hosts, which all share `GrindRowSource`;
  ShotServer answers a one-shot request with no binding to re-fire. A per-surface hook is also a
  thing a future surface can forget to add.
- **Derive on every read, synchronously, on the calling thread.** 1.7 ms today makes this look
  free; 33.9 ms at 20× (≈150–200 ms on a tablet, 702 ms tail observed) does not, and the
  `busy_timeout` contention case is unbounded at any size.

### D2: Covering index `shots(equipment_id, grinder_setting, rpm)`

*Why:* it removes the scan's dependence on blob size. `EXPLAIN QUERY PLAN` reports
`SEARCH s USING COVERING INDEX` — SQLite answers from the index and never visits a row page, so
20× the shots cost 1.18 ms instead of 33.9 ms, a 29× win, and the number stops tracking how large
`debug_log` grows. Growth in this table is overwhelmingly blob growth, so this is the axis that
matters.

Three columns, not two, so the RPM counterpart is covered by the same index rather than needing a
second one. It costs one extra index maintained per shot insert — a single row per shot, against a
write path that already writes several kilobytes.

*Alternatives considered:* plain `(equipment_id)` — only halves the cost (16.5 ms at 20×), because
SQLite still visits every matching row page to read `grinder_setting`, and nearly every row matches
the dominant grinder. Not worth a migration.

### D3: Delete `grinderWideStep()` / `grinderWideRpmStep()`

*Why:* they are the same SQL and the same estimator as the widget path, differing only in plumbing.
Two implementations of one number is exactly the drift risk CLAUDE.md's centralization rule
describes, and the comment above `grinderWideStep` already has to *assert* that the two agree —
which is the tell. Pointing `queryGrinderContext()` at the shared map makes agreement structural,
and removes a full scan from every dialing call (34 ms per call at 20×).

*Resolved shape (see R1):* they are collapsed into one static `deriveGrinderSteps(db, modelFilter)`
rather than deleted in favour of an instance member. `queryGrinderContext()` calls it filtered on
its own background connection — its documented threading contract is unchanged — and the store's map
is built from the same function called unfiltered. One derivation, one estimator, two WHERE clauses.
This also means the map is never read off the main thread (R2), so the change adds no threading
question at all.

### D4: Keep the caller-side `1.0` / `50` fallbacks

*Why:* they are spec'd behaviour for a grinder with no derivable history, and they stay correct
under this change. What changes is that they stop being reachable for a grinder that *does* have
history. `GrindRowSource` is untouched.

### D5: Extend the `reportGrindStep()` narration

*Why:* #1713 could not be diagnosed from a 25,720-line log because the value was never written
down, and it cannot be reconstructed afterwards — it depends on the user's own history. The same
argument now applies to *which state answered*: "derived from 28 settings", "held from a previous
derivation", "no derivable history". A log that only ever prints a number cannot distinguish a
correct `1.0` from a broken one, which is the confusion this whole change exists to end. Keep the
existing dedupe.

### D6: Repair the two cache holes rather than route around them

Moving the step into a resident map takes it out of the cache's blast radius, but does not fix the
cache. Four composite key families remain live afterwards — `bean_type:<brand>`,
`eq_grinder_model:<brand>`, `eq_grinder_burrs:<brand>:<model>`, and `grinder_setting:<model>` in
its remaining role as the picker's observed-settings list (subject to T1.2). Both defects still
apply to all of them:

- `requestDistinctCache()` clears every key and refills only its six bare columns → **fix:** snapshot
  the composite keys being invalidated and re-request them, so a key that was answerable before an
  invalidation is answerable after it.
- the discard path (`if (!m_pendingDistinctKeys.remove(cacheKey)) return;`) drops a result with no
  retry and no signal → **fix:** re-issue the fetch, or at minimum emit `distinctCacheReady` so a
  consumer re-asks. Silence is the bug; discarding the stale result itself is correct.

*Why now rather than later:* both live in the two functions this change already rewrites the
neighbours of, and the project rule is to fix pre-existing violations in files you touch rather
than bank them. Deferring also leaves the file in a state where the *reason* the grind step needed
moving is still present and unexplained.

*Severity, stated honestly, since it was understated in an earlier draft of this proposal:* these
are milder than the grind-step bug but not cosmetic. They do not persist until app restart — a
newly opened dialog re-evaluates its bindings, misses, and fetches again. But a fetch discarded
while a dialog is open emits nothing, so that dialog shows an empty suggestion list for its entire
life and the user has to close and reopen it.

*Alternative considered:* leave the cache alone on the grounds that its remaining consumers fail
softly. Rejected — "fails softly" was load-bearing in the earlier draft and turned out to be
half-true, which is exactly the kind of claim that should not be what a deferral rests on.

## Risks / Trade-offs

- **Staleness between invalidations** → the map is rebuilt on every shot save and equipment change,
  which are the only events that can move the step. A step summarising 28 settings does not move on
  one shot; being one invalidation behind is invisible and is strictly better than falling to `1.0`.
- **A future caller reads the map from a worker thread** → R2 establishes the map as main-thread-only
  and the implementation asserts it, so such a caller trips the assertion instead of racing silently.
  This matters because the tooling would not catch it: ASan/UBSan do not flag a benign-looking data
  race, and the nightly Linux job is the only sanitizer net that runs at all.
- **Migration on a large database** → the index build measured 327 ms on 190 MB, on the existing
  background migration path, once. Guarded with `IF NOT EXISTS` like every other index in
  `createTables()`/migrations, so a retried or partially-applied migration is harmless.
- **A grinder whose model string is empty** → `grindStepForGrinder("")` currently derives from the
  full cross-grinder history, and the ShotServer `/beans` form depends on that (a new bag has no
  equipment selected yet). The map must keep an explicit all-grinders entry; dropping it would
  silently regress that form to `1.0`.
- **The map grows with grinder count, not shot count** → 10 grinders today, 31 `(model, setting)`
  pairs. Not a memory concern at any plausible scale.
- **Re-requesting composite keys on every invalidation costs queries** (D6) → invalidation fires on
  shot save and equipment change, not per frame, and each re-request is one indexed or small query.
  Re-request only the keys that were actually resident, so the cost tracks what the session has
  genuinely used rather than every key the app could ever ask for.
- **The discard path's re-issue could loop** if an invalidation storm keeps cancelling fetches →
  bound it: re-issue once and otherwise fall back to emitting `distinctCacheReady`, which returns
  the decision to the consumer instead of retrying underneath it.

## Migration Plan

1. Schema migration **36**: `CREATE INDEX IF NOT EXISTS idx_shots_equipment_grind ON shots(equipment_id, grinder_setting, rpm)`, following the version-bump pattern at `shothistorystorage.cpp:1481-1500` (latest existing version is 35). Also added to `createTables()` so fresh databases get it without migrating.
2. Derivation runs on the existing background pre-warm at store init, and again on each
   `invalidateDistinctCache()` / history change.
3. Readers switch to the map; `grinderWideStep()` / `grinderWideRpmStep()` deleted in the same
   change so no path is left able to disagree.

Rollback: the index is additive and unused by anything else; reverting the code leaves a harmless
extra index behind.

## Resolved Questions

### R1: The db-scoped static derivation IS the one derivation (was: D3's signature fork)

`buildGrinderContextBlock()` takes a `QSqlDatabase&` and calls the **static**
`ShotHistoryStorage::queryGrinderContext(db, …)`. That is not an accident to be refactored away:
`dialing_blocks.h:26-33` states the contract deliberately — these builders run SQL on the caller's
thread and callers must own a background-thread connection. There is no `ShotHistoryStorage`
instance anywhere on the dialing path, and the eight tests in `tests/tst_dialing_blocks.cpp` build a
bare database precisely because none is needed.

So the resolution is the second option, and it is the better one on its merits: **one static
`deriveGrinderSteps(db, modelFilter)`** returning `QHash<foldedModel, {grindStep, rpmStep}>`.

- `queryGrinderContext()` calls it with a model filter and takes the single entry.
- The store's map builder calls it with no filter, on its background thread, and caches the result.

`grinderWideStep()` / `grinderWideRpmStep()` are **collapsed into** that function, not deleted in
favour of an instance member. There is still exactly one derivation and one estimator; what differs
between callers is only the WHERE clause. The AI path keeps its current shape, its current thread
and its current tests, and the widget/web path stops being the only one that can fail.

### R2: No cross-thread mechanism is required (was: snapshot vs mutex)

R1 removes the hazard rather than mitigating it. The map's readers are `GrindRowSource` (QML,
main thread) and the ShotServer grind-candidates handler — and ShotServer is main-thread too
(`QTcpServer::newConnection` is delivered to `this`; its heavy work is explicitly moved to
`QThread::create` lambdas). The map is written from the derivation's completion callback, which is
already marshalled to the main thread. The AI path never reads the map at all — it derives from its
own connection on its own thread.

So: **the map is main-thread-only**, and the correct implementation is to say so and assert it, not
to add a mutex that would imply a sharing that does not happen. A future caller reaching for it from
a worker thread should trip the assertion rather than silently race.

### R3: The observed-settings list stays on the distinct-value cache

`getDistinctGrinderSettingsForGrinder()` returns a *list* whose consumers tolerate an async fill,
and section 6 repairs the cache it depends on. Folding it into the map would make the map hold two
unrelated shapes (steps and value lists) to save a query that is no longer failing. Left where it
is; the cache repair is what makes it correct.

## Open Questions

None outstanding — R1–R3 resolved before implementation began.
