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

**The distinct-value cache is deleted.** `m_distinctCache`, `requestDistinctCache()`,
`requestDistinctValueAsync()`, the pending/dirty/refreshing flags and the `distinctCacheReady()`
signal are gone. Every `getDistinct*()` getter now runs its query through one `queryDistinctList()`
helper against the live database, and the nine `invalidateDistinctCache()` call sites are removed.

The grind step follows the same route: `grindStepForGrinder()` and `grindRpmStepForGrinder()` call
`grinderWideNumericSettings()` / `grinderWideRpmStep()` — the helpers the AI path already used — so
the widget and the AI payload are the same number by construction rather than by two queries
happening to agree.

Four QML surfaces lose the re-evaluation protocol the cache required: the `distinctCacheVersion`
counters in `GrindRowSource` and `PostShotReviewPage`, the `_distinctVersion` counter in
`ChangeBeansDialog`, and `GrindPickerDialog`'s `_cacheConn` handler together with the
`_autoTextPendingHistory` state machine that existed only to promote text mode to the wheel when a
cold cache warmed mid-dialog. With live reads, "no rows at open" means the grinder genuinely has no
history and cannot change while the dialog is open.

## Cost

Measured with a fresh connection per run, on a real 1,124-shot / 18.5 MB database and a 16× copy
(17,984 shots / 157 MB):

| query | real (median / worst) | 16× (median) |
|---|---|---|
| grind-step derivation | 3.3 ms / 87 ms | 37 ms |
| a single distinct column | 0.36–1.9 ms | 1.6–17 ms |
| all six bare columns (one old refresh) | 4.66 ms | 55 ms |

Against the ~1 s bar for an inline read on a user action that is a 10–300× margin.

**The cache was invalidated more often than it was read.** Reads come from seven call sites, all
discrete user actions — opening a dialog or the picker, loading the review page, one HTTP endpoint.
Invalidation fired on every shot save (`refreshTotalShots`), every delete, every metadata edit
(`requestUpdateShotMetadata` — a rating, a note, one taste slider), every import, and three
external web/MCP edit paths. Nudging five taste sliders in the post-shot review meant five full
wipes and five six-query refreshes, to serve dialogs that might never open. It did more work
maintaining itself than it saved.

A correction to an earlier draft of this proposal, kept because it is the lesson: the derivation
was documented as running "when the grind picker opens". It did not. `GrindRowSource` is built
eagerly by `GrindField`, `GrindQuickSelectItem` is a resident bar widget, and both step bindings
also depended on a counter bumped by *every* `distinctCacheReady()` — including each single-key
async fill. The premise the whole cost argument rested on was wrong until the counters were
removed; two independent reviewers caught it.

An earlier draft of this change went the other way: it kept a resident map of every grinder's
steps, refreshed on a background thread, plus a covering index behind a new schema version, a
supersession guard and a failed-key set — about 615 lines of production code. It was reverted in
full. The index bought 37 ms → 1.2 ms on a background thread that nothing waits on, and the
machinery that kept the map honest across refreshes is the same class of machinery that produced
the original bug. The guardrails that should have caught it are now in `CLAUDE.md` under
"Complexity has to come with a measurable win" and the main-thread I/O rule.

## Impact

- Affected specs: `grind-step-derivation`, `history-distinct-cache`
- Affected code: `src/history/shothistorystorage{,_queries}.{h,cpp}`, `src/network/shotserver.cpp`,
  `src/mcp/mcptools_write.cpp`, `qml/components/{GrindRowSource,GrindPickerDialog,ChangeBeansDialog}.qml`,
  `qml/pages/PostShotReviewPage.qml`
- No schema change, no migration, no new index. Net **−207 lines** of production code.
- `tests/shotrowfixtures.h` is extracted so the four test files that had hand-copied `withRawDb`
  share one copy — three of them had silently dropped its open assertion.
