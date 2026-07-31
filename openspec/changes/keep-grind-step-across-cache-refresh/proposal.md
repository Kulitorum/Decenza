## Why

The grind picker's step for a Niche Zero with 28 distinct observed settings is stuck at `1.0`
instead of the `0.25` the history plainly supports — so the wheel steps in whole numbers and, per
the `_stepDecimals` note added for #1713, a recorded `7.75` reformats to `8` as soon as the picker
renders it.

The data is not wrong. `dialing_get_context` reports `grinderContext.stepSize: 0.25` from the same
rows. The AI path derives it **live** (`grinderWideStep`); the widget and the web endpoint derive
it through the **async distinct-value cache**, and only the cached path fails. The device log names
the failure exactly:

```
t= 96.6  grind step for Zero = 0.25, derived from 28 distinct numeric setting(s)
t=422.3  grind step for Zero = 0,    derived from 0                     <- cache wiped
t=423.9  grind step for Zero = 0.25, derived from 28                    <- recovered
t=644.9  grind step for Zero = 0,    derived from 0                     <- wiped again
         (no further line — the app stayed on the 1.0 fallback)
```

`requestDistinctCache()` clears the whole cache but repopulates only its six bare columns, so the
composite key `grinder_setting:<model>` is dropped by every shot save, shot edit and equipment
change. Recovery depends on a consumer noticing and re-asking; when that re-fetch is discarded
mid-flight (`!m_pendingDistinctKeys.remove(cacheKey)` returns with no notification and no retry)
nothing ever asks again and the fallback becomes permanent for the session. That is the state the
log ends in.

So the bug is not that the cache was refreshed wrongly. **The bug is that there was a cache.** Two
call sites derived the same number two ways, and the cached one had a failure mode the live one
structurally cannot have.

## What Changes

`grindStepForGrinder()` and `grindRpmStepForGrinder()` stop reading `m_distinctCache` and call the
same `grinderWideStep()` / `grinderWideRpmStep()` helpers the AI path already uses, against the
live database. `grinderWideStep()` gains an empty-model branch (pool every grinder) that the widget
needs and the AI path does not, plus an out-param for the sample count the log line reports.

No cache, no invalidation coupling, no cold-cache state to be wrong about, and the widget and the
AI payload are now the same number by construction rather than by two queries happening to agree.

## Cost

Live derivation, measured with a fresh connection per run:

| database | median | worst |
|---|---|---|
| real: 1,124 shots / 18.5 MB | 3.3 ms | 87 ms |
| 16×: 17,984 shots / 157 MB | 37 ms | 41 ms |

It runs on a discrete user action — the grind picker opening — so nothing re-evaluates it in a
loop. Against the ~1 s bar for an inline read on a user action this has a 10–300× margin.

An earlier draft of this change went the other way: it kept a resident map of every grinder's
steps, refreshed on a background thread, plus a covering index behind a new schema version, a
supersession guard and a failed-key set — about 615 lines of production code. It was reverted in
full. The index bought 37 ms → 1.2 ms on a background thread that nothing waits on, and the
machinery that kept the map honest across refreshes is the same class of machinery that produced
the original bug. The guardrails that should have caught it are now in `CLAUDE.md` under
"Complexity has to come with a measurable win" and the main-thread I/O rule.

## Impact

- Affected specs: `grind-step-derivation`
- Affected code: `src/history/shothistorystorage_queries.cpp`
- No schema change, no migration, no new index.
- `tests/shotrowfixtures.h` is extracted so the four test files that had hand-copied `withRawDb`
  share one copy — three of them had silently dropped its open assertion.
