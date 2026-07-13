## Why

`MainController`'s recipe/dial/bag write-through wiring is some of the most intricate logic in the app — deactivate-on-ingredient-swap, the `m_pendingRecipeSelfWrites` stamp echo-guard, same-id re-activation, and the just-merged Flow-3 grind-refresh ([#1483](https://github.com/Kulitorum/Decenza/pull/1483)) — yet it has **zero automated regression coverage**. It is untestable today: `MainController` creates its `RecipeStorage`/`ShotHistoryStorage`/`CoffeeBagStorage` internally against the real app DB, and its constructor eagerly builds `ProfileManager`/`VisualizerUploader`/`BeanBaseClient`/`AIManager`/`LiveSteamCoach`. So every author who has touched this wiring has skipped the test or tested a static-method proxy that does not exercise the signal flow. This change makes the wiring testable and locks the behavior down.

## What Changes

- **No production C++ changes.** The investigation spike (task 1) confirmed the harness needs no seam:
  - **DB isolation is free**: `MainController` initializes `ShotHistoryStorage` with the default path (`QStandardPaths::AppDataLocation/shots.db`) and points the other storages at it, so `QStandardPaths::setTestModeEnabled(true)` isolates all four against a throwaway DB.
  - **Construction is inert headless**: ShotServer/Mqtt auto-start are off by default, ProfileManager does no disk scan in its ctor, network clients only fire on demand.
  - Constructor injection is therefore unnecessary — a test pre-seeds recipes into the same temp DB via `RecipeStorage::insertRecipeStatic`.
- **The real cost is build wiring, not production code.** `MainController`'s constructor unconditionally constructs every collaborator, so the test target must link ~150 of the app's 192 sources. Wire this via a **curated source list** on the test target (derived from the root `SOURCES` variable, minus `main.cpp`/QML), which leaves the app build untouched. A `Decenza_core` OBJECT library is the fallback only if the list proves unmaintainable.
- Add `tests/tst_maincontroller.cpp` (registered in `tests/CMakeLists.txt`), driving the **real** `recipeUpdated → requestRecipe → recipeReady` chain with `QTRY_VERIFY` (polling, not `qWait`, to avoid flakiness).
- Cover the recipe-wiring contract, at minimum:
  - editing the **active** recipe's grind pushes to `Settings.dye.dyeGrinderSetting`;
  - editing an **inactive** recipe does **not**;
  - **startup restore** does not re-apply grind to the live dial;
  - **relink refresh** and **QML editor-prefill** reads do not push;
  - recipe **switch/deactivate** clears `m_refreshDialFromRecipeEdit`;
  - the dial→recipe stamp **echo-guard does not loop**;
  - **deactivate-on-ingredient-swap** fires on a profile/bag/equipment change.
  - Recorded known-limitation case: an edit that **zeroes `rpmPinned`** does not clear a stale RPM on the dial (parity with `applyActivatedRecipe`) — asserted as current behavior so a future fix is a deliberate, visible change.
- **Audit and migrate proxy tests**: find existing recipe tests whose assertions only stand in for `MainController` signal-wiring behavior (e.g. `RecipeStorage`-static or pure-helper checks used as a substitute for the live wiring), move those assertions into the harness, and delete the proxies.

Non-goal / **not** changing: any production recipe-wiring behavior. The seams are additive and default to today's behavior.

## Capabilities

### New Capabilities
- `maincontroller-test-coverage`: `MainController` exposes test seams (DB isolation + optional storage injection) and a `tst_maincontroller` harness that regression-covers the recipe/dial/bag write-through wiring through the real async signal chain.

### Modified Capabilities
<!-- None. Production behavior is unchanged; the injection seam defaults to the existing internal-create path. -->

## Impact

- **Code**: **none in production** — `src/controllers/maincontroller.{h,cpp}` are untouched (spike confirmed no seam is needed).
- **Tests**: `tests/tst_maincontroller.cpp` (new) + a new target in `tests/CMakeLists.txt` linking ~150 app sources via a curated list. **Proxy-migration is a no-op**: an audit of the recipe tests found **zero** proxies for `MainController` wiring — every existing test is a genuine `RecipeStorage`/`RecipeSelectionModel` unit test. This change is therefore **100% net-new coverage**, not replacement.
- **Build/CI**: one new (heavy) test executable; compiles/links a large fraction of the app, so it is slower than the other `tst_*` targets. Curated-list wiring keeps the app build config unchanged.
- **Risk**: low for production (no production code touched); the cost/risk is concentrated in the build-graph wiring and keeping the source list in sync as the app grows.
