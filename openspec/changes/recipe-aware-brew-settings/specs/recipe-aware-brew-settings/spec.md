## ADDED Requirements

### Requirement: Brew Settings layout branches on active-recipe state

`BrewDialog.qml` SHALL detect whether a recipe is currently active (an active recipe exists when `Settings.dye.activeRecipeId >= 0`, equivalently `MainController.activeRecipe` is non-empty) and choose its layout accordingly. The detection SHALL be reactive: if the active-recipe state changes while the dialog is open (e.g. the user switches recipes from within the dialog), the layout SHALL update without requiring the dialog to be reopened.

When no recipe is active, the dialog SHALL retain its existing layout and behavior — the Profile row, Beans row, and Equipment row are all present, and every dial-in field behaves identically — with the sole exception of the value-color cleanup below (the override-highlight scheme), which applies uniformly in both modes. This change SHALL NOT otherwise alter the behavior of the no-recipe dialog.

#### Scenario: No recipe active — dialog unchanged
- **WHEN** the Brew Settings dialog is opened with no recipe active (`Settings.dye.activeRecipeId < 0`)
- **THEN** the Profile, Beans, and Equipment rows are shown
- **AND** all dial-in fields (Temp Delta, Dose, Cup tare, Ratio, Yield, Grind, RPM) behave exactly as before this change

#### Scenario: Recipe active — dialog switches to recipe mode
- **WHEN** the Brew Settings dialog is opened with a recipe active (`Settings.dye.activeRecipeId >= 0`)
- **THEN** the dialog shows a Recipe row in place of the Profile row
- **AND** the Beans row and Equipment row are not shown

#### Scenario: State change while open is reactive
- **WHEN** the dialog is open in recipe mode and the active recipe is deactivated (from any surface)
- **THEN** the dialog returns to the no-recipe layout (Profile, Beans, Equipment rows) without being reopened

### Requirement: Brew Settings values use a single override-highlight color scheme

Brew Settings SHALL color each editable numeric value by a single rule instead of the current mix of per-value-type semantic colors (weight amber-brown, temperature red, ratio blue) and manual-vs-calculated state: a value SHALL render in the default text color (`Theme.textColor`) when it holds its baseline, and in the override-highlight color (`Theme.highlightColor` — the same highlight the Shot Plan uses for an active override, and the amber of the Clear button) when it deviates from that baseline. The invariant SHALL be: **a value is highlighted if and only if the Clear action would change it.** The per-value-type colors (`weightColor`, `temperatureColor`, `primaryColor` on the value text) and the `targetManuallySet` blue/amber distinction SHALL be removed.

The baseline for each field is the value the Clear handler restores: Temp Delta → the profile temperature (delta 0); Dose → the dose Clear restores (the bean's remembered dose `Settings.dye.dyeBeanWeight`, else 18); Ratio → the profile default ratio (`profileTargetWeight / dose`); Stop-at → the profile target weight. The Dose cup field is NOT reset by Clear, so it SHALL always use the default text color. The scheme SHALL apply uniformly whether or not a recipe is active (Clear's baseline is unchanged by this change), and the "Profile: …" sub-indicators MAY use the same highlight color when their field is overridden for visual consistency.

The +/- stepper accent SHALL be unified to a single accent across all fields rather than per-value-type colors (recommended: the override-highlight color when the field is overridden, otherwise the app accent), so a field reads as a whole when it is holding an override.

#### Scenario: Values at their baseline render in the default color
- **WHEN** Temp Delta is 0°, Dose equals the bean's remembered dose (or 18), Ratio equals the profile ratio, and Stop-at equals the profile target
- **THEN** all four values render in `Theme.textColor` (no highlight)

#### Scenario: An overridden value renders in the highlight color
- **WHEN** the user sets a Temp Delta of +2° (or any Stop-at / Ratio / Dose that differs from its Clear baseline)
- **THEN** that value renders in `Theme.highlightColor`
- **AND** tapping Clear returns it to baseline and its color returns to `Theme.textColor`

#### Scenario: Dose cup is never highlighted
- **WHEN** the Dose cup holds any value (0 or non-zero)
- **THEN** it renders in the default text color, because Clear does not reset it

#### Scenario: Highlight tracks exactly what Clear reverts
- **WHEN** any editable numeric value differs from the value the Clear handler would restore
- **THEN** it is highlighted; and when it equals that value, it is not — with no dependence on value type or on whether the yield was set manually vs. calculated

### Requirement: Shot Review and Shot Detail show which values were overridden at shot time

The top of the Shot Review and Shot Detail pages SHALL indicate which per-brew values (temperature, yield) were overridden **at the time the shot was taken**, using the override-highlight color (`Theme.highlightColor`), derived from the shot's frozen snapshot — never from the current live dial:

- **Temperature:** when the shot recorded a temperature override (`temperatureOverrideC > 0`), the temperature SHALL be shown in the highlight color where the header presents it (the "{profile} ({temp})" title parenthetical, which already appears only when an override was recorded).
- **Yield:** when the shot's target weight deviated from the profile default recorded in the shot's own profile snapshot (`profileJson`), the yield item in the plan snapshot line SHALL be highlighted (per the plan-widgets per-item scheme), sourced from the shot's frozen values.

Values that were not overridden, and every other item, SHALL remain the default color. Dose and grind are recorded dial-in values, not overrides, and SHALL NOT be highlighted here.

#### Scenario: Shot taken with a temperature override
- **WHEN** a shot whose snapshot has `temperatureOverrideC > 0` is opened in Shot Review or Shot Detail
- **THEN** the temperature in the header is shown in `Theme.highlightColor`

#### Scenario: Shot taken with a yield override
- **WHEN** a shot whose recorded target deviated from its profile-snapshot default yield is opened
- **THEN** the yield item in the plan snapshot line is shown in `Theme.highlightColor`

#### Scenario: Shot taken with no overrides
- **WHEN** a shot whose snapshot recorded no temperature or yield override is opened (even while a live override is currently active)
- **THEN** nothing at the top of the page is highlighted

### Requirement: Recipe row replaces the Profile row in recipe mode

In recipe mode, the top row of the dialog SHALL be a Recipe row that replaces the Profile row. The Recipe control SHALL let the user quick-switch the active recipe by choosing from the list of selectable (non-archived) recipes. The control SHALL be presented as a `SuggestionField` seeded with the active recipe's name, mirroring the Profile `SuggestionField` it replaces, so it inherits the app's existing accessibility affordance: with `AccessibilityManager.enabled` off it is an inline type-to-filter dropdown; with it on, the inline overlay is hidden and a labeled "Open suggestions" button opens a modal `SelectionDialog` list.

#### Scenario: Recipe row seeded with the active recipe
- **WHEN** the dialog opens in recipe mode
- **THEN** the Recipe control displays the active recipe's name
- **AND** its suggestion list contains the selectable non-archived recipes

#### Scenario: Accessibility off — inline dropdown
- **WHEN** the dialog is in recipe mode and `AccessibilityManager.enabled` is false
- **THEN** the Recipe control presents the inline type-to-filter dropdown affordance

#### Scenario: Accessibility on — modal selection dialog
- **WHEN** the dialog is in recipe mode and `AccessibilityManager.enabled` is true
- **THEN** the inline dropdown overlay is not shown
- **AND** a labeled "Open suggestions" button is shown that opens a modal `SelectionDialog` list of recipes

### Requirement: Selecting a different recipe re-activates it through the single activation path

When the user picks a recipe from the Recipe control that differs from the currently active recipe, `BrewDialog.qml` SHALL re-activate it by calling the existing single activation path `MainController.activateRecipe(id)` — it SHALL NOT introduce a separate activation mechanism. Because activation applies the recipe's profile, bag, equipment, dose, yield, temperature, and grind, the dialog's dial-in fields SHALL be re-seeded from the resulting DYE/profile state so the editable values reflect the newly activated recipe. Re-selecting the already-active recipe SHALL be a no-op (no redundant re-activation).

#### Scenario: Switching recipes re-activates and re-seeds
- **WHEN** the user selects a recipe in the Recipe control that is not the active one
- **THEN** `MainController.activateRecipe(id)` is called for the chosen recipe
- **AND** the dialog's dial-in fields (Temp Delta, Dose, Ratio, Yield, Grind, RPM) are re-seeded from the newly activated recipe's applied state

#### Scenario: Re-selecting the active recipe is a no-op
- **WHEN** the user selects the recipe that is already active
- **THEN** the recipe is not re-activated and the dial-in fields are not reset

### Requirement: "Update Profile" becomes "Update Recipe" in recipe mode

Today the Temp Delta and Stop-at rows each carry an "Update Profile" button that bakes the shown value into the underlying profile (Temp Delta → `ProfileManager.applyTemperatureToProfile(...)`; Stop-at → `uploadProfile` + `saveProfile` with the new `target_weight`). In recipe mode this would leak a recipe-specific tweak into the shared profile, changing every recipe and use that points at that profile. Therefore, when a recipe is active, each of these two buttons SHALL be labeled "Update Recipe" and SHALL persist the shown value into the **active recipe record** — Temp Delta → the recipe's `tempOverrideC`, Stop-at → the recipe's `yieldG` — via `MainController.recipeStorage.requestUpdateRecipe(Settings.dye.activeRecipeId, {...})`, and SHALL NOT modify the profile. When no recipe is active, both buttons SHALL remain "Update Profile" with their existing behavior unchanged.

The "Update Recipe" button SHALL be the **sole** way a yield/temp change reaches the recipe: yield and temperature are per-brew overrides (see the overrides requirement below), so committing the dialog with OK SHALL NOT write them to the recipe. The button's enabled state MAY continue to gate on the value differing from the profile default (a deliberate override worth persisting into the recipe). The action persists immediately (like "Update Profile" does today) and is independent of OK.

#### Scenario: Stop-at persists to the recipe, not the profile
- **WHEN** a recipe is active and the user changes Stop-at and taps the (now "Update Recipe") button
- **THEN** the value is written to the active recipe's `yieldG` via `requestUpdateRecipe`
- **AND** the profile's `target_weight` is not modified and the profile is not re-saved

#### Scenario: Temp Delta persists to the recipe, not the profile
- **WHEN** a recipe is active and the user changes Temp Delta and taps the (now "Update Recipe") button
- **THEN** the resulting temperature is written to the active recipe's `tempOverrideC` via `requestUpdateRecipe`
- **AND** `applyTemperatureToProfile` is not called and the profile is not modified

#### Scenario: No recipe active — buttons still update the profile
- **WHEN** no recipe is active
- **THEN** both buttons read "Update Profile" and bake the value into the profile exactly as before this change

### Requirement: Dial-in editing and OK/Cancel are unchanged in recipe mode

In recipe mode, the dial-in fields SHALL remain editable and the OK and Cancel actions SHALL behave as they do today: OK commits the dose/yield/temperature/grind values via `ProfileManager.activateBrewWithOverrides(...)` and saves grind/RPM to `Settings.dye`, and Cancel discards the dialog's edits. Removing the Profile/Beans/Equipment rows SHALL NOT change how the dial-in values are applied. Yield and temperature are applied as per-brew overrides only (see the overrides requirement below); OK SHALL NOT persist them to the recipe.

#### Scenario: OK applies values in recipe mode
- **WHEN** the user edits dial-in fields in recipe mode and taps OK
- **THEN** the values are applied via the same `activateBrewWithOverrides(...)` path used in the no-recipe dialog
- **AND** grind setting and RPM are saved to `Settings.dye`
- **AND** the yield and temperature changes apply as overrides in `Settings.brew` and do not modify the active recipe

#### Scenario: Cancel discards edits in recipe mode
- **WHEN** the user edits dial-in fields in recipe mode and taps Cancel
- **THEN** the dialog's dial-in edits are discarded and the active recipe is unchanged by the cancel

### Requirement: Yield and temperature are per-brew overrides, never auto-written to the recipe

Yield (Stop-at) and temperature (Temp Delta) set in Brew Settings are per-brew **overrides**, exactly as they are for a profile: editing them adjusts the next brew relative to the baseline and SHALL NOT modify that baseline. When a recipe is active, a yield/temp change SHALL apply only as an override in `Settings.brew` (persisted per-brew, cleared on profile/recipe switch as today) and SHALL NOT be written into the active recipe. The recipe's `yieldG` / `tempOverrideC` SHALL change only via the explicit "Update Recipe" button (mirroring how the profile's target/temperature change only via "Update Profile").

To honor this, the existing auto-stamp of `yieldG` and `tempOverrideC` onto the active recipe SHALL be removed — specifically the `MainController` write-through watchers on `SettingsBrew::brewOverridesChanged` (→ `yieldG`) and `SettingsBrew::temperatureOverrideChanged` (→ `tempOverrideC`). `RECIPES.md` SHALL be updated to drop yield/temp from the "tweaks stamp the active recipe" description.

On recipe activation, the recipe's stored `yieldG` / `tempOverrideC` SHALL still be applied as the starting overrides (unchanged), so an activated recipe still opens with its saved yield/temperature.

#### Scenario: One-off yield tweak does not change the recipe
- **WHEN** a recipe with `yieldG` = 36 is active, the user sets Stop-at to 40 and taps OK
- **THEN** the brew uses 40 as a `Settings.brew` yield override
- **AND** the active recipe's `yieldG` remains 36
- **AND** re-activating the recipe restores 36

#### Scenario: One-off temperature tweak does not change the recipe
- **WHEN** a recipe is active, the user changes Temp Delta and taps OK
- **THEN** the temperature applies as a `Settings.brew` override for the brew
- **AND** the active recipe's `tempOverrideC` is unchanged

#### Scenario: Update Recipe is the only path yield/temp reach the recipe
- **WHEN** the user wants the shown yield or temperature to persist to the recipe
- **THEN** they tap "Update Recipe", which writes `yieldG` / `tempOverrideC` via `requestUpdateRecipe`; no other Brew Settings action writes those fields

### Requirement: Dose and grind keep their existing dial write-through

Dose and grind/RPM are dial-in values, not overrides: they have no per-brew "override vs. baseline" split and no "Update" button. This change SHALL leave their existing write-through untouched — dose continues to write through to the active bag and stamp the active recipe's `doseG`; grind/RPM continue to write through to the active bag and stamp the recipe's `grindPinned`/`rpmPinned` (per `fix-recipe-grind-integrity`, with the non-tea guard). This change SHALL NOT add or remove any write-back for dose or grind.

The re-seed performed on a recipe switch SHALL write only the dialog's local QML values (`root.*`), never `Settings`, so that re-seeding never triggers a dose/grind stamp into the newly activated recipe.

Cup tare and ratio are NOT recipe-stored (they live in DYE / `Settings.brew` only); steam and hot-water blocks are recipe-stored but are not edited by this dialog. This change SHALL NOT add recipe handling for any of those.

#### Scenario: Editing dose in recipe mode still writes through
- **WHEN** a recipe is active and the user changes the dose and taps OK
- **THEN** the change is applied to `Settings.dye`, writes through to the active bag, and stamps the active recipe's `doseG` — exactly as before this change

#### Scenario: Grind edit still mirrors to the bag and stamps the recipe
- **WHEN** a non-tea recipe is active and the user changes the grind setting (or RPM) and taps OK
- **THEN** the active recipe's `grindPinned` (`rpmPinned`) is stamped and the setter's unconditional bag write-through mirrors the value onto the linked bag — unchanged from `fix-recipe-grind-integrity`

#### Scenario: Re-seed after a switch does not stamp the new recipe
- **WHEN** the user switches recipes in the dialog and the dial-in fields are re-seeded
- **THEN** only local `root.*` values are written
- **AND** no `Settings` mutation occurs from the re-seed, so the newly activated recipe is not stamped with re-seeded dose/grind values
