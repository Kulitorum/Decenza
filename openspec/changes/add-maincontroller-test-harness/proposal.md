## Why

`MainController`'s recipe/dial/bag write-through wiring is some of the most intricate logic in the app — deactivate-on-ingredient-swap, the `m_pendingRecipeSelfWrites` stamp echo-guard, same-id re-activation, and the just-merged Flow-3 grind-refresh ([#1483](https://github.com/Kulitorum/Decenza/pull/1483)) — yet it has **zero automated regression coverage**. It is untestable today: `MainController` creates its `RecipeStorage`/`ShotHistoryStorage`/`CoffeeBagStorage` internally against the real app DB, and its constructor eagerly builds `ProfileManager`/`VisualizerUploader`/`BeanBaseClient`/`AIManager`/`LiveSteamCoach`. So every author who has touched this wiring has skipped the test or tested a static-method proxy that does not exercise the signal flow. This change makes the wiring testable and locks the behavior down.

## What Changes

- Add **test seams** to `MainController` so it can be constructed in isolation:
  - Isolate the internally-created storages from the real app DB via `QStandardPaths` test mode + a temp data dir (confirm the storages resolve their path through `QStandardPaths`; if not, add a minimal DB-path override seam).
  - **Optional constructor injection** of the three storages, defaulting to `nullptr` → create-internally, so **production behavior is unchanged**.
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

- **Code**: `src/controllers/maincontroller.{h,cpp}` (additive constructor param + DB-path seam only); `tests/tst_maincontroller.cpp` (new); `tests/CMakeLists.txt` (new target).
- **Tests**: proxy/fake recipe-wiring tests removed and re-expressed in the harness (net coverage increase; no loss).
- **Build/CI**: one new test executable; runs headless like the existing `tst_*` targets. No production-binary impact.
- **Risk**: low — production construction path is the default (no injection, no test-mode); the seam is inert unless a test opts in.
