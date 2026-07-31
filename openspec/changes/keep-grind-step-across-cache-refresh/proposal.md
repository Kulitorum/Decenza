## Why

The grind picker's step for a Niche Zero with 28 distinct observed settings is stuck at `1.0`
instead of the `0.25` the history plainly supports — so the wheel steps in whole numbers and, per
the `_stepDecimals` note added for #1713, a recorded `7.75` reformats to `8` as soon as the picker
renders it.

The data is not wrong. `dialing_get_context` reports `grinderContext.stepSize: 0.25` from the same
SQL over the same rows. The AI path derives it **synchronously** (`grinderWideStep`); the widget
and the web endpoint derive it through the **async distinct-value cache**, and only the cached path
fails. The device log names the failure exactly:

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

Underneath the bug sits a design that will not survive growth. The step is derived by a **full
table scan** of `shots` — there is no index on `equipment_id` — so its cost is linear in table
*bytes*, and the bytes are almost entirely `debug_log` / `profile_json` / `steam_json` blobs the
query never reads. Measured on a real 1,123-shot database and on a 20× copy of it (21,776 shots,
190 MB), same machine, warm page cache:

| Query | 1,123 shots | 21,776 shots | + `(equipment_id)` | + covering `(equipment_id, grinder_setting)` |
|---|---|---|---|---|
| One grinder | 1.7 ms | **33.9 ms** (max 702) | 16.5 ms | **1.18 ms** |
| All grinders | 2.0 ms | **40.1 ms** | 21.9 ms | **5.12 ms** |

On a Decent tablet (4–6× slower) the unindexed 20× figure is ~150–200 ms per call, with a tail
already observed near a second on an M2. That is a visible hitch, and it is why "just derive it on
every read" cannot be the answer on its own — quite apart from the project rule against main-thread
DB I/O, which exists because a read contending with an in-flight shot-save transaction waits on
`busy_timeout` for an unbounded time no matter how cheap the query is.

## What Changes

Stop routing the grind step through the general distinct-value cache. The store derives every
grinder's step once, off the main thread, and holds the answer in memory, so every reader gets a
correct value synchronously at the instant of the read.

- **Covering index.** A schema migration SHALL add `shots(equipment_id, grinder_setting, rpm)` so
  the derivation is answered from the index without touching row pages, making its cost independent
  of how large the shot blobs grow.
- **One derivation for all grinders.** The store SHALL derive the grind step and the RPM step for
  every grinder model in a single background query — measured at the same cost as deriving one —
  and hold the result in an in-memory map keyed by folded model name.
- **Synchronous reads.** `grindStepForGrinder(model)` and `grindRpmStepForGrinder(model)` SHALL
  answer from that map. They no longer return `0` because a cache is cold, so the `1.0` / `50`
  caller fallbacks recover their spec'd meaning: this grinder has no derivable history.
- **Recompute on invalidation.** The map SHALL be rebuilt whenever shot history changes, on the
  background path that already runs there.
- **Collapse the duplicate.** `grinderWideStep()` / `grinderWideRpmStep()` — the AI block's
  separate synchronous copy of the same SQL and the same `deriveGrindStep` — SHALL be removed in
  favour of the shared map, so widget, web endpoint and AI payload cannot diverge and the dialing
  path stops paying for a full scan per call.

`deriveGrindStep()` itself is unchanged: same estimator, same smallest-repeated-gap semantics, same
grinder-model-wide scope. The `0.25` is already derivable today — it is being discarded, not
miscalculated.

**And the two cache holes get closed rather than routed around.** Moving the step out of the
distinct-value cache stops it being *this* bug's victim, but the holes stay live under four more
composite key families — `bean_type:<brand>`, `eq_grinder_model:<brand>`,
`eq_grinder_burrs:<brand>:<model>`, and `grinder_setting:<model>` in its remaining role as the
picker's observed-settings list. Their failure is milder than a wrong step but not cosmetic: a
discarded fetch emits no signal, so a suggestion list that comes up empty **stays empty for the
whole life of the open dialog**, and the user must close and reopen to get it back.

- A cache refresh SHALL re-request the composite keys it invalidates, instead of clearing them and
  refilling only the six bare columns.
- A discarded in-flight single-key fetch SHALL leave either a pending re-fetch or a
  `distinctCacheReady` notification behind. No path may leave a key absent with nothing scheduled
  to fill it and no consumer told to re-ask.

No API, schema-visible or setting changes beyond the added index.

## Capabilities

### New Capabilities

- `grind-step-derivation`: how the store derives, holds and serves each grinder's grind and RPM
  step — a single background derivation over an index-covered query, resident in memory, rebuilt on
  invalidation, and read synchronously by every consumer (QML picker, ShotServer web endpoint, AI
  dialing payload).
- `history-distinct-cache`: lifecycle guarantees for the store's async distinct-value cache — what
  an invalidation must preserve, and the invariant that no key may be left absent without either a
  pending fetch or a notification telling consumers to re-ask.

### Modified Capabilities

- `layout-brew-widgets`: the existing requirement says the Grind step falls back to `1.0` "when
  history is too thin to derive". It is silent on a store that *has* the history but has
  momentarily lost it, which is how the fallback became the steady state. Tighten it: the fallback
  covers thin history only, and SHALL NOT be shown while a derivable step exists.
- `dialing-context-payload`: `grinderContext.stepSize` keeps its value and its estimator, but its
  source becomes the shared derivation rather than a private query, so the AI payload and the
  widget are the same number by construction rather than by two implementations agreeing.

## Impact

- `src/history/shothistorystorage.cpp` — schema migration 36 adding the covering index.
- `src/history/shothistorystorage_queries.cpp` — new all-grinder derivation; `grindStepForGrinder()`
  and `grindRpmStepForGrinder()` reworked to read the map; `grinderWideStep()` /
  `grinderWideRpmStep()` removed and `queryGrinderContext()` pointed at the map; `requestDistinctCache()`
  and `requestDistinctValueAsync()` repaired so composite keys survive an invalidation and a
  discarded fetch never ends silently.
- Consumers of the repaired cache that benefit without changing: the bean-type-by-brand,
  grinder-model-by-brand and burrs-by-model suggestion lists in the bag and equipment forms.
- `src/history/shothistorystorage.h` — the map member and its accessors.
- `reportGrindStep()` narration extended to say which state answered, so a submitted log
  distinguishes "derived", "held" and "never derivable". The #1713 line exists precisely because
  this value cannot be reconstructed after the fact.
- Consumers that benefit without changing: `GrindRowSource.grindStep` / `.rpmStep` (brew bar pill,
  Brew Settings, post-shot review, beans dialog, recipe wizard), the ShotServer grind-candidates
  endpoint (`shotserver.cpp:2826`), and `dialing_get_context`.
- Migration cost: one index build, measured at 327 ms on the 190 MB database, run once on the
  existing background migration path.
- Testable without the UI: derive, invalidate, read again, assert the step is still `0.25`; and
  assert the widget-facing and AI-facing values are the same call.
