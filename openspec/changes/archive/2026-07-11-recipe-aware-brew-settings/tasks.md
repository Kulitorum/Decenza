## 1. Recipe-active predicate + dial-in re-seed refactor

- [x] 1.0 Branch from `main` after `fix-recipe-grind-integrity` (PR #1472) has merged, so this work sits on the recipe-owned grind model (no inherit/pin, truthful override flags).
- [x] 1.1 In `qml/components/BrewDialog.qml`, add `readonly property bool recipeActive: Settings.dye.activeRecipeId >= 0` (verify it re-evaluates on `activeRecipeIdChanged`).
- [x] 1.2 Factor the existing `onAboutToShow` dial-in seeding (dose, grind, rpm, profile temp/target, yield, ratio, `targetManuallySet`) into a reusable `function seedFromCurrentState()`; call it from `onAboutToShow`.

## 2. Gate Profile / Beans / Equipment rows

- [x] 2.1 Wrap or bind the Profile row (`profileInput` RowLayout) so it renders only when `!recipeActive`, collapsing its height fully when hidden.
- [x] 2.2 Do the same for the Beans row (`BeanSummary` + Change Beans button + `ChangeBeansDialog`).
- [x] 2.3 Do the same for the Equipment row (read-only summary + info + Switch Equipment button) inside the content column.
- [x] 2.4 Guard the Cancel/`onRejected` profile-restore (`originalProfileFilename`) so it is inert when `recipeActive` (the recipe owns the profile).

## 3. Recipe quick-switch row

- [x] 3.1 Add a Recipe row (label `Recipe:` + `SuggestionField recipeInput`) in the Profile row's slot, `visible: recipeActive`, i18n key `brewDialog.recipeLabel` / `brewDialog.recipe`.
- [x] 3.2 Add `property var recipeChoices: []`; on `onAboutToShow` and on `recipesChanged`, call `MainController.recipeStorage.requestInventory()` and store `onInventoryReady(recipes)` into `recipeChoices`.
- [x] 3.3 Bind `recipeInput.suggestions` to the recipe names from `recipeChoices`; seed `recipeInput.text` from `MainController.activeRecipe.name`.
- [x] 3.4 Implement a name→id resolver against `recipeChoices` (on ambiguity prefer an id ≠ the active one); on exact-match selection where the resolved id ≠ `Settings.dye.activeRecipeId`, call `MainController.activateRecipe(id)`.
- [x] 3.5 After a switch settles on the new recipe (event-based, keyed off `activeRecipeId`/`activeRecipe` — no timer), call `seedFromCurrentState()` to re-seed the dial-in fields.

## 4. Yield/temp become per-brew overrides (stop auto-stamping the recipe)

- [x] 4.1 In `src/controllers/maincontroller.cpp` (~935-940), remove the two auto-stamp watchers: `SettingsBrew::brewOverridesChanged` → `stampActiveRecipe("yieldG", …)` and `SettingsBrew::temperatureOverrideChanged` → `stampActiveRecipe("tempOverrideC", …)`. Leave the dose and grind/rpm stamps intact.
- [x] 4.2 Update `docs/CLAUDE_MD/RECIPES.md` to drop yield/temp from the "dose/yield/temp … stamp the active recipe" description (dose + grind still write through; yield/temp are overrides).
- [x] 4.3 Confirm recipe activation still applies the recipe's stored `yieldG`/`tempOverrideC` as the opening overrides (no change to `applyActivatedRecipe`).

## 5. "Update Profile" → "Update Recipe" on Temp Delta & Stop-at

- [x] 5.1 On the Temp Delta row button (BrewDialog.qml:513-526): when `recipeActive`, set text to `brewDialog.updateRecipe` ("Update Recipe") and route `onClicked` to `MainController.recipeStorage.requestUpdateRecipe(Settings.dye.activeRecipeId, {"tempOverrideC": root.temperatureValue})`; do not call `applyTemperatureToProfile`. When not `recipeActive`, keep the existing "Update Profile" behavior.
- [x] 5.2 On the Stop-at row button (BrewDialog.qml:795-812): when `recipeActive`, set text to "Update Recipe" and route `onClicked` to `MainController.recipeStorage.requestUpdateRecipe(Settings.dye.activeRecipeId, {"yieldG": root.targetValue})`; do not `uploadProfile`/`saveProfile`. When not `recipeActive`, keep existing behavior.
- [x] 5.3 Add the `brewDialog.updateRecipe` translation key + accessibleName variants ("Save temperature to recipe" / "Save stop-at-weight to recipe").
- [x] 5.4 Confirm the profile is untouched in recipe mode (no `uploadProfile`/`saveProfile`/`applyTemperatureToProfile`), and that `m_activeRecipe` refreshes after `requestUpdateRecipe` (the `recipeUpdated` handler).

## 6. One override-highlight color scheme for values

- [x] 6.1 Replace each editable ValueInput's `valueColor` with `<overridden> ? Theme.highlightColor : Theme.textColor`, where `<overridden>` = value differs from what Clear restores: Temp Delta `|temperatureValue-profileTemperature|>0.1`; Dose `|doseValue-(dyeBeanWeight>0?dyeBeanWeight:18)|>0.05`; Ratio `|ratio-(dose>0&&profileTargetWeight>0?profileTargetWeight/doseValue:ratio)|>0.05`; Stop-at `|targetValue-profileTargetWeight|>0.1` (drop the `targetManuallySet` blue/amber logic). Dose cup: always `Theme.textColor`.
- [x] 6.2 Remove the per-value-type value colors (`weightColor`/`temperatureColor`/`primaryColor` on the value text) and unify each field's `accentColor` to a single accent (recommended `<overridden> ? Theme.highlightColor : Theme.primaryColor`).
- [x] 6.3 (Optional) Recolor the "Profile: …" sub-indicators to `Theme.highlightColor` when their field is overridden.
- [x] 6.4 Confirm the scheme applies in both no-recipe and recipe modes and does not change Clear's behavior.

## 7. Shot Plan: per-item override highlight

- [x] 7.1 In `qml/components/ShotPlanText.qml`, drop `_tempOverride` from `_color` (base becomes `mouseArea.pressed ? Theme.accentColor : (_isCleaning ? Theme.errorColor : Theme.textColor)`), so the whole sentence + icon no longer recolor on override.
- [x] 7.2 Extend the `_build` formatter to `fmt(value, live, overridden)`; in the rich formatter, wrap an overridden value in `<font color="#RRGGBB">…</font>` using `Theme.highlightColor` (6-digit hex, alpha stripped) around the existing `<b>`. The plain `text` formatter ignores the flag.
- [x] 7.3 Add `readonly property bool _yieldOverride: yieldOverridden && profileYield > 0 && Math.abs(targetWeight - profileYield) > 0.1`; pass `_tempOverride` at `_tempStr` call sites and `_yieldOverride` at `_yieldStr` call sites; leave all other `fmt(...)` calls 2-arg.
- [x] 7.4 Audit every `ShotPlanText` consumer so the highlight shows everywhere it's used, each driven by its own override state: `ShotPlanItem` (live defaults — no change); `RecipeDrinkCard` (already wires recipe values — verify); `ScreensaverEditorPopup` (preview — leave). Confirm the a11y `text` string is unchanged.
- [x] 7.5 Make `ShotDetailPage` and `PostShotReviewPage` show which values were overridden at shot time, from the snapshot (never live `Settings.brew`):
  - Yield: wire the plan-line `profileYield` (from `profileJson`'s `target_weight`), `targetWeight` (`targetWeightG`), and `yieldOverridden` (`both>0 && |diff|>0.1`) so the yield item highlights when the shot's target deviated.
  - Temperature: color the header title's `(temp)` parenthetical `Theme.highlightColor` when `temperatureOverrideC > 0` (title is `Text.RichText` — wrap in a `<font color>` span). Do the same for `ShotDetailPage`'s equivalent title.
  - Ensure a frozen shot with no recorded override highlights nothing at the top, even while a live override is active.

## 8. Verify unchanged paths

- [x] 8.1 Confirm the no-recipe dialog is behaviorally unchanged (Profile/Beans/Equipment rows present; OK/Cancel/Clear identical; both buttons still read "Update Profile" and bake into the profile) — the only visual change is the override-highlight color scheme, which also applies here.
- [x] 8.2 Confirm OK in recipe mode applies via `ProfileManager.activateBrewWithOverrides(...)` and saves grind/RPM to `Settings.dye` — and that a yield/temp tweak committed with OK does NOT change the active recipe's `yieldG`/`tempOverrideC` (it's an override; re-activating restores the recipe's value).
- [x] 8.3 Confirm the recipe control's accessibility behavior: `AccessibilityManager.enabled` off → inline dropdown; on → labeled "Open suggestions" button opening the modal `SelectionDialog`.
- [x] 8.4 Confirm dose and grind edits in recipe mode still write through as before (dose→`doseG` + bag; grind/rpm→recipe's own `grindPinned`/`rpmPinned` + unconditional bag mirror, per `fix-recipe-grind-integrity`).
- [x] 8.5 Confirm `seedFromCurrentState()` writes only local `root.*` values and never mutates `Settings`, so a recipe switch does not trigger a dose/grind stamp on the newly activated recipe.

## 9. Accessibility + i18n hygiene on touched rows

- [x] 9.1 Ensure the Recipe row label uses `Accessible.ignored: true` and the field carries a meaningful `accessibleName`; fix any pre-existing a11y violations in the rows edited.
- [x] 9.2 Add the new translation keys with English fallbacks via `TranslationManager.translate(...)`.

## 10. Build + verify

- [x] 10.1 Compile via Qt Creator (quick check) and confirm no new `qrc:/…qml` TypeErrors in the app log.
- [x] 10.2 Ask Jeff to launch the app and verify: at defaults all values are white; dialing Temp Delta / Dose / Ratio / Stop-at off-baseline turns each amber, and Clear returns them to white (Dose cup stays white throughout); the Shot Plan highlights only the overridden item(s) (temp and/or yield), not the whole sentence; opening a shot in Shot Review/Detail that was pulled with a temp and/or yield override shows those values highlighted at the top (from the shot, not the live dial), and a no-override shot highlights nothing; activate a recipe → Recipe row shown, Profile/Beans/Equipment hidden; Temp Delta & Stop-at buttons read "Update Recipe" and persist to the recipe (profile untouched); a one-off Stop-at tweak + OK does NOT change the recipe (re-activate restores it); switch recipes in-dialog → dial-in fields re-seed; deactivate → rows return and buttons read "Update Profile".
