## Why

`MainController`'s recipe/dial/bag write-through wiring is intricate and has produced a string of subtle bugs — Flow-3 (grind not refreshing the Shot Plan), Bug A (coincidental-default overrides), Bug B2 (a re-read racing a write-through), the `m_pendingRecipeSelfWrites` echo-guard, and same-id re-activation. It has **zero regression coverage**, because the logic is tangled into `MainController`, a god-object whose constructor unconditionally builds ~27 collaborators — so a test that constructs it must link ~150 of the app's 192 sources (plus Paho MQTT, WebSockets, and qrc resource bundles). A full-graph test harness was investigated (superseded change `add-maincontroller-test-harness`) and found to be an expensive, slow, brittle band-aid over the real problem.

The codebase already demonstrates the right fix in miniature: `RecipeSelectionModel` is a header-only policy class, unit-tested in isolation, with `MainController` merely wiring Qt signals to it. This change applies that same pattern to the recipe/dial/bag wiring: **extract the logic into a dependency-injected controller that can be unit-tested with fakes**, so we test the real behavior (not a proxy) while linking almost nothing.

## What Changes

- Introduce **`RecipeWiringController`** — a plain C++ class (no Qt event-loop or DB dependency in its core) that owns the recipe-wiring **state** (`activeRecipe` cache, the `refreshDialFromRecipeEdit` flag, the self-write echo-guard counter) and the **decisions** (when to stamp the recipe, when to refresh the dial, when to deactivate, when a re-read is an edit).
- Define narrow **ports** the controller talks to, implemented by `MainController` over the real objects and by **fakes** in tests:
  - a **dial port** (read/write `SettingsDye` grind/rpm/dose/activeBag/activeEquipment/activeRecipeId);
  - a **recipe-store port** (`updateRecipe`, `requestRecipe`, `requestForActivation`; results arrive back as controller callbacks);
  - a **profile port** (current profile title + load, for deactivate-on-swap and activation);
  - a **side-effect port** for the heater-hold re-assert (kept out of the pure core).
- **`MainController` becomes the adapter**: `setupRecipeConnections()` forwards Qt signals to controller methods and implements the ports; `m_activeRecipe`/`m_refreshDialFromRecipeEdit`/`m_pendingRecipeSelfWrites` move into the controller (the `activeRecipe` Q_PROPERTY delegates).
- **Behavior-preserving.** No user-observable change to activation, stamping, dial refresh, or deactivation — this is a structural move, verified by the new unit tests plus a manual pass.
- Add **`tests/tst_recipewiringcontroller.cpp`** covering the wiring contract with synchronous fakes (links only the controller + a couple of value types).
- Establish this as the first application of a **strangler pattern**: future gnarly slices of `MainController` (shot lifecycle, SAW, dialing) get the same treatment opportunistically — not as a crash program.

## Capabilities

### New Capabilities
- `recipe-wiring-controller`: the recipe/dial/bag write-through logic lives in a standalone, dependency-injected `RecipeWiringController` that `MainController` adapts, and is unit-tested with fakes covering the full wiring contract.

### Modified Capabilities
<!-- None. The extraction is behavior-preserving; recipe-activation / recipe-aware-brew-settings requirements are unchanged. -->

## Impact

- **Code**: new `src/controllers/recipewiringcontroller.{h,cpp}` (+ a small ports header); `src/controllers/maincontroller.{h,cpp}` refactored to delegate (moves state + replaces inline lambdas with forwarders). Sensitive, just-shipped code — the change is behavior-preserving and covered by new tests.
- **Tests**: new `tst_recipewiringcontroller` (fast, ~handful of sources). No existing test is replaced — the recipe-test audit found **zero** proxies; this is net-new coverage of previously-untested logic.
- **Build/CI**: one small new test target; no heavy graph, no Paho/WebSockets/qrc needed.
- **Risk**: moderate but contained — the refactor touches live wiring, so it lands behind the new unit tests + a manual verification pass; scope is strictly the recipe/dial/bag cluster, nothing else in `MainController`.
- **Supersedes**: `add-maincontroller-test-harness` (full-graph harness) — removed as part of this change.
