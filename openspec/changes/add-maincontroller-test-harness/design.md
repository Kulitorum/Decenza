## Context

`MainController` is the hub for recipe activation and the dial/bag/recipe write-through wiring. Today it is impossible to unit-test because:

- It constructs `RecipeStorage`, `ShotHistoryStorage`, and `CoffeeBagStorage` internally (`maincontroller.cpp` ~L196/206/226), each resolving the shared shot-history DB path — so a test would read/write the real user DB.
- Its constructor eagerly builds `ProfileManager`, `VisualizerUploader`, `BeanBaseClient`, `AIManager`, and `LiveSteamCoach`.

The recipe tests (`tst_recipestorage`, `tst_recipeselectionmodel`, etc.) deliberately exercise `RecipeStorage` **static** methods and the header-only `RecipeSelectionModel` to sidestep this. That leaves the actual `MainController` signal wiring — `recipeUpdated → requestRecipe → recipeReady`, the `m_pendingRecipeSelfWrites` echo-guard, `deactivateRecipe` watchers, same-id re-activation, and the Flow-3 grind refresh from [#1483](https://github.com/Kulitorum/Decenza/pull/1483) — with no coverage.

The DB layer already provides isolation primitives: `withTempDb()` (`src/core/dbutils.h`) and the storages' `SerialDbWorker`. The gap is purely that `MainController` doesn't let a test point those storages at a temp DB.

## Goals / Non-Goals

**Goals:**
- Make `MainController` constructible in a headless test against a throwaway DB.
- Add `tst_maincontroller` that drives the real async recipe-wiring chain and asserts the Flow-3 contract plus the surrounding invariants.
- Migrate proxy/fake recipe-wiring assertions into the harness and delete the proxies.
- Keep the production construction path byte-for-byte behaviorally identical by default.

**Non-Goals:**
- Changing any production recipe-wiring behavior (including the recorded rpm-clear limitation — the test pins it, a fix is a separate change).
- Building a full end-to-end harness for BLE/profile-upload/AI/visualizer paths. Only the recipe/dial/bag wiring is in scope; unrelated collaborators may stay as real inert objects or be left unexercised.
- Refactoring `MainController` into a fully dependency-injected object. Only the minimal seams needed for this coverage.

## Decisions

### D1: DB isolation via `QStandardPaths` test mode first, explicit seam only if needed

**Decision:** In the test's `initTestCase`, call `QStandardPaths::setTestModeEnabled(true)` and set `HOME`/`AppDataLocation` to a `QTemporaryDir`. First verify the three storages resolve their DB path through `QStandardPaths::writableLocation(...)`; if they do, no production code changes for isolation. Only if a storage hard-codes a path do we add a minimal static DB-path override (test-only setter) — the smallest possible seam.

**Why over alternatives:** Test-mode is the idiomatic Qt approach and touches zero production logic. A blanket "inject a DB path everywhere" refactor is larger and riskier. Injecting the storages (D2) also solves it, but test-mode keeps the internal-create path — the one that actually ships — under test, which is more valuable than only ever testing injected doubles.

### D1-confirmed (spike): test mode alone isolates everything

The spike confirmed `MainController` calls `ShotHistoryStorage::initialize()` with no path (defaults to `QStandardPaths::AppDataLocation/shots.db`) and points the other storages at `databasePath()`. `QStandardPaths::setTestModeEnabled(true)` therefore isolates all four with **no production change**. Chosen.

### D2 (dropped): constructor injection is unnecessary

**Decision:** Do **not** add storage-injection parameters. The spike showed test mode isolates the DB, so a test constructs `MainController` normally and pre-seeds recipes by inserting into the same temp DB via `RecipeStorage::insertRecipeStatic`. Injection would add production surface for no benefit.

**Why:** Keeps the change strictly test-only. The originally-proposed injection seam solved a problem (DB isolation) that test mode already solves for free.

### D6: Link the graph via a curated source list, not an app-build refactor

**Decision:** `MainController`'s constructor unconditionally constructs every collaborator, so the test target must link ~150 of the app's 192 sources. Wire this as a **curated source list** on `add_decenza_test(tst_maincontroller ...)`, derived from the root `SOURCES` variable minus `main.cpp` and QML-only pieces. Do **not** refactor the app into a `Decenza_core` OBJECT library unless the curated list proves unmaintainable.

**Why over the OBJECT-library alternative:** the curated list touches **zero** app-build configuration, so the user's day-to-day Qt Creator app build is unaffected and cannot be destabilized by this test work. The OBJECT-library refactor is cleaner in the abstract but reshapes the shared production target (QML module + resource wiring), risking the primary build for a test-only benefit. Trade-off: the curated list must be kept in sync as the app gains sources — acceptable, and a link error flags drift immediately.

### D3: `QTRY_VERIFY`/`QTRY_COMPARE`, never `qWait`

**Decision:** All async assertions poll with `QTRY_*` (default 5s timeout) on the observable end-state (`Settings.dye.dyeGrinderSetting`, `activeRecipeId`, etc.). No fixed `qWait`/sleep anywhere.

**Why:** The wiring hops across the `SerialDbWorker` thread and back via queued signals; timing is nondeterministic. `QTRY_*` waits exactly as long as needed and fails fast on regressions, avoiding both flakiness and wasted wall-clock — and honors the repo's "no timers as guards" ethos in test code too.

### D4: Assert observable settings/state, not private members

**Decision:** Tests assert on public observables (`Settings` values, `activeRecipeId`, emitted signals via `QSignalSpy`) rather than reaching into `m_refreshDialFromRecipeEdit` or `m_pendingRecipeSelfWrites`. The flag/counter behaviors are verified through their effects (no stale push, no loop, no swallowed edit).

**Why:** Keeps tests robust to internal refactors and avoids needing `friend`/`#ifdef DECENZA_TESTING` access. If a specific invariant can only be observed privately, use the existing `friend class ... #ifdef DECENZA_TESTING` pattern rather than widening the public API.

### D5: Proxy-test audit is explicit and subtractive

**Decision:** Enumerate current recipe tests, classify each assertion as "genuine `RecipeStorage`/model unit test" (keep) vs "proxy for `MainController` wiring" (migrate + delete). Land the migration in the same change so coverage never regresses in between.

## Risks / Trade-offs

- **[Storage path is not `QStandardPaths`-derived]** → then D1 alone won't isolate; mitigation is the minimal test-only DB-path override seam (D2's sibling). Confirm before writing tests.
- **[Constructor builds heavy collaborators with side effects]** (network, file I/O) → mitigation: those collaborators must be inert without a connected device/network in a headless run; if any performs I/O on construction, guard it behind the same test-mode/temp-dir isolation or make its creation lazy. Investigate during implementation; expand the seam only as forced.
- **[Async test flakiness]** → mitigated by D3 (`QTRY_*`, generous timeout) and by asserting end-state rather than intermediate ordering.
- **[Injection signature churn]** → mitigated by trailing-optional-nullptr params; the single production call site is unchanged.
- **[Over-scoping into a general MainController harness]** → mitigated by the Non-Goals: only recipe/dial/bag wiring; other subsystems stay unexercised.

## Migration Plan

1. Confirm storage DB-path resolution; add the DB-path override seam only if `QStandardPaths` test mode is insufficient.
2. Add optional storage-injection params (default `nullptr`).
3. Create `tests/tst_maincontroller.cpp` + `tests/CMakeLists.txt` target; get one green async assertion (active-recipe grind edit → dial) end-to-end first to prove the harness.
4. Fill in the remaining scenarios from the spec.
5. Audit + migrate proxy assertions; delete proxies.
6. Build all test targets; confirm no coverage regression.

## Open Questions — resolved by the spike

- ~~Do the storages resolve their DB path via `QStandardPaths`?~~ **Yes** — test mode is sufficient; no seam (D1-confirmed).
- ~~Which existing recipe tests qualify as proxies to migrate?~~ **None** — the audit found every recipe test is a genuine `RecipeStorage`/`RecipeSelectionModel`/generator unit test; coverage is 100% net-new.
- Remaining unknown, to resolve while building: whether any linked source references qrc/QML resources at construction (e.g. a bundled-profile load) such that the headless target needs resource init or a guarded skip. Surface it on the first link+run.
