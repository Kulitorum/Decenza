## Why

Issue [#1533](https://github.com/Kulitorum/Decenza/issues/1533) asks for a recipe (and the brew) to define its yield as a **ratio of the dose** rather than an absolute gram target, so that the stop-at-weight follows the beans actually dosed instead of a number that goes stale the moment the dose drifts.

Auditing the request found the feature is **already half-built, three different ways, and they disagree with each other**. There are three ways the (dose, ratio, yield) triple can change and each does something different:

| Trigger | Site | Behaviour |
|---|---|---|
| Scale auto-captures a dose | `qml/pages/IdlePage.qml:248` | **Recomputes** `yield = dose × lastUsedRatio` — ratio held fixed |
| User taps a ratio preset | `qml/components/RatioPresetDialog.qml:66` | Snapshots `dose × ratio` into an absolute — ratio discarded |
| Dose edited by hand | `src/controllers/profilemanager.cpp:337` | Notify-only — yield pinned, the **displayed ratio silently drifts** |

So the scale path already implements the issue's ask — it just does it against `Settings.brew.lastUsedRatio`, the global "last preset tapped" value whose own code comment (`RatioQuickSelectItem.qml:18-21`, `RatioPresetDialog.qml:40-41`) admits it "can diverge". Weigh 17.5 g instead of 18 and the ratio pill quietly reads `1:2.1` while the stop target stays pinned at 36 g. Worse, that recompute is unconditional: an absolute-yield recipe **already** has its yield stomped by `dose × lastUsedRatio` on every dose capture today.

The root cause is that **no ratio is persisted anywhere**. Ratio is a derived display quantity (`targetWeight ÷ dose`) everywhere except two advisory settings, and "is this an override?" is inferred by float-comparing grams against the profile default in five places. That inference is the second bug: `ProfileManager::brewByRatioActive()` (`profilemanager.cpp:397`) does not mean what its name says — it means "yield differs from profile" — so a 1:2 ratio on an 18 g dose against a 36 g profile derives exactly 36 g, fails the `> 0.1` test, and the ratio silently evaporates precisely when it happens to agree.

This change gives the yield a **stored anchor** — the answer to "which quantity is held constant when the dose moves" — and makes every surface agree on it.

## What Changes

**The model.** Yield stops being a number and becomes a `YieldSpec` — `{value, mode}` where mode is `none` | `absolute` | `ratio`. One value column plus an explicit mode discriminator, so "both an absolute and a ratio" is **structurally unrepresentable** rather than an invariant that code must remember to uphold. It lives in three homes:

- **Recipe** (persisted) — the recipe's design.
- **Bag** (persisted) — the bean's own yield, for brewing without a recipe.
- **Session** (`Settings.brew`, QSettings) — what is armed for the next shot.

**One rule everywhere:** the anchor is whichever of {ratio, yield} was **last written**. Editing a row anchors it; the other is derived through the dose and shown as a consequence. This is not a mode the user selects — it is a property of the last thing they did.

- **Resolution + write-back ladder:** recipe (if active) → bag (if it has an anchor) → profile `target_weight` (absolute only). The same ladder answers "where does the value come from" and "where does Update write to".
- **The scale owns the dose; the recipe/bag owns the anchor.** A dose capture always writes the dose (unchanged) and **never** changes the mode. This is a deliberate behaviour change: absolute-yield recipes stop having their yield stomped on every capture (`IdlePage.qml:248`, `BrewDialog.qml:206`).
- **Ratio survives a profile change; absolute does not.** `resetBrewOverridesForLoadedProfile()` clears an absolute override (36 g was about *that* profile) but keeps a ratio (1:2 is 1:2 on any profile). This delivers the issue's "persistent brew-by-ratio mode" as a consequence of what a ratio *is* — with no new setting.
- **Brew Settings gets a single Update button** that sits on the anchored row and moves when the user edits the other row. Its label follows the ladder: *Update Recipe* → *Update Bag*. Pressing it is the only way an anchor reaches a recipe or bag.
- **Bag dial memory splits on the measurement/intent line.** `grinderSetting`, `rpm`, and `doseWeightG` keep their existing unconditional write-through (they are dial-in — things the user physically did). The yield anchor becomes button-protected, exactly like the recipe's. This means removing **two** auto-writers, not one: the Brew Settings OK write (`profilemanager.cpp:444`) and the per-shot stamp (`maincontroller.cpp:3193-3207`) — cutting only the first would leave the bean still silently learning its yield from every shot.
- **Ratio never reaches `MachineState`.** The anchor resolves to grams before `MachineState::setTargetWeight`, so `WeightProcessor`, all five quality detectors, `shots.yield_override`, the MQTT `target_weight` HA entity, and DYE/Visualizer export are untouched.
- **Shots record intent alongside outcome.** `shots.yield_override` keeps storing resolved grams; two new columns record the anchor that produced it. Promotion then becomes a straight copy — which dissolves the "promote against the frozen dose or the post-shot-corrected dose?" question entirely.
- **Dose is latched at `espressoCycleStarted`**, so a dose write mid-shot can never move the live SAW target through the ungated forwarder at `main.cpp:953`.

**The sweep** (the bulk of the work, and invisible from the issue): a ratio-anchored recipe has no absolute yield, and every site that falls back on `yieldG > 0 ? yieldG : profileYield` silently substitutes the profile's target as the baseline — reintroducing the exact spurious `"36.0 → 40.0g"` override arrow that #1485 fixed. Seven sites need the sweep (`tasks.md` §5), of which four match that literal pattern. Five `qAbs(yield − profileTarget) > 0.1` "is this an override?" inferences are retired by the stored mode.

## Capabilities

### New Capabilities
- `yield-anchor`: the `YieldSpec` type ({value, mode}, mutual exclusivity), the last-written anchor rule, the recipe → bag → profile resolution/write-back ladder, the resolve-to-grams boundary at `MachineState`, the ratio-survives-profile-load asymmetry, the dose latch, per-shot anchor provenance, and promotion-as-copy.

### Modified Capabilities
- `recipe-model`: the Recipe entity's `yield target (g)` becomes a `YieldSpec` (`yield_value` + `yield_mode`), mirroring how `temp_offset_c` already stores a relative quantity resolved at use time.
- `coffee-bag-model`: `yieldOverrideG` becomes a `YieldSpec` and a first-class anchor rather than a profile-relative override; the "Dose/yield-override stamped on shot save" requirement drops its yield half (grind/rpm/dose write-through unchanged).
- `brew-overrides`: the session override becomes `{value, mode}`; `brewByRatioActive()` reads the stored mode instead of float-comparing grams; the ratio widget writes a ratio anchor instead of flattening it; profile load clears absolute overrides but keeps ratio ones.
- `recipe-aware-brew-settings`: one Update button on the anchored row instead of a fixed per-row button; label follows the ladder (*Update Recipe* / *Update Bag*); the highlight scheme anchors on the recipe's own type.
- `recipe-activation`: activation applies the recipe's anchor, and the dose must land before any ratio is resolved (today yield is written synchronously at `maincontroller.cpp:1371` while the dose is queued at `:1356`).

## Impact

- `src/history/recipestorage.h/.cpp` — `Recipe` struct, `kCols`, `ensureTableStatic`
- `src/history/coffeebagstorage.h/.cpp` — `CoffeeBag` struct, `kCols`, `yieldOverrideForTarget` (deleted once its three callers go), `touchesVisualizerFields` (mode column is local-only)
- `src/history/shothistorystorage.cpp` — migration 34 (`recipes`, `coffee_bags`, `shots`); shot save INSERT / read SELECT / device-transfer INSERT
- `src/history/shothistory_types.h`, `src/history/shotprojection.h/.cpp` — per-shot anchor provenance
- `src/history/recipepromotion.cpp` — promotion becomes a copy of the shot's anchor
- `src/core/settings_brew.h/.cpp` — session `{value, mode}`; `clearAllBrewOverrides` split (five callers disagree); ratio bounds enforced; `lastUsedRatio` demoted to preset memory
- `src/core/settings_dye.h/.cpp` — `persistYieldOverrideToBag` becomes button-driven (its call site moves out of `profilemanager.cpp:444`)
- `src/controllers/profilemanager.cpp` — `targetWeight()` → resolve, `brewByRatioActive()` → stored mode, the Bug-A float-compares (`:400`/`:429`/`:478`), the profile-load mode asymmetry (`:466-471` — the early-return branch, not the visible tail), `activateBrewWithOverrides`
- `src/controllers/maincontroller.cpp` — `applyActivatedRecipe` write order, `activeBaselineYieldG`/`yieldIsRealOverride`, the bag-anchor apply guard (`:257`), shot restore (`:788-795`), shot save resolution (`:3036`), **the shot-save bag stamp (`:3193-3207`)**
- `src/main.cpp` — dose latch at `espressoCycleStarted`
- `qml/components/BrewDialog.qml` — seed, baseline, dose watcher, Clear, the single Update button
- `qml/components/RatioPresetDialog.qml`, `qml/components/layout/items/RatioQuickSelectItem.qml` — write a ratio anchor; show override state
- `qml/pages/IdlePage.qml` — dose capture stops flipping the anchor
- `qml/pages/RecipeWizardPage.qml`, `qml/components/ChangeBeansDialog.qml` — three-state yield control
- `qml/components/RecipeDrinkCard.qml`, `qml/components/BagCard.qml`, `qml/components/ShotPlanText.qml`, `qml/components/layout/items/ShotPlanItem.qml` — anchor mark; the `yieldG == 0` sweep
- `qml/pages/ShotDetailPage.qml` — target ratio alongside the achieved `formatRatio`
- `src/mcp/mcptools_recipes.cpp`, `src/mcp/mcptools_write.cpp` — sparse `yieldG`/`yieldRatio`, loud rejection of both-present (mirrors the retired `temperatureOverrideC` precedent)
- `src/mcp/mcptools_control.cpp` — `machine_start_espresso` defaults a missing dose to 0 and passes it through unguarded; a ratio would resolve to a 0 g target
- `src/network/shotserver_recipes.cpp`, `src/network/shotserver_bags.cpp` — web editors gain the anchor control
- `docs/CLAUDE_MD/RECIPES.md`, `docs/CLAUDE_MD/RECIPE_PROFILES.md`, `docs/CLAUDE_MD/SAW_LEARNING.md` — document the anchor model and why SAW is unaffected
- Wiki manual — the ratio anchor, and the absolute-yield behaviour change after a dose capture
- GitHub issue #1533 — closed on merge
