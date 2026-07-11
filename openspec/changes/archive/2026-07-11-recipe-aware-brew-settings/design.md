## Context

`qml/components/BrewDialog.qml` is the global "Brew Settings" dialog (`globalBrewDialog` in `qml/main.qml`), opened from the shot-plan line, StatusBar, home-screen widgets, and the `brewSettings` custom action. Top to bottom it renders: a header (title + Clear/Cancel/OK), a **Profile** row (`SuggestionField profileInput`), a **Beans** row (`BeanSummary` + Change Beans → `ChangeBeansDialog`), then a content column of dial-in fields (Temp Delta, Dose, Cup tare, Ratio, Yield), an **Equipment** row (read-only summary + Switch Equipment), and Grind/RPM dial-in fields.

The recipes feature already has a single activation path: `MainController.activateRecipe(qint64 id)` applies profile + bag + equipment + dose/yield/temp + grind + steam, and sets `Settings.dye.activeRecipeId` last. The active-recipe state is exposed to QML as `Settings.dye.activeRecipeId` (int, `-1` = none), `MainController.activeRecipe` (QVariantMap, empty = none), and `MainController.selectedRecipeId` (optimistic selection). A non-archived, MRU-ordered recipe list is available via `MainController.recipeStorage.requestInventory()` → `onInventoryReady(recipes)`, each entry a map with at least `id` and `name` (the same source `RecipesItem.qml` uses for its pill row).

When a recipe is active, its profile/bag/equipment are fixed by the recipe. The current dialog still lets the user edit Profile/Beans/Equipment independently, which can silently desync the app from the active recipe. This change makes the dialog reflect the recipe as the source of truth in that state.

**Builds on `fix-recipe-grind-integrity` (PR #1472).** That change lands first and does not touch `BrewDialog.qml`, so there is no merge conflict. It does, however, simplify the model this change relies on: grind now always lives on the recipe (the inherit/pin split and `setGrindBagWriteThroughSuspended` are retired; the bag write-through is unconditional), and the brew-override flags (`hasBrewYieldOverride`/`hasTemperatureOverride`) now mean "deliberately differs from the profile default" rather than being latched by every profile load — which makes the dialog's override seeding trustworthy. (This change then goes further on the override model: see Decision 5a, it removes the yield/temp auto-stamp so overrides no longer leak into the recipe.)

## Goals / Non-Goals

**Goals:**
- When a recipe is active, hide the Profile, Beans, and Equipment rows and show a Recipe quick-switch row in the Profile row's place.
- Let the user switch to another recipe from the dialog, re-activating through the existing single activation path and re-seeding the dial-in fields.
- Treat yield/temp as per-brew overrides in recipe mode (as for profiles): they never auto-write to the recipe; only the explicit "Update Recipe" button persists them. This requires removing the two yield/temp recipe auto-stamp watchers in `MainController`.
- Leave the no-recipe dialog byte-for-byte unchanged.
- Reuse `SuggestionField` so the recipe control inherits the app's existing accessibility behavior (inline dropdown vs. modal `SelectionDialog`) with no new branching.

**Non-Goals:**
- No new activation mechanism — reuse `MainController.activateRecipe(id)`.
- No new user-facing settings or new C++ Q_PROPERTYs. (The only C++ change is *removing* the two yield/temp stamp watchers; the recipe list and updates are reached via existing `Q_INVOKABLE`s.)
- No change to dose/grind write-through — those keep stamping the recipe/bag as `fix-recipe-grind-integrity` left them.
- No in-dialog "deactivate recipe / exit recipe mode" affordance in v1 (see Decisions — optional follow-on).
- No change to `brew-overrides` (dial-in commit) or `brew-settings-equipment` (equipment row) behavior in the no-recipe path.

## Decisions

**1. Branch the layout on a single reactive predicate.**
Add `readonly property bool recipeActive: Settings.dye.activeRecipeId >= 0` (equivalently `Object.keys(MainController.activeRecipe).length > 0`). Because `activeRecipeId` is a NOTIFYing property, `recipeActive` re-evaluates live, satisfying the "reactive while open" requirement. Gate the three existing rows with `visible: !root.recipeActive` and `Layout.preferredHeight`/`Layout.maximumHeight` collapse (or wrap each in a `visible`-bound container) so hidden rows take no vertical space. The new Recipe row is `visible: root.recipeActive`.

**2. Recipe row = `SuggestionField`, a 1:1 swap for the Profile row.**
Place the Recipe row in the same slot as the Profile row (label `Recipe:` + `SuggestionField recipeInput`). It reuses the same field the Profile row uses, so with `AccessibilityManager.enabled` off it is the inline type-to-filter dropdown, and with it on the inline overlay is suppressed and a labeled "Open suggestions" button opens the modal `SelectionDialog` — no custom accessibility code. This directly answers the accessibility requirement: no bespoke "if a11y open a dialog" branch is needed; `SuggestionField` already does exactly that.

**3. Recipe list + name↔id resolution.**
On `onAboutToShow` (and on `recipesChanged`), call `MainController.recipeStorage.requestInventory()`; store the delivered `onInventoryReady(recipes)` list in a `property var recipeChoices`. Build the `SuggestionField.suggestions` as the recipe names, and keep a name→id lookup from the same list. Seed `recipeInput.text` from `MainController.activeRecipe.name`. Recipe names can collide; resolve the selection to an id by matching the chosen name against `recipeChoices` and, when ambiguous, prefer an entry whose id differs from the active one. (If duplicate names prove common we can render a differentiator subtitle via `SuggestionField.descriptions`, which already supports a value→subtitle map.)

**4. Switching re-activates through the single path, then re-seeds.**
On an exact-match selection whose resolved id differs from `Settings.dye.activeRecipeId`, call `MainController.activateRecipe(id)`. Activation writes profile/bag/equipment/dose/yield/temp/grind and updates `Settings.dye`. Re-seed the dialog's dial-in fields from the resulting DYE/profile state — reuse the existing `onAboutToShow` seeding logic by factoring it into a `seedFromCurrentState()` function and calling it (a) on show and (b) after a recipe switch (triggered when `activeRecipeId`/`activeRecipe` settles on the new recipe, mirroring how the dialog already reads DYE fields). Selecting the already-active recipe is a no-op guard (`if (id === Settings.dye.activeRecipeId) return`).

**5. OK/Cancel and dial-in editing are untouched.**
The header actions, `ProfileManager.activateBrewWithOverrides(...)` OK path, grind/RPM save to `Settings.dye`, and Cancel/`onRejected` remain as-is. In recipe mode the Profile-restore-on-reject logic (`originalProfileFilename`) is inert because the Profile row is hidden and no profile edit happens in the dialog; keep it guarded so it does nothing when `recipeActive` (avoid restoring a profile the recipe owns).

**5a. Yield/temp are overrides (don't touch the recipe); dose/grind keep their dial write-through.**
The key correction to the earlier draft: **Brew Settings yield and temp are per-brew overrides** — like a profile, editing them must NOT modify the baseline (profile *or* recipe) unless the user explicitly saves. Dose and grind/rpm are *dial-in* values (no override/baseline split, no "Update" button) that continue to write through to the bean/bag (and, for grind post-#1472, stamp the recipe). The user confirmed this split: only yield/temp become override-only; dose/grind keep auto write-through.
  - **Remove two stamp watchers.** `MainController` currently auto-writes overrides into the active recipe via watchers on `SettingsBrew::brewOverridesChanged` → `stampActiveRecipe("yieldG", ...)` and `SettingsBrew::temperatureOverrideChanged` → `stampActiveRecipe("tempOverrideC", ...)` (`maincontroller.cpp:935-940`). These make a one-off Stop-at/Temp tweak silently become the recipe's permanent value on OK. **Delete both watchers** so yield/temp live only in `Settings.brew`. Update `RECIPES.md` (which documents "dose/yield/temp … stamp the active recipe") to drop yield/temp. This is a behavior change to `add-recipes`/#1472 code, intentional and in scope.
  - **Keep dose & grind stamps as-is.** `stampActiveRecipe("doseG", …)` on `dyeBeanWeightChanged` and the grind/rpm stamps on `dyeGrinderSettingChanged`/`dyeGrinderRpmChanged` (non-tea guard, + unconditional bag write-through) stay exactly as #1472 left them. No change.
  - **Activation still applies the recipe's stored yield/temp** as the opening overrides (unchanged): `applyActivatedRecipe` reads `yieldG`/`tempOverrideC` and arms `Settings.brew` when they differ from the profile default. So an activated recipe still opens with its saved yield/temperature; further tweaks just don't write back.
  - Keep the OK handler routing through `activateBrewWithOverrides(...)` + the `Settings.dye` setters. #1472 made `activateBrewWithOverrides` clear yield/temp overrides when they equal the profile default, so the dialog's `Settings.brew.hasBrewYieldOverride`/`hasTemperatureOverride` seeding reads stay trustworthy.
  - `seedFromCurrentState()` must write only local `root.*` QML properties — never `Settings` — so re-seeding after a switch cannot trigger a dose/grind stamp on the just-activated recipe. (Activation is already guarded by `m_applyingRecipe`.)
  - Cup tare (`dyeCupWeight`) and ratio (`Settings.brew.lastUsedRatio`) are not recipe fields; steam/hot-water blocks are recipe fields but not edited here. No handling for any of these.

**5b. Recipe-switch list scope.** *(Edge now handled upstream by #1472.)*
Seed the quick-switch suggestions from the same non-archived MRU inventory `RecipesItem` uses (`recipeStorage.requestInventory()`). This can include profile-less (tea/hot-water) recipes. Switching to such a recipe from this espresso-brew dialog is harmless: `applyActivatedRecipe` skips dose/yield/temp for profile-less recipes, and post-#1472 the grind stamp watcher ignores `drinkType == "tea*"`, so a subsequent dial edit can't turn a tea recipe into one that grinds. v1 keeps the full list for parity with the pill row rather than filtering; revisit only if it proves confusing.

**5c. "Update Profile" → "Update Recipe" on the Temp Delta and Stop-at rows.**
Both rows carry a primary button that today bakes the shown value into the *profile* (Temp Delta: `ProfileManager.applyTemperatureToProfile(temperatureValue)`, BrewDialog.qml:513-526; Stop-at: `profile.target_weight = targetValue` → `uploadProfile` → `saveProfile`, BrewDialog.qml:795-812). In recipe mode this is a footgun — it rewrites the shared profile, affecting every sibling recipe on that profile. So in recipe mode:
  - Relabel each to "Update Recipe" (`brewDialog.updateRecipe` key) and swap the `onClicked` to a direct recipe write: `MainController.recipeStorage.requestUpdateRecipe(Settings.dye.activeRecipeId, {"yieldG": root.targetValue})` and `{"tempOverrideC": root.temperatureValue}` respectively. `requestUpdateRecipe(qint64, QVariantMap)` is `Q_INVOKABLE` (recipestorage.h:207); it writes the field and emits `recipeUpdated`/`recipesChanged`, which `MainController` already handles by refreshing `m_activeRecipe` (same path MCP/composer edits use) — so the in-memory active recipe stays consistent with no new C++.
  - Do **not** touch the profile in recipe mode (no `uploadProfile`/`saveProfile`/`applyTemperatureToProfile`).
  - Store absolute values (`yieldG` in grams, `tempOverrideC` in °C), matching what activation reads; on next activation a value equal to the profile default simply won't arm the override (post-#1472 semantics), which is correct.
  - Relationship to OK: with the yield/temp auto-stamp removed (Decision 5a), OK does **not** write yield/temp to the recipe — those stay per-brew overrides. "Update Recipe" is therefore the **only** way a yield/temp value reaches the recipe, exactly mirroring how "Update Profile" is the only way they reach the profile. Neither OK nor "Update Recipe" writes the profile in recipe mode.
  - Enabled state: keep the existing "differs from profile default" gate for v1 (a deliberate override worth persisting). Optionally tighten to compare against the recipe's current `MainController.activeRecipe.yieldG`/`tempOverrideC` so the button disables once the recipe already holds the shown value — a minor refinement, not required.

**5d. One override-highlight color scheme for all values (cleanup).**
Today the numeric fields mix semantic colors (`weightColor` on Dose/Dose cup/Stop-at, `temperatureColor` on Temp Delta when set, `primaryColor` on Ratio) with state colors (Temp Delta grey at 0; Stop-at blue when `targetManuallySet`). Replace all of it with one rule matching the Shot Plan (`ShotPlanText.qml:276-278` → `_tempOverride ? Theme.highlightColor : Theme.textColor`):
  - `valueColor: <overridden> ? Theme.highlightColor : Theme.textColor` on every editable numeric ValueInput.
  - `<overridden>` per field = "current value ≠ what the Clear handler (BrewDialog.qml:244-259) restores":
    - Temp Delta: `Math.abs(temperatureValue - profileTemperature) > 0.1`
    - Dose: `Math.abs(doseValue - (Settings.dye.dyeBeanWeight > 0 ? Settings.dye.dyeBeanWeight : 18)) > 0.05`
    - Ratio: `Math.abs(ratio - (doseValue > 0 && profileTargetWeight > 0 ? profileTargetWeight / doseValue : ratio)) > 0.05`
    - Stop-at: `Math.abs(targetValue - profileTargetWeight) > 0.1` (drops the `targetManuallySet` distinction)
    - Dose cup: always `Theme.textColor` — Clear never touches `Settings.brew.doseCupTareWeight`.
  - `Theme.highlightColor` == `Theme.warningColor` == `#ffaa00` by default, so overridden numbers visually match the Clear button (the control that reverts them) — reinforcing "amber = Clear will reset this" — and match the Shot Plan's override highlight. Use `highlightColor` (the override semantic), not `warningColor`, so a custom theme that redefines them keeps numbers aligned with the Shot Plan.
  - Unify each ValueInput's `accentColor` (the +/- stepper accent) to a single accent instead of per-type colors — recommended `<overridden> ? Theme.highlightColor : Theme.primaryColor` — so the whole field reads as "holding an override" when lit.
  - Optionally recolor the "Profile: …" sub-indicators to `highlightColor` when their field is overridden (consistency; low priority).
  - This applies in both no-recipe and recipe mode. It does not change Clear's behavior; the invariant "highlighted ⟺ Clear would change it" holds because the highlight condition is defined against Clear's own restore values.

**5e. Shot Plan: highlight the overridden items, not the whole sentence.**
Today `ShotPlanText` colors its entire text via `_color` (`ShotPlanText.qml:276-278`), which flips to `Theme.highlightColor` on a temperature override — so the whole plan turns amber. Change it to per-item highlighting matching Brew Settings (Decision 5d), so only the overridden segment lights up:
  - Base color: drop `_tempOverride` from `_color` — it becomes `mouseArea.pressed ? accentColor : (_isCleaning ? errorColor : Theme.textColor)`. The icon (`iconColor: root._color`) follows the base, so it too stops turning amber.
  - Per-item color in the rich builder: extend the `_build` formatter to `fmt(value, live, overridden)`. The rich `fmt` wraps an overridden live value in a color span — `<font color="#RRGGBB">…</font>` using `Theme.highlightColor` (serialize to a 6-digit hex; strip alpha so Qt StyledText parses it) around the existing `<b>…</b>`. The plain `text` formatter ignores the flag (a11y string unchanged).
  - Pass `overridden` at the two call sites that already know: the temperature segment (`_tempStr`) with `_tempOverride`, and the yield segment (`_yieldStr`) with `_yieldOverride := yieldOverridden && profileYield > 0 && Math.abs(targetWeight - profileYield) > 0.1` (the same condition that already switches `_yieldStr` to the "36.0 → 40.0g" arrow form). All other `fmt(...)` calls stay 2-arg (default false).
  - This means the yield item now highlights too (it currently only shows the arrow). That's safe because it keys off the truthful `hasBrewYieldOverride` flag (post-#1472), not raw dose drift — which is exactly why the old code kept the whole-plan highlight temperature-only.
  - Central win: because the highlight is in the shared component, it applies on **every** `ShotPlanText` consumer, not just the idle page — but only if each consumer feeds the right override inputs. Audit and wire:
    - `ShotPlanItem` (idle): no override props set → defaults read live `Settings.brew` → correct for the live dial. No change.
    - `RecipeDrinkCard` (RecipeDrinkCard.qml:211-232): the reference wiring — sets `profileTemp`/`overrideTemp`/`tempOverridden` and `profileYield`/`targetWeight`/`yieldOverridden` from the recipe. Already correct; inherits per-item coloring for free.
    - `ShotDetailPage` (ShotDetailPage.qml:516) and `PostShotReviewPage` (PostShotReviewPage.qml:1128): the user wants these to **show which values were overridden at shot time** (see the dedicated requirement). They currently leave `tempOverridden`/`yieldOverridden` at their live-`Settings.brew` defaults (a latent bug — a frozen shot can reflect the *current* dial). All the data exists on the snapshot: `shotData.temperatureOverrideC`, `shotData.targetWeightG`, and `profileJson` (the shot-time profile, giving the default temp/yield to compare). Concretely:
      - **Temperature** already lives in the header title as the `(92°C)` parenthetical, which renders only when `temperatureOverrideC > 0` (PostShotReviewPage.qml:944-957; ShotDetailPage has the analogous title). Color just that parenthetical `Theme.highlightColor` (the title is `Text.RichText`, so wrap it in a `<font color>` span) — its presence already means "temp was overridden"; the color makes it read as one.
      - **Yield** shows in the plan snapshot line (`yieldTargetOnly: true`). Wire `profileYield` = the profile-snapshot default target (parse `profileJson`'s `target_weight`), `targetWeight` = `targetWeightG`, `yieldOverridden` = both > 0 && `|targetWeight - profileYield| > 0.1`. Then the per-item scheme highlights the yield when the shot's target deviated. Keep `yieldTargetOnly` (no arrow needed — the highlight is the signal).
      - Source every override input from the snapshot, never live `Settings.brew`; set flags `false` when the snapshot lacks the comparison, so a frozen shot never borrows the live override.
    - `ScreensaverEditorPopup` preview: sample/live preview — leave as-is.

  *Out of scope (flagged):* the compact `CustomItem` brew-settings button fills its whole background amber on an override (`_effectiveBackground` via `_brewOverrideActive`, `CustomItem.qml:48-54`). It is a plain filled button with no separable items, so per-item coloring can't apply. Left unchanged by default; if the whole-fill should also go, that is a small separate follow-on the user can request.

**6. Re-seeding source of truth.**
The dial-in fields already read from `Settings.dye` (`dyeBeanWeight`, `dyeGrinderSetting`, `dyeGrinderRpm`), `Settings.brew` (yield override / ratio), and `ProfileManager` (temp/target). Since recipe activation writes all of these, re-seeding is simply re-running the existing seed logic — no recipe-specific field plumbing.

## Risks / Trade-offs

- **Duplicate recipe names** make a plain name→id lookup ambiguous. Mitigation: prefer a non-active match on switch; optionally add subtitles via `SuggestionField.descriptions`. Acceptable for v1 since recipe names are user-chosen and usually distinct.
- **Async activation timing.** `activateRecipe` is optimistic then confirms asynchronously; re-seeding must key off the settled state, not fire before activation applies. Mitigation: re-seed when `activeRecipeId`/`activeRecipe` reflects the new recipe (event-based, not a timer — per project convention).
- **Row collapse layout.** Hiding rows in a `ColumnLayout` must fully collapse their height (not just `opacity`) so the dialog doesn't leave gaps; verify with the height-cap logic (`_frameWidth`, `mainColumn.implicitHeight`).
- **No exit-recipe-mode control in v1.** A user who wants the Profile/Beans/Equipment rows back must deselect the recipe from the Recipes/idle screen. If this proves awkward, a small "Clear recipe" affordance calling `MainController.deactivateRecipe()` is an easy additive follow-on (the predicate already flips the layout reactively).
- **Accessibility parity.** Reusing `SuggestionField` inherits its a11y behavior, but the surrounding Recipe row (label, spacing) must still follow the project's accessibility rules (`Accessible.ignored` on the label, focusable field); fix any pre-existing violations in the rows touched.
