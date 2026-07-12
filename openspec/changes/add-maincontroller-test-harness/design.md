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

### D2: Optional constructor injection, defaulting to internal create

**Decision:** Add trailing optional parameters to the `MainController` constructor (or a dedicated test-only overload guarded appropriately) for the three storages, defaulting to `nullptr`. When null, construct internally exactly as today. This gives tests a second lever (inject temp-DB-backed storages) and makes intent explicit, without changing the production call site.

**Why:** Preserves the shipped path as the default (D1 keeps it isolated), while allowing a test to supply pre-seeded storages when that is simpler than driving inserts through the async API. Trailing-optional-with-nullptr is the least invasive signature change and mirrors the existing `profileStorage = nullptr` parameter already on the constructor.

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

## Open Questions

- Do `RecipeStorage`/`ShotHistoryStorage`/`CoffeeBagStorage` resolve their DB path via `QStandardPaths` (D1 sufficient) or a hard-coded/derived path (needs the D2-sibling seam)? Resolve in step 1.
- Which existing recipe tests, if any, actually qualify as proxies to migrate vs. legitimate storage/model unit tests to keep? Resolve in the step-5 audit.
