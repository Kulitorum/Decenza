## Why

When a recipe is active it already owns the profile, the bean/bag, and the equipment package — those are baked into the recipe. Yet the Brew Settings dialog (`BrewDialog.qml`) still shows editable Profile, Beans, and Equipment rows, so a user can silently pick a profile or bag that contradicts the active recipe, quietly breaking recipe coherence. The dialog should instead reflect that the recipe is the source of truth and give the user one control that matters in recipe mode: quickly switching to a different recipe.

## What Changes

- Make the Brew Settings dialog **recipe-aware**: its layout branches on whether a recipe is currently active (`Settings.dye.activeRecipeId >= 0` / `MainController.activeRecipe`).
- **Recipe active:**
  - Remove the **Profile**, **Beans**, and **Equipment** rows from the dialog.
  - In place of the Profile row at the top, show a **Recipe** row that lets the user quick-switch to another recipe. Selecting a different recipe re-activates it (the standard single activation path) and re-seeds the dialog's dial-in fields from the newly activated recipe/DYE state.
  - Keep the dial-in fields (Temp Delta, Dose, Cup tare, Ratio, Yield, Grind, RPM) editable exactly as today.
  - Retarget the two "Update Profile" buttons (on the Temp Delta and Stop-at rows) to **"Update Recipe"**, persisting the shown value into the active recipe's `tempOverrideC` / `yieldG` instead of baking it into the shared profile (which would leak the tweak into every recipe on that profile).
- **Shot Plan: highlight only the overridden items, not the whole sentence.** Today the entire Shot Plan text turns the highlight color when an override is active. Change `ShotPlanText` to color only the overridden segment(s) — the temperature item on a temperature override, the yield item on a yield override — in `Theme.highlightColor`, leaving the rest (and the icon) the default color. This matches the Brew Settings scheme and flows to every `ShotPlanText` consumer (home widget, recipe/shot cards, shot detail/review). The compact `CustomItem` brew-settings button (a plain filled button with no items) is left as-is.
- **Clean up value coloring to one override-highlight scheme (both modes).** Replace the current mix of per-value-type colors (weight amber, temp red, ratio blue) and manual-vs-calculated state with a single rule: a numeric value is shown in the default text color unless it deviates from its baseline, in which case it uses the override-highlight color (`Theme.highlightColor` — the same amber the Shot Plan and the Clear button use). Invariant: a value is highlighted iff Clear would revert it. (Dose cup, which Clear never touches, is always default-colored.)
- **Treat yield/temp as per-brew overrides in recipe mode, matching profiles.** Editing Stop-at or Temp Delta adjusts *this brew* only; it must not modify the recipe unless the user taps "Update Recipe". This requires **removing the existing auto-stamp** that currently writes `yieldG`/`tempOverrideC` into the active recipe on every override change (`MainController` watchers on `brewOverridesChanged` / `temperatureOverrideChanged`), and updating `RECIPES.md` accordingly. Dose and grind/RPM are dial-in values and keep their existing write-through (bag + recipe) unchanged.
- **No recipe active:** the dialog is **unchanged** — Profile / Beans / Equipment rows and all dial-in fields behave byte-for-byte as before.
- The Recipe control is a `SuggestionField` (a 1:1 structural swap for the Profile `SuggestionField` it replaces). This inherits the existing accessibility behavior for free: with `AccessibilityManager.enabled` off it is an inline type-to-filter dropdown; with it on, the inline overlay is hidden and a labeled "Open suggestions" button opens a modal `SelectionDialog` list.

## Capabilities

### New Capabilities
- `recipe-aware-brew-settings`: How `BrewDialog.qml` branches its layout on the active-recipe state — swapping the Profile row for a Recipe quick-switch control and hiding the Profile/Beans/Equipment rows when a recipe is active, while leaving the no-recipe layout and all dial-in editing unchanged.

### Modified Capabilities
- `recipe-activation`: its "Tweaks write through; ingredient swaps deactivate" requirement currently lists **yield** and **temperature override** among the edits that write through to the active recipe. This change removes them — yield/temp become per-brew overrides that reach the recipe only via "Update Recipe" — and removes the two corresponding `MainController` auto-stamp watchers. (The delta also refreshes the requirement's grind clause to the post-#1472 recipe-owned model.)
- `plan-widgets`: its "Override indicators" requirement highlights the **whole** Shot Plan text on a temperature override. This change narrows the highlight to the individual overridden item(s) — temperature and/or yield — in `Theme.highlightColor`, matching the Brew Settings scheme.

*(No requirement change to `brew-overrides` or `brew-settings-equipment`: this change gates which rows render based on recipe state and does not alter their behavior in the no-recipe path.)*

## Impact

- **QML:** `qml/components/BrewDialog.qml` — conditional rendering of the Profile/Beans/Equipment rows; new Recipe `SuggestionField` row bound to the recipe list; re-seed of dial-in fields on recipe switch; retarget the two "Update Profile" buttons to "Update Recipe" in recipe mode.
- **C++:** `src/controllers/maincontroller.cpp` — remove the two auto-stamp watchers that write `yieldG` / `tempOverrideC` into the active recipe (the only C++ change; the recipe list/update use existing `Q_INVOKABLE`s). Update `docs/CLAUDE_MD/RECIPES.md` to drop yield/temp from the "tweaks stamp the recipe" description.
- **QML:** `qml/components/ShotPlanText.qml` — move the override highlight from the whole-text `_color` to per-item spans in the rich builder (temperature and yield segments). Benefits all `ShotPlanText` consumers. `CustomItem.qml`'s compact brew-settings fill is left unchanged.
- **Recipe data source:** reads `MainController.activeRecipe` / `Settings.dye.activeRecipeId` and the recipe list (via `MainController` / `RecipeStorage`) to populate the quick-switch suggestions and resolve the selected name → recipe id.
- **Activation:** recipe switch routes through the existing single activation path `MainController.activateRecipe(id)` — no new activation logic.
- **No new user-facing settings**, no new C++ properties required beyond what recipe activation already exposes (a read-only recipe-name list may be needed if one is not already reachable from QML).
- **Unchanged paths:** the no-recipe dialog, `ProfileManager.activateBrewWithOverrides(...)` OK path, grind/RPM write-through, and the `brew-overrides` / `brew-settings-equipment` behaviors when no recipe is active.
- **Depends on `fix-recipe-grind-integrity` (PR #1472).** That change merges first and does not modify `BrewDialog.qml` (no conflict), but it is the source of the current grind model this change builds on: grind lives on the recipe with an unconditional bag mirror (no inherit/pin), and the brew-override flags are truthful. Land this change on top of it.
