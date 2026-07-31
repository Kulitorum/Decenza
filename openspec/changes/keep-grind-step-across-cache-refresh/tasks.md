## 1. Decide the two open questions first

- [x] 1.1 Decide the cross-thread mechanism for the derived map. **Resolved (design R2): none required.** R1 keeps the AI path deriving from its own connection, so the map's only readers are `GrindRowSource` (QML) and the ShotServer handler — both main-thread (`QTcpServer::newConnection` is delivered to `this`; heavy work goes to `QThread::create`). Implement as main-thread-only **with an assertion**, not a mutex: a mutex would imply a sharing that does not happen, and a future worker-thread caller should trip rather than race.
- [x] 1.2 Decide whether `getDistinctGrinderSettingsForGrinder()` is served from the new derivation or left on the cache. **Resolved (design R3): left on the cache.** It returns a list whose consumers tolerate an async fill, section 6 repairs the cache underneath it, and folding it in would make the map hold two unrelated shapes to save a query that is no longer failing.
- [x] 1.3 Decide `DialingBlocks::buildGrinderContextBlock()`'s signature. **Resolved (design R1): the db-scoped static derivation IS the one derivation.** `queryGrinderContext()` is static and db-scoped by documented contract (`dialing_blocks.h:26-33`) with no store instance on that path. One static `deriveGrinderSteps(db, modelFilter)` returning `QHash<foldedModel, {grindStep, rpmStep}>`; the dialing path calls it filtered, the store's map builder calls it unfiltered. `buildGrinderContextBlock()`'s signature is unchanged and the eight tests keep working.

## 2. Schema: covering index

- [x] 2.1 ~~Add the index to `createTables()`~~ **Corrected: it must NOT go there.** `createTables()`'s `CREATE TABLE shots` declares `grinder_setting` but not `equipment_id` or `rpm` — both arrive via `ALTER TABLE` in later migrations — so creating this index there fails with "no such column" on every fresh launch, the trap already documented at `shothistorystorage.cpp:384-387` for `idx_shots_grinder`. A fresh database is stamped version 1 (`shothistorystorage.cpp:395`) and runs every migration, so migration 36 covers fresh and existing databases alike. Nothing to add in `createTables()`.
- [x] 2.2 Add schema migration **36** following the version-bump pattern at `shothistorystorage.cpp:1481-1500` (latest existing version is 35), creating the index and bumping `schema_version` only when it is confirmed present.
- [x] 2.3 Verify the plan: `EXPLAIN QUERY PLAN` for the all-grinder derivation reports `SEARCH ... USING COVERING INDEX`, not `SCAN shots`.

## 3. Derivation: one pass, all grinders, resident in memory

- [x] 3.1 Add the map member (`model -> {grindStep, rpmStep}`, folded-model keys, plus an explicit all-grinders entry) and its accessors to `src/history/shothistorystorage.h`.
- [x] 3.2 Write the single background query that returns every `(model, grinder_setting, rpm)` distinct triple, and derive each model's grind and RPM step from it via the existing `deriveGrindStep()` — estimator unchanged.
- [x] 3.3 Run the pass when the store becomes ready, on the background path that already pre-warms the distinct cache.
- [x] 3.4 Rebuild the map on every history invalidation (`invalidateDistinctCache()` and the shot save / edit / equipment-change paths that call it).
- [x] 3.5 Keep the all-grinders entry populated — `grindStepForGrinder("")` must keep deriving from full cross-grinder history, which the ShotServer `/beans` form depends on (a new bag has no equipment selected).

## 4. Reads become synchronous

- [x] 4.1 Rework `grindStepForGrinder()` to read the map and never touch the distinct-value cache; it returns `0` only for genuinely thin history.
- [x] 4.2 Same for `grindRpmStepForGrinder()`.
- [x] 4.3 Confirm no call path can now issue a DB query on the calling thread from either function.
- [x] 4.4 Leave `GrindRowSource`'s `1.0` / `50` fallbacks untouched — they keep their spec'd meaning and are now only reachable for thin history.

## 5. Collapse the duplicate derivation (per R1)

- [x] 5.1 Replace `grinderWideStep()` / `grinderWideRpmStep()` with one static `deriveGrinderSteps(db, modelFilter)` returning `QHash<foldedModel, {grindStep, rpmStep}>`, sharing the existing `deriveGrindStep()` estimator. An empty filter derives every grinder.
- [x] 5.2 Point `queryGrinderContext()` at it with a model filter; its signature, its thread and its db-scoped contract (`dialing_blocks.h:26-33`) stay exactly as they are. `buildGrinderContextBlock()` is untouched.
- [x] 5.3 Confirm the eight `stepSize` tests in `tests/tst_dialing_blocks.cpp` still pass unchanged — R1 was chosen partly so they would. If any needs editing, stop: that means the derivation's semantics moved, which this change does not intend.
- [x] 5.4 Grep for any remaining second implementation of this derivation and remove it; there must be exactly one.

## 6. Repair the distinct-value cache (D6)

- [x] 6.1 In `requestDistinctCache()`, snapshot the composite keys resident before the clear and re-request them once the refresh completes, so an invalidation never narrows what the cache can answer. Scope to keys that were actually resident — do not speculatively fetch keys nothing has asked for.
- [x] 6.2 In `requestDistinctValueAsync()`, stop the discard path (`if (!m_pendingDistinctKeys.remove(cacheKey)) return;`) ending in silence: re-issue the fetch once, otherwise emit `distinctCacheReady` so consumers re-ask. Keep discarding the stale result itself — that part is correct.
- [x] 6.3 Bound the re-issue so an invalidation storm cannot drive an unbounded retry loop underneath a consumer.
- [x] 6.4 Audit every early `return` in both functions against the invariant "populated, or pending, or consumers notified" — and fix any other path that violates it.

## 7. Logging

- [x] 7.1 Extend `reportGrindStep()` to distinguish **derived** / **held** / **no derivable history**, keeping the sample count and the existing dedupe. Read `docs/CLAUDE_MD/LOGGING.md` first — tier and marker grammar are enforced per PR.
- [x] 7.2 Confirm a submitted log can answer "why did this grinder step in whole numbers" without the reader knowing the user's history.

## 8. Tests

- [x] 8.1 Read `docs/CLAUDE_MD/TESTING.md` before writing any test — the warning rules are strict and a test that emits WARN lines does not ship.
- [x] 8.2 Test: derive a step of `0.25` from 28 distinct settings, invalidate history, read again immediately — assert still `0.25`, never `0`.
- [x] 8.3 Test: a grinder with fewer than two distinct numeric settings returns `0` so the caller's fallback applies.
- [x] 8.4 Test: model lookup folds case and whitespace (`" Zero "` vs `"zero"`), matching the equipment identity folding.
- [x] 8.5 Test: the widget-facing read and the `grinderContext.stepSize` value come from the same derivation and are equal.
- [x] 8.6 Test: an empty model still derives from full cross-grinder history.
- [x] 8.7 Test: migration 36 applied to a pre-change database leaves the index present, and re-running it is harmless.
- [x] 8.8 Test (D6): populate a composite key, invalidate, and assert it is resident or pending afterwards — never silently absent.
- [x] 8.9 Test (D6): the race that produced this bug — start a single-key fetch, land a full refresh while it is in flight, and assert the key ends up populated and `distinctCacheReady` was emitted. This is the regression test for the whole change; write it so it fails against the current code.

## 9. Verify

- [x] 9.1 Ask before building — Qt Creator is shared. Then run the full suite via `mcp__qtcreator__run_tests` (scope `all`); no CI job builds or tests a PR, so this is the gate.
- [x] 9.2 Open the grind picker on the brew bar with the Niche Zero active, pull or edit a shot to force an invalidation, and confirm the wheel still steps by `0.25` and `7.75` is not reformatted to `8`. QML has no test harness — this one is verified by eye.
- [x] 9.3 ~~Force an invalidation while a suggestion-list form is open~~ **Not reachable from the UI — task was mis-scoped.** Triggering an invalidation requires a shot save or an equipment edit, both of which mean leaving the form; there is no way to hold a bag/equipment dropdown open across one by hand. The window is covered instead by `compositeCacheKeySurvivesInvalidation` and `racedFetchStillResolves` (tasks 8.8/8.9), which reach it directly and deterministically. A hand check was never going to be the evidence here.
- [x] 9.4 Check the ShotServer grind-candidates endpoint returns the derived step on a first request after a restart.
- [x] 9.5 Confirm `dialing_get_context` still reports `grinderContext.stepSize: 0.25` after the AI path switched to the shared map.
- [x] 9.6 Import a backup or run a device-to-device transfer and confirm the map rebuilds afterwards. The import path already calls `refreshTotalShots()` + `invalidateDistinctCache()` (`shothistorystorage.cpp:3573`), so hooking 3.4 there should cover it — verify rather than assume, because a stale map after importing thousands of shots is the same "wrong value, no recovery" shape this change exists to remove.
- [x] 9.7 Confirm migration 36 does not disturb `crossedSchemaVersion()`. `maincontroller.cpp:289` fires one-time UI injections on crossing 22 and 25; nothing reads 36, so this is expected to be inert.

## 10. Ship

- [x] 10.1 Open a PR (never push to `main`).
- [ ] 10.2 Run the automated `/pr-review-toolkit:review-pr` review and address findings.
- [ ] 10.3 Archive + spec-sync as the final commit on this PR, not a separate archive-only PR.
