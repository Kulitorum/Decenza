## Context

The recipe/dial/bag write-through wiring currently lives inline in `MainController::setupRecipeConnections()` and a handful of methods (`activateRecipe`, `applyActivatedRecipe`, `deactivateRecipe`, `stampActiveRecipe`, and the `recipeUpdated`/`recipeReady` lambdas). Its state is three `MainController` members: `m_activeRecipe`, `m_refreshDialFromRecipeEdit`, `m_pendingRecipeSelfWrites` (plus the already-extracted `m_recipeSelection` / `RecipeSelectionModel`).

The logic is decision-rich and async: an edit → `requestUpdateRecipe` → `recipeUpdated` (echo-guarded) → `requestRecipe` → `recipeReady` → update cache + refresh dial + maybe deactivate; dial changes → `stampActiveRecipe`; ingredient swaps → deactivate. It cannot be unit-tested because reaching it means constructing the whole god-object.

`RecipeSelectionModel` (header-only, `tst_recipeselectionmodel`) is the proof that extracting a policy object from `MainController` and unit-testing it works cleanly in this codebase. This change extends that pattern to the wiring itself.

## Goals / Non-Goals

**Goals:**
- Move the recipe-wiring **state and decisions** into a `RecipeWiringController` with no Qt-event-loop or DB/Settings concrete dependency in its core.
- Make the controller unit-testable with **synchronous fakes**, testing the real decisions (not a pure-helper proxy).
- Keep app behavior **identical** — a pure structural move.
- Cover the wiring contract, including the Flow-3 refresh and its rpm-clear parity limitation.

**Non-Goals:**
- Changing any user-observable behavior.
- Touching `MainController` beyond the recipe/dial/bag cluster (no shot-lifecycle / BLE / MQTT refactor here).
- Building the full-graph `tst_maincontroller` (explicitly superseded).
- Making the heavy collaborators injectable (that was the harness's problem to solve; the extraction sidesteps it entirely).

## Decisions

### D1: A plain controller behind narrow ports, not a QObject with signals

**Decision:** `RecipeWiringController` is a plain C++ class. Inbound events are **method calls** (`onDialGrindChanged`, `onRecipeUpdated`, `onRecipeReady`, `onIngredientChanged`, `activate`, `deactivate`). Outbound effects go through injected **port interfaces** (pure-virtual structs). No `Q_OBJECT`, no internal `connect`.

**Why:** Keeps the core free of the Qt event loop, so tests drive it synchronously and deterministically — no `QTRY`, no threads, no DB. `MainController` owns the Qt wiring and translates signals ↔ method calls, exactly as it already does for `RecipeSelectionModel`.

### D2: Model the async round-trip as store-port calls + callbacks

**Decision:** The recipe-store port exposes `updateRecipe(id, fields)`, `requestRecipe(id)`, `requestForActivation(id)`. Their asynchronous results re-enter the controller as `onRecipeUpdated(id, ok)` / `onRecipeReady(id, map)` / `onActivationReady(...)`. In production `MainController` wires these to `RecipeStorage`'s real async signals; in tests the fake store invokes the callbacks **synchronously** (immediately or on demand), so the whole edit→re-read→refresh chain is exercised without a DB or event loop.

**Why:** The echo-guard, the edit-refresh flag, and the "re-read racing a write-through" hazards are all about **ordering** of these callbacks. Modeling them as explicit calls lets a test reproduce any ordering (including the adversarial ones) deterministically — something the async harness could only do flakily.

### D3: Ports kept minimal and value-typed

**Decision:** Define the smallest ports that cover the wiring:
- **DialPort**: get/set `grinderSetting`, `rpm`, `beanWeight`; get/set `activeRecipeId`; read `activeBagId`/`activeEquipmentId`; a hook to apply bag/equipment/identity on activation.
- **RecipeStorePort**: `updateRecipe`, `requestRecipe`, `requestForActivation`.
- **ProfilePort**: `currentProfileTitle()`, `loadProfile(...)`.
- **SideEffectPort**: `reassertHeaterHold()` (and any dose/yield/temp application) — keeps steam/heater and profile-upload effects out of the pure core.

Data crosses as `QVariantMap`/`QString`/scalars (the shapes already flowing through `recipeReady`), so no new domain types are required and `MainController`'s existing calls map over directly.

**Why:** Narrow ports are trivial to fake and keep the controller honest about its true dependencies. Value-typed boundaries avoid dragging concrete Qt classes into the core.

### D4: Behavior-preserving extraction, verified two ways

**Decision:** Move logic verbatim (same conditions, same order), only relocating it behind ports. Verify by (a) the new unit tests asserting each contract scenario, and (b) a manual pass in the running app (activate → edit grind → Shot Plan refreshes; startup doesn't re-apply; deactivate-on-swap still fires). No behavioral change is intended or accepted.

**Why:** This touches code that shipped days ago (Flow-3). The safety comes from porting the exact logic and pinning it with tests, not from "improving" it mid-move. Any behavior change is a separate, deliberate follow-up.

### D5: Strangler, starting here

**Decision:** Frame this as the first extraction, not a one-off. The ports/controller/fakes pattern is the template for later `MainController` slices (shot lifecycle, SAW, dialing), done opportunistically when those areas are next touched.

**Why:** A big-bang restructure of a live Qt app is high-risk and unnecessary. Incremental strangling delivers testability where bugs actually cluster, one reviewable PR at a time, without ever breaking the app build.

## Risks / Trade-offs

- **[Refactor of just-shipped, sensitive wiring]** → Mitigation: verbatim port of the logic (D4), new unit tests pinning every scenario, plus a manual verification pass before merge.
- **[Missed edge in the move — e.g. an ordering the inline code handled implicitly]** → Mitigation: D2's deterministic ordering tests specifically target the echo-guard and re-read-vs-write-through hazards; add a test per known past bug (Flow-3, Bug B2 ordering).
- **[Port boundary leaks Qt/DB concerns]** → Mitigation: if a piece genuinely needs the event loop or profile I/O, it stays in `MainController` behind the SideEffectPort rather than contaminating the core.
- **[Scope creep into other MainController logic]** → Mitigation: Non-Goals fence this to the recipe/dial/bag cluster.

## Migration Plan

1. Define the port interfaces + the controller skeleton owning the three state members.
2. Move `stampActiveRecipe` / `recipeUpdated` / `recipeReady` / deactivate / activate logic into the controller behind the ports, verbatim.
3. Reduce `MainController` to signal↔method forwarding + port implementations; delegate the `activeRecipe` property.
4. Write `tst_recipewiringcontroller` covering the full contract with fakes.
5. Build (Qt Creator) + run the new test; manual verification pass in the app.
6. Archive the change.

## Open Questions

- Does `RecipeSelectionModel` fold into `RecipeWiringController`, or stay a separate collaborator the controller composes? Lean: keep separate (it already has its own tests and a clean responsibility); the wiring controller composes/observes it. Confirm during step 1.
- Exact seam for activation's profile/dose/yield/steam application: how much rides the SideEffectPort vs stays inline in `applyActivatedRecipe`. Resolve while moving step 2 — keep the pure core to grind/dial/stamp/refresh/deactivate; leave heavy activation side-effects in `MainController`.
